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

#include <tsre/world/TerrainGridLayout.h>

namespace {

bool isSupportedSpacing(int spacing) {
    return spacing == 1 || spacing == 2 || spacing == 4
            || spacing == 8 || spacing == 16 || spacing == 32;
}

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
        bool *profileViolation) {
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
            if (row == 0 || column == 0
                    || row == patches - 1 || column == patches - 1)
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
                        changed = true;
                    } else if (second > first * 2) {
                        second = first * 2;
                        result[neighbourId].effectiveSampleSpacing =
                                layout.sampleSpacing * second;
                        changed = true;
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
    return result;
}
