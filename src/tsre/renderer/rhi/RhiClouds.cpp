/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Volumetric clouds of the QRhi renderer (task 28), after Schneider (Horizon
// Zero Dawn 2015, Nubis 2017) and Hillaire (Frostbite 2016). After the main
// view's opaque passes:
// 1. cover maps: where the low, mid and high layers have clouds, the same
//    function as the weather's 2D detail (WeatherDetail::cover: the noise,
//    its octaves limited to what a texel shows, its quantiles, the edge),
//    around the camera in a near (20 km) and a far (205 km) map, redrawn
//    only when the weather, its drift or the camera's place changes;
// 2. a march at a reduced resolution through the low and mid layers on a
//    shell around the planet: density from the cover, a height profile by
//    cloud type and 3D Perlin-Worley noise eroded by Worley detail; light
//    from a short march towards the sun (multiple scattering in octaves,
//    two-lobe phase) and from the sky, less under a deep cloud above (a
//    storm's base dark); the flat high layer behind them;
//    haze over the distance from the sky-view table;
// 3. the result blended with the last frames' where the clouds were in the
//    same direction (the march starts at a different jitter each frame),
//    with where each pixel's clouds are (their front and mean distance);
// 4. composited over the view: in front of the scene where the scene lies
//    past the clouds' front, at the view's resolution.

#include "RhiRenderer.h"
#include "RhiProgram.h"
#include "RhiRenderSurface.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <qfloat16.h>
#include <tsre/environment/Atmosphere.h>
#include <tsre/environment/CloudNoise.h>
#include <tsre/environment/WeatherDetail.h>

namespace {

constexpr int CoverSize = 512;
// The cloud shadow map is drawn again when a layer's map has drifted this
// far from the camera (m), or the light has moved its shadows this far at
// the clouds' top (m).
constexpr double CloudShadowRecentre = 1000.0;
constexpr float CloudShadowLightStep = 5.0f;
// The maps keep the weather's large scales only (its octaves finer than a
// few texels drop out): the 3D noise makes the smaller shapes, which the 2D
// noise, the same at every height, would carve into vertical flutes.
constexpr float NearCoverTexel = 120.0f;
constexpr float FarCoverTexel = 600.0f;
constexpr int ShapeSize = 128;
constexpr int DetailSize = 32;
constexpr int CurlSize = 128;
constexpr int BlueSize = 64;
// Metres one period of the shape and detail volumes spans.
constexpr double ShapePeriod = 6000.0;
constexpr double DetailPeriod = 900.0;

// Per quality: the cloud image's pixels (whatever the window's size or its
// display scaling), march steps, light steps. Each frame marches one pixel
// of every 4 x 4 block of the image (Schneider 2015); the others come from
// the last frames, reprojected.
struct Preset {
    int pixels;
    int steps;
    int lightSteps;
};
const Preset Presets[5] = {{0, 0, 0}, {320 * 180, 48, 3}, {480 * 270, 64, 4}, {640 * 360, 96, 6},
                           {960 * 540, 128, 6}};
// The environment cube's clouds, a face at a time: below Low (the cube is
// read blurred, by rough surfaces or through waves; user, 2026-10-12).
const Preset EnvironmentPreset = {96 * 96, 32, 2};
constexpr int UpdateBlock = 4;
constexpr int UpdatePasses = UpdateBlock * UpdateBlock;
// A face is drawn every few frames (round robin): four of a block's pixels
// each time.
constexpr int EnvironmentPasses = 4;
// The order the pixels of a block are marched in (a Bayer pattern: each
// frame's pixels spread evenly).
const int UpdateOrder[UpdatePasses][2] = {{0, 0}, {2, 2}, {0, 2}, {2, 0}, {1, 1}, {3, 3}, {1, 3}, {3, 1},
                                          {0, 1}, {2, 3}, {0, 3}, {2, 1}, {1, 0}, {3, 2}, {1, 2}, {3, 0}};
// The march reaches this far (km): the weather around the camera reaches
// about 58 km. The cloud shell curves down more than the Earth so that its
// top meets the horizon within it (HorizonDistance; Horizon Zero Dawn's
// clouds sat on a smaller planet too).
constexpr float MarchDistance = 60.0f;
constexpr float HorizonDistance = 55.0f;
constexpr float EarthRadius = 6360.0f;
constexpr float SmallestShellRadius = 120.0f;

// Per update pass: the block's size and the pixel marched (x, y), the share
// of a new sample in the history; the image's and the march's sizes; the
// cloud shell's radius (km).
struct UpdateParameters {
    float update[4];
    float sizes[4];
    float shell[4];
};

struct CoverParameters {
    float map[4];          // the map's west, north edges in the weather texture (m from its NW corner), texel (m), weather extent (m)
    float origin[3][4];    // the map's NW corner per layer, drift taken off (m; noise space)
    quint32 seeds[4];      // noise seed per layer
    float quantiles[65][4];
};

struct MarchParameters {
    float inverse[16];
    float previous[16];
    float sun[4];          // towards the sun; w: the rain's slant east (km a km of fall)
    float camera[4];       // height (km), y sign, exposure, jitter (0-1)
    float weatherMap[4];   // camera in the weather texture (m from its NW corner), extent (m), texels per side
    float nearMap[4];      // camera in the near cover map (m), extent (m)
    float farMap[4];
    float slab[4];         // bottom, top (km), distance (km), steps
    float projection[4];   // x and y scale, scene near and far
    float bands[4];        // distant near and far, scene and distant depth ends
    float noiseLow[4];     // shape and detail offsets of the low layer (m), mod their periods
    float noiseMid[4];
    float lighting[4];     // light steps, still, buffer texel w, h
    float high[4];         // high layer altitude (km), haze; deep layers: extra samples a ray may take, samples a step at most
    float shadow[4];       // cloud shadow map: extent (km), its plane's altitude (km), the low layer's centre east and north of the camera (km)
    float night[4];        // the night sky's floor (display colour); the scene's scale (auto exposure, task 29)
    float light[4];        // towards the clouds' light: the sun, or the moon at night (task 31)
    float lightColour[4];  // its light relative to the sun's; w: the rain's slant north
    // How far the layers' weather and cover maps have moved with their wind
    // since they were drawn (km east, north): low, mid; high.
    float shiftLowMid[4];
    float shiftHigh[4];
    // The layers' moves since this view's last frame (km east, north): low
    // and mid; high, and the low layer's middle height (km) for pixels
    // without clouds. The history follows each pixel's clouds. The cloud
    // shell's radius (km) last, for the composite.
    float motion[4];
    float shadowMid[4];    // the mid layer's cloud shadow map: its centre east and north of the camera (km); downwind at the top (east, north)
    float motionHigh[4];
};

const char *CloudCommon = R"(
const float Pi = 3.14159265;
const float BottomRadius = 6360.0;
const float TopRadius = 6460.0;

float fromUnitToSubUvs(float u, float resolution) {
    return (u + 0.5 / resolution) * (resolution / (resolution + 1.0));
}

uint hashCell(int x, int y, uint seed) {
    uint h = uint(x) * 0x8da6b343u ^ uint(y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float corner(int ix, int iy, uint seed, float dx, float dy) {
    float a = float(hashCell(ix, iy, seed) >> 24) * (2.0 * Pi / 256.0);
    return cos(a) * dx + sin(a) * dy;
}

float fadeCurve(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float gradientNoise(float x, float y, uint seed) {
    float fx = floor(x), fy = floor(y);
    int ix = int(fx), iy = int(fy);
    float dx = x - fx, dy = y - fy;
    float u = fadeCurve(dx), v = fadeCurve(dy);
    float a = corner(ix, iy, seed, dx, dy);
    float b = corner(ix + 1, iy, seed, dx - 1.0, dy);
    float c = corner(ix, iy + 1, seed, dx, dy - 1.0);
    float d = corner(ix + 1, iy + 1, seed, dx - 1.0, dy - 1.0);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}

// WeatherNoise::fractal with the octaves limited to what a texel shows.
float fractalLimited(float x, float y, uint seed, float texel) {
    float sum = 0.0, amplitude = 1.0, squares = 0.0, fullSquares = 0.0, fullTotal = 0.0, cell = 1.0;
    for (int i = 0; i < 4; ++i) {
        float weight = clamp((cell / texel - 2.0) / 2.0, 0.0, 1.0);
        if (weight > 0.0) {
            float turn = 0.5 + float(i) * 1.3;
            float c = cos(turn), s = sin(turn);
            float rx = x * c - y * s;
            float ry = x * s + y * c;
            sum += weight * amplitude * gradientNoise(rx + float(i) * 17.31, ry - float(i) * 9.77,
                                                      seed + uint(i) * 0x9e3779b9u);
            squares += weight * weight * amplitude * amplitude;
        }
        fullSquares += amplitude * amplitude;
        fullTotal += amplitude;
        amplitude *= 0.5;
        cell *= 0.5;
        x *= 2.0;
        y *= 2.0;
    }
    if (squares <= 0.0)
        return 0.0;
    return sum / sqrt(squares) * sqrt(fullSquares) / fullTotal * 1.6;
}
)";

const char *CoverFragment = R"(
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Cover {
    vec4 map;
    vec4 origin[3];
    uvec4 seeds;
    vec4 quantiles[65];
};
layout(binding = 1) uniform sampler2D weatherTexture;

float quantile(int i) {
    return quantiles[i >> 2][i & 3];
}

// WeatherNoise::distribution.
float distribution(float value) {
    if (value <= quantile(0))
        return 0.0;
    if (value >= quantile(256))
        return 1.0;
    int low = 0, high = 256;
    while (high - low > 1) {
        int middle = (low + high) / 2;
        if (quantile(middle) > value)
            high = middle;
        else
            low = middle;
    }
    float a = quantile(low), b = quantile(high);
    return (float(low) + (b > a ? (value - a) / (b - a) : 0.0)) / 256.0;
}

const float Scales[4] = float[4](5000.0, 2500.0, 1200.0, 8000.0);

// WeatherDetail::cover for one layer at a point of the noise space; with
// even set, the noise's even value (0-1) the cover thresholds instead.
float cover(vec2 point, vec2 values, uint seed, float texel, bool even) {
    if (values.x <= 0.0 && !even)
        return 0.0;
    float t3 = clamp(values.y, 0.0, 1.0) * 3.0;
    int i = min(2, int(t3));
    float along = t3 - float(i);
    // Storm cells without the noise's detail finer than 2 km: drawn
    // straight up their 10 km, it grooved the towers' walls; the billows
    // make their walls instead.
    float limit = mix(texel, max(texel, 1000.0), smoothstep(0.8, 1.0, values.y));
    float a = fractalLimited(point.x / Scales[i], point.y / Scales[i], seed + uint(i) * 7919u, limit / Scales[i]);
    float noise = a;
    if (along > 0.001) {
        float b = fractalLimited(point.x / Scales[i + 1], point.y / Scales[i + 1], seed + uint(i + 1) * 7919u,
                                 limit / Scales[i + 1]);
        noise = along >= 0.999 ? b : (a * (1.0 - along) + b * along)
                / sqrt((1.0 - along) * (1.0 - along) + along * along);
    }
    float evenValue = distribution(noise);
    if (even)
        return evenValue;
    float scale = Scales[i] + (Scales[i + 1] - Scales[i]) * along;
    float edge = max(0.04 + 0.08 * (1.0 - values.y), clamp(1.5 * texel / scale, 0.0, 0.3));
    float t = clamp((evenValue - (1.0 - values.x) + edge) / (2.0 * edge), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void main() {
    // Texel centres (metres east and south of the map's corner); rows from
    // north to south.
    vec2 offset = uv * (map.z * 512.0);
    vec2 weather = (map.xy + offset) / map.w;
    vec4 layers[3];
    float size = float(textureSize(weatherTexture, 0).y);
    for (int l = 0; l < 3; ++l) {
        vec2 w = clamp(weather, vec2(0.5 / size), vec2(1.0 - 0.5 / size));
        layers[l] = textureLod(weatherTexture, vec2((float(l) + w.x) / 4.0, w.y), 0.0);
    }
    vec3 c;
    for (int l = 0; l < 3; ++l) {
        vec2 point = vec2(origin[l].x + offset.x, origin[l].y - offset.y);
        // The low and mid layers keep the noise itself: the march adds 3D
        // noise to it before the threshold.
        c[l] = cover(point, layers[l].xy, seeds[l], map.z, l < 2);
    }
    result = vec4(c, 1.0);
}
)";

const char *CloudField = R"(
layout(location = 0) in vec2 uv;
layout(std140, binding = 0) uniform Clouds {
    mat4 inverse;
    mat4 previous;
    vec4 sun;
    vec4 camera;
    vec4 weatherMap;
    vec4 nearMap;
    vec4 farMap;
    vec4 slab;
    vec4 projection;
    vec4 bands;
    vec4 noiseLow;
    vec4 noiseMid;
    vec4 lighting;
    vec4 high;
    vec4 shadow;
    vec4 night;
    vec4 light;
    vec4 lightColour;
    vec4 shiftLowMid;
    vec4 shiftHigh;
    vec4 motion;
    vec4 shadowMid;
    vec4 motionHigh;
};
layout(binding = 1) uniform sampler2D weatherTexture;
layout(binding = 2) uniform sampler2D nearCover;
layout(binding = 3) uniform sampler2D farCover;
layout(binding = 4) uniform sampler3D shapeNoise;
layout(binding = 5) uniform sampler3D detailNoise;
layout(binding = 6) uniform sampler2D transmittanceTable;
layout(binding = 7) uniform sampler2D skyView;
layout(binding = 8) uniform sampler2D sceneDepth;
layout(binding = 9) uniform sampler2D curlNoise;
// The pass: the block's size, the pixel marched in each block (x, y), a new
// sample's share; the image's and the march's sizes; the cloud shell's
// planet radius (km), smaller than the Earth's.
layout(std140, binding = 10) uniform Update {
    vec4 update;
    vec4 sizes;
    vec4 shell;
};

// Light samples' offsets across the cone (unit-ish, fixed).
const vec3 Cone[8] = vec3[8](vec3(0.38, 0.92, -0.12), vec3(-0.71, 0.17, 0.68), vec3(0.15, -0.62, 0.77),
                             vec3(-0.85, -0.43, -0.30), vec3(0.62, -0.21, -0.75), vec3(-0.12, 0.81, 0.57),
                             vec3(0.55, 0.55, 0.62), vec3(-0.40, -0.80, 0.45));

vec2 transmittanceUv(float radius, float mu) {
    float h = sqrt(TopRadius * TopRadius - BottomRadius * BottomRadius);
    float rho = sqrt(max(0.0, radius * radius - BottomRadius * BottomRadius));
    float discriminant = radius * radius * (mu * mu - 1.0) + TopRadius * TopRadius;
    float d = max(0.0, -radius * mu + sqrt(max(0.0, discriminant)));
    float dMin = TopRadius - radius;
    float dMax = rho + h;
    return vec2((d - dMin) / (dMax - dMin), rho / h);
}

// Intersections of a ray from inside or below with a sphere around the
// planet's centre: the far one from inside, the near one from outside.
vec2 sphereHits(vec3 p, vec3 d, float radius) {
    float b = dot(p, d);
    float c = dot(p, p) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0)
        return vec2(-1.0);
    float root = sqrt(discriminant);
    return vec2(-b - root, -b + root);
}

float henyeyGreenstein(float c, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * Pi * pow(1.0 + g2 - 2.0 * g * c, 1.5));
}

float remap(float v, float a, float b, float c, float d) {
    return c + (v - a) / (b - a) * (d - c);
}

// How far a layer's weather and cover have moved since they were drawn
// (km east, north): they are read that much back.
vec2 layerShift(int layer) {
    return layer == 0 ? shiftLowMid.xy : (layer == 1 ? shiftLowMid.zw : shiftHigh.xy);
}

// A layer's place in the weather texture's columns, at a point given by its
// offset from the camera (km east, north).
vec2 weatherUv(vec2 offset, int layer) {
    offset -= layerShift(layer);
    vec2 w = (weatherMap.xy + vec2(offset.x, -offset.y) * 1000.0) / weatherMap.z;
    float size = weatherMap.w;
    return clamp(w, vec2(0.5 / size), vec2(1.0 - 0.5 / size));
}

// The weather's values (coverage, type, base, top km) of a layer at a point.
vec4 weatherAt(vec2 offset, int layer) {
    vec2 w = weatherUv(offset, layer);
    return textureLod(weatherTexture, vec2((float(layer) + w.x) / 4.0, w.y), 0.0);
}

// The density of a layer (0 low, 1 mid) at a point, against its type's
// usual (the weather's extras, moving with the layer).
float layerScale(vec2 offset, int layer) {
    vec2 w = weatherUv(offset, layer);
    return textureLod(weatherTexture, vec2((3.0 + w.x) / 4.0, w.y), 0.0)[layer];
}

// The cover maps' value of a layer (0 low, 1 mid, 2 high): the near map
// near the camera, then the far one.
float coverAt(vec2 offset, int layer) {
    offset -= layerShift(layer);
    vec2 m = vec2(offset.x, -offset.y) * 1000.0;
    vec2 nearUv = (nearMap.xy + m) / nearMap.z;
    float distance = length(offset);
    float nearShare = 1.0 - smoothstep(20.0, 28.0, distance);
    float value = 0.0;
    if (nearShare > 0.0)
        value = textureLod(nearCover, nearUv, 0.0)[layer] * nearShare;
    if (nearShare < 1.0)
        value += textureLod(farCover, (farMap.xy + m) / farMap.z, 0.0)[layer] * (1.0 - nearShare);
    return value;
}

// Extinction (1/km) of a layer (0 low, 1 mid) at a point.
float layerDensity(vec2 offset, float altitude, bool detail, int layer) {
    vec4 w = weatherAt(offset, layer);
    if (w.x <= 0.0 || altitude < w.z || altitude > w.w)
        return 0.0;
    float h = (altitude - w.z) / max(w.w - w.z, 0.05);
    float convective = clamp((w.y - 0.3) / 0.4, 0.0, 1.0);
    // The local coverage from the weather's 2D noise (even, 0-1): a half
    // where it crosses the coverage's threshold (the map's cloud edge),
    // all of it well inside a patch, none well outside.
    // The billows (Schneider 2015): Perlin-Worley eroded by Worley
    // octaves; finer for heaps than for sheets.
    vec4 offsets = layer == 0 ? noiseLow : noiseMid;
    vec3 metres = vec3(offset.x * 1000.0, altitude * 1000.0, -offset.y * 1000.0);
    vec3 q = (metres + vec3(offsets.x, 0.0, -offsets.y)) / ShapePeriod;
    vec4 s = textureLod(shapeNoise, q * mix(1.2, 3.2, convective), 0.0);
    // Storm clouds (cumulonimbus, type 0.8 to 1) are towers, then anvils
    // spreading at their top, which reach past their cells. Only in the
    // shaders built with STORMS (the march's variant for storms): the code
    // slowed every view a little.
#ifdef STORMS
    float storm = smoothstep(0.8, 1.0, w.y);
#else
    const float storm = 0.0;
#endif
    // The map read a little aside, by height: sides bulge and lean
    // instead of standing straight.
    vec2 warp = (s.gb - 0.5) * 0.5 * mix(0.5, 1.0, convective);
    float even = coverAt(offset + warp, layer);
    float threshold = 1.0 - w.x;
    float coverage = clamp(0.5 + (even - threshold) * mix(2.5, 1.8, convective), 0.0, 1.0);
    if (coverage <= 0.0 && (storm <= 0.0 || h < 0.7))
        return 0.0;
    float fbm = dot(s.gba, vec3(0.625, 0.25, 0.125));
    float base = clamp(remap(s.r, fbm - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0);
    float v = 0.0;
    if (storm < 1.0 && coverage > 0.0) {
        // The height profile by type, before the coverage: only a patch's
        // cores reach the top, so heaps rise as domes from flat bases;
        // sheets fill their layer with ragged edges.
        // Sheets vary in thickness with the 2D noise and the billows, so
        // their bases show lighter and darker patches (rolls, thin parts).
        float thickness = mix(0.45, 1.0, clamp(even * 1.2 + (s.r - 0.5) * 0.6, 0.0, 1.0));
        float hs = h / thickness;
        float stratusProfile = smoothstep(0.0, 0.25, hs + (s.g - 0.5) * 0.3) * (1.0 - smoothstep(0.55, 1.0, hs));
        float cumulusProfile = smoothstep(0.0, 0.06, h) * (1.0 - smoothstep(0.15, 1.0, h));
        float shaped = base * mix(stratusProfile, cumulusProfile, convective);
        v = clamp(remap(shaped, 1.0 - coverage, 1.0, 0.0, 1.0), 0.0, 1.0) * coverage;
    }
    if (storm > 0.0) {
        // The tower: its cell solid from a flat base, its walls billowing at
        // every height (the billows move its edge: not walls drawn straight
        // up from the 2D map, which stood as pillars of streaks), each part
        // of the cell as high as it lies deep in it: the edge stops low, the
        // core reaches the anvil; towers narrow upwards into domes, their
        // heights as their cells' strength. Weak cells stay heaps a km or two
        // tall between the storms, others congestus (alone, the towers stood
        // stark).
        float top = mix(0.12, 0.9, smoothstep(0.0, 1.0, (even - threshold) / 0.3));
        float edge = even + (s.r - 0.5) * 0.3 + (fbm - 0.5) * 0.2;
        float tower = clamp(0.5 + (edge - threshold) * 4.0, 0.0, 1.0)
                * smoothstep(0.0, 0.015, h) * (1.0 - smoothstep(top - 0.12, top, h + (s.g - 0.5) * 0.1));
        // The anvil: the top spread from the cores that reach it, streaming
        // downwind (shadowMid.zw: the map read upwind too), thinner (ice):
        // a quarter of the sky at the top where cells are a third. Its top
        // flat, its underside rising from the core to a thin edge.
        float anvil = 0.0;
        if (h > 0.72) {
            float spread = even;
            for (int k = 1; k <= 2; ++k)
                spread = max(spread, coverAt(offset - shadowMid.zw * (3.0 * float(k)) + warp, layer) - 0.06 * float(k));
            float inside = clamp((spread + (s.g - 0.5) * 0.15 - threshold + 0.05) / 0.35, 0.0, 1.0);
            float underside = mix(0.94, 0.74, inside);
            anvil = smoothstep(underside, underside + 0.04, h) * (1.0 - smoothstep(0.95, 1.0, h))
                    * smoothstep(0.0, 0.25, inside) * 0.3;
        }
        v = mix(v, max(tower * mix(0.65, 1.0, base), anvil), storm);
    }
    if (v <= 0.0)
        return 0.0;
    if (detail) {
        // Wisps at the bottom, billows at the top, eroding the edges.
        // Distorted by curl noise into swirls, as turbulence would
        // (Schneider 2015), more at the base.
        vec2 swirl = (textureLod(curlNoise, (metres.xz + vec2(offsets.x, -offsets.y)) / (ShapePeriod * 0.5), 0.0).rg
                      - 0.5) * 2.0;
        vec3 swirled = metres + vec3(swirl.x, 0.0, swirl.y) * 150.0 * (1.0 - 0.6 * h);
        vec3 dn = textureLod(detailNoise, (swirled + vec3(offsets.z, 0.0, -offsets.w))
                             / (DetailPeriod * mix(0.9, 0.55, convective)), 0.0).rgb;
        float dfbm = dot(dn, vec3(0.625, 0.25, 0.125));
        float erosion = mix(dfbm, 1.0 - dfbm, clamp(h * 5.0, 0.0, 1.0));
        // (less on a storm: its towers' edges are hard)
        v = clamp(remap(v, erosion * mix(mix(0.3, 0.55, convective), 0.3, storm), 1.0, 0.0, 1.0), 0.0, 1.0);
    }
    return v * mix(35.0, 70.0, convective) * layerScale(offset, layer);
}

// Extinction (1/km) of the low and mid layers at a point.
float density(vec3 p, vec2 offset, float altitude, bool detail) {
    float sum = 0.0;
    for (int layer = 0; layer < 2; ++layer)
        sum += layerDensity(offset, altitude, detail, layer);
    return sum;
}

// The cloud in the low and mid layers' columns at a point, as optical
// depth: above the altitude (x) and the whole column (y). Estimated from the
// weather and the cover as layerDensity makes them, the billows' noise at its
// mean, by its profile at three heights: a storm's tower some ten times a
// cumulus's (about 20).
vec2 cloudAbove(vec2 offset, float altitude) {
    vec2 depth = vec2(0.0);
    for (int layer = 0; layer < 2; ++layer) {
        vec4 w = weatherAt(offset, layer);
        if (w.x <= 0.0 || altitude >= w.w)
            continue;
        float convective = clamp((w.y - 0.3) / 0.4, 0.0, 1.0);
#ifdef STORMS
        float storm = smoothstep(0.8, 1.0, w.y);
#else
        const float storm = 0.0;
#endif
        float even = coverAt(offset, layer);
        float coverage = clamp(0.5 + (even - (1.0 - w.x)) * mix(2.5, 1.8, convective), 0.0, 1.0);
        if (coverage <= 0.0 && storm <= 0.0)
            continue;
        float from = clamp((altitude - w.z) / max(w.w - w.z, 0.05), 0.0, 1.0);
        vec2 sums = vec2(0.0);
        for (int k = 0; k < 3; ++k) {
            float f = (float(k) + 0.5) / 3.0;
            vec2 h = vec2(mix(from, 1.0, f), f);
            vec2 hs = h / 0.75;
            vec2 stratus = smoothstep(0.0, 0.25, hs) * (1.0 - smoothstep(0.55, 1.0, hs));
            vec2 cumulus = smoothstep(0.0, 0.06, h) * (1.0 - smoothstep(0.15, 1.0, h));
            vec2 base = 0.6 * mix(stratus, cumulus, convective);
            vec2 shaped = clamp((base - (1.0 - coverage)) / max(coverage, 0.01), 0.0, 1.0) * coverage;
            // A storm's tower and anvil (layerDensity), the billows at
            // their mean.
            float top = mix(0.12, 0.9, smoothstep(0.0, 1.0, (even - (1.0 - w.x)) / 0.3));
            vec2 tower = clamp(0.5 + (even - (1.0 - w.x)) * 4.0, 0.0, 1.0)
                    * smoothstep(0.0, 0.015, h) * (1.0 - smoothstep(top - 0.12, top, h)) * 0.8;
            float inside = clamp((even - (1.0 - w.x) + 0.05) / 0.35, 0.0, 1.0);
            float underside = mix(0.94, 0.74, inside);
            vec2 anvil = smoothstep(underside, underside + 0.04, h) * (1.0 - smoothstep(0.95, 1.0, h))
                    * smoothstep(0.0, 0.25, inside) * 0.3;
            sums += mix(shaped, max(tower, anvil), storm);
        }
        float column = mix(35.0, 70.0, convective) * layerScale(offset, layer) * max(w.w - w.z, 0.05) / 3.0;
        depth += sums * column * vec2(1.0 - from, 1.0);
    }
    return depth;
}
)";

// After the Clouds block (the march's and the composite's).
const char *SceneDistance = R"(
// Scene depth to distance along the view ray (km); -1 for none (sky).
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
    return viewZ * length(vec3(ndc.x / projection.x, ndc.y / projection.y, 1.0)) / 1000.0;
}
)";

// After the Clouds block and a skyView sampler (the march's and the
// composite's).
const char *SkyAir = R"(
// Sky light along a direction from the sky-view table.
vec3 skyLight(vec3 d) {
    float radius = BottomRadius + max(camera.x, 0.001);
    float horizonLength = sqrt(max(0.0, radius * radius - BottomRadius * BottomRadius));
    float beta = acos(clamp(horizonLength / radius, -1.0, 1.0));
    float zenithHorizon = Pi - beta;
    float angle = min(acos(clamp(d.y, -1.0, 1.0)), zenithHorizon - 1e-4);
    float c = 1.0 - sqrt(max(0.0, 1.0 - angle / zenithHorizon));
    vec2 dh = d.xz, sh = sun.xz;
    float lightViewCos = (dot(dh, dh) > 1e-10 && dot(sh, sh) > 1e-10) ? dot(normalize(dh), normalize(sh)) : 1.0;
    float u = sqrt(clamp(-lightViewCos * 0.5 + 0.5, 0.0, 1.0));
    return textureLod(skyView, vec2(fromUnitToSubUvs(u, 192.0), fromUnitToSubUvs(c * 0.5, 108.0)), 0.0).rgb;
}

// What the air between the camera and clouds a distance (km) along d lets
// through, on the cloud shell of the given radius (km): Rayleigh and Mie at
// the path's mean height and the weather's low haze (high.y, per km), each
// colour its own.
vec3 airTo(vec3 d, float distance, float radius) {
    vec3 p0 = vec3(0.0, radius + max(camera.x, 0.001), 0.0);
    float cloudAltitude = length(p0 + d * distance) - radius;
    float mean = max(0.5 * (camera.x + cloudAltitude), 0.0);
    vec3 sigma = vec3(5.802e-3, 13.558e-3, 33.1e-3) * exp(-mean / 8.0) + vec3(4.44e-3 * exp(-mean / 1.2))
            + vec3(high.y * exp(-mean / 1.5));
    return exp(-sigma * distance);
}
)";

const char *MarchFragment = R"(
layout(location = 0) out vec4 result;
// Where each pixel's march starts (CloudNoise::blue, 64 x 64).
layout(binding = 11) uniform sampler2D blueNoise;
// Where the pixel's clouds are, times their cover (km: front, mean
// distance; layer 0 low to 2 high), the cover.
layout(location = 1) out vec4 cloudDepth;
)";

const char *MarchMain = R"(
// The march's state along the ray: its light, what it lets through; where
// the cloud starts (2 % of the light taken), the mean distance by the light
// each sample takes, the layer by it (mid 1, high 2).
vec3 marchLight;
float marchThrough;
float marchFront, marchDistance, marchWeights, marchLayers;
// Along the ray: the shell's radius, towards the clouds' light, the angle's
// cosine to it, the sky's light from above.
float marchRadius;
vec3 marchLightDir;
float marchCos;
vec3 marchSky;

// The low and mid layers' extinction at a point (1/km), the mid's apart.
float cloudDensity(vec3 p, out float mid) {
    float altitude = length(p) - marchRadius;
    vec2 offset = vec2(p.x, -p.z);
    mid = layerDensity(offset, altitude, true, 1);
    return layerDensity(offset, altitude, true, 0) + mid;
}

// The light a cloud of extinction sigma at a point scatters towards the
// camera.
vec3 cloudLight(vec3 p, float sigma) {
    float R = marchRadius;
    vec3 lightDir = marchLightDir;
    float cosAngle = marchCos;
    vec3 skyAbove = marchSky;
    float phaseScale[3] = float[3](1.0, 0.5, 0.25);
    float altitude = length(p) - R;
    vec2 offset = vec2(p.x, -p.z);
    // Towards the sun in growing steps spread in a cone, which
    // smooths the banding and gathers the neighbours' density, then
    // one far sample for the shadows of distant clouds (Schneider
    // 2015).
    float opticalDepth = 0.0;
    float stepLength = 0.05;
    float along = 0.0;
    for (int k = 0; k < int(lighting.x); ++k) {
        along += stepLength;
        vec3 q = p + lightDir * along + Cone[k] * (stepLength * 0.6);
        opticalDepth += density(q, vec2(q.x, -q.z), length(q) - R, false) * stepLength;
        stepLength *= 1.8;
    }
    {
        vec3 q = p + lightDir * (along + 2.0);
        opticalDepth += density(q, vec2(q.x, -q.z), length(q) - R, false) * 0.8;
    }
    // The sun's light as on the Earth at this height and place: the
    // smaller shell would tilt a far cloud's sky away from the sun
    // and set it early.
    vec3 earthPoint = vec3(p.x, BottomRadius + altitude, p.z);
    float earthRadius = length(earthPoint);
    vec3 up = earthPoint / earthRadius;
    float sunCos = dot(up, lightDir);
    vec3 sunLight = textureLod(transmittanceTable, transmittanceUv(earthRadius, sunCos), 0.0).rgb
            * lightColour.rgb;
    vec2 ground = sphereHits(earthPoint, lightDir, BottomRadius);
    if (ground.x > 0.0)
        sunLight = vec3(0.0);
    // The cloud above it and its column (cloudAbove).
    vec2 column = cloudAbove(offset, altitude);
    // Multiple scattering in octaves (Wrenninge 2013).
    float scattered = 0.0;
    for (int o = 0; o < 3; ++o) {
        float a = phaseScale[o];
        float phase = mix(henyeyGreenstein(cosAngle, 0.6 * a), henyeyGreenstein(cosAngle, -0.3 * a), 0.3);
        scattered += a * exp(-opticalDepth * a) * phase;
    }
    // Beer-powder (Schneider 2015): cloud gathers light, so its
    // edges facing the light are darker than its depths; seen only
    // looking away from the sun.
    float powder = 1.0 - exp(-2.0 * (opticalDepth + sigma * 0.05));
    scattered *= mix(1.0, 2.0 * powder, clamp(0.5 - 0.5 * cosAngle, 0.0, 1.0) * 0.8);
    float h = clamp((altitude - slab.x) / max(slab.y - slab.x, 0.1), 0.0, 1.0);
    // The sky's light reaches less far in: less at the bottom of
    // the layer and where the sun's path is dense (tuned on
    // cumulus); under more cloud than a cumulus has above that
    // height, less by what the cloud above lets through
    // (two-stream): a storm's base and its lower sides dark.
    float deeper = min(1.0, (1.0 + 0.11 * 20.0 * (1.0 - h)) / (1.0 + 0.11 * column.x));
    float open = mix(0.15, 1.0, h * h) * mix(0.4, 1.0, exp(-opticalDepth * 0.05)) * deeper;
    // The ground's light from below, where the sun reaches the
    // ground (not under a covered sky); under a cloud many times a
    // cumulus's depth (a storm, wide as it is deep) the ground
    // below lies in its shadow.
    float sunnyGround = (1.0 - 0.9 * max(weatherAt(offset, 0).x, weatherAt(offset, 1).x))
            * (1.0 - smoothstep(20.0, 150.0, column.y));
    // Through a deep cloud, the sky's light is the cloud's own,
    // scattered many times: grey, not the sky's blue.
    vec3 skyThrough = mix(vec3(dot(skyAbove, vec3(0.2126, 0.7152, 0.0722))), skyAbove, deeper);
    vec3 ambient = skyThrough * open * 0.7
            + vec3(0.11, 0.11, 0.09) * sunLight * max(sunCos, 0.0) * (1.0 - h) * sunnyGround / Pi;
    // Light diffused through a thick layer, which the octaves miss
    // (two-stream: 1 / (1 + 0.75 (1 - g) depth), g about 0.85): what
    // lights an overcast's base; nothing near the surface.
    // (the sun's path marched only a few km: at least the cloud
    // above, for a storm's tower)
    float diffused = (1.0 - exp(-opticalDepth / 6.0)) / (1.0 + 0.11 * max(opticalDepth, column.x));
    return sunLight * (scattered * Pi + max(sunCos, 0.0) * diffused * 0.14) + ambient;
}

// A sample of extinction sigma (the mid layer's sigmaMid), dt long at t along
// the ray: what it lets through, where it is (its light added by the march).
void addSample(float sigma, float sigmaMid, float t, float dt) {
    if (sigma <= 0.0)
        return;
    float before = marchThrough;
    marchThrough *= exp(-sigma * dt);
    marchDistance += t * (before - marchThrough);
    marchWeights += before - marchThrough;
    marchLayers += (before - marchThrough) * sigmaMid / sigma;
    if (marchFront < 0.0 && marchThrough < 0.98)
        marchFront = t;
}

#ifdef RAIN
// Rain falling from the low clouds, seen from under their base: grey
// curtains where the weather's rain falls (its rate, column 4 of the
// weather), under the cells for showers (as WeatherDetail::precipitation),
// streaked along the fall, slanted by the wind (sun.w, lightColour.w: km
// aside a km of fall), lit by what the cloud above lets through: dark under
// a storm, grey under a deck. From the near scene's end (near, km: the
// composite draws the clouds over the sky and the distant scene only; begun
// nearer, the rain covered distant terrain and not near terrain, with a
// seam between them) to the base, the ground or the march's limit (the
// storms on the horizon stand 30 to 60 km off), its samples closer near.
void marchRain(vec3 p0, vec3 d, float R, float near, float limit, float jitter) {
    vec2 baseHits = sphereHits(p0, d, R + slab.x);
    vec2 groundHits = sphereHits(p0, d, R);
    float far = baseHits.y > 0.0 ? baseHits.y : limit;
    if (groundHits.x > 0.0)
        far = min(far, groundHits.x);
    far = min(far, limit);
    float Near = max(near, 0.3);
    if (far <= Near)
        return;
    vec3 earthPoint = vec3(0.0, BottomRadius + camera.x, 0.0);
    float sunCos = marchLightDir.y;
    vec3 sunLight = textureLod(transmittanceTable, transmittanceUv(length(earthPoint), sunCos), 0.0).rgb
            * lightColour.rgb;
    vec3 grey = vec3(dot(marchSky, vec3(0.2126, 0.7152, 0.0722)));
    const int Steps = 16;
    float span = far - Near;
    for (int i = 0; i < Steps; ++i) {
        float f = (float(i) + jitter) / float(Steps);
        float t = Near + span * f * f;
        float dt = span * (2.0 * f + 1.0 / float(Steps)) / float(Steps);
        vec3 p = p0 + d * t;
        float altitude = length(p) - R;
        vec2 offset = vec2(p.x, -p.z);
        vec4 w = weatherAt(offset, 0);
        if (w.x <= 0.0 || altitude >= w.z)
            continue;
        vec2 at = weatherUv(offset, 0);
        vec4 extra = textureLod(weatherTexture, vec2((3.0 + at.x) / 4.0, at.y), 0.0);
        if (extra.z <= 0.0)
            continue;
        // Fallen from the cloud upwind.
        vec2 from = offset - vec2(sun.w, lightColour.w) * (w.z - altitude);
        float convective = clamp((w.y - 0.45) / 0.35, 0.0, 1.0);
        // Showers from their cells' cores (hard edged as the towers).
        float cell = clamp(0.5 + (coverAt(from, 0) - (1.0 - w.x) - 0.05) * 4.0, 0.0, 1.0);
        float rate = extra.z * mix(1.0, min(cell / max(w.x, 0.1), 3.0), convective);
        if (rate <= 0.0)
            continue;
        // Streaks along the fall, lighter near the ground; snow even.
        vec3 metres = vec3(from.x * 1000.0, altitude * 1000.0, -from.y * 1000.0);
        float streak = smoothstep(0.25, 0.75, textureLod(shapeNoise, metres / vec3(2500.0, 25000.0, 2500.0), 0.0).r);
        float sigma = 0.35 * pow(rate, 0.63) * mix(0.5, 1.0, convective)
                * mix(mix(0.15, 1.85, streak), 1.0, extra.w) * mix(0.7, 1.0, altitude / max(w.z, 0.1));
        // Lit through the cloud above (two-stream; its column as cloudAbove's
        // estimate, at the cell's cover).
        float column = mix(35.0, 70.0, clamp((w.y - 0.3) / 0.4, 0.0, 1.0)) * extra.x * max(w.w - w.z, 0.05)
                * mix(0.35, 0.7, smoothstep(0.8, 1.0, w.y)) * max(cell, 1.0 - convective);
        float through = 1.0 / (1.0 + 0.11 * column);
        vec3 lit = (mix(grey, marchSky, through) * 0.7 + sunLight * max(sunCos, 0.0) * 0.25) * through
                * mix(1.0, 1.8, extra.w);
        float before = marchThrough;
        addSample(sigma, 0.0, t, dt);
        marchLight += lit * (before - marchThrough);
    }
}
#endif

void main() {
    // This march texel is one pixel of its block of the cloud image: the
    // one this pass updates.
    vec2 pixel = floor(uv * sizes.zw) * update.x + update.yz;
    vec2 imageUv = (pixel + 0.5) / sizes.xy;
    vec2 ndc = vec2(imageUv.x * 2.0 - 1.0, (imageUv.y * 2.0 - 1.0) * camera.y);
    vec4 far = inverse * vec4(ndc, 1.0, 1.0);
    vec3 d = normalize(far.xyz / far.w);
    // The farthest scene depth under the image's pixel (3 x 3 samples over
    // it), so clouds behind a hill's edge are marched; the composite cuts
    // them at the full resolution.
    vec2 depthSize = vec2(textureSize(sceneDepth, 0));
    float depth = 0.0;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            vec2 at = (pixel + (vec2(i, j) + 0.5) / 3.0) / sizes.xy * depthSize;
            depth = max(depth, texelFetch(sceneDepth, ivec2(clamp(floor(at), vec2(0.0), depthSize - 1.0)), 0).r);
        }
    float scene = sceneDistance(depth, ndc);
    // Only distant terrain stops the march. Nearer things (the scene band:
    // objects, near terrain) never stand behind a cloud; the composite cuts
    // the clouds at them at the full resolution. Stopped at them, a pixel
    // would keep "no cloud" in the history after they moved off it (with
    // the camera) until its next update.
    bool distantScene = depth >= bands.z && depth < bands.w;
    float limit = distantScene ? min(scene, slab.z) : slab.z;

    // The shell around a smaller planet than the Earth (shell.x), so the
    // clouds reach the horizon within the weather the march has.
    float R = shell.x;
    vec3 p0 = vec3(0.0, R + max(camera.x, 0.001), 0.0);
    float pr = length(p0);
    vec2 bottomHits = sphereHits(p0, d, R + slab.x);
    vec2 topHits = sphereHits(p0, d, R + slab.y);
    vec2 groundHits = sphereHits(p0, d, R);
    float start, end;
    if (pr < R + slab.x) {
        // Below the clouds: from the bottom of the shell to its top, unless
        // the ground comes first.
        start = bottomHits.y;
        end = groundHits.x > 0.0 ? -1.0 : topHits.y;
    } else if (pr < R + slab.y) {
        start = 0.0;
        end = (bottomHits.x > 0.0) ? bottomHits.x : topHits.y;
    } else {
        start = topHits.x;
        end = bottomHits.x > 0.0 ? bottomHits.x : topHits.y;
    }
    end = min(end, limit);

    // The clouds' light: the sun, or the moon at night (task 31); the sky's
    // light keeps the sun.
    vec3 lightDir = normalize(light.xyz);
    float cosAngle = dot(d, lightDir);
    // Each pixel starts the march at its own place (blue noise: the
    // interleaved gradient noise before left what error remains in its
    // diagonal pattern, a moiré on storms), moved on each frame unless
    // still.
    // (by the image's pixel: the pixels of a block are marched by different
    // passes on the same march texel)
    float jitter = fract(texelFetch(blueNoise, ivec2(mod(pixel, vec2(64.0))), 0).r + camera.w);
    marchRadius = R;
    marchLightDir = lightDir;
    marchCos = cosAngle;
    marchLight = vec3(0.0);
    marchThrough = 1.0;
    marchFront = -1.0;
    marchDistance = 0.0;
    marchWeights = 0.0;
    marchLayers = 0.0;
    // Light of the sky from above and of the ground from below.
    vec3 skyAbove = skyLight(vec3(0.0, 1.0, 0.0)) * 0.6 + skyLight(normalize(vec3(sun.x, 0.0, sun.z) + vec3(0.0, 0.3, 0.0))) * 0.4;
    marchSky = skyAbove;
#ifdef RAIN
    if (pr < R + slab.x)
        marchRain(p0, d, R, projection.w * length(vec3(ndc.x / projection.x, ndc.y / projection.y, 1.0)) / 1000.0,
                  limit, jitter);
#endif
    if (start >= 0.0 && end > start) {
        int steps = int(slab.w);
        float segment = end - start;
        float previousT = start;
#ifdef DEEP_SPLIT
        // Steps longer than the clouds' shapes show at their distance in a
        // deep layer (a storm's slab 10 km tall: the steps spread over some
        // 30 km of ray, 400 m and more each at High, twice that at Low, left
        // its towers grainy and hatched by each pixel's start) are split
        // into samples no longer than that (high.w at most), each lit on
        // its own (a lit cloud is far brighter at its surface than a few
        // hundred metres in), while the ray's budget lasts (high.z; only
        // then this variant). A probe without the detail first (it only
        // erodes), so empty steps cost as before; other steps (thin layers:
        // their long steps lie far off, where they do not show) are marched
        // as they were.
        int extra = int(high.z);
        bool wasCloud = false;
        // One sample an iteration (the lighting's code once in the shader;
        // twice, it ran slower): a split step's further samples pending
        // (its end, their length, how many).
        int s = 0;
        float pendingEnd = 0.0, pendingDt = 0.0;
        int pendingLeft = 0;
        for (int n = 0; n < steps + int(high.z); ++n) {
            float t, dt;
            bool coarse = pendingLeft == 0;
            if (!coarse) {
                --pendingLeft;
                t = pendingEnd - pendingDt * float(pendingLeft);
                dt = pendingDt;
            } else {
                if (s >= steps)
                    break;
                float f = (float(s) + jitter) / float(steps);
                t = start + segment * f * f;
                dt = max(t - previousT, 0.0);
                if (s == 0)
                    dt = segment * (1.0 / float(steps)) * (1.0 / float(steps));
                previousT = t;
                ++s;
                float longest = max(0.12, t * 0.01);
                if (extra > 0 && dt > longest) {
                    vec3 p = p0 + d * t;
                    vec2 offset = vec2(p.x, -p.z);
                    vec4 low = weatherAt(offset, 0), mid = weatherAt(offset, 1);
                    if (max(low.x > 0.0 ? low.w - low.z : 0.0, mid.x > 0.0 ? mid.w - mid.z : 0.0) >= 3.0) {
                        bool cloud = density(p, offset, length(p) - R, false) > 0.0;
                        // (into the step after a cloud too: its trailing edge)
                        bool skip = !cloud && !wasCloud;
                        wasCloud = cloud;
                        if (skip)
                            continue;
                        int split = min(int(ceil(dt / longest)), min(int(high.w), extra + 1));
                        extra -= split - 1;
                        dt /= float(split);
                        pendingEnd = t;
                        pendingDt = dt;
                        pendingLeft = split - 1;
                        t -= dt * float(split - 1);
                        coarse = false;
                    }
                }
            }
            vec3 q = p0 + d * t;
            float mid;
            float sigma = cloudDensity(q, mid);
            if (coarse)
                wasCloud = sigma > 0.0;
            if (sigma > 0.0) {
                float through = marchThrough;
                addSample(sigma, mid, t, dt);
                marchLight += cloudLight(q, sigma) * (through - marchThrough);
            }
            if (marchThrough < 0.01)
                break;
        }
#else
        for (int s = 0; s < steps; ++s) {
            float f = (float(s) + jitter) / float(steps);
            float t = start + segment * f * f;
            float dt = max(t - previousT, 0.0);
            if (s == 0)
                dt = segment * (1.0 / float(steps)) * (1.0 / float(steps));
            previousT = t;
            vec3 p = p0 + d * t;
            float mid;
            float sigma = cloudDensity(p, mid);
            if (sigma <= 0.0)
                continue;
            float through = marchThrough;
            addSample(sigma, mid, t, dt);
            marchLight += cloudLight(p, sigma) * (through - marchThrough);
            if (marchThrough < 0.01)
                break;
        }
#endif
    }
    vec3 luminance = marchLight;
    float transmittance = marchThrough;
    float front = marchFront;
    float weightedDistance = marchDistance, weights = marchWeights, layers = marchLayers;
    float highShare = 0.0, highDistance = 0.0;
    // The flat high layer, behind the others.
    if (high.x > 0.0 && pr < R + high.x) {
        float th = sphereHits(p0, d, R + high.x).y;
        if (th > 0.0 && th < slab.z * 1.6 && !distantScene && sphereHits(p0, d, R).x < 0.0) {
            vec3 p = p0 + d * th;
            vec2 offset = vec2(p.x, -p.z);
            float c = coverAt(offset, 2);
            if (c > 0.0) {
                vec3 metres = vec3(offset.x * 1000.0, 0.0, -offset.y * 1000.0);
                vec4 s = textureLod(shapeNoise, vec3(metres.x / 9000.0, 0.37, metres.z / 3000.0), 0.0);
                float alpha = c * clamp(0.25 + 0.6 * s.g, 0.0, 1.0) * 0.6;
                vec3 earthPoint = vec3(p.x, BottomRadius + high.x, p.z);
                vec3 sunLight = textureLod(transmittanceTable, transmittanceUv(length(earthPoint),
                                                                               dot(normalize(earthPoint), lightDir)), 0.0).rgb
                        * lightColour.rgb;
                vec3 lit = sunLight * (henyeyGreenstein(cosAngle, 0.7) * 0.6 + 0.4 / (4.0 * Pi)) * 4.0 * Pi * 0.3
                        + skyAbove * 0.8;
                luminance += transmittance * lit * alpha;
                highShare = transmittance * alpha;
                highDistance = th;
                transmittance *= 1.0 - alpha;
                if (front < 0.0 && transmittance < 0.98)
                    front = th;
            }
        }
    }
    // The air between the camera and the clouds (at their mean distance, all
    // layers): the clouds fade towards the sky's light along the ray, mixed
    // in linear light, so bright clouds still show through haze. Over the
    // scene the composite takes this air's light back out: the clouds fade
    // into the scene there, which has its own fog.
    float cover = weights + highShare;
    float mean = cover > 0.0 ? (weightedDistance + highDistance * highShare) / cover : -1.0;
    if (transmittance < 1.0 && mean > 0.0) {
        vec3 air = airTo(d, mean, R);
        luminance = luminance * air + skyLight(d) * (vec3(1.0) - air) * (1.0 - transmittance);
    }
    result = vec4(luminance, transmittance);
    // Times the cover, so the history blends them as it blends the clouds
    // (a pixel without clouds adds nothing).
    if (front < 0.0)
        front = max(mean, 0.0);
    cloudDepth = vec4(front * cover, weightedDistance + highDistance * highShare, layers + 2.0 * highShare, cover);
}
)";

const char *ShadowFragment = R"(
layout(location = 0) out vec4 result;
)";

const char *ShadowMain = R"(
// Transmittance along the clouds' light (the sun, or the moon at night) from
// points of a plane under the clouds, around the camera: the lit shaders
// take that light through it. A map per layer, each on a grid that drifts
// with its layer: what passes straight through, low in red, mid in green
// (the light shafts' rays); and what passes in all, the light scattered
// through the cloud too (two-stream, 1 / (1 + 0.75 (1 - g) depth), g about
// 0.85, as the march's diffused light), low in blue, mid in alpha: a thin
// cloud's shadow light, a cumulus's about a quarter of the sun, a storm's
// tower almost none (the lit shaders).
void main() {
    vec3 lightDir = normalize(light.xyz);
    if (lightDir.y < 0.03) {
        result = vec4(1.0);
        return;
    }
    // Rows from north to south; km east and north of the camera. Each
    // map's centre stays on whole texels of its layer's own ground (its
    // drift taken off; shadow.zw and shadowMid.xy from the camera): drawn
    // again, the texels fall on the same points of the clouds.
    vec2 cell = vec2(uv.x - 0.5, 0.5 - uv.y) * shadow.x;
    vec2 low = cell + shadow.zw;
    vec2 mid = cell + shadowMid.xy;
    float bottom = (slab.x - shadow.y) / lightDir.y;
    float top = (slab.y - shadow.y) / lightDir.y;
    const int Steps = 20;
    float dt = (top - bottom) / float(Steps);
    vec2 opticalDepth = vec2(0.0);
    for (int s = 0; s < Steps; ++s) {
        float t = bottom + (float(s) + 0.5) * dt;
        vec2 along = vec2(lightDir.x, -lightDir.z) * t;
        float altitude = shadow.y + lightDir.y * t;
        opticalDepth.x += layerDensity(low + along, altitude, false, 0) * dt;
        opticalDepth.y += layerDensity(mid + along, altitude, false, 1) * dt;
    }
    result = vec4(exp(-opticalDepth), 1.0 / (1.0 + 0.11 * opticalDepth));
}
)";

// The sky's light through the clouds around the camera: what they let
// through straight down (two-stream, the layers' depths added), over the
// shadow map's extent centred on the camera.
const char *SkyLightMain = R"(
void main() {
    vec2 ground = vec2(uv.x - 0.5, 0.5 - uv.y) * shadow.x;
    const int Steps = 16;
    float dt = (slab.y - slab.x) / float(Steps);
    float opticalDepth = 0.0;
    for (int s = 0; s < Steps; ++s) {
        float altitude = slab.x + (float(s) + 0.5) * dt;
        opticalDepth += (layerDensity(ground, altitude, false, 0) + layerDensity(ground, altitude, false, 1)) * dt;
    }
    result = vec4(1.0 / (1.0 + 0.11 * opticalDepth), 0.0, 0.0, 1.0);
}
)";

// Their cover (the share of the ground under clouds) and the mean of what the
// clouds let through.
const char *SkyMeanFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(binding = 0) uniform sampler2D through;

void main() {
    float cloudy = 0.0, cloudyThrough = 0.0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            float t = texelFetch(through, ivec2(x, y), 0).r;
            if (t < 0.95) {
                cloudy += 1.0;
                cloudyThrough += t;
            }
        }
    result = vec4(cloudy / 4096.0, cloudy > 0.0 ? cloudyThrough / cloudy : 1.0, 0.0, 1.0);
}
)";

// The march blended with the last frames' result in the same direction,
// clamped to this frame's neighbourhood.
const char *TemporalFragment = R"(
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(location = 1) out vec4 imageOut;
layout(location = 2) out vec4 depthResult;
layout(location = 3) out vec4 depthOut;
layout(std140, binding = 0) uniform Clouds {
    mat4 inverse;
    mat4 previous;
    vec4 sun;
    vec4 camera;
    vec4 weatherMap;
    vec4 nearMap;
    vec4 farMap;
    vec4 slab;
    vec4 projection;
    vec4 bands;
    vec4 noiseLow;
    vec4 noiseMid;
    vec4 lighting;
    vec4 high;
    vec4 shadow;
    vec4 night;
    vec4 light;
    vec4 lightColour;
    vec4 shiftLowMid;
    vec4 shiftHigh;
    vec4 motion;
    vec4 shadowMid;
    vec4 motionHigh;
};
layout(binding = 1) uniform sampler2D current;
layout(binding = 2) uniform sampler2D history;
layout(binding = 3) uniform sampler2D currentDepth;
layout(binding = 4) uniform sampler2D historyDepth;
layout(std140, binding = 10) uniform Update {
    vec4 update;
    vec4 sizes;
    vec4 shell;
};

// The cloud image and where its clouds are: the pixels this pass marched
// (one of each block), the others from the last frames where the clouds
// were (the view's turn and the wind's drift reprojected, clamped to the
// marched neighbourhood); off the last frame's image, the march's own
// result. While filling (still, or no history), the image is built by one
// pass per pixel of the block in the same frame.
void cloudImage(out vec4 image, out vec4 depth) {
    vec2 pixel = floor(uv * sizes.xy);
    vec2 cell = floor(pixel / update.x);
    bool marched = all(equal(pixel - cell * update.x, update.yz));
    ivec2 marchTexel = ivec2(clamp(cell, vec2(0.0), sizes.zw - 1.0));
    vec4 fresh = texelFetch(current, marchTexel, 0);
    vec4 freshDepth = texelFetch(currentDepth, marchTexel, 0);
    if (lighting.y > 0.5) {
        image = marched ? fresh : textureLod(history, uv, 0.0);
        depth = marched ? freshDepth : textureLod(historyDepth, uv, 0.0);
        return;
    }
    vec4 low = fresh, highValue = fresh;
    vec4 lowDepth = freshDepth, highDepth = freshDepth;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            ivec2 at = ivec2(clamp(cell + vec2(i, j), vec2(0.0), sizes.zw - 1.0));
            vec4 n = texelFetch(current, at, 0);
            low = min(low, n);
            highValue = max(highValue, n);
            vec4 nd = texelFetch(currentDepth, at, 0);
            lowDepth = min(lowDepth, nd);
            highDepth = max(highDepth, nd);
        }
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * camera.y);
    vec4 far = inverse * vec4(ndc, 1.0, 1.0);
    vec3 d = normalize(far.xyz / far.w);
    // Where the clouds seen here were at the last frame: they drift with
    // their layer's wind, at their own distance along the ray (the march's
    // mean; its layer between low, mid and high). Without clouds, the low
    // layer's middle height; far off only the view's turn counts.
    vec3 before = d;
    if (freshDepth.w > 0.01) {
        float t = freshDepth.y / freshDepth.w;
        float layer = clamp(freshDepth.z / freshDepth.w, 0.0, 2.0);
        vec2 move = layer < 1.0 ? mix(motion.xy, motion.zw, layer) : mix(motion.zw, motionHigh.xy, layer - 1.0);
        before = d * t - vec3(move.x, 0.0, -move.y);
    } else if (d.y > 0.01) {
        float t = min(max(motionHigh.z - camera.x, 0.05) / d.y, slab.z);
        before = d * t - vec3(motion.x, 0.0, -motion.y);
    }
    vec4 clip = previous * vec4(before, 0.0);
    vec2 beforeUv = vec2(-1.0);
    if (clip.w > 0.0) {
        vec2 before = clip.xy / clip.w;
        beforeUv = vec2(before.x * 0.5 + 0.5, (before.y * camera.y) * 0.5 + 0.5);
    }
    bool onImage = all(greaterThanEqual(beforeUv, vec2(0.0))) && all(lessThanEqual(beforeUv, vec2(1.0)));
    if (!onImage) {
        // New sky at the image's edge: the march's result near it.
        image = marched ? fresh : textureLod(current, uv, 0.0);
        depth = marched ? freshDepth : textureLod(currentDepth, uv, 0.0);
        return;
    }
    // Clamped loosely: the marched neighbours lie a block apart.
    vec4 spread = (highValue - low) * 0.25;
    vec4 old = clamp(textureLod(history, beforeUv, 0.0), low - spread, highValue + spread);
    vec4 depthSpread = (highDepth - lowDepth) * 0.25;
    vec4 oldDepth = clamp(textureLod(historyDepth, beforeUv, 0.0), lowDepth - depthSpread, highDepth + depthSpread);
    image = marched ? mix(old, fresh, update.w) : old;
    depth = marched ? mix(oldDepth, freshDepth, update.w) : oldDepth;
}

// The history (alternating) and the same image in a fixed texture for the
// composite and the light shafts; their depths likewise.
void main() {
    cloudImage(result, depthResult);
    imageOut = result;
    depthOut = depthResult;
}
)";

// Over the view, where nothing nearer than the clouds stands: display colour
// with premultiplied alpha (dst * T + colour * (1 - T)).
const char *CompositeFragment = R"(
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
#ifdef GLOW
// The glow (bloom) behind the clouds dims with them.
layout(location = 1) out vec4 glow;
#endif
layout(std140, binding = 0) uniform Clouds {
    mat4 inverse;
    mat4 previous;
    vec4 sun;
    vec4 camera;
    vec4 weatherMap;
    vec4 nearMap;
    vec4 farMap;
    vec4 slab;
    vec4 projection;
    vec4 bands;
    vec4 noiseLow;
    vec4 noiseMid;
    vec4 lighting;
    vec4 high;
    vec4 shadow;
    vec4 night;
    vec4 light;
    vec4 lightColour;
    vec4 shiftLowMid;
    vec4 shiftHigh;
    vec4 motion;
    vec4 shadowMid;
    vec4 motionHigh;
};
layout(binding = 1) uniform sampler2D clouds;
layout(binding = 2) uniform sampler2D sceneDepth;
layout(binding = 3) uniform sampler2D cloudDepth;
layout(binding = 4) uniform sampler2D skyView;
)";

const char *CompositeMain = R"(
void main() {
    vec4 c = textureLod(clouds, uv, 0.0);
    float depth = textureLod(sceneDepth, uv, 0.0).r;
#ifdef GLOW
    glow = vec4(0.0, 0.0, 0.0, 1.0);
#endif
    if (c.a >= 0.999) {
        result = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    // How much of the clouds stands in front of the scene here, at the full
    // resolution: none of a cloud behind it, all of it past the clouds' mean
    // distance, between them in part. The march stops at distant terrain
    // only; nearer things (objects, near terrain) are cut here, so a moving
    // object leaves no outline in the clouds' history. Above the clouds, in
    // them, or a mountain in their base: the scene behind them is clouded.
    float share = 1.0;
    vec3 light = c.rgb;
    float covered = 1.0 - c.a;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * camera.y);
    float scene = sceneDistance(depth, ndc);
    if (scene >= 0.0) {
        vec4 where = textureLod(cloudDepth, uv, 0.0);
        if (where.w > 1e-3) {
            float front = where.x / where.w;
            float mean = max(where.y / where.w, front);
            share = clamp((scene - front) / max(mean - front, 0.05), 0.0, 1.0);
            // The march faded the clouds towards the sky's light; over the
            // scene they fade into it instead (it has its own fog), as far
            // as the clearest colour lets them: the air's light in front of
            // them taken back out. (Far clouds in front of hazy mountains
            // showed as patches of sky; user, 2026-10-13, the env file's
            // weather on POLAND.)
            vec4 far = inverse * vec4(ndc, 1.0, 1.0);
            vec3 d = normalize(far.xyz / far.w);
            vec3 air = airTo(d, where.y / where.w, motionHigh.w);
            float clearest = max(air.r, max(air.g, air.b));
            // (Rayleigh's blue, fading faster, keeps its share of the sky.)
            light = max(light - skyLight(d) * ((1.0 - clearest) * covered), vec3(0.0));
            covered *= clearest;
        } else {
            // (no place known: nearer things in front, as before)
            share = depth < bands.z ? 0.0 : 1.0;
        }
    }
    if (share <= 0.0 || covered <= 1e-4) {
        result = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 colour = light / max(covered, 1e-3);
    vec3 display = pow(1.0 - exp(-colour * camera.z), vec3(1.0 / 2.2));
    // At night a little darker than the night sky's floor, not black.
    display = max(display, night.rgb * 0.8) * night.w;
    covered *= share;
    result = vec4(display * covered, 1.0 - covered);
#ifdef GLOW
    glow = vec4(0.0, 0.0, 0.0, 1.0 - covered);
#endif
}
)";

QByteArray cloudSource(const char *body, bool common) {
    QByteArray source("#version 440\n");
    source += QByteArray("const float ShapePeriod = ") + QByteArray::number(ShapePeriod, 'f', 1) + ";\n";
    source += QByteArray("const float DetailPeriod = ") + QByteArray::number(DetailPeriod, 'f', 1) + ";\n";
    if (common)
        source += CloudCommon;
    return source + body;
}

// Deleted when the frame being recorded is submitted: views drawn earlier
// in the frame (the reflection, the cube's faces) may use it still, and
// QRhi objects a frame refers to must stay until then.
template <typename T>
void dropLater(T *resource) {
    if (resource != nullptr)
        resource->deleteLater();
}

double positiveModulo(double value, double period) {
    const double r = std::fmod(value, period);
    return r < 0.0 ? r + period : r;
}

}

static QByteArray glowVariant(QByteArray source) {
    return source.replace("#version 440\n", "#version 440\n#define GLOW\n");
}

static QByteArray marchSource() {
    return cloudSource(MarchFragment, true) + CloudField + SceneDistance + SkyAir + MarchMain;
}

// The march for heavy weather (CloudView::deep): long steps in deep layers
// split, storm clouds' towers and anvils, rain under the clouds.
static QByteArray deepVariant(QByteArray source) {
    return source.replace("#version 440\n", "#version 440\n#define DEEP_SPLIT\n#define STORMS\n#define RAIN\n");
}

// The shadow map and the sky's light always take storms' shapes (drawn
// seldom, small).
static QByteArray stormShapes(QByteArray source) {
    return source.replace("#version 440\n", "#version 440\n#define STORMS\n");
}

static QByteArray shadowSource() {
    return stormShapes(cloudSource(ShadowFragment, true) + CloudField + ShadowMain);
}

static QByteArray skyLightSource() {
    return stormShapes(cloudSource(ShadowFragment, true) + CloudField + SkyLightMain);
}

static QByteArray compositeSource() {
    return cloudSource(CompositeFragment, true) + SceneDistance + SkyAir + CompositeMain;
}

QList<QByteArray> RhiRenderer::cloudShaders() {
    return {cloudSource(CoverFragment, true), marchSource(), deepVariant(marchSource()), shadowSource(),
            cloudSource(TemporalFragment, false),
            compositeSource(), glowVariant(compositeSource()), skyLightSource(), SkyMeanFragment};
}

namespace {

struct CloudShaderSet {
    QShader vertex, cover, march, marchDeep, shadow, skyLight, skyMean, temporal, composite, glowComposite;
};

const CloudShaderSet &cloudShaderSet(QRhi *rhi) {
    static CloudShaderSet s;
    if (!s.vertex.isValid()) {
        s.vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
        s.cover = bakeInline(cloudSource(CoverFragment, true).constData(), QShader::FragmentStage, rhi);
        s.march = bakeInline(marchSource().constData(), QShader::FragmentStage, rhi);
        s.marchDeep = bakeInline(deepVariant(marchSource()).constData(), QShader::FragmentStage, rhi);
        s.shadow = bakeInline(shadowSource().constData(), QShader::FragmentStage, rhi);
        s.skyLight = bakeInline(skyLightSource().constData(), QShader::FragmentStage, rhi);
        s.skyMean = bakeInline(SkyMeanFragment, QShader::FragmentStage, rhi);
        s.temporal = bakeInline(cloudSource(TemporalFragment, false).constData(), QShader::FragmentStage, rhi);
        s.composite = bakeInline(compositeSource().constData(), QShader::FragmentStage, rhi);
        s.glowComposite = bakeInline(glowVariant(compositeSource()).constData(), QShader::FragmentStage, rhi);
    }
    return s;
}

// The march's size for a view: the image is the given pixels in the view's
// shape (never more than the view), whole blocks; the march one texel per
// block.
QSize cloudMarchSize(const QSize &viewSize, int pixels) {
    const double viewPixels = double(std::max(1, viewSize.width())) * std::max(1, viewSize.height());
    const double scale = std::min(1.0, std::sqrt(double(pixels) / viewPixels));
    auto blocks = [scale](int extent) {
        return std::max(1, int(std::ceil(extent * scale / UpdateBlock)));
    };
    return QSize(blocks(viewSize.width()), blocks(viewSize.height()));
}

QRhiTexture *cloudTexture(QRhi *rhi, const QSize &size) {
    QRhiTexture *t = rhi->newTexture(QRhiTexture::RGBA16F, size, 1, QRhiTexture::RenderTarget);
    if (!t->create()) {
        delete t;
        return nullptr;
    }
    return t;
}

// A target of the textures; the first drawn into at the layer given (a
// cube's face).
bool cloudTarget(QRhi *rhi, std::initializer_list<QRhiTexture *> colors, bool preserve, QRhiTextureRenderTarget *&out,
                 QRhiRenderPassDescriptor *&pass, int layer = 0) {
    QRhiTextureRenderTargetDescription description;
    QVarLengthArray<QRhiColorAttachment, 4> attachments;
    for (QRhiTexture *t : colors) {
        attachments.append(QRhiColorAttachment(t));
        if (attachments.size() == 1)
            attachments.back().setLayer(layer);
    }
    description.setColorAttachments(attachments.cbegin(), attachments.cend());
    out = rhi->newTextureRenderTarget(description, preserve ? QRhiTextureRenderTarget::PreserveColorContents
                                                            : QRhiTextureRenderTarget::Flags());
    pass = out->newCompatibleRenderPassDescriptor();
    out->setRenderPassDescriptor(pass);
    return out->create();
}

QRhiShaderResourceBindings *cloudBindings(QRhi *rhi, std::initializer_list<QRhiShaderResourceBinding> list) {
    QRhiShaderResourceBindings *set = rhi->newShaderResourceBindings();
    set->setBindings(list.begin(), list.end());
    set->create();
    return set;
}

QRhiGraphicsPipeline *cloudPipeline(QRhi *rhi, const QShader &shader, QRhiShaderResourceBindings *set,
                                    QRhiRenderPassDescriptor *pass, int outputs, bool blend) {
    QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
    ps->setShaderStages({{QRhiShaderStage::Vertex, cloudShaderSet(rhi).vertex}, {QRhiShaderStage::Fragment, shader}});
    QVarLengthArray<QRhiGraphicsPipeline::TargetBlend, 4> blends;
    for (int i = 0; i < outputs; ++i) {
        QRhiGraphicsPipeline::TargetBlend b;
        if (blend) {
            // colour + view * T; the view's alpha stays.
            b.enable = true;
            b.srcColor = QRhiGraphicsPipeline::One;
            b.dstColor = QRhiGraphicsPipeline::SrcAlpha;
            b.srcAlpha = QRhiGraphicsPipeline::Zero;
            b.dstAlpha = QRhiGraphicsPipeline::One;
        }
        blends.append(b);
    }
    ps->setTargetBlends(blends.cbegin(), blends.cend());
    ps->setShaderResourceBindings(set);
    ps->setRenderPassDescriptor(pass);
    if (!ps->create()) {
        delete ps;
        return nullptr;
    }
    return ps;
}

}

void RhiRenderer::setClouds(const Clouds &next) {
    clouds = next;
    placeCloudShadow();
}

bool RhiRenderer::cloudsWanted() const {
    return sky.physical && clouds.quality > 0 && clouds.size > 0 && clouds.layers
            && clouds.layers->size() == size_t(clouds.size) * clouds.size * 12
            && rhi->isFeatureSupported(QRhi::ThreeDimensionalTextures)
            && rhi->isTextureFormatSupported(QRhiTexture::RGBA16F);
}

bool RhiRenderer::createCloudVolumes() {
    CloudResources &c = cloudResources;
    if (c.shape != nullptr)
        return true;
    c.shape = rhi->newTexture(QRhiTexture::RGBA8, ShapeSize, ShapeSize, ShapeSize, 1, QRhiTexture::ThreeDimensional);
    c.curl = rhi->newTexture(QRhiTexture::RGBA8, QSize(CurlSize, CurlSize));
    c.curl->create();
    c.blue = rhi->newTexture(QRhiTexture::R16, QSize(BlueSize, BlueSize));
    c.blue->create();
    c.detail = rhi->newTexture(QRhiTexture::RGBA8, DetailSize, DetailSize, DetailSize, 1, QRhiTexture::ThreeDimensional);
    if (!c.shape->create() || !c.detail->create()) {
        delete c.shape;
        delete c.detail;
        c.shape = c.detail = nullptr;
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    const std::vector<quint8> shape = CloudNoise::shape(ShapeSize);
    const std::vector<quint8> detail = CloudNoise::detail(DetailSize);
    qInfo().noquote() << QString("Cloud noise volumes made in %1 ms").arg(timer.elapsed());
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    auto upload = [&](QRhiTexture *texture, const std::vector<quint8> &data, int size) {
        QVarLengthArray<QRhiTextureUploadEntry, 128> entries;
        const int slice = size * size * 4;
        for (int z = 0; z < size; ++z) {
            QRhiTextureSubresourceUploadDescription description(data.data() + size_t(z) * slice, quint32(slice));
            description.setSourceSize(QSize(size, size));
            entries.append(QRhiTextureUploadEntry(z, 0, description));
        }
        QRhiTextureUploadDescription all;
        all.setEntries(entries.cbegin(), entries.cend());
        batch->uploadTexture(texture, all);
    };
    upload(c.shape, shape, ShapeSize);
    upload(c.detail, detail, DetailSize);
    const std::vector<quint8> curl = CloudNoise::curl(CurlSize);
    QRhiTextureSubresourceUploadDescription curlData(curl.data(), quint32(curl.size()));
    curlData.setSourceSize(QSize(CurlSize, CurlSize));
    batch->uploadTexture(c.curl, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, curlData)));
    const std::vector<quint16> blue = CloudNoise::blue(BlueSize);
    QRhiTextureSubresourceUploadDescription blueData(blue.data(), quint32(blue.size() * sizeof(quint16)));
    blueData.setSourceSize(QSize(BlueSize, BlueSize));
    batch->uploadTexture(c.blue, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, blueData)));
    c.pendingUploads = batch;
    return true;
}

bool RhiRenderer::createCloudResources(const QSize &viewSize, int quality) {
    CloudResources &c = cloudResources;
    const int pixels = Presets[quality].pixels;
    if (c.march != nullptr && c.size == cloudMarchSize(viewSize, pixels) && c.compositeColor == view.color
            && c.shadowPipeline != nullptr && c.skyLightPipeline != nullptr)
        return true;
    if (c.shadow == nullptr && !createSharedCloudMaps())
        return false;
    // The main view's own (march, image, composite): again for another size
    // or view, the shared maps kept (rebuilt with them, a resized window lost
    // its cloud shadows for a frame; user, 2026-10-14).
    if (!createCloudView(c, viewSize, pixels, view.color, view.color3, view.depth)) {
        releaseCloudBuffers();
        return false;
    }
    // The shadow map and the sky's light take the main view's march
    // parameters (its resource set's layout).
    dropLater(c.shadowPipeline);
    dropLater(c.skyLightPipeline);
    const CloudShaderSet &shaders = cloudShaderSet(rhi);
    c.shadowPipeline = cloudPipeline(rhi, shaders.shadow, c.marchBindings, c.shadowPass, 1, false);
    c.skyLightPipeline = cloudPipeline(rhi, shaders.skyLight, c.marchBindings, c.skyLightPass, 1, false);
    if (c.shadowPipeline == nullptr || c.skyLightPipeline == nullptr) {
        releaseCloudBuffers();
        return false;
    }
    return true;
}

bool RhiRenderer::createSharedCloudMaps() {
    CloudResources &c = cloudResources;
    if (!createCloudVolumes())
        return false;
    // Shared by the views: the samplers, the weather, the cover maps, the
    // shadow map and the sky's light.
    c.linear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                               QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    c.nearest = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    c.repeat = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                               QRhiSampler::Repeat, QRhiSampler::Repeat, QRhiSampler::Repeat);
    for (QRhiSampler *s : {c.linear, c.nearest, c.repeat})
        s->create();
    c.coverUniforms[0] = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(CoverParameters));
    c.coverUniforms[1] = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(CoverParameters));
    c.coverUniforms[0]->create();
    c.coverUniforms[1]->create();
    if (c.weather == nullptr) {
        c.weather = rhi->newTexture(QRhiTexture::RGBA16F, QSize(4 * clouds.size, clouds.size));
        c.weather->create();
        c.weatherRevision = ~0ull;
    }
    c.coverNear = cloudTexture(rhi, QSize(CoverSize, CoverSize));
    c.coverFar = cloudTexture(rhi, QSize(CoverSize, CoverSize));
    c.shadow = cloudTexture(rhi, QSize(CloudShadowSize, CloudShadowSize));
    c.skyLight = cloudTexture(rhi, QSize(64, 64));
    c.skyMean = rhi->newTexture(QRhiTexture::RGBA16F, QSize(1, 1), 1,
                                QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    if (!c.skyMean->create()) {
        delete c.skyMean;
        c.skyMean = nullptr;
    }
    const bool ok = c.coverNear != nullptr && c.coverFar != nullptr && c.shadow != nullptr
            && c.skyLight != nullptr && c.skyMean != nullptr
            && cloudTarget(rhi, {c.coverNear}, false, c.coverTargets[0], c.coverPasses[0])
            && cloudTarget(rhi, {c.coverFar}, false, c.coverTargets[1], c.coverPasses[1])
            && cloudTarget(rhi, {c.shadow}, false, c.shadowTarget, c.shadowPass)
            && cloudTarget(rhi, {c.skyLight}, false, c.skyLightTarget, c.skyLightPass)
            && cloudTarget(rhi, {c.skyMean}, false, c.skyMeanTarget, c.skyMeanPass);
    if (!ok) {
        releaseCloudBuffers();
        return false;
    }
    const auto fragment = QRhiShaderResourceBinding::FragmentStage;
    for (int i = 0; i < 2; ++i)
        c.coverBindings[i] = cloudBindings(rhi, {QRhiShaderResourceBinding::uniformBuffer(0, fragment, c.coverUniforms[i]),
                                                 QRhiShaderResourceBinding::sampledTexture(1, fragment, c.weather, c.linear)});
    const CloudShaderSet &shaders = cloudShaderSet(rhi);
    c.coverPipeline = cloudPipeline(rhi, shaders.cover, c.coverBindings[0], c.coverPasses[0], 1, false);
    c.skyMeanBindings = cloudBindings(rhi, {QRhiShaderResourceBinding::sampledTexture(0, fragment, c.skyLight, c.nearest)});
    c.skyMeanPipeline = cloudPipeline(rhi, shaders.skyMean, c.skyMeanBindings, c.skyMeanPass, 1, false);
    c.skyReadback.completed = [this] {
        CloudResources &r = cloudResources;
        r.skyPending = false;
        if (r.skyReadback.data.size() < int(2 * sizeof(qfloat16)))
            return;
        qfloat16 values[2];
        std::memcpy(values, r.skyReadback.data.constData(), sizeof(values));
        if (std::isfinite(float(values[0])) && std::isfinite(float(values[1]))) {
            r.skyCover = std::clamp(float(values[0]), 0.0f, 1.0f);
            r.skyThrough = std::clamp(float(values[1]), 0.0f, 1.0f);
            r.skyValid = true;
        }
    };
    if (c.coverPipeline == nullptr || c.skyMeanPipeline == nullptr) {
        releaseCloudBuffers();
        return false;
    }
    c.coverKey.clear();
    return true;
}

bool RhiRenderer::createCloudView(CloudView &v, const QSize &viewSize, int pixels, QRhiTexture *color,
                                  QRhiTexture *glow, QRhiTexture *depth, int colorLayer, const CloudView *share) {
    const CloudResources &c = cloudResources;
    const QSize marchSize = cloudMarchSize(viewSize, pixels);
    if (v.march != nullptr && v.size == marchSize && v.compositeColor == color)
        return true;
    releaseCloudView(v);
    v.size = marchSize;
    v.imageSize = marchSize * UpdateBlock;
    v.march = cloudTexture(rhi, marchSize);
    for (int i = 0; i < 2; ++i)
        v.history[i] = cloudTexture(rhi, v.imageSize);
    v.image = cloudTexture(rhi, v.imageSize);
    v.marchDepth = cloudTexture(rhi, marchSize);
    for (int i = 0; i < 2; ++i)
        v.historyDepth[i] = cloudTexture(rhi, v.imageSize);
    v.imageDepth = cloudTexture(rhi, v.imageSize);
    v.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(MarchParameters));
    v.uniforms->create();
    // One block per update pass, picked by a dynamic offset.
    v.updateStride = quint32(rhi->ubufAligned(int(sizeof(UpdateParameters))));
    v.updateUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, v.updateStride * UpdatePasses);
    v.updateUniforms->create();
    const bool ok = v.march != nullptr && v.history[0] != nullptr && v.history[1] != nullptr && v.image != nullptr
            && v.marchDepth != nullptr && v.historyDepth[0] != nullptr && v.historyDepth[1] != nullptr
            && v.imageDepth != nullptr
            && cloudTarget(rhi, {v.march, v.marchDepth}, false, v.marchTarget, v.marchPass)
            && cloudTarget(rhi, {v.history[0], v.image, v.historyDepth[0], v.imageDepth}, false, v.historyTargets[0],
                           v.historyPasses[0])
            && cloudTarget(rhi, {v.history[1], v.image, v.historyDepth[1], v.imageDepth}, false, v.historyTargets[1],
                           v.historyPasses[1])
            && (glow != nullptr ? cloudTarget(rhi, {color, glow}, true, v.compositeTarget, v.compositePass, colorLayer)
                                : cloudTarget(rhi, {color}, true, v.compositeTarget, v.compositePass, colorLayer));
    if (!ok) {
        releaseCloudView(v);
        return false;
    }
    v.compositeColor = color;
    const auto fragment = QRhiShaderResourceBinding::FragmentStage;
    v.marchBindings = cloudBindings(rhi, {QRhiShaderResourceBinding::uniformBuffer(0, fragment, v.uniforms),
                                          QRhiShaderResourceBinding::sampledTexture(1, fragment, c.weather, c.linear),
                                          QRhiShaderResourceBinding::sampledTexture(2, fragment, c.coverNear, c.linear),
                                          QRhiShaderResourceBinding::sampledTexture(3, fragment, c.coverFar, c.linear),
                                          QRhiShaderResourceBinding::sampledTexture(4, fragment, c.shape, c.repeat),
                                          QRhiShaderResourceBinding::sampledTexture(5, fragment, c.detail, c.repeat),
                                          QRhiShaderResourceBinding::sampledTexture(6, fragment, skyResources.transmittance,
                                                                                    skyResources.linear),
                                          QRhiShaderResourceBinding::sampledTexture(7, fragment, skyResources.view,
                                                                                    skyResources.linear),
                                          QRhiShaderResourceBinding::sampledTexture(8, fragment, depth, c.nearest),
                                          QRhiShaderResourceBinding::sampledTexture(9, fragment, c.curl, c.repeat),
                                          QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                                                  10, fragment, v.updateUniforms, sizeof(UpdateParameters)),
                                          QRhiShaderResourceBinding::sampledTexture(11, fragment, c.blue, c.nearest)});
    for (int i = 0; i < 2; ++i) {
        // Writes history i from the march and history 1 - i.
        v.temporalBindings[i] = cloudBindings(rhi, {QRhiShaderResourceBinding::uniformBuffer(0, fragment, v.uniforms),
                                                    QRhiShaderResourceBinding::sampledTexture(1, fragment, v.march, c.linear),
                                                    QRhiShaderResourceBinding::sampledTexture(2, fragment, v.history[1 - i],
                                                                                              c.linear),
                                                    QRhiShaderResourceBinding::sampledTexture(3, fragment, v.marchDepth, c.linear),
                                                    QRhiShaderResourceBinding::sampledTexture(4, fragment, v.historyDepth[1 - i],
                                                                                              c.linear),
                                                    QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                                                            10, fragment, v.updateUniforms, sizeof(UpdateParameters))});
        v.compositeBindings[i] = cloudBindings(rhi, {QRhiShaderResourceBinding::uniformBuffer(0, fragment, v.uniforms),
                                                     QRhiShaderResourceBinding::sampledTexture(1, fragment, v.image, c.linear),
                                                     QRhiShaderResourceBinding::sampledTexture(2, fragment, depth, c.nearest),
                                                     QRhiShaderResourceBinding::sampledTexture(3, fragment, v.imageDepth, c.linear),
                                                     QRhiShaderResourceBinding::sampledTexture(4, fragment, skyResources.view,
                                                                                               skyResources.linear)});
    }
    if (share != nullptr && share->marchPipeline != nullptr) {
        // Targets of the same formats, resource sets of the same layout.
        v.marchPipeline = share->marchPipeline;
        v.marchDeepPipeline = share->marchDeepPipeline;
        v.temporalPipeline = share->temporalPipeline;
        v.compositePipeline = share->compositePipeline;
        v.ownsPipelines = false;
    } else {
        const CloudShaderSet &shaders = cloudShaderSet(rhi);
        v.marchPipeline = cloudPipeline(rhi, shaders.march, v.marchBindings, v.marchPass, 2, false);
        v.marchDeepPipeline = cloudPipeline(rhi, shaders.marchDeep, v.marchBindings, v.marchPass, 2, false);
        v.temporalPipeline = cloudPipeline(rhi, shaders.temporal, v.temporalBindings[0], v.historyPasses[0], 4, false);
        v.compositePipeline = glow != nullptr
                ? cloudPipeline(rhi, shaders.glowComposite, v.compositeBindings[0], v.compositePass, 2, true)
                : cloudPipeline(rhi, shaders.composite, v.compositeBindings[0], v.compositePass, 1, true);
    }
    if (v.marchPipeline == nullptr || v.marchDeepPipeline == nullptr || v.temporalPipeline == nullptr
            || v.compositePipeline == nullptr) {
        releaseCloudView(v);
        return false;
    }
    v.historyValid = false;
    v.previousValid = false;
    return true;
}

bool RhiRenderer::cloudShadowPlane(float &altitude) const {
    // Just under the clouds: the map does not change with the camera's
    // height (any plane under them gives the same light).
    altitude = clouds.bottom - 0.05f;
    return cloudResources.shadowValid && cloudsWanted();
}

void RhiRenderer::cloudShadowOffset(int layer, double &east, double &north) const {
    // The map has moved on with its layer since it was drawn.
    east = cloudResources.shadowCentre[layer][0] + clouds.drift[layer][0] - clouds.cameraEast;
    north = cloudResources.shadowCentre[layer][1] + clouds.drift[layer][1] - clouds.cameraNorth;
}

void RhiRenderer::placeCloudShadow() {
    // Here, before the frame is drawn: the lit draws take the maps' place
    // as they are queued, the map is drawn later (prepareClouds, before the
    // view's geometry). Moved there, a redrawn map was read where the last
    // one lay for a frame: the shadows jumped by the drift since (user,
    // 2026-10-13: a flash at the shadows' edges every second or so).
    CloudResources &c = cloudResources;
    bool away = !c.shadowValid;
    for (int l = 0; l < 2; ++l) {
        double east, north;
        cloudShadowOffset(l, east, north);
        away = away || std::hypot(east, north) > CloudShadowRecentre;
    }
    if (!away)
        return;
    // On whole texels of the layer's own ground: drawn again, the texels
    // fall on the same points of the clouds as before.
    const double texel = CloudShadowExtent * 1000.0 / CloudShadowSize;
    for (int l = 0; l < 2; ++l) {
        c.shadowCentre[l][0] = std::floor((clouds.cameraEast - clouds.drift[l][0]) / texel + 0.5) * texel;
        c.shadowCentre[l][1] = std::floor((clouds.cameraNorth - clouds.drift[l][1]) / texel + 0.5) * texel;
    }
    c.shadowPlaced = true;
}

void RhiRenderer::cloudShadowCentre(int layer, float &x, float &z) const {
    double east, north;
    cloudShadowOffset(layer, east, north);
    // North is -z in the view's space.
    x = viewPosition[0] + float(east);
    z = viewPosition[2] - float(north);
}

bool RhiRenderer::cloudFarMaps(float *weatherMap, float *farMap, float *shift) const {
    const CloudResources &c = cloudResources;
    if (!cloudsWanted() || c.weather == nullptr || c.coverFar == nullptr || clouds.size <= 0)
        return false;
    weatherMap[0] = float(clouds.cameraEast - clouds.west);
    weatherMap[1] = float(clouds.north - clouds.cameraNorth);
    weatherMap[2] = float(double(clouds.size) * clouds.texel);
    weatherMap[3] = float(clouds.size);
    // As prepareClouds places it, snapped to its texels.
    const double span = double(CoverSize) * FarCoverTexel;
    const double west = std::floor((clouds.cameraEast - span * 0.5) / FarCoverTexel) * FarCoverTexel;
    const double north = std::ceil((clouds.cameraNorth + span * 0.5) / FarCoverTexel) * FarCoverTexel;
    farMap[0] = float(clouds.cameraEast - west);
    farMap[1] = float(north - clouds.cameraNorth);
    farMap[2] = float(span);
    farMap[3] = 0.0f;
    for (int l = 0; l < 2; ++l) {
        shift[2 * l] = (clouds.drift[l][0] - clouds.built[l][0]) / 1000.0f;
        shift[2 * l + 1] = (clouds.drift[l][1] - clouds.built[l][1]) / 1000.0f;
    }
    return true;
}

float RhiRenderer::deepSamples(int steps) const {
    return clouds.deepest >= 3.0f ? float(std::max(16, steps / 6)) : 0.0f;
}

float RhiRenderer::cloudShellRadius() const {
    // The radius at which the top of the clouds, seen from the camera's
    // height, meets the horizon HorizonDistance away: s^2 = 2 R (top - h).
    const float above = clouds.top - std::max(sky.cameraHeight / 1000.0f, 0.0f);
    if (above <= 0.1f)
        return EarthRadius;
    return std::clamp(HorizonDistance * HorizonDistance / (2.0f * above), SmallestShellRadius, EarthRadius);
}

void RhiRenderer::cloudLight(float *direction, float *colour) const {
    auto normalized = [](const float *v, float *out) {
        const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (int i = 0; i < 3; ++i)
            out[i] = length > 0.0f ? v[i] / length : (i == 1 ? 1.0f : 0.0f);
    };
    // High clouds see the sun until about 3 degrees under the horizon (the
    // afterglow); the moon lights them from 5 degrees under it, full at 9.
    normalized(sky.sun, direction);
    const float sunElevation = std::asin(std::clamp(direction[1], -1.0f, 1.0f)) * 180.0f / float(M_PI);
    if (sunElevation > -5.0f) {
        std::fill(colour, colour + 3, 1.0f);
        return;
    }
    normalized(sky.moon, direction);
    const float t = std::clamp((-5.0f - sunElevation) / 4.0f, 0.0f, 1.0f);
    const float fade = t * t * (3.0f - 2.0f * t);
    // Under a fifth of the scene's moonlight: in full, the moonlit clouds
    // made the night sky a grey overcast dusk; a quarter still lit them too
    // strongly (task 31).
    constexpr float CloudMoonlight = 0.18f;
    for (int c = 0; c < 3; ++c)
        colour[c] = sky.moonLight[c] * fade * CloudMoonlight;
}

void RhiRenderer::cloudMotion(CloudView &v, float *motion, float *motionHigh) {
    const bool known = v.historyValid && v.driftValid;
    for (int l = 0; l < 3; ++l) {
        float *out = l == 2 ? motionHigh : motion + 2 * l;
        out[0] = known ? (clouds.drift[l][0] - v.lastDrift[l][0]) / 1000.0f : 0.0f;
        out[1] = known ? (clouds.drift[l][1] - v.lastDrift[l][1]) / 1000.0f : 0.0f;
        v.lastDrift[l][0] = clouds.drift[l][0];
        v.lastDrift[l][1] = clouds.drift[l][1];
    }
    // Without a cloud in the pixel: the low layer's middle (the lowest
    // clouds' base and the layers' top halve it, about twice too high).
    motionHigh[2] = 0.5f * (clouds.bottom + clouds.top);
    // The composite's haze, as the march's (the update block's radius).
    motionHigh[3] = cloudShellRadius();
    v.driftValid = true;
}

void RhiRenderer::writeCloudShadowUniforms(RhiProgram *program) {
    float plane = 0.0f;
    const bool enabled = cloudShadowPlane(plane);
    // Along the clouds' light: the sun's or, at night, the moon's.
    float light[3], colour[3];
    cloudLight(light, colour);
    if (!enabled || light[1] < 0.03f) {
        program->setVec("cloudShadowArea", 0.0f, 0.0f, 1.0f, 0.0f);
        return;
    }
    float low[2], mid[2];
    cloudShadowCentre(0, low[0], low[1]);
    cloudShadowCentre(1, mid[0], mid[1]);
    program->setVec("cloudShadowArea", low[0], low[1], 1.0f / (CloudShadowExtent * 1000.0f), 1.0f);
    program->setVec("cloudShadowMid", mid[0], mid[1], 0.0f, 0.0f);
    // Along the light to the map's plane (y in the view's space).
    // Past the map: about the share of the sky the clouds leave open, and
    // under the clouds the light scattered through them (a cumulus about a
    // quarter or a third of the sun).
    program->setVec("cloudShadowSun", light[0] / light[1], light[2] / light[1],
                    viewPosition[1] + plane * 1000.0f - sky.cameraHeight,
                    1.0f - 0.7f * std::clamp(sky.overcast, 0.0f, 1.0f));
}

void RhiRenderer::prepareClouds(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *&updates) {
    if (cloudsPrepared || !cloudsWanted() || secondaryView || !view.valid())
        return;
    cloudsPrepared = true;
    const int quality = std::clamp(clouds.quality, 1, 4);
    CloudResources &c = cloudResources;
    if (c.weather != nullptr && c.weather->pixelSize() != QSize(4 * clouds.size, clouds.size)) {
        releaseCloudBuffers();
        dropLater(c.weather);
        c.weather = nullptr;
    }
    if (c.quality != quality) {
        releaseCloudBuffers();
        c.quality = quality;
    }
    if (!createSkyResources() || !createCloudResources(view.size, quality))
        return;
    const Preset &preset = Presets[quality];
    if (updates == nullptr)
        updates = rhi->nextResourceUpdateBatch();
    if (c.pendingUploads != nullptr) {
        updates->merge(c.pendingUploads);
        c.pendingUploads->release();
        c.pendingUploads = nullptr;
    }
    // The weather around the camera.
    if (c.weatherRevision != clouds.revision) {
        c.weatherRevision = clouds.revision;
        const std::vector<float> &layers = *clouds.layers;
        const std::vector<float> *extra = clouds.extra.get();
        // Side by side: low, mid, high; the extras (densities, rain).
        const int n = clouds.size;
        std::vector<qfloat16> half(size_t(4) * n * n * 4);
        for (int l = 0; l < 4; ++l)
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i)
                    for (int k = 0; k < 4; ++k) {
                        float value;
                        if (l < 3)
                            value = layers[((size_t(l) * n + j) * n + i) * 4 + k];
                        else if (extra != nullptr && extra->size() == size_t(n) * n * 4)
                            value = (*extra)[(size_t(j) * n + i) * 4 + k];
                        else
                            value = k < 2 ? 1.0f : 0.0f;
                        half[(size_t(j) * 4 * n + size_t(l) * n + i) * 4 + k] = qfloat16(value);
                    }
        QRhiTextureSubresourceUploadDescription data(half.data(), quint32(half.size() * sizeof(qfloat16)));
        data.setSourceSize(QSize(4 * n, n));
        updates->uploadTexture(c.weather, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, data)));
        c.coverKey.clear();
    }
    const double extent = double(clouds.size) * clouds.texel;
    // The cover maps, snapped to their texels, redrawn when they move or
    // the weather changes.
    const float texels[2] = {NearCoverTexel, FarCoverTexel};
    double mapWest[2], mapNorth[2];
    for (int m = 0; m < 2; ++m) {
        const double span = double(CoverSize) * texels[m];
        mapWest[m] = std::floor((clouds.cameraEast - span * 0.5) / texels[m]) * texels[m];
        mapNorth[m] = std::ceil((clouds.cameraNorth + span * 0.5) / texels[m]) * texels[m];
    }
    std::vector<double> key = {mapWest[0], mapNorth[0], mapWest[1], mapNorth[1], double(clouds.revision),
                               double(clouds.seed)};
    // At the drift the weather was sampled with: the march reads them
    // shifted as the clouds drift on.
    for (int l = 0; l < 3; ++l) {
        key.push_back(clouds.built[l][0]);
        key.push_back(clouds.built[l][1]);
    }
    const bool coverChanged = key != c.coverKey;
    if (coverChanged) {
        c.coverKey = key;
        const std::vector<double> &quantiles = WeatherNoise::quantiles();
        for (int m = 0; m < 2; ++m) {
            CoverParameters parameters = {};
            parameters.map[0] = float(mapWest[m] - clouds.west);
            parameters.map[1] = float(clouds.north - mapNorth[m]);
            parameters.map[2] = texels[m];
            parameters.map[3] = float(extent);
            for (int l = 0; l < 3; ++l) {
                parameters.origin[l][0] = float(mapWest[m] - clouds.built[l][0]);
                parameters.origin[l][1] = float(mapNorth[m] - clouds.built[l][1]);
                parameters.seeds[l] = clouds.seed * 3u + quint32(l) + 101u;
            }
            for (int q = 0; q < 257 && q < int(quantiles.size()); ++q)
                parameters.quantiles[q >> 2][q & 3] = float(quantiles[q]);
            updates->updateDynamicBuffer(c.coverUniforms[m], 0, sizeof(parameters), &parameters);
        }
    }
    // The frame's parameters, for the shadow map now and the march later.
    MarchParameters parameters = {};
    std::copy(skyInverse[TARGET_VIEW], skyInverse[TARGET_VIEW] + 16, parameters.inverse);
    std::copy(skyForward[TARGET_VIEW], skyForward[TARGET_VIEW] + 16, parameters.previous);
    if (c.previousValid)
        std::copy(c.previous, c.previous + 16, parameters.previous);
    const float length = std::sqrt(sky.sun[0] * sky.sun[0] + sky.sun[1] * sky.sun[1] + sky.sun[2] * sky.sun[2]);
    for (int i = 0; i < 3; ++i)
        parameters.sun[i] = length > 0.0f ? sky.sun[i] / length : (i == 1 ? 1.0f : 0.0f);
    parameters.camera[0] = std::max(0.001f, sky.cameraHeight / 1000.0f);
    parameters.camera[1] = rhi->isYUpInFramebuffer() ? 1.0f : -1.0f;
    parameters.camera[2] = Atmosphere::SkyExposure;
    // The march's start: one place when still, else a new one each frame.
    c.frame = (c.frame + 1) % 1024;
    parameters.camera[3] = clouds.still ? 0.5f : float(std::fmod(0.5 + c.frame * 0.6180339887, 1.0));
    parameters.weatherMap[0] = float(clouds.cameraEast - clouds.west);
    parameters.weatherMap[1] = float(clouds.north - clouds.cameraNorth);
    parameters.weatherMap[2] = float(extent);
    parameters.weatherMap[3] = float(clouds.size);
    for (int m = 0; m < 2; ++m) {
        float *out = m == 0 ? parameters.nearMap : parameters.farMap;
        out[0] = float(clouds.cameraEast - mapWest[m]);
        out[1] = float(mapNorth[m] - clouds.cameraNorth);
        out[2] = float(double(CoverSize) * texels[m]);
    }
    parameters.slab[0] = clouds.bottom;
    parameters.slab[1] = std::max(clouds.top, clouds.bottom + 0.05f);
    parameters.slab[2] = MarchDistance;
    parameters.slab[3] = float(preset.steps);
    parameters.projection[0] = sceneProjection[0];
    parameters.projection[1] = sceneProjection[1];
    parameters.projection[2] = sceneProjection[2];
    parameters.projection[3] = sceneProjection[3];
    parameters.bands[0] = 600.0f;
    parameters.bands[1] = distantFar;
    parameters.bands[2] = 0.98f;
    parameters.bands[3] = 0.99f;
    for (int l = 0; l < 2; ++l) {
        float *out = l == 0 ? parameters.noiseLow : parameters.noiseMid;
        out[0] = float(positiveModulo(clouds.cameraEast - clouds.drift[l][0], ShapePeriod));
        out[1] = float(positiveModulo(clouds.cameraNorth - clouds.drift[l][1], ShapePeriod));
        out[2] = float(positiveModulo(clouds.cameraEast - clouds.drift[l][0], DetailPeriod));
        out[3] = float(positiveModulo(clouds.cameraNorth - clouds.drift[l][1], DetailPeriod));
    }
    std::copy(sky.night, sky.night + 3, parameters.night);
    parameters.night[3] = sceneScaleDisplay();
    cloudLight(parameters.light, parameters.lightColour);
    for (int l = 0; l < 3; ++l) {
        float *out = l == 2 ? parameters.shiftHigh : parameters.shiftLowMid + 2 * l;
        out[0] = (clouds.drift[l][0] - clouds.built[l][0]) / 1000.0f;
        out[1] = (clouds.drift[l][1] - clouds.built[l][1]) / 1000.0f;
    }
    cloudMotion(c, parameters.motion, parameters.motionHigh);
    // A jump of the camera leaves the last frames' clouds behind.
    if (std::hypot(clouds.cameraEast - c.lastEast, clouds.cameraNorth - c.lastNorth) > 300.0)
        c.historyValid = false;
    c.lastEast = clouds.cameraEast;
    c.lastNorth = clouds.cameraNorth;
    parameters.lighting[0] = float(preset.lightSteps);
    parameters.lighting[1] = (clouds.still || !c.historyValid) ? 1.0f : 0.0f;
    parameters.lighting[2] = 1.0f / float(c.size.width());
    parameters.lighting[3] = 1.0f / float(c.size.height());
    // The high layer at the weather's base under the camera.
    {
        const int n = clouds.size;
        const size_t centre = ((size_t(2) * n + n / 2) * n + n / 2) * 4;
        parameters.high[0] = (*clouds.layers)[centre] > 0.0f ? (*clouds.layers)[centre + 2] : 0.0f;
    }
    parameters.high[1] = std::clamp(sky.haze, 0.0f, 0.02f) * 1000.0f;
    // Long steps in deep layers (3 km and more) split, 16 samples more at
    // most a ray, or a sixth of the steps (the march's loop), when a layer
    // is that deep somewhere; at every quality (the fewer the steps, the
    // plainer the hatching), in the main view. Into three at most at
    // Medium (its steps half as long again as High's), two otherwise (four
    // at High cost 2.5 ms more in a storm for nothing seen; Low keeps a
    // little, user).
    parameters.high[2] = deepSamples(preset.steps);
    parameters.high[3] = quality == 2 ? 3.0f : 2.0f;
    c.deep = parameters.high[2] > 0.0f || clouds.stormiest >= 0.8f || clouds.rainiest > 0.05f;
    float plane = 0.0f;
    cloudShadowPlane(plane);
    // The shadow map is drawn for the clouds' maps, its plane and the light's
    // slant, in steps that move the shadows of the clouds' top a few metres
    // (the lit shaders take the light's slant as it is now; the old key, the
    // direction in steps of 1e-4, redrew it every second or so).
    std::vector<double> shadowKey = c.coverKey;
    shadowKey.push_back(plane);
    {
        const float rise = std::max(parameters.light[1], 0.03f);
        const float step = CloudShadowLightStep / std::max((parameters.slab[1] - plane) * 1000.0f, 1.0f);
        shadowKey.push_back(std::round(parameters.light[0] / rise / step));
        shadowKey.push_back(std::round(parameters.light[2] / rise / step));
        shadowKey.push_back(parameters.light[1] < 0.03f ? 1.0 : 0.0);
    }
    // Between draws each layer's map drifts with it (a grid fixed to the
    // ground resampled the moving clouds at each draw, and the mid layer
    // was read moving with the low one); drawn again where it is, or where
    // placeCloudShadow moved it.
    const bool shadowRedraw = !c.shadowValid || c.shadowPlaced || shadowKey != c.shadowKey;
    c.shadowKey = shadowKey;
    c.shadowPlaced = false;
    double shadowEast, shadowNorth;
    cloudShadowOffset(0, shadowEast, shadowNorth);
    parameters.shadow[0] = CloudShadowExtent;
    parameters.shadow[1] = plane;
    parameters.shadow[2] = float(shadowEast / 1000.0);
    parameters.shadow[3] = float(shadowNorth / 1000.0);
    cloudShadowOffset(1, shadowEast, shadowNorth);
    parameters.shadowMid[0] = float(shadowEast / 1000.0);
    parameters.shadowMid[1] = float(shadowNorth / 1000.0);
    // Rain slants with the low wind (the low layer's drift so far), half a
    // km aside a km of fall.
    const float lowDrift = std::hypot(clouds.drift[0][0], clouds.drift[0][1]);
    parameters.sun[3] = lowDrift > 1.0f ? 0.5f * clouds.drift[0][0] / lowDrift : 0.0f;
    parameters.lightColour[3] = lowDrift > 1.0f ? 0.5f * clouds.drift[0][1] / lowDrift : 0.0f;
    // Downwind at the clouds' top (the high layer's drift so far): storms'
    // anvils stream that way.
    const float topDrift = std::hypot(clouds.drift[2][0], clouds.drift[2][1]);
    parameters.shadowMid[2] = topDrift > 1.0f ? clouds.drift[2][0] / topDrift : 0.0f;
    parameters.shadowMid[3] = topDrift > 1.0f ? clouds.drift[2][1] / topDrift : 0.0f;
    updates->updateDynamicBuffer(c.uniforms, 0, sizeof(parameters), &parameters);
    const float *values = reinterpret_cast<const float *>(&parameters);
    c.parameters.assign(values, values + sizeof(parameters) / sizeof(float));
    writeCloudUpdates(c, updates);
    auto fullScreen = [cb](QRhiTextureRenderTarget *target, QRhiGraphicsPipeline *pipeline,
                           QRhiShaderResourceBindings *bindings, const QSize &size, QRhiResourceUpdateBatch *&batch) {
        cb->beginPass(target, Qt::white, {1.0f, 0}, batch);
        batch = nullptr;
        cb->setGraphicsPipeline(pipeline);
        cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
        cb->setShaderResources(bindings);
        cb->draw(3);
        cb->endPass();
    };
    if (coverChanged) {
        fullScreen(c.coverTargets[0], c.coverPipeline, c.coverBindings[0], QSize(CoverSize, CoverSize), updates);
        fullScreen(c.coverTargets[1], c.coverPipeline, c.coverBindings[1], QSize(CoverSize, CoverSize), updates);
    }
    if (shadowRedraw) {
        cb->beginPass(c.shadowTarget, Qt::white, {1.0f, 0}, updates);
        updates = nullptr;
        cb->setGraphicsPipeline(c.shadowPipeline);
        cb->setViewport(QRhiViewport(0, 0, float(CloudShadowSize), float(CloudShadowSize)));
        const QRhiCommandBuffer::DynamicOffset offset(10, 0);
        cb->setShaderResources(c.marchBindings, 1, &offset);
        cb->draw(3);
        cb->endPass();
        c.skyWanted = true;
    }
    // The sky's light through the same clouds, measured again with the map
    // (one read-back at a time; it lands a frame or two later).
    if (c.skyWanted && !c.skyPending) {
        c.skyWanted = false;
        cb->beginPass(c.skyLightTarget, Qt::white, {1.0f, 0}, updates);
        updates = nullptr;
        cb->setGraphicsPipeline(c.skyLightPipeline);
        cb->setViewport(QRhiViewport(0, 0, 64.0f, 64.0f));
        const QRhiCommandBuffer::DynamicOffset offset(10, 0);
        cb->setShaderResources(c.marchBindings, 1, &offset);
        cb->draw(3);
        cb->endPass();
        cb->beginPass(c.skyMeanTarget, Qt::black, {1.0f, 0});
        cb->setGraphicsPipeline(c.skyMeanPipeline);
        cb->setViewport(QRhiViewport(0, 0, 1.0f, 1.0f));
        cb->setShaderResources(c.skyMeanBindings);
        cb->draw(3);
        cb->endPass();
        QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
        batch->readBackTexture(QRhiReadbackDescription(c.skyMean), &c.skyReadback);
        cb->resourceUpdate(batch);
        c.skyPending = true;
    }
    c.shadowValid = true;
}

void RhiRenderer::writeCloudUpdates(CloudView &v, QRhiResourceUpdateBatch *updates) {
    // The update passes' blocks: which pixel of each block they march.
    std::vector<char> blocks(size_t(v.updateStride) * UpdatePasses, 0);
    const float radius = cloudShellRadius();
    for (int i = 0; i < UpdatePasses; ++i) {
        UpdateParameters u = {};
        u.update[0] = float(UpdateBlock);
        u.update[1] = float(UpdateOrder[i][0]);
        u.update[2] = float(UpdateOrder[i][1]);
        // A new sample half of the pixel: the jittered march's noise
        // settles over a few updates. In a fast timelapse (more passes a
        // frame) the clouds change more than the noise: a new sample whole
        // (30 s a second, two passes: pixels more than 8 off a still image
        // from 1.0 % to 0.5 %).
        u.update[3] = &v == &cloudResources && clouds.passes > 1 ? 1.0f : 0.5f;
        u.sizes[0] = float(v.imageSize.width());
        u.sizes[1] = float(v.imageSize.height());
        u.sizes[2] = float(v.size.width());
        u.sizes[3] = float(v.size.height());
        u.shell[0] = radius;
        std::memcpy(blocks.data() + size_t(i) * v.updateStride, &u, sizeof(u));
    }
    updates->updateDynamicBuffer(v.updateUniforms, 0, quint32(blocks.size()), blocks.data());
}

void RhiRenderer::runCloudView(CloudView &v, QRhiCommandBuffer *cb, const QSize &viewSize,
                               QRhiResourceUpdateBatch *updates, int passes) {
    auto fullScreen = [cb, &v, &updates](QRhiTextureRenderTarget *target, QRhiGraphicsPipeline *pipeline,
                                         QRhiShaderResourceBindings *bindings, const QSize &size, int pass) {
        cb->beginPass(target, Qt::black, {1.0f, 0}, updates);
        updates = nullptr;
        cb->setGraphicsPipeline(pipeline);
        cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
        if (pass >= 0) {
            const QRhiCommandBuffer::DynamicOffset offset(10, quint32(pass) * v.updateStride);
            cb->setShaderResources(bindings, 1, &offset);
        } else {
            cb->setShaderResources(bindings);
        }
        cb->draw(3);
        cb->endPass();
    };
    // The given pixels of each block a run; all of them at once when the
    // image must be complete (still captures, no history after a jump).
    const bool fill = clouds.still || !v.historyValid;
    const int first = fill ? 0 : v.nextPass;
    if (fill)
        passes = UpdatePasses;
    int write = v.current;
    for (int p = 0; p < passes; ++p) {
        const int pass = (first + p) % UpdatePasses;
        fullScreen(v.marchTarget, v.deep ? v.marchDeepPipeline : v.marchPipeline, v.marchBindings, v.size, pass);
        write = v.current = 1 - v.current;
        fullScreen(v.historyTargets[write], v.temporalPipeline, v.temporalBindings[write], v.imageSize, pass);
    }
    v.nextPass = (first + passes) % UpdatePasses;
    fullScreen(v.compositeTarget, v.compositePipeline, v.compositeBindings[write], viewSize, -1);
    v.historyValid = true;
}

void RhiRenderer::applySecondaryClouds() {
    const CloudResources &c = cloudResources;
    if (currentTarget == TargetReflection) {
        if (reflectionCloudsApplied || !reflection.valid())
            return;
        reflectionCloudsApplied = true;
        // A quarter of the main view's pixels (the reflection has a quarter
        // of the window's); Low's steps: the waves blur it.
        const Preset &preset = Presets[std::clamp(c.quality, 1, 4)];
        applyViewClouds(reflectionClouds, TargetReflection, reflection, 0, preset.pixels / 4,
                        std::min(Presets[1].steps, preset.steps), std::min(Presets[1].lightSteps, preset.lightSteps),
                        1, nullptr);
    } else if (currentTarget == TargetEnvironment) {
        const int face = environmentFace;
        if (environmentCloudsApplied || face < 0 || face >= 6 || environment.cube == nullptr)
            return;
        environmentCloudsApplied = true;
        // The faces share face 0's pipelines: made first, the others after.
        CloudView &first = environmentClouds[0];
        const Attachments &target = environment.faces[face];
        if (first.march == nullptr) {
            for (int i = 1; i < 6; ++i)
                releaseCloudView(environmentClouds[i]);
            if (cloudsWanted() && c.march != nullptr && skyResources.view != nullptr
                    && !createCloudView(first, target.size, EnvironmentPreset.pixels, environment.cube, nullptr,
                                        environment.depth, 0, nullptr))
                return;
        }
        applyViewClouds(environmentClouds[face], TargetEnvironment, target, face, EnvironmentPreset.pixels,
                        EnvironmentPreset.steps, EnvironmentPreset.lightSteps, EnvironmentPasses,
                        face == 0 ? nullptr : &first);
    }
}

void RhiRenderer::applyViewClouds(CloudView &v, int target, const Attachments &attachments, int colorLayer,
                                  int pixels, int steps, int lightSteps, int passes, const CloudView *share) {
    if (!cloudsWanted() || !skyInverseValid[target])
        return;
    RhiRenderSurface *surfaceNow = surface();
    if (surfaceNow == nullptr || surfaceNow->frame().commandBuffer == nullptr)
        return;
    // The view's opaque passes and sky so far (the first view of a frame
    // draws the sky-view table with its sky).
    flushTarget();
    // These views are drawn before this frame's maps and parameters are
    // prepared: they take the last frame's (the camera moved a step since).
    const CloudResources &c = cloudResources;
    if (c.march == nullptr || c.parameters.size() * sizeof(float) != sizeof(MarchParameters)
            || !skyResources.viewRendered)
        return;
    if (!createCloudView(v, attachments.size, pixels, attachments.color, nullptr, attachments.depth, colorLayer,
                         share))
        return;
    MarchParameters parameters;
    std::memcpy(&parameters, c.parameters.data(), sizeof(parameters));
    // The view's directions (the reflection's: the sky's above the water);
    // the march starts at the camera, the clouds being far.
    std::copy(skyInverse[target], skyInverse[target] + 16, parameters.inverse);
    const float *previous = v.previousValid ? v.previous : skyForward[target];
    std::copy(previous, previous + 16, parameters.previous);
    // Rows as the sky's pass of the view takes them.
    parameters.camera[1] = (attachments.openGlRows || rhi->isYUpInFramebuffer()) ? 1.0f : -1.0f;
    parameters.projection[0] = bandScale[target][0];
    parameters.projection[1] = bandScale[target][1];
    parameters.slab[3] = float(steps);
    parameters.lighting[0] = float(lightSteps);
    // Long steps in deep layers not split: the reflection is blurred by the
    // waves, the cube read blurred (user). Storm clouds keep their shapes.
    parameters.high[2] = 0.0f;
    v.deep = clouds.stormiest >= 0.8f || clouds.rainiest > 0.05f;
    if (std::hypot(clouds.cameraEast - v.lastEast, clouds.cameraNorth - v.lastNorth) > 300.0)
        v.historyValid = false;
    v.lastEast = clouds.cameraEast;
    v.lastNorth = clouds.cameraNorth;
    parameters.lighting[1] = (clouds.still || !v.historyValid) ? 1.0f : 0.0f;
    // This view's own history: the clouds' move since its last frame.
    cloudMotion(v, parameters.motion, parameters.motionHigh);
    parameters.lighting[2] = 1.0f / float(v.size.width());
    parameters.lighting[3] = 1.0f / float(v.size.height());
    QRhiResourceUpdateBatch *updates = rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(v.uniforms, 0, sizeof(parameters), &parameters);
    writeCloudUpdates(v, updates);
    runCloudView(v, surfaceNow->frame().commandBuffer, attachments.size, updates, passes);
    std::copy(skyForward[target], skyForward[target] + 16, v.previous);
    v.previousValid = true;
}

void RhiRenderer::applyClouds() {
    // The water reflection's and the cube faces' own clouds, after their
    // opaque passes.
    if (secondaryView) {
        applySecondaryClouds();
        return;
    }
    if (cloudsApplied || !cloudsWanted() || currentTarget != TARGET_VIEW
            || baseProgram != PROGRAM_MAIN || !sceneProjectionValid || !skyInverseValid[TARGET_VIEW])
        return;
    RhiRenderSurface *surfaceNow = surface();
    if (surfaceNow == nullptr || surfaceNow->frame().commandBuffer == nullptr || !view.valid())
        return;
    cloudsApplied = true;
    // The opaque passes and the sky so far (the view's first pass prepares
    // the clouds' maps).
    flushTarget();
    CloudResources &c = cloudResources;
    if (!cloudsPrepared || !skyResources.viewRendered || c.march == nullptr || c.compositeColor != view.color)
        return;
    runCloudView(c, surfaceNow->frame().commandBuffer, view.size, nullptr, std::clamp(clouds.passes, 1, UpdatePasses));
    std::copy(skyForward[TARGET_VIEW], skyForward[TARGET_VIEW] + 16, c.previous);
    c.previousValid = true;
}

void RhiRenderer::releaseEnvironmentClouds() {
    // Face 0 owns the pipelines the others use.
    for (int i = 5; i >= 0; --i)
        releaseCloudView(environmentClouds[i]);
}

void RhiRenderer::releaseCloudView(CloudView &v) {
    if (v.ownsPipelines) {
        dropLater(v.marchPipeline);
        dropLater(v.marchDeepPipeline);
        dropLater(v.temporalPipeline);
        dropLater(v.compositePipeline);
    }
    dropLater(v.marchBindings);
    for (int i = 0; i < 2; ++i) {
        dropLater(v.temporalBindings[i]);
        dropLater(v.compositeBindings[i]);
        dropLater(v.historyTargets[i]);
        dropLater(v.historyPasses[i]);
        dropLater(v.history[i]);
        dropLater(v.historyDepth[i]);
    }
    dropLater(v.marchTarget);
    dropLater(v.marchPass);
    dropLater(v.compositeTarget);
    dropLater(v.compositePass);
    dropLater(v.march);
    dropLater(v.image);
    dropLater(v.marchDepth);
    dropLater(v.imageDepth);
    dropLater(v.uniforms);
    dropLater(v.updateUniforms);
    v = CloudView();
}

void RhiRenderer::releaseMainCloudView() {
    CloudResources &c = cloudResources;
    releaseCloudView(c);
    // Made with the view's resource set as their layout.
    dropLater(c.shadowPipeline);
    dropLater(c.skyLightPipeline);
    c.shadowPipeline = c.skyLightPipeline = nullptr;
}

void RhiRenderer::releaseCloudBuffers() {
    CloudResources &c = cloudResources;
    // The reflection's and the cube's resource sets hold the shared maps.
    releaseCloudView(reflectionClouds);
    releaseEnvironmentClouds();
    releaseCloudView(c);
    dropLater(c.coverPipeline);
    dropLater(c.shadowPipeline);
    dropLater(c.skyLightPipeline);
    dropLater(c.skyMeanPipeline);
    dropLater(c.skyMeanBindings);
    dropLater(c.skyLightTarget);
    dropLater(c.skyLightPass);
    dropLater(c.skyMeanTarget);
    dropLater(c.skyMeanPass);
    dropLater(c.skyLight);
    dropLater(c.skyMean);
    c.skyLightPipeline = c.skyMeanPipeline = nullptr;
    c.skyMeanBindings = nullptr;
    c.skyLightTarget = c.skyMeanTarget = nullptr;
    c.skyLightPass = c.skyMeanPass = nullptr;
    c.skyLight = c.skyMean = nullptr;
    // (A read-back in flight still lands: the last clouds' value.)
    c.skyPending = c.skyValid = c.skyWanted = false;
    for (int i = 0; i < 2; ++i) {
        dropLater(c.coverBindings[i]);
        dropLater(c.coverTargets[i]);
        dropLater(c.coverPasses[i]);
        dropLater(c.coverUniforms[i]);
        c.coverBindings[i] = nullptr;
        c.coverTargets[i] = nullptr;
        c.coverPasses[i] = nullptr;
        c.coverUniforms[i] = nullptr;
    }
    dropLater(c.shadowTarget);
    dropLater(c.shadowPass);
    dropLater(c.shadow);
    dropLater(c.coverNear);
    dropLater(c.coverFar);
    dropLater(c.linear);
    dropLater(c.nearest);
    dropLater(c.repeat);
    c.coverPipeline = c.shadowPipeline = nullptr;
    c.shadowTarget = nullptr;
    c.shadowPass = nullptr;
    c.shadow = c.coverNear = c.coverFar = nullptr;
    c.linear = c.nearest = c.repeat = nullptr;
    c.shadowValid = false;
    c.shadowKey.clear();
    c.coverKey.clear();
    c.parameters.clear();
}

void RhiRenderer::releaseCloudResources() {
    releaseCloudBuffers();
    CloudResources &c = cloudResources;
    dropLater(c.weather);
    dropLater(c.shape);
    dropLater(c.detail);
    dropLater(c.curl);
    dropLater(c.blue);
    if (c.pendingUploads != nullptr)
        c.pendingUploads->release();
    c = CloudResources();
}
