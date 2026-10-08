/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Ambient occlusion of the QRhi renderer (task 23). After the opaque passes
// of the main view, ground-truth ambient occlusion (GTAO, Jimenez et al.
// 2016) is computed from the view's depth at a reduced resolution, blurred
// with depth-aware weights, and the occluded part of the ambient light
// share the lit shaders wrote as the view's second colour output is
// subtracted from the view. Direct sun and lamp light, fog and sky stay as
// they are.

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include <QDebug>
#include <algorithm>
#include <tsre/Game.h>

namespace {

// Per quality: resolution divisor, slice directions, steps per side, blur.
struct Preset {
    int divisor;
    int slices;
    int steps;
    bool blur;
};
const Preset Presets[4] = {{0, 0, 0, false}, {4, 2, 4, true}, {2, 3, 6, true}, {1, 4, 8, true}};

// Metres a surface looks for occluders, the curve applied to the result and
// the distances over which occlusion fades out.
constexpr float Radius = 2.0f;
constexpr float Power = 1.5f;
constexpr float FadeStart = 150.0f;
constexpr float FadeEnd = 300.0f;

// std140 parameters shared by the three passes.
struct Parameters {
    float projection[4];  // x scale, y scale, near, far
    float slice[4];       // scene depth range start and end, 1 / view width and height
    float settings[4];    // radius, slices, steps, power
    float fade[4];        // fade start and end, largest screen radius in pixels, 0
    float occlusion[4];   // 1 / occlusion width and height, 0, 0
};

const char *OcclusionFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Parameters {
    vec4 projection;
    vec4 slice;
    vec4 settings;
    vec4 fade;
    vec4 occlusion;
};
layout(binding = 1) uniform sampler2D depthTexture;

const float Pi = 3.14159265;

// Distance along the view axis of a depth in the scene band; -1 outside it
// (sky and distant terrain).
float viewDistance(float depth) {
    if (depth >= slice.y)
        return -1.0;
    float z = (depth - slice.x) / (slice.y - slice.x) * 2.0 - 1.0;
    float near = projection.z, far = projection.w;
    return 2.0 * near * far / ((far + near) - z * (far - near));
}

// The view position of the depth texel at p: positions are rebuilt at the
// texel's centre, the point its depth belongs to (elsewhere sloping surfaces
// would turn into steps that look like occluders).
vec3 positionAt(vec2 p, out bool valid) {
    p = (floor(p / slice.zw) + 0.5) * slice.zw;
    float distance = viewDistance(textureLod(depthTexture, p, 0.0).r);
    valid = distance > 0.0;
    vec2 ndc = p * 2.0 - 1.0;
    distance = max(distance, 0.0);
    return vec3(ndc.x * distance / projection.x, ndc.y * distance / projection.y, -distance);
}

void main() {
    bool valid;
    // The view texel under this pixel's centre.
    vec2 centre = (floor(uv / slice.zw) + 0.5) * slice.zw;
    vec3 position = positionAt(centre, valid);
#ifdef SHOW_DEPTH
    result = vec4(fract(-position.z / 10.0), textureLod(depthTexture, uv, 0.0).r, valid ? 1.0 : 0.0, 1.0);
    return;
#endif
    if (!valid || -position.z > fade.y) {
        result = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }
    // The normal from the neighbours on the nearer side in each direction,
    // so it does not bend across silhouettes.
    vec2 texel = slice.zw;
    bool rightValid, leftValid, upValid, downValid;
    vec3 right = positionAt(centre + vec2(texel.x, 0.0), rightValid) - position;
    vec3 left = position - positionAt(centre - vec2(texel.x, 0.0), leftValid);
    vec3 up = positionAt(centre + vec2(0.0, texel.y), upValid) - position;
    vec3 down = position - positionAt(centre - vec2(0.0, texel.y), downValid);
    vec3 dx = (!leftValid || (rightValid && abs(right.z) < abs(left.z))) ? right : left;
    vec3 dy = (!downValid || (upValid && abs(up.z) < abs(down.z))) ? up : down;
    vec3 view = normalize(-position);
    vec3 normal = normalize(cross(dx, dy));
    if (dot(normal, view) < 0.0)
        normal = -normal;
#ifdef SHOW_NORMAL
    result = vec4(normal * 0.5 + 0.5, 1.0);
    return;
#endif

    float radius = settings.x;
    // The radius on screen, in view pixels.
    float radiusPixels = min(radius * projection.y * 0.5 / (-position.z * texel.y), fade.z);
    if (radiusPixels < 2.0) {
        result = vec4(1.0, -position.z, 0.0, 1.0);
        return;
    }
    // Interleaved gradient noise turns the slices and spreads the steps;
    // the blur averages it away.
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    int slices = int(settings.y);
    int steps = int(settings.z);
    float visibility = 0.0;
    for (int s = 0; s < slices; ++s) {
        float angle = (float(s) + noise) / float(slices) * Pi;
        vec2 direction = vec2(cos(angle), sin(angle));
        vec3 direction3 = vec3(direction, 0.0);
        // The slice through the view direction and this screen direction,
        // and the normal projected into it.
        vec3 ortho = direction3 - dot(direction3, view) * view;
        vec3 axis = normalize(cross(direction3, view));
        vec3 projected = normal - axis * dot(normal, axis);
        float projectedLength = length(projected);
        float cosNormal = clamp(dot(projected, view) / max(projectedLength, 1e-4), -1.0, 1.0);
        float normalAngle = sign(dot(ortho, projected)) * acos(cosNormal);
        float horizons[2];
        for (int side = 0; side < 2; ++side) {
            float sideSign = side == 0 ? -1.0 : 1.0;
            float highest = -1.0;
            for (int j = 0; j < steps; ++j) {
                float t = (float(j) + fract(noise + float(j) * 0.618034)) / float(steps);
                // Denser near the pixel, at least a pixel away.
                vec2 tap = centre + sideSign * direction * (1.0 + t * t * radiusPixels) * texel;
                if (any(lessThan(tap, vec2(0.0))) || any(greaterThan(tap, vec2(1.0))))
                    break;
                bool sampleValid;
                vec3 delta = positionAt(tap, sampleValid) - position;
                if (!sampleValid)
                    continue;
                float length2 = dot(delta, delta);
                float cosHorizon = dot(delta, view) * inversesqrt(max(length2, 1e-8));
                // Occluders fade out towards the radius.
                float weight = clamp(1.0 - length2 / (radius * radius), 0.0, 1.0);
                highest = max(highest, mix(-1.0, cosHorizon, weight));
            }
            horizons[side] = sideSign * acos(clamp(highest, -1.0, 1.0));
        }
        // Horizons limited to the hemisphere around the normal, and the
        // cosine-weighted visible arc between them.
        float h0 = normalAngle + max(horizons[0] - normalAngle, -0.5 * Pi);
        float h1 = normalAngle + min(horizons[1] - normalAngle, 0.5 * Pi);
        visibility += projectedLength * 0.25
                * (-cos(2.0 * h0 - normalAngle) + cos(normalAngle) + 2.0 * h0 * sin(normalAngle)
                   - cos(2.0 * h1 - normalAngle) + cos(normalAngle) + 2.0 * h1 * sin(normalAngle));
    }
    float ambient = pow(clamp(visibility / float(slices), 0.0, 1.0), settings.w);
    ambient = mix(ambient, 1.0, smoothstep(fade.x, fade.y, -position.z));
    result = vec4(ambient, -position.z, 0.0, 1.0);
}
)";

// A 5 x 5 blur weighted by depth similarity, so occlusion does not bleed
// across silhouettes.
const char *BlurFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Parameters {
    vec4 projection;
    vec4 slice;
    vec4 settings;
    vec4 fade;
    vec4 occlusion;
};
layout(binding = 1) uniform sampler2D occlusionTexture;

void main() {
    vec2 centre = textureLod(occlusionTexture, uv, 0.0).rg;
    if (centre.g <= 0.0) {
        result = vec4(centre, 0.0, 1.0);
        return;
    }
    float sum = 0.0, weights = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 tap = textureLod(occlusionTexture, uv + vec2(x, y) * occlusion.xy, 0.0).rg;
            if (tap.g <= 0.0)
                continue;
            float weight = exp(-float(x * x + y * y) / 8.0)
                    * exp(-abs(tap.g - centre.g) / (0.02 * centre.g + 0.05));
            sum += tap.r * weight;
            weights += weight;
        }
    }
    result = vec4(weights > 0.0 ? sum / weights : centre.r, centre.g, 0.0, 1.0);
}
)";

// The occluded share of the ambient light, subtracted from the view by a
// reverse-subtract blend.
const char *CompositeFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(binding = 1) uniform sampler2D ambientTexture;
layout(binding = 2) uniform sampler2D occlusionTexture;

void main() {
    float ambient = textureLod(occlusionTexture, uv, 0.0).r;
#ifdef SHOW_NORMAL
    result = vec4(textureLod(occlusionTexture, uv, 0.0).rgb, 1.0);
#elif defined(SHOW_OCCLUSION)
    result = vec4(vec3(ambient), 1.0);
#else
    result = vec4(textureLod(ambientTexture, uv, 0.0).rgb * (1.0 - ambient), 0.0);
#endif
}
)";

// TSRE_AO_DEBUG shows the occlusion term in place of the view.
bool showOcclusion() {
    static const bool show = qEnvironmentVariableIsSet("TSRE_AO_DEBUG");
    return show;
}

}

QList<QByteArray> RhiRenderer::ambientOcclusionShaders() {
    return {OcclusionFragment, BlurFragment, CompositeFragment};
}

int RhiRenderer::ambientOcclusionQuality() const {
    return std::clamp(Game::ambientOcclusionQuality, 0, 3);
}

bool RhiRenderer::createAmbientOcclusion(int quality) {
    releaseAmbientOcclusion();
    AmbientOcclusion &ao = ambientOcclusion;
    const Preset &preset = Presets[quality];
    ao.quality = quality;
    ao.size = QSize(std::max(1, view.size.width() / preset.divisor),
                    std::max(1, view.size.height() / preset.divisor));
    const QRhiTexture::Format format = rhi->isTextureFormatSupported(QRhiTexture::RGBA16F)
            ? QRhiTexture::RGBA16F : QRhiTexture::RGBA32F;
    ao.raw = rhi->newTexture(format, ao.size, 1, QRhiTexture::RenderTarget);
    ao.blurred = rhi->newTexture(format, ao.size, 1, QRhiTexture::RenderTarget);
    ao.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Parameters));
    ao.nearest = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                 QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    ao.linear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    if (!ao.raw->create() || !ao.blurred->create() || !ao.uniforms->create()
            || !ao.nearest->create() || !ao.linear->create()) {
        releaseAmbientOcclusion();
        return false;
    }
    auto target = [this](QRhiTexture *texture, bool preserve, QRhiTextureRenderTarget *&out,
                         QRhiRenderPassDescriptor *&pass) {
        out = rhi->newTextureRenderTarget({QRhiColorAttachment(texture)},
                                          preserve ? QRhiTextureRenderTarget::PreserveColorContents
                                                   : QRhiTextureRenderTarget::Flags());
        pass = out->newCompatibleRenderPassDescriptor();
        out->setRenderPassDescriptor(pass);
        return out->create();
    };
    if (!target(ao.raw, false, ao.rawTarget, ao.rawPass)
            || !target(ao.blurred, false, ao.blurTarget, ao.blurPass)
            || !target(view.color, true, ao.compositeTarget, ao.compositePass)) {
        releaseAmbientOcclusion();
        return false;
    }
    const auto fragment = QRhiShaderResourceBinding::FragmentStage;
    auto bindings = [&](std::initializer_list<QRhiShaderResourceBinding> list) {
        QRhiShaderResourceBindings *set = rhi->newShaderResourceBindings();
        set->setBindings(list.begin(), list.end());
        set->create();
        return set;
    };
    ao.aoBindings = bindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, ao.uniforms),
                              QRhiShaderResourceBinding::sampledTexture(1, fragment, view.depth, ao.nearest)});
    ao.blurBindings = bindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, ao.uniforms),
                                QRhiShaderResourceBinding::sampledTexture(1, fragment, ao.raw, ao.nearest)});
    QRhiTexture *occlusion = preset.blur ? ao.blurred : ao.raw;
    ao.compositeBindings = bindings({QRhiShaderResourceBinding::sampledTexture(1, fragment, view.color2, ao.nearest),
                                     QRhiShaderResourceBinding::sampledTexture(2, fragment, occlusion, ao.linear)});
    static QShader vertex, occlusionShader, blurShader, compositeShader;
    if (!vertex.isValid()) {
        vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
        QByteArray occlusionSource(OcclusionFragment);
        if (qgetenv("TSRE_AO_DEBUG") == "depth")
            occlusionSource.replace("#version 440\n", "#version 440\n#define SHOW_DEPTH\n");
        if (qgetenv("TSRE_AO_DEBUG") == "normal")
            occlusionSource.replace("#version 440\n", "#version 440\n#define SHOW_NORMAL\n");
        occlusionShader = bakeInline(occlusionSource.constData(), QShader::FragmentStage, rhi);
        blurShader = bakeInline(BlurFragment, QShader::FragmentStage, rhi);
        QByteArray composite(CompositeFragment);
        if (qgetenv("TSRE_AO_DEBUG") == "normal" || qgetenv("TSRE_AO_DEBUG") == "depth")
            composite.replace("#version 440\n", "#version 440\n#define SHOW_NORMAL\n");
        else if (showOcclusion())
            composite.replace("#version 440\n", "#version 440\n#define SHOW_OCCLUSION\n");
        compositeShader = bakeInline(composite.constData(), QShader::FragmentStage, rhi);
    }
    auto pipeline = [&](const QShader &shader, QRhiShaderResourceBindings *set,
                        QRhiRenderPassDescriptor *pass, bool subtract) {
        QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
        ps->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, shader}});
        if (subtract && !showOcclusion()) {
            // view - occluded ambient light; the view's alpha stays.
            QRhiGraphicsPipeline::TargetBlend blend;
            blend.enable = true;
            blend.srcColor = QRhiGraphicsPipeline::One;
            blend.dstColor = QRhiGraphicsPipeline::One;
            blend.opColor = QRhiGraphicsPipeline::ReverseSubtract;
            blend.srcAlpha = QRhiGraphicsPipeline::Zero;
            blend.dstAlpha = QRhiGraphicsPipeline::One;
            ps->setTargetBlends({blend});
        }
        ps->setShaderResourceBindings(set);
        ps->setRenderPassDescriptor(pass);
        if (!ps->create()) {
            delete ps;
            return static_cast<QRhiGraphicsPipeline *>(nullptr);
        }
        return ps;
    };
    ao.aoPipeline = pipeline(occlusionShader, ao.aoBindings, ao.rawPass, false);
    ao.blurPipeline = pipeline(blurShader, ao.blurBindings, ao.blurPass, false);
    ao.compositePipeline = pipeline(compositeShader, ao.compositeBindings, ao.compositePass, true);
    if (ao.aoPipeline == nullptr || ao.blurPipeline == nullptr || ao.compositePipeline == nullptr) {
        releaseAmbientOcclusion();
        return false;
    }
    return true;
}

void RhiRenderer::applyAmbientOcclusion() {
    const int quality = ambientOcclusionQuality();
    if (quality == 0 || ambientOcclusionApplied || !sceneProjectionValid || secondaryView
            || currentTarget != TARGET_VIEW || baseProgram != PROGRAM_MAIN
            || view.color2 == nullptr)
        return;
    RhiRenderSurface *s = surface();
    if (s == nullptr || s->frame().commandBuffer == nullptr)
        return;
    ambientOcclusionApplied = true;
    // The opaque passes so far, then the occlusion of what they drew.
    flushTarget();
    AmbientOcclusion &ao = ambientOcclusion;
    const Preset &preset = Presets[quality];
    if (ao.quality != quality || ao.compositeTarget == nullptr
            || ao.size != QSize(std::max(1, view.size.width() / preset.divisor),
                                std::max(1, view.size.height() / preset.divisor))) {
        if (!createAmbientOcclusion(quality))
            return;
    }
    Parameters parameters = {};
    std::copy(sceneProjection, sceneProjection + 4, parameters.projection);
    parameters.slice[0] = sceneDepthRange[0];
    parameters.slice[1] = sceneDepthRange[1];
    parameters.slice[2] = 1.0f / float(view.size.width());
    parameters.slice[3] = 1.0f / float(view.size.height());
    parameters.settings[0] = Radius;
    parameters.settings[1] = float(preset.slices);
    parameters.settings[2] = float(preset.steps);
    parameters.settings[3] = Power;
    parameters.fade[0] = FadeStart;
    parameters.fade[1] = FadeEnd;
    parameters.fade[2] = 0.15f * float(view.size.height());
    parameters.occlusion[0] = 1.0f / float(ao.size.width());
    parameters.occlusion[1] = 1.0f / float(ao.size.height());
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->updateDynamicBuffer(ao.uniforms, 0, sizeof(parameters), &parameters);
    QRhiCommandBuffer *cb = s->frame().commandBuffer;
    auto fullScreen = [cb](QRhiTextureRenderTarget *target, QRhiGraphicsPipeline *pipeline,
                           QRhiShaderResourceBindings *bindings, const QSize &size,
                           QRhiResourceUpdateBatch *updates) {
        cb->beginPass(target, Qt::white, {1.0f, 0}, updates);
        cb->setGraphicsPipeline(pipeline);
        cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
        cb->setShaderResources(bindings);
        cb->draw(3);
        cb->endPass();
    };
    fullScreen(ao.rawTarget, ao.aoPipeline, ao.aoBindings, ao.size, batch);
    if (preset.blur)
        fullScreen(ao.blurTarget, ao.blurPipeline, ao.blurBindings, ao.size, nullptr);
    fullScreen(ao.compositeTarget, ao.compositePipeline, ao.compositeBindings, view.size, nullptr);
}

void RhiRenderer::releaseAmbientOcclusion() {
    AmbientOcclusion &ao = ambientOcclusion;
    delete ao.aoPipeline;
    delete ao.blurPipeline;
    delete ao.compositePipeline;
    delete ao.aoBindings;
    delete ao.blurBindings;
    delete ao.compositeBindings;
    delete ao.rawTarget;
    delete ao.blurTarget;
    delete ao.compositeTarget;
    delete ao.rawPass;
    delete ao.blurPass;
    delete ao.compositePass;
    delete ao.raw;
    delete ao.blurred;
    delete ao.uniforms;
    delete ao.nearest;
    delete ao.linear;
    ao = AmbientOcclusion();
}
