#include <tsre/world/TerrainAdjacentEdge.h>
#include <tsre/world/Terrain.h>
#include <algorithm>
#include <cmath>

TerrainPhysicalBounds TerrainPhysicalBounds::of(const Terrain &terrain) {
    const qint64 size = terrain.getGridLayout().terrainWorldSize;
    const qint64 x = qint64(terrain.mojex) * TerrainGridLayout::WorldTileSize
            - TerrainGridLayout::WorldTileHalfSize;
    const qint64 z = qint64(terrain.mojez) * TerrainGridLayout::WorldTileSize
            + TerrainGridLayout::WorldTileHalfSize;
    return {x, z - size, x + size, z};
}

int TerrainEdgeSection::nativeSpacing() const {
    if (points.size() < 2)
        return 0;
    const int step = points[1].alongM - points[0].alongM;
    return std::max(0, step);
}

int TerrainEdgeSection::sourcePatchAt(const Terrain &owner,
                                      const Terrain &sourceTerrain,
                                      int alongM) const {
    if (points.isEmpty() || alongM < firstAlongM() || alongM > lastAlongM()
            || sourceTerrain.name != source.name
            || sourceTerrain.lowTile != source.distantDomain)
        return -1;
    const auto a = TerrainPhysicalBounds::of(owner);
    const auto b = TerrainPhysicalBounds::of(sourceTerrain);
    const auto &layout = sourceTerrain.getGridLayout();
    if (layout.patchWorldSize <= 0)
        return -1;
    const bool xEdge = source.sourceSide == TerrainEdgeSide::LocalX0
            || source.sourceSide == TerrainEdgeSide::LocalXMax;
    const qint64 local = (xEdge ? a.minZ - b.minZ : a.minX - b.minX) + alongM;
    const int patch = int(std::clamp<qint64>(local / layout.patchWorldSize,
                                            0, layout.patchesPerSide - 1));
    switch (source.sourceSide) {
    case TerrainEdgeSide::LocalX0: return patch * layout.patchesPerSide;
    case TerrainEdgeSide::LocalXMax: return (patch + 1) * layout.patchesPerSide - 1;
    case TerrainEdgeSide::LocalZ0: return patch;
    case TerrainEdgeSide::LocalZMax:
        return (layout.patchesPerSide - 1) * layout.patchesPerSide + patch;
    }
    return -1;
}

void TerrainAdjacentEdge::finish() {
    sections.erase(std::remove_if(sections.begin(), sections.end(),
                                 [](const TerrainEdgeSection &s) {
        const int step = s.nativeSpacing();
        if (step == 0) return true;
        for (int i = 2; i < s.points.size(); ++i)
            if (s.points[i].alongM - s.points[i - 1].alongM != step)
                return true;
        return false;
    }), sections.end());
    std::sort(sections.begin(), sections.end(), [](const TerrainEdgeSection &a,
                                                  const TerrainEdgeSection &b) {
        return a.firstAlongM() < b.firstAlongM();
    });
    status = sections.isEmpty() ? Status::Missing : Status::Complete;
    int end = 0;
    for (int i = 0; i < sections.size(); ++i) {
        const auto &s = sections[i];
        if (s.firstAlongM() > end && status != Status::Conflict)
            status = Status::Partial;
        if (s.firstAlongM() < end)
            status = Status::Conflict;
        if (i > 0 && sections[i - 1].lastAlongM() == s.firstAlongM()
                && sections[i - 1].points.last().height != s.points.first().height)
            status = Status::Conflict;
        end = std::max(end, s.lastAlongM());
    }
    if (end < ownerLengthM && status == Status::Complete)
        status = Status::Partial;
    dirty = false;
}

bool TerrainAdjacentEdge::sampleHeight(int alongM, float &height) const {
    Cursor cursor;
    return sampleHeight(alongM, height, cursor);
}

bool TerrainAdjacentEdge::sampleHeight(int alongM, float &height, Cursor &c) const {
    if (alongM < 0 || alongM > ownerLengthM)
        return false;
    while (c.section < sections.size() && sections[c.section].lastAlongM() < alongM) {
        ++c.section;
        c.point = 0;
    }
    if (c.section >= sections.size())
        return false;
    const auto &s = sections[c.section];
    if (alongM < s.firstAlongM())
        return false;
    // A malformed overlap must not interpolate between unrelated sources.
    if (c.section + 1 < sections.size()) {
        const auto &next = sections[c.section + 1];
        if (next.firstAlongM() <= alongM) {
            if (next.firstAlongM() < s.lastAlongM()
                    || s.points.last().height != next.points.first().height)
                return false;
            ++c.section;
            c.point = 0;
            return sampleHeight(alongM, height, c);
        }
    }
    while (c.point + 1 < s.points.size() && s.points[c.point + 1].alongM <= alongM)
        ++c.point;
    const auto &p = s.points[c.point];
    if (p.alongM == alongM) {
        height = p.height;
    } else {
        if (c.point + 1 >= s.points.size())
            return false;
        const auto &q = s.points[c.point + 1];
        const float t = float(alongM - p.alongM) / float(q.alongM - p.alongM);
        height = p.height + t * (q.height - p.height);
    }
    return std::isfinite(height);
}
