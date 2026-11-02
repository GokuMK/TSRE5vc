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
// the main view, after its blended parts and water (their translucent texels
// write no depth: a window drawn after a puff in front of it showed through,
// dark), before glTF transmission:
// 1. the plume seen from the sun: each puff near the camera (in view or not)
//    adds its optical depth and its depth's first two moments to a map
//    along the sun's light;
//    their light at their centres, one texel a particle, by a program of the
//    scene's shading (ExhaustLight.fs): the sun through the shadow maps, the
//    clouds' shadow and cover, the sky's light and the lamps', the air and
//    the fog before them;
// 2. each a soft sphere of noisy density, back to front, into a target of
//    half the view's size (High: the view's): its thickness along the
//    pixel's ray (a profile keeping the puff's extinction, its edge eroded
//    by a 3D noise drifting through it with its age), cut where the scene
//    stands in it; lit as a whole (a thin puff scattering the sun once, steam
//    forward, smoke less; a thick one evenly) and by the sky, the sun through
//    the plume at its surface seen along the pixel's ray (the map: a field of
//    the world; the surface: the puffs' depths over the pixel weighted by
//    how much each covers, their mean less their mean radius, a pass before,
//    so the same point for every puff over the pixel);
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

// Particles a row of the particle textures, rows (High's limit and its
// copies).
constexpr int Row = 256;
constexpr int Rows = 64;
constexpr int MaxParticles = Row * Rows;
// An even flow's puffs from a moving nozzle (or a fast jet) leave apart: a
// row of beads near the nozzle. Copies fill the gap to the next puff where
// it is wider than this share of the radius (the eroded puff shows about
// half its sphere; at a half, the trail stayed lumpy), up to MaxCopies, each taking its share of the density;
// grown puffs need none.
constexpr float CopySpacing = 0.33f;
constexpr int MaxCopies = 3;
// The puffs' step through the noise (ExhaustParticles' SeedStep).
constexpr float SeedStep = 0.05f;
constexpr int NoiseSize = 32;
// The sun's map of the plume: its size, and how far from the camera puffs
// cast and take its shadow (m).
constexpr int SunMapSize = 256;
constexpr float SunReach = 250.0f;

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
    float toSun[16];     // scene space to the sun's map (x, y 0-1 across it, depth 0-1 along the light)
    float sunMap[4];     // the map on, depth units a metre, 0, 0
};

// The sun's map of the plume (std140).
struct SunParameters {
    float toMap[16]; // scene space to the map: x, y 0-1 across it, depth 0-1 along the light
    float scale[4];  // map units a metre across, depth units a metre, reach (m), clip y's sign
    float camera[4]; // where the camera is (scene space)
};

// The composite (std140).
struct CompositeParameters {
    float projection[4]; // scene near, far, its depth band's end, 0
    float size[4];       // the puffs' target: width, height
};

const char *DrawVertex = R"(#version 440
layout(location = 0) out vec3 ray;
// The puff's centre from the camera and its radius; its extinction (1/m),
// its place in the noise and its age (s); its light (linear, seen through
// the air and the fog): the sun's, to be dimmed by the plume before the
// point seen, and the rest.
layout(location = 1) flat out vec4 centre;
layout(location = 2) flat out vec4 matter;
layout(location = 3) flat out vec3 sunColour;
layout(location = 4) flat out vec3 restColour;
layout(location = 5) flat out vec3 skyColour;
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
    mat4 toSun;
    vec4 sunMap;
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
// Steam's droplets white; the smoke's colour is the particle's (its soot's
// grey or the ENG's).
const vec3 SteamAlbedo = vec3(0.95);

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
    // Its light is the whole puff's (point by point, from each sphere's own
    // shape, every puff's sunlit rim showed and a plume looked like a stack
    // of discs), but for the plume's shadow (per pixel, the fragment
    // shader's; the scene's shadows are the light pass's). A thin puff
    // scatters the sun once (steam forward, its droplets; smoke less), a
    // thick one evenly (light scattered many times inside).
    vec4 tint = texelFetch(particleTints, at, 0);
    float sigma = matter.x + matter.y;
    float tauPuff = 2.0 * sigma * r;
    float cosTheta = dot(normalize(rel), sun.xyz);
    float steamShare = matter.x / max(sigma, 1e-6);
    float single = mix(phase(0.4, cosTheta), mix(phase(-0.2, cosTheta), phase(0.75, cosTheta), 0.7), steamShare);
    float thick = 1.0 - exp(-0.5 * tauPuff);
    float sunShare = mix(single, 0.8, thick);
    vec3 albedo = mix(tint.rgb, SteamAlbedo, steamShare);
    // Through the fog and the air before it (Haze.glsl applyHaze).
    vec4 fog = texelFetch(lightFog, at, 0);
    vec3 through = texelFetch(lightThrough, at, 0).rgb;
    sunColour = albedo * texelFetch(lightSun, at, 0).rgb * (sunShare * SunScale) * fog.a * through;
    skyColour = albedo * texelFetch(lightSky, at, 0).rgb * SkyScale * fog.a * through;
    restColour = fog.rgb * through + texelFetch(lightAir, at, 0).rgb * (1.0 - through);
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
layout(location = 3) flat in vec3 sunColour;
layout(location = 4) flat in vec3 restColour;
layout(location = 5) flat in vec3 skyColour;
// Display colour with premultiplied alpha, back to front.
layout(location = 0) out vec4 result;
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
    mat4 toSun;
    vec4 sunMap;
};
layout(binding = 9) uniform sampler3D noise;
layout(binding = 10) uniform sampler2D sceneDepth;
// The plume seen from the sun: its optical depth added along the light, and
// its depths' sums (times the optical depth, of their squares).
layout(binding = 11) uniform sampler2D sunPlume;
// The puffs over the pixel: how much each covers added, times its centre's
// depth along the view's axis, times its radius (both an eighth, m).
layout(binding = 12) uniform sampler2D plumeFront;

// What the plume lets through of the sun to a point (scene space) known to
// within some radius (m) along the light: the optical depth before it, the
// plume's along the light taken as spread normally about its mean depth
// (its two moments, widened by that radius); multiple scattering's light
// passing much deeper (a heavy tail, as the clouds' shadow, 1 / (1 + k d)):
// a dense plume's shade is grey, not black, and its sunlit skin no step.
float plumeSun(vec3 p, float radius) {
    if (sunMap.x < 0.5)
        return 1.0;
    vec3 m = (toSun * vec4(p, 1.0)).xyz;
    if (any(lessThan(m.xy, vec2(0.0))) || any(greaterThan(m.xy, vec2(1.0))))
        return 1.0;
    vec4 sums = textureLod(sunPlume, m.xy, 0.0);
    if (sums.x < 1e-4)
        return 1.0;
    if (!(sums.x < 6e4) || !(sums.w < 6e4))
        return 0.0;
    float mean = sums.y / sums.x;
    float blur = 0.5 * radius * sunMap.y;
    float spread = sqrt(max(sums.w / sums.x - mean * mean, 0.0) + blur * blur + 1e-8);
    float before = sums.x / (1.0 + exp(-1.702 * (m.z - mean) / spread));
    return 0.6 * exp(-before) + 0.4 / (1.0 + 0.3 * before);
}

// The wisps' size (m) and their share of the noise.
const float WispMetres = 4.0;
const float WispShare = 0.4;

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
    // r through the centre), its edge carved by the noise in its own place,
    // as the clouds' edges are eroded (a dense puff saturates to an even disc
    // inside, so the shape shows at its edge): billows of the puff's size,
    // drifting through it with its age, and wisps of a fixed size, so a grown
    // puff turns ragged rather than a larger copy of a young one; eroded the
    // more as it ages (young smoke dense and round, old torn); each puff
    // denser or thinner than its neighbours.
    vec3 local = q / r;
    vec3 place = matter.z * vec3(1.0, 0.43, 0.78);
    vec4 n = texture(noise, local * shading.x + place + vec3(0.0, -shading.y, 0.0) * matter.w);
    vec4 wisps = texture(noise, q / WispMetres + place * 3.7 + vec3(0.11, 0.05, -0.08) * matter.w);
    // (stretched: a dense puff's edge lies where the eroded profile reaches
    // zero, and the noise's middle range moved it only a tenth of its radius)
    float large = n.r * 0.6 + n.g * 0.4;
    float billows = smoothstep(0.2, 0.8, mix(large, wisps.g * 0.6 + wisps.b * 0.4, WispShare));
    // (1 - x^2)^3 rather than ^2: a softer rim, so neighbours blend into one
    // plume instead of showing each ball's edge.
    float profile = (1.0 - x2) * (1.0 - x2) * (1.0 - x2) * 1.4;
    float erosion = 0.55 + 0.3 * smoothstep(1.0, 15.0, matter.w);
    float inside = clamp((profile - erosion * (1.0 - billows)) / (1.0 - erosion), 0.0, 1.0);
    float density = 0.7 + 0.6 * fract(matter.z * 13.71);
    float tau = 4.0 * (matter.x + matter.y) * r * inside * (0.4 + 1.2 * billows) * seen * density;
    // A fringe over the eroded edge's first third, whatever the density: a
    // dense puff went from clear to opaque within a pixel, stair-stepped at
    // the half size.
    float alpha = (1.0 - exp(-tau)) * smoothstep(0.0, 0.35, inside);
    if (alpha < 0.003)
        discard;
    // The plume's light at its surface seen along this pixel's ray, a little
    // inside (the pass before: the same point for every puff over the
    // pixel); a puff well behind it (another plume) at its own near side.
    // Taken at each puff's own place, a plume of puffs receding from the
    // camera stepped from lit to shaded at every puff.
    vec4 sums = texelFetch(plumeFront, ivec2(gl_FragCoord.xy), 0);
    float own = along - 0.7 * r * (1.0 - exp(-(matter.x + matter.y) * r));
    float surface = own;
    if (sums.x > 0.02)
        surface = max(8.0 * (sums.y - 0.8 * sums.z) / sums.x, 0.0) / cosAxis;
    float at = own > surface + 3.0 * r ? own : surface + 0.2 * r;
    // The billows' relief: the large noise's highs stand out towards the
    // light, its lows sink into the shade (the wisps' would speckle).
    at -= (large - 0.5) * 0.8 * r;
    // The sky's light: less deep in a puff (the plume hides the sky about
    // it), more at its eroded edges and on its top, and along the billows
    // (in shade a plume lit by the sky alone was one flat grey).
    float skyOpen = (1.0 - 0.3 * inside) * (0.92 + 0.08 * clamp(local.y, -1.0, 1.0)) * (0.85 + 0.3 * large);
    vec3 lit = sunColour * plumeSun(camera.xyz + dir * at, r) + skyColour * skyOpen + restColour;
    result = vec4(pow(max(lit, vec3(0.0)), vec3(1.0 / 2.2)) * alpha, alpha);
}
)";

// The plume's surface over a pixel: each puff's centre's depth along the
// view's axis and its radius (an eighth, m), weighted by how much it covers
// (its profile, without the noise), added up: smooth across the plume (the
// nearest dense puff alone jumped at each big puff's rim, drawing discs; the
// depths' spread from half floats' sums of squares speckled). The scene does
// not cut it.
const char *FrontFragment = R"(#version 440
layout(location = 0) in vec3 ray;
layout(location = 1) flat in vec4 centre;
layout(location = 2) flat in vec4 matter;
layout(location = 0) out vec4 front;
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
    mat4 toSun;
    vec4 sunMap;
};

void main() {
    vec3 dir = normalize(ray);
    float r = centre.w;
    float along = dot(centre.xyz, dir);
    vec3 q = dir * along - centre.xyz;
    float x2 = dot(q, q) / (r * r);
    if (x2 >= 1.0)
        discard;
    float w = 1.0 - exp(-2.4 * (matter.x + matter.y) * r * (1.0 - x2) * (1.0 - x2));
    if (w < 0.01)
        discard;
    float depth = max(dot(centre.xyz, axis.xyz), 0.0);
    front = vec4(w, w * depth / 8.0, w * r / 8.0, 0.0);
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
// The puffs' surface sums (the colour pass's plumeFront).
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
    // (each texel's puffs' front: their mean depth less their mean radius,
    // as 1 / (1 + d), the largest the nearest)
    float f = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec4 sums = texelFetch(front, clamp(base + ivec2(i & 1, i >> 1), ivec2(0), last), 0);
        if (sums.x > 0.02)
            f = max(f, 1.0 / (1.0 + max(8.0 * (sums.y - sums.z) / sums.x, 0.0)));
    }
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

const char *SunVertex = R"(#version 440
// Across the disc (-1 to 1); the puff's optical depth through its centre,
// its depth (0-1 along the light), its radius in depth units.
layout(location = 0) out vec2 disc;
layout(location = 1) flat out vec3 puff;
layout(std140, binding = 0) uniform SunMap {
    mat4 toMap;
    vec4 scale;
    vec4 camera;
};
layout(binding = 1) uniform sampler2D particleCentres;
layout(binding = 2) uniform sampler2D particleMatter;

void main() {
    ivec2 at = ivec2(gl_InstanceIndex % 256, gl_InstanceIndex / 256);
    vec4 a = texelFetch(particleCentres, at, 0);
    vec4 m = texelFetch(particleMatter, at, 0);
    int corner = gl_VertexIndex;
    float s = (corner == 1 || corner == 4 || corner == 5) ? 1.0 : -1.0;
    float t = (corner == 2 || corner == 3 || corner == 5) ? 1.0 : -1.0;
    disc = vec2(s, t);
    // Beyond the map's reach: none (the map is placed about the near ones).
    if (length(a.xyz - camera.xyz) > scale.z) {
        puff = vec3(0.0);
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    vec3 p = (toMap * vec4(a.xyz, 1.0)).xyz;
    // At most an optical depth of 30 through it (the sun is gone long
    // before): the sums stay within half floats where cold air keeps
    // hundreds of dense steam puffs (they overflowed, and the plume drew
    // black).
    puff = vec3(min(4.0 * (m.x + m.y) * a.w, 30.0), p.z, a.w * scale.y);
    vec2 corner2 = p.xy + vec2(s, t) * a.w * scale.x;
    gl_Position = vec4(corner2.x * 2.0 - 1.0, (corner2.y * 2.0 - 1.0) * scale.w, 0.5, 1.0);
}
)";

const char *SunFragment = R"(#version 440
layout(location = 0) in vec2 disc;
layout(location = 1) flat in vec3 puff;
// Added: the optical depth along the light, times the depth, times the
// depth's square (with the puff's own depth spread across its chord).
layout(location = 0) out vec4 result;

void main() {
    float x2 = dot(disc, disc);
    if (x2 >= 1.0)
        discard;
    float tau = puff.x * (1.0 - x2) * (1.0 - x2);
    float spread = 0.4 * puff.z * sqrt(1.0 - x2);
    result = vec4(tau, tau * puff.y, 0.0, tau * (puff.y * puff.y + spread * spread));
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
    e.sunUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(SunParameters));
    e.sunMap = rhi->newTexture(QRhiTexture::RGBA16F, QSize(SunMapSize, SunMapSize), 1, QRhiTexture::RenderTarget);
    if (!e.lightUniforms->create() || !e.drawUniforms->create() || !e.compositeUniforms->create()
            || !e.sunUniforms->create() || !e.sunMap->create()) {
        releaseExhaust();
        e.failed = true;
        return false;
    }
    e.sunTarget = rhi->newTextureRenderTarget({QRhiColorAttachment(e.sunMap)});
    e.sunPass = e.sunTarget->newCompatibleRenderPassDescriptor();
    e.sunTarget->setRenderPassDescriptor(e.sunPass);
    const auto vertex = QRhiShaderResourceBinding::VertexStage;
    e.sunBindings = rhi->newShaderResourceBindings();
    e.sunBindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, vertex, e.sunUniforms),
            QRhiShaderResourceBinding::sampledTexture(1, vertex, e.particles[0], e.nearest),
            QRhiShaderResourceBinding::sampledTexture(2, vertex, e.particles[1], e.nearest)});
    if (!e.sunTarget->create() || !e.sunBindings->create()) {
        releaseExhaust();
        e.failed = true;
        return false;
    }
    static QShader sunVertex, sunFragment;
    if (!sunVertex.isValid()) {
        sunVertex = bakeInline(SunVertex, QShader::VertexStage, rhi);
        sunFragment = bakeInline(SunFragment, QShader::FragmentStage, rhi);
    }
    QRhiGraphicsPipeline *sun = rhi->newGraphicsPipeline();
    sun->setShaderStages({{QRhiShaderStage::Vertex, sunVertex}, {QRhiShaderStage::Fragment, sunFragment}});
    sun->setVertexInputLayout(QRhiVertexInputLayout());
    // Added up.
    QRhiGraphicsPipeline::TargetBlend add;
    add.enable = true;
    add.srcColor = add.dstColor = add.srcAlpha = add.dstAlpha = QRhiGraphicsPipeline::One;
    sun->setTargetBlends({add});
    sun->setShaderResourceBindings(e.sunBindings);
    sun->setRenderPassDescriptor(e.sunPass);
    if (!sun->create()) {
        delete sun;
        releaseExhaust();
        e.failed = true;
        return false;
    }
    e.sunPipeline = sun;
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
    dropLater(e.frontPipeline);
    dropLater(e.frontTarget);
    dropLater(e.frontPass);
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
    const QRhiTexture::Format frontFormat = QRhiTexture::RGBA16F;
    e.image = rhi->newTexture(QRhiTexture::RGBA16F, size, 1, QRhiTexture::RenderTarget);
    e.front = rhi->newTexture(frontFormat, size, 1, QRhiTexture::RenderTarget);
    if (!e.image->create() || !e.front->create())
        return false;
    e.frontTarget = rhi->newTextureRenderTarget({QRhiColorAttachment(e.front)});
    e.frontPass = e.frontTarget->newCompatibleRenderPassDescriptor();
    e.frontTarget->setRenderPassDescriptor(e.frontPass);
    e.imageTarget = rhi->newTextureRenderTarget({QRhiColorAttachment(e.image)});
    e.imagePass = e.imageTarget->newCompatibleRenderPassDescriptor();
    e.imageTarget->setRenderPassDescriptor(e.imagePass);
    e.compositeTarget = rhi->newTextureRenderTarget({QRhiColorAttachment(view.color)},
                                                    QRhiTextureRenderTarget::PreserveColorContents);
    e.compositePass = e.compositeTarget->newCompatibleRenderPassDescriptor();
    e.compositeTarget->setRenderPassDescriptor(e.compositePass);
    if (!e.frontTarget->create() || !e.imageTarget->create() || !e.compositeTarget->create())
        return false;
    static QShader drawVertex, drawFragment, frontFragment, compositeVertex, compositeFragment;
    if (!drawVertex.isValid()) {
        drawVertex = bakeInline(DrawVertex, QShader::VertexStage, rhi);
        drawFragment = bakeInline(DrawFragment, QShader::FragmentStage, rhi);
        frontFragment = bakeInline(FrontFragment, QShader::FragmentStage, rhi);
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
            QRhiShaderResourceBinding::sampledTexture(10, fragment, view.depth, e.nearest),
            QRhiShaderResourceBinding::sampledTexture(11, fragment, e.sunMap, e.linear),
            QRhiShaderResourceBinding::sampledTexture(12, fragment, e.front, e.nearest)});
    if (!e.drawBindings->create())
        return false;
    // Added up.
    QRhiGraphicsPipeline *frontPipeline = rhi->newGraphicsPipeline();
    frontPipeline->setShaderStages({{QRhiShaderStage::Vertex, drawVertex},
                                    {QRhiShaderStage::Fragment, frontFragment}});
    frontPipeline->setVertexInputLayout(QRhiVertexInputLayout());
    QRhiGraphicsPipeline::TargetBlend sum;
    sum.enable = true;
    sum.srcColor = sum.dstColor = sum.srcAlpha = sum.dstAlpha = QRhiGraphicsPipeline::One;
    frontPipeline->setTargetBlends({sum});
    frontPipeline->setShaderResourceBindings(e.drawBindings);
    frontPipeline->setRenderPassDescriptor(e.frontPass);
    if (!frontPipeline->create()) {
        delete frontPipeline;
        return false;
    }
    e.frontPipeline = frontPipeline;
    QRhiGraphicsPipeline *draw = rhi->newGraphicsPipeline();
    draw->setShaderStages({{QRhiShaderStage::Vertex, drawVertex}, {QRhiShaderStage::Fragment, drawFragment}});
    draw->setVertexInputLayout(QRhiVertexInputLayout());
    // Back to front over what is there.
    QRhiGraphicsPipeline::TargetBlend over;
    over.enable = true;
    over.srcColor = QRhiGraphicsPipeline::One;
    over.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    over.srcAlpha = QRhiGraphicsPipeline::One;
    over.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    draw->setTargetBlends({over});
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
    // The puffs: each particle, and copies towards its emitter's next one.
    e.puffs.clear();
    for (const ExhaustParticles::Particle &p : live) {
        ExhaustResources::Puff puff;
        puff.radius = p.radius;
        // Its smoke's colour (linear): the ENG's (ARGB), else soot's grey,
        // the darker the sootier (a notching diesel's near black).
        if (p.color != 0) {
            for (int c = 0; c < 3; ++c)
                puff.tint[c] = std::pow(float((p.color >> (16 - 8 * c)) & 0xff) / 255.0f, 2.2f);
        } else {
            std::fill(puff.tint, puff.tint + 3, 0.45f + (0.05f - 0.45f) * std::sqrt(p.soot));
        }
        float gap[3] = {0.0f, 0.0f, 0.0f};
        int copies = 0;
        if (p.interval > 0.0f) {
            for (int i = 0; i < 3; ++i)
                gap[i] = p.trail[i] - p.interval * p.velocity[i];
            copies = std::clamp(int(std::sqrt(dot3(gap, gap)) / (CopySpacing * p.radius)), 0, MaxCopies);
        }
        const float share = 1.0f / float(copies + 1);
        for (int k = 0; k <= copies; ++k) {
            const float f = float(k) * share;
            for (int i = 0; i < 3; ++i)
                puff.position[i] = p.position[i] + f * gap[i];
            puff.matter[0] = p.steamDensity * share;
            puff.matter[1] = p.smokeDensity * share;
            puff.matter[2] = p.seed + f * SeedStep - std::floor(p.seed + f * SeedStep);
            puff.matter[3] = std::max(p.age - f * p.interval, 0.0f);
            e.puffs.push_back(puff);
        }
    }
    // Those in view (their spheres against the view's sides), far to near.
    e.order.clear();
    e.keys.resize(e.puffs.size());
    e.drawn.assign(e.puffs.size(), 0);
    for (size_t i = 0; i < e.puffs.size(); ++i) {
        const ExhaustResources::Puff &p = e.puffs[i];
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
        e.drawn[i] = 1;
    }
    if (e.order.empty())
        return;
    std::sort(e.order.begin(), e.order.end(), [&e](int a, int b) { return e.keys[size_t(a)] > e.keys[size_t(b)]; });
    if (int(e.order.size()) > MaxParticles)
        e.order.erase(e.order.begin(), e.order.end() - MaxParticles);
    const int count = int(e.order.size());
    // Then those out of view near enough to shade the ones in it.
    for (size_t i = 0; i < e.puffs.size() && int(e.order.size()) < MaxParticles; ++i) {
        if (e.drawn[i])
            continue;
        const float *at = e.puffs[i].position;
        const float rel[3] = {at[0] - viewPosition[0], at[1] - viewPosition[1], at[2] - viewPosition[2]};
        if (dot3(rel, rel) < SunReach * SunReach)
            e.order.push_back(int(i));
    }
    const int total = int(e.order.size());
    const int rows = (total + Row - 1) / Row;
    flushTarget();
    if (!createExhaust() || !createExhaustImage())
        return;
    // The puffs' texels, rows of 256.
    for (std::vector<float> &band : e.data)
        band.assign(size_t(rows) * Row * 4, 0.0f);
    for (int k = 0; k < total; ++k) {
        const ExhaustResources::Puff &p = e.puffs[size_t(e.order[size_t(k)])];
        float *centre = e.data[0].data() + k * 4;
        std::copy(p.position, p.position + 3, centre);
        centre[3] = p.radius;
        std::copy(p.matter, p.matter + 4, e.data[1].data() + k * 4);
        float *tint = e.data[2].data() + k * 4;
        std::copy(p.tint, p.tint + 3, tint);
        tint[3] = 1.0f;
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
    // 1. The plume seen from the sun, about the puffs within its reach: the
    // light's axes (across: u, v; along: w, away from the sun), their extent.
    float sun[3];
    std::copy(Game::sunLightDirection, Game::sunLightDirection + 3, sun);
    normalize3(sun);
    const float w[3] = {-sun[0], -sun[1], -sun[2]};
    const float reference[3] = {std::fabs(w[1]) > 0.9f ? 1.0f : 0.0f, std::fabs(w[1]) > 0.9f ? 0.0f : 1.0f, 0.0f};
    float u[3] = {w[1] * reference[2] - w[2] * reference[1], w[2] * reference[0] - w[0] * reference[2],
                  w[0] * reference[1] - w[1] * reference[0]};
    normalize3(u);
    const float v[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
    float low[3] = {1e30f, 1e30f, 1e30f}, high[3] = {-1e30f, -1e30f, -1e30f};
    int casters = 0;
    for (int k = 0; k < total; ++k) {
        const ExhaustResources::Puff &p = e.puffs[size_t(e.order[size_t(k)])];
        const float rel[3] = {p.position[0] - viewPosition[0], p.position[1] - viewPosition[1],
                              p.position[2] - viewPosition[2]};
        if (dot3(rel, rel) > SunReach * SunReach)
            continue;
        const float at[3] = {dot3(p.position, u), dot3(p.position, v), dot3(p.position, w)};
        for (int i = 0; i < 3; ++i) {
            low[i] = std::min(low[i], at[i] - p.radius);
            high[i] = std::max(high[i], at[i] + p.radius);
        }
        ++casters;
    }
    // At night, or with none near: no map.
    const bool sunMapped = casters > 0 && sun[1] > -0.05f;
    const float across = std::max({high[0] - low[0], high[1] - low[1], 4.0f});
    const float deep = std::max(high[2] - low[2], 1.0f);
    float toMap[16] = {};
    for (int i = 0; i < 3; ++i) {
        toMap[4 * i] = u[i] / across;
        toMap[4 * i + 1] = v[i] / across;
        toMap[4 * i + 2] = w[i] / deep;
    }
    toMap[12] = -low[0] / across;
    toMap[13] = -low[1] / across;
    toMap[14] = -low[2] / deep;
    toMap[15] = 1.0f;
    if (sunMapped) {
        SunParameters m = {};
        std::copy(toMap, toMap + 16, m.toMap);
        m.scale[0] = 1.0f / across;
        m.scale[1] = 1.0f / deep;
        m.scale[2] = SunReach;
        // The map's rows as its texture's (fullScreenVertex).
        m.scale[3] = rhi->isYUpInNDC() != rhi->isYUpInFramebuffer() ? -1.0f : 1.0f;
        std::copy(viewPosition, viewPosition + 3, m.camera);
        batch->updateDynamicBuffer(e.sunUniforms, 0, sizeof(m), &m);
    }
    // 2. Their light: the scene's frame values, the frame's textures.
    RhiProgram &program = *e.lightProgram;
    writeFrameUniforms(&program);
    program.setFloat("exhaustCount", float(count));
    program.setFloat("exhaustOvercast", 1.0f - cloudSkyLight());
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
    // 3. The puffs.
    DrawParameters d = {};
    std::copy(forward, forward + 16, d.forward);
    std::copy(skyInverse[TARGET_VIEW], skyInverse[TARGET_VIEW] + 16, d.inverse);
    std::copy(right, right + 3, d.right);
    // Clip y as the view's passes take it (RhiRain.cpp).
    d.right[3] = (rhi->isYUpInFramebuffer() ? 1.0f : -1.0f) * (rhi->isYUpInNDC() != rhi->isYUpInFramebuffer() ? -1.0f : 1.0f);
    std::copy(up, up + 3, d.up);
    std::copy(axis, axis + 3, d.axis);
    std::copy(viewPosition, viewPosition + 3, d.camera);
    std::copy(sun, sun + 3, d.sun);
    d.projection[0] = sceneProjection[2];
    d.projection[1] = sceneProjection[3];
    d.projection[2] = sceneDepthRange[1];
    d.projection[3] = float(e.scale);
    d.shading[0] = 1.1f;
    d.shading[1] = 0.03f;
    std::copy(toMap, toMap + 16, d.toSun);
    d.sunMap[0] = sunMapped ? 1.0f : 0.0f;
    d.sunMap[1] = 1.0f / deep;
    batch->updateDynamicBuffer(e.drawUniforms, 0, sizeof(d), &d);
    CompositeParameters c = {};
    c.projection[0] = sceneProjection[2];
    c.projection[1] = sceneProjection[3];
    c.projection[2] = sceneDepthRange[1];
    c.size[0] = float(e.size.width());
    c.size[1] = float(e.size.height());
    batch->updateDynamicBuffer(e.compositeUniforms, 0, sizeof(c), &c);
    QRhiCommandBuffer *cb = surfaceNow->frame().commandBuffer;
    cb->beginPass(e.sunTarget, QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f), {1.0f, 0}, batch);
    if (sunMapped) {
        cb->setGraphicsPipeline(e.sunPipeline);
        cb->setViewport(QRhiViewport(0, 0, float(SunMapSize), float(SunMapSize)));
        cb->setShaderResources(e.sunBindings);
        cb->draw(6, quint32(total));
    }
    cb->endPass();
    cb->beginPass(e.lightTarget, Qt::black, {1.0f, 0});
    cb->setGraphicsPipeline(e.lightPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(Row), float(Rows)));
    cb->setShaderResources(e.lightBindings);
    cb->draw(3);
    cb->endPass();
    cb->beginPass(e.frontTarget, QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f), {1.0f, 0});
    cb->setGraphicsPipeline(e.frontPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(e.size.width()), float(e.size.height())));
    cb->setShaderResources(e.drawBindings);
    cb->draw(6, quint32(count));
    cb->endPass();
    cb->beginPass(e.imageTarget, QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f), {1.0f, 0});
    cb->setGraphicsPipeline(e.drawPipeline);
    cb->setViewport(QRhiViewport(0, 0, float(e.size.width()), float(e.size.height())));
    cb->setShaderResources(e.drawBindings);
    cb->draw(6, quint32(count));
    cb->endPass();
    // 4. Over the view.
    cb->beginPass(e.compositeTarget, Qt::black, {1.0f, 0});
    cb->setGraphicsPipeline(e.compositePipeline);
    cb->setViewport(QRhiViewport(0, 0, float(view.size.width()), float(view.size.height())));
    cb->setShaderResources(e.compositeBindings);
    cb->draw(3);
    cb->endPass();
}

void RhiRenderer::releaseExhaust() {
    ExhaustResources &e = exhaustResources;
    dropLater(e.sunPipeline);
    dropLater(e.sunBindings);
    dropLater(e.sunTarget);
    dropLater(e.sunPass);
    dropLater(e.sunUniforms);
    dropLater(e.sunMap);
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
    dropLater(e.frontPipeline);
    dropLater(e.frontTarget);
    dropLater(e.frontPass);
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
    return {QByteArray(DrawVertex), QByteArray(DrawFragment), QByteArray(CompositeFragment), QByteArray(SunVertex),
            QByteArray(SunFragment), QByteArray(FrontFragment)};
}
