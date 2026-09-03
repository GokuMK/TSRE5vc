/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine / editor
 *  Copyright (C) 2016-2026 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/world/TerrainActionRaster.h>

#include <algorithm>
#include <cmath>
#include <limits>

TerrainActionRaster::TerrainActionRaster(float minimumXValue,
                                         float minimumZValue,
                                         float maximumXValue,
                                         float maximumZValue,
                                         int sampleSpacing) {
    if (sampleSpacing <= 0
            || !std::isfinite(minimumXValue)
            || !std::isfinite(minimumZValue)
            || !std::isfinite(maximumXValue)
            || !std::isfinite(maximumZValue)
            || minimumXValue > maximumXValue
            || minimumZValue > maximumZValue)
        return;

    const double alignedMinimumX = std::floor(
                static_cast<double>(minimumXValue) / sampleSpacing)
            * sampleSpacing;
    const double alignedMinimumZ = std::floor(
                static_cast<double>(minimumZValue) / sampleSpacing)
            * sampleSpacing;
    const double alignedMaximumX = std::ceil(
                static_cast<double>(maximumXValue) / sampleSpacing)
            * sampleSpacing;
    const double alignedMaximumZ = std::ceil(
                static_cast<double>(maximumZValue) / sampleSpacing)
            * sampleSpacing;
    if (alignedMinimumX < std::numeric_limits<int>::min()
            || alignedMinimumZ < std::numeric_limits<int>::min()
            || alignedMaximumX > std::numeric_limits<int>::max()
            || alignedMaximumZ > std::numeric_limits<int>::max())
        return;

    const qint64 widthValue = static_cast<qint64>(
                (alignedMaximumX - alignedMinimumX) / sampleSpacing) + 1;
    const qint64 heightValue = static_cast<qint64>(
                (alignedMaximumZ - alignedMinimumZ) / sampleSpacing) + 1;
    if (widthValue <= 0 || heightValue <= 0
            || widthValue > std::numeric_limits<int>::max()
            || heightValue > std::numeric_limits<int>::max()
            || widthValue > std::numeric_limits<int>::max() / heightValue)
        return;

    originX = static_cast<int>(alignedMinimumX);
    originZ = static_cast<int>(alignedMinimumZ);
    endX = static_cast<int>(alignedMaximumX);
    endZ = static_cast<int>(alignedMaximumZ);
    sampleStep = sampleSpacing;
    width = static_cast<int>(widthValue);
    height = static_cast<int>(heightValue);
    minimumDistances.fill(std::numeric_limits<float>::infinity(),
                          width * height);
}

bool TerrainActionRaster::isValid() const {
    return sampleStep > 0 && width > 0 && height > 0
            && minimumDistances.size() == width * height;
}

int TerrainActionRaster::spacing() const {
    return sampleStep;
}

int TerrainActionRaster::minimumX() const {
    return originX;
}

int TerrainActionRaster::minimumZ() const {
    return originZ;
}

int TerrainActionRaster::maximumX() const {
    return endX;
}

int TerrainActionRaster::maximumZ() const {
    return endZ;
}

void TerrainActionRaster::stampLegacyPoint(float pointX, float pointZ,
                                           float radiusMetres,
                                           float bedRadiusMetres) {
    if (!isValid() || !std::isfinite(pointX) || !std::isfinite(pointZ)
            || !std::isfinite(radiusMetres) || radiusMetres < 0.0f
            || !std::isfinite(bedRadiusMetres) || bedRadiusMetres < 0.0f)
        return;

    // Match the historical F operation: source points are snapped down to the
    // active terrain lattice before their circular influence is stamped.
    const int centerX = static_cast<int>(std::floor(
                (static_cast<double>(pointX) - originX) / sampleStep));
    const int centerZ = static_cast<int>(std::floor(
                (static_cast<double>(pointZ) - originZ) / sampleStep));
    const int radiusCells = static_cast<int>(std::ceil(
                static_cast<double>(radiusMetres) / sampleStep));
    const int bedCells = static_cast<int>(std::ceil(
                static_cast<double>(bedRadiusMetres) / sampleStep));
    const double radiusSquared = static_cast<double>(radiusMetres)
            * radiusMetres;

    for (int offsetX = -radiusCells; offsetX <= radiusCells; ++offsetX) {
        const int rasterX = centerX + offsetX;
        if (rasterX < 0 || rasterX >= width)
            continue;
        for (int offsetZ = -radiusCells; offsetZ <= radiusCells; ++offsetZ) {
            const int rasterZ = centerZ + offsetZ;
            if (rasterZ < 0 || rasterZ >= height)
                continue;
            const double distanceSquared =
                    (static_cast<double>(offsetX) * offsetX
                     + static_cast<double>(offsetZ) * offsetZ)
                    * sampleStep * sampleStep;
            if (distanceSquared > radiusSquared)
                continue;

            int outsideX = 0;
            int outsideZ = 0;
            if (offsetX <= -bedCells)
                outsideX = offsetX + bedCells - 1;
            else if (offsetX >= bedCells)
                outsideX = offsetX - bedCells;
            if (offsetZ <= -bedCells)
                outsideZ = offsetZ + bedCells - 1;
            else if (offsetZ >= bedCells)
                outsideZ = offsetZ - bedCells;

            const float distance = std::sqrt(static_cast<float>(
                        outsideX * outsideX + outsideZ * outsideZ))
                    * sampleStep;
            float &stored = minimumDistances[rasterZ * width + rasterX];
            stored = std::min(stored, distance);
        }
    }
}

void TerrainActionRaster::stampSegment(float startX, float startZ,
                                       float endXValue, float endZValue,
                                       float radiusMetres,
                                       float bedRadiusMetres) {
    if (!isValid() || !std::isfinite(startX) || !std::isfinite(startZ)
            || !std::isfinite(endXValue) || !std::isfinite(endZValue)
            || !std::isfinite(radiusMetres) || radiusMetres < 0.0f
            || !std::isfinite(bedRadiusMetres) || bedRadiusMetres < 0.0f)
        return;

    const double minimumWorldX = std::min(startX, endXValue) - radiusMetres;
    const double minimumWorldZ = std::min(startZ, endZValue) - radiusMetres;
    const double maximumWorldX = std::max(startX, endXValue) + radiusMetres;
    const double maximumWorldZ = std::max(startZ, endZValue) + radiusMetres;
    const int firstX = std::clamp(static_cast<int>(std::floor(
                (minimumWorldX - originX) / sampleStep)), 0, width - 1);
    const int firstZ = std::clamp(static_cast<int>(std::floor(
                (minimumWorldZ - originZ) / sampleStep)), 0, height - 1);
    const int lastX = std::clamp(static_cast<int>(std::ceil(
                (maximumWorldX - originX) / sampleStep)), 0, width - 1);
    const int lastZ = std::clamp(static_cast<int>(std::ceil(
                (maximumWorldZ - originZ) / sampleStep)), 0, height - 1);
    const double segmentX = static_cast<double>(endXValue) - startX;
    const double segmentZ = static_cast<double>(endZValue) - startZ;
    const double segmentLengthSquared = segmentX * segmentX
            + segmentZ * segmentZ;
    const double radiusSquared = static_cast<double>(radiusMetres)
            * radiusMetres;

    for (int rasterZ = firstZ; rasterZ <= lastZ; ++rasterZ) {
        const double worldZ = originZ
                + static_cast<double>(rasterZ) * sampleStep;
        for (int rasterX = firstX; rasterX <= lastX; ++rasterX) {
            const double worldX = originX
                    + static_cast<double>(rasterX) * sampleStep;
            double closestX = startX;
            double closestZ = startZ;
            if (segmentLengthSquared > 0.0) {
                const double projection = std::clamp(
                            ((worldX - startX) * segmentX
                             + (worldZ - startZ) * segmentZ)
                            / segmentLengthSquared,
                            0.0, 1.0);
                closestX += projection * segmentX;
                closestZ += projection * segmentZ;
            }
            const double differenceX = worldX - closestX;
            const double differenceZ = worldZ - closestZ;
            const double distanceSquared = differenceX * differenceX
                    + differenceZ * differenceZ;
            if (distanceSquared > radiusSquared)
                continue;

            const float distance = static_cast<float>(std::sqrt(distanceSquared));
            const float distanceOutsideBed = std::max(
                        0.0f, distance - bedRadiusMetres);
            float &stored = minimumDistances[rasterZ * width + rasterX];
            stored = std::min(stored, distanceOutsideBed);
        }
    }
}

bool TerrainActionRaster::sampleNearest(float worldX, float worldZ,
                                        float &distanceOutsideBedMetres) const {
    if (!isValid() || !std::isfinite(worldX) || !std::isfinite(worldZ))
        return false;
    const int sampleX = static_cast<int>(std::lround(
                (static_cast<double>(worldX) - originX) / sampleStep));
    const int sampleZ = static_cast<int>(std::lround(
                (static_cast<double>(worldZ) - originZ) / sampleStep));
    if (sampleX < 0 || sampleX >= width || sampleZ < 0 || sampleZ >= height)
        return false;
    const float value = minimumDistances[sampleZ * width + sampleX];
    if (!std::isfinite(value))
        return false;
    distanceOutsideBedMetres = value;
    return true;
}
