/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef CHARACTERS_CARCOLLISION_H
#define CHARACTERS_CARCOLLISION_H

#include <tsre/characters/CollisionWorld.h>
#include <tsre/characters/Gangways.h>

class Eng;

namespace ShapeSeats {
struct EndPassage;
}

namespace Characters {

// A car's collision in its own frame (task characters 03: a character in
// a car moves in it, the train carrying it): its body's space as drawn
// (+z its shape's front, +x its left, y up; S files mirrored as drawn).
// Its meshes: its shape, its freight animations' (MaSzyna's low-poly
// interior and attachments), its 3D cabs' (in the car's space; MaSzyna's
// over the body, Open Rails' in place of its own cab); read at once (the
// cache's, by file). An assumed floor within its footprint (its
// definition's width and length, else its shape's bounds): 1.1 m over the
// rail, so a car without floor triangles holds the character; only where
// none of its meshes is a floor (a double-deck car's lower deck is below
// it; the user's Bhp, 2026-11-21).
class CarCollision {
public:
    static constexpr float AssumedFloor = 1.1f;

    // The car's (none: cleared).
    void build(const Eng *eng);
    void clear();
    const Eng *car() const { return of; }
    const CollisionWorld &world() const { return collisions; }
    CollisionWorld &world() { return collisions; }
    // Within its footprint (across, along), `margin` metres in from its
    // sides (negative: out), or over one of its open gangways' plates
    // (Gangways::within).
    bool within(float x, float z, float margin = 0.0f) const;
    float halfWidth() const { return half[0]; }
    float halfLength() const { return half[1]; }
    float height() const { return tall; }
    int meshes() const { return collisions.count(); }
    // Its .sd Passages (closed partition doors drawn on a wall, each on an
    // aisle along the car) as openings the character walks through: the
    // doorway's width and a body's radius either side, 2.1 m high, 0.35 m
    // either side of the wall; read once its seats are (StationPeople's,
    // on a worker: asked each tick until then). How many.
    void readPassages();
    int passages() const { return passageCount; }
    // Its ends' gangways (task characters 05, Gangways): one opened at its
    // EndPassage's end with the narrower door's width (its plate, and its
    // door's opening), closed (both taken away). An end: 1 its front, -1
    // its rear.
    // `otherX`: the facing door's middle across in this car's frame
    // (Gangways::facingX).
    void openEnd(const ShapeSeats::EndPassage &door, float width, float otherX);
    void closeEnd(int end);
    const Gangways::Plate &plate(int end) const { return ends[end > 0 ? 0 : 1].plate; }
    // Its coupling end along it (m): half its full length, as the train
    // spaces its cars (Consist::initPos).
    float couplingEnd() const { return coupling; }
    static float couplingEndOf(const Eng &eng);

private:
    CollisionWorld collisions;
    const Eng *of = nullptr;
    float half[2] = {0.0f, 0.0f};
    float tall = 0.0f;
    float floor = 0.0f;
    bool passagesRead = false;
    int passageCount = 0;
    float coupling = 0.0f;
    struct End {
        Gangways::Plate plate;
        float doorX = 0.0f;
        float doorWidth = 0.0f;
        int mesh = -1;
        int opening = -1;
    };
    End ends[2];
};

}

#endif
