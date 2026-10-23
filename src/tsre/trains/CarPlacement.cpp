/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "CarPlacement.h"
#include <algorithm>
#include <cmath>
#include <tsre/math3d/GLMatrix.h>

namespace CarPlacement {

namespace {

// The cubic between two ends, their places p0, p1 and their ways t0, t1
// scaled by `span`: where it is at u (0 to 1) and which way it runs there.
void hermite(const float *p0, const float *t0, const float *p1, const float *t1, float span, float u, float *at,
             float *way) {
    const float u2 = u * u, u3 = u2 * u;
    const float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    const float d00 = 6 * u2 - 6 * u, d10 = 3 * u2 - 4 * u + 1, d01 = -6 * u2 + 6 * u, d11 = 3 * u2 - 2 * u;
    for (int i = 0; i < 3; ++i) {
        at[i] = h00 * p0[i] + h10 * span * t0[i] + h01 * p1[i] + h11 * span * t1[i];
        way[i] = d00 * p0[i] + d10 * span * t0[i] + d01 * p1[i] + d11 * span * t1[i];
    }
}

}

void placeCar(float *m, const float *playerT, const float *front, const float *rear, float roll, bool flipped) {
    float drawPosition1[7], drawPosition2[7];
    std::copy(front, front + 7, drawPosition1);
    std::copy(rear, rear + 7, drawPosition2);

    float pos[5];
    pos[3] = drawPosition1[5];
    pos[4] = drawPosition1[6];
    drawPosition2[0] += 2048*(drawPosition2[5] - drawPosition1[5]);
    drawPosition2[2] += 2048*(drawPosition2[6] - drawPosition1[6]);
    Vec3::add(pos, drawPosition1, drawPosition2);
    Vec3::scale(pos, pos, 0.5);
    float dlugosc = Vec3::distance(drawPosition1, drawPosition2);

    int someval = (((drawPosition1[2] - drawPosition2[2]) + 0.00001f) / fabs((drawPosition1[2] - drawPosition2[2]) + 0.00001f));
    float rotY = ((float)someval + 1.0)*(M_PI / 2.0)+(float)(atan((drawPosition1[0] - drawPosition2[0]) / (drawPosition1[2] - drawPosition2[2])));
    float rotX = -(float)(asin((drawPosition1[1] - drawPosition2[1]) / (dlugosc)));

    Mat4::translate(m, m, pos[0] + 2048 * (pos[3] - playerT[0]), pos[1] + 0.28, -pos[2] + 2048 * (-pos[4] - playerT[1]));
    Mat4::rotateY(m, m, -rotY);
    Mat4::rotateX(m, m, rotX);
    Mat4::rotate(m, m, -roll, 0, 0, 1);
    Mat4::rotateY(m, m, flipped ? M_PI : 0.0);
}

bool onCarriers(const float *front, const float *p0, const float *t0, const float *p1, const float *t1, float roll,
                bool flipped, const std::vector<float> &carriers, const std::vector<Bogie> &bogies,
                float *placedFront, float *placedRear, std::vector<float> &bogieTurns) {
    const float span = std::sqrt((p1[0] - p0[0]) * (p1[0] - p0[0]) + (p1[1] - p0[1]) * (p1[1] - p0[1])
                                 + (p1[2] - p0[2]) * (p1[2] - p0[2]));
    if (span < 0.5f || carriers.size() < 2)
        return false;
    // A shape z on the track: its place along the car from its front end
    // (as its consist lists it, 0 to 1), s toward that front from its middle.
    const float sign = flipped ? -1.0f : 1.0f;
    auto along = [&](float z) { return 0.5f - sign * z / span; };
    double sumS = 0, sumSS = 0, sumP[3] = {0, 0, 0}, sumSP[3] = {0, 0, 0};
    for (float z : carriers) {
        float at[3], way[3];
        hermite(p0, t0, p1, t1, span, along(z), at, way);
        const double s = sign * z;
        sumS += s;
        sumSS += s * s;
        for (int i = 0; i < 3; ++i) {
            sumP[i] += at[i];
            sumSP[i] += s * at[i];
        }
    }
    const double n = double(carriers.size());
    const double spread = n * sumSS - sumS * sumS;
    if (spread < 1e-6)
        return false;
    for (int i = 0; i < 3; ++i) {
        const double slope = (n * sumSP[i] - sumS * sumP[i]) / spread;
        const double middle = (sumP[i] - slope * sumS) / n;
        placedFront[i] = float(middle + slope * span * 0.5);
        placedRear[i] = float(middle - slope * span * 0.5);
    }
    for (int i = 3; i < 7; ++i)
        placedFront[i] = placedRear[i] = front[i];

    bogieTurns.assign(bogies.size() * 16, 0.0f);
    if (bogies.empty())
        return true;
    float body[16];
    Mat4::identity(body);
    const float tile[2] = {front[5], -front[6]};
    placeCar(body, tile, placedFront, placedRear, roll, flipped);
    // The track's ways are as its places are (x, y, z); the world's z is
    // their -z (placeCar).
    const float ahead[3] = {body[8], body[9], body[10]};
    for (size_t b = 0; b < bogies.size(); ++b) {
        float *turn = bogieTurns.data() + b * 16;
        float at[3], track[3];
        hermite(p0, t0, p1, t1, span, along(bogies[b].z), at, track);
        float way[3] = {track[0], track[1], -track[2]};
        // Toward its shape's front.
        if (way[0] * ahead[0] + way[1] * ahead[1] + way[2] * ahead[2] < 0)
            for (float &c : way)
                c = -c;
        // Into the shape's space: the body's rotation undone, then the
        // root's mirror (x).
        const float local[3] = {-(body[0] * way[0] + body[1] * way[1] + body[2] * way[2]),
                                body[4] * way[0] + body[5] * way[1] + body[6] * way[2],
                                body[8] * way[0] + body[9] * way[1] + body[10] * way[2]};
        const float length = std::sqrt(local[0] * local[0] + local[1] * local[1] + local[2] * local[2]);
        if (length < 1e-6f) {
            turn[15] = std::nanf("");
            continue;
        }
        const float yaw = std::atan2(local[0], local[2]);
        const float pitch = std::asin(std::clamp(local[1] / length, -1.0f, 1.0f));
        // RotY(yaw) * RotX(-pitch) (column major), its rest translation kept.
        const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
        const float *rest = bogies[b].rest;
        const float rotation[16] = {cy, 0, -sy, 0, sy * sp, cp, cy * sp, 0, sy * cp, -sp, cy * cp, 0,
                                    rest[12], rest[13], rest[14], 1};
        bool same = true;
        for (int i = 0; i < 16 && same; ++i)
            same = std::fabs(rotation[i] - rest[i]) < 1e-5f;
        if (same)
            turn[15] = std::nanf("");
        else
            std::copy(rotation, rotation + 16, turn);
    }
    return true;
}

}
