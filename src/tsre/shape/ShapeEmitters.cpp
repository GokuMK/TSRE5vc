/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/ShapeEmitters.h>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/ExhaustParticles.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/SFileDocument.h>

#include <QCoreApplication>
#include <QHash>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSet>
#include <QVector3D>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ShapeEmitters {

namespace {

// A field's values in turn: from a token stream (ParserX's) or a parsed
// .sd's node (as ShapeLights reads its blocks).
struct Tokens {
    FileBuffer *data;
    QString string() { return ParserX::GetString(data); }
    float number() { return ParserX::GetNumber(data); }
};
struct Scalars {
    const SFileDetail::Node &node;
    int next = 0;
    QString string() { return node.scalar(next++); }
    float number() { return float(node.number(next++)); }
};

// What a block gave: its kind's defaults are taken at the end, the fields
// given overriding them whatever their order.
struct Read {
    QString kind;
    bool flow = false, speed = false, temperature = false, vapour = false, soot = false, beats = false;
    ExhaustOutput given;
    QString badColor;
    // QDirection, Rotation: both given, the later used.
    int direction = 0;
    bool directions = false;
};

void setDirection(Emitter &emitter, const QVector3D &direction) {
    const QVector3D d = direction.normalized();
    emitter.direction[0] = d.x();
    emitter.direction[1] = d.y();
    emitter.direction[2] = d.z();
}

void turned(Read &read, int which) {
    read.directions = read.directions || (read.direction != 0 && read.direction != which);
    read.direction = which;
}

// A block's field, its name read (lower case).
template <class Values>
void field(Emitter &emitter, Read &read, const QString &name, Values &values) {
    if (name == "name") {
        emitter.name = values.string().trimmed();
    } else if (name == "shapehierarchy") {
        emitter.hierarchy = values.string().trimmed();
    } else if (name == "position") {
        for (float &p : emitter.position)
            p = values.number();
    } else if (name == "qdirection") {
        // x y z w, as a W file's: the gas leaves along +y turned by it.
        float q[4];
        for (float &c : q)
            c = values.number();
        QQuaternion rotation(q[3], q[0], q[1], q[2]);
        rotation = rotation.length() > 0.0f ? rotation.normalized() : QQuaternion();
        setDirection(emitter, rotation.rotatedVector(QVector3D(0.0f, 1.0f, 0.0f)));
        turned(read, 1);
    } else if (name == "rotation") {
        // As a light's and a display square's: about x, then y, then z.
        float r[3];
        for (float &c : r)
            c = values.number();
        QMatrix4x4 m;
        m.rotate(r[2], 0.0f, 0.0f, 1.0f);
        m.rotate(r[1], 0.0f, 1.0f, 0.0f);
        m.rotate(r[0], 1.0f, 0.0f, 0.0f);
        setDirection(emitter, m.mapVector(QVector3D(0.0f, 1.0f, 0.0f)));
        turned(read, 2);
    } else if (name == "diameter") {
        const float diameter = values.number();
        if (diameter > 0.0f)
            emitter.diameter = diameter;
    } else if (name == "kind") {
        read.kind = values.string().trimmed().toLower();
    } else if (name == "flow") {
        read.given.flow = std::max(0.0f, values.number());
        read.flow = true;
    } else if (name == "exitspeed") {
        read.given.speed = std::max(0.0f, values.number());
        read.speed = true;
    } else if (name == "temperature") {
        read.given.temperature = values.number();
        read.temperature = true;
    } else if (name == "vapour") {
        read.given.steam = std::clamp(values.number(), 0.0f, 1.0f);
        read.vapour = true;
    } else if (name == "soot") {
        read.given.soot = std::clamp(values.number(), 0.0f, 1.0f);
        read.soot = true;
    } else if (name == "beats") {
        read.given.beats = std::max(0.0f, values.number());
        read.beats = true;
    } else if (name == "colorrgba") {
        // RRGGBBAA, as a light's (a parsed .sd may drop leading zeros):
        // the output's ARGB.
        QString hex = values.string().trimmed();
        if (hex.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
            hex = hex.mid(2);
        bool ok = false;
        const unsigned int value = hex.rightJustified(8, QLatin1Char('0')).toUInt(&ok, 16);
        if (ok && hex.size() <= 8)
            read.given.color = (value >> 8) | ((value & 0xffu) << 24);
        else
            read.badColor = hex;
    } else if (name == "variation") {
        emitter.variation = std::clamp(values.number(), 0.0f, 1.0f);
    } else if (name == "on") {
        emitter.on = values.number() != 0.0f;
    }
}

bool known(const QString &kind) {
    return kind == QLatin1String("exhaust") || kind == QLatin1String("stack")
            || kind == QLatin1String("safetyvalves") || kind == QLatin1String("heating")
            || kind == QLatin1String("steam");
}

// The block read: its kind's values with the fields given.
bool finish(Emitter &emitter, Read &read, QStringList *problems) {
    QStringList found;
    if (read.kind.isEmpty()) {
        read.kind = QStringLiteral("steam");
    } else if (!known(read.kind)) {
        //% "Kind %1 is none of Exhaust, Stack, SafetyValves, Heating, Steam; Steam is used"
        found << qtTrId("shapeemitters.problem.kind").arg(read.kind);
        read.kind = QStringLiteral("steam");
    }
    if (read.directions)
        //% "both QDirection and Rotation; the later is used"
        found << qtTrId("shapeemitters.problem.directions");
    if (!read.badColor.isNull())
        //% "ColorRGBA %1 is not hex RRGGBBAA; the renderer's colour is used"
        found << qtTrId("shapeemitters.problem.color.hex").arg(read.badColor);
    emitter.kind = read.kind;
    emitter.output = testExhaust(QStringList{read.kind}).outputs.front();
    if (read.flow)
        emitter.output.flow = read.given.flow;
    if (read.speed)
        emitter.output.speed = read.given.speed;
    if (read.temperature)
        emitter.output.temperature = read.given.temperature;
    if (read.vapour)
        emitter.output.steam = read.given.steam;
    if (read.soot)
        emitter.output.soot = read.given.soot;
    if (read.beats)
        emitter.output.beats = read.given.beats;
    if (read.given.color != 0)
        emitter.output.color = read.given.color;
    emitter.wetness = exhaustWetness(read.kind);
    if (problems != nullptr)
        for (const QString &problem : found)
            *problems << (emitter.name.isEmpty() ? problem : emitter.name + ": " + problem);
    return true;
}

// The shape's part of this name; -1 for none.
int namedPart(const ComplexShape &shape, const QString &name) {
    for (int part = 0; part < shape.partCount() && !name.isEmpty(); ++part)
        if (shape.partName(part).compare(name, Qt::CaseInsensitive) == 0)
            return part;
    return -1;
}

// The part an emitter hangs on: its ShapeHierarchy's, else the root (the
// first matrix, as the shape's vertices are drawn); -1 without parts.
int partOf(const ComplexShape &shape, const Emitter &emitter) {
    const int named = namedPart(shape, emitter.hierarchy);
    return named >= 0 ? named : (shape.partCount() > 0 ? 0 : -1);
}

// The parts' matrices of a shape's state, taken once a frame (the many
// placed objects of a shape share them).
struct Posed {
    quint64 frame = ~quint64(0);
    std::vector<std::array<float, 16>> matrices;
};

const std::vector<std::array<float, 16>> &posed(const ComplexShape &shape, unsigned int stateId) {
    static QHash<QPair<quintptr, unsigned int>, Posed> cache;
    static quint64 cacheFrame = ~quint64(0);
    const quint64 frame = Renderer::frameNumber();
    if (frame != cacheFrame) {
        cache.clear();
        cacheFrame = frame;
    }
    Posed &p = cache[qMakePair(quintptr(&shape), stateId)];
    if (p.frame != frame) {
        p.frame = frame;
        const QVector<Emitter> &emitters = shape.emitters();
        p.matrices.resize(size_t(emitters.size()));
        for (int i = 0; i < emitters.size(); ++i) {
            const int part = partOf(shape, emitters.at(i));
            if (part < 0 || !shape.partMatrix(stateId, part, p.matrices[size_t(i)].data()))
                Mat4::identity(p.matrices[size_t(i)].data());
        }
    }
    return p.matrices;
}

}

bool readBlock(FileBuffer *data, Emitter &emitter, QStringList *problems) {
    emitter = Emitter();
    Read read;
    Tokens tokens{data};
    QString sh;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        field(emitter, read, sh, tokens);
        ParserX::SkipToken(data);
    }
    return finish(emitter, read, problems);
}

QVector<Emitter> read(const SFileDetail::Node &shape, QStringList *problems) {
    QVector<Emitter> emitters;
    for (const SFileDetail::Node block : shape.children("esd_tsreemitter")) {
        Emitter emitter;
        Read read;
        for (const SFileDetail::Node value : block.children()) {
            Scalars scalars{value};
            field(emitter, read, value.name().toLower(), scalars);
        }
        if (finish(emitter, read, problems))
            emitters.push_back(emitter);
    }
    return emitters;
}

void report(const ComplexShape &shape) {
    static QSet<QString> reported;
    const QString shapePath = shape.getPathId();
    if (!shape.isLoaded() || reported.contains(shapePath))
        return;
    reported.insert(shapePath);
    const QString name = shapePath.section('/', -1);
    auto tell = [&shapePath](const QString &text) {
        ErrorMessagesLib::PushErrorMessage(new ErrorMessage(ErrorMessage::Type_Warning, ErrorMessage::Source_World,
                                                            text, shapePath));
    };
    for (const QString &problem : shape.emitterProblems())
        //% "Shape %1: an emitter (ESD_TSREEmitter): %2."
        tell(qtTrId("shapeemitters.shape.problem").arg(name, problem));
    for (const Emitter &emitter : shape.emitters())
        if (!emitter.hierarchy.isEmpty() && namedPart(shape, emitter.hierarchy) < 0)
            //% "Shape %1: its emitter's ShapeHierarchy %2 is not one of the shape's matrices; the emitter hangs on its root."
            tell(qtTrId("shapeemitters.shape.hierarchy").arg(name, emitter.hierarchy));
}

void giveOut(const ComplexShape &shape, unsigned int stateId, const float *model, const float *camera,
             const void *owner, quint32 uid) {
    ExhaustParticles &particles = ExhaustParticles::instance();
    const QVector<Emitter> &emitters = shape.emitters();
    if (emitters.isEmpty() || !particles.acceptsGas() || !shape.isLoaded())
        return;
    const float dx = model[12] - camera[0], dy = model[13] - camera[1], dz = model[14] - camera[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance > Distance)
        return;
    report(shape);
    const std::vector<std::array<float, 16>> &parts = posed(shape, stateId);
    // Its Variation's place: the same each frame, a different one for each
    // object (its UiD's hash, -1 to 1).
    const float spread = float((quint64(uid) * 2654435761ull >> 8) & 0xffffu) / 65535.0f * 2.0f - 1.0f;
    for (int i = 0; i < emitters.size(); ++i) {
        const Emitter &e = emitters.at(i);
        if (!e.on || size_t(i) >= parts.size())
            continue;
        float placed[16];
        Mat4::multiply(placed, const_cast<float *>(model), const_cast<float *>(parts[size_t(i)].data()));
        float at[3], way[3];
        for (int r = 0; r < 3; ++r) {
            at[r] = placed[r] * e.position[0] + placed[4 + r] * e.position[1] + placed[8 + r] * e.position[2]
                    + placed[12 + r];
            way[r] = placed[r] * e.direction[0] + placed[4 + r] * e.direction[1] + placed[8 + r] * e.direction[2];
        }
        const float length = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);
        if (length > 0.0f)
            for (float &w : way)
                w /= length;
        ExhaustOutput out = e.output;
        out.flow *= std::max(0.0f, 1.0f + e.variation * spread);
        particles.emitGas(owner, i, at, way, e.diameter, out, 1.0f, e.wetness, ExhaustParticles::Pool::Scenery,
                          distance);
    }
}

}
