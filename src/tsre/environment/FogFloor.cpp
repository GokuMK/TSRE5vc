/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/FogFloor.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float None = std::numeric_limits<float>::infinity();
constexpr int N = FogFloor::Size;

float smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The lowest value within r cells along a line of n values (van Herk and
// Gil-Werman: a running minimum forwards and backwards in blocks of the
// window's width, three comparisons a value whatever the window).
void minLine(const float *in, float *out, int n, int stride, int r, std::vector<float> &g, std::vector<float> &h) {
    const int w = 2 * r + 1;
    const int m = n + 2 * r;
    g.resize(size_t(m));
    h.resize(size_t(m));
    auto value = [&](int i) {
        const int j = i - r;
        return j < 0 || j >= n ? None : in[j * stride];
    };
    for (int i = 0; i < m; ++i)
        g[size_t(i)] = i % w == 0 ? value(i) : std::min(g[size_t(i - 1)], value(i));
    for (int i = m - 1; i >= 0; --i)
        h[size_t(i)] = i % w == w - 1 || i == m - 1 ? value(i) : std::min(h[size_t(i + 1)], value(i));
    for (int j = 0; j < n; ++j)
        out[j * stride] = std::min(h[size_t(j)], g[size_t(j + 2 * r)]);
}

// The mean of the known values within r cells along a line.
void meanLine(const float *in, float *out, int n, int stride, int r, std::vector<double> &sum, std::vector<int> &count) {
    sum.assign(size_t(n + 1), 0.0);
    count.assign(size_t(n + 1), 0);
    for (int i = 0; i < n; ++i) {
        const float v = in[i * stride];
        const bool known = std::isfinite(v);
        sum[size_t(i + 1)] = sum[size_t(i)] + (known ? v : 0.0);
        count[size_t(i + 1)] = count[size_t(i)] + (known ? 1 : 0);
    }
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - r), b = std::min(n, i + r + 1);
        const int c = count[size_t(b)] - count[size_t(a)];
        out[i * stride] = c > 0 ? float((sum[size_t(b)] - sum[size_t(a)]) / c) : None;
    }
}

// Both along the rows, then down the columns.
void minArea(const std::vector<float> &in, std::vector<float> &out, int r) {
    std::vector<float> rows(in.size()), g, h;
    for (int y = 0; y < N; ++y)
        minLine(in.data() + y * N, rows.data() + y * N, N, 1, r, g, h);
    out.resize(in.size());
    for (int x = 0; x < N; ++x)
        minLine(rows.data() + x, out.data() + x, N, N, r, g, h);
}

void meanArea(const std::vector<float> &in, std::vector<float> &out, int r) {
    std::vector<float> rows(in.size());
    std::vector<double> sum;
    std::vector<int> count;
    for (int y = 0; y < N; ++y)
        meanLine(in.data() + y * N, rows.data() + y * N, N, 1, r, sum, count);
    out.resize(in.size());
    for (int x = 0; x < N; ++x)
        meanLine(rows.data() + x, out.data() + x, N, N, r, sum, count);
}

} // namespace

void FogFloor::build(double west, double north, const std::vector<float> &ground, const std::vector<float> &water,
                     const std::vector<float> &cellLowest) {
    isValid = false;
    const size_t cells = size_t(N) * N;
    if (ground.size() != cells || water.size() != cells || (!cellLowest.empty() && cellLowest.size() != cells))
        return;
    westEdge = west;
    northEdge = north;
    groundCells = ground;
    lowestCells = cellLowest.empty() ? ground : cellLowest;
    unknown = 0;
    std::vector<float> known(cells);
    for (size_t i = 0; i < cells; ++i) {
        const bool finite = std::isfinite(ground[i]);
        known[i] = finite ? ground[i] : None;
        unknown += finite ? 0 : 1;
    }
    if (unknown == int(cells))
        return;

    // The floor: the lowest ground near, smoothed over about 300 m (two
    // passes of five cells), never above the ground itself.
    std::vector<float> lowest, smoothed;
    minArea(known, lowest, int(std::lround(FloorRadius / CellSize)));
    meanArea(lowest, smoothed, 2);
    meanArea(smoothed, floorCells, 2);
    for (size_t i = 0; i < cells; ++i)
        if (std::isfinite(known[i]))
            floorCells[i] = std::min(floorCells[i], known[i]);

    // How far the floor stands above the region's lowest.
    std::vector<float> region;
    minArea(floorCells, region, int(std::lround(RegionRadius / CellSize)));
    std::vector<float> above(cells);
    for (size_t i = 0; i < cells; ++i)
        above[i] = std::isfinite(floorCells[i]) && std::isfinite(region[i]) ? floorCells[i] - region[i] : None;
    meanArea(above, regionCells, 2);

    // Moisture: water within a few cells; a narrow river counts nearly full.
    std::vector<float> wet;
    meanArea(water, wet, 2);
    moistureCells.resize(cells);
    for (size_t i = 0; i < cells; ++i)
        moistureCells[i] = std::isfinite(wet[i]) ? std::clamp(wet[i] * 3.0f, 0.0f, 1.0f) : 0.0f;
    isValid = true;
}

FogFloor::Sample FogFloor::cell(int column, int row) const {
    Sample s;
    if (!isValid || column < 0 || row < 0 || column >= N || row >= N)
        return s;
    const size_t i = size_t(row) * N + column;
    if (!std::isfinite(groundCells[i]) || !std::isfinite(floorCells[i]))
        return s;
    s.known = true;
    s.ground = groundCells[i];
    s.floor = floorCells[i];
    s.aboveRegion = std::isfinite(regionCells[i]) ? regionCells[i] : 0.0f;
    s.moisture = moistureCells[i];
    return s;
}

FogFloor::Sample FogFloor::at(double east, double north) const {
    return cell(int(std::floor((east - westEdge) / CellSize)), int(std::floor((northEdge - north) / CellSize)));
}

float FogFloor::share(const Sample &s) {
    return s.known ? 1.0f - smooth(30.0f, 250.0f, s.aboveRegion) : 0.0f;
}

float FogFloor::top(const Sample &s, float depth, float pooling) {
    // Pooled from the floor, or lying on water and wet ground wherever they
    // are (a pond on a hill), up to half the depth.
    return std::max(s.floor + depth * share(s) * pooling, s.ground + depth * 0.5f * s.moisture);
}
