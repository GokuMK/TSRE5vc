/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/trains/CarLights.h>
#include <tsre/Game.h>
#include <tsre/renderer/DiscMesh.h>
#include <tsre/renderer/LightGrid.h>
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/RenderContext.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/Renderer.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace {

// Emitted radiance of a lamp's colour at full strength, linear: enough for
// the bloom to show it lit (as signal lights).
constexpr float TrainEmission = 4.0f;
// Lenses sit this far in front of their position (metres), over the car's
// own lamp glass.
constexpr float FrontOffset = 0.01f;
// Lenses of one colour within a cell this wide share a glow splat.
constexpr float ClusterCell = 0.25f;
// A beam's spot cone: full light within this share of the cone's
// half-angle, none beyond the outer share; half at the cone's own edge, as
// Open Rails' headlight falls off (the shader squares a ramp in the
// cosine between the two).
constexpr float BeamInner = 0.7f;
constexpr float BeamOuter = 1.5f;
// The size of a headlamp, softening surfaces close to it (metres).
constexpr float BeamRadius = 0.1f;
// A beam's strength: at its reach it lights a surface facing it to this
// (task 21's units, the sun's light 0.7), so shorter beams (dim, shunting)
// are weaker. TSRE's own scale, not Open Rails' headlight; to be tuned by
// eye.
constexpr float BeamLightAtReach = 0.01f;

float toLinear(float c) {
    return std::pow(std::clamp(c, 0.0f, 1.0f), 2.2f);
}

// LED dots of a few centimetres need few segments.
int segmentsFor(float radius) {
    return radius < 0.05f ? 8 : 16;
}

void appendLens(std::vector<float> &out, RenderItem::VertexAttr layout, const CarLights::Lens &lens) {
    const float *n = lens.direction;
    // Two axes across the direction: horizontal, then the other.
    float u[3] = {n[2], 0.0f, -n[0]};
    if (std::fabs(n[1]) > 0.9f) {
        u[0] = 0.0f;
        u[1] = n[2];
        u[2] = -n[1];
    }
    const float length = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (float &c : u)
        c /= length;
    const float v[3] = {n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]};
    auto vertex = [&](float x, float y) {
        const size_t at = out.size();
        DiscMesh::appendVertex(out, layout, x, y);
        for (int c = 0; c < 3; ++c)
            out[at + c] = lens.position[c] + lens.radius * (x * u[c] + y * v[c]);
        if (layout == RenderItem::PBR)
            for (int c = 0; c < 3; ++c) {
                out[at + 3 + c] = n[c];
                out[at + 9 + c] = u[c];
            }
    };
    const int segments = segmentsFor(lens.radius);
    for (int i = 0; i < segments; ++i) {
        const float a = float(2.0 * M_PI * i / segments);
        const float b = float(2.0 * M_PI * (i + 1) / segments);
        vertex(0.0f, 0.0f);
        vertex(std::cos(a), std::sin(a));
        vertex(std::cos(b), std::sin(b));
    }
}

// Glow splats of a colour's lenses: one per cluster of lenses facing about
// the same way, at its centre, as bright as the lenses' area, which
// overlapping LED dots count once (no more than the disc around them).
void addEmitters(RenderItem *r, const std::vector<const CarLights::Lens *> &group, const float *emission) {
    const float peak = std::max({emission[0], emission[1], emission[2]});
    if (peak <= 0.0f)
        return;
    struct Cell {
        double sum[3] = {0.0, 0.0, 0.0};
        double facing[3] = {0.0, 0.0, 0.0};
        double area = 0.0;
        std::vector<const CarLights::Lens *> members;
    };
    std::map<std::tuple<int, int, int, int, int, int>, Cell> cells;
    for (const CarLights::Lens *lens : group) {
        const float *d = lens->direction;
        const std::tuple<int, int, int, int, int, int> key(
                int(std::floor(lens->position[0] / ClusterCell)), int(std::floor(lens->position[1] / ClusterCell)),
                int(std::floor(lens->position[2] / ClusterCell)), int(std::lround(d[0])), int(std::lround(d[1])),
                int(std::lround(d[2])));
        Cell &cell = cells[key];
        const double area = M_PI * lens->radius * lens->radius;
        for (int c = 0; c < 3; ++c) {
            cell.sum[c] += lens->position[c] * area;
            cell.facing[c] += d[c] * area;
        }
        cell.area += area;
        cell.members.push_back(lens);
    }
    for (const auto &entry : cells) {
        const Cell &cell = entry.second;
        if (cell.area <= 0.0)
            continue;
        RenderItem::Light emitter;
        emitter.emissive = true;
        emitter.signal = true;
        for (int c = 0; c < 3; ++c)
            emitter.position[c] = float(cell.sum[c] / cell.area);
        double reach = 0.0;
        for (const CarLights::Lens *lens : cell.members) {
            const double dx = lens->position[0] - emitter.position[0];
            const double dy = lens->position[1] - emitter.position[1];
            const double dz = lens->position[2] - emitter.position[2];
            reach = std::max(reach, std::sqrt(dx * dx + dy * dy + dz * dz) + lens->radius);
        }
        const double lit = std::min(cell.area, M_PI * reach * reach);
        // As signal lights: intensity M A / (4 pi), the radius of a sphere
        // of that area.
        emitter.intensity = float(peak * lit / (4.0 * M_PI));
        emitter.radius = float(std::sqrt(lit / (4.0 * M_PI)));
        for (int c = 0; c < 3; ++c)
            emitter.color[c] = emission[c] / peak;
        // Shining the way its lenses face only, as signal lights: its glow
        // splat and its light in fog none from behind (a head lamp's in
        // fog showed about the cab's back and through the car).
        const double facing = std::sqrt(cell.facing[0] * cell.facing[0] + cell.facing[1] * cell.facing[1]
                                        + cell.facing[2] * cell.facing[2]);
        if (facing > 0.0) {
            emitter.type = RenderItem::Light::SPOT;
            for (int c = 0; c < 3; ++c)
                emitter.direction[c] = float(cell.facing[c] / facing);
            emitter.innerCone = 0.0f;
            emitter.outerCone = float(M_PI / 2.0);
        }
        r->lights.push_back(emitter);
    }
}

}

CarLights::~CarLights() {
    release();
}

void CarLights::release() {
    for (RenderItem *packet : packets)
        Renderer::retirePacket(packet);
    packets.clear();
    if (beamPacket != nullptr)
        Renderer::retirePacket(beamPacket);
    beamPacket = nullptr;
    beamReach = 0.0f;
    for (MeshHandle &mesh : meshes)
        Meshes::release(mesh);
    meshes.clear();
}

std::vector<CarLights::Lens> CarLights::lenses(const EngLights &lights, const EngLights::Train &train) {
    std::vector<Lens> out;
    for (const EngLights::Light &light : lights.lights) {
        if (light.type != EngLights::Glow || light.states.isEmpty() || !EngLights::isOn(light, train))
            continue;
        const EngLights::State &s = light.states[0];
        if (s.radius <= 0.0f || (s.colour >> 24) == 0)
            continue;
        float d[3];
        s.direction(d);
        Lens lens;
        // The shape's root mirrors x (matrix 0, as both shape loaders draw
        // it); lights on other matrices (ShapeIndex, ShapeHierarchy) are
        // not used by any content seen.
        lens.direction[0] = -d[0];
        lens.direction[1] = d[1];
        lens.direction[2] = d[2];
        lens.position[0] = -s.position[0] + FrontOffset * lens.direction[0];
        lens.position[1] = s.position[1] + FrontOffset * lens.direction[1];
        lens.position[2] = s.position[2] + FrontOffset * lens.direction[2];
        // A glow is a square Radius wide in Open Rails.
        lens.radius = s.radius * 0.5f;
        lens.colour = s.colour;
        out.push_back(lens);
    }
    return out;
}

std::vector<RenderItem::Light> CarLights::beams(const EngLights &lights, const EngLights::Train &train,
                                                float halfLength) {
    std::vector<RenderItem::Light> out;
    for (const EngLights::Light &light : lights.lights) {
        if (light.type != EngLights::Cone || light.states.isEmpty() || !EngLights::isOn(light, train))
            continue;
        const EngLights::State &s = light.states[0];
        const float reach = s.reach();
        const float alpha = (s.colour >> 24) / 255.0f;
        if (reach <= 0.0f || alpha <= 0.0f)
            continue;
        float d[3];
        s.direction(d);
        RenderItem::Light beam;
        beam.type = RenderItem::Light::SPOT;
        // The shape's root mirrors x, as for the lenses.
        beam.position[0] = -s.position[0];
        beam.position[1] = s.position[1];
        beam.position[2] = s.position[2];
        beam.direction[0] = -d[0];
        beam.direction[1] = d[1];
        beam.direction[2] = d[2];
        // Cones are often placed ahead of the car (the CMK Vectron's 1.5 m
        // beyond its end, for Open Rails' headlight); a beam starting in
        // the air looks wrong in mist, so it starts at the car's end, moved
        // back along the beam.
        const float along = beam.position[2] * beam.direction[2];
        if (halfLength > 0.0f && along > 0.0f && std::fabs(beam.direction[2]) > 0.1f
                && std::fabs(beam.position[2]) > halfLength) {
            const float back = (std::fabs(beam.position[2]) - halfLength) / std::fabs(beam.direction[2]);
            for (int c = 0; c < 3; ++c)
                beam.position[c] -= beam.direction[c] * back;
        }
        const float rgb[3] = {toLinear(((s.colour >> 16) & 255) / 255.0f), toLinear(((s.colour >> 8) & 255) / 255.0f),
                              toLinear((s.colour & 255) / 255.0f)};
        const float peak = std::max({rgb[0], rgb[1], rgb[2]});
        if (peak <= 0.0f)
            continue;
        for (int c = 0; c < 3; ++c)
            beam.color[c] = rgb[c] / peak;
        // I lights a surface facing it at d to I / (pi d^2) (task 21).
        beam.intensity = BeamLightAtReach * float(M_PI) * reach * reach * peak;
        beam.range = std::min(reach, LightGrid::MaxReach);
        beam.radius = BeamRadius;
        beam.innerCone = BeamInner * s.halfAngle();
        beam.outerCone = std::min(BeamOuter * s.halfAngle(), float(M_PI) * 0.5f);
        out.push_back(beam);
    }
    return out;
}

void CarLights::build(const EngLights &lights, const EngLights::Train &train, float halfLength) {
    release();
    built = true;
    builtLights = &lights;
    builtTrain = train;
    builtHalfLength = halfLength;
    const std::vector<Lens> all = lenses(lights, train);
    std::map<quint32, std::vector<const Lens *>> byColour;
    for (const Lens &lens : all)
        byColour[lens.colour].push_back(&lens);
    const RenderItem::VertexAttr layout = emissive ? RenderItem::PBR : RenderItem::V;
    for (const auto &entry : byColour) {
        const quint32 colour = entry.first;
        const std::vector<const Lens *> &group = entry.second;
        MeshData data;
        data.layout = layout;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const Lens *lens : group) {
            appendLens(data.vertices, layout, *lens);
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], lens->position[c] - lens->radius);
                hi[c] = std::max(hi[c], lens->position[c] + lens->radius);
            }
        }
        const int count = int(data.vertices.size() / size_t(layout));
        MeshHandle mesh = Meshes::create(std::move(data));
        if (!mesh.valid())
            continue;
        meshes.push_back(mesh);
        auto *r = new RenderItem();
        r->mesh.handle = mesh;
        r->mesh.first = 0;
        r->mesh.count = count;
        r->mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
        r->material.surface = RenderItem::SURFACE_OPAQUE;
        r->material.brightness = 1.0f;
        // The shape's mirrored root turns the winding over.
        r->material.doubleSided = true;
        float centre[3];
        float extent = 0.0f;
        for (int c = 0; c < 3; ++c) {
            centre[c] = (lo[c] + hi[c]) * 0.5f;
            extent += (hi[c] - lo[c]) * (hi[c] - lo[c]);
        }
        r->setBounds(centre, std::sqrt(extent) * 0.5f);
        const float rgb[3] = {((colour >> 16) & 255) / 255.0f, ((colour >> 8) & 255) / 255.0f,
                              (colour & 255) / 255.0f};
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
            // Train lamps stay visible by day, as signals do.
            p.glow = RenderItem::Pbr::GLOW_SIGNAL;
            // The colour's alpha is the lamp's strength (Open Rails blends
            // its glow by it).
            const float alpha = (colour >> 24) / 255.0f;
            for (int c = 0; c < 3; ++c)
                p.emissive[c] = toLinear(rgb[c]) * alpha * TrainEmission;
            addEmitters(r, group, p.emissive);
        } else {
            r->setVertexAttributes(RenderItem::V);
            r->material.lit = false;
            r->disableTextures(rgb[0], rgb[1], rgb[2], 1.0f);
        }
        packets.push_back(r);
    }
    // Beams light only with local lights (task 21).
    if (emissive) {
        const std::vector<RenderItem::Light> lit = beams(lights, train, halfLength);
        if (!lit.empty()) {
            beamPacket = new RenderItem();
            for (const RenderItem::Light &beam : lit) {
                beamPacket->lights.push_back(beam);
                beamReach = std::max(beamReach, beam.range);
            }
        }
    }
}

void CarLights::push(RenderQueue &queue, const EngLights &lights, const EngLights::Train &train,
                     float halfLength, quint32 selectionId) {
    const bool wantEmissive = RenderContext::rhi() && Game::localLightsEnabled;
    if (!built || builtLights != &lights || builtTrain != train || builtHalfLength != halfLength
            || wantEmissive != emissive) {
        if (!RenderContext::ready())
            return;
        emissive = wantEmissive;
        build(lights, train, halfLength);
    }
    // Lamps cast no shadows.
    const bool casting = queue.shadowCastingEnabled();
    queue.setShadowCasting(false);
    for (RenderItem *packet : packets)
        queue.submit(packet, selectionId);
    queue.setShadowCasting(casting);
    if (beamPacket != nullptr) {
        // As far as the cones reach (the grid's default is 30 m).
        const float reach = queue.lightReach();
        queue.setLightReach(beamReach);
        queue.submit(beamPacket, selectionId);
        queue.setLightReach(reach);
    }
}
