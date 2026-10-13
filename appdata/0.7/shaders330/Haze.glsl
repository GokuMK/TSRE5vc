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
vec3 hazeThrough(vec3 position) {
    vec3 ray = position - cameraPosition;
    float distance = length(ray) / 1000.0;
    float h0 = hazeParams.x;
    float h1 = h0 + ray.y / 1000.0;
    vec3 sigma = vec3(5.802e-3, 13.558e-3, 33.1e-3) * hazeLayer(h0, h1, 8.0)
            + vec3(4.44e-3 * hazeLayer(h0, h1, 1.2) + hazeParams.y * hazeLayer(h0, h1, 1.5));
    return exp(-sigma * distance);
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
    vec3 through = mix(vec3(1.0), hazeThrough(position), amount);
    vec3 air = pow(max(hazeLight(position) * lightScale, vec3(0.0)), vec3(2.2));
    return pow(pow(max(display, vec3(0.0)), vec3(2.2)) * through + air * (vec3(1.0) - through), vec3(1.0 / 2.2));
}
#endif
