/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef NIGHTTEXTURES_H
#define NIGHTTEXTURES_H

#include <QString>

// Night textures of MSTS shapes (task 35). A shape whose .sd file has the
// night bit in ESD_Alternative_Texture has, for each of its textures, a night
// texture of the same name in a Night folder. MSTS and Open Rails draw it
// instead of the day texture after sunset, fully bright; TSRE keeps the day
// texture lit and adds the night texture's light as the daylight fades.
namespace NightTextures {

// The night bit of ESD_Alternative_Texture.
constexpr int AlternativeFlag = 0x100;

// The night texture of a texture (the folder it is loaded from, its file
// name) as Open Rails finds it: in a Night folder next to it, else in one
// a level up (TEXTURES/NIGHT for a seasonal TEXTURES/SPRING texture), the
// folder's name in any case; an .ace name also finds the .dds, as the day
// textures do. Empty when there is none. Folders are looked up once.
QString find(const QString &textureFolder, const QString &name);

// This frame's night textures, set by the view before it draws.
struct Frame {
    // Loaded: from the sun some degrees above the horizon until it is as high
    // again in the morning. Shapes release theirs while not.
    bool wanted = false;
    // How strongly they shine (linear), as the lamps: with auto exposure the
    // lamps' constant scale (the exposure shows them by night and hides them
    // by day), otherwise the lamps' fade (full at night). 0 without them.
    float scale = 0.0f;
    // Under cover without auto exposure they shine at full (as the lamps).
    float enclosedScale = 0.0f;
};
void setFrame(const Frame &frame);
const Frame &frame();

// Whether night textures are wanted with the sun this high (degrees), given
// whether they were: loaded below LoadElevation, released above
// ReleaseElevation.
constexpr double LoadElevation = 20.0;
constexpr double ReleaseElevation = 23.0;
bool wantedForSun(double elevationDegrees, bool wanted);

}

#endif
