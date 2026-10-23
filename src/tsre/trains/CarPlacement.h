/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef CARPLACEMENT_H
#define CARPLACEMENT_H

#include <vector>

// Where a car is drawn on the track (Eng; task shapes 12). Places are as
// the track gives them (getDrawPositionOnTrNode: x, y, z, angle, slope,
// tile x, tile z), its front and rear as its consist lists them.
namespace CarPlacement {

// The car between two places, its roll (superelevation) and flip, applied
// to `m`: its shape's space (its root's mirror applied) to the world's,
// relative to tile playerT.
void placeCar(float *m, const float *playerT, const float *front, const float *rear, float roll, bool flipped);

// A bogie: its pivot's shape z and its rest transform (relative to its
// parent, the body).
struct Bogie {
    float z = 0.0f;
    float rest[16];
};

// On its carriers instead of its ends: the track between its ends taken as
// a cubic from their places p0, p1 (x, y, z in one tile) and their ways t0,
// t1 (unit, toward its rear end: at the rear onward past it); sampled at
// the carriers (shape z, + toward its shape's front; at least two) and the
// body's line fitted to them (Open Rails' least squares; two: the chord
// between them). Out: the places of the line's ends (as front and rear,
// x, y, z in p0's tile; their other elements copied from `front`), and each
// bogie's transform relative to the body (16 floats each), turned to the
// track's way at its pivot as Open Rails turns bogies (its rotation
// replaced, its translation kept), NaN as the last element where that is
// its rest. False when the carriers do not fix a line.
bool onCarriers(const float *front, const float *p0, const float *t0, const float *p1, const float *t1, float roll,
                bool flipped, const std::vector<float> &carriers, const std::vector<Bogie> &bogies,
                float *placedFront, float *placedRear, std::vector<float> &bogieTurns);

}

#endif
