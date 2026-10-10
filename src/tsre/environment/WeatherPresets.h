/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef WEATHERPRESETS_H
#define WEATHERPRESETS_H

#include <tsre/environment/Weather.h>

// Named weathers in TSRE's code: a weather block can start from one and
// override its values (task environment 01).
namespace WeatherPresets {
QStringList names();
bool exists(const QString &name);
// The preset's keyframes; empty for an unknown name.
QVector<WeatherKeyframe> keyframes(const QString &name);
// The preset's values at a time (hours), keyframes blended: the base a
// weather block's keyframe at that time overrides.
WeatherKeyframe at(const QString &name, double hours);
// A weather of only the preset.
Weather weather(const QString &name, quint32 seed = 1);
}

#endif
