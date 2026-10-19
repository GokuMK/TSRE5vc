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

// The distance levels (levels of detail) of MSTS shapes: which one an object
// draws, as Open Rails picks it: the most detailed level whose distance
// (dlevel_selection), with the shape's radius, reaches the camera's
// horizontal distance; past them all the last one.
//
// TSRE_SHAPE_LOD (read once) chooses: unset or 1 picks the levels, the last
// one drawn as far as objects are (Open Rails' default, its viewing
// extension); 0 always draws the most detailed level (TSRE before); 2 also
// hides an object past its last level's distance (MSTS, Open Rails without
// the extension: small objects vanish from a high editor camera).
namespace ShapeLod {

enum class Mode { Off, Levels, Cull };
Mode mode();

// The level to draw at this horizontal distance (metres) from the camera, -1
// for none (past the last level's distance in Cull mode).
int level(const ComplexShape &shape, float distance);

// The level for this object, set on its shape state; false when it draws
// none. Nothing is set while the mode is Off.
bool apply(ComplexShape &shape, unsigned int stateId, float distance);

}

#endif
