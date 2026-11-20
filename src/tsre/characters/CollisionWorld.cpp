/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include "CollisionWorld.h"

#include <algorithm>
#include <cmath>

namespace Characters {

namespace {

void point(const float *m, const float *p, float *out) {
    for (int i = 0; i < 3; ++i)
        out[i] = m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i];
}

void direction(const float *m, const float *d, float *out) {
    for (int i = 0; i < 3; ++i)
        out[i] = m[i] * d[0] + m[4 + i] * d[1] + m[8 + i] * d[2];
}

void unit(float *v) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length > 0.0f)
        for (int i = 0; i < 3; ++i)
            v[i] /= length;
}

}

void CollisionWorld::placeOne(Placed &p, const float *matrix) {
    std::copy(matrix, matrix + 16, p.matrix);
    // A turn times one scale: the scale is a column's length.
    p.scale = std::sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1] + matrix[2] * matrix[2]);
    const float s2 = p.scale > 0.0f ? 1.0f / (p.scale * p.scale) : 1.0f;
    // The inverse: the turn's transpose over the scale, the move undone.
    float *inv = p.inverse;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            inv[c * 4 + r] = matrix[r * 4 + c] * s2;
    inv[3] = inv[7] = inv[11] = 0.0f;
    inv[15] = 1.0f;
    for (int i = 0; i < 3; ++i)
        inv[12 + i] = -(inv[i] * matrix[12] + inv[4 + i] * matrix[13] + inv[8 + i] * matrix[14]);
    // The world box: the mesh's box's corners placed.
    const float *b = p.mesh->bounds();
    for (int corner = 0; corner < 8; ++corner) {
        const float local[3] = {b[(corner & 1) ? 3 : 0], b[(corner & 2) ? 4 : 1], b[(corner & 4) ? 5 : 2]};
        float world[3];
        point(matrix, local, world);
        for (int i = 0; i < 3; ++i) {
            p.box[i] = corner == 0 ? world[i] : std::min(p.box[i], world[i]);
            p.box[i + 3] = corner == 0 ? world[i] : std::max(p.box[i + 3], world[i]);
        }
    }
}

int CollisionWorld::add(std::shared_ptr<const CollisionMesh> mesh, const float *matrix, int tag) {
    if (!mesh)
        return -1;
    Placed p;
    p.mesh = std::move(mesh);
    p.tag = tag;
    placeOne(p, matrix);
    if (!free.empty()) {
        const int index = free.back();
        free.pop_back();
        placed[size_t(index)] = std::move(p);
        return index;
    }
    placed.push_back(std::move(p));
    return int(placed.size()) - 1;
}

void CollisionWorld::place(int index, const float *matrix) {
    if (index >= 0 && size_t(index) < placed.size() && placed[size_t(index)].mesh)
        placeOne(placed[size_t(index)], matrix);
}

void CollisionWorld::remove(int index) {
    if (index < 0 || size_t(index) >= placed.size() || !placed[size_t(index)].mesh)
        return;
    placed[size_t(index)].mesh.reset();
    free.push_back(index);
}

void CollisionWorld::clear() {
    placed.clear();
    free.clear();
}

int CollisionWorld::tagOf(int index) const {
    return index >= 0 && size_t(index) < placed.size() && placed[size_t(index)].mesh ? placed[size_t(index)].tag : 0;
}

bool CollisionWorld::floorAt(float x, float z, float above, float below, Floor &out) const {
    bool any = false;
    float terrainHeight = 0.0f;
    if (terrain && terrain(x, z, &terrainHeight) && terrainHeight <= above && terrainHeight >= below) {
        any = true;
        out.height = terrainHeight;
        out.placed = -1;
        // Its slope from the heights half a metre either way.
        float hx = terrainHeight, hz = terrainHeight;
        terrain(x + 0.5f, z, &hx);
        terrain(x, z + 0.5f, &hz);
        out.normal[0] = -(hx - terrainHeight) / 0.5f;
        out.normal[1] = 1.0f;
        out.normal[2] = -(hz - terrainHeight) / 0.5f;
        unit(out.normal);
    }
    for (size_t i = 0; i < placed.size(); ++i) {
        const Placed &p = placed[i];
        if (!p.mesh || x < p.box[0] || x > p.box[3] || z < p.box[2] || z > p.box[5] || p.box[1] > above || p.box[4] < below)
            continue;
        // A vertical line in the world is one in the mesh's space only
        // when the mesh stands upright; else a ray along it.
        const float top[3] = {x, above, z}, down[3] = {0.0f, -1.0f, 0.0f};
        float localTop[3], localDown[3];
        point(p.inverse, top, localTop);
        direction(p.inverse, down, localDown);
        unit(localDown);
        float distance = 0.0f, normal[3];
        if (!p.mesh->ray(localTop, localDown, (above - below) / p.scale, &distance, normal))
            continue;
        const float height = above - distance * p.scale;
        if (any && height <= out.height)
            continue;
        float worldNormal[3];
        direction(p.matrix, normal, worldNormal);
        unit(worldNormal);
        if (worldNormal[1] < 0.0f)
            for (int k = 0; k < 3; ++k)
                worldNormal[k] = -worldNormal[k];
        any = true;
        out.height = height;
        out.placed = int(i);
        std::copy(worldNormal, worldNormal + 3, out.normal);
    }
    return any;
}

bool CollisionWorld::ray(const float *origin, const float *dir, float length, float *distance, float *normal,
                         int *placedIndex) const {
    bool any = false;
    float best = length;
    for (size_t i = 0; i < placed.size(); ++i) {
        const Placed &p = placed[i];
        if (!p.mesh)
            continue;
        // Its box against the ray's.
        bool outside = false;
        for (int k = 0; k < 3 && !outside; ++k) {
            const float a = origin[k], b = origin[k] + dir[k] * best;
            outside = std::max(a, b) < p.box[k] || std::min(a, b) > p.box[k + 3];
        }
        if (outside)
            continue;
        float localOrigin[3], localDir[3];
        point(p.inverse, origin, localOrigin);
        direction(p.inverse, dir, localDir);
        unit(localDir);
        float d = 0.0f, n[3];
        if (!p.mesh->ray(localOrigin, localDir, best / p.scale, &d, n))
            continue;
        any = true;
        best = d * p.scale;
        direction(p.matrix, n, normal);
        unit(normal);
        if (placedIndex != nullptr)
            *placedIndex = int(i);
    }
    // The terrain: where the ray goes under it, in half metre steps.
    if (terrain) {
        float previous = 0.0f, height = 0.0f;
        bool above = false;
        for (float t = 0.0f; t <= best; t += 0.5f) {
            const float x = origin[0] + dir[0] * t, y = origin[1] + dir[1] * t, z = origin[2] + dir[2] * t;
            if (!terrain(x, z, &height))
                continue;
            const bool over = y >= height;
            if (t > 0.0f && above && !over) {
                any = true;
                best = std::max(previous, t - 0.5f);
                normal[0] = 0.0f;
                normal[1] = 1.0f;
                normal[2] = 0.0f;
                if (placedIndex != nullptr)
                    *placedIndex = -1;
                break;
            }
            above = over;
            previous = t;
        }
    }
    if (any)
        *distance = best;
    return any;
}

void CollisionWorld::addOpening(const float *box) {
    openings.insert(openings.end(), box, box + 6);
}

void CollisionWorld::contacts(const float *a, const float *b, float radius, std::vector<Contact> &out) const {
    std::vector<CollisionMesh::Contact> found;
    for (size_t i = 0; i < placed.size(); ++i) {
        const Placed &p = placed[i];
        if (!p.mesh)
            continue;
        bool outside = false;
        for (int k = 0; k < 3 && !outside; ++k)
            outside = std::max(a[k], b[k]) + radius < p.box[k] || std::min(a[k], b[k]) - radius > p.box[k + 3];
        if (outside)
            continue;
        float la[3], lb[3];
        point(p.inverse, a, la);
        point(p.inverse, b, lb);
        found.clear();
        p.mesh->contacts(la, lb, radius / p.scale, found);
        for (const CollisionMesh::Contact &c : found) {
            Contact w;
            point(p.matrix, c.onTriangle, w.contact.onTriangle);
            bool open = false;
            for (size_t o = 0; o + 5 < openings.size() && !open; o += 6) {
                const float *box = &openings[o];
                open = w.contact.onTriangle[0] >= box[0] && w.contact.onTriangle[0] <= box[3]
                        && w.contact.onTriangle[1] >= box[1] && w.contact.onTriangle[1] <= box[4]
                        && w.contact.onTriangle[2] >= box[2] && w.contact.onTriangle[2] <= box[5];
            }
            if (open)
                continue;
            point(p.matrix, c.onSegment, w.contact.onSegment);
            w.contact.distance = c.distance * p.scale;
            direction(p.matrix, c.normal, w.contact.normal);
            unit(w.contact.normal);
            w.placed = int(i);
            out.push_back(w);
        }
    }
}

}
