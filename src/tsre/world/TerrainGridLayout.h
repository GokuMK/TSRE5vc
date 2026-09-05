/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef TERRAINGRIDLAYOUT_H
#define TERRAINGRIDLAYOUT_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <QString>

enum class TerrainHeightProfile {
    Standard256x8,
    Low128x16,
    High512x4,
    Ultra1024x2,
    Extreme2048x1
};

struct TerrainGridLayout {
    static constexpr int WorldTileSize = 2048;
    static constexpr int WorldTileHalfSize = WorldTileSize / 2;
    static constexpr int DefaultPatchesPerSide = 16;
    static constexpr int MaximumLoadablePatchesPerSide = 32;
    static constexpr int MaximumEditablePatchesPerSide = 32;
    static constexpr int MaximumPatchesPerSide = MaximumLoadablePatchesPerSide;
    static constexpr int SupportedPatchesPerSide = DefaultPatchesPerSide;
    static constexpr int SupportedPatchRecordCount =
            MaximumPatchesPerSide * MaximumPatchesPerSide;
    static constexpr int MinimumSamples = SupportedPatchesPerSide;
    static constexpr int MaximumSamples = 2048;
    static constexpr int MinimumPatchResolution = 4;
    static constexpr int MaximumPatchResolution = 128;
    static constexpr int PagedPatchesPerPage = 256;
    static constexpr int PagedVertexStride = 8;

    int sampleCount = 0;
    int sampleSpacing = 0;
    int patchesPerSide = 0;
    int patchResolution = 0;
    int terrainWorldSize = 0;
    int patchWorldSize = 0;
    std::size_t storedCellCount = 0;
    std::size_t memoryCellCount = 0;
    std::size_t terrainVboBytes = 0;
    std::size_t patchVboBytes = 0;
    std::size_t pagedPatchVertexBytes = 0;
    std::size_t pagedTerrainVertexBytes = 0;
    std::size_t pagedIndexBytes = 0;

    static TerrainGridLayout profile(
            TerrainHeightProfile value,
            int patches = DefaultPatchesPerSide) {
        TerrainGridLayout layout;
        QString ignored;
        switch (value) {
        case TerrainHeightProfile::Low128x16:
            tryCreate(128, 16.0f, patches, 0.0f, layout, ignored);
            break;
        case TerrainHeightProfile::High512x4:
            tryCreate(512, 4.0f, patches, 0.0f, layout, ignored);
            break;
        case TerrainHeightProfile::Ultra1024x2:
            tryCreate(1024, 2.0f, patches, 0.0f, layout, ignored);
            break;
        case TerrainHeightProfile::Extreme2048x1:
            tryCreate(2048, 1.0f, patches, 0.0f, layout, ignored);
            break;
        case TerrainHeightProfile::Standard256x8:
        default:
            tryCreate(256, 8.0f, patches, 0.0f, layout, ignored);
            break;
        }
        return layout;
    }

    static QString heightProfileName(TerrainHeightProfile value) {
        switch (value) {
        case TerrainHeightProfile::Low128x16:
            return "Low resolution - 128 x 128 samples at 16 m";
        case TerrainHeightProfile::High512x4:
            return "High resolution - 512 x 512 samples at 4 m";
        case TerrainHeightProfile::Ultra1024x2:
            return "Ultra resolution - 1024 x 1024 samples at 2 m";
        case TerrainHeightProfile::Extreme2048x1:
            return "Extreme resolution - 2048 x 2048 samples at 1 m";
        case TerrainHeightProfile::Standard256x8:
        default:
            return "Standard - 256 x 256 samples at 8 m";
        }
    }

    static QString profileName(
            TerrainHeightProfile value,
            int patches = DefaultPatchesPerSide) {
        const TerrainGridLayout layout = profile(value, patches);
        if (layout.sampleCount == 0)
            return QString("%1; %2 x %2 patches (unsupported layout)")
                    .arg(heightProfileName(value)).arg(patches);
        return QString("%1; %2 x %2 patches; R=%3")
                .arg(heightProfileName(value)).arg(patches)
                .arg(layout.patchResolution);
    }

    static bool checkedMultiply(std::size_t left, std::size_t right,
                                std::size_t &result) {
        if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
            return false;
        result = left * right;
        return true;
    }

    static bool normalizeWorldPosition(int &tileX, int &tileZ,
                                       float &localX, float &localZ) {
        if (!std::isfinite(localX) || !std::isfinite(localZ))
            return false;
        const double offsetXValue = std::floor(
                (static_cast<double>(localX) + WorldTileHalfSize)
                / WorldTileSize);
        const double offsetZValue = std::floor(
                (static_cast<double>(localZ) + WorldTileHalfSize)
                / WorldTileSize);
        if (offsetXValue < std::numeric_limits<int>::min()
                || offsetXValue > std::numeric_limits<int>::max()
                || offsetZValue < std::numeric_limits<int>::min()
                || offsetZValue > std::numeric_limits<int>::max())
            return false;
        const int offsetX = static_cast<int>(offsetXValue);
        const int offsetZ = static_cast<int>(offsetZValue);
        if ((offsetX > 0 && tileX > std::numeric_limits<int>::max() - offsetX)
                || (offsetX < 0 && tileX < std::numeric_limits<int>::min() - offsetX)
                || (offsetZ > 0 && tileZ > std::numeric_limits<int>::max() - offsetZ)
                || (offsetZ < 0 && tileZ < std::numeric_limits<int>::min() - offsetZ))
            return false;
        tileX += offsetX;
        tileZ += offsetZ;
        localX -= static_cast<double>(offsetX) * WorldTileSize;
        localZ -= static_cast<double>(offsetZ) * WorldTileSize;
        return true;
    }

    static bool tryCreate(int samples, float spacing, int patches,
                          float rotation, TerrainGridLayout &layout,
                          QString &error) {
        layout = TerrainGridLayout{};
        if (samples < MinimumSamples) {
            error = QString("sample count %1 is below the supported minimum %2")
                    .arg(samples).arg(MinimumSamples);
            return false;
        }
        if (samples > MaximumSamples) {
            error = QString("sample count %1 exceeds the supported maximum %2")
                    .arg(samples).arg(MaximumSamples);
            return false;
        }
        if (patches <= 0 || patches > MaximumPatchesPerSide) {
            error = QString("patch grid %1 x %1 is unsupported; patch count must be between 1 and %2 per side")
                    .arg(patches).arg(MaximumPatchesPerSide);
            return false;
        }
        if (samples % patches != 0) {
            error = QString("sample count %1 does not divide evenly into %2 patches")
                    .arg(samples).arg(patches);
            return false;
        }
        if (!std::isfinite(spacing) || spacing <= 0.0f
                || std::floor(spacing) != spacing
                || spacing > static_cast<float>(std::numeric_limits<int>::max())) {
            error = QString("sample spacing %1 is unsupported; it must be a positive whole number of metres")
                    .arg(spacing);
            return false;
        }
        if (!std::isfinite(rotation) || rotation != 0.0f) {
            error = QString("sample rotation %1 is unsupported; only unrotated grids are supported")
                    .arg(rotation);
            return false;
        }

        const int integralSpacing = static_cast<int>(spacing);
        if (samples > std::numeric_limits<int>::max() / integralSpacing) {
            error = "terrain world size overflows";
            return false;
        }
        const int terrainSize = samples * integralSpacing;
        if (terrainSize < WorldTileSize || terrainSize % WorldTileSize != 0) {
            error = QString("terrain footprint %1 m is unsupported; quadtree terrain must cover a whole number of %2 m World tiles")
                    .arg(terrainSize).arg(WorldTileSize);
            return false;
        }

        std::size_t storedCells = 0;
        std::size_t memorySide = static_cast<std::size_t>(samples) + 1;
        std::size_t memoryCells = 0;
        std::size_t patchCells = 0;
        std::size_t patchFloats = 0;
        std::size_t totalFloats = 0;
        const int patchResolution = samples / patches;
        if (patchResolution < MinimumPatchResolution
                || patchResolution > MaximumPatchResolution) {
            error = QString("terrain patch resolution %1 is unsupported; it must be between %2 and %3 samples")
                    .arg(patchResolution).arg(MinimumPatchResolution)
                    .arg(MaximumPatchResolution);
            return false;
        }
        if (!checkedMultiply(static_cast<std::size_t>(samples),
                             static_cast<std::size_t>(samples), storedCells)
                || !checkedMultiply(memorySide, memorySide, memoryCells)
                || !checkedMultiply(static_cast<std::size_t>(patchResolution),
                                    static_cast<std::size_t>(patchResolution), patchCells)
                || !checkedMultiply(patchCells, 6u * 8u, patchFloats)
                || !checkedMultiply(storedCells, 6u * 8u, totalFloats)) {
            error = "terrain grid size overflows";
            return false;
        }

        std::size_t patchBytes = 0;
        std::size_t totalBytes = 0;
        if (!checkedMultiply(patchFloats, sizeof(float), patchBytes)
                || !checkedMultiply(totalFloats, sizeof(float), totalBytes)
                || patchBytes > static_cast<std::size_t>(std::numeric_limits<int>::max())
                || totalBytes > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            error = "terrain render buffer exceeds OpenGL API limits";
            return false;
        }

        layout.sampleCount = samples;
        layout.sampleSpacing = integralSpacing;
        layout.patchesPerSide = patches;
        layout.patchResolution = patchResolution;
        layout.terrainWorldSize = terrainSize;
        layout.patchWorldSize = patchResolution * integralSpacing;
        layout.storedCellCount = storedCells;
        layout.memoryCellCount = memoryCells;
        layout.terrainVboBytes = totalBytes;
        layout.patchVboBytes = patchBytes;
        std::size_t pagedVertices = 0;
        std::size_t pagedPatchBytes = 0;
        std::size_t pagedTotalBytes = 0;
        std::size_t regularIndexCount = 0;
        std::size_t lodIndexCount = 0;
        std::size_t indexBytes = 0;
        const std::size_t pagedSide = static_cast<std::size_t>(patchResolution) + 1u;
        if (!checkedMultiply(pagedSide, pagedSide, pagedVertices)
                || !checkedMultiply(pagedVertices, PagedVertexStride, pagedPatchBytes)
                || !checkedMultiply(pagedPatchBytes,
                                    static_cast<std::size_t>(patches) * patches,
                                    pagedTotalBytes)
                || !checkedMultiply(static_cast<std::size_t>(patchResolution),
                                    static_cast<std::size_t>(patchResolution) * 6u,
                                    regularIndexCount)
                || pagedPatchBytes > static_cast<std::size_t>(std::numeric_limits<int>::max())
                || pagedTotalBytes > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            error = "paged terrain render buffer exceeds OpenGL API limits";
            layout = TerrainGridLayout{};
            return false;
        }
        for (int sourceStep = 1; sourceStep <= patchResolution;
             sourceStep *= 2) {
            if (patchResolution % sourceStep != 0)
                break;
            const int effectiveSpacing = integralSpacing * sourceStep;
            if (effectiveSpacing > 32 && sourceStep > 1)
                break;
            const int cells = patchResolution / sourceStep;
            const bool hasTransitions = cells >= 2 && cells % 2 == 0;
            const std::size_t levelIndices = hasTransitions
                    ? static_cast<std::size_t>(32 * cells * cells
                                               - 16 * cells) * 3u
                    : static_cast<std::size_t>(cells) * cells * 6u;
            if (levelIndices > std::numeric_limits<std::size_t>::max()
                    - lodIndexCount) {
                error = "paged terrain LOD index buffer size overflows";
                layout = TerrainGridLayout{};
                return false;
            }
            lodIndexCount += levelIndices;
            if (sourceStep > patchResolution / 2)
                break;
        }
        if (lodIndexCount == 0)
            lodIndexCount = regularIndexCount;
        if (!checkedMultiply(lodIndexCount, sizeof(quint16), indexBytes)
                || indexBytes > static_cast<std::size_t>(
                    std::numeric_limits<int>::max())) {
            error = "paged terrain LOD index buffer exceeds OpenGL API limits";
            layout = TerrainGridLayout{};
            return false;
        }
        layout.pagedPatchVertexBytes = pagedPatchBytes;
        layout.pagedTerrainVertexBytes = pagedTotalBytes;
        layout.pagedIndexBytes = indexBytes;
        error.clear();
        return true;
    }

    bool expectedPayloadBytes(std::size_t bytesPerCell,
                              std::size_t &bytes) const {
        return checkedMultiply(storedCellCount, bytesPerCell, bytes)
                && bytes <= static_cast<std::size_t>(std::numeric_limits<int>::max());
    }

    bool supportsEditing() const {
        return patchesPerSide > 0
                && patchesPerSide <= MaximumEditablePatchesPerSide;
    }

    int patchRecordCount() const {
        return patchesPerSide * patchesPerSide;
    }

    int pagedPageCount() const {
        return (patchRecordCount() + PagedPatchesPerPage - 1)
                / PagedPatchesPerPage;
    }

    int pagedVerticesPerPatch() const {
        return (patchResolution + 1) * (patchResolution + 1);
    }

    int pagedIndicesPerPatch() const {
        return patchResolution * patchResolution * 6;
    }

    bool isPatchIndexValid(int index) const {
        return index >= 0 && index < patchRecordCount();
    }

    int patchRow(int index) const {
        return index / patchesPerSide;
    }

    int patchColumn(int index) const {
        return index % patchesPerSide;
    }

    int patchIndex(int row, int column) const {
        return row * patchesPerSide + column;
    }

    float defaultPatchTextureScale() const {
        return 1.0f / static_cast<float>(patchResolution);
    }
};

#endif // TERRAINGRIDLAYOUT_H
