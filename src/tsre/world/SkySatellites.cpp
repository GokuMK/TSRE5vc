/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/SkySatellites.h>
#include <settings/SettingsAccess.h>
#include <tsre/renderer/DiscMesh.h>
#include <tsre/renderer/Mesh.h>
#include <tsre/renderer/RenderContext.h>
#include <tsre/renderer/RenderItem.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/Renderer.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Distance of the discs from the camera: inside the sky band (100 m to
// 10 km) and in front of the skydome (MSTS domes are 2000 m across).
constexpr float Distance = 1500.0f;
// Emitted radiance (linear) of the sun's and the moon's colour on the QRhi
// renderer: the sun bright enough for the bloom to give it a halo by day,
// the moon a glow at night (its glow follows the lamps' daylight scale).
constexpr float SunEmission = 12.0f;
constexpr float MoonDayEmission = 1.0f;
constexpr float MoonNightEmission = 3.0f;
// Rows of the moon's lit part (pole to pole).
constexpr int LitRows = 24;

float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float mix(float a, float b, float t) {
    return a + (b - a) * t;
}

float toLinear(float c) {
    return std::pow(std::clamp(c, 0.0f, 1.0f), 2.2f);
}

void normalize(float *v) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length > 0.0f)
        for (int i = 0; i < 3; ++i)
            v[i] /= length;
}

// A disc of the given radius at distance along dir, facing the camera (its
// -z towards it), its x axis towards `towards` projected on the sky (any
// direction across when null).
void place(float *m, const float *dir, const float *towards, float distance, float radius) {
    float x[3] = {0.0f, 0.0f, 0.0f};
    if (towards != nullptr) {
        const float along = towards[0] * dir[0] + towards[1] * dir[1] + towards[2] * dir[2];
        for (int i = 0; i < 3; ++i)
            x[i] = towards[i] - along * dir[i];
    }
    if (x[0] * x[0] + x[1] * x[1] + x[2] * x[2] < 1e-8f) {
        // Across the sky: up crossed with the direction, or east overhead.
        x[0] = dir[2];
        x[2] = -dir[0];
        if (x[0] * x[0] + x[2] * x[2] < 1e-8f)
            x[0] = 1.0f;
    }
    normalize(x);
    const float y[3] = {dir[1] * x[2] - dir[2] * x[1], dir[2] * x[0] - dir[0] * x[2],
                        dir[0] * x[1] - dir[1] * x[0]};
    for (int i = 0; i < 3; ++i) {
        m[i] = x[i] * radius;
        m[4 + i] = y[i] * radius;
        m[8 + i] = dir[i] * radius;
        m[12 + i] = dir[i] * distance;
    }
    m[3] = m[7] = m[11] = 0.0f;
    m[15] = 1.0f;
}

void configure(RenderItem *r, MeshHandle mesh, int count, bool pbr, const float *colour,
               float emission, unsigned char glow, float *matrix) {
    r->mesh.handle = mesh;
    r->mesh.first = 0;
    r->mesh.count = unsigned(count);
    r->mesh.primitive = RenderItem::PRIMITIVE_TRIANGLES;
    r->material = RenderItem::Material();
    r->material.surface = RenderItem::SURFACE_OPAQUE;
    r->material.brightness = 1.0f;
    // Seen from the camera only; double-sided spares getting the winding right.
    r->material.doubleSided = true;
    r->pbr = RenderItem::Pbr();
    r->msMatrix = matrix;
    const float centre[3] = {0.0f, 0.0f, 0.0f};
    r->setBounds(centre, 1.0f, matrix);
    if (pbr) {
        r->mesh.layout = RenderItem::PBR;
        r->material.lit = true;
        r->disableTextures(1.0f, 1.0f, 1.0f, 1.0f);
        RenderItem::Pbr &p = r->pbr;
        p.enabled = true;
        std::fill(p.baseColor, p.baseColor + 3, 0.0f);
        p.metallic = 0.0f;
        p.roughness = 1.0f;
        p.specular = 0.0f;
        p.glow = glow;
        for (int c = 0; c < 3; ++c)
            p.emissive[c] = toLinear(colour[c]) * emission;
    } else {
        r->setVertexAttributes(RenderItem::V);
        r->material.lit = false;
        r->disableTextures(colour[0], colour[1], colour[2], 1.0f);
    }
}

}

SkySatellites::~SkySatellites() {
    for (RenderItem *&packet : packets)
        if (packet != nullptr) {
            Renderer::retirePacket(packet);
            packet = nullptr;
        }
}

// The lit part of the moon in the disc's plane, its bright limb along +x
// (towards the sun): between the limb x = sqrt(1 - y^2) and the terminator,
// a half ellipse x = (1 - 2 fraction) sqrt(1 - y^2).
void SkySatellites::updateLitMesh(float fraction, bool pbr) {
    if (litMesh.valid() && litPbr == pbr && std::fabs(fraction - litFraction) < 0.002f)
        return;
    litFraction = fraction;
    litPbr = pbr;
    const RenderItem::VertexAttr layout = pbr ? RenderItem::PBR : RenderItem::V;
    std::vector<float> v;
    v.reserve(size_t(LitRows) * 6 * layout);
    const float terminator = 1.0f - 2.0f * fraction;
    auto row = [&](int i, float &y, float &half) {
        y = -float(std::cos(M_PI * i / LitRows));
        half = std::sqrt(std::max(0.0f, 1.0f - y * y));
    };
    for (int i = 0; i < LitRows; ++i) {
        float y0, h0, y1, h1;
        row(i, y0, h0);
        row(i + 1, y1, h1);
        const float a[2] = {terminator * h0, y0}, b[2] = {h0, y0};
        const float c[2] = {h1, y1}, d[2] = {terminator * h1, y1};
        for (const float *p : {a, b, c, a, c, d})
            DiscMesh::appendVertex(v, layout, p[0], p[1]);
    }
    litVertices = int(v.size()) / layout;
    MeshData data;
    data.layout = layout;
    data.vertices = std::move(v);
    Meshes::update(litMesh, std::move(data));
}

void SkySatellites::push(RenderQueue &queue, const State &state) {
    if (!RenderContext::ready())
        return;
    const bool showSun = state.sun && Settings::boolean("core.rendering.sky.sun");
    const bool showMoon = state.moon && Settings::boolean("core.rendering.sky.moon");
    if (!showSun && !showMoon)
        return;
    const float size = float(std::clamp(Settings::floating("core.rendering.sky.sunSize"), 0.05, 20.0));
    const float radius = Distance * std::tan(float(M_PI) / 180.0f * size * 0.5f);
    // Emissive on the QRhi renderer, for the bloom.
    const bool pbr = RenderContext::rhi();
    const RenderItem::VertexAttr layout = pbr ? RenderItem::PBR : RenderItem::V;
    const MeshHandle disc = DiscMesh::shared(layout);
    if (!disc.valid())
        return;
    for (RenderItem *&packet : packets)
        if (packet == nullptr)
            packet = new RenderItem();
    // 0 by day, 1 by night, as the sky turns dark.
    const float night = 1.0f - smoothstep(-8.0f, 2.0f, state.sunElevation);

    // Hidden once wholly below the horizon (terrain hides it before that).
    if (showSun && state.sunElevation > -size) {
        const float warm = smoothstep(-1.0f, 12.0f, state.sunElevation);
        const float colour[3] = {1.0f, mix(0.45f, 0.96f, warm), mix(0.15f, 0.88f, warm)};
        place(matrices[SUN], state.sunDirection, nullptr, Distance, radius);
        configure(packets[SUN], disc, DiscMesh::Vertices, pbr, colour, SunEmission,
                  RenderItem::Pbr::GLOW_FULL, matrices[SUN]);
        queue.submit(packets[SUN]);
    }

    if (showMoon && state.moonElevation > -size) {
        const float pale[3] = {0.95f, 0.93f, 0.88f};
        // By day the moon is pale against the sky.
        const float strength = mix(0.55f, 1.0f, night);
        float colour[3];
        for (int c = 0; c < 3; ++c)
            colour[c] = pbr ? pale[c] : mix(state.skyColor[c], pale[c], strength);
        if (night > 0.3f) {
            // The dark side, a little lighter than the night sky (earthshine).
            float dark[3];
            for (int c = 0; c < 3; ++c)
                dark[c] = std::min(1.0f, state.skyColor[c] * 1.15f + 0.02f);
            place(matrices[MOON_DARK], state.moonDirection, state.sunDirection, Distance, radius);
            configure(packets[MOON_DARK], DiscMesh::shared(RenderItem::V), DiscMesh::Vertices, false, dark,
                      0.0f, RenderItem::Pbr::GLOW_LAMP, matrices[MOON_DARK]);
            if (packets[MOON_DARK]->mesh.handle.valid())
                queue.submit(packets[MOON_DARK]);
        }
        updateLitMesh(std::clamp(state.moonFraction, 0.0f, 1.0f), pbr);
        if (litMesh.valid() && litVertices > 0 && state.moonFraction > 0.005f) {
            // A little nearer than the dark side, the same size on the sky.
            place(matrices[MOON_LIT], state.moonDirection, state.sunDirection, Distance * 0.999f,
                  radius * 0.999f);
            configure(packets[MOON_LIT], litMesh, litVertices, pbr, colour,
                      mix(MoonDayEmission, MoonNightEmission, night), RenderItem::Pbr::GLOW_LAMP,
                      matrices[MOON_LIT]);
            queue.submit(packets[MOON_LIT]);
        }
    }
}
