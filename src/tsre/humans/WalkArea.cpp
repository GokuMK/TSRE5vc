/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "WalkArea.h"

#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

namespace Humans {

namespace {

// Columns a mesh may have (a station 600 m by 100 m at 0.15 m is 2.7 million).
constexpr long long MaxColumns = 12000000;

struct HeightfieldDeleter {
    void operator()(rcHeightfield *p) const { rcFreeHeightField(p); }
};
struct CompactDeleter {
    void operator()(rcCompactHeightfield *p) const { rcFreeCompactHeightfield(p); }
};
struct ContoursDeleter {
    void operator()(rcContourSet *p) const { rcFreeContourSet(p); }
};
struct PolyMeshDeleter {
    void operator()(rcPolyMesh *p) const { rcFreePolyMesh(p); }
};
struct DetailDeleter {
    void operator()(rcPolyMeshDetail *p) const { rcFreePolyMeshDetail(p); }
};
struct QueryDeleter {
    void operator()(dtNavMeshQuery *p) const { dtFreeNavMeshQuery(p); }
};

bool inPolygon(const float *poly, int count, float x, float z) {
    bool inside = false;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        const float xi = poly[i * 3], zi = poly[i * 3 + 2], xj = poly[j * 3], zj = poly[j * 3 + 2];
        if (((zi > z) != (zj > z)) && (x < (xj - xi) * (z - zi) / (zj - zi) + xi))
            inside = !inside;
    }
    return inside;
}

// Sets the area of the spans whose cell centre is in the polygon (x, z;
// three floats a point) and whose top is from low to high, where `from`
// accepts their area (never the null area).
void markArea(rcCompactHeightfield &chf, const float *poly, int count, float low, float high, unsigned char to,
              const std::function<bool(unsigned char)> &from) {
    float minX = poly[0], maxX = poly[0], minZ = poly[2], maxZ = poly[2];
    for (int i = 1; i < count; ++i) {
        minX = std::min(minX, poly[i * 3]);
        maxX = std::max(maxX, poly[i * 3]);
        minZ = std::min(minZ, poly[i * 3 + 2]);
        maxZ = std::max(maxZ, poly[i * 3 + 2]);
    }
    const int x0 = std::max(0, int((minX - chf.bmin[0]) / chf.cs));
    const int x1 = std::min(chf.width - 1, int((maxX - chf.bmin[0]) / chf.cs));
    const int z0 = std::max(0, int((minZ - chf.bmin[2]) / chf.cs));
    const int z1 = std::min(chf.height - 1, int((maxZ - chf.bmin[2]) / chf.cs));
    const int y0 = int(std::floor((low - chf.bmin[1]) / chf.ch));
    const int y1 = int(std::ceil((high - chf.bmin[1]) / chf.ch));
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const float cx = chf.bmin[0] + (float(x) + 0.5f) * chf.cs;
            const float cz = chf.bmin[2] + (float(z) + 0.5f) * chf.cs;
            if (!inPolygon(poly, count, cx, cz))
                continue;
            const rcCompactCell &cell = chf.cells[x + z * chf.width];
            for (unsigned int i = cell.index, end = cell.index + cell.count; i < end; ++i) {
                const rcCompactSpan &span = chf.spans[i];
                if (int(span.y) < y0 || int(span.y) > y1 || chf.areas[i] == RC_NULL_AREA || !from(chf.areas[i]))
                    continue;
                chf.areas[i] = to;
            }
        }
}

// A polyline's points by distance along it: where it is at `along`, its way
// there (level, unit).
struct Line {
    const std::vector<float> &points;
    std::vector<float> lengths;
    explicit Line(const std::vector<float> &p) : points(p) {
        lengths.push_back(0.0f);
        for (size_t i = 3; i + 2 < p.size(); i += 3) {
            const float dx = p[i] - p[i - 3], dz = p[i + 2] - p[i - 1];
            lengths.push_back(lengths.back() + std::sqrt(dx * dx + dz * dz));
        }
    }
    float length() const { return lengths.back(); }
    void at(float along, float position[3], float way[3]) const {
        size_t k = 1;
        while (k + 1 < lengths.size() && lengths[k] < along)
            ++k;
        const float *a = points.data() + (k - 1) * 3, *b = points.data() + k * 3;
        const float span = lengths[k] - lengths[k - 1];
        const float t = span > 1e-6f ? std::clamp((along - lengths[k - 1]) / span, 0.0f, 1.0f) : 0.0f;
        for (int c = 0; c < 3; ++c)
            position[c] = a[c] + (b[c] - a[c]) * t;
        const float dx = b[0] - a[0], dz = b[2] - a[2];
        const float l = std::sqrt(dx * dx + dz * dz);
        way[0] = l > 1e-6f ? dx / l : 1.0f;
        way[1] = 0.0f;
        way[2] = l > 1e-6f ? dz / l : 0.0f;
    }
};

// A quad along a line from `from` to `to` (distances), between `near` and
// `far` off it towards `side`.
void stripQuad(const Line &line, float from, float to, float side, float near, float far, float quad[12]) {
    float a[3], b[3], wayA[3], wayB[3];
    line.at(from, a, wayA);
    line.at(to, b, wayB);
    const float na[2] = {-wayA[2] * side, wayA[0] * side}, nb[2] = {-wayB[2] * side, wayB[0] * side};
    const float corners[4][3] = {{a[0] + na[0] * near, a[1], a[2] + na[1] * near},
                                 {b[0] + nb[0] * near, b[1], b[2] + nb[1] * near},
                                 {b[0] + nb[0] * far, b[1], b[2] + nb[1] * far},
                                 {a[0] + na[0] * far, a[1], a[2] + na[1] * far}};
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 3; ++c)
            quad[i * 3 + c] = corners[i][c];
}

}

WalkArea::Built WalkArea::build(const Input &input) {
    return build(input, Settings());
}

WalkArea::Built WalkArea::build(const Input &input, const Settings &s) {
    QElapsedTimer timer;
    timer.start();
    Built out;
    const int vertexCount = int(input.vertices.size() / 3);
    const int triangleCount = int(input.triangles.size() / 3);
    if (triangleCount == 0 || int(input.roles.size()) != triangleCount) {
        out.error = "no triangles";
        return out;
    }

    // Bounds: the triangles and the platforms, with room above.
    float bmin[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max()};
    float bmax[3] = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                     -std::numeric_limits<float>::max()};
    auto grow = [&](const float *p) {
        for (int c = 0; c < 3; ++c) {
            bmin[c] = std::min(bmin[c], p[c]);
            bmax[c] = std::max(bmax[c], p[c]);
        }
    };
    for (int i = 0; i < vertexCount; ++i)
        grow(input.vertices.data() + i * 3);
    for (const Platform &platform : input.platforms)
        for (size_t i = 0; i + 2 < platform.line.size(); i += 3)
            grow(platform.line.data() + i);
    bmax[1] += s.personHeight + 1.0f;
    std::copy(bmin, bmin + 3, out.bounds);
    std::copy(bmax, bmax + 3, out.bounds + 3);

    rcContext context(false);
    const int walkableHeight = int(std::ceil(s.personHeight / s.cellHeight));
    const int walkableClimb = int(std::floor(s.step / s.cellHeight));
    const int walkableRadius = int(std::ceil(s.personRadius / s.cellSize));
    int width = 0, depth = 0;
    rcCalcGridSize(bmin, bmax, s.cellSize, &width, &depth);
    out.cells[0] = width;
    out.cells[1] = depth;
    if (width <= 0 || depth <= 0 || (long long)width * depth > MaxColumns) {
        out.error = QString("area too large: %1 x %2 cells").arg(width).arg(depth);
        return out;
    }

    // Each triangle's area by its role: floor and ground where level enough
    // to stand on, else none (it still takes space).
    std::vector<unsigned char> areas(size_t(triangleCount), RC_NULL_AREA);
    const float steepest = std::cos(s.maxSlope * float(M_PI) / 180.0f);
    for (int t = 0; t < triangleCount; ++t) {
        if (input.roles[size_t(t)] == Role::Solid)
            continue;
        const float *v0 = input.vertices.data() + input.triangles[size_t(t) * 3] * 3;
        const float *v1 = input.vertices.data() + input.triangles[size_t(t) * 3 + 1] * 3;
        const float *v2 = input.vertices.data() + input.triangles[size_t(t) * 3 + 2] * 3;
        const float e0[3] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]};
        const float e1[3] = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]};
        const float n[3] = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2], e0[0] * e1[1] - e0[1] * e1[0]};
        const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        // Either winding: shapes come from either hand.
        if (length > 1e-12f && std::fabs(n[1]) / length >= steepest)
            areas[size_t(t)] = input.roles[size_t(t)] == Role::Floor ? AreaFloor : AreaGround;
    }

    std::unique_ptr<rcHeightfield, HeightfieldDeleter> heightfield(rcAllocHeightfield());
    if (!heightfield || !rcCreateHeightfield(&context, *heightfield, width, depth, bmin, bmax, s.cellSize, s.cellHeight)) {
        out.error = "no heightfield";
        return out;
    }
    if (!rcRasterizeTriangles(&context, input.vertices.data(), vertexCount, input.triangles.data(), areas.data(),
                              triangleCount, *heightfield, walkableClimb)) {
        out.error = "rasterizing failed";
        return out;
    }
    rcFilterLowHangingWalkableObstacles(&context, walkableClimb, *heightfield);
    rcFilterLedgeSpans(&context, walkableHeight, walkableClimb, *heightfield);
    rcFilterWalkableLowHeightSpans(&context, walkableHeight, *heightfield);
    std::unique_ptr<rcCompactHeightfield, CompactDeleter> chf(rcAllocCompactHeightfield());
    if (!chf || !rcBuildCompactHeightfield(&context, walkableHeight, walkableClimb, *heightfield, *chf)) {
        out.error = "no compact heightfield";
        return out;
    }
    heightfield.reset();
    auto any = [](unsigned char) { return true; };

    // Track: closed.
    for (const std::vector<float> &track : input.tracks) {
        for (size_t i = 3; i + 2 < track.size(); i += 3) {
            const float *a = track.data() + i - 3, *b = track.data() + i;
            const float dx = b[0] - a[0], dz = b[2] - a[2];
            const float l = std::sqrt(dx * dx + dz * dz);
            if (l < 1e-4f)
                continue;
            const float nx = -dz / l * s.trackHalfWidth, nz = dx / l * s.trackHalfWidth;
            // Its ends a little past each point, so bends leave no gaps.
            const float ex = dx / l * 0.2f, ez = dz / l * 0.2f;
            const float quad[12] = {a[0] + nx - ex, a[1], a[2] + nz - ez, b[0] + nx + ex, b[1], b[2] + nz + ez,
                                    b[0] - nx + ex, b[1], b[2] - nz + ez, a[0] - nx - ex, a[1], a[2] - nz - ez};
            markArea(*chf, quad, 4, std::min(a[1], b[1]) - s.trackBelow, std::max(a[1], b[1]) + s.trackAbove,
                     RC_NULL_AREA, any);
        }
    }

    // Seeds: the highest floor or ground in each one's window.
    out.platforms.assign(input.platforms.size(), Built::PlatformSeeds());
    for (size_t p = 0; p < input.platforms.size(); ++p) {
        const Platform &platform = input.platforms[p];
        if (platform.line.size() < 6)
            continue;
        const Line line(platform.line);
        const float inset = s.trackHalfWidth + s.seedInset;
        for (float along = 0.5f * s.seedSpacing; along < line.length(); along += s.seedSpacing) {
            float at[3], way[3];
            line.at(along, at, way);
            Seed seed;
            seed.platform = int(p);
            seed.position[0] = at[0] - way[2] * platform.side * inset;
            seed.position[1] = at[1];
            seed.position[2] = at[2] + way[0] * platform.side * inset;
            const int x = int((seed.position[0] - chf->bmin[0]) / chf->cs);
            const int z = int((seed.position[2] - chf->bmin[2]) / chf->cs);
            if (x >= 0 && z >= 0 && x < chf->width && z < chf->height) {
                const rcCompactCell &cell = chf->cells[x + z * chf->width];
                // The highest floor or ground in the window; else the highest
                // ground below it.
                float best = -std::numeric_limits<float>::max(), below = best;
                for (unsigned int i = cell.index, end = cell.index + cell.count; i < end; ++i) {
                    const float top = chf->bmin[1] + float(chf->spans[i].y) * chf->ch;
                    const unsigned char area = chf->areas[i];
                    if (area != AreaFloor && area != AreaGround)
                        continue;
                    if (top >= at[1] - s.windowBelow && top <= at[1] + s.windowAbove && top > best) {
                        best = top;
                        seed.kind = area == AreaFloor ? Seed::OnFloor : Seed::OnGround;
                        seed.position[1] = top;
                    } else if (area == AreaGround && top < at[1] - s.windowBelow && top >= at[1] - s.groundDepth
                               && top > below) {
                        below = top;
                    }
                }
                if (seed.kind == Seed::Missed && below > -std::numeric_limits<float>::max()) {
                    seed.kind = Seed::OnGround;
                    seed.position[1] = below;
                }
            }
            Built::PlatformSeeds &count = out.platforms[p];
            (seed.kind == Seed::OnFloor ? count.floor : seed.kind == Seed::OnGround ? count.ground : count.missed)++;
            out.seeds.push_back(seed);

            // A seed on ground: its stretch walks on a ground strip.
            if (seed.kind == Seed::OnGround) {
                float quad[12];
                stripQuad(line, along - 0.5f * s.seedSpacing, along + 0.5f * s.seedSpacing, platform.side,
                          s.stripFrom, s.stripFrom + s.stripWidth, quad);
                markArea(*chf, quad, 4, at[1] - s.groundDepth - 1.0f, at[1] + s.windowAbove, AreaStrip,
                         [](unsigned char area) { return area == AreaGround; });
            }
        }
    }
    // Ground elsewhere: closed.
    for (int i = 0; i < chf->spanCount; ++i)
        if (chf->areas[i] == AreaGround)
            chf->areas[i] = RC_NULL_AREA;
    if (!rcErodeWalkableArea(&context, walkableRadius, *chf)) {
        out.error = "erosion failed";
        return out;
    }
    // The edge zones.
    for (const Platform &platform : input.platforms) {
        if (platform.line.size() < 6)
            continue;
        const Line line(platform.line);
        for (float along = 0.0f; along < line.length(); along += 1.0f) {
            float quad[12];
            stripQuad(line, along, std::min(along + 1.0f, line.length()), platform.side, s.trackHalfWidth,
                      s.trackHalfWidth + s.edgeWidth, quad);
            float at[3], way[3];
            line.at(along, at, way);
            markArea(*chf, quad, 4, at[1] - s.groundDepth - 1.0f, at[1] + s.windowAbove, AreaEdge,
                     [](unsigned char area) { return area == AreaFloor || area == AreaStrip; });
        }
    }

    if (!rcBuildDistanceField(&context, *chf) || !rcBuildRegions(&context, *chf, 0, 8 * 8, 20 * 20)) {
        out.error = "regions failed";
        return out;
    }
    std::unique_ptr<rcContourSet, ContoursDeleter> contours(rcAllocContourSet());
    std::unique_ptr<rcPolyMesh, PolyMeshDeleter> polys(rcAllocPolyMesh());
    std::unique_ptr<rcPolyMeshDetail, DetailDeleter> detail(rcAllocPolyMeshDetail());
    if (!contours || !polys || !detail
            || !rcBuildContours(&context, *chf, 1.3f, int(12.0f / s.cellSize), *contours)
            || !rcBuildPolyMesh(&context, *contours, 6, *polys)
            || !rcBuildPolyMeshDetail(&context, *polys, *chf, s.cellSize * 6.0f, s.cellHeight, *detail)) {
        out.error = "polygons failed";
        return out;
    }
    if (polys->npolys == 0) {
        out.error = "nothing walkable";
        out.milliseconds = double(timer.nsecsElapsed()) / 1e6;
        return out;
    }
    for (int i = 0; i < polys->npolys; ++i)
        polys->flags[i] = FlagWalk;

    dtNavMeshCreateParams params{};
    params.verts = polys->verts;
    params.vertCount = polys->nverts;
    params.polys = polys->polys;
    params.polyAreas = polys->areas;
    params.polyFlags = polys->flags;
    params.polyCount = polys->npolys;
    params.nvp = polys->nvp;
    params.detailMeshes = detail->meshes;
    params.detailVerts = detail->verts;
    params.detailVertsCount = detail->nverts;
    params.detailTris = detail->tris;
    params.detailTriCount = detail->ntris;
    params.walkableHeight = s.personHeight;
    params.walkableRadius = s.personRadius;
    params.walkableClimb = s.step;
    std::copy(polys->bmin, polys->bmin + 3, params.bmin);
    std::copy(polys->bmax, polys->bmax + 3, params.bmax);
    params.cs = s.cellSize;
    params.ch = s.cellHeight;
    params.buildBvTree = true;
    unsigned char *data = nullptr;
    int size = 0;
    if (!dtCreateNavMeshData(&params, &data, &size)) {
        out.error = "Detour data failed";
        return out;
    }
    std::shared_ptr<dtNavMesh> mesh(dtAllocNavMesh(), dtFreeNavMesh);
    if (!mesh || dtStatusFailed(mesh->init(data, size, DT_TILE_FREE_DATA))) {
        dtFree(data);
        out.error = "Detour mesh failed";
        return out;
    }

    // What the seeds reach.
    std::unique_ptr<dtNavMeshQuery, QueryDeleter> query(dtAllocNavMeshQuery());
    if (!query || dtStatusFailed(query->init(mesh.get(), 2048))) {
        out.error = "Detour query failed";
        return out;
    }
    const dtMeshTile *tile = static_cast<const dtNavMesh *>(mesh.get())->getTile(0);
    const dtPolyRef base = mesh->getPolyRefBase(tile);
    std::vector<char> reached(size_t(tile->header->polyCount), 0);
    std::vector<dtPolyRef> open;
    dtQueryFilter filter;
    for (const Seed &seed : out.seeds) {
        if (seed.kind == Seed::Missed)
            continue;
        const float extents[3] = {1.0f, 0.5f, 1.0f};
        dtPolyRef ref = 0;
        float nearest[3];
        if (dtStatusSucceed(query->findNearestPoly(seed.position, extents, &filter, &ref, nearest)) && ref != 0) {
            const unsigned int index = unsigned(ref - base);
            if (index < reached.size() && !reached[index]) {
                reached[index] = 1;
                open.push_back(ref);
            }
        }
    }
    while (!open.empty()) {
        const dtPolyRef ref = open.back();
        open.pop_back();
        const dtMeshTile *t = nullptr;
        const dtPoly *poly = nullptr;
        mesh->getTileAndPolyByRefUnsafe(ref, &t, &poly);
        for (unsigned int link = poly->firstLink; link != DT_NULL_LINK; link = t->links[link].next) {
            const dtPolyRef next = t->links[link].ref;
            const unsigned int index = unsigned(next - base);
            if (next != 0 && index < reached.size() && !reached[index]) {
                reached[index] = 1;
                open.push_back(next);
            }
        }
    }
    out.polygons = tile->header->polyCount;
    for (int i = 0; i < tile->header->polyCount; ++i) {
        if (reached[size_t(i)])
            ++out.reached;
        else
            mesh->setPolyFlags(base | dtPolyRef(i), 0);
        // Its triangles, from the detail mesh.
        const unsigned int *m = &detail->meshes[i * 4];
        const unsigned char area = polys->areas[i] | (reached[size_t(i)] ? 0 : DroppedFlag);
        for (unsigned int k = 0; k < m[3]; ++k) {
            const unsigned char *tri = &detail->tris[(m[2] + k) * 4];
            for (int j = 0; j < 3; ++j) {
                const float *v = &detail->verts[(m[0] + tri[j]) * 3];
                out.triangles.insert(out.triangles.end(), v, v + 3);
            }
            out.areas.push_back(area);
        }
    }
    out.mesh = mesh;
    out.ok = true;
    out.milliseconds = double(timer.nsecsElapsed()) / 1e6;
    return out;
}

}
