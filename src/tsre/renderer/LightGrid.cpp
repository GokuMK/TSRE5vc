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
#include <cstdint>
#include <cstring>

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
    return x + z * CellsX + y * CellsX * CellsZ;
}

float LightGrid::packCell(int first, int count) {
    if (count <= 0)
        return 0.0f;
    const std::uint32_t bits = 0x40000000u | (std::uint32_t(first) & 0x7FFFFFu) << 7 | (std::uint32_t(count) & 0x7Fu);
    float cell;
    std::memcpy(&cell, &bits, sizeof(cell));
    return cell;
}

void LightGrid::unpackCell(float cell, int &first, int &count) {
    std::uint32_t bits;
    std::memcpy(&bits, &cell, sizeof(bits));
    first = int((bits >> 7) & 0x7FFFFFu);
    count = int(bits & 0x7Fu);
}

LightGrid::Stats LightGrid::stats(const float *position) const {
    Stats s;
    double listed = 0.0;
    for (size_t cell = 0; cell < cellTexels.size(); ++cell) {
        int first, count;
        unpackCell(cellTexels[cell], first, count);
        if (count <= 0)
            continue;
        ++s.cells;
        s.longest = std::max(s.longest, count);
        listed += count;
    }
    s.mean = s.cells > 0 ? listed / s.cells : 0.0;
    const int at = cellAt(position);
    int first = 0;
    if (at >= 0 && size_t(at) < cellTexels.size())
        unpackCell(cellTexels[size_t(at)], first, s.at);
    return s;
}

long long LightGrid::uploadBytes() const {
    return (long long)(lightTexels.size() + cellTexels.size() + indexTexels.size()) * (long long)sizeof(float);
}

void LightGrid::build(const std::vector<Light> &lights, const float *eye) {
    lightTexels.clear();
    indexTexels.clear();
    cellTexels.assign(size_t(CellsX) * CellsZ * CellsY, 0.0f);
    // Horizontally the grid lies around the camera, moved in steps: 16 m
    // cells at any distance, aligned, so static lights keep their cells.
    horizontalCell = CellSize;
    origin[0] = std::floor((eye[0] - MaxExtent / 2) / Step) * Step;
    origin[2] = std::floor((eye[2] - MaxExtent / 2) / Step) * Step;
    // The lights reaching it.
    std::vector<int> kept;
    float low = 1e30f, high = -1e30f;
    for (size_t i = 0; i < lights.size() && int(kept.size()) < MaxLights; ++i) {
        const Light &light = lights[i];
        if (light.range <= 0.0f)
            continue;
        const float *p = light.position;
        bool inside = std::abs(p[1] - eye[1]) - light.range <= MaxHeight / 2;
        for (int axis = 0; axis < 3; axis += 2)
            inside = inside && p[axis] + light.range >= origin[axis]
                    && p[axis] - light.range <= origin[axis] + MaxExtent;
        if (!inside)
            continue;
        kept.push_back(int(i));
        low = std::min(low, std::max(p[1] - light.range, eye[1] - MaxHeight / 2));
        high = std::max(high, std::min(p[1] + light.range, eye[1] + MaxHeight / 2));
    }
    if (kept.empty())
        return;
    // Vertically the layers span the lights, at least 4 m each.
    verticalCell = std::max(4.0f, (high - low) / CellsY);
    origin[1] = low;

    lightTexels.reserve(kept.size() * LightTexels * 4);
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
                    // Distance from the light to the cell's box, and its
                    // light over the cell on average: as from the cell's
                    // middle, over a quarter of the cell's size squared.
                    // (By the box's nearest point a small lamp inside the
                    // cell outranked a headlight's beam across it; the fog
                    // takes a cell's 16 first, and dropped beams in cells
                    // by the train: the user, 2026-10-23, CMK, valley fog.)
                    const int cell[3] = {x, y, z};
                    float distance = 0.0f, middle = 0.0f, spread = 0.0f;
                    for (int axis = 0; axis < 3; ++axis) {
                        const float lowCorner = origin[axis] + cell[axis] * size[axis];
                        const float nearest = std::clamp(p[axis], lowCorner, lowCorner + size[axis]);
                        distance += (p[axis] - nearest) * (p[axis] - nearest);
                        const float centre = lowCorner + size[axis] * 0.5f;
                        middle += (p[axis] - centre) * (p[axis] - centre);
                        spread += size[axis] * size[axis] * 0.25f;
                    }
                    if (distance <= range * range)
                        visit(x + z * CellsX + y * CellsX * CellsZ,
                              brightness / (middle + spread + light.radius * light.radius + 1.0f));
                }
            }
        }
    };
    std::vector<int> counts(size_t(CellsX) * CellsZ * CellsY + 1, 0);
    for (int index = 0; index < int(kept.size()); ++index) {
        const Light &light = lights[size_t(kept[size_t(index)])];
        const float *p = light.position;
        const float spread = light.cosInner - light.cosOuter;
        float texels[LightTexels * 4] = {
            p[0], p[1], p[2], light.range,
            light.color[0], light.color[1], light.color[2], light.radius * light.radius,
            light.direction[0], light.direction[1], light.direction[2],
            (light.spot ? 1.0f : 0.0f) + (light.boxCount > 0 ? 2.0f : 0.0f),
            light.cosOuter, spread > 1e-4f ? 1.0f / spread : 1e4f, float(light.shadowLayer + 1), float(light.shadowSet)};
        std::copy(light.shadowMatrix, light.shadowMatrix + 16, texels + 16);
        texels[32] = float(light.boxStart);
        texels[33] = float(light.boxCount);
        lightTexels.insert(lightTexels.end(), texels, texels + LightTexels * 4);
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
        cellTexels[cell] = packCell(int(indexTexels.size()), int(count));
        for (size_t i = 0; i < count; ++i)
            indexTexels.push_back(float(begin[std::ptrdiff_t(i)].light));
    }
    // Whole rows for the index texture.
    const size_t rows = std::max<size_t>(1, (indexTexels.size() + IndexWidth - 1) / IndexWidth);
    indexTexels.resize(rows * IndexWidth, 0.0f);
}
