// Water surface for the TSRE_WATER variant: moving waves from a wave map,
// Fresnel reflection of the environment cube (a sky gradient without one)
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
// Tileable slopes (rg, 0.5 flat) and their squares (ba); see WaterNormalMap.
uniform sampler2D waterNormalMap;
// The scene mirrored in the water plane (PlanarReflection), read at the
// screen position: inverse viewport size and mipmap levels in w (0: none),
// and the plane (n . p + d = 0), tilted along sloping rivers.
uniform sampler2D waterReflectionMap;
uniform vec4 waterReflectionView;
uniform vec4 waterReflectionPlane;

#include "EnvironmentLighting.glsl"

// Two scales of the wave map, in metres per repeat (dividing the 2048 m
// tile, so the waves continue across tiles), drift in metres per second and
// slope strength. The second is sampled transposed to break up repeats.
const float WaterLargePeriod = 16.0;
const vec2 WaterLargeDrift = vec2(0.31, 0.18);
const float WaterLargeSlope = 0.22;
const float WaterSmallPeriod = 5.12;
const vec2 WaterSmallDrift = vec2(-0.17, 0.26);
const float WaterSmallSlope = 0.14;
// Part of the layer colour that is the water itself, not reflection.
const float WaterBodyShare = 0.6;
// Screen offset of the mirrored scene per unit of slope, and mipmap levels
// of blur at roughness 1.
const float WaterReflectionDistortion = 0.08;
const float WaterReflectionBlur = 4.0;

// Mean slope over the pixel and its variance, from one scale of the map.
vec2 waterWaves(vec2 uv, bool transposed, float strength, inout float variance) {
    vec4 t = texture(waterNormalMap, uv);
    vec2 slope = t.rg * 2.0 - 1.0;
    vec2 square = t.ba;
    if (transposed) {
        slope = slope.yx;
        square = square.yx;
    }
    variance += strength * strength * dot(max(square - slope * slope, vec2(0.0)), vec2(1.0));
    return slope * strength;
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

    vec2 p = vWorldPosition.xz;
    float variance = 0.0;
    vec2 slope = waterWaves((p + WaterLargeDrift * waterTime) / WaterLargePeriod, false,
                            WaterLargeSlope, variance)
            + waterWaves((p + WaterSmallDrift * waterTime).yx / WaterSmallPeriod, true,
                         WaterSmallSlope, variance);
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
    // keeps the rest, and the reflection is added for the actual view.
    vec3 color = mix(toLinear(body) * WaterBodyShare,
                     waterReflection(r, slope, roughness) * colorBrightness, fresnel);

    float nDotL = max(dot(n, l), 0.0);
    if (nDotL > 0.0) {
        vec3 sunFresnel;
        vec3 sun = PbrPi * toLinear(diffuseColor.rgb) * shadow;
        color += ggxSpecular(n, v, l, roughness, vec3(0.02), sunFresnel) * sun * nDotL
                * colorBrightness;
    }
    return vec4(mix(toDisplay(color), skyColor.rgb, fogFactor), 1.0);
}
