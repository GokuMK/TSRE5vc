#include <tsre/world/TerrainLib.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainMeshBackend.h>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
int worldCell(qint64 metre) {
    return int(std::floor(double(metre + TerrainGridLayout::WorldTileHalfSize)
                          / TerrainGridLayout::WorldTileSize));
}

bool isXEdge(TerrainEdgeSide side) {
    return side == TerrainEdgeSide::LocalX0 || side == TerrainEdgeSide::LocalXMax;
}

// Read only persistent RAW samples. The last interval repeats its last stored
// height unless an already loaded canonical endpoint owner is available.
float rawHeight(const Terrain &terrain, qint64 x, qint64 z) {
    const auto bounds = TerrainPhysicalBounds::of(terrain);
    const auto &layout = terrain.getGridLayout();
    const double sx = std::clamp(double(x - bounds.minX) / layout.sampleSpacing,
                                0.0, double(layout.sampleCount - 1));
    const double sz = std::clamp(double(z - bounds.minZ) / layout.sampleSpacing,
                                0.0, double(layout.sampleCount - 1));
    const int ix = int(sx), iz = int(sz);
    const int nx = std::min(ix + 1, layout.sampleCount - 1);
    const int nz = std::min(iz + 1, layout.sampleCount - 1);
    const double tx = sx - ix, tz = sz - iz;
    return float((terrain.terrainData[iz][ix] * (1 - tx)
                 + terrain.terrainData[iz][nx] * tx) * (1 - tz)
               + (terrain.terrainData[nz][ix] * (1 - tx)
                 + terrain.terrainData[nz][nx] * tx) * tz);
}
}

Terrain *TerrainLib::edgeTerrainAt(int x, int z, bool low, bool load) {
    Terrain *terrain = getTerrainByXY(x, z, load);
    return terrain != nullptr && terrain->lowTile == low ? terrain : nullptr;
}

const TerrainAdjacentEdge &TerrainLib::resolveAdjacentEdge(
        Terrain &owner, TerrainEdgeSide side, TerrainEdgeDiscovery mode) {
    auto &edge = owner.adjacentEdges[size_t(side)];
    edge.side = side;
    edge.ownerLengthM = owner.getGridLayout().terrainWorldSize;
    if (!owner.loaded || owner.terrainData == nullptr || edge.ownerLengthM <= 0)
        return edge;
    if (mode == TerrainEdgeDiscovery::LoadDirectNeighbor
            && edge.discovery == TerrainEdgeDiscovery::LoadedOnly)
        edge.dirty = true;
    // One registry check per section, never per sample. No retained pointers.
    if (!edge.dirty) {
        for (const auto &section : edge.sections) {
            const auto &source = section.source;
            Terrain *t = edgeTerrainAt(source.worldX, source.worldZ,
                                       source.distantDomain, false);
            if (t == nullptr || !t->loaded || t->name != source.name) {
                edge.dirty = true;
                break;
            }
        }
    }
    if (!edge.dirty)
        return edge;

    edge.sections.clear();
    edge.discovery = mode;
    const auto bounds = TerrainPhysicalBounds::of(owner);
    const bool xEdge = isXEdge(side);
    const bool positive = side == TerrainEdgeSide::LocalXMax
            || side == TerrainEdgeSide::LocalZMax;
    const qint64 fixed = xEdge ? (positive ? bounds.maxX : bounds.minX)
                              : (positive ? bounds.maxZ : bounds.minZ);
    const qint64 begin = xEdge ? bounds.minZ : bounds.minX;
    QSet<Terrain*> seen; // local construction scratch only
    struct Candidate { Terrain *terrain; int worldX; int worldZ; };
    QVector<Candidate> candidates;
    for (qint64 offset = 0; offset < edge.ownerLengthM;
         offset += TerrainGridLayout::WorldTileSize) {
        const qint64 along = begin + offset;
        const qint64 probe = positive ? fixed : fixed - 1;
        const int wx = worldCell(xEdge ? probe : along);
        const int wz = worldCell(xEdge ? along : probe);
        Terrain *adjacent = edgeTerrainAt(wx, wz, owner.lowTile,
                           mode == TerrainEdgeDiscovery::LoadDirectNeighbor);
        if (adjacent == nullptr || adjacent == &owner || !adjacent->loaded
                || adjacent->terrainData == nullptr || seen.contains(adjacent))
            continue;
        seen.insert(adjacent);
        candidates.push_back({adjacent, wx, wz});
    }
    // Finish direct loading before reading shared section endpoints. Otherwise
    // the first section could cache a missing height owned by the next section.
    for (const auto &candidate : candidates) {
        Terrain *adjacent = candidate.terrain;
        const auto other = TerrainPhysicalBounds::of(*adjacent);
        const qint64 sourceFixed = xEdge ? (positive ? other.minX : other.maxX)
                                        : (positive ? other.minZ : other.maxZ);
        if (sourceFixed != fixed)
            continue; // unusual overlapping layout: best-effort fallback
        const qint64 otherBegin = xEdge ? other.minZ : other.minX;
        const qint64 otherEnd = xEdge ? other.maxZ : other.maxX;
        const qint64 first = std::max(begin, otherBegin);
        const qint64 last = std::min(begin + edge.ownerLengthM, otherEnd);
        const int step = adjacent->getGridLayout().sampleSpacing;
        if (step <= 0 || first >= last || (first - otherBegin) % step != 0
                || (last - first) % step != 0)
            continue;
        TerrainEdgeSection section;
        section.source = {candidate.worldX, candidate.worldZ, adjacent->name, owner.lowTile,
            xEdge ? (positive ? TerrainEdgeSide::LocalX0 : TerrainEdgeSide::LocalXMax)
                  : (positive ? TerrainEdgeSide::LocalZ0 : TerrainEdgeSide::LocalZMax)};
        section.points.reserve(int((last - first) / step) + 1);
        Terrain *canonical = positive ? adjacent : &owner;
        const auto canonicalBounds = TerrainPhysicalBounds::of(*canonical);
        for (qint64 at = first; at <= last; at += step) {
            const qint64 x = xEdge ? fixed : at;
            const qint64 z = xEdge ? at : fixed;
            float height;
            if (x >= canonicalBounds.minX && x < canonicalBounds.maxX
                    && z >= canonicalBounds.minZ && z < canonicalBounds.maxZ) {
                height = rawHeight(*canonical, x, z);
            } else {
                // Cheap optional corner improvement: only look for an already
                // loaded owner. Never load a diagonal tile or build its edges.
                Terrain *endpoint = edgeTerrainAt(worldCell(x), worldCell(z),
                                                  owner.lowTile, false);
                if (endpoint != nullptr && endpoint->loaded
                        && endpoint->terrainData != nullptr) {
                    const auto eb = TerrainPhysicalBounds::of(*endpoint);
                    height = (x >= eb.minX && x < eb.maxX && z >= eb.minZ && z < eb.maxZ)
                            ? rawHeight(*endpoint, x, z)
                            : std::numeric_limits<float>::quiet_NaN();
                } else {
                    // Keep absence explicit: fallback is evaluated against the
                    // owner's current heights each fill, not frozen in cache.
                    height = std::numeric_limits<float>::quiet_NaN();
                }
            }
            section.points.push_back({int(at - begin), height});
        }
        edge.sections.push_back(std::move(section));
    }
    edge.finish();
    return edge;
}

void TerrainLib::fillCachedRaw(Terrain &terrain) {
    if (!terrain.loaded || terrain.terrainData == nullptr)
        return;
    const int n = terrain.getSampleCount();
    const int spacing = terrain.getGridLayout().sampleSpacing;
    // Missing data is retried on a real fill/refresh, not on every LOD query.
    for (auto &edge : terrain.adjacentEdges)
        if (edge.status != TerrainAdjacentEdge::Status::Complete)
            edge.dirty = true;
    const auto &xEdge = resolveAdjacentEdge(terrain, TerrainEdgeSide::LocalXMax,
                                           TerrainEdgeDiscovery::LoadDirectNeighbor);
    const auto &zEdge = resolveAdjacentEdge(terrain, TerrainEdgeSide::LocalZMax,
                                           TerrainEdgeDiscovery::LoadDirectNeighbor);
    auto fill = [&](const TerrainAdjacentEdge &edge, bool x) {
        TerrainAdjacentEdge::Cursor cursor;
        int firstChanged = n, lastChanged = -1;
        for (int i = 0; i < n; ++i) {
            float h = x ? terrain.terrainData[i][n - 1] : terrain.terrainData[n - 1][i];
            float sampled;
            if (edge.sampleHeight(i * spacing, sampled, cursor))
                h = sampled;
            float &destination = x ? terrain.terrainData[i][n] : terrain.terrainData[n][i];
            if (destination != h) {
                destination = h;
                firstChanged = std::min(firstChanged, i);
                lastChanged = i;
            }
        }
        if (lastChanged >= firstChanged)
            terrain.invalidateSynthesizedSamples(x ? n : firstChanged,
                                                  x ? firstChanged : n,
                                                  x ? n : lastChanged,
                                                  x ? lastChanged : n,
                                                  TerrainDirtyHeight | TerrainDirtyNormals);
    };
    fill(xEdge, true);
    fill(zEdge, false);
    float hx, hz;
    const bool hasX = xEdge.sampleHeight(n * spacing, hx);
    const bool hasZ = zEdge.sampleHeight(n * spacing, hz);
    float corner = terrain.terrainData[n - 1][n - 1];
    if (hasX && (!hasZ || hx == hz))
        corner = hx;
    else if (hasZ && !hasX)
        corner = hz;
    if (terrain.terrainData[n][n] != corner) {
        terrain.terrainData[n][n] = corner;
        terrain.invalidateSynthesizedSamples(n, n, n, n,
                                             TerrainDirtyHeight | TerrainDirtyNormals);
    }
}

void TerrainLib::terrainAvailabilityChanged(Terrain *source) {
    if (source == nullptr || source->getGridLayout().terrainWorldSize <= 0)
        return;
    for (auto &edge : source->adjacentEdges)
        edge.dirty = true;
    if (source->loaded) {
        const int n = source->getSampleCount();
        source->invalidateSynthesizedSamples(n, 0, n, n,
                                              TerrainDirtyHeight | TerrainDirtyNormals);
        source->invalidateSynthesizedSamples(0, n, n, n,
                                              TerrainDirtyHeight | TerrainDirtyNormals);
    }
    const auto b = TerrainPhysicalBounds::of(*source);
    QSet<Terrain*> neighbours;
    auto visit = [&](qint64 x, qint64 z) {
        Terrain *t = edgeTerrainAt(worldCell(x), worldCell(z), source->lowTile, false);
        if (t != nullptr && t != source && t->loaded)
            neighbours.insert(t);
    };
    for (qint64 x = b.minX; x < b.maxX; x += TerrainGridLayout::WorldTileSize) {
        visit(x, b.minZ - 1);
        visit(x, b.maxZ);
    }
    for (qint64 z = b.minZ; z < b.maxZ; z += TerrainGridLayout::WorldTileSize) {
        visit(b.minX - 1, z);
        visit(b.maxX, z);
    }
    visit(b.minX - 1, b.minZ - 1);
    visit(b.maxX, b.minZ - 1);
    visit(b.minX - 1, b.maxZ);
    visit(b.maxX, b.maxZ);
    for (Terrain *t : neighbours) {
        // Availability events are rare; invalidate complete border strips.
        // No route scan and no vertex/normal generation during notification.
        for (auto &edge : t->adjacentEdges)
            edge.dirty = true;
        const int n = t->getSampleCount();
        t->invalidateSynthesizedSamples(n, 0, n, n,
                                         TerrainDirtyHeight | TerrainDirtyNormals);
        t->invalidateSynthesizedSamples(0, n, n, n,
                                         TerrainDirtyHeight | TerrainDirtyNormals);
    }
}
