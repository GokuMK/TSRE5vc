/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Temporal anti-aliasing (task renderer 39): the main view is drawn a
// fraction of a pixel off each frame (RhiRenderer::beginViewBand) and each
// pixel blended with the last frames' result where its surface was: the
// velocity's motion for moving instances, the static world's from depth
// and last frame's view (task 38). The history is limited to the colours
// around the pixel (its 3 x 3's mean and spread, in YCoCg), so what moved
// in or was uncovered does not trail. After the scene's composites, before
// the rain and motion blur, two full-screen passes: the resolve into the
// next frame's history, then a contrast-adaptive sharpening of it into the
// view (the history itself unsharpened). The editor's overlays wait for
// them, drawn without the jitter.
// After Karis 2014 ("High Quality Temporal Supersampling"), Salvi 2016
// (variance clipping) and AMD's FidelityFX CAS.

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include <algorithm>
#include <QMatrix4x4>
#include <tsre/ogl/GLUU.h>
#include <tsre/math3d/GLMatrix.h>

namespace {

struct TemporalParameters {
    float toPrevious[16];  // this frame's clip space to last frame's (OpenGL's): the static world
    float size[4];         // the view's width, height and their inverses
    float params[4];       // this frame's share when still, history valid, y up in the framebuffer, debug view
    float slice[4];        // the scene band's slice of the depth range
    float sharpen[4];      // the sharpening's strength (0 to 1)
    float planes[4];       // the scene band's near and far planes (m)
};

#define TEMPORAL_BLOCK R"(#version 440
layout(location = 0) in vec2 uv;
layout(std140, binding = 0) uniform Temporal {
    mat4 toPrevious;
    vec4 size;
    vec4 params;
    vec4 slice;
    vec4 sharpen;
    vec4 planes;
};
float sceneDepth(float d) {
    return clamp((d - slice.x) / max(slice.y - slice.x, 1e-6), 0.0, 1.0);
}
// Bright texels squashed (reversibly) so a lamp or the sun's glint does not
// rule the neighbourhood or flicker (Karis); the history keeps them so.
vec3 squash(vec3 c) {
    return c / (1.0 + max(c.r, max(c.g, c.b)));
}
vec3 unsquash(vec3 c) {
    return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-4);
}
// The distance (m) along the view, the scene band's (beyond it, its far
// plane).
float linearDepth(float d) {
    float z = sceneDepth(d) * 2.0 - 1.0;
    return 2.0 * planes.x * planes.y / max(planes.y + planes.x - z * (planes.y - planes.x), 1e-6);
}
// A pixel's point in this frame's clip space (OpenGL's).
vec4 clipOf(ivec2 at, float depth) {
    vec2 ndc = (vec2(at) + 0.5) * size.zw * 2.0 - 1.0;
    if (params.z < 0.5)
        ndc.y = -ndc.y;
    return vec4(ndc, sceneDepth(depth) * 2.0 - 1.0, 1.0);
}
// A pixel's motion (half normalized device units, as the framebuffer): a
// moving instance's from the velocity; the static world's (x 64: nothing
// moving drawn there; beyond the scene band) from depth.
vec2 motionAt(ivec2 at, float depth, vec4 velocity) {
    if (velocity.x <= 32.0 && depth < slice.y)
        return velocity.xy;
    vec4 c = clipOf(at, depth);
    vec4 a = toPrevious * c;
    vec2 motion = a.w > 1e-5 ? (c.xy - a.xy / a.w) * 0.5 : vec2(0.0);
    if (params.z < 0.5)
        motion.y = -motion.y;
    return motion;
}
)"

const char *ResolveFragment = TEMPORAL_BLOCK R"(
layout(location = 0) out vec4 historyOut;
layout(location = 1) out vec4 depthOut;
layout(binding = 1) uniform sampler2D colorMap;
layout(binding = 2) uniform sampler2D historyMap;
layout(binding = 3) uniform sampler2D depthMap;
layout(binding = 4) uniform sampler2D velocityMap;
layout(binding = 5) uniform sampler2D historyDepthMap;

vec3 toYCoCg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}
vec3 fromYCoCg(vec3 c) {
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}
// The history at a place between texels: Catmull-Rom from 5 bilinear taps
// (bilinear alone would blur a little more each frame).
vec3 historyAt(vec2 at) {
    vec2 position = at * size.xy;
    vec2 centre = floor(position - 0.5) + 0.5;
    vec2 f = position - centre;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 at0 = (centre - 1.0) * size.zw;
    vec2 at3 = (centre + 2.0) * size.zw;
    vec2 at12 = (centre + w2 / w12) * size.zw;
    vec3 sum = textureLod(historyMap, vec2(at12.x, at0.y), 0.0).rgb * (w12.x * w0.y)
            + textureLod(historyMap, vec2(at0.x, at12.y), 0.0).rgb * (w0.x * w12.y)
            + textureLod(historyMap, at12, 0.0).rgb * (w12.x * w12.y)
            + textureLod(historyMap, vec2(at3.x, at12.y), 0.0).rgb * (w3.x * w12.y)
            + textureLod(historyMap, vec2(at12.x, at3.y), 0.0).rgb * (w12.x * w3.y);
    float weights = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(sum / weights, vec3(0.0));
}
// Towards the box's centre until inside it.
vec3 clipToBox(vec3 c, vec3 centre, vec3 extent) {
    vec3 offset = c - centre;
    vec3 ratio = abs(offset) / max(extent, vec3(1e-5));
    float most = max(ratio.x, max(ratio.y, ratio.z));
    return most > 1.0 ? centre + offset / most : c;
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 limit = ivec2(size.xy) - 1;
    vec4 centre = texelFetch(colorMap, p, 0);
    vec3 current = toYCoCg(squash(max(centre.rgb, vec3(0.0))));
    // The 3 x 3's colours (their mean and spread) and its nearest depth:
    // an edge takes the motion of what is in front (Karis).
    vec3 sum = vec3(0.0);
    vec3 sumSquares = vec3(0.0);
    float nearest = 2.0;
    ivec2 nearestAt = p;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 q = clamp(p + ivec2(x, y), ivec2(0), limit);
            vec3 c = toYCoCg(squash(max(texelFetch(colorMap, q, 0).rgb, vec3(0.0))));
            sum += c;
            sumSquares += c * c;
            float d = texelFetch(depthMap, q, 0).r;
            if (d < nearest) {
                nearest = d;
                nearestAt = q;
            }
        }
    }
    vec3 mean = sum / 9.0;
    vec3 spread = sqrt(max(sumSquares / 9.0 - mean * mean, vec3(0.0)));
    vec4 nearestVelocity = texelFetch(velocityMap, nearestAt, 0);
    vec2 motion = motionAt(nearestAt, nearest, nearestVelocity);
    vec2 there = (vec2(p) + 0.5) * size.zw - motion;
    // The same static surface as last frame (the 3 x 3's nearest depth then,
    // kept with the history, as far as the surface was): its history is
    // kept whole, not clipped to this frame's colours. A wire thinner than a
    // pixel, missed by this frame's jitter, would else be clipped away and
    // fade; it builds up to its share of the pixel instead. What moved or
    // was uncovered (a train passing, parallax) differs in depth: clipped.
    // Nearer kept than expected, static, and that nearer surface moving less
    // than a pixel a frame here (its own motion at its own depth: from a
    // moving cab the sky barely moves while a gantry passing in front of it
    // moves far, and its history over the sky would trail): a thin static
    // feature this frame's jitter missed in the whole 3 x 3, a distant wire.
    // A moving instance's depth is kept negative: where a train was is
    // clipped.
    float speed = length(motion * size.xy);
    float nearestLinear = linearDepth(nearest);
    bool still = nearestVelocity.x > 32.0 || nearest >= slice.y;
    bool same = false;
    if (still) {
        // (The point taken with w 1: its true clip position is this times
        // its distance now, as is last frame's.)
        float expected = (toPrevious * clipOf(nearestAt, nearest)).w * nearestLinear;
        float stored = texelFetch(historyDepthMap, clamp(ivec2(there * size.xy), ivec2(0), limit), 0).r;
        same = stored > 0.0 && abs(stored - expected) <= 0.02 * expected + 0.05;
        if (!same && stored > 0.0 && stored < expected && speed < 1.0) {
            float ndcZ = (planes.y + planes.x - 2.0 * planes.x * planes.y / max(stored, 1e-3)) / (planes.y - planes.x);
            vec4 c = vec4(clipOf(p, nearest).xy, ndcZ, 1.0);
            vec4 a = toPrevious * c;
            float storedSpeed = a.w > 1e-5 ? length((c.xy - a.xy / a.w) * 0.5 * size.xy) : 1e3;
            same = storedSpeed < 1.0;
        }
    }
    depthOut = vec4(still ? nearestLinear : -nearestLinear, 0.0, 0.0, 0.0);
    bool inside = all(greaterThanEqual(there, vec2(0.0))) && all(lessThanEqual(there, vec2(1.0)));
    // More of this frame the faster the pixel moves: each frame's history is
    // resampled where the surface was, which blurs along the motion (motion
    // blur hides the edges there); a tenth still, up to 0.4 by 20 pixels a
    // frame.
    float share = clamp(params.x + speed * 0.015, params.x, 0.4);
    vec3 history = current;
    bool kept = params.y > 0.5 && inside;
    if (kept) {
        vec3 old = toYCoCg(historyAt(there));
        history = same ? old : clipToBox(old, mean, spread * 1.25);
    } else {
        share = 1.0;
    }
    // TSRE_TAA_DEBUG=2: the history kept whole (green), clipped static
    // (red), a moving instance's (blue), none (black).
    if (params.w > 1.5) {
        historyOut = vec4(kept ? (still ? (same ? vec3(0.0, 0.6, 0.0) : vec3(0.6, 0.0, 0.0)) : vec3(0.0, 0.0, 0.6))
                               : vec3(0.0), 1.0);
        return;
    }
    // The squashed colour, the view's alpha.
    historyOut = vec4(max(fromYCoCg(mix(history, current, share)), vec3(0.0)), centre.a);
}
)";

// The resolved frame into the view, sharpened by the contrast around each
// pixel (AMD's CAS: less where it is already high), in the squashed range.
const char *SharpenFragment = TEMPORAL_BLOCK R"(
layout(location = 0) out vec4 viewOut;
layout(binding = 1) uniform sampler2D resolvedMap;
layout(binding = 3) uniform sampler2D depthMap;
layout(binding = 4) uniform sampler2D velocityMap;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 limit = ivec2(size.xy) - 1;
    vec4 centre = texelFetch(resolvedMap, p, 0);
    // TSRE_TAA_DEBUG=2: the resolve's colours as they are.
    if (params.w > 1.5) {
        viewOut = vec4(centre.rgb, 1.0);
        return;
    }
    // TSRE_TAA_DEBUG=1: the motion (red x, green y, 8 px a frame full).
    if (params.w > 0.5) {
        vec2 motion = motionAt(p, texelFetch(depthMap, p, 0).r, texelFetch(velocityMap, p, 0));
        viewOut = vec4(abs(motion * size.xy) / 8.0, 0.0, 1.0);
        return;
    }
    vec3 c = centre.rgb;
    vec3 a = texelFetch(resolvedMap, clamp(p + ivec2(0, -1), ivec2(0), limit), 0).rgb;
    vec3 b = texelFetch(resolvedMap, clamp(p + ivec2(-1, 0), ivec2(0), limit), 0).rgb;
    vec3 d = texelFetch(resolvedMap, clamp(p + ivec2(1, 0), ivec2(0), limit), 0).rgb;
    vec3 e = texelFetch(resolvedMap, clamp(p + ivec2(0, 1), ivec2(0), limit), 0).rgb;
    vec3 low = min(min(min(a, b), min(d, e)), c);
    vec3 high = max(max(max(a, b), max(d, e)), c);
    vec3 amplitude = sqrt(clamp(min(low, 1.0 - high) / max(high, vec3(1e-4)), 0.0, 1.0));
    vec3 w = amplitude * (-1.0 / mix(8.0, 5.0, sharpen.x));
    vec3 sharpened = clamp(((a + b + d + e) * w + c) / (1.0 + 4.0 * w), vec3(0.0), vec3(0.9995));
    viewOut = vec4(unsquash(sharpened), centre.a);
}
)";

#undef TEMPORAL_BLOCK

} // namespace

QList<QByteArray> RhiRenderer::temporalShaders() {
    return {QByteArray(ResolveFragment), QByteArray(SharpenFragment)};
}

void RhiRenderer::releaseTemporal() {
    TemporalResources &t = temporal;
    for (QRhiGraphicsPipeline *p : t.pipelines)
        delete p;
    for (QRhiShaderResourceBindings *b : t.bindings)
        delete b;
    for (QRhiTextureRenderTarget *target : t.targets)
        delete target;
    for (QRhiRenderPassDescriptor *d : t.passes)
        delete d;
    for (QRhiTexture *h : t.history)
        delete h;
    for (QRhiTexture *h : t.historyDepth)
        delete h;
    delete t.uniforms;
    t = TemporalResources();
}

bool RhiRenderer::overlaysAfterTemporal() const {
    return antiAliasing == 1 && viewMotionTaken && !secondaryView && currentTarget == TARGET_VIEW && view.valid()
            && !plainImage && baseProgram != PROGRAM_SELECTION;
}

void RhiRenderer::drawDeferredOverlays() {
    PassQueue &queue = passes[PASS_OVERLAY];
    if (!overlayProjectionValid || (queue.ordered.empty() && queue.grouped.empty()))
        return;
    // The scene band's projection without the jitter, its slice of the
    // depth range, its culling (the band has ended: without it every overlay
    // gathered round the camera was drawn, 168 draws for 21, 1 to 2 ms of
    // the processor's); the frame's values (another band's, the UI's) after.
    float savedProjection[16];
    std::copy(gluu->pMatrix, gluu->pMatrix + 16, savedProjection);
    const float savedRange[2] = {depthRange[0], depthRange[1]};
    const bool savedFogOverride = fogLodOverride;
    const float savedFogLod = fogLod;
    const Frustum savedCull = cullFrustum;
    std::copy(overlayProjection, overlayProjection + 16, gluu->pMatrix);
    depthRange[0] = 0.0f;
    depthRange[1] = 0.98f;
    setCullView(gluu->pMatrix);
    applyFrameUniforms();
    recordInstances(queue.ordered, PASS_OVERLAY, false);
    sortByTexture(queue.grouped);
    recordInstances(queue.grouped, PASS_OVERLAY, true);
    consumePass(queue);
    std::copy(savedProjection, savedProjection + 16, gluu->pMatrix);
    depthRange[0] = savedRange[0];
    depthRange[1] = savedRange[1];
    cullFrustum = savedCull;
    applyFrameUniforms();
    if (savedFogOverride)
        setFogLod(savedFogLod);
}

void RhiRenderer::applyTemporal() {
    if (temporalApplied || !overlaysAfterTemporal())
        return;
    temporalApplied = true;
    resolveTemporal();
    // The editor's overlays over the result, without the jitter.
    drawDeferredOverlays();
}

void RhiRenderer::resolveTemporal() {
    if (!tracksMotion() || view.color4 == nullptr || view.depth == nullptr)
        return;
    RhiRenderSurface *surfaceNow = surface();
    if (surfaceNow == nullptr || surfaceNow->frame().commandBuffer == nullptr)
        return;
    flushTarget();
    TemporalResources &t = temporal;
    const QSize size = view.size;
    if (t.viewColor != view.color || t.size != size) {
        releaseTemporal();
        t.viewColor = view.color;
        t.size = size;
        auto texture = [this](QRhiTexture::Format format, const QSize &s, QRhiTexture::Flags flags) {
            QRhiTexture *created = rhi->newTexture(format, s, 1, flags);
            if (!created->create()) {
                delete created;
                return static_cast<QRhiTexture *>(nullptr);
            }
            return created;
        };
        t.history[0] = texture(QRhiTexture::RGBA16F, size, QRhiTexture::RenderTarget);
        t.history[1] = texture(QRhiTexture::RGBA16F, size, QRhiTexture::RenderTarget);
        t.historyDepth[0] = texture(QRhiTexture::R16F, size, QRhiTexture::RenderTarget);
        t.historyDepth[1] = texture(QRhiTexture::R16F, size, QRhiTexture::RenderTarget);
        t.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(TemporalParameters));
        if (t.history[0] == nullptr || t.history[1] == nullptr || t.historyDepth[0] == nullptr
                || t.historyDepth[1] == nullptr || !t.uniforms->create()) {
            releaseTemporal();
            t.viewColor = view.color;
            t.size = size;
            t.failed = true;
            return;
        }
        static QShader vertex, resolve, sharpen;
        if (!vertex.isValid()) {
            vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
            resolve = bakeInline(ResolveFragment, QShader::FragmentStage, rhi);
            sharpen = bakeInline(SharpenFragment, QShader::FragmentStage, rhi);
        }
        const auto fragment = QRhiShaderResourceBinding::FragmentStage;
        const auto stages = QRhiShaderResourceBinding::VertexStage | fragment;
        QRhiSampler *nearest = sampler(false, true, true);
        QRhiSampler *linear = sampler(false, true, false);
        // Targets and passes: history 0 (and its depth), history 1, the view.
        for (int i = 0; i < 3; ++i) {
            QRhiTextureRenderTargetDescription description;
            if (i < 2)
                description.setColorAttachments({QRhiColorAttachment(t.history[i]), QRhiColorAttachment(t.historyDepth[i])});
            else
                description.setColorAttachments({QRhiColorAttachment(view.color)});
            QRhiTextureRenderTarget *target = rhi->newTextureRenderTarget(description);
            t.passes[i] = target->newCompatibleRenderPassDescriptor();
            target->setRenderPassDescriptor(t.passes[i]);
            target->create();
            t.targets[i] = target;
        }
        // Pipelines: the resolve into history i from history 1 - i, the
        // sharpening from history i into the view.
        for (int i = 0; i < 4; ++i) {
            const bool resolving = i < 2;
            const int h = i % 2;
            QRhiShaderResourceBindings *b = rhi->newShaderResourceBindings();
            if (resolving)
                b->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, t.uniforms),
                                QRhiShaderResourceBinding::sampledTexture(1, fragment, view.color, nearest),
                                QRhiShaderResourceBinding::sampledTexture(2, fragment, t.history[1 - h], linear),
                                QRhiShaderResourceBinding::sampledTexture(3, fragment, view.depth, nearest),
                                QRhiShaderResourceBinding::sampledTexture(4, fragment, view.color4, nearest),
                                QRhiShaderResourceBinding::sampledTexture(5, fragment, t.historyDepth[1 - h], nearest)});
            else
                b->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, t.uniforms),
                                QRhiShaderResourceBinding::sampledTexture(1, fragment, t.history[h], nearest),
                                QRhiShaderResourceBinding::sampledTexture(3, fragment, view.depth, nearest),
                                QRhiShaderResourceBinding::sampledTexture(4, fragment, view.color4, nearest)});
            b->create();
            t.bindings[i] = b;
            QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
            ps->setShaderStages({{QRhiShaderStage::Vertex, vertex},
                                 {QRhiShaderStage::Fragment, resolving ? resolve : sharpen}});
            ps->setVertexInputLayout(QRhiVertexInputLayout());
            if (resolving)
                ps->setTargetBlends({QRhiGraphicsPipeline::TargetBlend(), QRhiGraphicsPipeline::TargetBlend()});
            ps->setShaderResourceBindings(b);
            ps->setRenderPassDescriptor(t.passes[resolving ? h : 2]);
            if (!ps->create()) {
                delete ps;
                ps = nullptr;
                t.failed = true;
            }
            t.pipelines[i] = ps;
        }
    }
    if (t.failed)
        return;

    // A jump (another place, another camera) or the view's motion unknown:
    // this frame alone.
    if (!viewMotion.valid)
        t.historyValid = false;
    TemporalParameters p = {};
    {
        float inverse[16];
        // Mat4 has no inverse; QMatrix4x4 reads its array row by row.
        const QMatrix4x4 q = QMatrix4x4(viewMotion.viewProjection).transposed().inverted();
        std::copy(q.constData(), q.constData() + 16, inverse);
        float step[16];
        Mat4::multiply(step, viewMotion.viewProjection, viewMotion.world);
        Mat4::multiply(p.toPrevious, step, inverse);
    }
    p.size[0] = float(size.width());
    p.size[1] = float(size.height());
    p.size[2] = 1.0f / p.size[0];
    p.size[3] = 1.0f / p.size[1];
    // About a tenth of this frame (still).
    p.params[0] = 0.1f;
    p.params[1] = t.historyValid ? 1.0f : 0.0f;
    p.params[2] = rhi->isYUpInFramebuffer() ? 1.0f : 0.0f;
    static const int debug = qEnvironmentVariableIntValue("TSRE_TAA_DEBUG");
    p.params[3] = float(debug);
    p.slice[0] = sceneDepthRange[0];
    p.slice[1] = sceneDepthRange[1];
    static const float sharpness = qEnvironmentVariableIsSet("TSRE_TAA_SHARPEN")
            ? std::clamp(qEnvironmentVariable("TSRE_TAA_SHARPEN").toFloat(), 0.0f, 1.0f) : 0.4f;
    p.sharpen[0] = sharpness;
    p.planes[0] = viewMotion.nearPlane;
    p.planes[1] = viewMotion.farPlane;

    QRhiCommandBuffer *cb = surfaceNow->frame().commandBuffer;
    auto run = [&](int pipeline, int target, QRhiResourceUpdateBatch *batch) {
        cb->beginPass(t.targets[target], Qt::black, {1.0f, 0}, batch);
        cb->setGraphicsPipeline(t.pipelines[pipeline]);
        cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
        cb->setShaderResources(t.bindings[pipeline]);
        cb->draw(3);
        cb->endPass();
    };
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->updateDynamicBuffer(t.uniforms, 0, sizeof(p), &p);
    const int i = t.current;
    run(i, i, batch);
    run(2 + i, 2, nullptr);
    t.current = 1 - i;
    t.historyValid = true;
}
