/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/objects/SignalLights.h>
#include <tsre/Game.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/renderer/DiscMesh.h>
#include <tsre/renderer/RenderContext.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/SignalShape.h>
#include <tsre/tdb/SignalType.h>
#include <tsre/tdb/TDB.h>
#include <tsre/texture/TexLib.h>
#include <QFileInfo>
#include <QHash>
#include <algorithm>
#include <cmath>

namespace {

// Emitted radiance of a light's colour at full strength, linear: enough for
// the bloom to show it as a lit lamp.
constexpr float SignalEmission = 4.0f;
// Lights sit this far in front of the head (metres), as in Open Rails...
constexpr float FrontOffset = 0.015f;
// ...and farther out the more the depth buffer's step is there: about
// 3e-7 d^2 metres (32-bit float depth, the scene's 0.2 m near plane), twice
// that, so a lens never sinks into its own head (a GPU's depth is coarser
// than the software renderer's); a metre at most (1.3 km).
constexpr float FrontOffsetGrowth = 6e-7f;
constexpr float FrontOffsetMax = 1.0f;
// A light texture's texels below this alpha are not the light (Open Rails
// cuts them out and blends the rest; here only cut).
constexpr float LightAlphaCutoff = 0.5f;

float toLinear(float c) {
    return std::pow(std::clamp(c, 0.0f, 1.0f), 2.2f);
}

const SignalLights::StateSource *stateSource = nullptr;
// Bumped when the source changes: serials of two sources never match.
unsigned long long sourceGeneration = 0;

unsigned long long currentSerial() {
    return stateSource != nullptr ? (sourceGeneration << 40) ^ stateSource->stateSerial() : sourceGeneration << 40;
}

// A route's light texture as drawn: its TexLib texture (-1: none, drawn as
// a disc) and its UV rectangle as a transform of the square's coordinates.
struct LightTextureUse {
    int id = -1;
    float uv[6] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};

// Found as Open Rails finds it: in the route's TEXTURES (beside its
// sigcfg.dat), else GLOBAL/TEXTURES; once per sigcfg.dat and name.
const LightTextureUse &lightTexture(const SigCfg &cfg, const QString &name) {
    static QHash<QString, LightTextureUse> known;
    const QString key = cfg.path + QLatin1Char('|') + name;
    const auto found = known.constFind(key);
    if (found != known.constEnd())
        return *found;
    LightTextureUse use;
    const auto texture = cfg.lightTextures.constFind(name);
    if (texture != cfg.lightTextures.constEnd() && !texture->file.isEmpty()) {
        const QString file = QString(texture->file).replace(QLatin1Char('\\'), QLatin1Char('/'));
        QString path = QFileInfo(cfg.path).absolutePath() + QStringLiteral("/TEXTURES/") + file;
        if (!QFileInfo::exists(path))
            path = Game::root + QStringLiteral("/GLOBAL/TEXTURES/") + file;
        if (QFileInfo::exists(path)) {
            use.id = TexLib::addTex(path);
            SignalLights::lightTextureUv(*texture, use.uv);
        }
    }
    return *known.insert(key, use);
}

float determinant3(const float *m) {
    return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2])
            + m[8] * (m[1] * m[6] - m[5] * m[2]);
}

}

float SignalLights::frontOffset(float distance) {
    return std::min(FrontOffset + FrontOffsetGrowth * distance * distance, FrontOffsetMax);
}

void SignalLights::lightTextureUv(const SigCfg::LightTexture &texture, float *rows) {
    // The square's u runs 0 to 1 from x -1 to 1, v 0 to 1 from y 1 to -1
    // (DiscMesh); Open Rails gives x -1 the rectangle's u1, y 1 its v0.
    const float uv[6] = {texture.u0 - texture.u1, 0.0f, texture.u1, 0.0f, texture.v1 - texture.v0, texture.v0};
    std::copy(uv, uv + 6, rows);
}

void SignalLights::setStateSource(const StateSource *source) {
    if (source == stateSource)
        return;
    stateSource = source;
    ++sourceGeneration;
}

SignalLights::~SignalLights() {
    retirePackets();
}

void SignalLights::invalidate() {
    retirePackets();
    lights.clear();
    built = false;
}

void SignalLights::retirePackets() {
    for (RenderItem *packet : packets)
        Renderer::retirePacket(packet);
    packets.clear();
}

void SignalLights::build(ComplexShape *shape, const SignalShape *signalShape, unsigned int heads) {
    built = true;
    builtHeads = heads;
    lights.clear();
    const SigCfg *cfg = Game::trackDB != nullptr ? Game::trackDB->sigCfg : nullptr;
    if (cfg == nullptr)
        return;
    for (int i = 0; i < signalShape->iSubObj && i < 32; ++i) {
        const SignalShape::SubObj &sub = signalShape->subObj[i];
        if (((heads >> i) & 1) == 0 || sub.sigSubTypeId != SignalShape::SIGNAL_HEAD)
            continue;
        const SignalType *type = cfg->findSignalType(sub.sigSubSType);
        const SignalType::DrawState *state = nullptr;
        if (type != nullptr && i < states.size() && !states[i].isEmpty())
            state = type->drawState(states[i]);
        if (type != nullptr && state == nullptr)
            state = type->defaultDrawState();
        if (state == nullptr)
            continue;
        // The head's matrix; a shape without it places the lights from its root.
        float head[16];
        if (!shape->matrixByName(sub.type, head)) {
            Mat4::identity(head);
            head[0] = -1.0f;
        }
        for (const SignalType::DrawLight &draw : state->lights) {
            const SignalType::Light *light = type->light(draw.light);
            if (light == nullptr || light->radius <= 0.0f)
                continue;
            const auto colour = cfg->lightsTable.constFind(light->name);
            if (colour == cfg->lightsTable.constEnd())
                continue;
            Light out;
            out.color[0] = colour->r / 255.0f;
            out.color[1] = colour->g / 255.0f;
            out.color[2] = colour->b / 255.0f;
            std::copy(head, head + 16, out.head);
            std::copy(light->position, light->position + 3, out.position);
            out.radius = light->radius;
            const LightTextureUse &texture = lightTexture(
                    *cfg, !light->lightTexture.isEmpty() ? light->lightTexture : type->lightTexture);
            if (texture.id >= 0) {
                out.texture = texture.id;
                std::copy(texture.uv, texture.uv + 6, out.uv);
            }
            lights.push_back(out);
        }
    }
    place(offset >= 0.0f ? offset : FrontOffset);
}

void SignalLights::place(float frontOffset) {
    offset = frontOffset;
    for (int i = 0; i < lights.size(); ++i) {
        Light &light = lights[i];
        // sigcfg.dat positions are taken as Open Rails takes them: x and z
        // turned about the head's vertical axis, the light in front along
        // -z.
        float local[16];
        Mat4::identity(local);
        local[0] = local[5] = local[10] = light.radius;
        local[12] = -light.position[0];
        local[13] = light.position[1];
        local[14] = -light.position[2] - frontOffset;
        Mat4::multiply(light.matrix, light.head, local);
        if (i < packets.size()) {
            const float centre[3] = {0.0f, 0.0f, 0.0f};
            packets[i]->setBounds(centre, 1.5f, packets[i]->msMatrix);
        }
    }
}

bool SignalLights::texturesReady() const {
    for (const Light &light : lights)
        if (light.texture >= 0 && !TexLib::request(light.texture))
            return false;
    return true;
}

void SignalLights::makePackets() {
    retirePackets();
    const MeshHandle disc = DiscMesh::shared(RenderItem::PBR);
    const MeshHandle quad = DiscMesh::sharedQuad();
    if (!disc.valid() || !quad.valid())
        return;
    for (Light &light : lights) {
        const bool square = textured && light.texture >= 0;
        auto *r = new RenderItem();
        r->mesh.handle = square ? quad : disc;
        r->mesh.first = 0;
        r->mesh.count = square ? DiscMesh::QuadVertices : DiscMesh::Vertices;
        r->mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
        r->mesh.layout = RenderItem::PBR;
        r->material.surface = RenderItem::SURFACE_OPAQUE;
        r->material.brightness = 1.0f;
        r->material.lit = true;
        // The square faces -z, as the lens does: seen from behind it is not
        // drawn (its light showed through thin heads' backs on a GPU, user
        // 2026-11-17); the matrix's mirror (the shape's root) turns its
        // winding over. The disc stays seen from both sides.
        r->material.doubleSided = !square;
        r->material.mirrored = square && determinant3(light.matrix) < 0.0f;
        r->msMatrix = light.matrix;
        const float centre[3] = {0.0f, 0.0f, 0.0f};
        r->setBounds(centre, 1.5f, r->msMatrix);
        RenderItem::Pbr &p = r->pbr;
        p.enabled = true;
        p.metallic = 0.0f;
        p.roughness = 1.0f;
        p.specular = 0.0f;
        if (square) {
            // The texture's alpha cuts the light out; its colour tints the
            // light's (Open Rails: the texture's red blends towards it).
            r->material.textured = true;
            r->material.textureId = light.texture;
            p.alphaCutoff = LightAlphaCutoff;
            for (int map : {int(RenderItem::Pbr::MAP_BASE_COLOR), int(RenderItem::Pbr::MAP_EMISSIVE)})
                std::copy(light.uv, light.uv + 6, p.uvTransforms[map].rows);
        } else {
            r->disableTextures(1.0f, 1.0f, 1.0f, 1.0f);
        }
        if (!emissive) {
            // Unlit in the light's colour.
            p.unlit = true;
            for (int c = 0; c < 3; ++c)
                p.baseColor[c] = toLinear(light.color[c]);
            packets.push_back(r);
            continue;
        }
        std::fill(p.baseColor, p.baseColor + 3, 0.0f);
        if (square)
            p.textures[RenderItem::Pbr::MAP_EMISSIVE] = light.texture;
        p.glow = RenderItem::Pbr::GLOW_SIGNAL;
        float peak = 0.0f;
        for (int c = 0; c < 3; ++c) {
            p.emissive[c] = toLinear(light.color[c]) * SignalEmission;
            peak = std::max(peak, p.emissive[c]);
        }
        if (peak > 0.0f) {
            // Its glow splat, as for emissive surfaces (EmissiveEmitters):
            // intensity M A / (4 pi) of the disc of its radius, radius of a
            // sphere of its area; with a light texture too (its lens is
            // smaller, about half as wide: the glow stays what it was, as
            // Open Rails' glow is the same whatever the texture; only within
            // a few metres, where the lens takes over, is it brighter than
            // the lens's own).
            RenderItem::Light emitter;
            emitter.emissive = true;
            emitter.signal = true;
            emitter.intensity = peak * float(M_PI) / float(4.0 * M_PI);
            for (int c = 0; c < 3; ++c)
                emitter.color[c] = p.emissive[c] / peak;
            emitter.radius = 0.5f;
            // Shining the way the lens faces (-z) only: its glow splat
            // and its light in fog none from behind (the user,
            // 2026-10-17: signals seen from behind showed their aspect
            // as from the front).
            emitter.type = RenderItem::Light::SPOT;
            emitter.direction[0] = emitter.direction[1] = 0.0f;
            emitter.direction[2] = -1.0f;
            emitter.innerCone = 0.0f;
            emitter.outerCone = float(M_PI / 2.0);
            r->lights.push_back(emitter);
        }
        packets.push_back(r);
    }
}

void SignalLights::push(RenderQueue &queue, ComplexShape *shape, const SignalShape *signalShape,
                        unsigned int heads, const int *items, quint32 selectionId, float distance) {
    if (shape == nullptr || signalShape == nullptr || !shape->isLoaded()) {
        // A reloading shape may come back with other matrices.
        if (built)
            invalidate();
        return;
    }
    if (built && heads != builtHeads)
        invalidate();
    // The simulation's draw states, asked again when they may have changed.
    const unsigned long long serial = currentSerial();
    if (serial != statesSerial || states.size() != 32) {
        statesSerial = serial;
        QVector<QString> now(32);
        for (int i = 0; i < 32 && stateSource != nullptr; ++i)
            if (((heads >> i) & 1) != 0 && items[i] >= 0)
                now[i] = stateSource->drawStateOf(items[i]);
        if (now != states) {
            states = now;
            invalidate();
        }
    }
    if (!built)
        build(shape, signalShape, heads);
    if (lights.isEmpty())
        return;
    const bool wantEmissive = RenderContext::rhi() && Game::localLightsEnabled;
    const bool wantTextured = texturesReady();
    if (packets.isEmpty() || wantEmissive != emissive || wantTextured != textured) {
        emissive = wantEmissive;
        textured = wantTextured;
        makePackets();
    }
    const float wanted = frontOffset(distance);
    if (std::fabs(wanted - offset) > 0.001f)
        place(wanted);
    // Lights cast no shadows.
    const bool casting = queue.shadowCastingEnabled();
    queue.setShadowCasting(false);
    for (RenderItem *packet : packets)
        queue.submit(packet, selectionId);
    queue.setShadowCasting(casting);
}
