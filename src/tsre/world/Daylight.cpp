/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "Daylight.h"
#include <algorithm>
#include <cmath>

namespace Daylight {

namespace {

float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float mix(float a, float b, float t) {
    return a + (b - a) * t;
}

// Colours of the hours without a high sun.
const float SunsetSun[3] = {1.0f, 0.62f, 0.32f};
const float SunsetSky[3] = {0.93f, 0.62f, 0.45f};
const float TwilightSky[3] = {0.18f, 0.24f, 0.42f};
const float NightSky[3] = {0.015f, 0.022f, 0.05f};
const float NightAmbient[3] = {0.035f, 0.045f, 0.075f};

}

Light forElevation(double elevationDegrees, const float *daySky, const float *dayFog) {
    Light light;
    const float h = float(elevationDegrees);
    // The sun: full above 20 degrees, warm and weaker towards the horizon,
    // gone 1 degree under it (the disc and refraction).
    const float strength = smoothstep(-1.0f, 20.0f, h);
    const float white = smoothstep(2.0f, 25.0f, h);
    for (int c = 0; c < 3; ++c)
        light.diffuse[c] = 0.7f * strength * mix(SunsetSun[c], 1.0f, white);
    light.diffuse[3] = 0.7f * strength;
    // Ambient: the day's light down to civil twilight, then night.
    const float day = smoothstep(-12.0f, 8.0f, h);
    for (int c = 0; c < 3; ++c)
        light.ambient[c] = mix(NightAmbient[c], 0.3f, day);
    light.ambient[3] = 0.3f;
    // Sky and fog: night, then twilight blue, a warm band around sunset and
    // the configured colours of the day.
    const float twilight = smoothstep(-18.0f, -6.0f, h);
    const float daytime = smoothstep(-4.0f, 12.0f, h);
    const float sunset = std::exp(-std::pow((h - 1.0f) / 5.0f, 2.0f)) * 0.55f;
    for (int c = 0; c < 3; ++c) {
        const float dusk = mix(NightSky[c], TwilightSky[c], twilight);
        light.sky[c] = mix(mix(dusk, daySky[c], daytime), SunsetSky[c], sunset);
        light.fog[c] = mix(mix(dusk, dayFog[c], daytime), SunsetSky[c], sunset);
    }
    light.sky[3] = daySky[3];
    light.fog[3] = dayFog[3];
    light.sunUp = h > -1.0f;
    return light;
}

}
