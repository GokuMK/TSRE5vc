#ifdef TSRE_RHI
// The air between the camera and a surface (QRhi, a physical sky): it dims
// the surface's light, blue most (Rayleigh), and adds its own light, the
// sky's colour along the horizon in that direction. Over a path shorter
// than the sky's, a dark surface stays darker than the sky behind it: far
// mountains turn blue, farther ridges lighter. The weather's haze
// (visibility) and the fog density setting add extinction alike in every
// colour, turning them grey.
uniform vec3 cameraPosition;
// The camera's altitude (km), the haze's extinction at sea level (1/km),
// on (1; 0 the classic fog), 0.
uniform vec4 hazeParams;
// The sun's direction across the ground (x, z), normalized.
uniform vec4 hazeSun;
// The horizon's colour (display), from towards the sun (0) to away (7).
uniform vec4 hazeRing[8];
// 0 for the sky band: no haze (as the classic fog's lod).
uniform float lod;

bool hazeOn() {
    return hazeParams.z > 0.5 && lod > 0.0;
}

// The mean density over a straight path between two altitudes (km) of a
// layer falling off with the scale height (km).
float hazeLayer(float h0, float h1, float scale) {
    float dh = h1 - h0;
    if (abs(dh) < 1e-3)
        return exp(-0.5 * (h0 + h1) / scale);
    return (exp(-h0 / scale) - exp(-h1 / scale)) * scale / dh;
}

// What the air lets through of a surface's light, per colour; the same air
// as the clouds' (Rayleigh, Mie and the haze, RhiClouds.cpp airTo).
vec3 airThrough(vec3 position) {
    vec3 ray = position - cameraPosition;
    float distance = length(ray) / 1000.0;
    float h0 = hazeParams.x;
    float h1 = h0 + ray.y / 1000.0;
    vec3 sigma = vec3(5.802e-3, 13.558e-3, 33.1e-3) * hazeLayer(h0, h1, 8.0)
            + vec3(4.44e-3 * hazeLayer(h0, h1, 1.2) + hazeParams.y * hazeLayer(h0, h1, 1.5));
    return exp(-sigma * distance);
}

// Fog lying on the ground (task 33, RhiFog.cpp): the main view's volume of
// cells, x and y by the view's directions, slices by the distance (evenly
// in its logarithm); each holds the fog's light added up to its far end
// (linear) and the share let through.
uniform sampler3D fogVolume;
uniform mat4 fogForward;
// On, the nearest slice's distance (m), log(far / near), slices.
uniform vec4 fogParams;
// The volume's light to the scene's (it is kept scaled into the half
// floats' even range).
uniform float fogLightScale;
// High quality: on (1), the fog top's soft edge (m), 0, 0. A surface seen
// from above the fog takes the fog over its own place per pixel (fogAt).
uniform vec4 fogFilm;

vec3 hazeLight(vec3 position);

// The volume's sums along a ray to uv at a distance: the light added (in its
// scale) and the share let through.
vec4 fogVolumeAt(vec2 uv, float reach) {
    // Slice i holds the sums up to its far end (continuous slice i + 1):
    // the sums at the ends of the slice the point lies in, between them the
    // share let through falling exponentially, the light added as it falls
    // (blended evenly, in a thick fog's slice of one or two optical depths
    // it was out by up to a third in the slice's middle).
    float slice = clamp(log(max(reach, fogParams.y) / fogParams.y) / fogParams.z * fogParams.w,
                        0.0, fogParams.w);
    float first = floor(slice - 1.0);
    float along = slice - 1.0 - first;
    vec4 a = first < 0.0 ? vec4(0.0, 0.0, 0.0, 1.0)
                         : textureLod(fogVolume, vec3(uv, (first + 0.5) / fogParams.w), 0.0);
    vec4 b = textureLod(fogVolume, vec3(uv, min(first + 1.5, fogParams.w - 0.5) / fogParams.w), 0.0);
    float throughA = clamp(a.a, 1e-6, 1.0), throughB = clamp(b.a, 0.0, 1.0);
    float through = throughA * pow(max(throughB / throughA, 1e-6), along);
    float share = throughA - throughB > 1e-5 ? (throughA - through) / (throughA - throughB) : along;
    return vec4(a.rgb + (b.rgb - a.rgb) * share, through);
}

// The fog's extinction (1/m) at a height (m above sea level) over a place of
// the floor map, its patches' noise n there: as RhiFog.cpp's injection.
float fogBandDensity(vec4 cell, float n, float height) {
    float top = max(cell.x + fogShape.y * cell.y, cell.z + fogShape.y * 0.5 * cell.w)
            + (n - 0.5) * fogShape.y * (0.15 + 0.45 * fogPatchPlace.z);
    float inside = 1.0 - smoothstep(top - fogFilm.y, top + fogFilm.y, height);
    if (fogShape.z > 0.0)
        inside *= smoothstep(cell.x + fogShape.z - fogFilm.y, cell.x + fogShape.z + fogFilm.y, height);
    float holes = mix(1.0, smoothstep(0.3, 0.6, n), fogPatchPlace.z);
    return fogShape.x * (1.0 + 0.5 * cell.w) * holes * inside;
}

// A fragment asks for its fog two or three times (its colour, its ambient
// share, its glow): the last answer kept.
vec3 fogAtPosition;
vec4 fogAtResult;
bool fogAtKept = false;

vec4 fogAtCompute(vec3 position);

vec4 fogAt(vec3 position) {
    if (!fogAtKept || fogAtPosition != position) {
        fogAtResult = fogAtCompute(position);
        fogAtPosition = position;
        fogAtKept = true;
    }
    return fogAtResult;
}

vec4 fogAtCompute(vec3 position) {
    if (fogParams.x < 0.5)
        return vec4(0.0, 0.0, 0.0, 1.0);
    vec3 ray = position - cameraPosition;
    vec4 clip = fogForward * vec4(ray, 0.0);
    if (clip.w <= 0.0)
        return vec4(0.0, 0.0, 0.0, 1.0);
    vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
    float reach = length(ray);
    vec4 f = fogVolumeAt(uv, reach);
    // High: a surface seen from above the fog takes the fog its ray crosses
    // from the fog's top over the surface's place down to it per pixel, by
    // height from the floor map there, before it the volume's. The volume's
    // slices (50 m deep a few hundred metres off) cannot follow a thin fog
    // lying on the ground seen from above: each pixel's share of it depends
    // on where its ray meets the ground, and the slices showed as rings about
    // the camera (the user, 2026-10-15). Its light is the fog's in front of
    // the surface in the volume, on average: smooth where the amounts step.
    // Near the ground's level (looking along the fog) the volume's alone.
    float down = -ray.y / max(reach, 1e-3);
    vec2 m = (position.xz - fogPlace.xy) / fogPlace.z;
    if (fogFilm.x > 0.5 && down > 0.03 && all(greaterThan(m, vec2(0.0))) && all(lessThan(m, vec2(1.0)))) {
        vec4 cell = textureLod(fogFloorMap, m, 0.0);
        float n = fogPatchesAt(position);
        float bandTop = max(cell.x + fogShape.y * cell.y, cell.z + fogShape.y * 0.5 * cell.w)
                + (n - 0.5) * fogShape.y * (0.15 + 0.45 * fogPatchPlace.z) + fogFilm.y;
        float height = position.y - fogPlace.w;
        float weight = smoothstep(0.03, 0.08, down)
                * smoothstep(bandTop + 5.0, bandTop + 30.0, cameraPosition.y - fogPlace.w);
        if (weight > 0.0 && height < bandTop) {
            // In four steps from a slice before where the ray meets the
            // fog's top over the surface (the volume's column there may hold
            // the fog already: its slices showed through, and in the history,
            // following the drift, they lagged), each over its own place of
            // the floor map (a shallow ray crosses a few hundred metres of
            // ground on its way down through a thin fog), the patches as at
            // the surface (they change over 90 m and more).
            float enter = max(reach - (bandTop - height) / down, 0.0) / exp(fogParams.z / fogParams.w);
            vec4 before = fogVolumeAt(uv, enter);
            vec3 along = ray / max(reach, 1e-3);
            float depth = 0.0;
            for (int k = 0; k < 4; ++k) {
                vec3 q = cameraPosition + along * mix(enter, reach, (float(k) + 0.5) / 4.0);
                vec2 mq = clamp((q.xz - fogPlace.xy) / fogPlace.z, vec2(0.0), vec2(1.0));
                depth += fogBandDensity(textureLod(fogFloorMap, mq, 0.0), n, q.y - fogPlace.w);
            }
            depth *= (reach - enter) / 4.0;
            float fogged = 1.0 - f.a;
            vec3 light = fogged > 1e-3 ? f.rgb / fogged
                                       : pow(max(hazeLight(position), vec3(0.0)), vec3(2.2)) / max(fogLightScale, 1e-12);
            float band = exp(-depth);
            f = mix(f, vec4(before.rgb + light * before.a * (1.0 - band), before.a * band), weight);
        }
    }
    return vec4(max(f.rgb * fogLightScale, vec3(0.0)), clamp(f.a, 0.0, 1.0));
}

// What the air and the fog let through of a surface's light (its glow too).
vec3 hazeThrough(vec3 position) {
    return airThrough(position) * fogAt(position).a;
}

// The air's own light in a direction: the horizon's colour there (display).
vec3 hazeLight(vec3 position) {
    vec2 across = (position - cameraPosition).xz;
    float c = dot(across, across) > 1e-6 ? dot(normalize(across), hazeSun.xy) : 1.0;
    float t = acos(clamp(c, -1.0, 1.0)) * (7.0 / 3.14159265);
    int i = min(int(t), 6);
    return mix(hazeRing[i].rgb, hazeRing[i + 1].rgb, t - float(i));
}

// A surface's display colour seen through the air, its share of the haze
// (water over a bed already hazed takes less), the air's light scaled (under
// cover it is lit as the ambient is). Mixed in linear light, as the final
// pass reads the view.
vec3 applyHaze(vec3 display, vec3 position, float amount, float lightScale) {
    // The fog lies low, in front of what the air reaches: the fog first, then
    // the air over both (far fog banks turn blue with the distance).
    vec4 fog = fogAt(position);
    vec3 surface = pow(max(display, vec3(0.0)), vec3(2.2)) * mix(1.0, fog.a, amount) + fog.rgb * (amount * lightScale);
    vec3 through = mix(vec3(1.0), airThrough(position), amount);
    vec3 air = pow(max(hazeLight(position) * lightScale, vec3(0.0)), vec3(2.2));
    return pow(surface * through + air * (vec3(1.0) - through), vec3(1.0 / 2.2));
}
#endif
