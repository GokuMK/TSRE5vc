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
    // Strongest at the sun: its rays reach far, the sky around stays.
    float glow = pow(clamp((c - sun.w) / (1.0 - sun.w), 0.0, 1.0), 3.0);
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
// shadow map, 700 m about the camera; farther, the terrain's shade, so a hill
// the sun has set behind darkens the haze in front of it); single
// scattering, forward-peaked.
struct VolumeParameters {
    float inverse[16];
    float shadow[16];     // the far shadow map's matrix (OpenGL clip)
    float sun[4];         // towards the sun, haze extinction (1/m)
    float camera[4];      // camera (view space of the queue), y sign
    float projection[4];  // x and y scale, scene near and far
    float bands[4];       // distant near and far, scene and distant depth ends
    float clouds[4];      // cloud shadow map: centre x, z, 1 / extent, on
    float cloudSun[4];    // sun x / y, z / y, the map's plane y, shadow map on
    float march[4];       // distance (m), steps, jitter, blue noise on
    float colour[4];      // the sun's colour (display)
    float cloudsMid[4];   // the mid layer's cloud shadow map: centre x, z; the open share past the maps
    float ground[4];      // the camera above the ground (m), the near layer's extinction at it (1/m), reach (m), scale height (m)
    float shadePlace[4];  // the terrain's shade: its map's north-west corner (x, z), size (m), the view's y less heights
    float shadeParams[4]; // on, the map's reference height (m), 1 / the light's fading band (m), 0
    // Past the cloud shadow maps, the clouds from their weather and far cover
    // map (RhiRenderer::cloudFarMaps; weatherMap.w 0 for none).
    float weatherMap[4];
    float farMap[4];
    float farShift[4];
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
    vec4 cloudsMid;
    vec4 ground;
    vec4 shadePlace;
    vec4 shadeParams;
    vec4 weatherMap;
    vec4 farMap;
    vec4 farShift;
};
layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2) uniform sampler2D cloudShadowMap;
layout(binding = 3) uniform sampler2DShadow shadowMap;
layout(binding = 4) uniform sampler2D terrainShadeMap;
layout(binding = 5) uniform sampler2D weatherTexture;
layout(binding = 6) uniform sampler2D farCover;
// Where each pixel's march starts (the clouds' blue noise, 64 x 64).
layout(binding = 7) uniform sampler2D blueNoise;

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

// Far off, the share of the sun the low and mid layers let through at a
// point of the clouds' plane (km east, north of the camera): their weather
// and the far cover map as the clouds' march reads them, where a cloud is
// little passes.
float farCloudVisible(vec2 offset) {
    float visible = 1.0;
    for (int layer = 0; layer < 2; ++layer) {
        vec2 o = offset - (layer == 0 ? farShift.xy : farShift.zw);
        vec2 m = vec2(o.x, -o.y) * 1000.0;
        vec2 w = clamp((weatherMap.xy + m) / weatherMap.z, vec2(0.5 / weatherMap.w), vec2(1.0 - 0.5 / weatherMap.w));
        vec4 values = textureLod(weatherTexture, vec2((float(layer) + w.x) / 4.0, w.y), 0.0);
        if (values.x <= 0.0)
            continue;
        float even = textureLod(farCover, (farMap.xy + m) / farMap.z, 0.0)[layer];
        visible *= 1.0 - 0.9 * clamp(0.5 + (even - (1.0 - values.x)) * 2.0, 0.0, 1.0);
    }
    return visible;
}

// The sun's light at a point of the air through the clouds (their shadow
// map holds the light under them only).
float cloudVisible(vec3 p) {
    float visible = 1.0;
    if (clouds.w > 0.0 && p.y < cloudSun.z) {
        vec2 c = p.xz + cloudSun.xy * (cloudSun.z - p.y);
        vec2 low = (c - clouds.xy) * clouds.z + 0.5;
        vec2 mid = (c - cloudsMid.xy) * clouds.z + 0.5;
        // Past the maps (a low sun reaches the air through clouds many km
        // away, the clouds of a sunset's rays): the clouds from their weather
        // and far cover map (before, their average: no rays at sunset; their
        // clamped edge before that made all the air shaded or lit at once).
        float inside = smoothstep(0.5, 0.45, max(max(abs(low.x - 0.5), abs(low.y - 0.5)),
                                                 max(abs(mid.x - 0.5), abs(mid.y - 0.5))));
        float through = inside > 0.0 ? textureLod(cloudShadowMap, low, 0.0).r * textureLod(cloudShadowMap, mid, 0.0).g
                                     : 1.0;
        float far = cloudsMid.z;
        if (inside < 1.0 && weatherMap.w > 0.0)
            far = farCloudVisible(vec2(c.x - camera.x, camera.z - c.y) / 1000.0);
        visible *= mix(far, through, inside);
    }
    return visible;
}

// ... and past terrain and objects: near, the far shadow map; the terrain
// farther off (ShadowSampling.glsl's terrainShade).
float shadowVisible(vec3 p) {
    float visible = 1.0;
    if (cloudSun.w > 0.0) {
        vec4 q = shadowMatrix * vec4(p, 1.0);
        vec3 m = q.xyz / q.w * 0.5 + 0.5;
        if (all(greaterThan(m.xy, vec2(0.0))) && all(lessThan(m.xy, vec2(1.0))) && m.z < 1.0)
            visible *= texture(shadowMap, vec3(m.xy, m.z - 0.002));
    }
    if (shadeParams.x > 0.0) {
        vec2 m = (p.xz - shadePlace.xy) / shadePlace.z;
        if (all(greaterThan(m, vec2(0.0))) && all(lessThan(m, vec2(1.0)))) {
            float top = textureLod(terrainShadeMap, m, 0.0).r + shadeParams.y;
            visible *= clamp((p.y - shadePlace.w - top) * shadeParams.z + 1.0, 0.0, 1.0);
        }
    }
    return visible;
}

void main() {
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * camera.w);
    vec4 far = inverse * vec4(ndc, 1.0, 1.0);
    vec3 d = normalize(far.xyz / far.w);
    float scene = sceneDistance(texelFetch(sceneDepth, ivec2(uv * vec2(textureSize(sceneDepth, 0))), 0).r, ndc);
    // The sky far behind: a longer path, so rays under the clouds show.
    float limit = scene > 0.0 ? min(scene, march.x) : march.x * 4.0;
    // Up to the plane under the clouds' base, where their shadow map lies:
    // the shafts are in the air under them. (Stopped at each cloud's own
    // distance, the path changed length at every cloud's outline, in the
    // cloud image's pixels, user 2026-10-12.)
    if (clouds.w > 0.0) {
        float below = cloudSun.z - camera.y;
        if (below > 0.0 && d.y > 1e-4)
            limit = min(limit, below / d.y);
    }
    vec3 s = normalize(sun.xyz);
    float phase = mix(henyeyGreenstein(dot(d, s), 0.7), 1.0 / (4.0 * 3.14159265), 0.3);
    int steps = int(march.y);
    // (blue noise: the interleaved gradient noise's diagonal pattern showed
    // in the rays, their contrast drawn strongly)
    float jitter = march.w > 0.0 ? fract(texelFetch(blueNoise, ivec2(mod(gl_FragCoord.xy, vec2(64.0))), 0).r + march.z)
                                 : fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + march.z);
    float dt = limit / float(steps);
    float sigma = sun.w;
    float light = 0.0, open = 0.0, transmittance = 1.0;
    for (int i = 0; i < steps; ++i) {
        vec3 p = camera.xyz + d * ((float(i) + jitter) * dt);
        float visible = cloudVisible(p) * shadowVisible(p);
        light += transmittance * sigma * dt * visible;
        open += transmittance * sigma * dt;
        transmittance *= exp(-sigma * dt);
    }
    // The haze lying on the ground (ground.y at it, thinning over ground.w of
    // height) over the first ground.z of the view, where trees and buildings
    // cast their shadows into it: its steps closer near the camera. Its light
    // is mostly forward (as droplets' and dust's): it shows towards the sun,
    // rays between dark trunks and crowns, and adds next to nothing
    // elsewhere (the near view's colours keep).
    float nearLight = 0.0;
    if (ground.y > 0.0) {
        float reach = scene > 0.0 ? min(scene, ground.z) : ground.z;
        const int NearSteps = 12;
        float nearTransmittance = 1.0;
        for (int i = 0; i < NearSteps; ++i) {
            float f = (float(i) + jitter) / float(NearSteps);
            float t = reach * f * f;
            float step = reach * 2.0 * f / float(NearSteps);
            vec3 p = camera.xyz + d * t;
            float above = max(ground.x + (p.y - camera.y), 0.0);
            float sigmaNear = ground.y * exp(-above / ground.w);
            nearLight += nearTransmittance * sigmaNear * step * cloudVisible(p) * shadowVisible(p);
            nearTransmittance *= exp(-sigmaNear * step);
        }
        nearLight *= henyeyGreenstein(dot(d, s), 0.85) * 4.0 * 3.14159265;
    }
    // The haze's light with the sun's shadows, and without (the fog's); the
    // near layer's light.
    result = vec4(light * phase * 4.0 * 3.14159265, open * phase * 4.0 * 3.14159265, open, nearLight);
}
)";

// The volumetric shafts over the view: the aerial haze (task 32) already
// holds the haze's light as if the sun reached all of it; where it does not
// (the shadows of clouds, trees, buildings) the view darkens by the blocked
// share, and the lit haze towards the sun adds a little. Premultiplied:
// colour + view * a.
const char *VolumeAddFragment = R"(
layout(binding = 1) uniform sampler2D shafts;

void main() {
    // A pixel's march with its neighbours' (2 x 2, a mip): its noise down, the
    // rays wide.
    vec4 s = textureLod(shafts, uv, 1.0);
    float blocked = s.g > 1e-5 ? clamp(1.0 - s.r / s.g, 0.0, 1.0) : 0.0;
    // Against the surroundings' share in shade (the march's result some 30
    // pixels around, by its mips): the shafts' contrast, the air in shade
    // darker than around it, the air lit through the gaps lighter, not the
    // shade the clouds give all the air, which the ambient light already
    // has. (Against the clouds' average cover before: at a low sun most air
    // lies in some cloud's shade, more than they cover, and the lit air never
    // showed.)
    vec4 m = textureLod(shafts, uv, 4.5);
    float around = m.g > 1e-5 ? clamp(1.0 - m.r / m.g, 0.0, 1.0) : 0.0;
    // Only against surroundings of a like path (the haze it crosses, b): at
    // the horizon the terrain's short path next to the sky's long one made
    // a dark band along the hills.
    float like = min(s.b, m.b) / max(max(s.b, m.b), 1e-5);
    float contrast = smoothstep(0.6, 0.9, like);
    float shade = max(blocked - around, 0.0) * contrast;
    float lit = max(around - blocked, 0.0) * contrast;
    // The share of the view the haze makes up over this distance.
    float haze = clamp(s.b, 0.0, 1.0);
    // The haze's light is strongest towards the sun (forward scattering):
    // the shade of its shafts shows mostly there, faintly elsewhere (shafts
    // from every cloud at full strength looked wrong, user 2026-10-12).
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * camera.x);
    vec4 far = inverse * vec4(ndc, 1.0, 1.0);
    float towards = max(dot(normalize(far.xyz / far.w), normalize(direction.xyz)), 0.0);
    float forward = mix(0.2, 1.0, towards * towards * towards);
    // The near layer's light, added (camera.w: its strength).
    // The lit air's light (direction.w), the near layer's (camera.w), added;
    // thin haze's rays kept visible (its square root).
    // The rays where they fall, not where they run across the sky: on the
    // screen a sun ray at this pixel runs along the sun's direction less its
    // part along the view; steeper than some 35 degrees from the vertical
    // they show, flatter than 60 not (at a low sun the clouds' shadows drew
    // long near-horizontal stripes across the sky and its clouds, user, CMK
    // 5:00; under broken clouds the rays fall from the gaps).
    vec3 view = normalize(far.xyz / far.w);
    vec3 along = normalize(direction.xyz) - view * dot(view, normalize(direction.xyz));
    vec3 upright = vec3(0.0, 1.0, 0.0) - view * view.y;
    float vertical = abs(dot(along, upright)) / max(length(along) * length(upright), 1e-5);
    float rays = sqrt(haze) * forward * smoothstep(0.5, 0.82, vertical);
    result = vec4(colour.rgb * (s.r * sun.z + s.a * camera.w + lit * rays * direction.w),
                  1.0 - clamp(shade * rays * sun.w, 0.0, 0.35));
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
    delete l.mipLinear;
    delete l.white;
    delete l.volumeUniforms;
    delete l.volumeBindings;
    delete l.volumeAddBindings;
    delete l.volumePipeline;
    delete l.volumeAddPipeline;
    if (l.whiteUpload != nullptr)
        l.whiteUpload->release();
    l = LightShafts();
}

bool RhiRenderer::createLightShafts(QRhiTexture *cloudTexture) {
    LightShafts &l = lightShafts;
    const QSize size(std::max(1, view.size.width() / 2), std::max(1, view.size.height() / 2));
    const QRhiTexture *cloudMap = cloudResources.shadow != nullptr && cloudResources.shadowValid
            ? cloudResources.shadow : nullptr;
    const QRhiTexture *sunMap = shadowMaps[2].valid() && Game::shadowsEnabled > 0 ? shadowMaps[2].depth : nullptr;
    const QRhiTexture *shadeMap = terrainShadeReady() ? shadeResources.texture : nullptr;
    const QRhiTexture *weather = cloudMap != nullptr ? cloudResources.weather : nullptr;
    const QRhiTexture *farCover = cloudMap != nullptr ? cloudResources.coverFar : nullptr;
    const QRhiTexture *blue = cloudResources.blue;
    if (l.addPipeline != nullptr && l.size == size && l.viewColor == view.color && l.cloudTexture == cloudTexture
            && l.volumeCloudMap == cloudMap && l.volumeSunMap == sunMap && l.volumeShadeMap == shadeMap
            && l.volumeWeather == weather && l.volumeFarCover == farCover && l.volumeBlue == blue)
        return true;
    // The white texture and its upload, if not done yet, stay.
    QRhiTexture *white = l.white;
    QRhiResourceUpdateBatch *whiteUpload = l.whiteUpload;
    l.white = nullptr;
    l.whiteUpload = nullptr;
    releaseLightShafts();
    l.white = white;
    l.whiteUpload = whiteUpload;
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
    l.blurred = rhi->newTexture(format, size, 1,
                                QRhiTexture::RenderTarget | QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips);
    l.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(ShaftParameters));
    l.linear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                               QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    l.mipLinear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                  QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    if (!l.mask->create() || !l.blurred->create() || !l.uniforms->create() || !l.linear->create()
            || !l.mipLinear->create())
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
    // Without tone mapping the view holds display colours up to 1: the
    // shafts are screened over it (view + shafts (1 - view)), so they brighten
    // without clipping the sky by the sun to white; with it, added.
    const bool screen = view.color->format() == QRhiTexture::RGBA8;
    auto pipeline = [&](const QShader &shader, QRhiShaderResourceBindings *set, QRhiRenderPassDescriptor *pass,
                        bool additive) {
        QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
        ps->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, shader}});
        if (additive) {
            QRhiGraphicsPipeline::TargetBlend b;
            b.enable = true;
            b.srcColor = screen ? QRhiGraphicsPipeline::OneMinusDstColor : QRhiGraphicsPipeline::One;
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
    l.volumeShadeMap = shadeMap;
    l.volumeWeather = weather;
    l.volumeFarCover = farCover;
    l.volumeBlue = blue;
    l.volumeBindings = bindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, l.volumeUniforms),
                                 QRhiShaderResourceBinding::sampledTexture(1, fragment, view.depth, sampler(false, true, true)),
                                 QRhiShaderResourceBinding::sampledTexture(2, fragment, cloudMap != nullptr
                                                                           ? cloudResources.shadow : l.white, l.linear),
                                 QRhiShaderResourceBinding::sampledTexture(3, fragment, sunMap != nullptr
                                                                           ? shadowMaps[2].depth : dummyDepth,
                                                                           shadowSampler),
                                 QRhiShaderResourceBinding::sampledTexture(4, fragment, shadeMap != nullptr
                                                                           ? shadeResources.texture : l.white,
                                                                           l.linear),
                                 QRhiShaderResourceBinding::sampledTexture(5, fragment, weather != nullptr
                                                                           ? cloudResources.weather : l.white,
                                                                           l.linear),
                                 QRhiShaderResourceBinding::sampledTexture(6, fragment, farCover != nullptr
                                                                           ? cloudResources.coverFar : l.white,
                                                                           l.linear),
                                 QRhiShaderResourceBinding::sampledTexture(7, fragment, blue != nullptr
                                                                           ? cloudResources.blue : l.white,
                                                                           sampler(false, false, false))});
    l.volumeAddBindings = bindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, l.uniforms),
                                    QRhiShaderResourceBinding::sampledTexture(1, fragment, l.blurred, l.mipLinear)});
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
    // The whole view (after the exposure measured it).
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
    // Screened over display colours (no tone mapping) they cannot clip and
    // take more; added (tone mapping), 0.4 with the wider glow washed out the
    // clouds by the sun (user 2026-10-12).
    const float strength = view.color->format() == QRhiTexture::RGBA8 ? 0.4f : 0.2f;
    parameters.sun[2] = strength * onScreen * std::clamp((elevation + 1.0f) / 6.0f, 0.0f, 1.0f);
    // The glow the rays start from: 30 degrees around the sun (12 left the
    // shafts short, user 2026-10-12).
    parameters.sun[3] = std::cos(30.0f * float(M_PI) / 180.0f);
    const Atmosphere::Rgb sunLight = Atmosphere::model().sunLight(std::max(0.001f, sky.cameraHeight / 1000.0f),
                                                                  sunDirection.y());
    const float peak = std::max({sunLight[0], sunLight[1], sunLight[2], 1e-4f});
    for (int i = 0; i < 3; ++i)
        parameters.colour[i] = std::pow(sunLight[i] / peak, 1.0f / 2.2f) * sceneScaleDisplay();
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
    // The whole view (after the exposure measured it).
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
    v.sun[3] = std::clamp(sky.shaftHaze, 1e-5f, 0.02f);
    std::copy(viewPosition, viewPosition + 3, v.camera);
    v.camera[3] = rhi->isYUpInFramebuffer() ? 1.0f : -1.0f;
    std::copy(sceneProjection, sceneProjection + 4, v.projection);
    v.bands[0] = 600.0f;
    v.bands[1] = distantFar;
    v.bands[2] = 0.98f;
    v.bands[3] = 0.99f;
    float plane = 0.0f;
    if (cloudShadowPlane(plane) && l.volumeCloudMap != nullptr) {
        cloudShadowCentre(0, v.clouds[0], v.clouds[1]);
        cloudShadowCentre(1, v.cloudsMid[0], v.cloudsMid[1]);
        // Past the maps: the share of the sun the clouds leave open on average.
        v.cloudsMid[2] = 1.0f - 0.85f * std::clamp(sky.overcast, 0.0f, 1.0f);
        v.clouds[2] = 1.0f / (CloudShadowExtent * 1000.0f);
        v.clouds[3] = 1.0f;
        v.cloudSun[0] = sunDirection.x() / sunDirection.y();
        v.cloudSun[1] = sunDirection.z() / sunDirection.y();
        v.cloudSun[2] = viewPosition[1] + plane * 1000.0f - sky.cameraHeight;
    }
    v.cloudSun[3] = l.volumeSunMap != nullptr ? 1.0f : 0.0f;
    if (l.volumeWeather == nullptr || l.volumeFarCover == nullptr || !cloudFarMaps(v.weatherMap, v.farMap, v.farShift))
        v.weatherMap[3] = 0.0f;
    // The haze lying on the ground: ten times the weather's average at it,
    // thinning over 120 m of height, over the first 500 m of the view (the
    // far shafts are the long march's). At a sun 6 degrees up behind pines a
    // glow of lit haze around it (mean 2.9, at most 98 of 255); away from the
    // sun at most 1.
    v.ground[0] = std::max(sky.cameraHeight - sky.groundHeight, 0.0f);
    // With the fog volume's fog (task 33) the haze on the ground is its
    // thinnest case: the layer goes (not with its rain alone).
    const bool fogged = fogResources.filled && fogWanted() && fogFogged();
    v.ground[1] = fogged ? 0.0f : std::clamp(sky.shaftHaze, 0.0f, 0.02f) * 10.0f;
    v.ground[2] = 500.0f;
    v.ground[3] = 120.0f;
    terrainShadePlace(v.shadePlace, v.shadeParams);
    if (l.volumeShadeMap == nullptr)
        v.shadeParams[0] = 0.0f;
    v.march[0] = 1500.0f;
    // 24 steps: as 40 to within 0.03 of 255, 0.6 ms less on the Steam Deck.
    v.march[1] = 24.0f;
    v.march[2] = cloudsApplied && clouds.still ? 0.5f : float(std::fmod(0.5 + cloudResources.frame * 0.618034, 1.0));
    v.march[3] = l.volumeBlue != nullptr ? 1.0f : 0.0f;
    ShaftParameters parameters = {};
    // The haze's light; the sun's colour as the screen-space shafts.
    const float fade = std::clamp((elevation - 1.0f) / 5.0f, 0.0f, 1.0f);
    parameters.sun[2] = 0.1f * fade;
    // How much of the haze's light the shadows take away, beside the rays:
    // little (more read as the image darkened around them, user
    // 2026-10-16).
    parameters.sun[3] = 0.8f * fade;
    // For the view's directions (forward scattering, the rays' run).
    parameters.camera[0] = rhi->isYUpInFramebuffer() ? 1.0f : -1.0f;
    // The near layer's light, added.
    parameters.camera[3] = 0.1f * fade;
    std::copy(skyInverse[TARGET_VIEW], skyInverse[TARGET_VIEW] + 16, parameters.inverse);
    parameters.direction[0] = sunDirection.x();
    parameters.direction[1] = sunDirection.y();
    parameters.direction[2] = sunDirection.z();
    // The air lit through the clouds' gaps, lighter than around it: the rays.
    parameters.direction[3] = 6.0f * fade;
    const Atmosphere::Rgb sunLight = Atmosphere::model().sunLight(std::max(0.001f, sky.cameraHeight / 1000.0f),
                                                                  sunDirection.y());
    const float peak = std::max({sunLight[0], sunLight[1], sunLight[2], 1e-4f});
    for (int i = 0; i < 3; ++i)
        parameters.colour[i] = std::pow(sunLight[i] / peak, 1.0f / 2.2f) * sceneScaleDisplay();
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
    // Its mips: the surroundings' average for the contrast.
    QRhiResourceUpdateBatch *mips = rhi->nextResourceUpdateBatch();
    mips->generateMips(l.blurred);
    fullScreen(l.addTarget, l.volumeAddPipeline, l.volumeAddBindings, view.size, mips);
}
