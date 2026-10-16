/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Falling rain and snow near the camera (task renderer 28, the rain): the
// drops in the air within some 40 m, which the fog volume's rain
// (RhiFog.cpp) shows only as a veil. Particles, as games draw them: before
// the view's UI, thousands of streaks in two boxes about the camera (12 and
// 40 m), each drop's
// place in the world from its index (the box wraps as the camera moves, so a
// moving train passes through the drops), falling with the wind; each drawn
// as a thin quad from where it is back along its fall over a moment, at
// least a pixel wide, behind the scene's depth not. No buffers: the vertex
// shader makes them. (Drops found per pixel in shells about the camera cost
// 2 ms for a few of them, and stood in rings looking down; user,
// 2026-10-16.)

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include <algorithm>
#include <cmath>
#include <tsre/Game.h>

namespace {

struct RainParameters {
    float forward[16];   // the view's directions to clip space (the sky's)
    float camera[4];     // camera (view space), the screen's y sign
    float box[4];        // the box's half size across and up (m), drops, time (s, wrapped)
    float velocity[4];   // the drops' velocity (view space, m/s), the streak's time (s)
    float projection[4]; // scene near and far, its depth band's end, focal length (pixels)
    float viewport[4];   // 2 / width, 2 / height, the drop's size (m), snow share
    float colour[4];     // their light (display), strength
    float farBox[4];     // the far box's half size across and up (m), its drops (after the near box's), 0
};

const char *RainVertex = R"(#version 440
layout(location = 0) out vec3 streak;   // across (-1 to 1), along (0 head, 1 tail), strength
layout(location = 1) out float depth;   // the drop's distance in front of the camera (m)
layout(std140, binding = 0) uniform Rain {
    mat4 forward;
    vec4 camera;
    vec4 box;
    vec4 velocity;
    vec4 projection;
    vec4 viewport;
    vec4 colour;
    vec4 farBox;
};

vec3 hash3(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

void main() {
    float id = float(gl_InstanceIndex);
    vec3 seed = hash3(vec3(id * 0.618, id * 0.371 + 11.0, id * 1.713 + 29.0));
    // The near box's drops first, then the far box's: depth beyond the near
    // drops, up to the volume's veil.
    bool farDrop = id >= box.z;
    vec3 size = (farDrop ? farBox.xyx : box.xyx) * 2.0;
    float snow = viewport.w;
    // Where it is: its place in the world, falling, wrapped into the box
    // about the camera.
    vec3 p = camera.xyz + (fract((seed * size + velocity.xyz * box.w - camera.xyz) / size) - 0.5) * size;
    // Snow sways as it falls.
    p.xz += snow * 0.3 * vec2(sin(box.w * 1.3 + seed.y * 6.28), cos(box.w * 1.1 + seed.z * 6.28));
    vec3 tail = p - velocity.xyz * velocity.w;
    vec4 c0 = forward * vec4(p - camera.xyz, 0.0);
    vec4 c1 = forward * vec4(tail - camera.xyz, 0.0);
    streak = vec3(0.0);
    depth = c0.w;
    // Behind or at the camera: none.
    if (c0.w < 0.4 || c1.w < 0.4) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    // In pixels: the streak and its width (a pixel at least, the drop's size
    // nearer); a drop thinner than a pixel fainter.
    vec2 head = c0.xy / c0.w / viewport.xy;
    vec2 back = c1.xy / c1.w / viewport.xy;
    vec2 along = back - head;
    float squared = dot(along, along);
    vec2 dir = squared > 1e-4 ? along * inversesqrt(squared) : vec2(0.0, 1.0);
    vec2 side = vec2(-dir.y, dir.x);
    float body = viewport.z * projection.w / c0.w;
    float width = max(body, 1.2);
    // A flake no longer than wide (near, the wind's blur drew dashes).
    if (snow > 0.5)
        back = head + dir * min(sqrt(squared), width);
    // Fading in near the camera and out far off; a far drop in where the
    // near box's fade out (not doubling them), out at its own box's edge.
    float fade = farDrop ? smoothstep(box.x * 0.6, box.x, c0.w) * (1.0 - smoothstep(farBox.x * 0.6, farBox.x, c0.w))
                         : smoothstep(0.4, 1.5, c0.w) * (1.0 - smoothstep(box.x * 0.6, box.x, c0.w));
    float strength = colour.w * clamp(body / width, 0.5, 1.0) * fade;
    // The quad: two triangles, the ends a half width beyond the streak's.
    int corner = gl_VertexIndex;
    float s = (corner == 1 || corner == 4 || corner == 5) ? 1.0 : -1.0;
    float t = (corner == 2 || corner == 3 || corner == 5) ? 1.0 : 0.0;
    vec2 pixel = mix(head - dir * width * 0.5, back + dir * width * 0.5, t) + side * s * width * 0.5;
    streak = vec3(s, t, strength);
    vec2 ndc = pixel * viewport.xy;
    gl_Position = vec4(ndc.x, ndc.y * camera.w, 0.5, 1.0);
}
)";

const char *RainFragment = R"(#version 440
layout(location = 0) in vec3 streak;
layout(location = 1) in float depth;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Rain {
    mat4 forward;
    vec4 camera;
    vec4 box;
    vec4 velocity;
    vec4 projection;
    vec4 viewport;
    vec4 colour;
    vec4 farBox;
};
layout(binding = 1) uniform sampler2D sceneDepth;

void main() {
    // Behind the scene (its near band; the distant one and the sky lie far
    // past every drop): none.
    float d = texelFetch(sceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    if (d < projection.z) {
        float z = d / projection.z * 2.0 - 1.0;
        float viewZ = 2.0 * projection.x * projection.y / ((projection.y + projection.x) - z * (projection.y - projection.x));
        if (depth > viewZ)
            discard;
    }
    // Soft across; a rain streak brightest at its head; a flake round.
    float across = 1.0 - streak.x * streak.x;
    vec2 flake = vec2(streak.x, streak.y * 2.0 - 1.0);
    float a = streak.z * mix(across * mix(1.0, 0.5, streak.y), max(1.0 - dot(flake, flake), 0.0), viewport.w);
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
        r.bindings = rhi->newShaderResourceBindings();
        r.bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
                                         0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                         r.uniforms),
                                 QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                           view.depth, sampler(false, true, true))});
        r.bindings->create();
        static QShader vertex, fragment;
        if (!vertex.isValid()) {
            vertex = bakeInline(RainVertex, QShader::VertexStage, rhi);
            fragment = bakeInline(RainFragment, QShader::FragmentStage, rhi);
        }
        QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
        ps->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
        ps->setVertexInputLayout(QRhiVertexInputLayout());
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
    std::copy(skyForward[TARGET_VIEW], skyForward[TARGET_VIEW] + 16, p.forward);
    std::copy(viewPosition, viewPosition + 3, p.camera);
    // The sky's directions give the full-screen passes' y; the screen's as
    // the present vertex shader turns it.
    p.camera[3] = (rhi->isYUpInFramebuffer() ? 1.0f : -1.0f)
            * (rhi->isYUpInNDC() != rhi->isYUpInFramebuffer() ? -1.0f : 1.0f);
    if (!r.clock.isValid())
        r.clock.start();
    // Still (captures): one moment, so frames repeat.
    const double seconds = clouds.still ? 12.3 : r.clock.elapsed() / 1000.0;
    const float snow = std::clamp(fog.snow, 0.0f, 1.0f);
    const float rate = fog.rainHere;
    // A box of 24 m across and 14 m up (in one of 50 m most drops lay far
    // off, thinner than a pixel, and few near); heavier rain, more drops;
    // snow more flakes for its water.
    p.box[0] = 12.0f;
    p.box[1] = 7.0f;
    const float drops = std::clamp(8000.0f + rate * (4000.0f + 8000.0f * snow), 6000.0f, 30000.0f);
    p.box[2] = drops;
    // Beyond it a box of 80 m across and 40 m up, its drops filling the
    // distance towards the fog volume's veil: 2.5 times the near box's drops
    // in 32 times its volume (far off they run together).
    p.farBox[0] = 40.0f;
    p.farBox[1] = 20.0f;
    const float farDrops = std::min(drops * 2.5f, 40000.0f);
    p.farBox[2] = farDrops;
    p.box[3] = float(std::fmod(seconds, 600.0));
    // Rain falls at some 8 m/s, snow 1, with half the low wind: east is x,
    // north -z.
    p.velocity[0] = float(fog.rainWindEast) * 0.5f;
    p.velocity[1] = -(8.0f + (1.0f - 8.0f) * snow);
    p.velocity[2] = -float(fog.rainWindNorth) * 0.5f;
    // A streak: its fall over some 1/30 s (the eye's); a flake's shorter.
    p.velocity[3] = 1.0f / (30.0f + 90.0f * snow);
    p.projection[0] = sceneProjection[2];
    p.projection[1] = sceneProjection[3];
    p.projection[2] = 0.98f;
    p.projection[3] = std::max(sceneProjection[1], 0.1f) * float(std::max(view.size.height(), 1)) * 0.5f;
    p.viewport[0] = 2.0f / float(std::max(view.size.width(), 1));
    p.viewport[1] = 2.0f / float(std::max(view.size.height(), 1));
    // A drop's size (m), a flake's: 1.5 mm, 1 cm.
    p.viewport[2] = 0.0015f + (0.01f - 0.0015f) * snow;
    p.viewport[3] = snow;
    // Their light: the horizon's (they show the sky about them), the scene's
    // scale; faint.
    for (int i = 0; i < 3; ++i) {
        float sum = 0.0f;
        for (int k = 0; k < 8; ++k)
            sum += haze.ring[k][i];
        p.colour[i] = sum / 8.0f * sceneScaleDisplay();
    }
    p.colour[3] = 0.6f + 0.3f * snow;
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->updateDynamicBuffer(r.uniforms, 0, sizeof(p), &p);
    QRhiCommandBuffer *cb = surfaceNow->frame().commandBuffer;
    cb->beginPass(r.target, Qt::black, {1.0f, 0}, batch);
    cb->setGraphicsPipeline(r.pipeline);
    cb->setViewport(QRhiViewport(0, 0, float(view.size.width()), float(view.size.height())));
    cb->setShaderResources(r.bindings);
    cb->draw(6, quint32(drops + farDrops));
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

QList<QByteArray> RhiRenderer::rainShaders() {
    return {QByteArray(RainVertex), QByteArray(RainFragment)};
}
