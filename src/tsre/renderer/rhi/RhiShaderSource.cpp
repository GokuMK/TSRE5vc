/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "RhiShaderSource.h"
#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <algorithm>

namespace RhiShaderSource {

const QHash<QString, int> &attributeLocations() {
    static const QHash<QString, int> locations = {
        {"vertex", 0}, {"aTextureCoord", 1}, {"normal", 2}, {"alpha", 3},
        {"tangent", 4}, {"aTextureCoord1", 5}, {"vertexColor", 6},
        {"instanceColumn0", 8}, {"instanceColumn1", 9},
        {"instanceColumn2", 10}, {"instanceColumn3", 11}};
    return locations;
}

const QHash<QString, int> &samplerBindings() {
    // The units GLUU::initShader assigns; within one program variant they
    // are unique.
    static const QHash<QString, int> bindings = {
        {"uSampler", 0}, {"uSampler2", 1}, {"shadow1", 2}, {"shadow2", 3},
        {"terrainMaterialMap", 4}, {"terrainMaterialTextures", 5},
        {"terrainMaterialDetails", 6}, {"terrainMaterialParams", 7},
        {"shadow0", 9}, {"environmentMap", 10},
        {"pbrMetallicRoughnessMap", 11}, {"pbrNormalMap", 12}, {"pbrOcclusionMap", 13},
        {"pbrEmissiveMap", 14}, {"pbrClearcoatMap", 4}, {"pbrClearcoatRoughnessMap", 5},
        {"pbrClearcoatNormalMap", 6}, {"pbrSpecularMap", 7}, {"pbrSpecularColorMap", 15},
        {"pbrSceneColor", 1}, {"pbrTransmissionMap", 16}, {"pbrThicknessMap", 17},
        {"waterBottomMap", 4}, {"waterMiddleMap", 5}, {"waterReflectionMap", 6},
        {"waterNormalMap", 15}, {"uSampler4", 18}, {"environmentSource", 0},
        {"localLightData", 22}, {"localLightCells", 23}, {"localLightIndices", 24}};
    return bindings;
}

namespace {

// Whether a directive's condition holds: "defined(X)", "!defined(X)" or a
// bare name for #ifdef / #ifndef.
bool term(const QString &expression, const QSet<QString> &defines) {
    static const QRegularExpression defined(R"(^\s*(!?)\s*defined\s*\(\s*(\w+)\s*\)\s*$)");
    const QRegularExpressionMatch match = defined.match(expression);
    if (match.hasMatch())
        return defines.contains(match.captured(2)) != (match.captured(1) == "!");
    return defines.contains(expression.trimmed());
}

// "defined(A) && !defined(B) || defined(C)": && binds tighter than ||.
bool condition(const QString &expression, const QSet<QString> &defines) {
    for (const QString &alternative : expression.split("||")) {
        bool all = true;
        for (const QString &part : alternative.split("&&"))
            all = all && term(part, defines);
        if (all)
            return true;
    }
    return false;
}

int locationSlots(const QString &type) {
    if (type == "mat4")
        return 4;
    if (type == "mat3")
        return 3;
    if (type == "mat2")
        return 2;
    return 1;
}

struct Declaration {
    QString type;
    QString name;
    QString array;
};

}

QByteArray preprocess(const QByteArray &source, const QSet<QString> &defines) {
    // Each level: whether its current branch is active, whether a branch of
    // it was taken, and whether the enclosing level is active.
    struct Level { bool active; bool taken; bool parent; };
    QList<Level> stack;
    bool active = true;
    QByteArray out;
    static const QRegularExpression directive(R"(^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b\s*(.*)$)");
    for (const QByteArray &rawLine : source.split('\n')) {
        const QString line = QString::fromUtf8(rawLine);
        const QRegularExpressionMatch match = directive.match(line);
        if (match.hasMatch()) {
            const QString kind = match.captured(1);
            QString expression = match.captured(2);
            const int comment = expression.indexOf("//");
            if (comment >= 0)
                expression = expression.left(comment);
            if (kind == "ifdef" || kind == "ifndef" || kind == "if") {
                bool holds = kind == "if" ? condition(expression, defines)
                        : defines.contains(expression.trimmed()) == (kind == "ifdef");
                stack.push_back({active && holds, holds, active});
                active = active && holds;
            } else if (kind == "elif" && !stack.isEmpty()) {
                Level &level = stack.last();
                const bool holds = !level.taken && condition(expression, defines);
                level.active = level.parent && holds;
                level.taken = level.taken || holds;
                active = level.active;
            } else if (kind == "else" && !stack.isEmpty()) {
                Level &level = stack.last();
                level.active = level.parent && !level.taken;
                level.taken = true;
                active = level.active;
            } else if (kind == "endif" && !stack.isEmpty()) {
                active = stack.last().parent;
                stack.removeLast();
            }
            continue;
        }
        if (active) {
            out += rawLine;
            out += '\n';
        }
    }
    return out;
}

Program convert(const QByteArray &vertexSource, const QByteArray &fragmentSource,
                const QStringList &defineList) {
    Program program;
    QSet<QString> defines(defineList.begin(), defineList.end());
    defines.insert("TSRE_RHI");
    const QByteArray stages[2] = {preprocess(vertexSource, defines),
                                  preprocess(fragmentSource, defines)};

    static const QRegularExpression uniformLine(
            R"(^\s*uniform\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+(\w+)\s*(\[[^\]]*\])?\s*;\s*(//.*)?$)");
    static const QRegularExpression blockLine(R"(^\s*layout\s*\(\s*std140\s*\)\s*uniform\s+(\w+)\s*\{?\s*$)");
    static const QRegularExpression interfaceLine(
            R"(^\s*((?:(?:flat|smooth|noperspective)\s+)*)(in|out)\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+(\w+)\s*(\[[^\]]*\])?\s*;\s*(//.*)?$)");

    // Loose uniforms of both stages, one block for the program; varyings by
    // name, so both stages give a varying the same location.
    QMap<QString, Declaration> uniforms;
    QMap<QString, Declaration> varyings;
    QSet<QString> vertexOutputs;
    for (int stage = 0; stage < 2; ++stage) {
        int depth = 0;
        for (const QByteArray &rawLine : stages[stage].split('\n')) {
            const QString line = QString::fromUtf8(rawLine);
            // Only declarations at global scope.
            if (depth == 0) {
                const QRegularExpressionMatch u = uniformLine.match(line);
                if (u.hasMatch() && !u.captured(1).startsWith("sampler")) {
                    const Declaration declaration{u.captured(1), u.captured(2), u.captured(3)};
                    auto found = uniforms.constFind(declaration.name);
                    if (found != uniforms.constEnd()
                            && (found->type != declaration.type || found->array != declaration.array))
                        program.error += QString("uniform %1 declared as %2 and %3\n")
                                .arg(declaration.name, found->type, declaration.type);
                    uniforms.insert(declaration.name, declaration);
                }
                const QRegularExpressionMatch v = interfaceLine.match(line);
                if (v.hasMatch()) {
                    const bool varying = (stage == 0 && v.captured(2) == "out")
                            || (stage == 1 && v.captured(2) == "in");
                    if (stage == 0 && varying)
                        vertexOutputs.insert(v.captured(4));
                    // Inputs the vertex stage does not write take no location.
                    if (varying && (stage == 0 || vertexOutputs.contains(v.captured(4))))
                        varyings.insert(v.captured(4), {v.captured(3), v.captured(4), v.captured(5)});
                }
            }
            depth += line.count('{') - line.count('}');
        }
    }
    QHash<QString, int> varyingLocations;
    int nextLocation = 0;
    for (const Declaration &varying : varyings) {
        varyingLocations.insert(varying.name, nextLocation);
        nextLocation += locationSlots(varying.type);
    }
    QByteArray block = "layout(std140, binding = " + QByteArray::number(UniformBlockBinding)
            + ") uniform TsreUniforms {\n";
    for (const Declaration &uniform : uniforms)
        block += "    " + uniform.type.toUtf8() + " " + uniform.name.toUtf8()
                + uniform.array.toUtf8() + ";\n";
    block += "};\n";
    if (uniforms.isEmpty())
        block.clear();

    const QHash<QString, int> &attributes = attributeLocations();
    const QHash<QString, int> &samplers = samplerBindings();
    for (int stage = 0; stage < 2; ++stage) {
        QByteArray out;
        bool headerDone = false;
        int depth = 0;
        for (const QByteArray &rawLine : stages[stage].split('\n')) {
            QString line = QString::fromUtf8(rawLine);
            if (!headerDone && line.trimmed().startsWith("#version")) {
                out += "#version 440\n";
                for (const QString &define : std::as_const(defines))
                    out += "#define " + define.toUtf8() + " 1\n";
                out += block;
                headerDone = true;
                continue;
            }
            if (depth == 0) {
                const QRegularExpressionMatch u = uniformLine.match(line);
                if (u.hasMatch()) {
                    const QString type = u.captured(1);
                    if (!type.startsWith("sampler"))
                        continue;
                    if (!samplers.contains(u.captured(2))) {
                        program.error += "sampler without a binding: " + u.captured(2) + "\n";
                        continue;
                    }
                    line = QString("layout(binding = %1) uniform %2 %3;")
                            .arg(samplers.value(u.captured(2))).arg(type, u.captured(2));
                } else if (blockLine.match(line).hasMatch()) {
                    const QString name = blockLine.match(line).captured(1);
                    if (name != "TerrainPatchBlock")
                        program.error += "uniform block without a binding: " + name + "\n";
                    line.replace(QRegularExpression(R"(layout\s*\(\s*std140\s*\))"),
                                 QString("layout(std140, binding = %1)").arg(TerrainPatchBlockBinding));
                } else {
                    const QRegularExpressionMatch v = interfaceLine.match(line);
                    if (v.hasMatch()) {
                        const QString direction = v.captured(2);
                        const QString name = v.captured(4);
                        int location = -1;
                        if (stage == 0 && direction == "in") {
                            if (!attributes.contains(name))
                                program.error += "attribute without a location: " + name + "\n";
                            location = attributes.value(name, -1);
                        } else if (stage == 1 && direction == "out") {
                            location = 0;
                        } else if (stage == 1 && !vertexOutputs.contains(name)) {
                            // An input the vertex stage does not write: OpenGL
                            // leaves it undefined, Vulkan rejects it. Keep the
                            // name as a private zero.
                            line = v.captured(5).isEmpty()
                                    ? QString("%1 %2 = %1(0);").arg(v.captured(3), name)
                                    : QString("%1 %2%3;").arg(v.captured(3), name, v.captured(5));
                        } else {
                            location = varyingLocations.value(name, -1);
                        }
                        if (location >= 0)
                            line = QString("layout(location = %1) ").arg(location) + line.trimmed();
                    }
                }
            }
            depth += line.count('{') - line.count('}');
            line.replace("gl_VertexID", "gl_VertexIndex");
            line.replace("gl_InstanceID", "gl_InstanceIndex");
            out += line.toUtf8();
            out += '\n';
        }
        if (!headerDone)
            program.error += "no #version line\n";
        (stage == 0 ? program.vertex : program.fragment) = out;
    }
    return program;
}

}
