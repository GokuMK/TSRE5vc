/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef DAYLIGHT_H
#define DAYLIGHT_H

// The light of the scene for a sun elevation: the sun's colour (the diffuse
// light, 0 below the horizon), the ambient light and the sky and fog colours,
// in display colour as the shaders take them. A high sun gives the editor's
// usual light and the configured sky and fog colours; a low sun turns warm
// and weak, twilight blue, and night dark. A first step of the environment
// task: no moon, stars or weather.
namespace Daylight {

struct Light {
    float diffuse[4] = {0.7f, 0.7f, 0.7f, 0.7f};
    float ambient[4] = {0.3f, 0.3f, 0.3f, 0.3f};
    float sky[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float fog[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    // Whether the sun casts shadows (it is above the horizon).
    bool sunUp = true;
    // Scale of lamp and glow light (task 21): eyes and cameras adapt to
    // daylight, which outshines lamps; full at night, DayLocalLights by day.
    float localLights = 1.0f;
    // Scale of signal lights' glow (task 26): signals must stay visible by
    // day, so they keep DaySignalLights; full at night.
    float signalLights = 1.0f;
    // Auto exposure (task 29): the light of the sun and the sky in one scale
    // with lamps kept at DayLocalLights: DayLocalLights / localLights, 1 by
    // day, DayLocalLights at night. An exposure of 1 / sceneLight shows the
    // picture the lamps' scale gives.
    float sceneLight = 1.0f;
};

constexpr float DayLocalLights = 0.03f;
constexpr float DaySignalLights = 0.5f;
// The log2 luminance (log average) an ordinary outdoor view has at this sun
// elevation, in the scale of the light's display colours (plus
// log2(sceneLight) for auto exposure). Measured on CMK (task 29): by day a
// well-lit view, open fields and track with the sun behind the camera, clear
// sky; from sunset the lake (fair cumulus): the bright sky holding it up
// around sunset, then the night.
float viewLuminanceLog2(double elevationDegrees);
// The eye's adaptation for auto exposure (task 29): for ordinary outdoor
// views from night to a high sun, their luminance (log2, scene light,
// ascending) and the exposure (log2) that shows them as the lamps' scale
// does (1 / sceneLight). A view takes the exposure of the ordinary view as
// bright as it is; no view gets more than the night's. Returns the points
// written, up to capacity.
int adaptationCurve(float *luminanceLog2, float *exposureLog2, int capacity);
// Auto exposure over the lamps' scale's picture, in stops (user,
// 2026-10-13: with AgX, days and nights were too dark): DayExposureBias with
// the sun 20 degrees up, EveningExposureBias at sunset (with the lamps'
// fade), NightExposureBias with the sun 12 degrees down (through twilight).
constexpr float DayExposureBias = 0.25f;
constexpr float EveningExposureBias = 1.5f;
constexpr float NightExposureBias = 1.75f;
float exposureBias(double elevationDegrees);

// daySky and dayFog: the colours of a high sun (the sky and fog settings).
Light forElevation(double elevationDegrees, const float *daySky, const float *dayFog);

// Shadows from a sun this low would stretch across the maps; their light
// keeps at least this elevation (degrees).
constexpr double MinShadowElevation = 6.0;

// The moon's light (task 31), in the display units of Light: a directional
// light and what the moonlit sky adds to the ambient.
struct Moonlight {
    float diffuse[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float ambient[3] = {0.0f, 0.0f, 0.0f};
    // Of a full moon high up at night, 0 to 1.
    float strength = 0.0f;
};
// The sun's light high up (display units).
constexpr float SunLight = 0.7f;
// A full moon high up at night, as SunLight by day.
constexpr float FullMoonLight = 0.15f;
// With the phase (a half moon about a fifth of a full one), the moon's
// height (as the sun's) and the night: from the sun 1 degree under the
// horizon, where its own light is gone, to 8 degrees under it.
Moonlight moonlight(double sunElevationDegrees, double moonElevationDegrees, double illuminatedFraction);

}

#endif
