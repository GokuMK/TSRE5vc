/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MapView.h"
#include <algorithm>
#include <cmath>
#include <tsre/math3d/GLMatrix.h>

void MapView::up(float &ux, float &uz) const {
    ux = std::sin(heading);
    uz = std::cos(heading);
}

void MapView::right(float &rx, float &rz) const {
    // Looking down, right is up turned clockwise on the ground.
    float ux, uz;
    up(ux, uz);
    rx = -uz;
    rz = ux;
}

void MapView::groundAt(float px, float py, float &gx, float &gz) const {
    float rx, rz, ux, uz;
    right(rx, rz);
    up(ux, uz);
    const float sx = (px - 0.5f * width) * metresPerPixel;
    const float sy = (0.5f * height - py) * metresPerPixel;
    gx = x + rx * sx + ux * sy;
    gz = z + rz * sx + uz * sy;
}

void MapView::screenAt(float gx, float gz, float &px, float &py) const {
    float rx, rz, ux, uz;
    right(rx, rz);
    up(ux, uz);
    const float dx = gx - x;
    const float dz = gz - z;
    px = 0.5f * width + (dx * rx + dz * rz) / metresPerPixel;
    py = 0.5f * height - (dx * ux + dz * uz) / metresPerPixel;
}

void MapView::pan(float dxPixels, float dyPixels) {
    float rx, rz, ux, uz;
    right(rx, rz);
    up(ux, uz);
    x -= (rx * dxPixels - ux * dyPixels) * metresPerPixel;
    z -= (rz * dxPixels - uz * dyPixels) * metresPerPixel;
    normalize();
}

void MapView::zoomAt(float px, float py, float factor) {
    float gx, gz;
    groundAt(px, py, gx, gz);
    metresPerPixel = std::clamp(metresPerPixel * factor, MinMetresPerPixel, MaxMetresPerPixel);
    // Put the ground point back under the screen point.
    float nx, nz;
    groundAt(px, py, nx, nz);
    x += gx - nx;
    z += gz - nz;
    normalize();
}

void MapView::rotate(float radians) {
    heading = std::remainder(heading + radians, 2.0f * 3.14159265f);
}

void MapView::normalize() {
    while (x > 1024.0f) {
        x -= 2048.0f;
        ++tileX;
    }
    while (x < -1024.0f) {
        x += 2048.0f;
        --tileX;
    }
    while (z > 1024.0f) {
        z -= 2048.0f;
        ++tileZ;
    }
    while (z < -1024.0f) {
        z += 2048.0f;
        --tileZ;
    }
}

void MapView::viewMatrix(float *out) const {
    float ux, uz;
    up(ux, uz);
    float eye[3] = {x, EyeHeight, z};
    float target[3] = {x, 0.0f, z};
    float upVector[3] = {ux, 0.0f, uz};
    Mat4::lookAt(out, eye, target, upVector);
}

void MapView::projection(float *out) const {
    const float halfWidth = 0.5f * width * metresPerPixel;
    const float halfHeight = 0.5f * height * metresPerPixel;
    Mat4::ortho(out, -halfWidth, halfWidth, -halfHeight, halfHeight, NearPlane, FarPlane);
}
