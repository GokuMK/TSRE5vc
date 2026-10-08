/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "EmissiveEmitters.h"
#include <QHash>
#include <algorithm>
#include <cmath>

namespace EmissiveEmitters {

namespace {

struct Triangle {
    float centroid[3];
    float power[3];
    float area;
};

struct Cell {
    double weighted[3] = {0.0, 0.0, 0.0};
    double weight = 0.0;
    double power[3] = {0.0, 0.0, 0.0};
    double area = 0.0;
};

float srgbToLinear(int value) {
    return std::pow(value / 255.0f, 2.2f);
}

// The map's linear colour at texture coordinates, repeating.
void sampleMap(const QImage &map, float u, float v, float *out) {
    const int x = std::clamp(int((u - std::floor(u)) * map.width()), 0, map.width() - 1);
    const int y = std::clamp(int((v - std::floor(v)) * map.height()), 0, map.height() - 1);
    const QRgb pixel = map.pixel(x, y);
    out[0] = srgbToLinear(qRed(pixel));
    out[1] = srgbToLinear(qGreen(pixel));
    out[2] = srgbToLinear(qBlue(pixel));
}

}

QVector<RenderItem::Light> extract(const Surface &surface, int maxEmitters) {
    QVector<RenderItem::Light> lights;
    if (surface.vertices == nullptr || surface.vertexCount < 3 || maxEmitters < 1
            || std::max({surface.emissive[0], surface.emissive[1], surface.emissive[2]}) <= 0.0f)
        return lights;
    const bool mapped = surface.map != nullptr && !surface.map->isNull() && surface.uvOffset >= 0;
    std::vector<Triangle> triangles;
    float low[3] = {1e30f, 1e30f, 1e30f}, high[3] = {-1e30f, -1e30f, -1e30f};
    for (int first = 0; first + 2 < surface.vertexCount; first += 3) {
        const float *p[3];
        for (int k = 0; k < 3; ++k)
            p[k] = surface.vertices + size_t(first + k) * size_t(surface.stride);
        const float e1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
        const float e2[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
        const float cross[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                e1[0] * e2[1] - e1[1] * e2[0]};
        const float area = 0.5f * std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
        if (!(area > 0.0f))
            continue;
        Triangle triangle;
        triangle.area = area;
        float radiance[3] = {surface.emissive[0], surface.emissive[1], surface.emissive[2]};
        if (mapped) {
            float u = 0.0f, v = 0.0f;
            for (int k = 0; k < 3; ++k) {
                u += p[k][surface.uvOffset] / 3.0f;
                v += p[k][surface.uvOffset + 1] / 3.0f;
            }
            const float *t = surface.uvTransform;
            const float tu = t[0] * u + t[1] * v + t[2];
            const float tv = t[3] * u + t[4] * v + t[5];
            float texel[3];
            sampleMap(*surface.map, tu, tv, texel);
            for (int c = 0; c < 3; ++c)
                radiance[c] *= texel[c];
        }
        for (int c = 0; c < 3; ++c) {
            triangle.centroid[c] = (p[0][c] + p[1][c] + p[2][c]) / 3.0f;
            triangle.power[c] = radiance[c] * area;
            low[c] = std::min(low[c], triangle.centroid[c]);
            high[c] = std::max(high[c], triangle.centroid[c]);
        }
        if (std::max({triangle.power[0], triangle.power[1], triangle.power[2]}) > 0.0f)
            triangles.push_back(triangle);
    }
    if (triangles.empty())
        return lights;
    const float size = std::max({high[0] - low[0], high[1] - low[1], high[2] - low[2]});
    float cellSize = std::max(MinCell, size / 4.0f);
    QHash<quint64, Cell> cells;
    for (int attempt = 0; attempt < 16; ++attempt) {
        cells.clear();
        for (const Triangle &triangle : triangles) {
            quint64 key = 0;
            for (int c = 0; c < 3; ++c)
                key = key * 2097152u + quint64(std::clamp(
                        int(std::floor((triangle.centroid[c] - low[c]) / cellSize)), 0, 2097151));
            Cell &cell = cells[key];
            const float weight = std::max({triangle.power[0], triangle.power[1], triangle.power[2]});
            for (int c = 0; c < 3; ++c) {
                cell.weighted[c] += double(triangle.centroid[c]) * weight;
                cell.power[c] += triangle.power[c];
            }
            cell.weight += weight;
            cell.area += triangle.area;
        }
        if (cells.size() <= maxEmitters)
            break;
        cellSize *= 2.0f;
    }
    // Cells in a stable order.
    QList<quint64> keys = cells.keys();
    std::sort(keys.begin(), keys.end());
    for (quint64 key : std::as_const(keys)) {
        const Cell &cell = cells.value(key);
        if (cell.weight <= 0.0)
            continue;
        RenderItem::Light light;
        light.emissive = true;
        double peak = 0.0;
        for (int c = 0; c < 3; ++c) {
            light.position[c] = float(cell.weighted[c] / cell.weight);
            peak = std::max(peak, cell.power[c]);
        }
        // M A / (4 pi): intensity in engine units, split into the brightest
        // channel and a colour.
        light.intensity = float(peak / (4.0 * M_PI));
        for (int c = 0; c < 3; ++c)
            light.color[c] = float(cell.power[c] / peak);
        // The radius of a sphere of the cell's emitting area.
        light.radius = float(std::sqrt(cell.area / (4.0 * M_PI)));
        lights.push_back(light);
    }
    return lights;
}

}
