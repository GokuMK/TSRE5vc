/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef CHARACTERS_GANGWAYS_H
#define CHARACTERS_GANGWAYS_H

#include <vector>

namespace ShapeSeats {
struct EndPassage;
}

namespace Characters {

// Walking between cars (task characters 05; the user's design, 2026-11-23).
// A car's EndPassage (a door in its end wall, ShapeSeats) is open while the
// car is coupled to one with an EndPassage at the facing end: their floors
// at most ShapeSeats::OneStep apart, each door's wall at most MaxReach from
// its car's coupling end (half its full length: where the train begins the
// next car). Each open end has its own floor plate, fixed in its car's frame
// as a real gangway's: from its door's wall to its coupling end and Overlap
// past it, so the two cars' plates overlap however they move (curves,
// slack, cant), with walls along its sides (nobody steps off into the gap);
// for collisions only, never drawn. The character goes on to the next car
// once its feet are Handover past the coupling end.
namespace Gangways {

constexpr float MaxReach = 1.5f;
// A car shorter than this (its full length) is walked over: an articulated
// unit's shared bogie made a car of its own (the VD Bhp's 0.2 m joint); the
// gangway joins the cars either side of it.
constexpr float JointLength = 1.0f;
constexpr float Overlap = 0.5f;
constexpr float Handover = 0.15f;
// The walls' height over the plate's floor, and the door's opening (as a
// Passage's).
constexpr float WallHeight = 2.1f;
// The floor and the opening reach this far into the car past its wall; the
// opening is this much wider than the door either side (a body's radius to
// spare, as a Passage's).
constexpr float WallDepth = 0.35f;
constexpr float Aside = 0.3f;

// An open end's plate in its car's frame (the car's body as drawn: +z its
// shape's front).
struct Plate {
    // 1 the shape's front (+z), -1 its rear; 0 closed.
    int end = 0;
    // Its middle across, its floor.
    float x = 0.0f;
    float y = 0.0f;
    // Its door's wall, and its far end: its car's coupling end and Overlap
    // past it (z).
    float wall = 0.0f;
    float far = 0.0f;
    // The narrower door's width.
    float width = 0.0f;
    bool open() const { return end != 0; }
    // Over it: within its width across, along it from `inside` metres into
    // the car past its wall to its far end.
    bool holds(float x, float z, float inside = 0.0f) const;
};

// Where a moving car keeps its character's feet: within its footprint
// (half its width and length, `margin` in from its sides), or over an open
// plate from WallDepth inside its wall (the footprint ends short of an end
// wall by the body's radius).
bool within(float halfWidth, float halfLength, const Plate &front, const Plate &rear, float x, float z,
            float margin = 0.0f);
// A door's wall within MaxReach of its car's coupling end (half its full
// length), not past it.
bool reaches(const ShapeSeats::EndPassage &door, float couplingEnd);
// Two facing doors (each of its own car, with its car's coupling end) open
// a gangway: both reach, their floors at most OneStep apart. Its width: the
// narrower door's.
bool matches(const ShapeSeats::EndPassage &a, float couplingA, const ShapeSeats::EndPassage &b, float couplingB,
             float *width = nullptr);
// The plate of a car's open end, `width` wide on its door's middle.
Plate plateOf(const ShapeSeats::EndPassage &door, float couplingEnd, float width);
// Its triangles (nine floats each): its floor, from WallDepth inside its
// door's wall to its far end, and its two walls, from the wall on.
std::vector<float> triangles(const Plate &plate);
// Its door's opening (min x, y, z, then max): the door's width and Aside
// either side, from just over the floor to WallHeight, from WallDepth inside
// its wall to the plate's far end (its own car's bellows and a gangway's
// cover there are walked through: the plate's walls keep the way).
void opening(const ShapeSeats::EndPassage &door, const Plate &plate, float *box);
// The end of a car another car is at: the other's middle in the car's
// frame (`inverse`: the world's to the car's; `otherMatrix`: the other's to
// the world, column major). 1 its front, -1 its rear.
int endTowards(const float *inverse, const float *otherMatrix);
// Feet at z (in the plate's car) past its coupling end by Handover on that
// plate's end: in the next car.
bool handedOver(const Plate &plate, float couplingEnd, float z);
// The car next to `car` in a train whose cars are `lengths` long (full
// lengths, in order) on one side (`step`: -1 the one before, 1 the one
// after), past any shorter than JointLength; -1 none.
int nextCar(const std::vector<float> &lengths, int car, int step);
// Back inside from an end closing (uncoupled, the train made again): feet
// in its door's way (over its plate, or within WallDepth inside its wall)
// go StepIn in from its wall on the door's middle (out: x, z); false: not
// in the way.
constexpr float StepIn = 0.5f;
bool insidePlace(const Plate &plate, const float *feet, float *out);

}

}

#endif
