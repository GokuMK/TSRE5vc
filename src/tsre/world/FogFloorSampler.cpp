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
    building = std::async(std::launch::async, [west = west, north = northEdge, ground = std::move(ground),
                                               water = std::move(water), lowest = std::move(lowest)] {
        FogFloor floor;
        floor.build(west, north, ground, water, lowest);
        return floor;
    });
    return false;
}
