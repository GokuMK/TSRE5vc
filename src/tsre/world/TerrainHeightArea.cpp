#include <tsre/world/TerrainHeightArea.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainLibQt.h>
#include <tsre/world/TerrainMeshBackend.h>
#include <tsre/Undo.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
bool supportedSpacing(int spacing) {
    return spacing > 0 && spacing <= 1024 && (spacing & (spacing - 1)) == 0;
}
int floorSamples(qint64 metres, int spacing) { return int(std::floor(double(metres) / spacing)); }
int ceilSamples(qint64 metres, int spacing) { return int(std::ceil(double(metres) / spacing)); }
}

void TerrainHeightArea::Slice::capture() {
    if (captured) return;
    Undo::PushTerrainHeightMap(terrain->mojex, terrain->mojez,
                               terrain->terrainData, terrain->getSampleCount());
    captured = true;
}
TerrainHeightArea::Area TerrainHeightArea::getArea(TerrainLibQt &library,
        qint64 minX, qint64 minZ, qint64 maxX, qint64 maxZ, int spacingFilter) {
    if (minX > maxX || minZ > maxZ || spacingFilter < 0) return {};
    std::vector<Terrain*> candidates;
    QSet<Terrain*> seen;
    int spacing = 0;
    auto discover = [&](qint64 x0, qint64 z0, qint64 x1, qint64 z1) {
      for (int wz = floorSamples(z0 + 1024, 2048); wz <= floorSamples(z1 + 1024, 2048); ++wz)
        for (int wx = floorSamples(x0 + 1024, 2048); wx <= floorSamples(x1 + 1024, 2048); ++wx) {
            Terrain *t = library.getTerrainByXY(wx, wz);
            if (!t || !t->loaded || !t->isEditable() || seen.contains(t)) continue;
            seen.insert(t);
            const int s = t->getSampleSize();
            if (spacingFilter && s != spacingFilter) continue;
            if (!supportedSpacing(s)) return false;
            spacing = spacing ? std::min(spacing, s) : s;
            candidates.push_back(t);
        }
      return true;
    };
    if (!discover(minX, minZ, maxX, maxZ) || !spacing) return {};
    // Outward rounding can reach a neighbouring World cell exactly at +1024.
    // Discover those owners too. A newly found finer grid only shrinks these
    // rounded bounds, so no repeated convergence loop is necessary.
    if (!discover(qint64(floorSamples(minX, spacing)) * spacing,
                  qint64(floorSamples(minZ, spacing)) * spacing,
                  qint64(ceilSamples(maxX, spacing)) * spacing,
                  qint64(ceilSamples(maxZ, spacing)) * spacing)) return {};
    const qint64 ox = qint64(floorSamples(minX, spacing)) * spacing;
    const qint64 oz = qint64(floorSamples(minZ, spacing)) * spacing;
    const int width = ceilSamples(maxX - ox, spacing) + 1;
    const int height = ceilSamples(maxZ - oz, spacing) + 1;
    std::vector<Slice> slices;
    for (Terrain *t : candidates) {
        const auto b = TerrainPhysicalBounds::of(*t);
        const auto &g = t->getGridLayout();
        Slice s{t, b,
            std::max(0, ceilSamples(ox - b.minX, g.sampleSpacing)),
            std::max(0, ceilSamples(oz - b.minZ, g.sampleSpacing)),
            std::min(g.sampleCount - 1, floorSamples(ox + qint64(width - 1) * spacing - b.minX, g.sampleSpacing)),
            std::min(g.sampleCount - 1, floorSamples(oz + qint64(height - 1) * spacing - b.minZ, g.sampleSpacing))};
        s.patches.resize(g.patchRecordCount(), 0);
        slices.push_back(std::move(s));
    }
    return getArea(ox, oz, width, height, spacing, std::move(slices));
}

TerrainHeightArea::Area TerrainHeightArea::getArea(qint64 ox, qint64 oz,
        int width, int height, int spacing, std::vector<Slice> slices) {
    Area area;
    if (width <= 0 || height <= 0 || !supportedSpacing(spacing)) return area;
    for (const auto &s : slices) {
        const int native = s.terrain->getSampleSize();
        if (!supportedSpacing(native) || native < spacing
                || (s.bounds.minX - ox) % spacing || (s.bounds.minZ - oz) % spacing) return area;
    }
    area.originX = ox; area.originZ = oz;
    area.width = width; area.height = height; area.spacing = spacing;
    area.slices = std::move(slices);
    area.heights.assign(std::size_t(width) * height, std::numeric_limits<float>::quiet_NaN());
    for (const auto &s : area.slices) {
        const auto &g = s.terrain->getGridLayout();
        if (g.sampleSpacing == spacing) {
            const int gxBase = int((s.bounds.minX - ox) / spacing);
            const int gzBase = int((s.bounds.minZ - oz) / spacing);
            if (s.x0 <= s.x1) for (int z = s.z0; z <= s.z1; ++z)
                std::copy_n(s.terrain->terrainData[z] + s.x0, s.x1 - s.x0 + 1,
                            area.heights.data() + std::size_t(gzBase + z) * width + gxBase + s.x0);
            continue;
        }
        const int gx0 = std::max(0, ceilSamples(s.bounds.minX - ox, spacing));
        const int gz0 = std::max(0, ceilSamples(s.bounds.minZ - oz, spacing));
        const int gx1 = std::min(width - 1, floorSamples(s.bounds.maxX - 1 - ox, spacing));
        const int gz1 = std::min(height - 1, floorSamples(s.bounds.maxZ - 1 - oz, spacing));
        for (int gz = gz0; gz <= gz1; ++gz) {
            const qint64 localZ = oz + qint64(gz) * spacing - s.bounds.minZ;
            const int z = int(localZ / g.sampleSpacing);
            const float tz = float(localZ % g.sampleSpacing) / g.sampleSpacing;
            for (int gx = gx0; gx <= gx1; ++gx) {
                const qint64 localX = ox + qint64(gx) * spacing - s.bounds.minX;
                const int x = int(localX / g.sampleSpacing);
                const float tx = float(localX % g.sampleSpacing) / g.sampleSpacing;
                float **raw = s.terrain->terrainData;
                float value = raw[z][x];
                if (tx != 0 || tz != 0) {
                    const float a = raw[z][x] + tx * (raw[z][x + 1] - raw[z][x]);
                    const float b = raw[z + 1][x] + tx * (raw[z + 1][x + 1] - raw[z + 1][x]);
                    value = a + tz * (b - a);
                }
                area.heights[std::size_t(gz) * width + gx] = value;
            }
        }
    }
    area.supported = true;
    return area;
}

QSet<Terrain*> TerrainHeightArea::setArea(TerrainLibQt &library, Area &area) {
    if (!area.supported || area.committed
            || area.heights.size() != std::size_t(area.width) * area.height
            || (!area.touched.empty() && area.touched.size() != area.heights.size())) return {};
    area.committed = true;
    for (auto &s : area.slices) {
        if (!s.terrain->loaded || !s.terrain->isEditable()) continue;
        const int stride = s.terrain->getSampleSize() / area.spacing;
        const int gxBase = int((s.bounds.minX - area.originX) / area.spacing);
        const int gzBase = int((s.bounds.minZ - area.originZ) / area.spacing);
        for (int z = s.z0; z <= s.z1; ++z) {
            const int gz = gzBase + z * stride;
            for (int x = s.x0; x <= s.x1; ++x) {
                const std::size_t i = std::size_t(gz) * area.width + gxBase + x * stride;
                if (!area.touched.empty() && !area.touched[i]) continue;
                const float value = area.heights[i];
                if (!std::isfinite(value)) continue;
                float &height = s.terrain->terrainData[z][x];
                if (height == value && area.touched.empty()) continue;
                s.touch(x, z); // snapshot before the first native write
                if (height != value) { height = value; s.changed(x, z); }
            }
        }
    }
    return commitSlices(library, area.slices);
}

QSet<Terrain*> TerrainHeightArea::commitSlices(TerrainLibQt &library, std::vector<Slice> &slices) {
    QSet<Terrain*> result;
    for (auto &s : slices) {
        if (!s.touched || !s.terrain->loaded || !s.terrain->isEditable()) continue;
        for (int patch = 0; patch < int(s.patches.size()); ++patch)
            if (s.patches[patch]) s.terrain->setPatchErrorBias(patch, 0);
        s.terrain->setModified(true);
        if (s.dirtyX1 >= 0) {
            s.terrain->invalidateSamples(s.dirtyX0, s.dirtyZ0, s.dirtyX1, s.dirtyZ1,
                                         TerrainDirtyHeight | TerrainDirtyNormals);
            s.terrain->refreshModified();
        }
        result.insert(s.terrain);
        library.updateTerrainHeightmap(s.terrain);
    }
    return result;
}
