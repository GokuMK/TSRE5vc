/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/world/FogFloorSampler.h>

#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainLib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace {

constexpr int N = FogFloor::Size;
constexpr double Cell = FogFloor::CellSize;
constexpr double Tile = 2048.0;

// The world tile a coordinate lies in, the terrain's way: tiles centred on
// multiples of 2048 m. The terrain's z runs south (MSTS north negated).
int tileOf(double metres) {
    return int(std::floor((metres + Tile * 0.5) / Tile));
}

} // namespace

std::vector<int> FogFloorSampler::outlierTiles(const std::vector<float> &ground, const std::vector<TileCells> &tiles) {
    std::vector<int> rejected;
    if (ground.size() != size_t(N) * N)
        return rejected;
    auto at = [&](int r, int c) { return ground[size_t(r) * N + c]; };
    // The mean step across the edge two tiles share (NaN if they share none
    // or too few of its cells are known): b east of a, or south.
    auto edgeStep = [&](const TileCells &a, const TileCells &b, bool east) {
        double sum = 0.0;
        int count = 0;
        if (east ? b.c0 != a.c1 + 1 : b.r0 != a.r1 + 1)
            return std::numeric_limits<double>::quiet_NaN();
        const int from = east ? std::max(a.r0, b.r0) : std::max(a.c0, b.c0);
        const int to = east ? std::min(a.r1, b.r1) : std::min(a.c1, b.c1);
        for (int k = from; k <= to; ++k) {
            const float ha = east ? at(k, a.c1) : at(a.r1, k), hb = east ? at(k, b.c0) : at(b.r0, k);
            if (std::isfinite(ha) && std::isfinite(hb)) {
                sum += std::abs(double(ha) - hb);
                ++count;
            }
        }
        return count >= 4 ? sum / count : std::numeric_limits<double>::quiet_NaN();
    };
    // The pieces of terrain: tiles joined by the edges they agree on.
    auto find = [&](int x, int z) {
        for (size_t i = 0; i < tiles.size(); ++i)
            if (tiles[i].x == x && tiles[i].z == z && tiles[i].c1 >= tiles[i].c0)
                return int(i);
        return -1;
    };
    std::vector<std::vector<int>> joined(tiles.size());
    std::vector<std::pair<int, int>> apart;
    for (size_t i = 0; i < tiles.size(); ++i) {
        const TileCells &t = tiles[i];
        if (t.c1 < t.c0)
            continue;
        const int towards[2][2] = {{1, 0}, {0, 1}};
        for (const auto &d : towards) {
            const int j = find(t.x + d[0], t.z + d[1]);
            if (j < 0)
                continue;
            const double step = edgeStep(t, tiles[size_t(j)], d[0] != 0);
            if (!std::isfinite(step))
                continue;
            if (step > MaxEdgeStep) {
                apart.emplace_back(int(i), j);
            } else {
                joined[i].push_back(j);
                joined[size_t(j)].push_back(int(i));
            }
        }
    }
    std::vector<int> piece(tiles.size(), -1), size;
    for (size_t i = 0; i < tiles.size(); ++i) {
        if (piece[i] >= 0 || tiles[i].c1 < tiles[i].c0)
            continue;
        const int id = int(size.size());
        size.push_back(0);
        std::vector<int> open{int(i)};
        piece[i] = id;
        while (!open.empty()) {
            const int k = open.back();
            open.pop_back();
            ++size[size_t(id)];
            for (int j : joined[size_t(k)])
                if (piece[size_t(j)] < 0) {
                    piece[size_t(j)] = id;
                    open.push_back(j);
                }
        }
    }
    // The largest first: a piece is wrong where it lies apart from a larger
    // one that is not (the route's terrain a whole; not a piece beyond a
    // wrong one, nor two of a size).
    std::vector<int> order(size.size());
    for (size_t p = 0; p < order.size(); ++p)
        order[p] = int(p);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return size[size_t(a)] > size[size_t(b)]; });
    std::vector<bool> wrong(size.size(), false);
    for (int p : order)
        for (const auto &edge : apart) {
            const int a = piece[size_t(edge.first)], b = piece[size_t(edge.second)];
            const int other = a == p ? b : b == p ? a : -1;
            if (other >= 0 && other != p && size[size_t(other)] > size[size_t(p)] && !wrong[size_t(other)]) {
                wrong[size_t(p)] = true;
                break;
            }
        }
    for (size_t i = 0; i < tiles.size(); ++i)
        if (piece[i] >= 0 && wrong[size_t(piece[i])])
            rejected.push_back(int(i));
    return rejected;
}

void FogFloorSampler::start(double atEast, double atNorth) {
    // A map still being built is let go (its thread finishes first).
    if (building.valid())
        building = std::future<FogFloor>();
    east = atEast;
    north = atNorth;
    const double half = N * Cell * 0.5;
    west = std::floor((atEast - half) / Cell) * Cell;
    northEdge = std::ceil((atNorth + half) / Cell) * Cell;
    ground.assign(size_t(N) * N, std::numeric_limits<float>::quiet_NaN());
    water.assign(size_t(N) * N, 0.0f);
    lowest.assign(size_t(N) * N, std::numeric_limits<float>::quiet_NaN());
    pending.clear();
    sampled.clear();
    const int x0 = tileOf(west), x1 = tileOf(west + N * Cell - 1.0);
    const int z0 = tileOf(-northEdge), z1 = tileOf(-(northEdge - N * Cell) - 1.0);
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
            pending.emplace_back(x, z);
    next = 0;
    detailedTiles = distantTiles = missingTiles = 0;
    running = true;
}

bool FogFloorSampler::step(TerrainLib &terrain, int tiles, FogFloor &out) {
    if (!running)
        return false;
    if (building.valid()) {
        if (building.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return false;
        out = building.get();
        running = false;
        return true;
    }
    for (int done = 0; done < tiles && next < pending.size(); ++done, ++next) {
        const int x = pending[next].first, z = pending[next].second;
        Terrain *tile = terrain.groundTile(x, z);
        if (tile == nullptr) {
            ++missingTiles;
            continue;
        }
        ++(tile->lowTile ? distantTiles : detailedTiles);
        // The cells whose centres lie in this world tile.
        const double tileWest = x * Tile - Tile * 0.5, tileNorthZ = z * Tile - Tile * 0.5;
        const int c0 = std::max(0, int(std::ceil((tileWest - west) / Cell - 0.5)));
        const int c1 = std::min(N - 1, int(std::ceil((tileWest + Tile - west) / Cell - 0.5)) - 1);
        // Rows run south; a cell's terrain z is -(its north).
        const int r0 = std::max(0, int(std::ceil((tileNorthZ + northEdge) / Cell - 0.5)));
        const int r1 = std::min(N - 1, int(std::ceil((tileNorthZ + Tile + northEdge) / Cell - 0.5)) - 1);
        sampled.push_back({x, z, c0, c1, r0, r1});
        for (int r = r0; r <= r1; ++r) {
            const float posz = float(-(northEdge - (r + 0.5) * Cell) - z * Tile);
            for (int c = c0; c <= c1; ++c) {
                const float posx = float(west + (c + 0.5) * Cell - x * Tile);
                const float height = tile->getHeight(x, z, posx, posz, false);
                const size_t i = size_t(r) * N + c;
                ground[i] = height;
                lowest[i] = std::min(height, tile->lowestHeight(x, z, posx, posz, float(Cell * 0.5)));
                float level = 0.0f;
                water[i] = tile->waterAt(x, z, posx, posz, level) && level > height ? 1.0f : 0.0f;
            }
        }
    }
    if (next < pending.size())
        return false;
    // Wrong tiles: their cells as without terrain.
    rejectedTiles.clear();
    for (int index : outlierTiles(ground, sampled)) {
        const TileCells &t = sampled[size_t(index)];
        rejectedTiles.emplace_back(t.x, t.z);
        for (int r = t.r0; r <= t.r1; ++r)
            for (int c = t.c0; c <= t.c1; ++c) {
                const size_t i = size_t(r) * N + c;
                ground[i] = lowest[i] = std::numeric_limits<float>::quiet_NaN();
                water[i] = 0.0f;
            }
    }
    building = std::async(std::launch::async, [west = west, north = northEdge, ground = std::move(ground),
                                               water = std::move(water), lowest = std::move(lowest)] {
        FogFloor floor;
        floor.build(west, north, ground, water, lowest);
        return floor;
    });
    return false;
}
