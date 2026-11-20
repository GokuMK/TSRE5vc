/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#include "CollisionMesh.h"

#include <algorithm>
#include <cmath>

namespace Characters {

namespace {

inline void sub(const float *a, const float *b, float *out) {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}
inline float dot(const float *a, const float *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline void cross(const float *a, const float *b, float *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
inline void along(const float *a, const float *d, float t, float *out) {
    out[0] = a[0] + d[0] * t;
    out[1] = a[1] + d[1] * t;
    out[2] = a[2] + d[2] * t;
}
inline float distance2(const float *a, const float *b) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return dot(d, d);
}

// The triangle's nearest point to p (Ericson, Real-Time Collision
// Detection 5.1.5).
void closestOnTriangle(const float *p, const float *a, const float *b, const float *c, float *out) {
    float ab[3], ac[3], ap[3];
    sub(b, a, ab);
    sub(c, a, ac);
    sub(p, a, ap);
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        std::copy(a, a + 3, out);
        return;
    }
    float bp[3];
    sub(p, b, bp);
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        std::copy(b, b + 3, out);
        return;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        along(a, ab, d1 / (d1 - d3), out);
        return;
    }
    float cp[3];
    sub(p, c, cp);
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        std::copy(c, c + 3, out);
        return;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        along(a, ac, d2 / (d2 - d6), out);
        return;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float bc[3];
        sub(c, b, bc);
        along(b, bc, (d4 - d3) / ((d4 - d3) + (d5 - d6)), out);
        return;
    }
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom, w = vc * denom;
    for (int i = 0; i < 3; ++i)
        out[i] = a[i] + ab[i] * v + ac[i] * w;
}

// The nearest points of two segments (Ericson 5.1.9).
void closestSegments(const float *p1, const float *q1, const float *p2, const float *q2, float *c1, float *c2) {
    float d1[3], d2[3], r[3];
    sub(q1, p1, d1);
    sub(q2, p2, d2);
    sub(p1, p2, r);
    const float a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
    const float eps = 1e-12f;
    float s = 0.0f, t = 0.0f;
    if (a <= eps && e <= eps) {
    } else if (a <= eps) {
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = dot(d1, r);
        if (e <= eps) {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = dot(d1, d2), denom = a * e - b * b;
            s = denom != 0.0f ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    along(p1, d1, s, c1);
    along(p2, d2, t, c2);
}

// Where a ray (origin, direction) meets a triangle, either side (Moller and
// Trumbore): its parameter, false for none.
bool rayTriangle(const float *o, const float *d, const float *t, float *at) {
    float e1[3], e2[3], p[3];
    sub(t + 3, t, e1);
    sub(t + 6, t, e2);
    cross(d, e2, p);
    const float det = dot(e1, p);
    if (std::fabs(det) < 1e-12f)
        return false;
    const float inv = 1.0f / det;
    float s[3];
    sub(o, t, s);
    const float u = dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f)
        return false;
    float q[3];
    cross(s, e1, q);
    const float v = dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f)
        return false;
    *at = dot(e2, q) * inv;
    return true;
}

void normalOf(const float *t, float *n) {
    float e1[3], e2[3];
    sub(t + 3, t, e1);
    sub(t + 6, t, e2);
    cross(e1, e2, n);
    const float length = std::sqrt(dot(n, n));
    if (length > 0.0f)
        for (int i = 0; i < 3; ++i)
            n[i] /= length;
}

}

CollisionMesh::CollisionMesh(std::vector<float> triangles) : tris(std::move(triangles)) {
    tris.resize(tris.size() / 9 * 9);
    if (tris.empty())
        return;
    for (int i = 0; i < 3; ++i) {
        box[i] = tris[size_t(i)];
        box[i + 3] = tris[size_t(i)];
    }
    for (size_t v = 0; v < tris.size(); v += 3)
        for (int i = 0; i < 3; ++i) {
            box[i] = std::min(box[i], tris[v + size_t(i)]);
            box[i + 3] = std::max(box[i + 3], tris[v + size_t(i)]);
        }
    // Cells a metre across, coarser for very large shapes (at most 256 a
    // side).
    const float extent = std::max(box[3] - box[0], box[5] - box[2]);
    cell = std::max(1.0f, extent / 256.0f);
    cells[0] = int(std::floor((box[3] - box[0]) / cell)) + 1;
    cells[1] = int(std::floor((box[5] - box[2]) / cell)) + 1;
    const int count = triangleCount();
    std::vector<int> counts(size_t(cells[0]) * size_t(cells[1]) + 1, 0);
    auto range = [this](const float *t, int *r) {
        const float x0 = std::min({t[0], t[3], t[6]}), x1 = std::max({t[0], t[3], t[6]});
        const float z0 = std::min({t[2], t[5], t[8]}), z1 = std::max({t[2], t[5], t[8]});
        r[0] = std::clamp(int(std::floor((x0 - box[0]) / cell)), 0, cells[0] - 1);
        r[1] = std::clamp(int(std::floor((x1 - box[0]) / cell)), 0, cells[0] - 1);
        r[2] = std::clamp(int(std::floor((z0 - box[2]) / cell)), 0, cells[1] - 1);
        r[3] = std::clamp(int(std::floor((z1 - box[2]) / cell)), 0, cells[1] - 1);
    };
    for (int i = 0; i < count; ++i) {
        int r[4];
        range(&tris[size_t(i) * 9], r);
        for (int z = r[2]; z <= r[3]; ++z)
            for (int x = r[0]; x <= r[1]; ++x)
                ++counts[size_t(z) * size_t(cells[0]) + size_t(x) + 1];
    }
    for (size_t c = 1; c < counts.size(); ++c)
        counts[c] += counts[c - 1];
    start = counts;
    items.assign(size_t(counts.back()), 0);
    for (int i = 0; i < count; ++i) {
        int r[4];
        range(&tris[size_t(i) * 9], r);
        for (int z = r[2]; z <= r[3]; ++z)
            for (int x = r[0]; x <= r[1]; ++x)
                items[size_t(counts[size_t(z) * size_t(cells[0]) + size_t(x)]++)] = i;
    }
}

void CollisionMesh::gather(float x0, float z0, float x1, float z1, std::vector<int> &out) const {
    out.clear();
    if (tris.empty() || x1 < box[0] || x0 > box[3] || z1 < box[2] || z0 > box[5])
        return;
    const int cx0 = std::clamp(int(std::floor((x0 - box[0]) / cell)), 0, cells[0] - 1);
    const int cx1 = std::clamp(int(std::floor((x1 - box[0]) / cell)), 0, cells[0] - 1);
    const int cz0 = std::clamp(int(std::floor((z0 - box[2]) / cell)), 0, cells[1] - 1);
    const int cz1 = std::clamp(int(std::floor((z1 - box[2]) / cell)), 0, cells[1] - 1);
    for (int z = cz0; z <= cz1; ++z)
        for (int x = cx0; x <= cx1; ++x) {
            const size_t c = size_t(z) * size_t(cells[0]) + size_t(x);
            out.insert(out.end(), items.begin() + start[c], items.begin() + start[c + 1]);
        }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

bool CollisionMesh::floorAt(float x, float z, float above, float below, float *height, float *normal) const {
    std::vector<int> found;
    gather(x, z, x, z, found);
    const float origin[3] = {x, above, z}, down[3] = {0.0f, -1.0f, 0.0f};
    bool any = false;
    float best = below;
    for (int i : found) {
        const float *t = &tris[size_t(i) * 9];
        float at = 0.0f;
        if (!rayTriangle(origin, down, t, &at) || at < 0.0f || above - at < below)
            continue;
        const float y = above - at;
        if (!any || y > best) {
            any = true;
            best = y;
            normalOf(t, normal);
            if (normal[1] < 0.0f)
                for (int k = 0; k < 3; ++k)
                    normal[k] = -normal[k];
        }
    }
    if (any)
        *height = best;
    return any;
}

bool CollisionMesh::ray(const float *origin, const float *direction, float length, float *distance, float *normal) const {
    float end[3];
    along(origin, direction, length, end);
    std::vector<int> found;
    gather(std::min(origin[0], end[0]), std::min(origin[2], end[2]), std::max(origin[0], end[0]), std::max(origin[2], end[2]),
           found);
    bool any = false;
    float best = length;
    for (int i : found) {
        const float *t = &tris[size_t(i) * 9];
        float at = 0.0f;
        if (!rayTriangle(origin, direction, t, &at) || at < 0.0f || at > best)
            continue;
        any = true;
        best = at;
        normalOf(t, normal);
        if (dot(normal, direction) > 0.0f)
            for (int k = 0; k < 3; ++k)
                normal[k] = -normal[k];
    }
    if (any)
        *distance = best;
    return any;
}

float CollisionMesh::segmentTriangle(const float *a, const float *b, const float *t, float *onSegment, float *onTriangle) {
    // Through it: they touch.
    float d[3];
    sub(b, a, d);
    float at = 0.0f;
    if (rayTriangle(a, d, t, &at) && at >= 0.0f && at <= 1.0f) {
        along(a, d, at, onSegment);
        std::copy(onSegment, onSegment + 3, onTriangle);
        return 0.0f;
    }
    // Else the nearest of: each end to the triangle, the segment to each
    // edge.
    float best = -1.0f;
    float s[3], q[3];
    for (const float *end : {a, b}) {
        closestOnTriangle(end, t, t + 3, t + 6, q);
        const float d2 = distance2(end, q);
        if (best < 0.0f || d2 < best) {
            best = d2;
            std::copy(end, end + 3, onSegment);
            std::copy(q, q + 3, onTriangle);
        }
    }
    for (int e = 0; e < 3; ++e) {
        closestSegments(a, b, t + e * 3, t + ((e + 1) % 3) * 3, s, q);
        const float d2 = distance2(s, q);
        if (d2 < best) {
            best = d2;
            std::copy(s, s + 3, onSegment);
            std::copy(q, q + 3, onTriangle);
        }
    }
    return std::sqrt(best);
}

void CollisionMesh::contacts(const float *a, const float *b, float radius, std::vector<Contact> &out) const {
    std::vector<int> found;
    gather(std::min(a[0], b[0]) - radius, std::min(a[2], b[2]) - radius, std::max(a[0], b[0]) + radius,
           std::max(a[2], b[2]) + radius, found);
    const float middle[3] = {0.5f * (a[0] + b[0]), 0.5f * (a[1] + b[1]), 0.5f * (a[2] + b[2])};
    for (int i : found) {
        const float *t = &tris[size_t(i) * 9];
        // Outside the segment's box grown by the radius: none.
        const float y0 = std::min({t[1], t[4], t[7]}), y1 = std::max({t[1], t[4], t[7]});
        if (y1 < std::min(a[1], b[1]) - radius || y0 > std::max(a[1], b[1]) + radius)
            continue;
        Contact c;
        c.distance = segmentTriangle(a, b, t, c.onSegment, c.onTriangle);
        if (c.distance >= radius)
            continue;
        if (c.distance > 1e-6f) {
            for (int k = 0; k < 3; ++k)
                c.normal[k] = (c.onSegment[k] - c.onTriangle[k]) / c.distance;
        } else {
            normalOf(t, c.normal);
            float toMiddle[3];
            sub(middle, c.onTriangle, toMiddle);
            if (dot(c.normal, toMiddle) < 0.0f)
                for (int k = 0; k < 3; ++k)
                    c.normal[k] = -c.normal[k];
        }
        out.push_back(c);
    }
}

}
