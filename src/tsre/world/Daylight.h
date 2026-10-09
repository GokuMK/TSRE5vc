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
};

constexpr float DayLocalLights = 0.03f;
constexpr float DaySignalLights = 0.5f;

// daySky and dayFog: the colours of a high sun (the sky and fog settings).
Light forElevation(double elevationDegrees, const float *daySky, const float *dayFog);

// Shadows from a sun this low would stretch across the maps; their light
// keeps at least this elevation (degrees).
constexpr double MinShadowElevation = 6.0;

}

#endif
