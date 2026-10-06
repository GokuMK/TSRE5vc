/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "LightGrid.h"
#include <algorithm>
#include <cmath>

// M_PI is not standard (MSVC defines it only on request).
constexpr double Pi = 3.14159265358979323846;

float LightGrid::rangeFor(float brightness, float radius, float maxRange) {
    // pi * brightness / (d^2 + r^2) = 1 / 512: the radiance factor the
    // shaders apply, as for the sun.
    const float squared = 512.0f * Pi * std::max(brightness, 0.0f) - radius * radius;
    return std::clamp(squared > 0.0f ? std::sqrt(squared) : 0.0f, 0.0f, maxRange);
}

int LightGrid::cellAt(const float *position) const {
    const int x = int(std::floor((position[0] - origin[0]) / horizontalCell));
    const int y = int(std::floor((position[1] - origin[1]) / verticalCell));
    const int z = int(std::floor((position[2] - origin[2]) / horizontalCell));
    if (x < 0 || y < 0 || z < 0 || x >= CellsX || y >= CellsY || z >= CellsZ)
        return -1;
    return x + z * CellsX + y * CellsWidth;
}

void LightGrid::build(const std::vector<Light> &lights, const float *eye) {
    lightTexels.clear();
    indexTexels.clear();
    cellTexels.assign(size_t(CellsWidth) * CellsY * 4, 0.0f);
    // Lights reaching the area around the camera; the grid spans their reach.
    std::vector<int> kept;
    float low[3] = {1e30f, 1e30f, 1e30f}, high[3] = {-1e30f, -1e30f, -1e30f};
    const float reach[3] = {MaxExtent / 2, MaxHeight / 2, MaxExtent / 2};
    for (size_t i = 0; i < lights.size() && int(kept.size()) < MaxLights; ++i) {
        const Light &light = lights[i];
        if (light.range <= 0.0f)
            continue;
        const float *p = light.position;
        bool inside = true;
        for (int axis = 0; axis < 3; ++axis)
            inside = inside && std::abs(p[axis] - eye[axis]) - light.range <= reach[axis];
        if (!inside)
            continue;
        kept.push_back(int(i));
        for (int axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], std::max(p[axis] - light.range, eye[axis] - reach[axis]));
            high[axis] = std::max(high[axis], std::min(p[axis] + light.range, eye[axis] + reach[axis]));
        }
    }
    if (kept.empty())
        return;
    // Horizontally: 16 m cells while the lights fit, coarser powers of two
    // for wider spreads. Cells are aligned to their size, so static lights
    // keep their cells as the camera moves.
    const float span = std::max(high[0] - low[0], high[2] - low[2]);
    horizontalCell = CellSize;
    while (horizontalCell * (CellsX - 1) < span && horizontalCell * CellsX < MaxExtent)
        horizontalCell *= 2.0f;
    origin[0] = std::floor(low[0] / horizontalCell) * horizontalCell;
    origin[2] = std::floor(low[2] / horizontalCell) * horizontalCell;
    // Vertically the layers span the lights, at least 4 m each.
    verticalCell = std::max(4.0f, (high[1] - low[1]) / CellsY);
    origin[1] = low[1];

    lightTexels.reserve(kept.size() * 16);
    // The cells each light reaches, visited twice: counting, then filling one
    // flat list of entries per cell.
    struct Entry { int light; float weight; };
    auto forEachCell = [&](int index, auto &&visit) {
        const Light &light = lights[size_t(kept[size_t(index)])];
        const float *p = light.position;
        const float range = light.range;
        const float brightness = std::max({light.color[0], light.color[1], light.color[2]});
        auto cellRange = [&](int axis, float cell, int count, int &first, int &last) {
            first = std::max(0, int(std::floor((p[axis] - range - origin[axis]) / cell)));
            last = std::min(count - 1, int(std::floor((p[axis] + range - origin[axis]) / cell)));
        };
        int x0, x1, y0, y1, z0, z1;
        cellRange(0, horizontalCell, CellsX, x0, x1);
        cellRange(1, verticalCell, CellsY, y0, y1);
        cellRange(2, horizontalCell, CellsZ, z0, z1);
        const float size[3] = {horizontalCell, verticalCell, horizontalCell};
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    // Distance from the light to the cell's box.
                    const int cell[3] = {x, y, z};
                    float distance = 0.0f;
                    for (int axis = 0; axis < 3; ++axis) {
                        const float lowCorner = origin[axis] + cell[axis] * size[axis];
                        const float nearest = std::clamp(p[axis], lowCorner, lowCorner + size[axis]);
                        distance += (p[axis] - nearest) * (p[axis] - nearest);
                    }
                    if (distance <= range * range)
                        visit(x + z * CellsX + y * CellsWidth,
                              brightness / (distance + light.radius * light.radius + 1.0f));
                }
            }
        }
    };
    std::vector<int> counts(size_t(CellsWidth) * CellsY + 1, 0);
    for (int index = 0; index < int(kept.size()); ++index) {
        const Light &light = lights[size_t(kept[size_t(index)])];
        const float *p = light.position;
        const float spread = light.cosInner - light.cosOuter;
        const float texels[16] = {
            p[0], p[1], p[2], light.range,
            light.color[0], light.color[1], light.color[2], light.radius * light.radius,
            light.direction[0], light.direction[1], light.direction[2], light.spot ? 1.0f : 0.0f,
            light.cosOuter, spread > 1e-4f ? 1.0f / spread : 1e4f, 0.0f, 0.0f};
        lightTexels.insert(lightTexels.end(), texels, texels + 16);
        forEachCell(index, [&](int cell, float) { ++counts[size_t(cell) + 1]; });
    }
    for (size_t cell = 1; cell < counts.size(); ++cell)
        counts[cell] += counts[cell - 1];
    std::vector<Entry> entries(size_t(counts.back()));
    std::vector<int> fill(counts.begin(), counts.end() - 1);
    for (int index = 0; index < int(kept.size()); ++index)
        forEachCell(index, [&](int cell, float weight) {
            entries[size_t(fill[size_t(cell)]++)] = {index, weight};
        });
    for (size_t cell = 0; cell + 1 < counts.size(); ++cell) {
        auto begin = entries.begin() + counts[cell];
        auto end = entries.begin() + counts[cell + 1];
        if (begin == end)
            continue;
        const size_t count = std::min(size_t(end - begin), size_t(MaxLightsPerCell));
        std::partial_sort(begin, begin + std::ptrdiff_t(count), end, [](const Entry &a, const Entry &b) {
            return a.weight != b.weight ? a.weight > b.weight : a.light < b.light;
        });
        if (indexTexels.size() + count > size_t(MaxIndices))
            break;
        cellTexels[cell * 4] = float(indexTexels.size());
        cellTexels[cell * 4 + 1] = float(count);
        for (size_t i = 0; i < count; ++i)
            indexTexels.push_back(float(begin[std::ptrdiff_t(i)].light));
    }
    // Whole rows for the index texture.
    const size_t rows = std::max<size_t>(1, (indexTexels.size() + IndexWidth - 1) / IndexWidth);
    indexTexels.resize(rows * IndexWidth, 0.0f);
}
