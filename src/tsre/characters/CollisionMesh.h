/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef CHARACTERS_COLLISIONMESH_H
#define CHARACTERS_COLLISIONMESH_H

#include <vector>

namespace Characters {

// A shape's collision triangles in its own space (task characters 02):
// what a walking character stands on and is stopped by. Triangles listed
// by cells of a grid over x and z (people stand on floors and are stopped
// by walls, both found by where they are over the ground). Made once
// (any thread), then only read: queries may run on any thread.
class CollisionMesh {
public:
    // Nine floats a triangle (three points x, y, z).
    explicit CollisionMesh(std::vector<float> triangles);
    int triangleCount() const { return int(tris.size() / 9); }
    const std::vector<float> &triangles() const { return tris; }
    // Min x, y, z then max x, y, z; all 0 without triangles.
    const float *bounds() const { return box; }

    // The highest triangle under (x, z) between `below` and `above` (y),
    // either side up; its height and its normal turned up (unit).
    bool floorAt(float x, float z, float above, float below, float *height, float *normal) const;
    // The nearest triangle a ray meets (origin, unit direction) within
    // `length`, either side; how far and its normal turned against the ray.
    bool ray(const float *origin, const float *direction, float length, float *distance, float *normal) const;
    // The triangles within `radius` of a segment (a capsule's core, a to
    // b): each one's nearest point to it, the segment's nearest point, how
    // far apart, and the way out (unit, from the triangle to the segment;
    // the triangle's normal turned towards the segment's middle when they
    // touch).
    struct Contact {
        float onTriangle[3] = {0.0f, 0.0f, 0.0f};
        float onSegment[3] = {0.0f, 0.0f, 0.0f};
        float distance = 0.0f;
        float normal[3] = {0.0f, 1.0f, 0.0f};
    };
    void contacts(const float *a, const float *b, float radius, std::vector<Contact> &out) const;

    // The nearest points of a segment (a, b) and a triangle (t: nine
    // floats); their distance.
    static float segmentTriangle(const float *a, const float *b, const float *t, float *onSegment, float *onTriangle);

private:
    std::vector<float> tris;
    float box[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float cell = 1.0f;
    int cells[2] = {0, 0};
    std::vector<int> start, items;
    // The triangles over an area of x and z, each once.
    void gather(float x0, float z0, float x1, float z1, std::vector<int> &out) const;
};

}

#endif
