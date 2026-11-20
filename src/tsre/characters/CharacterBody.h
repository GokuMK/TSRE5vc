/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef CHARACTERS_CHARACTERBODY_H
#define CHARACTERS_CHARACTERBODY_H

#include <tsre/characters/CollisionWorld.h>
#include <functional>
#include <vector>

namespace Characters {

// A walking character's body (task characters 02): kinematic, a capsule
// floating a step over its feet, the feet on the floor below it.
// - On the ground: the floor under it found from a step above its feet to
//   a step below (the middle, else four points round it half its radius
//   off: the highest); it stands on it. Up a step or a slope: as long as
//   the floor is at most a step higher and walkable (no steeper than
//   maxSlope), or no higher than its feet; else it stays where it was.
//   Down a step: it keeps to the floor; more: it falls.
// - In the air: gravity, landing on the first floor its feet reach.
// - Its body (the capsule from a step over its feet to its height) is
//   pushed out of what it touches: walls stop it, it slides along them.
// - Where it may not be (the reach: `allowed`), it stays where it was.
// Moves in steps of at most a tenth of a metre.
class CharacterBody {
public:
    struct Settings {
        float radius = 0.3f;
        float height = 1.8f;
        float step = 0.35f;
        // Degrees from level.
        float maxSlope = 45.0f;
        float gravity = 9.81f;
        // Its speed up a jump (m/s): about half a metre high.
        float jump = 3.2f;
    };
    void setSettings(const Settings &value) { set = value; }
    const Settings &settings() const { return set; }
    // Its feet, in the world's frame; standing (on the ground) until it
    // moves.
    void setPosition(const float *feet);
    const float *position() const { return feet; }
    // Moved by `by` (x, y, z) as it is (a new frame: another tile), on the
    // ground or in the air as it was.
    void shift(const float *by) {
        for (int i = 0; i < 3; ++i)
            feet[i] += by[i];
    }
    bool onGround() const { return grounded; }
    float verticalSpeed() const { return vertical; }
    // The floor it stands on (the last found).
    const CollisionWorld::Floor &floor() const { return under; }

    // Moves at a velocity over the ground (x, z m/s) for `seconds`, jumping
    // when asked and on the ground; `allowed` says where it may be (none:
    // anywhere). Returns how far it moved over the ground.
    float move(const CollisionWorld &world, float vx, float vz, float seconds, bool jump = false,
               const std::function<bool(float x, float z)> &allowed = {});

private:
    Settings set;
    float feet[3] = {0.0f, 0.0f, 0.0f};
    bool grounded = true;
    float vertical = 0.0f;
    CollisionWorld::Floor under;
    std::vector<CollisionWorld::Contact> found;
    bool floorBelow(const CollisionWorld &world, float x, float z, float above, float below,
                    CollisionWorld::Floor &out) const;
    void pushOut(const CollisionWorld &world);
};

}

#endif
