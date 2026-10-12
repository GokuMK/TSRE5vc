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
#include <tsre/ogl/GLUU.h>
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
        decay *= 0.99;
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

// Volumetric (step 8): a march through the low haze in front of the camera
// up to the scene's depth or 1.5 km, the sun's light at each step through
// the clouds (their shadow map) and past terrain and objects (the far
// shadow map); single scattering, forward-peaked.
struct VolumeParameters {
    float inverse[16];
    float shadow[16];     // the far shadow map's matrix (OpenGL clip)
    float sun[4];         // towards the sun, haze extinction (1/m)
    float camera[4];      // camera (view space of the queue), y sign
    float projection[4];  // x and y scale, scene near and far
    float bands[4];       // distant near and far, scene and distant depth ends
    float clouds[4];      // cloud shadow map: centre x, z, 1 / extent, on
    float cloudSun[4];    // sun x / y, z / y, the map's plane y, shadow map on
    float march[4];       // distance (m), steps, jitter, clouds' distance on
    float colour[4];      // the sun's colour (display)
};

const char *VolumeFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Volume {
    mat4 inverse;
    mat4 shadowMatrix;
    vec4 sun;
    vec4 camera;
    vec4 projection;
    vec4 bands;
    vec4 clouds;
    vec4 cloudSun;
    vec4 march;
    vec4 colour;
};
layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2) uniform sampler2D cloudShadowMap;
layout(binding = 3) uniform sampler2DShadow shadowMap;
// The clouds' distance (km), where there are clouds (march.w > 0).
layout(binding = 4) uniform sampler2D cloudDistance;

float sceneDistance(float depth, vec2 ndc) {
    float viewZ;
    if (depth >= bands.w)
        return -1.0;
    if (depth >= bands.z) {
        float z = (depth - bands.z) / (bands.w - bands.z) * 2.0 - 1.0;
        viewZ = 2.0 * bands.x * bands.y / ((bands.y + bands.x) - z * (bands.y - bands.x));
    } else {
        float z = depth / bands.z * 2.0 - 1.0;
        viewZ = 2.0 * projection.z * projection.w / ((projection.w + projection.z) - z * (projection.w - projection.z));
    }
    return viewZ * length(vec3(ndc.x / projection.x, ndc.y / projection.y, 1.0));
}

float henyeyGreenstein(float c, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(1.0 + g2 - 2.0 * g * c, 1.5));
}

void main() {
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * camera.w);
    vec4 far = inverse * vec4(ndc, 1.0, 1.0);
    vec3 d = normalize(far.xyz / far.w);
    float scene = sceneDistance(texelFetch(sceneDepth, ivec2(uv * vec2(textureSize(sceneDepth, 0))), 0).r, ndc);
    // The sky far behind: a longer path, so rays under the clouds show.
    float limit = scene > 0.0 ? min(scene, march.x) : march.x * 4.0;
    // Up to the clouds, not through them.
    if (march.w > 0.0) {
        float cloud = textureLod(cloudDistance, uv, 0.0).r;
        if (cloud > 0.0)
            limit = min(limit, cloud * 1000.0);
    }
    vec3 s = normalize(sun.xyz);
    float phase = mix(henyeyGreenstein(dot(d, s), 0.7), 1.0 / (4.0 * 3.14159265), 0.3);
    int steps = int(march.y);
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + march.z);
    float dt = limit / float(steps);
    float sigma = sun.w;
    float light = 0.0, open = 0.0, transmittance = 1.0;
    for (int i = 0; i < steps; ++i) {
        vec3 p = camera.xyz + d * ((float(i) + jitter) * dt);
        float visible = 1.0;
        if (clouds.w > 0.0) {
            vec2 c = p.xz + cloudSun.xy * (cloudSun.z - p.y);
            visible *= textureLod(cloudShadowMap, (c - clouds.xy) * clouds.z + 0.5, 0.0).r;
        }
        if (cloudSun.w > 0.0) {
            vec4 q = shadowMatrix * vec4(p, 1.0);
            vec3 m = q.xyz / q.w * 0.5 + 0.5;
            if (all(greaterThan(m.xy, vec2(0.0))) && all(lessThan(m.xy, vec2(1.0))) && m.z < 1.0)
                visible *= texture(shadowMap, vec3(m.xy, m.z - 0.002));
        }
        light += transmittance * sigma * dt * visible;
        open += transmittance * sigma * dt;
        transmittance *= exp(-sigma * dt);
    }
    // The haze's light with the sun's shadows, and without (the fog's).
    result = vec4(light * phase * 4.0 * 3.14159265, open * phase * 4.0 * 3.14159265, open, 1.0);
}
)";

// The volumetric shafts over the view: the fog already holds the haze's
// light as if the sun reached all of it; where it does not (the shadows of
// clouds, trees, buildings) the view darkens by the blocked share, and the
// lit haze towards the sun adds a little. Premultiplied: colour + view * a.
const char *VolumeAddFragment = R"(
layout(binding = 1) uniform sampler2D shafts;

void main() {
    vec3 s = textureLod(shafts, uv, 0.0).rgb;
    float blocked = s.g > 1e-5 ? clamp(1.0 - s.r / s.g, 0.0, 1.0) : 0.0;
    // Against the clouds' average shade (colour.a): the shafts' contrast,
    // not the overcast's dimming, which the ambient light already has.
    float shade = max(blocked - colour.a, 0.0);
    // The share of the view the haze makes up over this distance.
    float haze = clamp(s.b, 0.0, 1.0);
    // The haze's light is strongest towards the sun (forward scattering):
    // the shade of its shafts shows mostly there, faintly elsewhere (shafts
    // from every cloud at full strength looked wrong, user 2026-10-12).
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * camera.x);
    vec4 far = inverse * vec4(ndc, 1.0, 1.0);
    float towards = max(dot(normalize(far.xyz / far.w), normalize(direction.xyz)), 0.0);
    float forward = mix(0.2, 1.0, towards * towards * towards);
    result = vec4(colour.rgb * s.r * sun.z, 1.0 - clamp(shade * haze * sun.w * forward, 0.0, 0.35));
}
)";

QByteArray shaftSource(const char *body) {
    return QByteArray(ShaftHeader) + body;
}

}

QList<QByteArray> RhiRenderer::lightShaftShaders() {
    return {shaftSource(MaskFragment), shaftSource(BlurFragment), shaftSource(AddFragment), QByteArray(VolumeFragment),
            shaftSource(VolumeAddFragment)};
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
    delete l.volumeUniforms;
    delete l.volumeBindings;
    delete l.volumeAddBindings;
    delete l.volumePipeline;
    delete l.volumeAddPipeline;
    l = LightShafts();
}

bool RhiRenderer::createLightShafts(QRhiTexture *cloudTexture) {
    LightShafts &l = lightShafts;
    const QSize size(std::max(1, view.size.width() / 2), std::max(1, view.size.height() / 2));
    const QRhiTexture *cloudMap = cloudResources.shadow != nullptr && cloudResources.shadowValid
            ? cloudResources.shadow : nullptr;
    const QRhiTexture *sunMap = shadowMaps[2].valid() && Game::shadowsEnabled > 0 ? shadowMaps[2].depth : nullptr;
    if (l.addPipeline != nullptr && l.size == size && l.viewColor == view.color && l.cloudTexture == cloudTexture
            && l.volumeCloudMap == cloudMap && l.volumeSunMap == sunMap
            && l.volumeCloudDistance == cloudResources.distanceImage)
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
                               QRhiShaderResourceBinding::sampledTexture(1, fragment, view.depth,
                                                                         sampler(false, true, true)),
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
    // The volumetric mode: the march into the blurred texture, added alone.
    l.volumeUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(VolumeParameters));
    l.volumeUniforms->create();
    l.volumeCloudMap = cloudMap;
    l.volumeSunMap = sunMap;
    l.volumeBindings = bindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, l.volumeUniforms),
                                 QRhiShaderResourceBinding::sampledTexture(1, fragment, view.depth, sampler(false, true, true)),
                                 QRhiShaderResourceBinding::sampledTexture(2, fragment, cloudMap != nullptr
                                                                           ? cloudResources.shadow : l.white, l.linear),
                                 QRhiShaderResourceBinding::sampledTexture(3, fragment, sunMap != nullptr
                                                                           ? shadowMaps[2].depth : dummyDepth,
                                                                           shadowSampler),
                                 QRhiShaderResourceBinding::sampledTexture(4, fragment, cloudResources.distanceImage != nullptr
                                                                           ? cloudResources.distanceImage : l.white,
                                                                           sampler(false, true, true))});
    l.volumeCloudDistance = cloudResources.distanceImage;
    l.volumeAddBindings = bindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, l.uniforms),
                                    QRhiShaderResourceBinding::sampledTexture(1, fragment, l.blurred, l.linear)});
    static QShader volume, volumeAdd;
    if (!volume.isValid()) {
        volume = bakeInline(VolumeFragment, QShader::FragmentStage, rhi);
        volumeAdd = bakeInline(shaftSource(VolumeAddFragment).constData(), QShader::FragmentStage, rhi);
    }
    l.volumePipeline = pipeline(volume, l.volumeBindings, l.blurPass, false);
    {
        QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
        ps->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, volumeAdd}});
        QRhiGraphicsPipeline::TargetBlend b;
        b.enable = true;
        b.srcColor = QRhiGraphicsPipeline::One;
        b.dstColor = QRhiGraphicsPipeline::SrcAlpha;
        b.srcAlpha = QRhiGraphicsPipeline::Zero;
        b.dstAlpha = QRhiGraphicsPipeline::One;
        ps->setTargetBlends({b});
        ps->setShaderResourceBindings(l.volumeAddBindings);
        ps->setRenderPassDescriptor(l.addPass);
        if (!ps->create()) {
            delete ps;
            ps = nullptr;
        }
        l.volumeAddPipeline = ps;
    }
    l.maskPipeline = pipeline(mask, l.maskBindings, l.maskPass, false);
    l.blurPipeline = pipeline(blur, l.blurBindings, l.blurPass, false);
    l.addPipeline = pipeline(add, l.addBindings, l.addPass, true);
    return l.maskPipeline != nullptr && l.blurPipeline != nullptr && l.addPipeline != nullptr
            && l.volumePipeline != nullptr && l.volumeAddPipeline != nullptr;
}

void RhiRenderer::applyLightShafts() {
    if (shaftsApplied || Game::lightShafts <= 0 || secondaryView || currentTarget != TARGET_VIEW
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
    if (Game::lightShafts == 2) {
        applyVolumetricShafts(sunDirection, elevation);
        return;
    }
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
    // The finished cloud image: one texture (the history alternates, which
    // would rebuild everything each frame; the march holds a different
    // pixel of each block every frame).
    if (!createLightShafts(cloudsApplied ? cloudResources.image : nullptr))
        return;
    LightShafts &l = lightShafts;
    ShaftParameters parameters = {};
    parameters.sun[0] = nx * 0.5f + 0.5f;
    parameters.sun[1] = ny * ySign * 0.5f + 0.5f;
    // Weaker towards the horizon and as the sun leaves the screen.
    parameters.sun[2] = 0.4f * onScreen * std::clamp((elevation + 1.0f) / 6.0f, 0.0f, 1.0f);
    // The glow the rays start from: 30 degrees around the sun (12 left the
    // shafts short, user 2026-10-12).
    parameters.sun[3] = std::cos(30.0f * float(M_PI) / 180.0f);
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

void RhiRenderer::applyVolumetricShafts(const QVector3D &sunDirection, float elevation) {
    RhiRenderSurface *surfaceNow = surface();
    if (elevation < 1.0f)
        return;
    // Everything before: the opaque passes, the sky, the clouds.
    flushTarget();
    if (!createLightShafts(cloudsApplied ? cloudResources.image : nullptr))
        return;
    LightShafts &l = lightShafts;
    VolumeParameters v = {};
    std::copy(skyInverse[TARGET_VIEW], skyInverse[TARGET_VIEW] + 16, v.inverse);
    std::copy(gluu->pShadowMatrix2, gluu->pShadowMatrix2 + 16, v.shadow);
    v.sun[0] = sunDirection.x();
    v.sun[1] = sunDirection.y();
    v.sun[2] = sunDirection.z();
    v.sun[3] = std::clamp(sky.haze, 1e-5f, 0.02f);
    std::copy(viewPosition, viewPosition + 3, v.camera);
    v.camera[3] = rhi->isYUpInFramebuffer() ? 1.0f : -1.0f;
    std::copy(sceneProjection, sceneProjection + 4, v.projection);
    v.bands[0] = 600.0f;
    v.bands[1] = distantFar;
    v.bands[2] = 0.98f;
    v.bands[3] = 0.99f;
    float plane = 0.0f;
    if (cloudShadowPlane(plane) && l.volumeCloudMap != nullptr) {
        cloudShadowCentre(v.clouds[0], v.clouds[1]);
        v.clouds[2] = 1.0f / (12.0f * 1000.0f);
        v.clouds[3] = 1.0f;
        v.cloudSun[0] = sunDirection.x() / sunDirection.y();
        v.cloudSun[1] = sunDirection.z() / sunDirection.y();
        v.cloudSun[2] = viewPosition[1] + plane * 1000.0f - sky.cameraHeight;
    }
    v.cloudSun[3] = l.volumeSunMap != nullptr ? 1.0f : 0.0f;
    v.march[0] = 1500.0f;
    v.march[1] = 40.0f;
    v.march[2] = cloudsApplied && clouds.still ? 0.5f : float(std::fmod(0.5 + cloudResources.frame * 0.618034, 1.0));
    v.march[3] = cloudsApplied && cloudResources.distanceImage != nullptr ? 1.0f : 0.0f;
    ShaftParameters parameters = {};
    // The haze's light; the sun's colour as the screen-space shafts.
    const float fade = std::clamp((elevation - 1.0f) / 5.0f, 0.0f, 1.0f);
    parameters.sun[2] = 0.1f * fade;
    // How much of the haze's light the shadows take away (the haze is
    // brighter than the scene behind it, so more than its share).
    parameters.sun[3] = 0.8f * fade;
    // For the view's directions (forward scattering).
    parameters.camera[0] = rhi->isYUpInFramebuffer() ? 1.0f : -1.0f;
    std::copy(skyInverse[TARGET_VIEW], skyInverse[TARGET_VIEW] + 16, parameters.inverse);
    parameters.direction[0] = sunDirection.x();
    parameters.direction[1] = sunDirection.y();
    parameters.direction[2] = sunDirection.z();
    parameters.colour[3] = std::clamp(sky.overcast, 0.0f, 0.9f);
    const Atmosphere::Rgb sunLight = Atmosphere::model().sunLight(std::max(0.001f, sky.cameraHeight / 1000.0f),
                                                                  sunDirection.y());
    const float peak = std::max({sunLight[0], sunLight[1], sunLight[2], 1e-4f});
    for (int i = 0; i < 3; ++i)
        parameters.colour[i] = std::pow(sunLight[i] / peak, 1.0f / 2.2f);
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    if (l.whiteUpload != nullptr) {
        batch->merge(l.whiteUpload);
        l.whiteUpload->release();
        l.whiteUpload = nullptr;
    }
    batch->updateDynamicBuffer(l.volumeUniforms, 0, sizeof(v), &v);
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
    fullScreen(l.blurTarget, l.volumePipeline, l.volumeBindings, l.size, batch);
    fullScreen(l.addTarget, l.volumeAddPipeline, l.volumeAddBindings, view.size, batch);
}
