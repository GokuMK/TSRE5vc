/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Smoke and steam of the QRhi renderer (task 41): ExhaustParticles' puffs in
// the main view, after its opaque passes, before the blended ones (glass and
// water see them):
// 1. their light at their centres, one texel a particle, by a program of the
//    scene's shading (ExhaustLight.fs): the sun through the shadow maps, the
//    clouds' shadow and cover, the sky's light and the lamps', the air and
//    the fog before them;
// 2. each a soft sphere of noisy density, back to front, into a target of
//    half the view's size (High: the view's): its thickness along the
//    pixel's ray (a profile keeping the puff's extinction, its edge eroded
//    by a 3D noise drifting through it with its age), cut where the scene
//    stands in it; lit as a whole (a thin puff scattering the sun once, steam
//    forward, smoke less; a thick one as a ball, its sunlit side) and by the
//    sky; the nearest seen puff's distance besides;
// 3. composited over the view, cut at each pixel where the scene stands in
//    front of the nearest puff.

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include "RhiShaderSource.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <tsre/Game.h>
#include <tsre/environment/CloudNoise.h>
#include <tsre/renderer/ExhaustParticles.h>

namespace {

// Particles a row of the particle textures, rows (High's limit).
constexpr int Row = 256;
constexpr int Rows = 40;
constexpr int MaxParticles = Row * Rows;
constexpr int NoiseSize = 32;

// The puffs' pass (std140).
struct DrawParameters {
    float forward[16];   // directions from the camera to clip space (the sky's)
    float inverse[16];   // clip space to directions
    float right[4];      // the camera's right (scene space), clip y's sign
    float up[4];         // its up
    float axis[4];       // its view axis
    float camera[4];     // where it is (scene space)
    float sun[4];        // towards the sun
    float projection[4]; // scene near, far, its depth band's end, view texels a target texel
    float shading[4];    // the noise's scale (periods a radius), its drift (periods a second), 0, 0
};

// The composite (std140).
struct CompositeParameters {
    float projection[4]; // scene near, far, its depth band's end, 0
    float size[4];       // the puffs' target: width, height
};

const char *DrawVertex = R"(#version 440
layout(location = 0) out vec3 ray;
// The puff's centre from the camera and its radius; its extinction (1/m),
// its place in the noise and its age (s); its colour.
layout(location = 1) flat out vec4 centre;
layout(location = 2) flat out vec4 matter;
layout(location = 3) flat out vec3 colour;
layout(std140, binding = 0) uniform Exhaust {
    mat4 forward;
    mat4 inverse;
    vec4 right;
    vec4 up;
    vec4 axis;
    vec4 camera;
    vec4 sun;
    vec4 projection;
    vec4 shading;
};
layout(binding = 1) uniform sampler2D particleCentres;
layout(binding = 2) uniform sampler2D particleMatter;
layout(binding = 3) uniform sampler2D particleTints;
layout(binding = 4) uniform sampler2D lightSun;
layout(binding = 5) uniform sampler2D lightSky;
layout(binding = 6) uniform sampler2D lightFog;
layout(binding = 7) uniform sampler2D lightThrough;
layout(binding = 8) uniform sampler2D lightAir;

// The sun's light and the sky's on a puff: as a white surface's in full sun
// (multiple scattering inside it), and from all round.
const float SunScale = 0.8;
const float SkyScale = 1.0;
// Albedos: steam's droplets white, soot dark (an ENG's colour in its place).
const vec3 SteamAlbedo = vec3(0.95);
const vec3 SmokeAlbedo = vec3(0.22);

// Henyey-Greenstein's phase function, relative to light scattered evenly.
float phase(float g, float c) {
    float d = 1.0 + g * g - 2.0 * g * c;
    return (1.0 - g * g) / (d * sqrt(d));
}

void main() {
    ivec2 at = ivec2(gl_InstanceIndex % 256, gl_InstanceIndex / 256);
    vec4 a = texelFetch(particleCentres, at, 0);
    vec3 rel = a.xyz - camera.xyz;
    float r = a.w;
    centre = vec4(rel, r);
    matter = texelFetch(particleMatter, at, 0);
    // Its light is the whole puff's, the same all over it: shaded point by
    // point, every puff's sunlit rim showed and a plume looked like a stack
    // of discs (the plume's and the scene's shadows are the light pass's). A
    // thin puff scatters the sun once, through half its depth (steam forward,
    // its droplets; smoke less); a thick one as a ball scattering it many
    // times: as much as its sunlit side shows, with the sun behind the camera
    // all of it, against the sun its dark side and a rim.
    vec4 tint = texelFetch(particleTints, at, 0);
    float sigma = matter.x + matter.y;
    float tauPuff = 2.0 * sigma * r;
    float cosTheta = dot(normalize(rel), sun.xyz);
    float steamShare = matter.x / max(sigma, 1e-6);
    float single = mix(phase(0.4, cosTheta), mix(phase(-0.2, cosTheta), phase(0.75, cosTheta), 0.7), steamShare);
    float thick = 1.0 - exp(-0.5 * tauPuff);
    float sunShare = mix(single * exp(-0.5 * tauPuff), 0.2 + 0.4 * (1.0 - cosTheta), thick);
    vec3 albedo = mix(tint.w > 0.5 ? tint.rgb : SmokeAlbedo, SteamAlbedo, steamShare);
    vec3 light = albedo * (texelFetch(lightSun, at, 0).rgb * sunShare * SunScale
                           + texelFetch(lightSky, at, 0).rgb * SkyScale);
    // Through the fog and the air before it (Haze.glsl applyHaze).
    vec4 fog = texelFetch(lightFog, at, 0);
    vec3 through = texelFetch(lightThrough, at, 0).rgb;
    vec3 lit = (light * fog.a + fog.rgb) * through + texelFetch(lightAir, at, 0).rgb * (1.0 - through);
    colour = pow(max(lit, vec3(0.0)), vec3(1.0 / 2.2));
    int corner = gl_VertexIndex;
    float s = (corner == 1 || corner == 4 || corner == 5) ? 1.0 : -1.0;
    float t = (corner == 2 || corner == 3 || corner == 5) ? 1.0 : -1.0;
    float dist = length(rel);
    float depth = dot(rel, axis.xyz);
    vec4 clip;
    if (dist < r * 1.2 + 0.5 || depth < r + 0.3) {
        // At or in the puff, or reaching behind the camera: the whole view.
        clip = vec4(s, t, 0.5, 1.0);
        vec4 far = inverse * vec4(s, t, 1.0, 1.0);
        ray = far.xyz / far.w;
    } else {
        // A square facing the view through its centre, as wide as the cone
        // touching the sphere there, stretched off the view's axis.
        float grow = r * dist / sqrt(dist * dist - r * r) * (dist / depth) * 1.05;
        ray = rel + (right.xyz * s + up.xyz * t) * grow;
        clip = forward * vec4(ray, 0.0);
    }
    gl_Position = vec4(clip.x, clip.y * right.w, 0.5 * clip.w, clip.w);
}
)";

const char *DrawFragment = R"(#version 440
layout(location = 0) in vec3 ray;
layout(location = 1) flat in vec4 centre;
layout(location = 2) flat in vec4 matter;
layout(location = 3) flat in vec3 colour;
// Display colour with premultiplied alpha, back to front; the nearest seen
// puff's distance along the view's axis, as 1 / (1 + d) (kept the largest).
layout(location = 0) out vec4 result;
layout(location = 1) out vec4 front;
layout(std140, binding = 0) uniform Exhaust {
    mat4 forward;
    mat4 inverse;
    vec4 right;
    vec4 up;
    vec4 axis;
    vec4 camera;
    vec4 sun;
    vec4 projection;
    vec4 shading;
};
layout(binding = 9) uniform sampler3D noise;
layout(binding = 10) uniform sampler2D sceneDepth;

// The scene's depth along the view's axis at a pixel of the view (m); far past
// every puff where the scene's band has nothing (the distant band, the sky).
float sceneDepthAt(ivec2 p) {
    float d = texelFetch(sceneDepth, p, 0).r;
    if (d >= projection.z)
        return 1e6;
    float z = d / projection.z * 2.0 - 1.0;
    return 2.0 * projection.x * projection.y / ((projection.y + projection.x) - z * (projection.y - projection.x));
}

void main() {
    vec3 dir = normalize(ray);
    vec3 c = centre.xyz;
    float r = centre.w;
    float along = dot(c, dir);
    // From the centre to the ray's nearest point.
    vec3 q = dir * along - c;
    float x2 = dot(q, q) / (r * r);
    if (x2 >= 1.0)
        discard;
    float chord = r * sqrt(1.0 - x2);
    // The scene behind: the farthest of the view's pixels this one covers
    // (four at half size), so the puff shows where any of them lets it; the
    // composite cuts it at each.
    int scale = int(projection.w);
    ivec2 last = textureSize(sceneDepth, 0) - 1;
    ivec2 full = min(ivec2(gl_FragCoord.xy) * scale, last);
    float depth = sceneDepthAt(full);
    if (scale > 1)
        depth = max(max(depth, sceneDepthAt(min(full + ivec2(1, 0), last))),
                    max(sceneDepthAt(min(full + ivec2(0, 1), last)), sceneDepthAt(min(full + ivec2(1, 1), last))));
    float cosAxis = max(dot(dir, axis.xyz), 1e-3);
    float near = along - chord;
    float far = along + chord;
    // The share of its chord between the camera and the scene.
    float seen = clamp((min(far, depth / cosAxis) - max(near, 0.0)) / (2.0 * chord), 0.0, 1.0);
    if (seen <= 0.0)
        discard;
    // Its density across it, (1 - x^2)^2 keeping the puff's extinction (4 sigma
    // r through the centre), its edge carved by the noise in its own place
    // (billows; which drifts through it with its age), as the clouds' edges
    // are eroded: a dense puff saturates to an even disc inside, so the shape
    // shows at its edge.
    vec3 local = q / r;
    vec3 place = matter.z * vec3(1.0, 0.43, 0.78);
    vec4 n = texture(noise, local * shading.x + place + vec3(0.0, -shading.y, 0.0) * matter.w);
    float billows = n.r * 0.5 + n.g * 0.35 + n.b * 0.15;
    float profile = (1.0 - x2) * (1.0 - x2);
    float shape = clamp((profile - 0.6 * (1.0 - billows)) / 0.4, 0.0, 1.0) * (0.4 + 1.2 * billows);
    float tau = 4.0 * (matter.x + matter.y) * r * shape * seen;
    float alpha = 1.0 - exp(-tau);
    if (alpha < 0.003)
        discard;
    result = vec4(colour * alpha, alpha);
    front = vec4(alpha > 0.05 ? 1.0 / (1.0 + max(near, 0.0) * cosAxis) : 0.0);
}
)";

const char *CompositeFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Composite {
    vec4 projection;
    vec4 size;
};
layout(binding = 1) uniform sampler2D puffs;
layout(binding = 2) uniform sampler2D front;
layout(binding = 3) uniform sampler2D sceneDepth;

void main() {
    vec4 c = textureLod(puffs, uv, 0.0);
    if (c.a < 0.002) {
        result = vec4(0.0);
        return;
    }
    // The nearest puff about this pixel (the texels its filtering reads); the
    // scene in front of it here cuts the puffs (softly, by a margin growing
    // with the distance).
    ivec2 base = ivec2(floor(uv * size.xy - 0.5));
    ivec2 last = ivec2(size.xy) - 1;
    float f = 0.0;
    for (int i = 0; i < 4; ++i)
        f = max(f, texelFetch(front, clamp(base + ivec2(i & 1, i >> 1), ivec2(0), last), 0).r);
    float d = texelFetch(sceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    if (f > 0.0 && d < projection.z) {
        float z = d / projection.z * 2.0 - 1.0;
        float scene = 2.0 * projection.x * projection.y / ((projection.y + projection.x) - z * (projection.y - projection.x));
        float nearest = 1.0 / f - 1.0;
        float margin = 0.3 + 0.02 * nearest;
        c *= clamp((scene - (nearest - margin)) / margin, 0.0, 1.0);
    }
    result = c;
}
)";

template <typename T>
void dropLater(T *&resource) {
    if (resource != nullptr)
        resource->deleteLater();
    resource = nullptr;
}

// A row of a column-major matrix (its first three columns).
void matrixRow(const float *m, int row, float *out) {
    out[0] = m[row];
    out[1] = m[4 + row];
    out[2] = m[8 + row];
}

float dot3(const float *a, const float *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void normalize3(float *v) {
    const float length = std::sqrt(dot3(v, v));
    if (length > 0.0f)
        for (int i = 0; i < 3; ++i)
            v[i] /= length;
}

}

bool RhiRenderer::createExhaust() {
    ExhaustResources &e = exhaustResources;
    if (e.failed)
        return false;
    if (e.lightPipeline != nullptr && e.lightProgram != nullptr)
        return true;
    if (!rhi->isFeatureSupported(QRhi::ThreeDimensionalTextures)
            || !rhi->isTextureFormatSupported(QRhiTexture::RGBA32F)
            || !rhi->isTextureFormatSupported(QRhiTexture::RGBA16F, QRhiTexture::RenderTarget)) {
        e.failed = true;
        return false;
    }
    // The puffs' noise: the clouds' Worley octaves, tiling.
    e.noise = rhi->newTexture(QRhiTexture::RGBA8, NoiseSize, NoiseSize, NoiseSize, 1, QRhiTexture::ThreeDimensional);
    e.noiseSampler = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                     QRhiSampler::Repeat, QRhiSampler::Repeat, QRhiSampler::Repeat);
    e.nearest = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    e.linear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                               QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    bool made = e.noise->create() && e.noiseSampler->create() && e.nearest->create() && e.linear->create();
    for (QRhiTexture *&texture : e.particles) {
        texture = rhi->newTexture(QRhiTexture::RGBA32F, QSize(Row, Rows));
        made = made && texture->create();
    }
    QRhiTextureRenderTargetDescription lights;
    QVarLengthArray<QRhiColorAttachment, 5> attachments;
    for (QRhiTexture *&texture : e.light) {
        texture = rhi->newTexture(QRhiTexture::RGBA16F, QSize(Row, Rows), 1, QRhiTexture::RenderTarget);
        made = made && texture->create();
        attachments.append(QRhiColorAttachment(texture));
    }
    if (!made) {
        releaseExhaust();
        e.failed = true;
        return false;
    }
    const std::vector<quint8> noise = CloudNoise::detail(NoiseSize);
    QVarLengthArray<QRhiTextureUploadEntry, NoiseSize> slices;
    const qsizetype sliceBytes = qsizetype(NoiseSize) * NoiseSize * 4;
    for (int z = 0; z < NoiseSize; ++z) {
        QRhiTextureSubresourceUploadDescription slice(noise.data() + z * sliceBytes, quint32(sliceBytes));
        slice.setSourceSize(QSize(NoiseSize, NoiseSize));
        slices.append(QRhiTextureUploadEntry(z, 0, slice));
    }
    QRhiTextureUploadDescription upload;
    upload.setEntries(slices.cbegin(), slices.cend());
    // Uploaded with the first frame's particles (applyExhaust).
    e.uploads = rhi->nextResourceUpdateBatch();
    e.uploads->uploadTexture(e.noise, upload);
    lights.setColorAttachments(attachments.cbegin(), attachments.cend());
    e.lightTarget = rhi->newTextureRenderTarget(lights);
    e.lightPass = e.lightTarget->newCompatibleRenderPassDescriptor();
    e.lightTarget->setRenderPassDescriptor(e.lightPass);
    e.lightProgram = new RhiProgram();
    e.lightProgram->kind = RhiProgram::OTHER;
    e.lightProgram->source = &context->program("ExhaustLight", "ExhaustLight", {});
    if (!e.lightTarget->create() || !e.lightProgram->valid()) {
        releaseExhaust();
        e.failed = true;
        return false;
    }
    reflect(*e.lightProgram);
    e.lightUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                     quint32(e.lightProgram->block.size()));
    e.drawUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(DrawParameters));
    e.compositeUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                         sizeof(CompositeParameters));
    if (!e.lightUniforms->create() || !e.drawUniforms->create() || !e.compositeUniforms->create()) {
        releaseExhaust();
        e.failed = true;
        return false;
    }
    return true;
}

bool RhiRenderer::createExhaustImage() {
    ExhaustResources &e = exhaustResources;
    const int scale = exhaustQuality >= 2 ? 1 : 2;
    const QSize size((view.size.width() + scale - 1) / scale, (view.size.height() + scale - 1) / scale);
    if (e.drawPipeline != nullptr && e.size == size && e.scale == scale && e.viewColor == view.color
            && e.viewDepth == view.depth)
        return true;
    dropLater(e.drawPipeline);
    dropLater(e.drawBindings);
    dropLater(e.imageTarget);
    dropLater(e.imagePass);
    dropLater(e.image);
    dropLater(e.front);
    dropLater(e.compositePipeline);
    dropLater(e.compositeBindings);
    dropLater(e.compositeTarget);
    dropLater(e.compositePass);
    e.size = size;
    e.scale = scale;
    e.viewColor = view.color;
    e.viewDepth = view.depth;
    const QRhiTexture::Format frontFormat = rhi->isTextureFormatSupported(QRhiTexture::R16F, QRhiTexture::RenderTarget)
            ? QRhiTexture::R16F : QRhiTexture::RGBA16F;
    e.image = rhi->newTexture(QRhiTexture::RGBA16F, size, 1, QRhiTexture::RenderTarget);
    e.front = rhi->newTexture(frontFormat, size, 1, QRhiTexture::RenderTarget);
    if (!e.image->create() || !e.front->create())
        return false;
    QRhiTextureRenderTargetDescription puffs;
    puffs.setColorAttachments({QRhiColorAttachment(e.image), QRhiColorAttachment(e.front)});
    e.imageTarget = rhi->newTextureRenderTarget(puffs);
    e.imagePass = e.imageTarget->newCompatibleRenderPassDescriptor();
    e.imageTarget->setRenderPassDescriptor(e.imagePass);
    e.compositeTarget = rhi->newTextureRenderTarget({QRhiColorAttachment(view.color)},
                                                    QRhiTextureRenderTarget::PreserveColorContents);
    e.compositePass = e.compositeTarget->newCompatibleRenderPassDescriptor();
    e.compositeTarget->setRenderPassDescriptor(e.compositePass);
    if (!e.imageTarget->create() || !e.compositeTarget->create())
        return false;
    static QShader drawVertex, drawFragment, compositeVertex, compositeFragment;
    if (!drawVertex.isValid()) {
        drawVertex = bakeInline(DrawVertex, QShader::VertexStage, rhi);
        drawFragment = bakeInline(DrawFragment, QShader::FragmentStage, rhi);
        compositeVertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
        compositeFragment = bakeInline(CompositeFragment, QShader::FragmentStage, rhi);
    }
    const auto vertex = QRhiShaderResourceBinding::VertexStage;
    const auto fragment = QRhiShaderResourceBinding::FragmentStage;
    e.drawBindings = rhi->newShaderResourceBindings();
    e.drawBindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, vertex | fragment, e.drawUniforms),
            QRhiShaderResourceBinding::sampledTexture(1, vertex, e.particles[0], e.nearest),
            QRhiShaderResourceBinding::sampledTexture(2, vertex, e.particles[1], e.nearest),
            QRhiShaderResourceBinding::sampledTexture(3, vertex, e.particles[2], e.nearest),
            QRhiShaderResourceBinding::sampledTexture(4, vertex, e.light[0], e.nearest),
            QRhiShaderResourceBinding::sampledTexture(5, vertex, e.light[1], e.nearest),
            QRhiShaderResourceBinding::sampledTexture(6, vertex, e.light[2], e.nearest),
            QRhiShaderResourceBinding::sampledTexture(7, vertex, e.light[3], e.nearest),
            QRhiShaderResourceBinding::sampledTexture(8, vertex, e.light[4], e.nearest),
            QRhiShaderResourceBinding::sampledTexture(9, fragment, e.noise, e.noiseSampler),
            QRhiShaderResourceBinding::sampledTexture(10, fragment, view.depth, e.nearest)});
    if (!e.drawBindings->create())
        return false;
    QRhiGraphicsPipeline *draw = rhi->newGraphicsPipeline();
    draw->setShaderStages({{QRhiShaderStage::Vertex, drawVertex}, {QRhiShaderStage::Fragment, drawFragment}});
    draw->setVertexInputLayout(QRhiVertexInputLayout());
    // Back to front over what is there; the nearest kept.
    QRhiGraphicsPipeline::TargetBlend over;
    over.enable = true;
    over.srcColor = QRhiGraphicsPipeline::One;
    over.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    over.srcAlpha = QRhiGraphicsPipeline::One;
    over.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    QRhiGraphicsPipeline::TargetBlend nearest;
    nearest.enable = true;
    nearest.srcColor = nearest.dstColor = nearest.srcAlpha = nearest.dstAlpha = QRhiGraphicsPipeline::One;
    nearest.opColor = nearest.opAlpha = QRhiGraphicsPipeline::Max;
    draw->setTargetBlends({over, nearest});
    draw->setShaderResourceBindings(e.drawBindings);
    draw->setRenderPassDescriptor(e.imagePass);
    if (!draw->create()) {
        delete draw;
        return false;
    }
    e.drawPipeline = draw;
    e.compositeBindings = rhi->newShaderResourceBindings();
    e.compositeBindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, fragment, e.compositeUniforms),
            QRhiShaderResourceBinding::sampledTexture(1, fragment, e.image, e.linear),
            QRhiShaderResourceBinding::sampledTexture(2, fragment, e.front, e.nearest),
            QRhiShaderResourceBinding::sampledTexture(3, fragment, view.depth, e.nearest)});
    if (!e.compositeBindings->create())
        return false;
    QRhiGraphicsPipeline *composite = rhi->newGraphicsPipeline();
    composite->setShaderStages({{QRhiShaderStage::Vertex, compositeVertex},
                                {QRhiShaderStage::Fragment, compositeFragment}});
    composite->setVertexInputLayout(QRhiVertexInputLayout());
    // Premultiplied over the view, its alpha kept.
    QRhiGraphicsPipeline::TargetBlend onto;
    onto.enable = true;
    onto.srcColor = QRhiGraphicsPipeline::One;
    onto.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    onto.srcAlpha = QRhiGraphicsPipeline::Zero;
    onto.dstAlpha = QRhiGraphicsPipeline::One;
    composite->setTargetBlends({onto});
    composite->setShaderResourceBindings(e.compositeBindings);
    composite->setRenderPassDescriptor(e.compositePass);
    if (!composite->create()) {
        delete composite;
        return false;
    }
    e.compositePipeline = composite;
    return true;
}

void RhiRenderer::applyExhaust() {
    if (exhaustApplied || secondaryView || currentTarget != TARGET_VIEW || baseProgram != PROGRAM_MAIN
            || !view.valid() || plainImage || !sceneProjectionValid || !skyInverseValid[TARGET_VIEW])
        return;
    exhaustApplied = true;
    const std::vector<ExhaustParticles::Particle> &live = ExhaustParticles::instance().particles();
    if (live.empty() || exhaustQuality <= 0)
        return;
    RhiRenderSurface *surfaceNow = surface();
    if (surfaceNow == nullptr || surfaceNow->frame().commandBuffer == nullptr)
        return;
    ExhaustResources &e = exhaustResources;
    // The view's axes from its directions to clip space: its rows (the
    // projection's sign kept), the axis w's.
    const float *forward = skyForward[TARGET_VIEW];
    float right[3], up[3], axis[3];
    matrixRow(forward, 0, right);
    matrixRow(forward, 1, up);
    matrixRow(forward, 3, axis);
    normalize3(axis);
    for (float *v : {right, up}) {
        const float along = dot3(v, axis);
        for (int i = 0; i < 3; ++i)
            v[i] -= along * axis[i];
        normalize3(v);
    }
    // The puffs in view (their spheres against the view's sides), far to near.
    float planes[4][3];
    for (int k = 0; k < 4; ++k) {
        float row[3];
        matrixRow(forward, k / 2, row);
        const float sign = (k & 1) ? -1.0f : 1.0f;
        float w[3];
        matrixRow(forward, 3, w);
        for (int i = 0; i < 3; ++i)
            planes[k][i] = w[i] + sign * row[i];
        normalize3(planes[k]);
    }
    const float reach = sceneProjection[3];
    e.order.clear();
    e.keys.resize(live.size());
    for (size_t i = 0; i < live.size(); ++i) {
        const ExhaustParticles::Particle &p = live[i];
        const float rel[3] = {p.position[0] - viewPosition[0], p.position[1] - viewPosition[1],
                              p.position[2] - viewPosition[2]};
        const float r = p.radius;
        if (dot3(rel, axis) < -r || dot3(rel, rel) > (reach + r) * (reach + r))
            continue;
        bool inside = true;
        for (int k = 0; k < 4 && inside; ++k)
            inside = dot3(planes[k], rel) > -r;
        if (!inside)
            continue;
        e.keys[i] = dot3(rel, rel);
        e.order.push_back(int(i));
    }
    if (e.order.empty())
        return;
    std::sort(e.order.begin(), e.order.end(), [&e](int a, int b) { return e.keys[size_t(a)] > e.keys[size_t(b)]; });
    if (int(e.order.size()) > MaxParticles)
        e.order.erase(e.order.begin(), e.order.end() - MaxParticles);
    const int count = int(e.order.size());
    const int rows = (count + Row - 1) / Row;
    flushTarget();
    if (!createExhaust() || !createExhaustImage())
        return;
    // The particles' texels, rows of 256.
    for (std::vector<float> &band : e.data)
        band.assign(size_t(rows) * Row * 4, 0.0f);
    for (int k = 0; k < count; ++k) {
        const ExhaustParticles::Particle &p = live[size_t(e.order[size_t(k)])];
        float *centre = e.data[0].data() + k * 4;
        float *matter = e.data[1].data() + k * 4;
        float *tint = e.data[2].data() + k * 4;
        std::copy(p.position, p.position + 3, centre);
        centre[3] = p.radius;
        matter[0] = p.steamDensity;
        matter[1] = p.smokeDensity;
        matter[2] = p.seed;
        matter[3] = p.age;
        // The ENG's colour (ARGB), linear; none: the renderer's own.
        if (p.color != 0) {
            for (int c = 0; c < 3; ++c)
                tint[c] = std::pow(float((p.color >> (16 - 8 * c)) & 0xff) / 255.0f, 2.2f);
            tint[3] = 1.0f;
        }
    }
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    if (e.uploads != nullptr) {
        batch->merge(e.uploads);
        e.uploads->release();
        e.uploads = nullptr;
    }
    for (int band = 0; band < 3; ++band) {
        QRhiTextureSubresourceUploadDescription rowsUpload(e.data[band].data(),
                                                           quint32(e.data[band].size() * sizeof(float)));
        rowsUpload.setSourceSize(QSize(Row, rows));
        batch->uploadTexture(e.particles[band], QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, rowsUpload)));
    }
    // 1. Their light: the scene's frame values, the frame's textures.
    RhiProgram &program = *e.lightProgram;
    writeFrameUniforms(&program);
    program.setFloat("exhaustCount", float(count));
    std::vector<QRhiTexture *> textures;
    std::vector<QRhiSampler *> samplers;
    for (const RhiProgram::Sampler &slot : program.samplers) {
        QRhiTexture *texture = dummy2D;
        QRhiSampler *slotSampler = e.nearest;
        if (slot.binding == RhiShaderSource::ExhaustParticlesBinding)
            texture = e.particles[0];
        else if (!frameSlot(slot.binding, slot.type, texture, slotSampler))
            texture = slot.type == 1 ? dummyArray : dummy2D;
        textures.push_back(texture);
        samplers.push_back(slotSampler);
    }
    // Its bindings: the block, the particles' centres, the frame's textures in
    // its slots; again when the frame's change.
    if (e.lightBindings == nullptr || textures != e.lightTextures || samplers != e.lightSamplers) {
        dropLater(e.lightBindings);
        QVarLengthArray<QRhiShaderResourceBinding, 32> list;
        list.append(QRhiShaderResourceBinding::uniformBuffer(
                RhiShaderSource::UniformBlockBinding,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, e.lightUniforms));
        for (size_t i = 0; i < program.samplers.size(); ++i)
            list.append(QRhiShaderResourceBinding::sampledTexture(program.samplers[i].binding,
                                                                  QRhiShaderResourceBinding::FragmentStage,
                                                                  textures[i], samplers[i]));
        e.lightBindings = rhi->newShaderResourceBindings();
        e.lightBindings->setBindings(list.cbegin(), list.cend());
        if (!e.lightBindings->create()) {
            dropLater(e.lightBindings);
            return;
        }
        e.lightTextures = textures;
        e.lightSamplers = samplers;
        if (e.lightPipeline == nullptr) {
            QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
            ps->setShaderStages({{QRhiShaderStage::Vertex, program.source->vertex},
                                 {QRhiShaderStage::Fragment, program.source->fragment}});
            ps->setVertexInputLayout(QRhiVertexInputLayout());
            QVarLengthArray<QRhiGraphicsPipeline::TargetBlend, 5> blends(5);
            ps->setTargetBlends(blends.cbegin(), blends.cend());
            ps->setShaderResourceBindings(e.lightBindings);
            ps->setRenderPassDescriptor(e.lightPass);
            if (!ps->create()) {
                delete ps;
                e.failed = true;
                return;
            }
            e.lightPipeline = ps;
        }
    }
    batch->updateDynamicBuffer(e.lightUniforms, 0, quint32(program.block.size()), program.block.data());
    // 2. The puffs.
    DrawParameters d = {};
    std::copy(forward, forward + 16, d.forward);
    std::copy(skyInverse[TARGET_VIEW], skyInverse[TARGET_VIEW] + 16, d.inverse);
    std::copy(right, right + 3, d.right);
    // Clip y as the view's passes take it (RhiRain.cpp).
    d.right[3] = (rhi->isYUpInFramebuffer() ? 1.0f : -1.0f) * (rhi->isYUpInNDC() != rhi->isYUpInFramebuffer() ? -1.0f : 1.0f);
    std::copy(up, up + 3, d.up);
    std::copy(axis, axis + 3, d.axis);
    std::copy(viewPosition, viewPosition + 3, d.camera);
    std::copy(Game::sunLightDirection, Game::sunLightDirection + 3, d.sun);
    normalize3(d.sun);
    d.projection[0] = sceneProjection[2];
    d.projection[1] = sceneProjection[3];
    d.projection[2] = sceneDepthRange[1];
    d.projection[3] = float(e.scale);
    d.shading[0] = 1.1f;
    d.shading[1] = 0.03f;
    batch->updateDynamicBuffer(e.drawUniforms, 0, sizeof(d), &d);
    CompositeParameters c = {};
    c.projection[0] = sceneProjection[2];
    c.projection[1] = sceneProjection[3];
    c.projection[2] = sceneDepthRange[1];
    c.size[0] = float(e.size.width());
    c.size[1] = float(e.size.height());
    batch->updateDynamicBuffer(e.compositeUniforms, 0, sizeof(c), &c);
    QRhiCommandBuffer *cb = surfaceNow->frame().commandBuffer;
    cb->beginPass(e.lightTarget, Qt::black, {1.0f, 0}, batch);
    cb->setGraphicsPipeline(e.lightPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(Row), float(Rows)));
    cb->setShaderResources(e.lightBindings);
    cb->draw(3);
    cb->endPass();
    cb->beginPass(e.imageTarget, QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f), {1.0f, 0});
    cb->setGraphicsPipeline(e.drawPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(e.size.width()), float(e.size.height())));
    cb->setShaderResources(e.drawBindings);
    cb->draw(6, quint32(count));
    cb->endPass();
    // 3. Over the view.
    cb->beginPass(e.compositeTarget, Qt::black, {1.0f, 0});
    cb->setGraphicsPipeline(e.compositePipeline);
    cb->setViewport(QRhiViewport(0, 0, float(view.size.width()), float(view.size.height())));
    cb->setShaderResources(e.compositeBindings);
    cb->draw(3);
    cb->endPass();
}

void RhiRenderer::releaseExhaust() {
    ExhaustResources &e = exhaustResources;
    dropLater(e.lightPipeline);
    dropLater(e.lightBindings);
    dropLater(e.lightTarget);
    dropLater(e.lightPass);
    dropLater(e.lightUniforms);
    for (QRhiTexture *&texture : e.light)
        dropLater(texture);
    for (QRhiTexture *&texture : e.particles)
        dropLater(texture);
    dropLater(e.drawPipeline);
    dropLater(e.drawBindings);
    dropLater(e.imageTarget);
    dropLater(e.imagePass);
    dropLater(e.image);
    dropLater(e.front);
    dropLater(e.drawUniforms);
    dropLater(e.compositePipeline);
    dropLater(e.compositeBindings);
    dropLater(e.compositeTarget);
    dropLater(e.compositePass);
    dropLater(e.compositeUniforms);
    dropLater(e.noise);
    dropLater(e.noiseSampler);
    dropLater(e.nearest);
    dropLater(e.linear);
    if (e.uploads != nullptr)
        e.uploads->release();
    delete e.lightProgram;
    e = ExhaustResources();
}

QList<QByteArray> RhiRenderer::exhaustShaders() {
    return {QByteArray(DrawVertex), QByteArray(DrawFragment), QByteArray(CompositeFragment)};
}
