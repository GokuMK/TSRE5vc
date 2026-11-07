/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/ShapeLights.h>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/Game.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/DiscMesh.h>
#include <tsre/renderer/RenderContext.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/SFileDocument.h>

#include <QCoreApplication>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSet>
#include <QVector3D>
#include <algorithm>
#include <cmath>

namespace ShapeLights {

namespace {

constexpr float Degrees = float(M_PI / 180.0);

// A field's values in turn: from a token stream (ParserX's) or a parsed
// .sd's node (as ShapeDisplays reads its blocks).
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

// What a block gave so far: the pairs of which the later wins.
struct Read {
    enum class Given { None, First, Second };
    Given color = Given::None;
    Given direction = Given::None;
    bool colors = false;
    bool directions = false;
    bool intensity = false;
    QString unknownType;
    // A ColorRGBA that is not hex (white used).
    QString badColor;
    // ColorRGBA's alpha, taken into the intensity at the end.
    float alpha = 1.0f;
    float cone[2] = {0.0f, 45.0f};
};

void setDirection(Light &light, const QVector3D &direction) {
    const QVector3D d = direction.normalized();
    light.direction[0] = d.x();
    light.direction[1] = d.y();
    light.direction[2] = d.z();
}

void given(Read::Given &which, Read::Given now, bool &both) {
    both = both || (which != Read::Given::None && which != now);
    which = now;
}

float toLinear(float c) {
    return std::pow(std::clamp(c, 0.0f, 1.0f), 2.2f);
}

// A block's field, its name read (lower case).
template <class Values>
void field(Light &light, Read &read, const QString &name, Values &values) {
    if (name == "name") {
        light.name = values.string().trimmed();
        light.bindName = light.name;
    } else if (name == "on") {
        light.on = values.number() != 0.0f;
    } else if (name == "type") {
        const QString type = values.string().trimmed();
        if (type.compare("point", Qt::CaseInsensitive) == 0)
            light.type = Light::Type::Point;
        else if (type.compare("spot", Qt::CaseInsensitive) == 0)
            light.type = Light::Type::Spot;
        else
            read.unknownType = type;
    } else if (name == "shapehierarchy") {
        light.hierarchy = values.string().trimmed();
    } else if (name == "position") {
        for (float &p : light.position)
            p = values.number();
    } else if (name == "qdirection") {
        // x y z w, as a W file's and a glTF node's rotation.
        float q[4];
        for (float &c : q)
            c = values.number();
        QQuaternion rotation(q[3], q[0], q[1], q[2]);
        if (rotation.length() > 0.0f)
            rotation.normalize();
        else
            rotation = QQuaternion();
        setDirection(light, rotation.rotatedVector(QVector3D(0.0f, 0.0f, -1.0f)));
        given(read.direction, Read::Given::First, read.directions);
    } else if (name == "rotation") {
        // As a display square's: about x, then y, then z.
        float r[3];
        for (float &c : r)
            c = values.number();
        QMatrix4x4 m;
        m.rotate(r[2], 0.0f, 0.0f, 1.0f);
        m.rotate(r[1], 0.0f, 1.0f, 0.0f);
        m.rotate(r[0], 1.0f, 0.0f, 0.0f);
        setDirection(light, m.mapVector(QVector3D(0.0f, 0.0f, -1.0f)));
        given(read.direction, Read::Given::Second, read.directions);
    } else if (name == "color3f") {
        for (float &c : light.color)
            c = std::max(0.0f, values.number());
        read.alpha = 1.0f;
        given(read.color, Read::Given::First, read.colors);
    } else if (name == "colorrgba") {
        // RRGGBBAA. A parsed .sd may give the hex as a number, without its
        // leading zeros: eight digits always.
        QString hex = values.string().trimmed();
        if (hex.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
            hex = hex.mid(2);
        bool ok = hex.size() <= 8;
        const unsigned int value = hex.rightJustified(8, QLatin1Char('0')).toUInt(&ok, 16);
        if (ok && hex.size() <= 8) {
            for (int c = 0; c < 3; ++c)
                light.color[c] = toLinear(float((value >> (24 - 8 * c)) & 0xff) / 255.0f);
            read.alpha = float(value & 0xff) / 255.0f;
        } else {
            std::fill(light.color, light.color + 3, 1.0f);
            read.alpha = 1.0f;
            read.badColor = hex;
        }
        given(read.color, Read::Given::Second, read.colors);
    } else if (name == "intensity") {
        light.intensity = values.number();
        read.intensity = true;
    } else if (name == "range") {
        light.range = std::max(0.0f, values.number());
    } else if (name == "viewdistance") {
        light.viewDistance = std::max(0.0f, values.number());
    } else if (name == "cone") {
        read.cone[0] = values.number();
        read.cone[1] = values.number();
    } else if (name == "radius") {
        const float radius = values.number();
        if (radius >= 0.0f)
            light.radius = radius;
    }
}

// The block read: whether it is a light; its problems.
bool finish(Light &light, Read &read, QStringList *problems) {
    QStringList found;
    if (!read.badColor.isNull())
        //% "ColorRGBA %1 is not hex RRGGBBAA; white is used"
        found << qtTrId("shapelights.problem.color.hex").arg(read.badColor);
    if (read.colors)
        //% "both Color3F and ColorRGBA; the later is used"
        found << qtTrId("shapelights.problem.colors");
    if (read.directions)
        //% "both QDirection and Rotation; the later is used"
        found << qtTrId("shapelights.problem.directions");
    // glTF's limits: 0 <= inner < outer <= 90 degrees.
    const float outer = std::clamp(read.cone[1], 1.0f, 90.0f);
    const float inner = std::clamp(read.cone[0], 0.0f, outer - 1.0f);
    if (light.type == Light::Type::Spot && (outer != read.cone[1] || inner != read.cone[0]))
        //% "Cone %1 %2 is not 0 <= inner < outer <= 90 degrees; %3 %4 is used"
        found << qtTrId("shapelights.problem.cone").arg(read.cone[0]).arg(read.cone[1]).arg(inner).arg(outer);
    light.innerCone = inner * Degrees;
    light.outerCone = outer * Degrees;
    light.intensity *= read.alpha;
    bool kept = true;
    if (!read.unknownType.isEmpty()) {
        //% "left out: Type %1 is neither Point nor Spot"
        found << qtTrId("shapelights.problem.type").arg(read.unknownType);
        kept = false;
    } else if (!read.intensity || light.intensity <= 0.0f) {
        //% "left out: no Intensity above 0"
        found << qtTrId("shapelights.problem.no.intensity");
        kept = false;
    }
    if (problems != nullptr) {
        // Named, when it has a name.
        for (const QString &problem : found)
            *problems << (light.name.isEmpty() ? problem : light.name + ": " + problem);
    }
    return kept;
}

RenderItem::Light itemLight(const Light &light) {
    RenderItem::Light out;
    std::copy(light.position, light.position + 3, out.position);
    std::copy(light.direction, light.direction + 3, out.direction);
    std::copy(light.color, light.color + 3, out.color);
    // Candela as the packets take glTF's (task renderer 21's units).
    out.intensity = light.intensity / float(M_PI);
    out.range = light.range;
    out.viewDistance = light.viewDistance;
    out.radius = light.radius;
    out.innerCone = light.innerCone;
    out.outerCone = light.outerCone;
    out.type = light.type == Light::Type::Spot ? RenderItem::Light::SPOT : RenderItem::Light::POINT;
    return out;
}

// A light's disc in its part's space: Radius wide at its place, facing the
// way it shines (the disc faces -z).
void discMatrix(const Light &light, float *out) {
    QVector3D z = -QVector3D(light.direction[0], light.direction[1], light.direction[2]).normalized();
    if (z.isNull())
        z = QVector3D(0.0f, 0.0f, 1.0f);
    const QVector3D up = std::abs(z.y()) < 0.99f ? QVector3D(0.0f, 1.0f, 0.0f) : QVector3D(1.0f, 0.0f, 0.0f);
    const QVector3D x = QVector3D::crossProduct(up, z).normalized();
    const QVector3D y = QVector3D::crossProduct(z, x);
    const float r = light.radius;
    const float m[16] = {x.x() * r, x.y() * r, x.z() * r, 0.0f,
                         y.x() * r, y.y() * r, y.z() * r, 0.0f,
                         z.x() * r, z.y() * r, z.z() * r, 0.0f,
                         light.position[0], light.position[1], light.position[2], 1.0f};
    std::copy(m, m + 16, out);
}

}

bool readBlock(FileBuffer *data, Light &light, QStringList *problems) {
    light = Light();
    Read read;
    Tokens tokens{data};
    QString sh;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        field(light, read, sh, tokens);
        ParserX::SkipToken(data);
    }
    return finish(light, read, problems);
}

QVector<Light> read(const SFileDetail::Node &shape, QStringList *problems) {
    QVector<Light> lights;
    for (const SFileDetail::Node block : shape.children("esd_tsrelight")) {
        Light light;
        Read read;
        for (const SFileDetail::Node value : block.children()) {
            Scalars scalars{value};
            field(light, read, value.name().toLower(), scalars);
        }
        if (finish(light, read, problems))
            lights.push_back(light);
    }
    return lights;
}

namespace {

// The shape's part of this name; -1 for none.
int namedPart(const ComplexShape &shape, const QString &name) {
    for (int part = 0; part < shape.partCount() && !name.isEmpty(); ++part)
        if (shape.partName(part).compare(name, Qt::CaseInsensitive) == 0)
            return part;
    return -1;
}

}

int partOf(const ComplexShape &shape, const Light &light) {
    const int named = namedPart(shape, light.hierarchy);
    if (named >= 0)
        return named;
    // The root: the first matrix, as the shape's vertices are drawn (an MSTS
    // shape's mirrors x); none for a shape without parts (glTF: its space).
    return shape.partCount() > 0 ? 0 : -1;
}

int bound(const ComplexShape &shape, const QString &name) {
    const QVector<Light> &lights = shape.lights();
    for (int i = 0; i < lights.size() && !name.isEmpty(); ++i)
        if (lights[i].bindName.compare(name, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

void report(const ComplexShape &shape) {
    const QString shapePath = shape.getPathId();
    if (!shape.isLoaded() || !ComplexShape::firstReport(shapePath, "lights"))
        return;
    const QString name = shapePath.section('/', -1);
    auto tell = [&shapePath](const QString &text) {
        ErrorMessagesLib::PushErrorMessage(new ErrorMessage(ErrorMessage::Type_Warning, ErrorMessage::Source_World,
                                                            text, shapePath));
    };
    for (const QString &problem : shape.lightProblems())
        //% "Shape %1: a light (ESD_TSRELight): %2."
        tell(qtTrId("shapelights.shape.problem").arg(name, problem));
    for (const Light &light : shape.lights())
        if (!light.hierarchy.isEmpty() && namedPart(shape, light.hierarchy) < 0)
            //% "Shape %1: its light's ShapeHierarchy %2 is not one of the shape's matrices; the light hangs on its root."
            tell(qtTrId("shapelights.shape.hierarchy").arg(name, light.hierarchy));
}

Drawn::~Drawn() {
    clear();
}

void Drawn::clear() {
    for (State &state : states)
        retire(state);
    states.clear();
    built = QVector<Light>();
    reported = false;
}

void Drawn::sync(const ComplexShape &shape) {
    // A copy keeps its lights' data alive: the same data is the same lights.
    if (shape.lights().constData() != built.constData()) {
        clear();
        built = shape.lights();
    }
}

Drawn::State &Drawn::stateOf(unsigned int stateId) {
    if (stateId >= states.size())
        states.resize(stateId + 1);
    State &state = states[stateId];
    if (state.strength.size() != size_t(built.size())) {
        state.strength.resize(built.size());
        for (int i = 0; i < built.size(); ++i)
            state.strength[i] = built.at(i).on ? 1.0f : 0.0f;
        state.changed = true;
    }
    return state;
}

void Drawn::setStrength(const ComplexShape &shape, unsigned int stateId, int light, float strength) {
    sync(shape);
    if (light < 0 || light >= built.size())
        return;
    State &state = stateOf(stateId);
    const float value = strength < 0.0f ? (built.at(light).on ? 1.0f : 0.0f) : std::clamp(strength, 0.0f, 1.0f);
    if (state.strength[light] != value) {
        state.strength[light] = value;
        state.changed = true;
    }
}

void Drawn::resetState(unsigned int stateId) {
    if (stateId >= states.size())
        return;
    states[stateId].strength.clear();
    states[stateId].changed = true;
}

void Drawn::shine(State &state) {
    for (Group &group : state.groups)
        for (size_t k = 0; k < group.members.size(); ++k)
            group.lights->lights[int(k)].intensity = group.intensity[k] * state.strength[group.members[k]];
    state.changed = false;
}

void Drawn::retire(State &state) {
    for (Group &group : state.groups) {
        if (group.lights != nullptr)
            Renderer::retirePacket(group.lights);
        for (RenderItem *disc : group.discs)
            Renderer::retirePacket(disc);
    }
    state.groups.clear();
    state.frame = ~quint64(0);
    state.discs = false;
}

void Drawn::build(State &state, const ComplexShape &shape, bool discs) {
    // A group per part with lights; packets point into the groups, made
    // once all are in place.
    for (int i = 0; i < built.size(); ++i) {
        const int part = partOf(shape, built.at(i));
        auto group = std::find_if(state.groups.begin(), state.groups.end(),
                                  [part](const Group &g) { return g.part == part; });
        if (group == state.groups.end()) {
            state.groups.emplace_back();
            state.groups.back().part = part;
            group = state.groups.end() - 1;
        }
        group->members.push_back(i);
    }
    const MeshHandle disc = discs ? DiscMesh::shared(RenderItem::V) : MeshHandle();
    // Without the disc's mesh yet, the next push tries again.
    state.discs = disc.valid();
    state.changed = true;
    for (Group &group : state.groups) {
        Mat4::identity(group.matrix.data());
        auto *r = new RenderItem();
        r->shared = true;
        r->msMatrix = group.part >= 0 ? group.matrix.data() : nullptr;
        for (int i : group.members) {
            r->lights.push_back(itemLight(built.at(i)));
            group.intensity.push_back(r->lights.back().intensity);
        }
        group.lights = r;
        if (!state.discs)
            continue;
        group.discMatrices.resize(group.members.size());
        for (size_t k = 0; k < group.members.size(); ++k) {
            const Light &light = built.at(group.members[k]);
            auto *d = new RenderItem();
            d->shared = true;
            d->mesh.handle = disc;
            d->mesh.first = 0;
            d->mesh.count = DiscMesh::Vertices;
            d->mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
            d->material.surface = RenderItem::SURFACE_OPAQUE;
            d->material.brightness = 1.0f;
            d->material.doubleSided = true;
            d->setVertexAttributes(RenderItem::V);
            d->material.lit = false;
            // Its colour at full strength, as painted.
            const float peak = std::max({light.color[0], light.color[1], light.color[2], 1e-6f});
            float shown[3];
            for (int c = 0; c < 3; ++c)
                shown[c] = std::pow(light.color[c] / peak, 1.0f / 2.2f);
            d->disableTextures(shown[0], shown[1], shown[2], 1.0f);
            d->msMatrix = group.discMatrices[k].data();
            group.discs.push_back(d);
        }
    }
}

void Drawn::place(State &state, const ComplexShape &shape, unsigned int stateId) {
    for (Group &group : state.groups) {
        if (group.part < 0 || !shape.partMatrix(stateId, group.part, group.matrix.data()))
            Mat4::identity(group.matrix.data());
        for (size_t k = 0; k < group.discs.size(); ++k) {
            float local[16];
            discMatrix(built.at(group.members[k]), local);
            Mat4::multiply(group.discMatrices[k].data(), group.matrix.data(), local);
            const float centre[3] = {0.0f, 0.0f, 0.0f};
            group.discs[k]->setBounds(centre, 1.0f, group.discMatrices[k].data());
        }
    }
}

void Drawn::push(RenderQueue &queue, const ComplexShape &shape, unsigned int stateId, quint32 selectionId,
                 What what) {
    // Its object's lights off (its light schedule, task renderer 46): no
    // light, no glow, no disc.
    if (!queue.lightsOn())
        return;
    sync(shape);
    if (built.isEmpty() || !shape.isLoaded())
        return;
    if (!reported) {
        report(shape);
        reported = true;
    }
    const bool discs = !(RenderContext::rhi() && Game::localLightsEnabled);
    State &state = stateOf(stateId);
    if (state.groups.empty() || state.discs != discs) {
        retire(state);
        build(state, shape, discs);
    }
    // A state's pose is the same all frame.
    const quint64 frame = Renderer::frameNumber();
    if (state.frame != frame) {
        place(state, shape, stateId);
        state.frame = frame;
    }
    if (state.changed)
        shine(state);
    for (const Group &group : state.groups)
        queue.submit(group.lights, selectionId);
    if (!state.discs || what == What::Lights)
        return;
    // The lit lights' discs. Lamps cast no shadows.
    const bool casting = queue.shadowCastingEnabled();
    queue.setShadowCasting(false);
    for (const Group &group : state.groups)
        for (size_t k = 0; k < group.discs.size(); ++k)
            if (state.strength[group.members[k]] > 0.0f)
                queue.submit(group.discs[k], selectionId);
    queue.setShadowCasting(casting);
}

Schedule scheduleFrom(const QString &name) {
    const QString value = name.trimmed();
    if (value.isEmpty())
        return Schedule::Default;
    for (const auto &[text, schedule] : {std::pair{"Default", Schedule::Default}, std::pair{"Night", Schedule::Night},
                                         std::pair{"Always", Schedule::Always}})
        if (value.compare(QLatin1String(text), Qt::CaseInsensitive) == 0)
            return schedule;
    return Schedule::Unknown;
}

QString scheduleName(Schedule schedule) {
    switch (schedule) {
    case Schedule::Night:
        return QStringLiteral("Night");
    case Schedule::Always:
        return QStringLiteral("Always");
    default:
        return QString();
    }
}

namespace {
bool nightNow = true;
}

void setNight(bool night) {
    nightNow = night;
}

bool night() {
    return nightNow;
}

bool lit(Schedule object, const ComplexShape &shape) {
    Schedule schedule = object;
    if (schedule != Schedule::Night && schedule != Schedule::Always) {
        schedule = shape.lightSchedule();
        if (schedule == Schedule::Unknown) {
            const QString path = shape.getPathId();
            if (ComplexShape::firstReport(path, "light schedule"))
                ErrorMessagesLib::PushErrorMessage(new ErrorMessage(ErrorMessage::Type_Warning,
                        ErrorMessage::Source_World,
                        //% "Shape %1: its ESD_TSRELightSchedule (glTF: TSRE_lightSchedule) is none of Night, Always and Default; it is ignored."
                        qtTrId("shapelights.schedule.unknown").arg(path.section('/', -1)), path));
        }
    }
    return schedule == Schedule::Always || nightNow;
}

}
