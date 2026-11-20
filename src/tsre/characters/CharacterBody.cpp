/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include "CharacterBody.h"

#include <algorithm>
#include <cmath>

namespace Characters {

namespace {
// Each step of a move at most this far over the ground (m) and this long (s).
constexpr float MostStep = 0.1f;
constexpr float MostTime = 1.0f / 60.0f;
}

void CharacterBody::setPosition(const float *at) {
    std::copy(at, at + 3, feet);
    grounded = true;
    vertical = 0.0f;
}

bool CharacterBody::floorBelow(const CollisionWorld &world, float x, float z, float above, float below,
                               CollisionWorld::Floor &out) const {
    if (world.floorAt(x, z, above, below, out))
        return true;
    // Half its radius round the middle: the highest (an edge, a gap).
    const float r = 0.5f * set.radius;
    const float around[4][2] = {{r, 0.0f}, {-r, 0.0f}, {0.0f, r}, {0.0f, -r}};
    bool any = false;
    for (const auto &d : around) {
        CollisionWorld::Floor f;
        if (world.floorAt(x + d[0], z + d[1], above, below, f) && (!any || f.height > out.height)) {
            any = true;
            out = f;
        }
    }
    return any;
}

void CharacterBody::pushOut(const CollisionWorld &world) {
    for (int iteration = 0; iteration < 4; ++iteration) {
        const float bottom = feet[1] + set.step + set.radius;
        const float top = std::max(bottom, feet[1] + set.height - set.radius);
        const float a[3] = {feet[0], bottom, feet[2]}, b[3] = {feet[0], top, feet[2]};
        found.clear();
        world.contacts(a, b, set.radius, found);
        // The deepest first; again until none.
        const CollisionWorld::Contact *deepest = nullptr;
        float normal[3] = {0.0f, 0.0f, 0.0f};
        for (const CollisionWorld::Contact &c : found) {
            float n[3] = {c.contact.normal[0], c.contact.normal[1], c.contact.normal[2]};
            if (grounded) {
                // On the ground the floor holds it up: walls push it aside
                // only.
                n[1] = 0.0f;
                const float length = std::hypot(n[0], n[2]);
                if (length < 0.2f)
                    continue;
                n[0] /= length;
                n[2] /= length;
            }
            if (deepest == nullptr || c.contact.distance < deepest->contact.distance) {
                deepest = &c;
                std::copy(n, n + 3, normal);
            }
        }
        if (deepest == nullptr)
            return;
        const float depth = set.radius - deepest->contact.distance + 1e-3f;
        for (int k = 0; k < 3; ++k)
            feet[k] += normal[k] * depth;
        // A ceiling stops a jump.
        if (normal[1] < -0.5f && vertical > 0.0f)
            vertical = 0.0f;
    }
}

float CharacterBody::move(const CollisionWorld &world, float vx, float vz, float seconds, bool jump,
                          const std::function<bool(float, float)> &allowed) {
    const float start[2] = {feet[0], feet[2]};
    if (jump && grounded) {
        vertical = set.jump;
        grounded = false;
    }
    seconds = std::max(seconds, 0.0f);
    const float travel = std::hypot(vx, vz) * seconds;
    const int steps = std::max({1, int(std::ceil(travel / MostStep)), int(std::ceil(seconds / MostTime))});
    const float dt = seconds / float(steps);
    const float walkable = std::cos(set.maxSlope * float(M_PI) / 180.0f);
    for (int i = 0; i < steps; ++i) {
        const float before[3] = {feet[0], feet[1], feet[2]};
        feet[0] += vx * dt;
        feet[2] += vz * dt;
        if (allowed && !allowed(feet[0], feet[2])) {
            feet[0] = before[0];
            feet[2] = before[2];
        }
        if (grounded) {
            CollisionWorld::Floor f;
            if (floorBelow(world, feet[0], feet[2], feet[1] + set.step, feet[1] - set.step, f)) {
                if (f.height - feet[1] > 0.01f && f.normal[1] < walkable) {
                    // Too steep to go up: it stays.
                    feet[0] = before[0];
                    feet[2] = before[2];
                } else {
                    feet[1] = f.height;
                    under = f;
                }
            } else {
                // Off an edge: it falls.
                grounded = false;
                vertical = 0.0f;
            }
        }
        if (!grounded) {
            vertical -= set.gravity * dt;
            const float to = feet[1] + vertical * dt;
            CollisionWorld::Floor f;
            if (vertical <= 0.0f && floorBelow(world, feet[0], feet[2], feet[1] + 0.05f, to, f)) {
                feet[1] = f.height;
                grounded = true;
                vertical = 0.0f;
                under = f;
            } else {
                feet[1] = to;
            }
        }
        pushOut(world);
        if (allowed && !allowed(feet[0], feet[2])) {
            feet[0] = before[0];
            feet[2] = before[2];
        }
        if (grounded) {
            // Pushed aside: on the floor where it is now.
            CollisionWorld::Floor f;
            if (floorBelow(world, feet[0], feet[2], feet[1] + set.step, feet[1] - set.step, f)) {
                feet[1] = f.height;
                under = f;
            }
        }
    }
    return std::hypot(feet[0] - start[0], feet[2] - start[1]);
}

}
