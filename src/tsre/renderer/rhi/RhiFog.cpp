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
#include <tsre/environment/Atmosphere.h>
#include <tsre/ogl/GLUU.h>
#include <tsre/world/Daylight.h>

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
    float scale[4];            // the cells' light scale, this frame's over the last's, the sums' over the cells'
    float lightGrid[4];        // the lamps' grid (LocalLights.glsl): origin, horizontal cell (m)
    float lightLayers[4];      // its vertical cell (m), lamps (0: none), their strength in open air, 0
    float signalGrid[4];       // the signals' grid, as the lamps'
    float signalLayers[4];
    float patches[4];          // the floor map's corner in world metres (east, north, wrapped), patchiness, pooling
    float drift[4];            // how far the fog has drifted (east, north, wrapped); since last frame (east, north)
    float rain[4];             // mm/h at the camera, its snow share, the clouds' maps on, the low clouds' base (m above sea level)
    float rainWeather[4];      // the clouds' weather texture about the camera (RhiRenderer::cloudRainMaps)
    float rainCover[4];        // their near cover map about the camera
    float rainShift[4];        // the low layer's shift since drawn (km east, north), the rain's slant (km a km of fall)
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
    vec4 scale;
    vec4 lightGrid;
    vec4 lightLayers;
    vec4 signalGrid;
    vec4 signalLayers;
    vec4 patches;
    vec4 drift;
    vec4 rain;
    vec4 rainWeather;
    vec4 rainCover;
    vec4 rainShift;
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
// Vegetation's shadow maps, with the main maps' matrices (task 37).
layout(binding = 18) uniform sampler2DShadow vegetationShadowMap0;
layout(binding = 19) uniform sampler2DShadow vegetationShadowMap1;
layout(binding = 20) uniform sampler2DShadow vegetationShadowMap2;
layout(binding = 6) uniform sampler3D history;
layout(binding = 7, rgba16f) uniform writeonly image3D cells;
layout(binding = 8) uniform sampler2D terrainShadeMap;
layout(binding = 9) uniform sampler2D localLightData;
layout(binding = 10) uniform sampler2D localLightCells;
layout(binding = 11) uniform sampler2D localLightIndices;
layout(binding = 12) uniform sampler2D signalLightData;
layout(binding = 13) uniform sampler2D signalLightCells;
layout(binding = 14) uniform sampler2D signalLightIndices;
layout(binding = 15) uniform sampler2D rainWeatherTexture;
layout(binding = 16) uniform sampler2D rainNearCover;
// The lamps' and signals' light in each cell, this frame's (not blended).
layout(binding = 17, rgba16f) uniform writeonly image3D lampCells;

// Rain (or snow) at a point (extinction, 1/m): the weather's under the low
// clouds' base, as the clouds' march has it beyond the volume (RhiClouds.cpp,
// marchRain): from their weather and near cover map, under the cells' cores
// for showers, slanted by the low wind, everywhere for a deck's rain; else
// the rate at the camera. through: what the cloud above lets through (the
// sky's light in the rain: dark under a storm); snow: its share.
float rainDensity(vec3 p, out float through, out float snow) {
    through = 1.0;
    snow = rain.y;
    if (rain.x <= 0.0)
        return 0.0;
    float altitude = (p.y - range.y) / 1000.0;
    float rate = rain.x, base = rain.w / 1000.0, convective = 0.0;
    if (rain.z > 0.5) {
        vec2 offset = vec2(p.x - camera.x, camera.z - p.z) / 1000.0;
        vec2 o = offset - rainShift.xy;
        float size = rainWeather.w;
        vec2 w = clamp((rainWeather.xy + vec2(o.x, -o.y) * 1000.0) / rainWeather.z, vec2(0.5 / size),
                       vec2(1.0 - 0.5 / size));
        vec4 low = textureLod(rainWeatherTexture, vec2(w.x / 4.0, w.y), 0.0);
        vec4 extra = textureLod(rainWeatherTexture, vec2((3.0 + w.x) / 4.0, w.y), 0.0);
        if (low.x <= 0.0 || extra.z <= 0.0)
            return 0.0;
        rate = extra.z;
        snow = extra.w;
        base = low.z;
        if (altitude >= base)
            return 0.0;
        convective = clamp((low.y - 0.45) / 0.35, 0.0, 1.0);
        // Fallen from the cloud upwind; showers from the cells' cores.
        vec2 from = offset - rainShift.zw * (base - altitude) - rainShift.xy;
        float even = textureLod(rainNearCover, (rainCover.xy + vec2(from.x, -from.y) * 1000.0) / rainCover.z, 0.0).r;
        float cell = clamp(0.5 + (even - (1.0 - low.x) - 0.05) * 4.0, 0.0, 1.0);
        rate *= mix(1.0, min(cell / max(low.x, 0.1), 3.0), convective);
        float column = mix(35.0, 70.0, clamp((low.y - 0.3) / 0.4, 0.0, 1.0)) * extra.x * max(low.w - low.z, 0.05)
                * mix(0.35, 0.7, smoothstep(0.8, 1.0, low.y)) * max(cell, 1.0 - convective);
        through = 1.0 / (1.0 + 0.11 * column);
    } else if (altitude >= base) {
        return 0.0;
    }
    if (rate <= 0.0)
        return 0.0;
    return 0.35e-3 * pow(rate, 0.63) * mix(0.5, 1.0, convective) * mix(0.7, 1.0, altitude / max(base, 0.1));
}

// Extinction (1/m): the weather's fog from the floor up to its top, where the
// floor map pools it (FogFloor), softly edged; none past the map. above: the
// fog over the point (m) and its extinction there, for the light reaching it.
// Patches (step 5): fog thinning into holes and its top rising and falling,
// a sum of value noise at 600, 240 and 90 m in world metres (east, north,
// wrapped every 36 km, which each scale repeats over exactly), drifting
// together with the wind near the ground, so that the fog's history can
// follow them (each at its own pace, they smeared in it with the clock
// running fast, unlike newly seen fog). 0-1, about 0.5.
float fogHash(vec2 i) {
    vec3 p3 = fract(vec3(i.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float fogNoise(vec2 p, float period) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = fogHash(mod(i, period)), b = fogHash(mod(i + vec2(1.0, 0.0), period));
    float c = fogHash(mod(i + vec2(0.0, 1.0), period)), d = fogHash(mod(i + vec2(1.0, 1.0), period));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fogPatches(vec2 q, vec2 drift) {
    vec2 p = q - drift;
    return fogNoise(p / 600.0, 60.0) * 0.55 + fogNoise(p / 240.0 + 17.0, 150.0) * 0.3
            + fogNoise(p / 90.0 + 41.0, 400.0) * 0.15;
}

// n: the patches' noise there (fogPatches).
float density(vec3 p, float n, out float above, out float sigmaAbove) {
    above = 0.0;
    sigmaAbove = 0.0;
    if (map.w < 0.5)
        return 0.0;
    vec2 m = (p.xz - map.xy) / map.z;
    if (any(lessThan(m, vec2(0.0))) || any(greaterThan(m, vec2(1.0))))
        return 0.0;
    vec4 cell = textureLod(floorMap, m, 0.0);
    float height = p.y - range.y;
    float top = max(cell.x + fog.y * cell.y * patches.w, cell.z + fog.y * 0.5 * cell.w);
    // Its top swells up and down (a deep fog's by about 13 % of its depth,
    // a patchy one's by up to 30 %); a patchy fog (thin, burning off) thins
    // into holes where the noise is low.
    top += (n - 0.5) * fog.y * (0.15 + 0.45 * patches.z);
    float holes = mix(1.0, smoothstep(0.3, 0.6, n), patches.z);
    float inside = 1.0 - smoothstep(top - fog.w, top + fog.w, height);
    if (fog.z > 0.0)
        inside *= smoothstep(cell.x + fog.z - fog.w, cell.x + fog.z + fog.w, height);
    vec2 edge = min(m, 1.0 - m);
    inside *= smoothstep(0.0, 0.05, min(edge.x, edge.y));
    // None deep in the ground: tunnels and stations under it stay clear. The
    // map's ground is its cells' (64 m): a cutting narrower than that lies
    // below it, so the fog goes from 15 m under it, gone at 25 m.
    inside *= smoothstep(cell.z - 25.0, cell.z - 15.0, height);
    // Thicker over water; a hole goes through the fog above too.
    sigmaAbove = fog.x * (1.0 + 0.5 * cell.w) * holes;
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
        return terrain * textureLod(shadowMap0, vec3(m.xy, m.z - 0.001), 0.0)
                * textureLod(vegetationShadowMap0, vec3(m.xy, m.z - 0.001), 0.0);
    if (mapPoint(shadow1, p, m))
        return terrain * textureLod(shadowMap1, vec3(m.xy, m.z - 0.002), 0.0)
                * textureLod(vegetationShadowMap1, vec3(m.xy, m.z - 0.002), 0.0);
    if (mapPoint(shadow2, p, m))
        return terrain * textureLod(shadowMap2, vec3(m.xy, m.z - 0.002), 0.0)
                * textureLod(vegetationShadowMap2, vec3(m.xy, m.z - 0.002), 0.0);
    return terrain;
}

// The first light and the count of a grid's cell at a point (none outside
// the grid), at most 16 (the brightest).
ivec2 gridCell(sampler2D cells, vec4 grid, vec4 layers, vec3 p) {
    ivec3 c = ivec3(floor((p - grid.xyz) / vec3(grid.w, layers.x, grid.w)));
    if (any(lessThan(c, ivec3(0))) || any(greaterThanEqual(c, ivec3(64, 16, 64))))
        return ivec2(0);
    ivec2 cell = ivec2(texelFetch(cells, ivec2(c.x + c.z * 64, c.y), 0).xy);
    return ivec2(cell.x, min(cell.y, 16));
}

int gridIndex(sampler2D indices, int slot) {
    return int(texelFetch(indices, ivec2(slot % 4096, slot / 4096), 0).r);
}

// The lamps' light scattered towards the camera over a cell's slice of its
// ray (o + d t from t0 to t1), per metre on average: the lit shaders' lamps
// (LocalLights.glsl, task 21), those of the grid's cells at the slice's two
// ends, with the sun's phase: halos about lamps in fog, light pooled under
// them, the cones of spot lights. Each lamp's inverse square is summed along
// the slice exactly (taken at one point of it, a slice 30 m deep missed the
// few bright metres by a lamp: its halo a thousandth of what it is), over
// the part of the slice within its reach and its cone; its fade, cone and
// phase at points over that part, each standing for an even share of the
// sum (equiangular, as seen from the lamp). (Taken at the slice's point
// nearest to the lamp, a spot's cone was in or out for all of a slice tens
// of metres deep: a headlight's beam seen from afar lay in slabs, which
// moved with the camera; the user, 2026-10-17.) (The grid's cell at one
// point of the slice left out lamps the slice passes in the next one: halos
// cut straight along the cells' edges.)
// The signals' too, from a grid of their own (their glow splats' emitters):
// lenses, spots facing the way they shine, none behind them.
const int LightPoints = 4;

// The part of the ray o + d t, t0 to t1, inside a cone from p along a, the
// cosine of its half-angle c (90 degrees at most: the cone is convex, the
// part one span); false for none.
bool coneSpan(vec3 o, vec3 d, vec3 p, vec3 a, float c, inout float t0, inout float t1) {
    vec3 w = o - p;
    float wa = dot(w, a), da = dot(d, a);
    // In front of the apex: wa + da t >= 0.
    if (abs(da) < 1e-6) {
        if (wa < 0.0)
            return false;
    } else if (da > 0.0) {
        t0 = max(t0, -wa / da);
    } else {
        t1 = min(t1, -wa / da);
    }
    if (c > 1e-4) {
        // Within the half-angle: (wa + da t)^2 >= c^2 |w + d t|^2.
        float c2 = c * c;
        float qa = da * da - c2, qb = wa * da - c2 * dot(w, d), qc = wa * wa - c2 * dot(w, w);
        float disc = qb * qb - qa * qc;
        if (abs(qa) < 1e-6) {
            if (abs(qb) < 1e-9) {
                if (qc < 0.0)
                    return false;
            } else if (qb > 0.0) {
                t0 = max(t0, -qc / (2.0 * qb));
            } else {
                t1 = min(t1, -qc / (2.0 * qb));
            }
        } else if (disc < 0.0) {
            if (qa < 0.0)
                return false;
        } else {
            float root = sqrt(disc);
            float r1 = (-qb - root) / qa, r2 = (-qb + root) / qa;
            float lo = min(r1, r2), hi = max(r1, r2);
            if (qa < 0.0) {
                // Crossing the cone: between the roots.
                t0 = max(t0, lo);
                t1 = min(t1, hi);
            } else if (da > 0.0) {
                // Along the cone: past the far root in front of the apex.
                t0 = max(t0, hi);
            } else {
                t1 = min(t1, lo);
            }
        }
    }
    return t1 > t0;
}

vec3 gridLight(sampler2D data, sampler2D cells, sampler2D indices, vec4 grid, vec4 layers,
               vec3 o, vec3 d, float t0, float t1) {
    if (layers.y <= 0.0)
        return vec3(0.0);
    ivec2 first = gridCell(cells, grid, layers, o + d * t0);
    ivec2 second = gridCell(cells, grid, layers, o + d * t1);
    if (second.x == first.x)
        second.y = 0;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < first.y + second.y; ++i) {
        int light;
        if (i < first.y) {
            light = gridIndex(indices, first.x + i);
        } else {
            light = gridIndex(indices, second.x + i - first.y);
            bool counted = false;
            for (int k = 0; k < first.y && !counted; ++k)
                counted = gridIndex(indices, first.x + k) == light;
            if (counted)
                continue;
        }
        vec4 placement = texelFetch(data, ivec2(0, light), 0);
        vec3 toLamp = placement.xyz - o;
        float along = dot(toLamp, d);
        vec3 across = toLamp - d * along;
        float aside = dot(across, across);
        float reach = placement.w * placement.w;
        if (aside >= reach)
            continue;
        // The slice's part within the lamp's reach, and within its cone (one
        // wider than 90 degrees is not cut).
        float chord = sqrt(reach - aside);
        float s0 = max(t0, along - chord), s1 = min(t1, along + chord);
        vec4 spot = texelFetch(data, ivec2(2, light), 0);
        vec4 cone = vec4(0.0);
        if (spot.w > 0.5) {
            cone = texelFetch(data, ivec2(3, light), 0);
            if (cone.x > -1e-4 && !coneSpan(o, d, placement.xyz, spot.xyz, cone.x, s0, s1))
                continue;
        }
        if (s1 <= s0)
            continue;
        vec4 colour = texelFetch(data, ivec2(1, light), 0);
        // 1 / (s^2 + b^2) from s0 to s1, b the ray's distance from the lamp
        // softened by the emitter's size, over the slice's length; the angle
        // the span covers as seen from the lamp, split evenly.
        float b = sqrt(aside + colour.w);
        float a0 = atan((s0 - along) / b), a1 = atan((s1 - along) / b);
        float shares = 0.0;
        for (int k = 0; k < LightPoints; ++k) {
            float t = along + b * tan(mix(a0, a1, (float(k) + 0.5) / float(LightPoints)));
            vec3 toLight = placement.xyz - (o + d * t);
            float squared = dot(toLight, toLight);
            // Fading to nothing at the range.
            float ratio = squared / reach;
            float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
            float share = window * window;
            vec3 l = toLight * inversesqrt(max(squared, 1e-8));
            if (spot.w > 0.5) {
                float inside = clamp((dot(-l, spot.xyz) - cone.x) * cone.y, 0.0, 1.0);
                share *= inside * inside;
            }
            float phase = mix(henyeyGreenstein(dot(d, l), 0.85), 1.0 / (4.0 * 3.14159265), 0.3) * 4.0 * 3.14159265;
            shares += share * phase;
        }
        sum += colour.rgb * ((a1 - a0) / (b * (t1 - t0)) * shares / float(LightPoints));
    }
    return sum * (layers.z * 0.8);
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
    // The patches' noise at the slice's two ends, the points between them
    // blended: it changes over 90 m and more (at each of the four points it
    // cost about 1 ms on the Steam Deck); neighbouring slices meet at the
    // same value (taken once at the slice's middle it stepped from slice to
    // slice).
    vec2 ends = vec2(0.5);
    if (low < range.z && map.w > 0.5) {
        vec3 near = camera.xyz + d * sliceDistance(float(id.z));
        vec3 far = camera.xyz + d * sliceDistance(float(id.z) + 1.0);
        ends = vec2(fogPatches(vec2(near.x - map.x, map.y - near.z) + patches.xy, drift.xy),
                    fogPatches(vec2(far.x - map.x, map.y - far.z) + patches.xy, drift.xy));
    }
    for (int k = 0; k < Points && low < range.z; ++k) {
        float a, sa;
        float along = (float(k) + jitter.z) / float(Points);
        float s = density(camera.xyz + d * sliceDistance(float(id.z) + along), mix(ends.x, ends.y, along), a, sa);
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
    // Rain there, added to the fog (lit as it is, the sky's light through the
    // cloud above; snow lighter).
    float rainThrough, snow;
    float sigmaRain = rainDensity(p, rainThrough, snow);
    sigma += sigmaRain;
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
        // The rain's sky light: what the cloud above lets through, and from
        // the open sky about the cell at its sides, 60 % at least (lit only
        // from above, the rain under a storm came out black; at a third, a
        // dark lens under the base, darker than the base).
        // The sun's in it: under its own cloud, past the cloud shadow map too
        // (7 km; beyond it the clouds' average lit the rain under a storm
        // and its veil glowed over the far hills).
        float rainShare = sigmaRain / sigma;
        skyLight *= mix(1.0, (0.6 + 0.4 * rainThrough) * mix(1.0, 1.8, snow), rainShare);
        sunLight *= mix(1.0, rainThrough, rainShare);
        scattered = (skyLight + sunLight) * sigma;
    }
    // Scaled into the half floats' even range (RhiRenderer::prepareFog);
    // next to a lamp at night past it (white anyway).
    vec4 current = vec4(min(scattered * scale.x, vec3(60000.0)), sigma);
    // Last frame's cell at this cell's middle, where it was in view, the fog
    // having drifted since.
    if (previousCamera.w > 0.0) {
        vec3 r = camera.xyz + d * sliceDistance(float(id.z) + 0.5) - previousCamera.xyz - vec3(drift.z, 0.0, -drift.w);
        vec4 clip = previous * vec4(r, 0.0);
        if (clip.w > 0.0) {
            vec2 before = clip.xy / clip.w * 0.5 + 0.5;
            float slice = log(max(length(r), range.x) / range.x) / grid.w;
            vec3 coord = vec3(before, slice);
            if (all(greaterThanEqual(coord, vec3(0.0))) && all(lessThanEqual(coord, vec3(1.0)))) {
                vec4 before = textureLod(history, coord, 0.0);
                current = mix(current, vec4(before.rgb * scale.y, before.a), previousCamera.w);
            }
        }
    }
    imageStore(cells, id, current);
    // The lamps and signals as this frame has them, through the fog's
    // averaged density: summed along the slice exactly, they need no
    // averaging over frames, and in the history a turning or moving camera
    // left their light where it had been for many frames (user, 2026-10-17:
    // lit fog moving with the camera, parts of it dark).
    vec3 lamps = vec3(0.0);
    if (current.a > 0.0)
        lamps = (gridLight(localLightData, localLightCells, localLightIndices, lightGrid, lightLayers,
                           camera.xyz, d, sliceDistance(float(id.z)), sliceDistance(float(id.z) + 1.0))
                 + gridLight(signalLightData, signalLightCells, signalLightIndices, signalGrid, signalLayers,
                             camera.xyz, d, sliceDistance(float(id.z)), sliceDistance(float(id.z) + 1.0)))
                * current.a;
    imageStore(lampCells, id, vec4(min(lamps * scale.x, vec3(60000.0)), 0.0));
}
)";

// Each column front to back: the light added up to the end of each slice and
// the share of what lies behind let through.
const char *IntegrateCompute = R"(
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(binding = 1) uniform sampler3D cells;
layout(binding = 2, rgba16f) uniform writeonly image3D sums;
layout(binding = 3) uniform sampler3D lampCells;

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
        cell.rgb = (cell.rgb + texelFetch(lampCells, ivec3(id, i), 0).rgb) * scale.z;
        float sigma = max(cell.a, 0.0);
        float slice = exp(-sigma * dt);
        // The light scattered over the slice, as it dims along it.
        vec3 scattered = sigma > 1e-7 ? (cell.rgb - cell.rgb * slice) / sigma : cell.rgb * dt;
        light += through * scattered;
        through *= slice;
        imageStore(sums, ivec3(id, i), vec4(min(light, vec3(60000.0)), through));
        start = end;
    }
}
)";

// The sky and the clouds behind the fog: the far end of each column over
// them, as the lit shaders take their fog (Haze.glsl): what the fog lets
// through of their light and its own light added, in linear light, from a
// copy of the view (display colours). (Mixed in display colours by its
// share, a thin fog lit brightly, as by headlights at night, added a fifth
// of what it adds to the surfaces before the sky: far roofs grey in it, the
// sky over them dark, a sharp line between; the user, 2026-10-17.)
const char *SkyFragment = R"(#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Sky {
    vec4 params;   // the sky's depth, the sums' light to the scene's, 0, 0
};
layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2) uniform sampler3D sums;
layout(binding = 3) uniform sampler2D under;

void main() {
    if (textureLod(sceneDepth, uv, 0.0).r < params.x)
        discard;
    vec4 f = textureLod(sums, vec3(uv, 1.0), 0.0);
    f.rgb *= params.y;
    float through = clamp(f.a, 0.0, 1.0);
    vec4 view = texelFetch(under, ivec2(gl_FragCoord.xy), 0);
    vec3 linear = pow(max(view.rgb, vec3(0.0)), vec3(2.2)) * through + max(f.rgb, vec3(0.0));
    result = vec4(pow(linear, vec3(1.0 / 2.2)), view.a);
}
)";

QByteArray computeSource(const char *body) {
    return QByteArray(FogBlock) + body;
}

// The signals' light in fog over their glow splats' emitters'
// (RhiRenderer::prepareFog).
constexpr float SignalFogGain = 50.0f;

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

bool RhiRenderer::fogFogged() const {
    return fog.quality > 0 && fog.amount > 0.0f && fog.map != nullptr && fog.size > 0
            && fog.map->size() == size_t(fog.size) * fog.size * 4;
}

bool RhiRenderer::fogWanted() const {
    return (fogFogged() || fogRain()) && !fogResources.unsupported && skyFor(TARGET_VIEW);
}

bool RhiRenderer::fogRain() const {
    return fog.rain > 0.05f && !fogResources.unsupported && skyFor(TARGET_VIEW);
}

int RhiRenderer::fogQuality() const {
    return fog.quality > 0 ? fog.quality : 1;
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
    delete f.lamps;
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
    delete f.skyUnder;
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
    f.lamps = rhi->newTexture(QRhiTexture::RGBA16F, width, height, slices, 1, volume);
    f.floor = rhi->newTexture(QRhiTexture::RGBA16F, QSize(std::max(fog.size, 1), std::max(fog.size, 1)));
    f.uniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(FogParameters));
    f.linear = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                               QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    bool ok = f.integrated->create() && f.lamps->create() && f.floor->create() && f.uniforms->create() && f.linear->create();
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
                                             QRhiShaderResourceBinding::imageStore(2, compute, f.integrated, 0),
                                             QRhiShaderResourceBinding::sampledTexture(3, compute, f.lamps, f.linear)});
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
                                      QRhiShaderResourceBinding::sampledTexture(8, compute, dummy2D, f.linear),
                                      QRhiShaderResourceBinding::sampledTexture(9, compute, dummy2D, sampler(false, true, true)),
                                      QRhiShaderResourceBinding::sampledTexture(10, compute, dummy2D, sampler(false, true, true)),
                                      QRhiShaderResourceBinding::sampledTexture(11, compute, dummy2D, sampler(false, true, true)),
                                      QRhiShaderResourceBinding::sampledTexture(12, compute, dummy2D, sampler(false, true, true)),
                                      QRhiShaderResourceBinding::sampledTexture(13, compute, dummy2D, sampler(false, true, true)),
                                      QRhiShaderResourceBinding::sampledTexture(14, compute, dummy2D, sampler(false, true, true)),
                                      QRhiShaderResourceBinding::sampledTexture(15, compute, dummy2D, f.linear),
                                      QRhiShaderResourceBinding::sampledTexture(16, compute, dummy2D, f.linear),
                                      QRhiShaderResourceBinding::imageStore(17, compute, f.lamps, 0),
                                      QRhiShaderResourceBinding::sampledTexture(18, compute, dummyDepth, shadowSampler),
                                      QRhiShaderResourceBinding::sampledTexture(19, compute, dummyDepth, shadowSampler),
                                      QRhiShaderResourceBinding::sampledTexture(20, compute, dummyDepth, shadowSampler)});
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
            || !createFog(fogQuality(), view.size))
        return;
    // The floor map, when it changed (half floats: heights to a few metres).
    if (updates == nullptr)
        updates = rhi->nextResourceUpdateBatch();
    const bool floorMap = fogFogged();
    if (floorMap && (f.floorRevision != fog.revision || f.floor->pixelSize() != QSize(fog.size, fog.size))) {
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
    // terrain's shade, the lamps' grid.
    const bool shadows = Game::shadowsEnabled > 0;
    const bool lamps = Game::localLightsEnabled && lightData != nullptr && lightCells != nullptr
            && lightIndices != nullptr && !lightGrid.empty();
    // The signals' grid: prepared with the lamps (prepareLights).
    const bool signalLights = signalData != nullptr && signalCells != nullptr && signalIndices != nullptr
            && !signalGrid.empty();
    // The clouds' weather and near cover map, for the rain under them.
    float rainWeather[4] = {}, rainCover[4] = {}, rainShift[4] = {};
    const bool rainMaps = fogRain() && cloudRainMaps(rainWeather, rainCover, rainShift);
    const QRhiTexture *maps[16] = {
        cloudResources.shadow != nullptr && cloudResources.shadowValid ? cloudResources.shadow : dummy2D,
        shadows && shadowMaps[0].valid() ? shadowMaps[0].depth : dummyDepth,
        shadows && shadowMaps[1].valid() ? shadowMaps[1].depth : dummyDepth,
        shadows && shadowMaps[2].valid() ? shadowMaps[2].depth : dummyDepth,
        terrainShadeReady() ? shadeResources.texture : dummy2D,
        lamps ? lightData : dummy2D,
        lamps ? lightCells : dummy2D,
        lamps ? lightIndices : dummy2D,
        signalLights ? signalData : dummy2D,
        signalLights ? signalCells : dummy2D,
        signalLights ? signalIndices : dummy2D,
        rainMaps ? cloudResources.weather : dummy2D,
        rainMaps ? cloudResources.coverNear : dummy2D,
        shadows && vegetationMaps[0].valid() ? vegetationMaps[0].depth : dummyDepth,
        shadows && vegetationMaps[1].valid() ? vegetationMaps[1].depth : dummyDepth,
        shadows && vegetationMaps[2].valid() ? vegetationMaps[2].depth : dummyDepth};
    if (!std::equal(maps, maps + 16, f.boundMaps))
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
                 QRhiShaderResourceBinding::sampledTexture(8, compute, const_cast<QRhiTexture *>(maps[4]), f.linear),
                 QRhiShaderResourceBinding::sampledTexture(9, compute, const_cast<QRhiTexture *>(maps[5]),
                                                           sampler(false, true, true)),
                 QRhiShaderResourceBinding::sampledTexture(10, compute, const_cast<QRhiTexture *>(maps[6]),
                                                           sampler(false, true, true)),
                 QRhiShaderResourceBinding::sampledTexture(11, compute, const_cast<QRhiTexture *>(maps[7]),
                                                           sampler(false, true, true)),
                 QRhiShaderResourceBinding::sampledTexture(12, compute, const_cast<QRhiTexture *>(maps[8]),
                                                           sampler(false, true, true)),
                 QRhiShaderResourceBinding::sampledTexture(13, compute, const_cast<QRhiTexture *>(maps[9]),
                                                           sampler(false, true, true)),
                 QRhiShaderResourceBinding::sampledTexture(14, compute, const_cast<QRhiTexture *>(maps[10]),
                                                           sampler(false, true, true)),
                 QRhiShaderResourceBinding::sampledTexture(15, compute, const_cast<QRhiTexture *>(maps[11]), f.linear),
                 QRhiShaderResourceBinding::sampledTexture(16, compute, const_cast<QRhiTexture *>(maps[12]), f.linear),
                 QRhiShaderResourceBinding::imageStore(17, compute, f.lamps, 0),
                 QRhiShaderResourceBinding::sampledTexture(18, compute, const_cast<QRhiTexture *>(maps[13]), shadowSampler),
                 QRhiShaderResourceBinding::sampledTexture(19, compute, const_cast<QRhiTexture *>(maps[14]), shadowSampler),
                 QRhiShaderResourceBinding::sampledTexture(20, compute, const_cast<QRhiTexture *>(maps[15]), shadowSampler)});
        f.injectBindings[i]->create();
    }
    std::copy(maps, maps + 16, f.boundMaps);

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
    if (!floorMap) {
        f.highestTop = -1e9f;
        f.topRevision = ~0ull;
    } else if (f.topRevision != fog.revision || f.topDepth != fog.depth || f.topPooling != fog.pooling) {
        float highest = -1e9f;
        for (size_t i = 0; i + 3 < fog.map->size(); i += 4) {
            const float *c = &(*fog.map)[i];
            highest = std::max(highest, std::max(c[0] + fog.depth * c[1] * fog.pooling, c[2] + fog.depth * 0.5f * c[3]));
        }
        f.highestTop = highest + std::clamp(fog.depth * 0.15f, 3.0f, 20.0f);
        f.topRevision = fog.revision;
        f.topDepth = fog.depth;
        f.topPooling = fog.pooling;
    }
    // The patches' swell raises the top (as the injection's density).
    p.range[2] = f.highestTop + 0.5f * fog.depth * (0.15f + 0.45f * std::clamp(fog.patchiness, 0.0f, 1.0f));
    // Still (captures): the cells' middles, so frames repeat.
    if (clouds.still) {
        p.jitter[0] = p.jitter[1] = p.jitter[2] = 0.5f;
    } else {
        const unsigned index = (f.frame % 64u) + 1u;
        p.jitter[0] = halton(index, 2);
        p.jitter[1] = halton(index, 3);
        p.jitter[2] = halton(index, 5);
    }
    p.fog[0] = floorMap ? fog.amount * 3.912f / std::max(fog.visibility, 10.0f) : 0.0f;
    p.fog[1] = fog.depth;
    p.fog[2] = fog.base;
    p.fog[3] = std::clamp(fog.depth * 0.15f, 3.0f, 20.0f);
    // The map's north-west corner in the view's space (north is -z).
    p.map[0] = viewPosition[0] + float(fog.west - fog.cameraEast);
    p.map[1] = viewPosition[2] - float(fog.north - fog.cameraNorth);
    p.map[2] = float(fog.size) * fog.cell;
    p.map[3] = floorMap ? 1.0f : 0.0f;
    // The rain.
    if (fogRain()) {
        p.rain[0] = fog.rain;
        p.rain[1] = std::clamp(fog.snow, 0.0f, 1.0f);
        p.rain[2] = rainMaps ? 1.0f : 0.0f;
        p.rain[3] = fog.rainBase;
        std::copy(rainWeather, rainWeather + 4, p.rainWeather);
        std::copy(rainCover, rainCover + 4, p.rainCover);
        std::copy(rainShift, rainShift + 4, p.rainShift);
    }
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
    // A low sun: the day's sun through the air towards it (Atmosphere), where
    // that is brighter than the lit shaders' sun, which fades by the horizon
    // as a look (Daylight): at 10 degrees two to three times, less red. The
    // mist around a low sun glows, and the trees' beams in it show twice as
    // strong (the user, 2026-10-15). From about 20 degrees up the lit
    // shaders' is the brighter, and stays.
    const float sunLength = std::sqrt(sky.sun[0] * sky.sun[0] + sky.sun[1] * sky.sun[1] + sky.sun[2] * sky.sun[2]);
    if (sunLength > 0.0f && length > 0.0f
            && (sky.sun[0] * l[0] + sky.sun[1] * l[1] + sky.sun[2] * l[2]) / (sunLength * length) > 0.9999f) {
        const double height = std::max(0.001, double(sky.cameraHeight) / 1000.0);
        const Atmosphere::Rgb through = Atmosphere::model().sunLight(height, p.light[1]);
        const Atmosphere::Rgb overhead = Atmosphere::model().sunLight(height, 1.0);
        const float noon = std::pow(Daylight::SunLight * scene, 2.2f);
        for (int i = 0; i < 3; ++i)
            p.lightColour[i] = std::max(p.lightColour[i], noon * float(through[i] / std::max(overhead[i], 1e-6f)));
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
    // The light in the cells and their sums, scaled into the half floats'
    // even range: at night (the scene's light 0.03 of the day's) a cell's
    // light per metre is a few of their smallest steps, a frame's share of
    // it (1 - 0.95) none, and the cells kept what they had (the user,
    // 2026-10-15: at 3:00 with the dawn coming the fog's old part darker
    // than what a turn brought into view, by 10 %, and banded). The sums in
    // the day's scale (x 16), the cells also per the fog's extinction.
    // (with rain alone, the rain's at the camera)
    const float extinction = std::max({p.fog[0], fogRain() ? 0.35e-3f * std::pow(fog.rain, 0.63f) : 0.0f, 1e-4f});
    const float cellScale = 1.0f / (fogLightScale() * extinction);
    p.scale[0] = cellScale;
    p.scale[1] = f.cellScale > 0.0f ? cellScale / f.cellScale : 1.0f;
    p.scale[2] = extinction;
    f.cellScale = cellScale;
    // The lamps (task 21's grid), as strong as the lit shaders have them in
    // the open (localLightDay).
    std::copy(lightGrid.origin, lightGrid.origin + 3, p.lightGrid);
    p.lightGrid[3] = lightGrid.horizontalCell;
    p.lightLayers[0] = lightGrid.verticalCell;
    p.lightLayers[1] = lamps ? float(lightGrid.lightCount()) : 0.0f;
    p.lightLayers[2] = enclosure.enabled && enclosureDrawn && enclosureMap.valid() && !exposureActive()
            ? gluu->localLightAdaptation : 1.0f;
    std::copy(signalGrid.origin, signalGrid.origin + 3, p.signalGrid);
    p.signalGrid[3] = signalGrid.horizontalCell;
    p.signalLayers[0] = signalGrid.verticalCell;
    p.signalLayers[1] = signalLights ? float(signalGrid.lightCount()) : 0.0f;
    // A signal's lens gathers its light into a beam along the track: along
    // it many times what the splat's even emitter of the lens's brightness
    // gives (as that, its halo in fog did not show; at 50 times a soft halo
    // of its colour, at 100 the fog about the signals tinted).
    p.signalLayers[2] = SignalFogGain;
    fogPatchPlace(p.patches, p.drift);
    // The fog's drift since last frame (none after a jump in the clock).
    {
        const double east = fog.driftEast - f.previousDrift[0], north = fog.driftNorth - f.previousDrift[1];
        const bool follows = f.historyValid && std::hypot(east, north) < 200.0;
        p.drift[2] = follows ? float(east) : 0.0f;
        p.drift[3] = follows ? float(north) : 0.0f;
        f.previousDrift[0] = fog.driftEast;
        f.previousDrift[1] = fog.driftNorth;
    }
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

float RhiRenderer::fogLightScale() const {
    return (exposureActive() ? std::max(exposureControl.sceneLight, 1e-6f) : 1.0f) / 16.0f;
}

void RhiRenderer::prepareSignalGrid(QRhiResourceUpdateBatch *batch) {
    // The signals' emitters as their glow splats have them in the open
    // (signalScale), each reaching a lamp's usual reach: their halos lie
    // within it. (By its brightness a signal reached 2 m at night; by it and
    // the exposure, as the lamps, bright halos lowered the exposure, which
    // shortened the reach, and the halos went.)
    std::vector<LightGrid::Light> emitters;
    if (Game::localLightsEnabled) {
        gatherLights(emitters, 0.0f, 0.0f, true, signalScale());
        emitters.erase(std::remove_if(emitters.begin(), emitters.end(),
                                     [](const LightGrid::Light &light) { return !light.signal; }),
                      emitters.end());
        for (LightGrid::Light &light : emitters)
            light.range = LightGrid::DefaultReach;
    }
    // As the lamps' grid: kept while the signals and the camera's window
    // are the same.
    quint64 hash = 1469598103934665603ull;
    auto mix = [&hash](const void *data, size_t bytes) {
        const unsigned char *p = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < bytes; ++i)
            hash = (hash ^ p[i]) * 1099511628211ull;
    };
    for (const LightGrid::Light &light : emitters) {
        mix(light.position, sizeof(light.position));
        mix(light.color, sizeof(light.color));
        const float shape[2] = {light.range, light.radius};
        mix(shape, sizeof(shape));
    }
    const float window[3] = {std::floor(viewPosition[0] / LightGrid::MaxExtent),
                             std::floor(viewPosition[1] / LightGrid::MaxHeight),
                             std::floor(viewPosition[2] / LightGrid::MaxExtent)};
    mix(window, sizeof(window));
    if (hash == signalHash)
        return;
    signalHash = hash;
    signalGrid.build(emitters, viewPosition);
    if (!signalGrid.empty() && !uploadLightGrid(signalGrid, signalData, signalCells, signalIndices, batch))
        signalGrid = LightGrid();
}

void RhiRenderer::fogPatchPlace(float patches[4], float drift[4]) const {
    // World metres wrapped every 36 km, which the patches' noise repeats
    // over: the floats keep a centimetre there, as the map's corner moves
    // and the fog drifts on.
    constexpr double Period = 36000.0;
    auto wrap = [](double metres) { return float(metres - std::floor(metres / Period) * Period); };
    patches[0] = wrap(fog.west);
    patches[1] = wrap(fog.north);
    patches[2] = std::clamp(fog.patchiness, 0.0f, 1.0f);
    patches[3] = std::clamp(fog.pooling, 0.0f, 1.0f);
    drift[0] = wrap(fog.driftEast);
    drift[1] = wrap(fog.driftNorth);
    drift[2] = drift[3] = 0.0f;
}

void RhiRenderer::writeFogUniforms(RhiProgram *program) {
    const bool on = fogResources.filled && fogWanted() && !secondaryView && currentTarget == TARGET_VIEW;
    float forward[16];
    fogForward(forward);
    program->setMat4("fogForward", forward);
    program->setVec("fogParams", on ? 1.0f : 0.0f, FogNear, std::log(FogFar / FogNear),
                    float(std::max(fogResources.slices, 1)));
    program->setFloat("fogLightScale", fogLightScale());
    // High: surfaces seen from above the fog take the fog over their place
    // per pixel (Haze.glsl); Low the volume's alone.
    program->setVec("fogFilm", on && fog.quality >= 2 && fog.amount > 0.0f ? 1.0f : 0.0f,
                    std::clamp(fog.depth * 0.15f, 3.0f, 20.0f),
                    0.0f, 0.0f);
    // The sun under the fog (ShadowSampling.glsl), in every view.
    const bool shading = fogResources.filled && fogWanted();
    const float *l = Game::sunLightDirection;
    const float length = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    const float rise = length > 0.0f ? l[1] / length : 1.0f;
    program->setVec("fogShape", shading && fog.quality > 0 && fog.map != nullptr
                    ? fog.amount * 3.912f / std::max(fog.visibility, 10.0f) : 0.0f, fog.depth,
                    fog.base, 1.0f / std::max(rise, 0.05f));
    float patches[4], drift[4];
    fogPatchPlace(patches, drift);
    program->setVec("fogPatchPlace", patches[0], patches[1], patches[2], patches[3]);
    program->setVec("fogDrift", drift[0], drift[1], drift[2], drift[3]);
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
    if (f.skyPipeline == nullptr || f.skyViewColor != view.color || f.skyUnder == nullptr
            || f.skyUnder->pixelSize() != view.size || f.skyUnder->format() != view.color->format()) {
        delete f.skyPipeline;
        delete f.skyBindings;
        delete f.skyTarget;
        delete f.skyPass;
        delete f.skyUnder;
        f.skyPipeline = nullptr;
        f.skyBindings = nullptr;
        f.skyViewColor = view.color;
        if (f.skyUniforms == nullptr) {
            f.skyUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
            f.skyUniforms->create();
        }
        // The view so far, which the pass reads under the fog.
        f.skyUnder = rhi->newTexture(view.color->format(), view.size, 1);
        if (!f.skyUnder->create()) {
            delete f.skyUnder;
            f.skyUnder = nullptr;
            return;
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
                                    QRhiShaderResourceBinding::sampledTexture(2, fragment, f.integrated, f.linear),
                                    QRhiShaderResourceBinding::sampledTexture(3, fragment, f.skyUnder,
                                                                              sampler(false, true, true))});
        f.skyBindings->create();
        static QShader vertex, sky;
        if (!vertex.isValid()) {
            vertex = bakeInline(fullScreenVertex(rhi).constData(), QShader::VertexStage, rhi);
            sky = bakeInline(SkyFragment, QShader::FragmentStage, rhi);
        }
        QRhiGraphicsPipeline *ps = rhi->newGraphicsPipeline();
        ps->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, sky}});
        // Replacing the sky's pixels (the rest discarded).
        ps->setTargetBlends({QRhiGraphicsPipeline::TargetBlend()});
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
    const float params[4] = {0.99f, fogLightScale(), 0.0f, 0.0f};
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->updateDynamicBuffer(f.skyUniforms, 0, sizeof(params), params);
    batch->copyTexture(f.skyUnder, view.color);
    QRhiCommandBuffer *cb = surfaceNow->frame().commandBuffer;
    cb->beginPass(f.skyTarget, Qt::black, {1.0f, 0}, batch);
    cb->setGraphicsPipeline(f.skyPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(view.size.width()), float(view.size.height())));
    cb->setShaderResources(f.skyBindings);
    cb->draw(3);
    cb->endPass();
}
