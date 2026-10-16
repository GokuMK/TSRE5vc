/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WEATHERFOG_H
#define WEATHERFOG_H

#include <tsre/environment/Weather.h>

#include <functional>

// The fog of a weather at a place and time (task renderer 33), before the
// terrain decides where it lies: radiation fog from a clear, calm, damp
// night, mist in rain, or what the weather's author set.
namespace WeatherFog {

struct State {
    float amount = 0.0f;          // 0-1
    float visibility = 40000.0f;  // m, in the fog at the valley floor
    float depth = 0.0f;           // m, its top above the valley floor
    float base = 0.0f;            // m above the floor it has lifted to (0: on the ground)
    float patchiness = 0.0f;      // 0-1
    float pooling = 1.0f;         // 1 pooling in valleys, 0 lying evenly on all ground (WeatherFogSetting)
};

// The sun's elevation (degrees) at a weather time (hours).
using SunElevation = std::function<double(double hours)>;

// Radiation fog: the night since sunset cools the air towards its dew point,
// faster under a clear sky and in light wind; strong wind mixes the fog
// away. After sunrise the sun's warmth burns it off, slower with a low sun.
State radiation(const Weather &weather, double hours, double east, double north,
                const SunElevation &sun);

// The fog at a time: each keyframe's setting (auto: radiation fog or mist in
// rain, whichever is more; off; set), blended between keyframes.
State at(const Weather &weather, double hours, double east, double north, const SunElevation &sun);

}

#endif
