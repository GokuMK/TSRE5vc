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
#include <algorithm>
#include <cmath>

namespace {

// Emitted radiance of a light's colour at full strength, linear: enough for
// the bloom to show it as a lit lamp.
constexpr float SignalEmission = 4.0f;
// Lights sit this far in front of the head (metres), as in Open Rails.
constexpr float FrontOffset = 0.015f;

float toLinear(float c) {
    return std::pow(std::clamp(c, 0.0f, 1.0f), 2.2f);
}

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
        const SignalType::DrawState *state = type != nullptr ? type->defaultDrawState() : nullptr;
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
            // sigcfg.dat positions are taken as Open Rails takes them: x and z
            // turned about the head's vertical axis, the light in front
            // along -z.
            float local[16];
            Mat4::identity(local);
            local[0] = local[5] = local[10] = light->radius;
            local[12] = -light->position[0];
            local[13] = light->position[1];
            local[14] = -light->position[2] - FrontOffset;
            Mat4::multiply(out.matrix, head, local);
            lights.push_back(out);
        }
    }
}

void SignalLights::makePackets() {
    retirePackets();
    const MeshHandle mesh = DiscMesh::shared(emissive ? RenderItem::PBR : RenderItem::V);
    if (!mesh.valid())
        return;
    for (Light &light : lights) {
        auto *r = new RenderItem();
        r->mesh.handle = mesh;
        r->mesh.first = 0;
        r->mesh.count = DiscMesh::Vertices;
        r->mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
        r->material.surface = RenderItem::SURFACE_OPAQUE;
        r->material.brightness = 1.0f;
        // The shape's mirrored root turns the winding over.
        r->material.doubleSided = true;
        r->msMatrix = light.matrix;
        const float centre[3] = {0.0f, 0.0f, 0.0f};
        r->setBounds(centre, 1.0f, r->msMatrix);
        if (emissive) {
            r->mesh.layout = RenderItem::PBR;
            r->material.lit = true;
            r->disableTextures(1.0f, 1.0f, 1.0f, 1.0f);
            RenderItem::Pbr &p = r->pbr;
            p.enabled = true;
            std::fill(p.baseColor, p.baseColor + 3, 0.0f);
            p.metallic = 0.0f;
            p.roughness = 1.0f;
            p.specular = 0.0f;
            p.glow = RenderItem::Pbr::GLOW_SIGNAL;
            float peak = 0.0f;
            for (int c = 0; c < 3; ++c) {
                p.emissive[c] = toLinear(light.color[c]) * SignalEmission;
                peak = std::max(peak, p.emissive[c]);
            }
            if (peak > 0.0f) {
                // Its glow splat, as for emissive surfaces (EmissiveEmitters):
                // intensity M A / (4 pi) of the unit disc, radius of a sphere
                // of its area.
                RenderItem::Light emitter;
                emitter.emissive = true;
                emitter.signal = true;
                emitter.intensity = peak * float(M_PI) / float(4.0 * M_PI);
                for (int c = 0; c < 3; ++c)
                    emitter.color[c] = p.emissive[c] / peak;
                emitter.radius = 0.5f;
                r->lights.push_back(emitter);
            }
        } else {
            r->setVertexAttributes(RenderItem::V);
            r->material.lit = false;
            r->disableTextures(light.color[0], light.color[1], light.color[2], 1.0f);
        }
        packets.push_back(r);
    }
}

void SignalLights::push(RenderQueue &queue, ComplexShape *shape, const SignalShape *signalShape,
                        unsigned int heads, quint32 selectionId) {
    if (shape == nullptr || signalShape == nullptr || !shape->isLoaded()) {
        // A reloading shape may come back with other matrices.
        if (built)
            invalidate();
        return;
    }
    if (built && heads != builtHeads)
        invalidate();
    if (!built)
        build(shape, signalShape, heads);
    if (lights.isEmpty())
        return;
    const bool wantEmissive = RenderContext::rhi() && Game::localLightsEnabled;
    if (packets.isEmpty() || wantEmissive != emissive) {
        emissive = wantEmissive;
        makePackets();
    }
    // Lights cast no shadows.
    const bool casting = queue.shadowCastingEnabled();
    queue.setShadowCasting(false);
    for (RenderItem *packet : packets)
        queue.submit(packet, selectionId);
    queue.setShadowCasting(casting);
}
