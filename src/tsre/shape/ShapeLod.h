/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SHAPELOD_H
#define SHAPELOD_H

class ComplexShape;

// The distance levels (levels of detail) of shapes: which one an object
// draws.
//
// MSTS shapes as Open Rails picks them: the most detailed level whose
// distance (dlevel_selection), with the shape's radius, reaches the camera's
// horizontal distance; past them all the last one.
//
// glTF shapes (MSFT_lod) by screen coverage: the object's relative height on
// the screen, its largest extent over the view's height at its distance
// (Unity's measure, which Open Rails follows); the most detailed level whose
// coverage (MSFT_screencoverage) it still reaches, past them all the last
// one.
//
// TSRE_SHAPE_LOD (read once) chooses: unset or 1 picks the levels, the last
// one drawn as far as objects are (Open Rails' default, its viewing
// extension); 0 always draws the most detailed level (TSRE before); 2 also
// hides an object past its last level's distance or coverage (MSTS, Open
// Rails without the extension: small objects vanish from a high editor
// camera).
namespace ShapeLod {

enum class Mode { Off, Levels, Cull };
Mode mode();

// The level of detail bias (core.rendering.lodBias, -100 to 100), as Open
// Rails' LOD bias: distances scaled and coverages divided by 1 + bias / 100,
// so above 0 the detailed levels reach farther, below 0 less far (weaker
// hardware); 100 always draws the most detailed level, -100 the least
// detailed (tree impostors). Read once and when the profile changes.
int lodBias();
// TSRE_SHAPE_LEVEL (read once, -1 unset): every shape with levels draws this
// one (its last when it has fewer), to compare the levels in captures.
int forcedLevel();

// Where an object is from the camera: the horizontal distance to its origin
// and the height of its origin above the camera (metres); for impostors, the
// direction from it to the camera and its transform (shape to world).
struct Place {
    float distance = 0.0f;
    float height = 0.0f;
    float toCamera[3] = {0.0f, 0.0f, 0.0f};
    const float *transform = nullptr;
};

// An object's screen coverage at this place: its largest extent over the
// height the view spans at its distance (vertical field of view, degrees).
float coverage(const ComplexShape &shape, const Place &place, float fieldOfView);

// The level to draw at this place, -1 for none (past the last level in Cull
// mode).
int level(const ComplexShape &shape, const Place &place);

// The level for this object, set on its shape state with the direction to
// the camera in the shape's space; false when it draws none. No level is set
// while the mode is Off.
bool apply(ComplexShape &shape, unsigned int stateId, const Place &place);

}

#endif
