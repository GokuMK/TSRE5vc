/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Fog lying on the ground (task 33; Wronski, "Volumetric Fog", SIGGRAPH
// 2014; Hillaire, "Physically Based and Unified Volumetric Rendering in
// Frostbite", SIGGRAPH 2015). A volume of cells over the main view, filled
// and summed by two compute passes before the view's geometry; the lit
// shaders read it at their fragments (Haze.glsl), the sky and the clouds
// behind it a full-screen pass after the clouds' composite.

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include "RhiShaderSource.h"
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/ogl/GLUU.h>

namespace {

struct FogParameters {
    float inverse[16];         // directions from the screen
    float previous[16];        // last frame's directions to the volume's x and y
    float shadow0[16];         // the sun's shadow maps: near, middle, far
    float shadow1[16];
    float shadow2[16];
    float camera[4];           // camera (view space), y sign
    float previousCamera[4];   // last frame's camera, the history's share
    float grid[4];             // cells across, down, slices, log(far / near)
    float range[4];            // nearest distance (m), the view's y less heights above sea level, the highest
                               // fog top on the map (m above sea level), 0
    float jitter[4];           // where in its cell each is sampled this frame (0-1)
    float fog[4];              // extinction at the floor (1/m), depth, base, soft edge (m)
    float map[4];              // the floor map's north-west corner (view x, z), its size (m), on
    float light[4];            // towards the light (sun or moon), 0
    float lightColour[4];      // linear
    float ambient[4];          // the sky's light, linear
    float clouds[4];           // cloud shadow map: centre x, z, 1 / extent, on
    float cloudSun[4];         // light x / y, z / y, the map's plane y, 0
    float cloudsMid[4];        // the mid layer's map: centre x, z; the open share past the maps
    float ringSun[4];          // the sun's direction across the ground (x, z), 0, 0
    float ring[8][4];          // the horizon's colour (display), from towards the sun to away
    float shadePlace[4];       // the terrain's shade: its map's north-west corner (x, z), size (m), the view's y
                               // less heights
    float shadeParams[4];      // on, the map's reference height (m), 1 / the light's fading band (m), 0
};

const char *FogBlock = R"(#version 440
layout(std140, binding = 0) uniform Fog {
    mat4 inverse;
    mat4 previous;
    mat4 shadow0;
    mat4 shadow1;
    mat4 shadow2;
    vec4 camera;
    vec4 previousCamera;
    vec4 grid;
    vec4 range;
    vec4 jitter;
    vec4 fog;
    vec4 map;
    vec4 light;
    vec4 lightColour;
    vec4 ambient;
    vec4 clouds;
    vec4 cloudSun;
    vec4 cloudsMid;
    vec4 ringSun;
    vec4 ring[8];
    vec4 shadePlace;
    vec4 shadeParams;
};

// The distance (m) a slice coordinate stands for.
float sliceDistance(float c) {
    return range.x * exp(c / grid.z * grid.w);
}
)";

// Each cell: the fog's density at a point in it, and the light it scatters
// towards the camera; blended with last frame's cell at that point.
const char *InjectCompute = R"(
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(binding = 1) uniform sampler2D floorMap;
layout(binding = 2) uniform sampler2D cloudShadowMap;
layout(binding = 3) uniform sampler2DShadow shadowMap0;
layout(binding = 4) uniform sampler2DShadow shadowMap1;
layout(binding = 5) uniform sampler2DShadow shadowMap2;
layout(binding = 6) uniform sampler3D history;
layout(binding = 7, rgba16f) uniform writeonly image3D cells;
layout(binding = 8) uniform sampler2D terrainShadeMap;

// Extinction (1/m): the weather's fog from the floor up to its top, where the
// floor map pools it (FogFloor), softly edged; none past the map. above: the
// fog over the point (m) and its extinction there, for the light reaching it.
float density(vec3 p, out float above, out float sigmaAbove) {
    above = 0.0;
    sigmaAbove = 0.0;
    if (map.w < 0.5)
        return 0.0;
    vec2 m = (p.xz - map.xy) / map.z;
    if (any(lessThan(m, vec2(0.0))) || any(greaterThan(m, vec2(1.0))))
        return 0.0;
    vec4 cell = textureLod(floorMap, m, 0.0);
    float height = p.y - range.y;
    float top = max(cell.x + fog.y * cell.y, cell.z + fog.y * 0.5 * cell.w);
    float inside = 1.0 - smoothstep(top - fog.w, top + fog.w, height);
    if (fog.z > 0.0)
        inside *= smoothstep(cell.x + fog.z - fog.w, cell.x + fog.z + fog.w, height);
    vec2 edge = min(m, 1.0 - m);
    inside *= smoothstep(0.0, 0.05, min(edge.x, edge.y));
    // Thicker over water.
    sigmaAbove = fog.x * (1.0 + 0.5 * cell.w);
    above = max(top - max(height, cell.x + fog.z), 0.0);
    return sigmaAbove * inside;
}

// The horizon's light in a direction (display), as the aerial haze's.
vec3 horizonLight(vec3 d) {
    vec2 across = d.xz;
    float c = dot(across, across) > 1e-6 ? dot(normalize(across), ringSun.xy) : 1.0;
    float t = acos(clamp(c, -1.0, 1.0)) * (7.0 / 3.14159265);
    int i = min(int(t), 6);
    return mix(ring[i].rgb, ring[i + 1].rgb, t - float(i));
}

float henyeyGreenstein(float c, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(1.0 + g2 - 2.0 * g * c, 1.5));
}

// The light through the clouds (their shadow map, as the light shafts read it).
float cloudVisible(vec3 p) {
    if (clouds.w < 0.5 || p.y >= cloudSun.z)
        return 1.0;
    vec2 c = p.xz + cloudSun.xy * (cloudSun.z - p.y);
    vec2 low = (c - clouds.xy) * clouds.z + 0.5;
    vec2 mid = (c - cloudsMid.xy) * clouds.z + 0.5;
    float inside = smoothstep(0.5, 0.45, max(max(abs(low.x - 0.5), abs(low.y - 0.5)),
                                             max(abs(mid.x - 0.5), abs(mid.y - 0.5))));
    float through = inside > 0.0 ? textureLod(cloudShadowMap, low, 0.0).r * textureLod(cloudShadowMap, mid, 0.0).g
                                 : 1.0;
    return mix(cloudsMid.z, through, inside);
}

// ... and past terrain and objects: the near map, else the middle one, else
// the far one, as the lit shaders take them.
bool mapPoint(mat4 matrix, vec3 p, out vec3 m) {
    vec4 q = matrix * vec4(p, 1.0);
    m = q.xyz / q.w * 0.5 + 0.5;
    return all(greaterThan(m.xy, vec2(0.0))) && all(lessThan(m.xy, vec2(1.0))) && m.z < 1.0;
}

// The terrain farther off (ShadowSampling.glsl's terrainShade): a hill the
// sun has set behind.
float terrainVisible(vec3 p) {
    if (shadeParams.x <= 0.0)
        return 1.0;
    vec2 m = (p.xz - shadePlace.xy) / shadePlace.z;
    if (any(lessThan(m, vec2(0.0))) || any(greaterThan(m, vec2(1.0))))
        return 1.0;
    float top = textureLod(terrainShadeMap, m, 0.0).r + shadeParams.y;
    return clamp((p.y - shadePlace.w - top) * shadeParams.z + 1.0, 0.0, 1.0);
}

float shadowVisible(vec3 p) {
    float terrain = terrainVisible(p);
    vec3 m;
    if (mapPoint(shadow0, p, m))
        return terrain * textureLod(shadowMap0, vec3(m.xy, m.z - 0.001), 0.0);
    if (mapPoint(shadow1, p, m))
        return terrain * textureLod(shadowMap1, vec3(m.xy, m.z - 0.002), 0.0);
    if (mapPoint(shadow2, p, m))
        return terrain * textureLod(shadowMap2, vec3(m.xy, m.z - 0.002), 0.0);
    return terrain;
}

void main() {
    ivec3 id = ivec3(gl_GlobalInvocationID);
    if (id.x >= int(grid.x) || id.y >= int(grid.y) || id.z >= int(grid.z))
        return;
    // The cell's middle ray. A thin layer seen at a grazing angle rises and
    // falls metres within a cell's depth and width: one point jittered over
    // them each frame crossed its top and switched the cell's fog on and
    // off (the user, 2026-10-14: mist flashing at CMK at 4:15). Four points
    // along the slice, shifted each frame, take its share of the layer.
    vec2 uv = (vec2(id.xy) + 0.5) / grid.xy;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * camera.w);
    vec4 far = inverse * vec4(ndc, 1.0, 1.0);
    vec3 d = normalize(far.xyz / far.w);
    const int Points = 4;
    float sigma = 0.0, above = 0.0, sigmaAbove = 0.0;
    // A slice above the highest fog top on the map has none (most cells
    // look at the sky or over the fog).
    float low = min(d.y * sliceDistance(float(id.z)), d.y * sliceDistance(float(id.z) + 1.0)) + camera.y - range.y;
    for (int k = 0; k < Points && low < range.z; ++k) {
        float a, sa;
        float s = density(camera.xyz + d * sliceDistance(float(id.z) + (float(k) + jitter.z) / float(Points)), a, sa);
        sigma += s;
        above += a * s;
        sigmaAbove += sa * s;
    }
    sigma /= float(Points);
    // The fog's depth and extinction above, as the fog in the cell has them.
    above = sigma > 0.0 ? above / (sigma * float(Points)) : 0.0;
    sigmaAbove = sigma > 0.0 ? sigmaAbove / (sigma * float(Points)) : 0.0;
    // Its light at one point, shifted along the slice each frame.
    vec3 p = camera.xyz + d * sliceDistance(float(id.z) + jitter.z);
    vec3 scattered = vec3(0.0);
    if (sigma > 0.0) {
        // The sky's light: the horizon's colour, as far fog shows it, dimmer
        // deeper in the fog (half at the floor of a thick one).
        vec3 skyLight = pow(max(horizonLight(d), vec3(0.0)), vec3(2.2)) * mix(1.0, 0.5, 1.0 - exp(-sigmaAbove * above * 0.5));
        // The sun's (or the moon's): through the fog above towards it, the
        // clouds and the shadows of terrain and objects. Droplets scatter it
        // mostly forward; many scatterings in thick fog spread it into an
        // even glow (an isotropic share stands in).
        float sunThrough = exp(-sigmaAbove * min(above / max(light.y, 0.05), 3000.0));
        float phase = mix(henyeyGreenstein(dot(d, light.xyz), 0.85), 1.0 / (4.0 * 3.14159265), 0.3)
                * 4.0 * 3.14159265;
        vec3 sunLight = lightColour.rgb * (cloudVisible(p) * shadowVisible(p) * sunThrough * phase * 0.8);
        scattered = (skyLight + sunLight) * sigma;
    }
    vec4 current = vec4(scattered, sigma);
    // Last frame's cell at this cell's middle, where it was in view.
    if (previousCamera.w > 0.0) {
        vec3 r = camera.xyz + d * sliceDistance(float(id.z) + 0.5) - previousCamera.xyz;
        vec4 clip = previous * vec4(r, 0.0);
        if (clip.w > 0.0) {
            vec2 before = clip.xy / clip.w * 0.5 + 0.5;
            float slice = log(max(length(r), range.x) / range.x) / grid.w;
            vec3 coord = vec3(before, slice);
            if (all(greaterThanEqual(coord, vec3(0.0))) && all(lessThanEqual(coord, vec3(1.0))))
                current = mix(current, textureLod(history, coord, 0.0), previousCamera.w);
        }
    }
    imageStore(cells, id, current);
}
)";

// Each column front to back: the light added up to the end of each slice and
// the share of what lies behind let through.
const char *IntegrateCompute = R"(
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(binding = 1) uniform sampler3D cells;
layout(binding = 2, rgba16f) uniform writeonly image3D sums;

void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy);
    if (id.x >= int(grid.x) || id.y >= int(grid.y))
        return;
    int slices = int(grid.z);
    vec3 light = vec3(0.0);
    float through = 1.0;
    float start = range.x;
    for (int i = 0; i < slices; ++i) {
        float end = sliceDistance(float(i + 1));
        float dt = end - start;
        vec4 cell = texelFetch(cells, ivec3(id, i), 0);
        float sigma = max(cell.a, 0.0);
        float slice = exp(-sigma * dt);
        // The light scattered over the slice, as it dims along it.
        vec3 scattered = sigma > 1e-7 ? (cell.rgb - cell.rgb * slice) / sigma : cell.rgb * dt;
        light += through * scattered;
        through *= slice;
        imageStore(sums, ivec3(id, i), vec4(light, through));
        start = end;
    }
}
)";

// The sky and the clouds behind the fog: the far end of each column, mixed
// over them (premultiplied: colour + view * a).
const char *SkyFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Sky {
    vec4 params;   // the sky's depth, 0, 0, 0
};
layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2) uniform sampler3D sums;

void main() {
    if (textureLod(sceneDepth, uv, 0.0).r < params.x) {
        result = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec4 f = textureLod(sums, vec3(uv, 1.0), 0.0);
    float through = clamp(f.a, 0.0, 1.0);
    // The fog's own colour, mixed by its share (display colours).
    vec3 own = through < 0.999 ? pow(max(f.rgb / (1.0 - through), vec3(0.0)), vec3(1.0 / 2.2)) : vec3(0.0);
    result = vec4(own * (1.0 - through), through);
}
)";

QByteArray computeSource(const char *body) {
    return QByteArray(FogBlock) + body;
}

// Halton points: where in its cell each is sampled, frame by frame.
float halton(unsigned index, unsigned base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= float(base);
        r += f * float(index % base);
        index /= base;
    }
    return r;
}

}

QList<QByteArray> RhiRenderer::fogShaders() {
    return {computeSource(InjectCompute), computeSource(IntegrateCompute), QByteArray(SkyFragment)};
}

void RhiRenderer::setFog(const Fog &next) {
    fog = next;
}

bool RhiRenderer::fogWanted() const {
    return fog.quality > 0 && fog.amount > 0.0f && fog.map != nullptr && fog.size > 0
            && fog.map->size() == size_t(fog.size) * fog.size * 4 && !fogResources.unsupported
            && skyFor(TARGET_VIEW);
}

void RhiRenderer::fogForward(float *out) const {
    // The view's directions to clip space, its y turned as the volume's rows
    // are (the screen's), then to 0-1.
    QMatrix4x4 forward = QMatrix4x4(skyForward[TARGET_VIEW]).transposed();
    QMatrix4x4 flip;
    flip.scale(1.0f, rhi->isYUpInFramebuffer() ? 1.0f : -1.0f, 1.0f);
    forward = flip * forward;
    // Column by column, as the shaders take matrices.
    std::copy(forward.constData(), forward.constData() + 16, out);
}

void RhiRenderer::releaseFog() {
    FogResources &f = fogResources;
    for (QRhiShaderResourceBindings *b : f.injectBindings)
        delete b;
    for (QRhiShaderResourceBindings *b : f.integrateBindings)
        delete b;
    for (QRhiTexture *t : f.scatter)
        delete t;
    delete f.integrated;
    delete f.floor;
    delete f.uniforms;
    delete f.linear;
    delete f.inject;
    delete f.integrate;
    delete f.skyUniforms;
    delete f.skyTarget;
    delete f.skyPass;
    delete f.skyBindings;
    delete f.skyPipeline;
    const bool unsupported = f.unsupported;
    f = FogResources();
    f.unsupported = unsupported;
}

bool RhiRenderer::createFog(int quality, const QSize &viewSize) {
    FogResources &f = fogResources;
    if (f.unsupported || viewSize.isEmpty())
        return false;
    // High: a cell of 8 x 8 pixels; low: 16 x 16; 64 slices.
    const int pixels = quality >= 2 ? 8 : 16;
    const int slices = 64;
    const int width = std::max(1, (viewSize.width() + pixels - 1) / pixels);
    const int height = std::max(1, (viewSize.height() + pixels - 1) / pixels);
    if (f.inject != nullptr && f.quality == quality && f.width == width && f.height == height
            && f.slices == slices && f.viewSize == viewSize)
        return true;
    releaseFog();
    if (!rhi->isFeatureSupported(QRhi::Compute) || !rhi->isFeatureSupported(QRhi::ThreeDimensionalTextures)
            || !rhi->isTextureFormatSupported(QRhiTexture::RGBA16F, QRhiTexture::UsedWithLoadStore)) {
        f.unsupported = true;
        qInfo() << "QRhi: no fog volume on this backend (compute and 3D storage textures needed)";
        return false;
    }
    f.quality = quality;
    f.viewSize = viewSize;
    f.width = width;
    f.height = height;
    f.slices = slices;
    const QRhiTexture::Flags volume = QRhiTexture::ThreeDimensional | QRhiTexture::UsedWithLoadStore;
    for (QRhiTexture *&t : f.scatter)
        t = rhi->newTexture(QRhiTexture::RGBA16F, width, height, slices, 1, volume);
    f.integrated = rhi->newTexture(QRhiTexture::RGBA16F, width, height, slices, 1, volume);
    f.floor = rhi->newTexture(QRhiTexture::RGBA16F, QSize(std::max(fog.size, 1), std::max(fog.size, 1)));
    f.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(FogParameters));
    f.linear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                               QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    bool ok = f.integrated->create() && f.floor->create() && f.uniforms->create() && f.linear->create();
    for (QRhiTexture *t : f.scatter)
        ok = ok && t->create();
    if (!ok) {
        releaseFog();
        f.unsupported = true;
        return false;
    }
    static QShader inject, integrate;
    if (!inject.isValid()) {
        inject = bakeInline(computeSource(InjectCompute).constData(), QShader::ComputeStage, rhi);
        integrate = bakeInline(computeSource(IntegrateCompute).constData(), QShader::ComputeStage, rhi);
    }
    const auto compute = QRhiShaderResourceBinding::ComputeStage;
    for (int i = 0; i < 2; ++i) {
        f.integrateBindings[i] = rhi->newShaderResourceBindings();
        f.integrateBindings[i]->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, compute, f.uniforms),
                                             QRhiShaderResourceBinding::sampledTexture(1, compute, f.scatter[i], f.linear),
                                             QRhiShaderResourceBinding::imageStore(2, compute, f.integrated, 0)});
        f.integrateBindings[i]->create();
    }
    // The injection's bindings follow the maps (prepareFog); a first set for
    // the pipeline's layout.
    f.injectBindings[0] = rhi->newShaderResourceBindings();
    f.injectBindings[0]->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, compute, f.uniforms),
                                      QRhiShaderResourceBinding::sampledTexture(1, compute, f.floor, f.linear),
                                      QRhiShaderResourceBinding::sampledTexture(2, compute, dummy2D, f.linear),
                                      QRhiShaderResourceBinding::sampledTexture(3, compute, dummyDepth, shadowSampler),
                                      QRhiShaderResourceBinding::sampledTexture(4, compute, dummyDepth, shadowSampler),
                                      QRhiShaderResourceBinding::sampledTexture(5, compute, dummyDepth, shadowSampler),
                                      QRhiShaderResourceBinding::sampledTexture(6, compute, f.scatter[1], f.linear),
                                      QRhiShaderResourceBinding::imageStore(7, compute, f.scatter[0], 0),
                                      QRhiShaderResourceBinding::sampledTexture(8, compute, dummy2D, f.linear)});
    f.injectBindings[0]->create();
    f.inject = rhi->newComputePipeline();
    f.inject->setShaderStage({QRhiShaderStage::Compute, inject});
    f.inject->setShaderResourceBindings(f.injectBindings[0]);
    f.integrate = rhi->newComputePipeline();
    f.integrate->setShaderStage({QRhiShaderStage::Compute, integrate});
    f.integrate->setShaderResourceBindings(f.integrateBindings[0]);
    if (!inject.isValid() || !integrate.isValid() || !f.inject->create() || !f.integrate->create()) {
        releaseFog();
        f.unsupported = true;
        qWarning() << "QRhi: the fog volume's compute pipelines failed";
        return false;
    }
    // Made again with this frame's maps.
    delete f.injectBindings[0];
    f.injectBindings[0] = nullptr;
    return true;
}

void RhiRenderer::prepareFog(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *&updates) {
    FogResources &f = fogResources;
    f.ready = false;
    f.skyApplied = false;
    if (!fogWanted() || secondaryView || currentTarget != TARGET_VIEW || !skyInverseValid[TARGET_VIEW]
            || !createFog(fog.quality, view.size))
        return;
    // The floor map, when it changed (half floats: heights to a few metres).
    if (updates == nullptr)
        updates = rhi->nextResourceUpdateBatch();
    if (f.floorRevision != fog.revision || f.floor->pixelSize() != QSize(fog.size, fog.size)) {
        if (f.floor->pixelSize() != QSize(fog.size, fog.size)) {
            f.floor->setPixelSize(QSize(fog.size, fog.size));
            f.floor->create();
            for (QRhiShaderResourceBindings *&b : f.injectBindings) {
                delete b;
                b = nullptr;
            }
        }
        QByteArray bytes(int(fog.map->size() * sizeof(qfloat16)), Qt::Uninitialized);
        qfloat16 *out = reinterpret_cast<qfloat16 *>(bytes.data());
        for (size_t i = 0; i < fog.map->size(); ++i)
            out[i] = qfloat16((*fog.map)[i]);
        updates->uploadTexture(f.floor, QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(bytes)));
        f.floorRevision = fog.revision;
    }
    // The maps the injection reads: the cloud shadow map, the sun's, the
    // terrain's shade.
    const bool shadows = Game::shadowsEnabled > 0;
    const QRhiTexture *maps[5] = {
        cloudResources.shadow != nullptr && cloudResources.shadowValid ? cloudResources.shadow : dummy2D,
        shadows && shadowMaps[0].valid() ? shadowMaps[0].depth : dummyDepth,
        shadows && shadowMaps[1].valid() ? shadowMaps[1].depth : dummyDepth,
        shadows && shadowMaps[2].valid() ? shadowMaps[2].depth : dummyDepth,
        terrainShadeReady() ? shadeResources.texture : dummy2D};
    if (!std::equal(maps, maps + 5, f.boundMaps))
        for (QRhiShaderResourceBindings *&b : f.injectBindings) {
            delete b;
            b = nullptr;
        }
    const auto compute = QRhiShaderResourceBinding::ComputeStage;
    for (int i = 0; i < 2; ++i) {
        if (f.injectBindings[i] != nullptr)
            continue;
        f.injectBindings[i] = rhi->newShaderResourceBindings();
        f.injectBindings[i]->setBindings(
                {QRhiShaderResourceBinding::uniformBuffer(0, compute, f.uniforms),
                 QRhiShaderResourceBinding::sampledTexture(1, compute, f.floor, f.linear),
                 QRhiShaderResourceBinding::sampledTexture(2, compute, const_cast<QRhiTexture *>(maps[0]), f.linear),
                 QRhiShaderResourceBinding::sampledTexture(3, compute, const_cast<QRhiTexture *>(maps[1]), shadowSampler),
                 QRhiShaderResourceBinding::sampledTexture(4, compute, const_cast<QRhiTexture *>(maps[2]), shadowSampler),
                 QRhiShaderResourceBinding::sampledTexture(5, compute, const_cast<QRhiTexture *>(maps[3]), shadowSampler),
                 QRhiShaderResourceBinding::sampledTexture(6, compute, f.scatter[1 - i], f.linear),
                 QRhiShaderResourceBinding::imageStore(7, compute, f.scatter[i], 0),
                 QRhiShaderResourceBinding::sampledTexture(8, compute, const_cast<QRhiTexture *>(maps[4]), f.linear)});
        f.injectBindings[i]->create();
    }
    std::copy(maps, maps + 5, f.boundMaps);

    FogParameters p = {};
    std::copy(skyInverse[TARGET_VIEW], skyInverse[TARGET_VIEW] + 16, p.inverse);
    std::copy(f.previousForward, f.previousForward + 16, p.previous);
    std::copy(gluu->pShadowMatrix0, gluu->pShadowMatrix0 + 16, p.shadow0);
    std::copy(gluu->pShadowMatrix, gluu->pShadowMatrix + 16, p.shadow1);
    std::copy(gluu->pShadowMatrix2, gluu->pShadowMatrix2 + 16, p.shadow2);
    std::copy(viewPosition, viewPosition + 3, p.camera);
    p.camera[3] = rhi->isYUpInFramebuffer() ? 1.0f : -1.0f;
    // The last frame's cells count most, unless the camera jumped.
    const float moved = std::sqrt(std::pow(viewPosition[0] - f.previousCamera[0], 2.0f)
                                  + std::pow(viewPosition[1] - f.previousCamera[1], 2.0f)
                                  + std::pow(viewPosition[2] - f.previousCamera[2], 2.0f));
    std::copy(f.previousCamera, f.previousCamera + 3, p.previousCamera);
    p.previousCamera[3] = f.historyValid && moved < 50.0f ? 0.95f : 0.0f;
    p.grid[0] = float(f.width);
    p.grid[1] = float(f.height);
    p.grid[2] = float(f.slices);
    p.grid[3] = std::log(FogFar / FogNear);
    p.range[0] = FogNear;
    p.range[1] = viewPosition[1] - sky.cameraHeight;
    // The highest fog top on the map (with its soft edge), when the map or
    // the fog's depth changed.
    if (f.topRevision != fog.revision || f.topDepth != fog.depth) {
        float highest = -1e9f;
        for (size_t i = 0; i + 3 < fog.map->size(); i += 4) {
            const float *c = &(*fog.map)[i];
            highest = std::max(highest, std::max(c[0] + fog.depth * c[1], c[2] + fog.depth * 0.5f * c[3]));
        }
        f.highestTop = highest + std::clamp(fog.depth * 0.15f, 3.0f, 20.0f);
        f.topRevision = fog.revision;
        f.topDepth = fog.depth;
    }
    p.range[2] = f.highestTop;
    // Still (captures): the cells' middles, so frames repeat.
    if (clouds.still) {
        p.jitter[0] = p.jitter[1] = p.jitter[2] = 0.5f;
    } else {
        const unsigned index = (f.frame % 64u) + 1u;
        p.jitter[0] = halton(index, 2);
        p.jitter[1] = halton(index, 3);
        p.jitter[2] = halton(index, 5);
    }
    p.fog[0] = fog.amount * 3.912f / std::max(fog.visibility, 10.0f);
    p.fog[1] = fog.depth;
    p.fog[2] = fog.base;
    p.fog[3] = std::clamp(fog.depth * 0.15f, 3.0f, 20.0f);
    // The map's north-west corner in the view's space (north is -z).
    p.map[0] = viewPosition[0] + float(fog.west - fog.cameraEast);
    p.map[1] = viewPosition[2] - float(fog.north - fog.cameraNorth);
    p.map[2] = float(fog.size) * fog.cell;
    p.map[3] = 1.0f;
    // The lit shaders' light (the sun, or the moon at night) and the sky's,
    // linear.
    const float *l = Game::sunLightDirection;
    const float length = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    for (int i = 0; i < 3; ++i)
        p.light[i] = length > 0.0f ? l[i] / length : (i == 1 ? 1.0f : 0.0f);
    const float scene = sceneScaleDisplay();
    const float skyScale = scene * std::pow(cloudSkyLight(), 1.0f / 2.2f);
    for (int i = 0; i < 3; ++i) {
        p.lightColour[i] = std::pow(std::max(gluu->diffuseColor[i] * scene, 0.0f), 2.2f);
        p.ambient[i] = std::pow(std::max(gluu->ambientColor[i] * skyScale, 0.0f), 2.2f);
    }
    float plane = 0.0f;
    if (cloudShadowPlane(plane) && maps[0] != dummy2D && p.light[1] > 0.01f) {
        cloudShadowCentre(0, p.clouds[0], p.clouds[1]);
        cloudShadowCentre(1, p.cloudsMid[0], p.cloudsMid[1]);
        p.cloudsMid[2] = 1.0f - 0.85f * std::clamp(sky.overcast, 0.0f, 1.0f);
        p.clouds[2] = 1.0f / (CloudShadowExtent * 1000.0f);
        p.clouds[3] = 1.0f;
        p.cloudSun[0] = p.light[0] / p.light[1];
        p.cloudSun[1] = p.light[2] / p.light[1];
        p.cloudSun[2] = viewPosition[1] + plane * 1000.0f - sky.cameraHeight;
    }
    // The horizon's colours of the aerial haze (display, the scene's scale).
    p.ringSun[0] = haze.sun[0];
    p.ringSun[1] = haze.sun[1];
    for (int i = 0; i < 8; ++i)
        for (int k = 0; k < 3; ++k)
            p.ring[i][k] = haze.ring[i][k] * scene;
    terrainShadePlace(p.shadePlace, p.shadeParams);
    if (f.boundMaps[4] == dummy2D)
        p.shadeParams[0] = 0.0f;
    updates->updateDynamicBuffer(f.uniforms, 0, sizeof(p), &p);

    const int current = f.current;
    cb->beginComputePass(updates);
    updates = nullptr;
    cb->setComputePipeline(f.inject);
    cb->setShaderResources(f.injectBindings[current]);
    cb->dispatch((f.width + 7) / 8, (f.height + 7) / 8, f.slices);
    cb->endComputePass();
    cb->beginComputePass();
    cb->setComputePipeline(f.integrate);
    cb->setShaderResources(f.integrateBindings[current]);
    cb->dispatch((f.width + 7) / 8, (f.height + 7) / 8, 1);
    cb->endComputePass();

    // This frame's cells are the next frame's history.
    f.current = 1 - current;
    fogForward(f.previousForward);
    std::copy(viewPosition, viewPosition + 3, f.previousCamera);
    f.historyValid = true;
    f.ready = true;
    f.filled = true;
    ++f.frame;
}

void RhiRenderer::writeFogUniforms(RhiProgram *program) {
    const bool on = fogResources.filled && fogWanted() && !secondaryView && currentTarget == TARGET_VIEW;
    float forward[16];
    fogForward(forward);
    program->setMat4("fogForward", forward);
    program->setVec("fogParams", on ? 1.0f : 0.0f, FogNear, std::log(FogFar / FogNear),
                    float(std::max(fogResources.slices, 1)));
    // The sun under the fog (ShadowSampling.glsl), in every view.
    const bool shading = fogResources.filled && fogWanted();
    const float *l = Game::sunLightDirection;
    const float length = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    const float rise = length > 0.0f ? l[1] / length : 1.0f;
    program->setVec("fogShape", shading ? fog.amount * 3.912f / std::max(fog.visibility, 10.0f) : 0.0f, fog.depth,
                    fog.base, 1.0f / std::max(rise, 0.05f));
    program->setVec("fogPlace", viewPosition[0] + float(fog.west - fog.cameraEast),
                    viewPosition[2] - float(fog.north - fog.cameraNorth), float(fog.size) * fog.cell,
                    viewPosition[1] - sky.cameraHeight);
}

void RhiRenderer::applySkyFog() {
    FogResources &f = fogResources;
    if (!f.ready || f.skyApplied || secondaryView || currentTarget != TARGET_VIEW || !view.valid())
        return;
    RhiRenderSurface *surfaceNow = surface();
    if (surfaceNow == nullptr || surfaceNow->frame().commandBuffer == nullptr)
        return;
    f.skyApplied = true;
    flushTarget();
    if (f.skyPipeline == nullptr || f.skyViewColor != view.color) {
        delete f.skyPipeline;
        delete f.skyBindings;
        delete f.skyTarget;
        delete f.skyPass;
        f.skyPipeline = nullptr;
        f.skyBindings = nullptr;
        f.skyViewColor = view.color;
        if (f.skyUniforms == nullptr) {
            f.skyUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
            f.skyUniforms->create();
        }
        f.skyTarget = rhi->newTextureRenderTarget({QRhiColorAttachment(view.color)},
                                                  QRhiTextureRenderTarget::PreserveColorContents);
        f.skyPass = f.skyTarget->newCompatibleRenderPassDescriptor();
        f.skyTarget->setRenderPassDescriptor(f.skyPass);
        f.skyTarget->create();
        const auto fragment = QRhiShaderResourceBinding::FragmentStage;
        f.skyBindings = rhi->newShaderResourceBindings();
        f.skyBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, fragment, f.skyUniforms),
                                    QRhiShaderResourceBinding::sampledTexture(1, fragment, view.depth,
                                                                              sampler(false, true, true)),
                                    QRhiShaderResourceBinding::sampledTexture(2, fragment, f.integrated, f.linear)});
        f.skyBindings->create();
        static QShader vertex, sky;
        if (!vertex.isValid()) {
            vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
            sky = bakeInline(SkyFragment, QShader::FragmentStage, rhi);
        }
        QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
        ps->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, sky}});
        QRhiGraphicsPipeline::TargetBlend b;
        b.enable = true;
        b.srcColor = QRhiGraphicsPipeline::One;
        b.dstColor = QRhiGraphicsPipeline::SrcAlpha;
        b.srcAlpha = QRhiGraphicsPipeline::Zero;
        b.dstAlpha = QRhiGraphicsPipeline::One;
        ps->setTargetBlends({b});
        ps->setShaderResourceBindings(f.skyBindings);
        ps->setRenderPassDescriptor(f.skyPass);
        if (!ps->create()) {
            delete ps;
            ps = nullptr;
        }
        f.skyPipeline = ps;
    }
    if (f.skyPipeline == nullptr)
        return;
    const float params[4] = {0.99f, 0.0f, 0.0f, 0.0f};
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->updateDynamicBuffer(f.skyUniforms, 0, sizeof(params), params);
    QRhiCommandBuffer *cb = surfaceNow->frame().commandBuffer;
    cb->beginPass(f.skyTarget, Qt::black, {1.0f, 0}, batch);
    cb->setGraphicsPipeline(f.skyPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(view.size.width()), float(view.size.height())));
    cb->setShaderResources(f.skyBindings);
    cb->draw(3);
    cb->endPass();
}
