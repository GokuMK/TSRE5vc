// Linear-light helpers and the environment cube shared by the PBR, water and
// classic shading. Included after the common uniforms (skyColor) of
// StandardFog.fs.
uniform samplerCube environmentMap;
// Mipmap levels of the environment map; 0 when none is bound.
uniform float environmentMapLevels;
#ifndef TSRE_CAMERA_POSITION
#define TSRE_CAMERA_POSITION
uniform vec3 cameraPosition;
#endif

const float PbrPi = 3.14159265;

vec3 toLinear(vec3 c) {
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 toDisplay(vec3 c) {
    return pow(clamp(c, vec3(0.0), vec3(1.0)), vec3(1.0 / 2.2));
}

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// The surroundings along a direction, blurred for a GGX roughness, in linear
// colour: the prefiltered level for that roughness. Without a map, the sky
// above dark ground, with a horizon as sharp as the roughness allows, so
// smooth surfaces show the horizon line and rough ones a soft gradient.
vec3 environmentRadiance(vec3 direction, float roughness) {
    if (environmentMapLevels > 0.0)
        return toLinear(textureLod(environmentMap, direction,
                                   roughness * (environmentMapLevels - 1.0)).rgb);
    float width = mix(0.03, 1.0, roughness * roughness);
    return mix(vec3(0.05), toLinear(skyColor.rgb), smoothstep(-width, width, direction.y));
}

// The sky's light on surfaces (linear), its colour held in: saturation
// ((max - min) / max) at most MaxSkyLightSaturation, nearer grey at the
// same luminance. A night sky's deep blue (about 0.93) tinted the shade
// strongly; a gentle limit (the user, 2026-10-28) leaves a clear day's
// (about 0.7) as it is. Diffuse light only: reflections show the sky.
const float MaxSkyLightSaturation = 0.75;

vec3 skyLight(vec3 light) {
    float peak = max(light.r, max(light.g, light.b));
    float low = min(light.r, min(light.g, light.b));
    if (peak <= 0.0 || peak - low <= MaxSkyLightSaturation * peak)
        return light;
    float l = luminance(light);
    // Mixed towards l: the saturation reaches the limit.
    return mix(vec3(l), light, MaxSkyLightSaturation * l / (peak - low - MaxSkyLightSaturation * (peak - l)));
}

// Split-sum environment BRDF as an analytic fit (Karis, mobile), in place
// of a lookup texture.
// f90 is the reflectance at grazing angles (1 unless KHR_materials_specular
// lowers it).
vec3 environmentBrdf(vec3 f0, vec3 f90, float roughness, float nDotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + f90 * ab.y;
}

vec3 environmentBrdf(vec3 f0, float roughness, float nDotV) {
    return environmentBrdf(f0, vec3(1.0), roughness, nDotV);
}

// GGX specular lobe (distribution, Smith visibility, Schlick Fresnel) for
// the sun; fresnel returns the Fresnel term for energy splitting.
vec3 ggxSpecular(vec3 n, vec3 v, vec3 l, float roughness, vec3 f0, vec3 f90,
                 out vec3 fresnel) {
    vec3 h = normalize(l + v);
    float nDotL = max(dot(n, l), 0.0);
    float nDotV = max(dot(n, v), 1e-4);
    float nDotH = max(dot(n, h), 0.0);
    float vDotH = max(dot(v, h), 0.0);
    float a = roughness * roughness;
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    float distribution = a2 / (PbrPi * d * d);
    float k = a * 0.5;
    float visibilityTerm = 0.25 / ((nDotL * (1.0 - k) + k) * (nDotV * (1.0 - k) + k));
    fresnel = f0 + (f90 - f0) * pow(1.0 - vDotH, 5.0);
    return distribution * visibilityTerm * fresnel;
}

vec3 ggxSpecular(vec3 n, vec3 v, vec3 l, float roughness, vec3 f0, out vec3 fresnel) {
    return ggxSpecular(n, v, l, roughness, f0, vec3(1.0), fresnel);
}
