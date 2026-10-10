// Water surface for the TSRE_WATER variant: wind waves from the wave
// cascades, Fresnel reflection of the environment cube (a sky gradient without one)
// and sun glints over the route's water colour. The colour is the route's
// water layers stacked, without their animation: the base texture is the
// top layer, over the middle and bottom layers. Included by StandardFog.fs
// after the shadow sampling.
in vec3 vWorldPosition;

// Seconds; frozen in tests.
uniform float waterTime;
// Bit per lower layer present: 1 bottom, 2 middle.
uniform int waterLayers;
uniform sampler2D waterBottomMap;
uniform sampler2D waterMiddleMap;
// Wave cascades (WaterWaves), one layer each: the slopes along x and z (rg)
// and their squares (ba), so the mipmaps keep the slope variance.
uniform sampler2DArray waterWaveMap;
// Wind: the direction it blows (x, z, as drawn) and its speed in m/s (z).
uniform vec4 waterWind;
// The scene mirrored in the water plane (PlanarReflection), read at the
// screen position: inverse viewport size and mipmap levels in w (0: none),
// and the plane (n . p + d = 0), tilted along sloping rivers.
uniform sampler2D waterReflectionMap;
uniform vec4 waterReflectionView;
uniform vec4 waterReflectionPlane;
// The frame drawn before the water and its depth, read at the screen
// position: inverse viewport size, colour mipmap levels, and 1 in w when
// they are there (0: water does not see the bed). The scene band's slice
// of the depth range (x, y) and its near and far planes (z, w).
uniform sampler2D waterSceneColor;
uniform sampler2D waterSceneDepth;
uniform vec4 waterScene;
uniform vec4 waterDepthRange;

#include "EnvironmentLighting.glsl"

// Metres per repeat of each cascade, as WaterWaves::Periods: 2048 / 7, / 53
// and / 389, so they continue across tiles and line up once per tile.
const vec3 WaterWavePeriods = vec3(292.571429, 38.641509, 5.264781);
// Gusts: wave strength varies over two sizes of patches (cells per 2048 m
// tile, so they too continue across tiles), drifting with the wind at this
// share of its speed. Short waves follow the gusts most; the longest waves
// are older and change less.
const vec2 WaterGustCells = vec2(9.0, 23.0);
const float WaterGustDrift = 0.4;
const vec2 WaterGustShortWaves = vec2(0.3, 1.7);
const vec2 WaterGustLongWaves = vec2(0.8, 1.2);
// Part of the layer colour that is the water itself, not reflection.
const float WaterBodyShare = 0.6;
// Screen offset of the mirrored scene per unit of slope, and mipmap levels
// of blur at roughness 1.
const float WaterReflectionDistortion = 0.08;
const float WaterReflectionBlur = 4.0;
// Light lost per metre of water, red first: water with some silt, the bed
// clear under a few decimetres and gone under about two metres.
const vec3 WaterExtinction = vec3(1.6, 1.1, 1.0);
// Screen offset of the bed per unit of slope, and the depth (m) below
// which the surface fades into the bed at the shore.
const float WaterRefraction = 0.03;
const float WaterShoreDepth = 0.15;

// Mean slope over the pixel and its variance, from one cascade, scaled by
// the gusts.
vec2 waterCascade(vec2 p, int cascade, float strength, inout float variance) {
    vec4 t = texture(waterWaveMap, vec3(p / WaterWavePeriods[cascade], float(cascade)));
    variance += strength * strength * dot(max(t.ba - t.rg * t.rg, vec2(0.0)), vec2(1.0));
    return t.rg * strength;
}

float waterHash(vec2 cell) {
    return fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
}

// Smooth value noise in 0-1 with the given cells per 2048 m, repeating
// with the tile.
float waterNoise(vec2 p, float cells) {
    vec2 q = p * (cells / 2048.0);
    vec2 i = floor(q);
    vec2 f = q - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = waterHash(mod(i, cells));
    float b = waterHash(mod(i + vec2(1.0, 0.0), cells));
    float c = waterHash(mod(i + vec2(0.0, 1.0), cells));
    float d = waterHash(mod(i + vec2(1.0, 1.0), cells));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// The wave slope over the pixel and the variance of the waves too small
// for it: the three cascades, stronger and weaker in gust patches.
vec2 waterWaves(vec2 p, out float variance) {
    vec2 drift = mod(waterWind.xy * (waterWind.z * WaterGustDrift * waterTime), 2048.0);
    float gust = 0.6 * waterNoise(p - drift, WaterGustCells.x)
            + 0.4 * waterNoise(p - drift, WaterGustCells.y);
    float shortWaves = mix(WaterGustShortWaves.x, WaterGustShortWaves.y, gust);
    float longWaves = mix(WaterGustLongWaves.x, WaterGustLongWaves.y, gust);
    variance = 0.0;
    return waterCascade(p, 0, longWaves, variance)
            + waterCascade(p, 1, shortWaves, variance)
            + waterCascade(p, 2, shortWaves, variance);
}

// The surroundings reflected along r: the mirrored scene where the water
// lies near its plane, otherwise the environment cube. Without the cube the
// horizon reflects as the dark banks and the sky only higher up.
vec3 waterReflection(vec3 r, vec2 slope, float roughness) {
    vec3 far;
    if (environmentMapLevels > 0.0) {
        far = environmentRadiance(r, roughness);
    } else {
        vec3 banks = toLinear(ambientColor.rgb * 0.6);
        far = mix(banks, toLinear(skyColor.rgb), smoothstep(0.1, 0.6, r.y));
    }
    if (waterReflectionView.w <= 0.0)
        return far;
    // Water off the plane would mirror the scene shifted by twice its
    // distance from it; allow what stays under about half a degree.
    float offPlane = abs(dot(waterReflectionPlane.xyz, vWorldPosition) + waterReflectionPlane.w);
    float tolerance = 0.3 + 0.006 * length(cameraPosition - vWorldPosition);
    float inPlane = 1.0 - smoothstep(tolerance, 2.0 * tolerance, offPlane);
    if (inPlane <= 0.0)
        return far;
    vec2 uv = gl_FragCoord.xy * waterReflectionView.xy + slope * WaterReflectionDistortion;
    vec3 mirrored = toLinear(textureLod(waterReflectionMap, clamp(uv, 0.0, 1.0),
                                        roughness * WaterReflectionBlur).rgb);
    return mix(far, mirrored, inPlane);
}

// Distance along the view axis of a depth in the scene band; far away for
// what lies behind it (distant terrain, sky).
float waterViewDistance(float depth) {
    if (depth >= waterDepthRange.y)
        return 1e6;
    float z = (depth - waterDepthRange.x) / (waterDepthRange.y - waterDepthRange.x) * 2.0 - 1.0;
    float near = waterDepthRange.z, far = waterDepthRange.w;
    return 2.0 * near * far / ((far + near) - z * (far - near));
}

// What is seen through the water: the bed, refracted by the waves and
// fading into the deep colour with the path through the water. Also the
// water's depth under the surface, in metres.
vec3 waterUnderwater(vec3 deep, vec2 slope, vec3 v, out vec3 transmittance, out float below) {
    vec2 uv = gl_FragCoord.xy * waterScene.xy;
    float surface = waterViewDistance(gl_FragCoord.z);
    // Metres along the view ray per metre along the view axis.
    float stretch = length(cameraPosition - vWorldPosition) / max(surface, 1e-3);
    float path = max(waterViewDistance(texture(waterSceneDepth, uv).r) - surface, 0.0) * stretch;
    below = path * clamp(v.y, 0.0, 1.0);
    // Refraction grows with the depth, so the shore line stays put; what
    // stands in front of the water is not refracted into it.
    vec2 refracted = uv + slope * WaterRefraction * clamp(below, 0.0, 1.0);
    float refractedPath = (waterViewDistance(texture(waterSceneDepth, refracted).r) - surface) * stretch;
    if (refractedPath > 0.0) {
        uv = refracted;
        path = refractedPath;
    }
    vec3 bed = toLinear(textureLod(waterSceneColor, uv, 0.0).rgb);
    transmittance = exp(-WaterExtinction * min(path, 100.0));
    return mix(deep, bed, transmittance);
}

vec4 waterShade() {
    // The layers stacked as the route draws them: bottom, middle, top.
    vec4 top = texture(uSampler, vTextureCoord);
    vec3 body = top.rgb;
    if (waterLayers != 0) {
        body = (waterLayers & 1) != 0 ? texture(waterBottomMap, vTextureCoord).rgb : vec3(0.0);
        if ((waterLayers & 2) != 0) {
            vec4 middle = texture(waterMiddleMap, vTextureCoord);
            body = mix(body, middle.rgb, middle.a);
        }
        body = mix(body, top.rgb, top.a);
    }
    // Lit like the legacy layers, by the sun on a flat surface.
    vec3 l = normalize(lightDirection);
    float shadow = clamp(shadowedVisibility(clamp(l.y, 0.0, 1.0)), 0.0, 1.0);
    body *= (diffuseColor.rgb * shadow + ambientColor.rgb) * colorBrightness;

    float variance;
    vec2 slope = waterWaves(vWorldPosition.xz, variance);
    vec3 n = normalize(vec3(-slope.x, 1.0, -slope.y));
    vec3 v = normalize(cameraPosition - vWorldPosition);
    float nDotV = clamp(dot(n, v), 1e-4, 1.0);
    // Waves smaller than a pixel roughen the surface instead (GGX alpha
    // from their slope variance).
    float alpha = sqrt(0.0004 + variance);
    float roughness = sqrt(alpha);

    vec3 r = reflect(-v, n);
    r.y = abs(r.y);
    float fresnel = 0.02 + 0.98 * pow(1.0 - nDotV, 5.0);
    // The layer textures include the sky they usually reflect; the body
    // keeps the rest as the colour of deep water, and the reflection is
    // added for the actual view.
    vec3 deep = toLinear(body) * WaterBodyShare;
    vec3 under = deep;
    // Share of the bed in the colour, already fogged in the frame copy.
    float bedShare = 0.0;
    // The surface (reflection and glints) fades in over the first
    // centimetres of depth, so the shore meets the bed without a line.
    float surface = 1.0;
    if (waterScene.w > 0.0) {
        vec3 transmittance;
        float below;
        under = waterUnderwater(deep, slope, v, transmittance, below);
        surface = smoothstep(0.0, WaterShoreDepth, below);
        fresnel *= surface;
        bedShare = dot(transmittance, vec3(1.0 / 3.0)) * (1.0 - fresnel);
    }
    vec3 color = mix(under, waterReflection(r, slope, roughness) * colorBrightness, fresnel);

    float nDotL = max(dot(n, l), 0.0);
    if (nDotL > 0.0) {
        vec3 sunFresnel;
        vec3 sun = PbrPi * toLinear(diffuseColor.rgb) * shadow;
        color += ggxSpecular(n, v, l, roughness, vec3(0.02), sunFresnel) * sun * nDotL
                * colorBrightness * surface;
    }
    return vec4(mix(toDisplay(color), skyColor.rgb, fogFactor * (1.0 - bedShare)), 1.0);
}
