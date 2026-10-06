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

float LightGrid::rangeFor(float brightness, float radius, float maxRange) {
    // pi * brightness / (d^2 + r^2) = 1 / 512: the radiance factor the
    // shaders apply, as for the sun.
    const float squared = 512.0f * float(M_PI) * std::max(brightness, 0.0f) - radius * radius;
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
    // Horizontally the grid follows the camera in whole cells, so lights do
    // not jump between cells as it moves.
    horizontalCell = CellSize;
    origin[0] = (std::floor(eye[0] / CellSize) - CellsX / 2) * CellSize;
    origin[2] = (std::floor(eye[2] / CellSize) - CellsZ / 2) * CellSize;
    const float extent[2] = {origin[0] + CellsX * CellSize, origin[2] + CellsZ * CellSize};

    // Lights reaching the grid's columns; vertically the grid spans them.
    std::vector<int> kept;
    float low = 1e30f, high = -1e30f;
    for (size_t i = 0; i < lights.size() && int(kept.size()) < MaxLights; ++i) {
        const Light &light = lights[i];
        if (light.range <= 0.0f)
            continue;
        const float *p = light.position;
        if (p[0] + light.range < origin[0] || p[0] - light.range > extent[0]
                || p[2] + light.range < origin[2] || p[2] - light.range > extent[1]
                || std::abs(p[1] - eye[1]) - light.range > 2048.0f)
            continue;
        kept.push_back(int(i));
        low = std::min(low, p[1] - light.range);
        high = std::max(high, p[1] + light.range);
    }
    if (kept.empty())
        return;
    // At most 2 km tall around the camera, at least 4 m per layer.
    low = std::max(low, eye[1] - 1024.0f);
    high = std::min(high, eye[1] + 1024.0f);
    verticalCell = std::max(4.0f, (high - low) / CellsY);
    origin[1] = low;

    lightTexels.reserve(kept.size() * 16);
    struct Entry { int light; float weight; };
    std::vector<std::vector<Entry>> cells(size_t(CellsWidth) * CellsY);
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
        const float brightness = std::max({light.color[0], light.color[1], light.color[2]});
        const float range = light.range;
        auto cellRange = [&](int axis, float cell, int count, int &first, int &last) {
            first = std::max(0, int(std::floor((p[axis] - range - origin[axis]) / cell)));
            last = std::min(count - 1, int(std::floor((p[axis] + range - origin[axis]) / cell)));
        };
        int x0, x1, y0, y1, z0, z1;
        cellRange(0, horizontalCell, CellsX, x0, x1);
        cellRange(1, verticalCell, CellsY, y0, y1);
        cellRange(2, horizontalCell, CellsZ, z0, z1);
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    // Distance from the light to the cell's box.
                    const float lowCorner[3] = {origin[0] + x * horizontalCell,
                                                origin[1] + y * verticalCell,
                                                origin[2] + z * horizontalCell};
                    const float size[3] = {horizontalCell, verticalCell, horizontalCell};
                    float distance = 0.0f;
                    for (int axis = 0; axis < 3; ++axis) {
                        const float nearest = std::clamp(p[axis], lowCorner[axis],
                                                         lowCorner[axis] + size[axis]);
                        distance += (p[axis] - nearest) * (p[axis] - nearest);
                    }
                    if (distance > range * range)
                        continue;
                    const float weight = brightness / (distance + light.radius * light.radius + 1.0f);
                    cells[size_t(x + z * CellsX + y * CellsWidth)].push_back({index, weight});
                }
            }
        }
    }
    for (size_t cell = 0; cell < cells.size(); ++cell) {
        std::vector<Entry> &entries = cells[cell];
        if (entries.empty())
            continue;
        std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
            return a.weight != b.weight ? a.weight > b.weight : a.light < b.light;
        });
        const size_t count = std::min(entries.size(), size_t(MaxLightsPerCell));
        if (indexTexels.size() + count > size_t(MaxIndices))
            break;
        cellTexels[cell * 4] = float(indexTexels.size());
        cellTexels[cell * 4 + 1] = float(count);
        for (size_t i = 0; i < count; ++i)
            indexTexels.push_back(float(entries[i].light));
    }
    // Whole rows for the index texture.
    const size_t rows = std::max<size_t>(1, (indexTexels.size() + IndexWidth - 1) / IndexWidth);
    indexTexels.resize(rows * IndexWidth, 0.0f);
}
