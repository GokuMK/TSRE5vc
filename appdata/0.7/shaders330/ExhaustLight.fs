#version 330 core

// Smoke and steam (task renderer 41, RhiExhaust.cpp): each particle's light
// at its centre, once a frame, one fragment a particle (its texel in the
// particle textures), as the scene's surfaces take it (StandardFog.fs): the
// sun that reaches it (the shadow maps, vegetation's, the clouds' shadow,
// cover over it), the sky's light and the lamps', and the air and the fog
// between it and the camera. The particles' pass shades each pixel of a
// puff from these, with the plume's shadow on itself (QRhi only).

// The sun's light (linear, the scene's scale), 0.
layout(location = 0) out vec4 sunOut;
// The sky's and the lamps' light (linear), 0.
layout(location = 1) out vec4 skyOut;
// The fog's light added before it (linear), the share it lets through.
layout(location = 2) out vec4 fogOut;
// What the air lets through, per colour; 0.
layout(location = 3) out vec4 throughOut;
// The air's own light (linear); 0.
layout(location = 4) out vec4 airOut;

// The particles: each one's centre (scene space) and radius, 256 to a row.
uniform sampler2D exhaustParticles;
uniform float exhaustCount;
// How much the clouds dim the sky's light (0 clear, 1 all of it).
uniform float exhaustOvercast;

uniform int shadowsEnabled;
uniform float shadow1Res;
uniform float shadow2Res;
uniform float shadow2Bias;
uniform sampler2DShadow shadow1;
uniform sampler2DShadow shadow2;
uniform sampler2DShadow shadow0;
uniform mat4 uShadowPMatrix;
uniform mat4 uShadow2PMatrix;
uniform mat4 uShadow0PMatrix;
uniform mat4 uFMatrix;
uniform float enableNormals;
uniform float fogDensity;
uniform vec4 skyColor;
uniform vec4 diffuseColor;
uniform vec4 ambientColor;
uniform vec3 lightDirection;
uniform vec4 skyAbove;

// ShadowSampling.glsl's surface lookups (not taken here) read these.
vec4 shadowPos;
vec4 shadow2Pos;
vec4 shadow0Pos;
vec2 poissonDisk[16] = vec2[](
   vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.094184101, -0.92938870),
   vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
   vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379), vec2(0.44323325, -0.97511554),
   vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
   vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367),
   vec2(0.14383161, -0.14100790));
// A puff's four shadow taps, a square about its centre.
const vec2 PuffTaps[4] = vec2[](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(-0.5, 0.5), vec2(0.5, 0.5));

#include "ShadowSampling.glsl"
#include "LocalLights.glsl"
#include "Haze.glsl"
#include "EnvironmentLighting.glsl"

// As StandardFog.fs: the shade's light (display colour), the ambient's
// brightness in the colour of the sky's light from above.
vec3 ambientLight() {
    vec3 sky = skyLight(skyAbove.w > 0.0 ? skyAbove.rgb : environmentRadiance(vec3(0.0, 1.0, 0.0), 1.0));
    float skyLuminance = luminance(sky);
    if (skyLuminance < 1e-12)
        return ambientColor.rgb;
    return pow(sky * (luminance(toLinear(ambientColor.rgb)) / skyLuminance), vec3(1.0 / 2.2));
}

// The share of four taps a shadow map lets through, about a point of it
// (its coordinates, 0-1) a spread apart; outside the map -1.
float shadowTaps(sampler2DShadow map, vec4 clip, float spread) {
    if (length(clip.xyz) >= 1.0)
        return -1.0;
    vec3 m = clip.xyz * 0.5 + 0.5;
    float lit = 0.0;
    for (int i = 0; i < 4; ++i)
        lit += texture(map, vec3(m.xy + PuffTaps[i] * spread, m.z - 0.001)) / 4.0;
    return lit;
}

// A shadow map's shift (map units) for a distance (m) across it.
float mapSpread(mat4 matrix, float metres) {
    return 0.5 * length(vec3(matrix[0][0], matrix[1][0], matrix[2][0])) * metres;
}

// The sun at a puff's centre: the near map, else the middle one, else the
// far one (as the surfaces take them), taps over half its size; the far
// map's shadow lighter, as on the surfaces; trees' shadows (task 37) too.
float puffSun(vec3 p, float radius) {
    if (shadowsEnabled == 0)
        return 1.0;
    vec4 c0 = uShadow0PMatrix * vec4(p, 1.0);
    vec4 c1 = uShadowPMatrix * vec4(p, 1.0);
    vec4 c2 = uShadow2PMatrix * vec4(p, 1.0);
    float lit = shadowTaps(shadow0, c0, mapSpread(uShadow0PMatrix, radius * 0.5));
    float trees = vegetationShadowsOn != 0 ? shadowTaps(vegetationShadow0, c0, mapSpread(uShadow0PMatrix, radius * 0.5))
                                           : 1.0;
    float contrast = 1.0;
    if (lit < 0.0) {
        lit = shadowTaps(shadow1, c1, mapSpread(uShadowPMatrix, radius * 0.5));
        trees = vegetationShadowsOn != 0 ? shadowTaps(vegetationShadow1, c1, mapSpread(uShadowPMatrix, radius * 0.5))
                                         : 1.0;
    }
    if (lit < 0.0) {
        lit = shadowTaps(shadow2, c2, mapSpread(uShadow2PMatrix, radius * 0.5));
        trees = vegetationShadowsOn != 0 ? shadowTaps(vegetationShadow2, c2, mapSpread(uShadow2PMatrix, radius * 0.5))
                                         : 1.0;
        contrast = 0.5;
    }
    if (lit < 0.0)
        return 1.0;
    float shaded = (1.0 - lit) + (1.0 - max(trees, 0.0)) - (1.0 - lit) * (1.0 - max(trees, 0.0));
    return clamp(1.0 - contrast * shaded, 0.0, 1.0);
}

// The lamps' light on a puff (task 34), from every side: each lamp's
// inverse square softened by the puff's own size too (it lights the puff as
// a whole, not its centre: at the centre a buffer beam's lamp a few
// decimetres off lit passing steam several times as bright as the sun).
vec3 puffLamps(vec3 p, float radius) {
    vec3 sum = vec3(0.0);
    ivec2 cell = localLightCell(p);
    for (int i = 0; i < cell.y; ++i) {
        vec3 l, radiance;
        if (!localLight(cell.x + i, p, l, radiance))
            continue;
        int slot = cell.x + i;
        int light = int(texelFetch(localLightIndices, ivec2(slot % 4096, slot / 4096), 0).r);
        vec3 toLight = texelFetch(localLightData, ivec2(0, light), 0).xyz - p;
        float soft = dot(toLight, toLight) + texelFetch(localLightData, ivec2(1, light), 0).w;
        sum += radiance / LocalLightPi * soft / (soft + radius * radius);
    }
    return sum;
}

void main() {
    ivec2 at = ivec2(gl_FragCoord.xy);
    if (float(at.x + at.y * 256) >= exhaustCount)
        discard;
    vec4 particle = texelFetch(exhaustParticles, at, 0);
    vec3 p = particle.xyz;
    vec3 sunDirection = normalize(lightDirection);
    // Cover over it (a roof, a tunnel): less of the sky, the sun past the
    // shadow maps as the surfaces take it.
    float shade;
    float enclosed = skyEnclosed(p, vec3(0.0), shade);
    // (the plume's own shadow is the puffs' pass's, point by point)
    float sun = puffSun(p, particle.w) * cloudShadow(p) * enclosureSun(p, shade, sunDirection);
    sunOut = vec4(toLinear(diffuseColor.rgb) * sun, 0.0);
    // The sky's light, under cover part way to the neutral ambient and
    // darker, as the surfaces'; the lamps' (task 34), from every side.
    vec3 ambient = ambientLight();
    float under = enclosed > 0.0 ? underCover(p) : 0.0;
    ambient = mix(ambient, ambientColor.rgb, UnderCoverColour * under);
    float skyShare = min(skyAmbient(shade), mix(1.0, UnderCoverLight, under));
    // In the open by day a lamp does not light steam visibly beside the sun's
    // light (the scene's lamps keep their night strength by day, for their
    // glow): none then, all of it at night and under cover; at most about a
    // white surface in full sun.
    float day = smoothstep(-0.05, 0.15, sunDirection.y) * (1.0 - enclosed);
    vec3 lamps = puffLamps(p, particle.w) * lampScale(shade) * (1.0 - day);
    lamps /= 1.0 + luminance(lamps) / 0.5;
    // The air and the fog before it, as applyHaze takes them; the classic
    // fog without a physical sky.
    if (hazeOn()) {
        fogOut = fogAt(p);
        throughOut = vec4(airThrough(p), 0.0);
        airOut = vec4(toLinear(hazeLight(p) * hazeLightScale(p, shade)), 0.0);
    } else {
        vec4 f = uFMatrix * vec4(p, 1.0);
        float fogFactor = abs(min(clamp(length(f.xz) / (lod * 1.4), 0.0, fogDensity), lod));
        fogOut = vec4(0.0, 0.0, 0.0, 1.0);
        throughOut = vec4(vec3(1.0 - fogFactor), 0.0);
        airOut = vec4(toLinear(skyColor.rgb * hazeLightScale(p, shade)), 0.0);
    }
    // Under clouds a puff scatters the grey sky about it and the light the
    // ground sends up, which the air's light stands for: the surfaces'
    // ambient, dimmed by the clouds, made steam in their shade black against
    // a snowy day's bright air (half the air's, slate grey under broken
    // clouds). Clear, the surfaces' (the horizon's bright air would leave a
    // sunlit plume no shaded side).
    vec3 sky = mix(toLinear(ambient) * skyShare, 0.8 * airOut.rgb * skyShare, clamp(exhaustOvercast, 0.0, 1.0));
    skyOut = vec4(sky + lamps, 0.0);
}
