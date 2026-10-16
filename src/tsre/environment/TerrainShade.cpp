/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/TerrainShade.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int N = TerrainShade::Size;

} // namespace

void TerrainShade::build(const FogFloor &floor, const float towards[3]) {
    isValid = false;
    if (floor.valid())
        build(floor.west(), floor.north(), floor.lowestGround(), towards);
}

void TerrainShade::build(double west, double north, const std::vector<float> &lowest, const float towards[3]) {
    isValid = false;
    const size_t cells = size_t(N) * N;
    if (lowest.size() != cells)
        return;
    westEdge = west;
    northEdge = north;
    // Where no terrain is loaded: far under the known ground, hiding nothing.
    float lowestKnown = std::numeric_limits<float>::infinity();
    for (float height : lowest)
        if (std::isfinite(height))
            lowestKnown = std::min(lowestKnown, height);
    if (!std::isfinite(lowestKnown))
        return;
    const float none = lowestKnown - 1000.0f;
    std::vector<float> ground(cells);
    for (size_t i = 0; i < cells; ++i)
        ground[i] = std::isfinite(lowest[i]) ? lowest[i] : none;

    // The height under which the ground towards the light hides it, from the
    // cells one step towards the light: their ground or their own top, less
    // the light's fall over the step. Nothing hides a light overhead.
    std::vector<float> hidden(cells, none);
    const float across = std::hypot(towards[0], towards[2]);
    const float length = std::hypot(across, towards[1]);
    if (length > 0.0f && across > 1e-4f * length) {
        // A light under the horizon falls as one on it.
        const float rise = std::max(towards[1], 0.0f) / across;
        // Columns run east, rows south (as z).
        const float dx = towards[0] / across, dz = towards[2] / across;
        // Lines along the axis nearer the light's: a step of one cell along
        // it, part of one across.
        const bool alongColumns = std::abs(dx) >= std::abs(dz);
        const float along = alongColumns ? dx : dz, aside = alongColumns ? dz : dx;
        const int step = along > 0.0f ? 1 : -1;
        const float slide = aside / std::abs(along);
        const float fall = float(CellSize) / std::abs(along) * rise;
        auto index = [&](int a, int b) {
            return alongColumns ? size_t(b) * N + size_t(a) : size_t(a) * N + size_t(b);
        };
        for (int n = 1; n < N; ++n) {
            const int a = step > 0 ? N - 1 - n : n;
            const int up = a + step;
            for (int b = 0; b < N; ++b) {
                const float place = float(b) + slide;
                const int b0 = int(std::floor(place));
                const float f = place - float(b0);
                // The lower ground of the two cells the step lands between (a
                // valley in either stays open), their tops blended.
                float g = std::numeric_limits<float>::infinity(), h = 0.0f, w = 0.0f;
                for (int k = 0; k < 2; ++k) {
                    const int side = b0 + k;
                    const float weight = k == 0 ? 1.0f - f : f;
                    if (side < 0 || side >= N || weight < 1e-3f)
                        continue;
                    const size_t j = index(up, side);
                    g = std::min(g, ground[j]);
                    h += weight * hidden[j];
                    w += weight;
                }
                if (w > 0.0f)
                    hidden[index(a, b)] = std::max(std::max(g, h / w) - fall, none);
            }
        }
    }

    // A cell lit down to its lowest ground keeps as its top the lowest ground
    // within a cell of its middle, under anything about it: a lit valley's
    // sides (their tops just under their own ground) would otherwise wall it
    // in as the tops blend between cells. It takes its own top as that
    // reaches its ground.
    std::vector<float> nearby(cells);
    for (int row = 0; row < N; ++row) {
        const float *line = &ground[size_t(row) * N];
        float *out = &nearby[size_t(row) * N];
        for (int column = 0; column < N; ++column)
            out[column] = std::min({line[std::max(column - 1, 0)], line[column], line[std::min(column + 1, N - 1)]});
    }
    topCells.resize(cells);
    for (int row = 0; row < N; ++row) {
        const float *above = &nearby[size_t(std::max(row - 1, 0)) * N];
        const float *line = &nearby[size_t(row) * N];
        const float *below = &nearby[size_t(std::min(row + 1, N - 1)) * N];
        for (int column = 0; column < N; ++column) {
            const float about = std::min({above[column], line[column], below[column]});
            const size_t i = size_t(row) * N + size_t(column);
            const float t = std::clamp((hidden[i] - (ground[i] - 2.0f)) / 6.0f, 0.0f, 1.0f);
            topCells[i] = about + (std::max(hidden[i], about) - about) * t * t * (3.0f - 2.0f * t);
        }
    }
    const float middle = lowest[size_t(N / 2) * N + size_t(N / 2)];
    referenceHeight = std::isfinite(middle) ? middle : lowestKnown;
    isValid = true;
}

float TerrainShade::light(double east, double north, float height) const {
    if (!isValid)
        return 1.0f;
    const double u = (east - westEdge) / CellSize - 0.5, v = (northEdge - north) / CellSize - 0.5;
    if (u < -0.5 || v < -0.5 || u > N - 0.5 || v > N - 0.5)
        return 1.0f;
    const double cu = std::clamp(u, 0.0, double(N - 1)), cv = std::clamp(v, 0.0, double(N - 1));
    const int c0 = std::min(int(cu), N - 2), r0 = std::min(int(cv), N - 2);
    const float fu = float(cu - c0), fv = float(cv - r0);
    auto top = [&](int r, int c) { return topCells[size_t(r) * N + size_t(c)]; };
    const float t = (top(r0, c0) * (1.0f - fu) + top(r0, c0 + 1) * fu) * (1.0f - fv)
            + (top(r0 + 1, c0) * (1.0f - fu) + top(r0 + 1, c0 + 1) * fu) * fv;
    return std::clamp((height - t) / Band + 1.0f, 0.0f, 1.0f);
}
