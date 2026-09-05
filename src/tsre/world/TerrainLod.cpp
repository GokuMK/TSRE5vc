/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/world/TerrainLod.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <QStringList>

#include <tsre/world/TerrainGridLayout.h>

namespace {

bool isSupportedSpacing(int spacing) {
    return spacing == 1 || spacing == 2 || spacing == 4
            || spacing == 8 || spacing == 16 || spacing == 32;
}

}

const QVector<TerrainLodLevel> &TerrainLod::defaultProfile() {
    static const QVector<TerrainLodLevel> levels = {
        {1, 500}, {2, 1000}, {4, 2000}, {8, 8000}
    };
    return levels;
}

QString TerrainLod::profileSummary(const QVector<TerrainLodLevel> &levels) {
    if (levels.isEmpty())
        return "Not set; using TSRE default.";
    QStringList parts;
    parts.append("0");
    for (const TerrainLodLevel &level : levels) {
        parts.append(QString("%1 m").arg(level.sampleSpacing));
        parts.append(QString("%1 m").arg(level.preferredEndDistance));
    }
    return parts.join(" - ");
}

bool TerrainLod::validateProfile(const QVector<TerrainLodLevel> &levels,
                                 QString *error, bool allowEmpty) {
    auto fail = [&](const QString &message) {
        if (error != nullptr)
            *error = message;
        return false;
    };
    if (levels.isEmpty()) {
        if (!allowEmpty)
            return fail("terrain LOD profile contains no levels");
        if (error != nullptr)
            error->clear();
        return true;
    }
    if (levels.size() > 6)
        return fail("terrain LOD profile contains more than six levels");

    int previousSpacing = 0;
    int previousEnd = 0;
    for (int index = 0; index < levels.size(); ++index) {
        const TerrainLodLevel &level = levels[index];
        if (!isSupportedSpacing(level.sampleSpacing))
            return fail(QString("terrain LOD level %1 has unsupported %2 m spacing")
                        .arg(index + 1).arg(level.sampleSpacing));
        if (level.preferredEndDistance <= 0)
            return fail(QString("terrain LOD level %1 has a non-positive preferred end distance")
                        .arg(index + 1));
        if (index > 0 && level.sampleSpacing != previousSpacing * 2)
            return fail(QString("terrain LOD level %1 must be the next coarser supported spacing")
                        .arg(index + 1));
        if (index > 0 && level.preferredEndDistance <= previousEnd)
            return fail(QString("terrain LOD level %1 preferred end distance is not increasing")
                        .arg(index + 1));
        previousSpacing = level.sampleSpacing;
        previousEnd = level.preferredEndDistance;
    }
    if (error != nullptr)
        error->clear();
    return true;
}

int TerrainLod::requestedSampleSpacing(
        const QVector<TerrainLodLevel> &levels, float squaredDistance) {
    if (levels.isEmpty() || !std::isfinite(squaredDistance))
        return 0;
    squaredDistance = std::max(0.0f, squaredDistance);
    for (const TerrainLodLevel &level : levels) {
        const double end = level.preferredEndDistance;
        if (static_cast<double>(squaredDistance) <= end * end)
            return level.sampleSpacing;
    }
    // The last preferred end is not a draw cutoff. Ordinary terrain
    // visibility decides where rendering stops.
    return levels.last().sampleSpacing;
}

QVector<int> TerrainLod::availableSourceSteps(
        const TerrainGridLayout &layout) {
    QVector<int> result;
    if (layout.sampleSpacing <= 0 || layout.patchResolution <= 0)
        return result;
    for (int step = 1; step <= layout.patchResolution; step *= 2) {
        if (layout.patchResolution % step != 0)
            break;
        const int effectiveSpacing = layout.sampleSpacing * step;
        if (effectiveSpacing > MaximumSampleSpacing)
            break;
        result.append(step);
        if (step > layout.patchResolution / 2)
            break;
    }
    return result;
}

int TerrainLod::sourceStepForRequest(const TerrainGridLayout &layout,
                                     int requestedSpacing) {
    const QVector<int> steps = availableSourceSteps(layout);
    if (steps.isEmpty() || requestedSpacing <= 0)
        return 1;
    int selected = steps.first();
    for (int step : steps) {
        if (layout.sampleSpacing * step > requestedSpacing)
            break;
        selected = step;
    }
    return selected;
}

QVector<TerrainPatchLodState> TerrainLod::buildTileState(
        const TerrainGridLayout &layout,
        const QVector<TerrainLodLevel> &levels,
        float cameraLocalX, float cameraLocalZ,
        const QVector<quint8> &patchHasGap,
        bool *profileViolation, bool pinOuterRing) {
    const int patches = layout.patchesPerSide;
    const int count = layout.patchRecordCount();
    QVector<TerrainPatchLodState> result(count);
    if (profileViolation != nullptr)
        *profileViolation = false;
    if (count <= 0)
        return result;

    for (TerrainPatchLodState &state : result)
        state.effectiveSampleSpacing = layout.sampleSpacing;
    const QVector<int> sourceSteps = availableSourceSteps(layout);
    if (levels.isEmpty() || sourceSteps.isEmpty()
            || !std::isfinite(cameraLocalX) || !std::isfinite(cameraLocalZ))
        return result;
    auto stepForRequest = [&](int requestedSpacing) {
        int selected = sourceSteps.first();
        for (int step : sourceSteps) {
            if (layout.sampleSpacing * step > requestedSpacing)
                break;
            selected = step;
        }
        return selected;
    };

    QVector<quint8> pinned(count, 0);
    for (int row = 0; row < patches; ++row) {
        for (int column = 0; column < patches; ++column) {
            const int patchId = layout.patchIndex(row, column);
            const float centerX = (column + 0.5f) * layout.patchWorldSize;
            const float centerZ = (row + 0.5f) * layout.patchWorldSize;
            const float dx = centerX - cameraLocalX;
            const float dz = centerZ - cameraLocalZ;
            const int requested = requestedSampleSpacing(
                        levels, dx * dx + dz * dz);
            result[patchId].sourceStep = stepForRequest(requested);
            result[patchId].effectiveSampleSpacing = layout.sampleSpacing
                    * result[patchId].sourceStep;
            if (pinOuterRing && (row == 0 || column == 0
                    || row == patches - 1 || column == patches - 1))
                pinned[patchId] = 1;
        }
    }

    if (profileViolation != nullptr) {
        for (int row = 0; row < patches; ++row) {
            for (int column = 0; column < patches; ++column) {
                const int patchId = layout.patchIndex(row, column);
                const int neighbours[2][2] = {
                    {row, column + 1}, {row + 1, column}
                };
                for (const auto &neighbour : neighbours) {
                    if (neighbour[0] >= patches || neighbour[1] >= patches)
                        continue;
                    const int neighbourId = layout.patchIndex(
                                neighbour[0], neighbour[1]);
                    const int lower = std::min(result[patchId].sourceStep,
                                               result[neighbourId].sourceStep);
                    const int higher = std::max(result[patchId].sourceStep,
                                                result[neighbourId].sourceStep);
                    if (higher > lower * 2)
                        *profileViolation = true;
                }
            }
        }
    }

    // Gap vertices can disappear from a decimated topology. Keep the gap patch
    // and its direct neighbours native until hole-aware LOD meshes exist.
    for (int patchId = 0; patchId < count; ++patchId) {
        if (patchId >= patchHasGap.size() || !patchHasGap[patchId])
            continue;
        const int row = layout.patchRow(patchId);
        const int column = layout.patchColumn(patchId);
        const int neighbours[5][2] = {
            {row, column}, {row - 1, column}, {row + 1, column},
            {row, column - 1}, {row, column + 1}
        };
        for (const auto &neighbour : neighbours) {
            if (neighbour[0] < 0 || neighbour[0] >= patches
                    || neighbour[1] < 0 || neighbour[1] >= patches)
                continue;
            pinned[layout.patchIndex(neighbour[0], neighbour[1])] = 1;
        }
    }
    for (int patchId = 0; patchId < count; ++patchId) {
        if (!pinned[patchId])
            continue;
        result[patchId].sourceStep = 1;
        result[patchId].effectiveSampleSpacing = layout.sampleSpacing;
    }

    refineTileState(layout, result);
    return result;
}

bool TerrainLod::refineTileState(const TerrainGridLayout &layout,
                                QVector<TerrainPatchLodState> &result) {
    const int patches = layout.patchesPerSide;
    bool refined = false;
    // Refine only the coarser side until every internal edge is 1:1 or 2:1.
    bool changed = true;
    while (changed) {
        changed = false;
        for (int row = 0; row < patches; ++row) {
            for (int column = 0; column < patches; ++column) {
                const int patchId = layout.patchIndex(row, column);
                const int neighbours[2][2] = {
                    {row, column + 1}, {row + 1, column}
                };
                for (const auto &neighbour : neighbours) {
                    if (neighbour[0] >= patches || neighbour[1] >= patches)
                        continue;
                    const int neighbourId = layout.patchIndex(
                                neighbour[0], neighbour[1]);
                    int &first = result[patchId].sourceStep;
                    int &second = result[neighbourId].sourceStep;
                    if (first > second * 2) {
                        first = second * 2;
                        result[patchId].effectiveSampleSpacing =
                                layout.sampleSpacing * first;
                        changed = refined = true;
                    } else if (second > first * 2) {
                        second = first * 2;
                        result[neighbourId].effectiveSampleSpacing =
                                layout.sampleSpacing * second;
                        changed = refined = true;
                    }
                }
            }
        }
    }

    for (int row = 0; row < patches; ++row) {
        for (int column = 0; column < patches; ++column) {
            const int patchId = layout.patchIndex(row, column);
            const int step = result[patchId].sourceStep;
            quint8 mask = 0;
            if (column > 0
                    && result[layout.patchIndex(row, column - 1)].sourceStep
                    == step * 2)
                mask |= LocalX0;
            if (column + 1 < patches
                    && result[layout.patchIndex(row, column + 1)].sourceStep
                    == step * 2)
                mask |= LocalXMax;
            if (row > 0
                    && result[layout.patchIndex(row - 1, column)].sourceStep
                    == step * 2)
                mask |= LocalZ0;
            if (row + 1 < patches
                    && result[layout.patchIndex(row + 1, column)].sourceStep
                    == step * 2)
                mask |= LocalZMax;
            result[patchId].edgeMask = mask;
        }
    }
    return refined;
}

void TerrainLod::connectTileStates(QVector<TerrainLodTileState> &tiles,
                                  const QVector<TerrainLodConnection> &connections) {
    auto pin = [](TerrainLodTileState &tile, int patch) {
        tile.patches[patch].sourceStep = 1;
        tile.patches[patch].effectiveSampleSpacing = tile.layout.sampleSpacing;
    };
    for (const auto &c : connections) {
        auto &a = tiles[c.firstTile];
        auto &b = tiles[c.secondTile];
        if (a.gaps.value(c.firstPatch)) pin(b, c.secondPatch);
        if (b.gaps.value(c.secondPatch)) pin(a, c.firstPatch);
    }
    auto refine = [](TerrainPatchLodState &a, const TerrainPatchLodState &b) {
        bool changed = false;
        // Never invent a finer-than-native level. A native 4:1 boundary keeps
        // the existing 2:1 transition as best effort, not extra templates.
        while (a.sourceStep > 1
               && a.effectiveSampleSpacing > b.effectiveSampleSpacing * 2) {
            a.sourceStep /= 2;
            a.effectiveSampleSpacing /= 2;
            changed = true;
        }
        return changed;
    };
    bool changed;
    do {
        changed = false;
        for (auto &tile : tiles)
            changed |= refineTileState(tile.layout, tile.patches);
        for (const auto &c : connections) {
            auto &a = tiles[c.firstTile].patches[c.firstPatch];
            auto &b = tiles[c.secondTile].patches[c.secondPatch];
            changed |= refine(a, b);
            changed |= refine(b, a);
        }
    } while (changed);

    auto stitch = [](TerrainLodTileState &tile, int patch, quint8 edge,
                     const TerrainPatchLodState &neighbor) {
        auto &state = tile.patches[patch];
        const int cells = tile.layout.patchResolution / state.sourceStep;
        if (cells >= 2 && cells % 2 == 0
                && neighbor.effectiveSampleSpacing >= state.effectiveSampleSpacing * 2)
            state.edgeMask |= edge;
    };
    // All selections are now final. OR implements the deterministic coarse
    // fallback for an edge meeting multiple, differently selected patches.
    for (const auto &c : connections) {
        auto &a = tiles[c.firstTile];
        auto &b = tiles[c.secondTile];
        stitch(a, c.firstPatch, c.firstEdge, b.patches[c.secondPatch]);
        stitch(b, c.secondPatch, c.secondEdge, a.patches[c.firstPatch]);
    }
    for (const auto &c : connections) {
        auto &a = tiles[c.firstTile];
        auto &b = tiles[c.secondTile];
        const auto &first = a.patches[c.firstPatch];
        const auto &second = b.patches[c.secondPatch];
        const int small = std::min(first.effectiveSampleSpacing, second.effectiveSampleSpacing);
        const int large = std::max(first.effectiveSampleSpacing, second.effectiveSampleSpacing);
        // An edge mask requested by one span but unsuitable for another span
        // reveals the unsupported mixed-neighbour case without a mapping table.
        if (large > small * 2
                || ((first.edgeMask & c.firstEdge) && second.effectiveSampleSpacing < first.effectiveSampleSpacing * 2)
                || ((second.edgeMask & c.secondEdge) && first.effectiveSampleSpacing < second.effectiveSampleSpacing * 2))
            a.bestEffortBoundary = b.bestEffortBoundary = true;
    }
}
