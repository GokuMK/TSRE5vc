/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Falling rain and snow near the camera (task renderer 28, the rain): drops
// in the air within some 12 m, which the fog volume's rain (RhiFog.cpp) does
// not show one by one. A full-screen pass over the view before its UI: four
// shells about the camera (1.2 to 9.6 m), each a grid of cells fixed in the
// world with one drop at a random place in each (so a moving train passes
// through them), falling with the wind; a drop is drawn where the pixel's
// ray passes it, as the short streak its fall makes in a frame, behind the
// scene's depth not.

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include <algorithm>
#include <cmath>
#include <tsre/Game.h>

namespace {

struct RainParameters {
    float inverse[16];   // directions from the screen
    float camera[4];     // camera (view space), y sign
    float projection[4]; // x and y scale, scene near and far
    float bands[4];      // distant near and far, scene and distant depth ends
    float drops[4];      // mm/h here, snow share, time (s, wrapped), one pixel's angle (rad)
    float wind[4];       // the drops' velocity across (x, z, m/s), their fall (m/s), streak length (m)
    float colour[4];     // their light (display), strength
};

const char *RainFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Rain {
    mat4 inverse;
    vec4 camera;
    vec4 projection;
    vec4 bands;
    vec4 drops;
    vec4 wind;
    vec4 colour;
};
layout(binding = 1) uniform sampler2D sceneDepth;

float sceneDistance(float depth, vec2 ndc) {
    float viewZ;
    if (depth >= bands.w)
        return 1e9;
    if (depth >= bands.z) {
        float z = (depth - bands.z) / (bands.w - bands.z) * 2.0 - 1.0;
        viewZ = 2.0 * bands.x * bands.y / ((bands.y + bands.x) - z * (bands.y - bands.x));
    } else {
        float z = depth / bands.z * 2.0 - 1.0;
        viewZ = 2.0 * projection.z * projection.w / ((projection.w + projection.z) - z * (projection.w - projection.z));
    }
    return viewZ * length(vec3(ndc.x / projection.x, ndc.y / projection.y, 1.0));
}

vec3 hash3(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

void main() {
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * camera.w);
    vec4 far = inverse * vec4(ndc, 1.0, 1.0);
    vec3 d = normalize(far.xyz / far.w);
    float scene = sceneDistance(texelFetch(sceneDepth, ivec2(uv * vec2(textureSize(sceneDepth, 0))), 0).r, ndc);
    float across = max(length(d.xz), 0.05);
    float snow = drops.y;
    // Heavier rain, more drops (every cell's at 2 mm/h, half at drizzle);
    // snow many more flakes for its water (every cell's at 1 mm/h).
    float chance = clamp(0.5 + drops.x / mix(4.0, 1.0, snow), 0.5, 1.0);
    // A drop's size (m), a flake's: 1.5 mm, 1 cm.
    float body = mix(0.0015, 0.01, snow);
    vec3 velocity = vec3(wind.x, -wind.z, wind.y);
    vec3 fall = normalize(velocity);
    float light = 0.0;
    for (int l = 0; l < 4; ++l) {
        float r = 1.2 * exp2(float(l));
        float t = r / across;
        if (t > scene)
            break;
        // The cells' size by the shell's distance: drops about evenly spaced
        // on the screen; a rain drop's cell taller (they fall in streaks).
        float cell = r * mix(0.035, 0.03, snow);
        vec3 size = vec3(cell, cell * mix(5.0, 1.0, snow), cell);
        vec3 p = camera.xyz + d * t;
        // In the frame falling with the drops, the cells fixed.
        vec3 q = p - velocity * drops.z;
        vec3 c = floor(q / size);
        {
            vec3 h = hash3(c + float(l) * 17.0);
            if (h.x > chance)
                continue;
            // Its streak within its cell (one cell a shell is looked at: two,
            // for streaks crossing into the next, and a fifth shell cost some
            // 1.5 ms more on the Steam Deck), so no longer than the cell.
            float streak = min(wind.w, size.y * 0.9);
            vec3 dropFalling = (c + vec3(0.2 + 0.6 * h.x / max(chance, 1e-3), 0.0, 0.2 + 0.6 * h.z)) * size
                    + vec3(0.0, streak + h.y * (size.y - streak), 0.0);
            vec3 drop = dropFalling + velocity * drops.z;
            // Snow sways as it falls.
            drop.xz += snow * 0.25 * vec2(sin(drops.z * 1.3 + h.y * 6.28), cos(drops.z * 1.1 + h.z * 6.28));
            // Only within the shell, fading at its edges: a drop of one
            // shell is not drawn by the next.
            float off = abs(length(drop.xz - camera.xz) - r) / cell;
            float inside = 1.0 - smoothstep(0.35, 0.6, off);
            if (inside <= 0.0)
                continue;
            // The ray's nearest approach to the drop's streak (its fall over
            // a frame, trailing back).
            vec3 a = drop - camera.xyz;
            vec3 s = -fall * streak;
            float b = dot(d, s), cc = dot(s, s), e = dot(d, a), f = dot(s, a);
            float denom = cc - b * b;
            float u = denom > 1e-6 ? clamp((b * e - f) / denom, 0.0, 1.0) : 0.0;
            float along = dot(d, a + s * u);
            if (along <= 0.0 || along > scene)
                continue;
            vec3 gap = a + s * u - d * along;
            // As wide as a pixel there or the drop, whichever is wider; a
            // drop thinner than a pixel faint by its share of it.
            float pixel = along * drops.w;
            float width = max(pixel, body);
            float cover = 1.0 - smoothstep(width * 0.5, width, length(gap));
            light = max(light, cover * inside * clamp(body * 2.5 / width, 0.15, 1.0)
                                   * (1.0 - float(l) * 0.12));
        }
    }
    float a = light * colour.w * mix(1.0, 1.6, snow);
    result = vec4(colour.rgb * a, a);
}
)";

} // namespace

void RhiRenderer::applyRain() {
    if (rainApplied || secondaryView || currentTarget != TARGET_VIEW || !view.valid() || plainImage)
        return;
    rainApplied = true;
    if (fog.rainHere <= 0.05f || !skyFor(TARGET_VIEW) || !sceneProjectionValid)
        return;
    // Under the low clouds' base only.
    if (sky.cameraHeight > fog.rainBase)
        return;
    RhiRenderSurface *surfaceNow = surface();
    if (surfaceNow == nullptr || surfaceNow->frame().commandBuffer == nullptr)
        return;
    flushTarget();
    RainResources &r = rainResources;
    if (r.pipeline == nullptr || r.viewColor != view.color || r.viewDepth != view.depth) {
        delete r.pipeline;
        delete r.bindings;
        delete r.target;
        delete r.pass;
        r.pipeline = nullptr;
        r.bindings = nullptr;
        r.viewColor = view.color;
        r.viewDepth = view.depth;
        if (r.uniforms == nullptr) {
            r.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(RainParameters));
            r.uniforms->create();
        }
        r.target = rhi->newTextureRenderTarget({QRhiColorAttachment(view.color)},
                                               QRhiTextureRenderTarget::PreserveColorContents);
        r.pass = r.target->newCompatibleRenderPassDescriptor();
        r.target->setRenderPassDescriptor(r.pass);
        r.target->create();
        const auto fragment = QRhiShaderResourceBinding::FragmentStage;
        r.bindings = rhi->newShaderResourceBindings();
        r.bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, r.uniforms),
                                 QRhiShaderResourceBinding::sampledTexture(1, fragment, view.depth,
                                                                           sampler(false, true, true))});
        r.bindings->create();
        static QShader vertex, shader;
        if (!vertex.isValid()) {
            vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
            shader = bakeInline(RainFragment, QShader::FragmentStage, rhi);
        }
        QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
        ps->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, shader}});
        QRhiGraphicsPipeline::TargetBlend b;
        b.enable = true;
        b.srcColor = QRhiGraphicsPipeline::One;
        b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        b.srcAlpha = QRhiGraphicsPipeline::Zero;
        b.dstAlpha = QRhiGraphicsPipeline::One;
        ps->setTargetBlends({b});
        ps->setShaderResourceBindings(r.bindings);
        ps->setRenderPassDescriptor(r.pass);
        if (!ps->create()) {
            delete ps;
            ps = nullptr;
        }
        r.pipeline = ps;
    }
    if (r.pipeline == nullptr)
        return;
    RainParameters p = {};
    std::copy(skyInverse[TARGET_VIEW], skyInverse[TARGET_VIEW] + 16, p.inverse);
    std::copy(viewPosition, viewPosition + 3, p.camera);
    p.camera[3] = rhi->isYUpInFramebuffer() ? 1.0f : -1.0f;
    std::copy(sceneProjection, sceneProjection + 4, p.projection);
    p.bands[0] = 600.0f;
    p.bands[1] = distantFar;
    p.bands[2] = 0.98f;
    p.bands[3] = 0.99f;
    if (!r.clock.isValid())
        r.clock.start();
    // Still (captures): one moment, so frames repeat.
    const double seconds = clouds.still ? 12.3 : r.clock.elapsed() / 1000.0;
    const float snow = std::clamp(fog.snow, 0.0f, 1.0f);
    p.drops[0] = fog.rainHere;
    p.drops[1] = snow;
    p.drops[2] = float(std::fmod(seconds, 600.0));
    p.drops[3] = 2.0f / (std::max(sceneProjection[1], 0.1f) * float(std::max(view.size.height(), 1)));
    // Rain falls at some 8 m/s, snow 1; with the wind near the ground (half
    // the low clouds'), across: east is x, north -z.
    p.wind[0] = float(fog.rainWindEast) * 0.5f;
    p.wind[1] = -float(fog.rainWindNorth) * 0.5f;
    p.wind[2] = 8.0f + (1.0f - 8.0f) * snow;
    // A drop's streak: its fall over some 1/30 s (the eye's); a flake's
    // shorter, it shows as a flake.
    p.wind[3] = std::hypot(std::hypot(p.wind[0], p.wind[1]), p.wind[2]) / (30.0f + 90.0f * snow);
    // Their light: the horizon's (they show the sky about them), the scene's
    // scale; faint.
    for (int i = 0; i < 3; ++i) {
        float sum = 0.0f;
        for (int k = 0; k < 8; ++k)
            sum += haze.ring[k][i];
        p.colour[i] = sum / 8.0f * sceneScaleDisplay();
    }
    p.colour[3] = 0.35f;
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->updateDynamicBuffer(r.uniforms, 0, sizeof(p), &p);
    QRhiCommandBuffer *cb = surfaceNow->frame().commandBuffer;
    cb->beginPass(r.target, Qt::black, {1.0f, 0}, batch);
    cb->setGraphicsPipeline(r.pipeline);
    cb->setViewport(QRhiViewport(0, 0, float(view.size.width()), float(view.size.height())));
    cb->setShaderResources(r.bindings);
    cb->draw(3);
    cb->endPass();
}

void RhiRenderer::releaseRain() {
    RainResources &r = rainResources;
    delete r.pipeline;
    delete r.bindings;
    delete r.target;
    delete r.pass;
    delete r.uniforms;
    r = RainResources();
}

QByteArray RhiRenderer::rainShader() {
    return QByteArray(RainFragment);
}
