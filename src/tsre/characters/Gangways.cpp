/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include "Gangways.h"

#include <tsre/shape/ShapeSeats.h>
#include <algorithm>
#include <cmath>

namespace Characters {
namespace Gangways {

bool Plate::holds(float px, float pz, float inside) const {
    if (!open() || std::fabs(px - x) > width / 2.0f)
        return false;
    const float from = float(end) * (wall - float(end) * inside), to = float(end) * far;
    const float along = float(end) * pz;
    return along >= from && along <= to;
}

bool within(float halfWidth, float halfLength, const Plate &front, const Plate &rear, float x, float z,
            float margin) {
    if (std::fabs(x) <= halfWidth - margin && std::fabs(z) <= halfLength - margin)
        return true;
    return front.holds(x, z, WallDepth) || rear.holds(x, z, WallDepth);
}

bool reaches(const ShapeSeats::EndPassage &door, float couplingEnd) {
    const float out = float(door.end()) * door.position[2];
    return couplingEnd > 0.0f && out <= couplingEnd && couplingEnd - out <= MaxReach;
}

bool matches(const ShapeSeats::EndPassage &a, float couplingA, const ShapeSeats::EndPassage &b, float couplingB,
             float *width) {
    if (!reaches(a, couplingA) || !reaches(b, couplingB)
            || std::fabs(a.position[1] - b.position[1]) > ShapeSeats::OneStep + ShapeSeats::StepTolerance)
        return false;
    if (width != nullptr)
        *width = std::min(a.width, b.width);
    return true;
}

Plate plateOf(const ShapeSeats::EndPassage &door, float couplingEnd, float width) {
    Plate plate;
    plate.end = door.end();
    plate.x = door.position[0];
    plate.y = door.position[1];
    plate.wall = door.position[2];
    plate.far = float(plate.end) * (couplingEnd + Overlap);
    plate.width = width;
    return plate;
}

namespace {

void quad(std::vector<float> &t, const float *a, const float *b, const float *c, const float *d) {
    for (const float *p : {a, b, c, a, c, d})
        t.insert(t.end(), p, p + 3);
}

}

std::vector<float> triangles(const Plate &plate) {
    std::vector<float> t;
    if (!plate.open())
        return t;
    const float x0 = plate.x - plate.width / 2.0f, x1 = plate.x + plate.width / 2.0f;
    const float y0 = plate.y, y1 = plate.y + WallHeight;
    const float in = plate.wall - float(plate.end) * WallDepth;
    {
        const float a[3] = {x0, y0, in}, b[3] = {x1, y0, in}, c[3] = {x1, y0, plate.far}, d[3] = {x0, y0, plate.far};
        quad(t, a, b, c, d);
    }
    for (const float x : {x0, x1}) {
        const float a[3] = {x, y0, plate.wall}, b[3] = {x, y0, plate.far}, c[3] = {x, y1, plate.far},
                    d[3] = {x, y1, plate.wall};
        quad(t, a, b, c, d);
    }
    return t;
}

void opening(const ShapeSeats::EndPassage &door, const Plate &plate, float *box) {
    const float in = plate.wall - float(plate.end) * WallDepth;
    box[0] = door.position[0] - door.width / 2.0f - Aside;
    box[1] = door.position[1] + 0.05f;
    box[2] = std::min(in, plate.far);
    box[3] = door.position[0] + door.width / 2.0f + Aside;
    box[4] = door.position[1] + WallHeight;
    box[5] = std::max(in, plate.far);
}

int endTowards(const float *inverse, const float *otherMatrix) {
    const float *p = otherMatrix + 12;
    const float z = inverse[2] * p[0] + inverse[6] * p[1] + inverse[10] * p[2] + inverse[14];
    return z >= 0.0f ? 1 : -1;
}

bool handedOver(const Plate &plate, float couplingEnd, float z) {
    return plate.open() && float(plate.end) * z > couplingEnd + Handover;
}

int nextCar(const std::vector<float> &lengths, int car, int step) {
    for (int next = car + step; next >= 0 && next < int(lengths.size()); next += step)
        if (lengths[size_t(next)] >= JointLength)
            return next;
    return -1;
}

bool insidePlace(const Plate &plate, const float *feet, float *out) {
    if (!plate.holds(feet[0], feet[2], WallDepth))
        return false;
    out[0] = plate.x;
    out[1] = plate.wall - float(plate.end) * StepIn;
    return true;
}

}
}
