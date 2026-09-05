#include <tsre/world/TerrainHeightBrush.h>
#include <tsre/world/TerrainHeightArea.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainLibQt.h>
#include <tsre/world/TerrainAdjacentEdge.h>
#include <tsre/texture/Brush.h>
#include <tsre/Game.h>
#include <QElapsedTimer>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
using TerrainHeightArea::Slice;
int worldCell(qint64 metre) { return int(std::floor(double(metre + 1024) / 2048)); }
int ceilSamples(qint64 metres, int spacing) { return int(std::ceil(double(metres) / spacing)); }
int floorSamples(qint64 metres, int spacing) { return int(std::floor(double(metres) / spacing)); }
bool supportedSpacing(int s) { return s > 0 && s <= 1024 && (s & (s - 1)) == 0; }

float apply(float value, float distance, float radius, const Brush &brush, float reference) {
    float h = radius > 0 ? (radius - distance) / radius : 1.0f;
    h = h * brush.alpha * brush.direction * 10.0;
    if (brush.hType == 0) value += h;
    else if (brush.hType == 1) {
        if (h < 0 && value > reference) value += h;
        if (h > 0 && value < reference) value += h;
    } else {
        // Preserve the original two independent comparisons and float order.
        if (value > reference) {
            value -= h * brush.direction;
            if (value < reference) value = reference;
        }
        if (value < reference) {
            value += h * brush.direction;
            if (value > reference) value = reference;
        }
    }
    return value;
}
}

QSet<Terrain*> TerrainHeightBrush::paint(TerrainLibQt &library, Brush &brush,
        int worldX, int worldZ, float *position, Method method,
        bool allowMixedResolution, Timings *timings) {
    if (timings) *timings = {};
    QElapsedTimer timer;
    timer.start();
    auto elapsed = [&] { const double ms = timer.nsecsElapsed() / 1e6; timer.restart(); return ms; };
    QSet<Terrain*> result;
    if (position == nullptr || brush.size < 0 || brush.hType < 0 || brush.hType > 3) return result;
    const int inputWorldX = worldX, inputWorldZ = worldZ;
    float px = position[0], pz = position[2];
    Game::check_coords(worldX, worldZ, px, pz);
    Terrain *start = library.getTerrainByXY(worldX, worldZ);
    if (!start || !start->loaded || !start->isEditable()) return result;
    const int originalSpacing = start->getSampleSize();
    const float radius = brush.size * 8.0f;
    // Discover World cells, not brush samples. A small margin covers centre
    // snapping without a second lookup pass. Larger terrain is deduplicated.
    const double unsnappedX = double(worldX) * 2048 + px;
    const double unsnappedZ = double(worldZ) * 2048 + pz;
    QSet<Terrain*> seen;
    std::vector<Terrain*> candidates;
    int step = originalSpacing;
    bool aligned = supportedSpacing(step);
    bool skippedMixed = false;
    for (int wz = worldCell(qint64(std::floor(unsnappedZ - radius - originalSpacing)));
         wz <= worldCell(qint64(std::ceil(unsnappedZ + radius + originalSpacing))); ++wz) {
        for (int wx = worldCell(qint64(std::floor(unsnappedX - radius - originalSpacing)));
             wx <= worldCell(qint64(std::ceil(unsnappedX + radius + originalSpacing))); ++wx) {
            Terrain *t = library.getTerrainByXY(wx, wz);
            if (!t || !t->loaded || !t->isEditable() || seen.contains(t)) continue;
            seen.insert(t);
            const int spacing = t->getSampleSize();
            if (!allowMixedResolution && spacing != originalSpacing) {
                skippedMixed = true;
                continue;
            }
            candidates.push_back(t);
            step = std::min(step, spacing);
            aligned &= supportedSpacing(spacing);
        }
    }
    if (skippedMixed) qWarning() << "Skipping mixed-resolution terrain brush seam";
    if (!aligned) {
        // Preserve loadability; the optimized paths target aligned power-of-two
        // grids. Do not silently invent a new lattice for unusual layouts.
        if (timings) timings->fallback = true;
        return library.paintHeightMapLegacy(&brush, inputWorldX, inputWorldZ, position);
    }
    px = std::round(px / step) * step;
    pz = std::round(pz / step) * step;
    const qint64 cx = qint64(worldX) * 2048 + qint64(px);
    const qint64 cz = qint64(worldZ) * 2048 + qint64(pz);
    const int cells = int(std::ceil(radius / step));
    const qint64 ox = cx - qint64(cells) * step, oz = cz - qint64(cells) * step;
    const int side = cells * 2 + 1;
    std::vector<Slice> slices;
    for (Terrain *t : candidates) {
        const auto b = TerrainPhysicalBounds::of(*t);
        const auto &g = t->getGridLayout();
        Slice s{t, b,
            std::max(0, ceilSamples(ox - b.minX, g.sampleSpacing)),
            std::max(0, ceilSamples(oz - b.minZ, g.sampleSpacing)),
            std::min(g.sampleCount - 1, floorSamples(cx + qint64(cells) * step - b.minX, g.sampleSpacing)),
            std::min(g.sampleCount - 1, floorSamples(cz + qint64(cells) * step - b.minZ, g.sampleSpacing))};
        // Include the original owner for mode 1's special centre, even if
        // snapping landed on its synthesized N edge (legacy behaviour).
        if ((s.x0 > s.x1 || s.z0 > s.z1) && t != start) continue;
        s.patches.resize(g.patchRecordCount(), 0);
        slices.push_back(std::move(s));
    }
    std::sort(slices.begin(), slices.end(), [](const Slice &a, const Slice &b) {
        return a.bounds.minZ < b.bounds.minZ;
    });
    auto gridDistance = [&](int dx, int dz) {
        return std::sqrt(float(qint64(dx) * dx + qint64(dz) * dz)) * step;
    };
    // Native samples only. No World/QuadTree lookup or Qt-container insertion
    // in these loops; row-major mutation accesses height memory contiguously.
    auto visit = [&](Slice &s, auto &&operation) {
        const int spacing = s.terrain->getSampleSize();
        const int stride = spacing / step;
        const int gxBase = int((s.bounds.minX - ox) / step);
        const int gzBase = int((s.bounds.minZ - oz) / step);
        for (int z = s.z0; z <= s.z1; ++z) {
            const int gz = gzBase + z * stride;
            for (int x = s.x0; x <= s.x1; ++x) {
                const int gx = gxBase + x * stride;
                const float d = gridDistance(gx - cells, gz - cells);
                if (d > radius || (brush.hType == 1 && gx == cells && gz == cells)) continue;
                operation(x, z, gx, gz, d);
            }
        }
    };
    for (auto &s : slices) if (s.terrain == start) s.capture();
    float reference = brush.hFixed;
    if (brush.hType == 1) {
        float lx = px, lz = pz;
        start->getLocalCoords(worldX, worldZ, lx, lz);
        const int x = std::clamp(int(std::floor(lx / originalSpacing)), 0, start->getSampleCount());
        const int z = std::clamp(int(std::floor(lz / originalSpacing)), 0, start->getSampleCount());
        for (auto &s : slices) if (s.terrain == start) {
            s.touch(x, z);
            const float old = start->terrainData[z][x];
            start->terrainData[z][x] += float(brush.alpha * brush.direction * 10.0);
            reference = start->terrainData[z][x];
            if (old != reference) s.changed(x, z);
        }
    } else if (brush.hType == 3) {
        // Preserve legacy X-major/Z-minor accumulation exactly on equal grids.
        // Only the column-level loop walks slices; no per-sample lookup.
        float sum = 0;
        int count = 0;
        for (int gx = 0; gx < side; ++gx) {
            const qint64 wx = ox + qint64(gx) * step;
            for (auto &s : slices) {
                const int spacing = s.terrain->getSampleSize();
                const qint64 local = wx - s.bounds.minX;
                if (local < 0 || local % spacing != 0) continue;
                const int x = int(local / spacing);
                if (x < s.x0 || x > s.x1) continue;
                for (int z = s.z0; z <= s.z1; ++z) {
                    if (gridDistance(gx - cells, int((s.bounds.minZ + qint64(z) * spacing - cz) / step)) > radius) continue;
                    sum += s.terrain->terrainData[z][x];
                    ++count;
                }
            }
        }
        reference = count ? sum / count : 0;
    }
    const double preparation = elapsed();
    if (timings) timings->prepareMs = preparation;

    if (method == Method::DirectSlices) {
        for (auto &s : slices) visit(s, [&](int x, int z, int, int, float d) {
            s.touch(x, z);
            float &height = s.terrain->terrainData[z][x];
            const float old = height;
            height = apply(old, d, radius, brush, reference);
            if (height != old) s.changed(x, z);
        });
        const double editing = elapsed();
        if (timings) timings->editMs = editing;
    } else {
        // Reusable get/edit/set example. Preparation already selected the native
        // slices, including the mode-1 centre's undo/dirty bookkeeping.
        auto area = TerrainHeightArea::getArea(ox, oz, side, side, step, std::move(slices));
        area.touched.resize(area.heights.size(), 0);
        if (timings) timings->bufferBytes = area.heights.size() * sizeof(float);
        const double gathering = elapsed();
        if (timings) timings->gatherMs = gathering;
        for (int z = 0; z < area.height; ++z) for (int x = 0; x < area.width; ++x) {
            const std::size_t i = std::size_t(z) * area.width + x;
            float &height = area.heights[i];
            const float d = gridDistance(x - cells, z - cells);
            if (!std::isfinite(height) || d > radius || (brush.hType == 1 && x == cells && z == cells)) continue;
            height = apply(height, d, radius, brush, reference);
            area.touched[i] = 1; // preserve legacy zero-change footprint semantics
        }
        const double editing = elapsed();
        if (timings) timings->editMs = editing;
        result = TerrainHeightArea::setArea(library, area);
        const double scatteringAndCommit = elapsed();
        if (timings) timings->scatterMs = scatteringAndCommit;
    }
    if (method == Method::DirectSlices)
        result = TerrainHeightArea::commitSlices(library, slices);
    const double committing = elapsed();
    if (timings) timings->commitMs = committing;
    return result;
}
