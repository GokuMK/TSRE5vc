/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Screen-space light shafts of the QRhi renderer (task 28 step 6; Mitchell,
// "Volumetric Light Scattering as a Post-Process", GPU Gems 3, 2007; Sousa,
// Crysis 2008). After the clouds: at half resolution, a mask of the open sky
// around the sun (the sky's depth, the clouds' transmittance), blurred
// radially towards the sun's place on the screen, then added to the view in
// the sun's colour. Only with the sun above the horizon, on the screen or
// near it.

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/environment/Atmosphere.h>

namespace {

struct ShaftParameters {
    float sun[4];      // the sun's place (uv), strength, cos of the mask's radius
    float colour[4];   // the sun's colour (display), 0
    float camera[4];   // y sign, aspect, sky depth start, 0
    float inverse[16]; // directions from the screen
    float direction[4];
};

const char *ShaftHeader = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Shafts {
    vec4 sun;
    vec4 colour;
    vec4 camera;
    mat4 inverse;
    vec4 direction;
};
)";

// Open sky near the sun: bright, dimmed by the clouds in front of it.
const char *MaskFragment = R"(
layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2) uniform sampler2D clouds;

void main() {
    float depth = textureLod(sceneDepth, uv, 0.0).r;
    float open = depth >= camera.z ? 1.0 : 0.0;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * camera.x);
    vec4 far = inverse * vec4(ndc, 1.0, 1.0);
    vec3 d = normalize(far.xyz / far.w);
    float c = dot(d, normalize(direction.xyz));
    // A glow around the sun, wider than its disc.
    float glow = pow(clamp((c - sun.w) / (1.0 - sun.w), 0.0, 1.0), 2.0);
    float transmittance = textureLod(clouds, uv, 0.0).a;
    result = vec4(vec3(open * transmittance * glow), 1.0);
}
)";

// Steps towards the sun, each weaker.
const char *BlurFragment = R"(
layout(binding = 1) uniform sampler2D mask;

void main() {
    const int Samples = 64;
    vec2 step = (sun.xy - uv) / float(Samples);
    vec2 p = uv;
    float decay = 1.0;
    float sum = 0.0;
    for (int i = 0; i < Samples; ++i) {
        sum += textureLod(mask, p, 0.0).r * decay;
        decay *= 0.98;
        p += step;
    }
    result = vec4(vec3(sum / float(Samples) * 3.0), 1.0);
}
)";

// Added to the view.
const char *AddFragment = R"(
layout(binding = 1) uniform sampler2D shafts;
layout(binding = 2) uniform sampler2D mask;

void main() {
    // The open sky by the sun glows already (bloom): the shafts show where
    // something stands in front of it.
    float s = textureLod(shafts, uv, 0.0).r * sun.z * (1.0 - 0.85 * textureLod(mask, uv, 0.0).r);
    result = vec4(colour.rgb * s, 0.0);
}
)";

QByteArray shaftSource(const char *body) {
    return QByteArray(ShaftHeader) + body;
}

}

QList<QByteArray> RhiRenderer::lightShaftShaders() {
    return {shaftSource(MaskFragment), shaftSource(BlurFragment), shaftSource(AddFragment)};
}

void RhiRenderer::releaseLightShafts() {
    LightShafts &l = lightShafts;
    delete l.maskPipeline;
    delete l.blurPipeline;
    delete l.addPipeline;
    delete l.maskBindings;
    delete l.blurBindings;
    delete l.addBindings;
    delete l.maskTarget;
    delete l.blurTarget;
    delete l.addTarget;
    delete l.maskPass;
    delete l.blurPass;
    delete l.addPass;
    delete l.mask;
    delete l.blurred;
    delete l.uniforms;
    delete l.linear;
    delete l.white;
    l = LightShafts();
}

bool RhiRenderer::createLightShafts(QRhiTexture *cloudTexture) {
    LightShafts &l = lightShafts;
    const QSize size(std::max(1, view.size.width() / 2), std::max(1, view.size.height() / 2));
    if (l.addPipeline != nullptr && l.size == size && l.viewColor == view.color && l.cloudTexture == cloudTexture)
        return true;
    QRhiTexture *white = l.white;
    l.white = nullptr;
    releaseLightShafts();
    l.white = white;
    l.size = size;
    l.viewColor = view.color;
    l.cloudTexture = cloudTexture;
    if (l.white == nullptr) {
        // No clouds: nothing in front of the sky (alpha 1).
        l.white = rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
        l.white->create();
        QImage one(1, 1, QImage::Format_RGBA8888);
        one.fill(Qt::white);
        QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
        batch->uploadTexture(l.white, one);
        l.whiteUpload = batch;
    }
    const auto format = rhi->isTextureFormatSupported(QRhiTexture::RGBA16F) ? QRhiTexture::RGBA16F
                                                                             : QRhiTexture::RGBA8;
    l.mask = rhi->newTexture(format, size, 1, QRhiTexture::RenderTarget);
    l.blurred = rhi->newTexture(format, size, 1, QRhiTexture::RenderTarget);
    l.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(ShaftParameters));
    l.linear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                               QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    if (!l.mask->create() || !l.blurred->create() || !l.uniforms->create() || !l.linear->create())
        return false;
    auto target = [&](QRhiTexture *texture, bool preserve, QRhiTextureRenderTarget *&out,
                      QRhiRenderPassDescriptor *&pass) {
        out = rhi->newTextureRenderTarget({QRhiColorAttachment(texture)},
                                          preserve ? QRhiTextureRenderTarget::PreserveColorContents
                                                   : QRhiTextureRenderTarget::Flags());
        pass = out->newCompatibleRenderPassDescriptor();
        out->setRenderPassDescriptor(pass);
        return out->create();
    };
    if (!target(l.mask, false, l.maskTarget, l.maskPass) || !target(l.blurred, false, l.blurTarget, l.blurPass)
            || !target(view.color, true, l.addTarget, l.addPass))
        return false;
    const auto fragment = QRhiShaderResourceBinding::FragmentStage;
    auto bindings = [&](std::initializer_list<QRhiShaderResourceBinding> list) {
        QRhiShaderResourceBindings *set = rhi->newShaderResourceBindings();
        set->setBindings(list.begin(), list.end());
        set->create();
        return set;
    };
    l.maskBindings = bindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, l.uniforms),
                               QRhiShaderResourceBinding::sampledTexture(1, fragment, view.depth, l.linear),
                               QRhiShaderResourceBinding::sampledTexture(2, fragment, cloudTexture != nullptr
                                                                         ? cloudTexture : l.white, l.linear)});
    l.blurBindings = bindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, l.uniforms),
                               QRhiShaderResourceBinding::sampledTexture(1, fragment, l.mask, l.linear)});
    l.addBindings = bindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, l.uniforms),
                              QRhiShaderResourceBinding::sampledTexture(1, fragment, l.blurred, l.linear),
                              QRhiShaderResourceBinding::sampledTexture(2, fragment, l.mask, l.linear)});
    static QShader vertex, mask, blur, add;
    if (!vertex.isValid()) {
        vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
        mask = bakeInline(shaftSource(MaskFragment).constData(), QShader::FragmentStage, rhi);
        blur = bakeInline(shaftSource(BlurFragment).constData(), QShader::FragmentStage, rhi);
        add = bakeInline(shaftSource(AddFragment).constData(), QShader::FragmentStage, rhi);
    }
    auto pipeline = [&](const QShader &shader, QRhiShaderResourceBindings *set, QRhiRenderPassDescriptor *pass,
                        bool additive) {
        QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
        ps->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, shader}});
        if (additive) {
            QRhiGraphicsPipeline::TargetBlend b;
            b.enable = true;
            b.srcColor = QRhiGraphicsPipeline::One;
            b.dstColor = QRhiGraphicsPipeline::One;
            b.srcAlpha = QRhiGraphicsPipeline::Zero;
            b.dstAlpha = QRhiGraphicsPipeline::One;
            ps->setTargetBlends({b});
        }
        ps->setShaderResourceBindings(set);
        ps->setRenderPassDescriptor(pass);
        if (!ps->create()) {
            delete ps;
            return static_cast<QRhiGraphicsPipeline *>(nullptr);
        }
        return ps;
    };
    l.maskPipeline = pipeline(mask, l.maskBindings, l.maskPass, false);
    l.blurPipeline = pipeline(blur, l.blurBindings, l.blurPass, false);
    l.addPipeline = pipeline(add, l.addBindings, l.addPass, true);
    return l.maskPipeline != nullptr && l.blurPipeline != nullptr && l.addPipeline != nullptr;
}

void RhiRenderer::applyLightShafts() {
    if (shaftsApplied || !Game::lightShafts || secondaryView || currentTarget != TARGET_VIEW
            || baseProgram != PROGRAM_MAIN || !sceneProjectionValid || !skyInverseValid[TARGET_VIEW])
        return;
    RhiRenderSurface *surfaceNow = surface();
    if (surfaceNow == nullptr || surfaceNow->frame().commandBuffer == nullptr || !view.valid())
        return;
    shaftsApplied = true;
    // The sun on the screen.
    const float length = std::sqrt(sky.sun[0] * sky.sun[0] + sky.sun[1] * sky.sun[1] + sky.sun[2] * sky.sun[2]);
    if (length <= 0.0f)
        return;
    const QVector3D sunDirection(sky.sun[0] / length, sky.sun[1] / length, sky.sun[2] / length);
    const float elevation = std::asin(std::clamp(sunDirection.y(), -1.0f, 1.0f)) * 180.0f / float(M_PI);
    if (elevation < -1.0f)
        return;
    const QMatrix4x4 forward = QMatrix4x4(skyForward[TARGET_VIEW]).transposed();
    const QVector4D clip = forward.map(QVector4D(sunDirection, 0.0f));
    if (clip.w() <= 0.0f)
        return;
    const float ySign = rhi->isYUpInFramebuffer() ? 1.0f : -1.0f;
    const float nx = clip.x() / clip.w(), ny = clip.y() / clip.w();
    // Full on the screen, gone a third of the screen past its edge.
    const float outside = std::max(std::abs(nx), std::abs(ny));
    const float onScreen = std::clamp((1.6f - outside) / 0.6f, 0.0f, 1.0f);
    if (onScreen <= 0.0f)
        return;
    // Everything before: the opaque passes, the sky, the clouds.
    flushTarget();
    const bool clouds = cloudsApplied && cloudResources.history[cloudResources.current] != nullptr;
    if (!createLightShafts(clouds ? cloudResources.history[cloudResources.current] : nullptr))
        return;
    LightShafts &l = lightShafts;
    ShaftParameters parameters = {};
    parameters.sun[0] = nx * 0.5f + 0.5f;
    parameters.sun[1] = ny * ySign * 0.5f + 0.5f;
    // Weaker towards the horizon and as the sun leaves the screen.
    parameters.sun[2] = 0.4f * onScreen * std::clamp((elevation + 1.0f) / 6.0f, 0.0f, 1.0f);
    parameters.sun[3] = std::cos(12.0f * float(M_PI) / 180.0f);
    const Atmosphere::Rgb sunLight = Atmosphere::model().sunLight(std::max(0.001f, sky.cameraHeight / 1000.0f),
                                                                  sunDirection.y());
    const float peak = std::max({sunLight[0], sunLight[1], sunLight[2], 1e-4f});
    for (int i = 0; i < 3; ++i)
        parameters.colour[i] = std::pow(sunLight[i] / peak, 1.0f / 2.2f);
    parameters.camera[0] = ySign;
    parameters.camera[2] = 0.99f;
    std::copy(skyInverse[TARGET_VIEW], skyInverse[TARGET_VIEW] + 16, parameters.inverse);
    parameters.direction[0] = sunDirection.x();
    parameters.direction[1] = sunDirection.y();
    parameters.direction[2] = sunDirection.z();
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    if (l.whiteUpload != nullptr) {
        batch->merge(l.whiteUpload);
        l.whiteUpload->release();
        l.whiteUpload = nullptr;
    }
    batch->updateDynamicBuffer(l.uniforms, 0, sizeof(parameters), &parameters);
    QRhiCommandBuffer *cb = surfaceNow->frame().commandBuffer;
    auto fullScreen = [cb](QRhiTextureRenderTarget *target, QRhiGraphicsPipeline *pipeline,
                           QRhiShaderResourceBindings *bindings, const QSize &size, QRhiResourceUpdateBatch *&updates) {
        cb->beginPass(target, Qt::black, {1.0f, 0}, updates);
        updates = nullptr;
        cb->setGraphicsPipeline(pipeline);
        cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
        cb->setShaderResources(bindings);
        cb->draw(3);
        cb->endPass();
    };
    fullScreen(l.maskTarget, l.maskPipeline, l.maskBindings, l.size, batch);
    fullScreen(l.blurTarget, l.blurPipeline, l.blurBindings, l.size, batch);
    fullScreen(l.addTarget, l.addPipeline, l.addBindings, view.size, batch);
}
