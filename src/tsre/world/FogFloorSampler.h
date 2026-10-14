/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef FOGFLOORSAMPLER_H
#define FOGFLOORSAMPLER_H

#include <tsre/environment/FogFloor.h>

#include <future>
#include <utility>
#include <vector>

class TerrainLib;

// Fills a fog floor map (task renderer 33) from the loaded terrain around a
// place: the detailed tiles, else the distant ones; each cell's ground at its
// middle and the lowest in it (for the terrain's shade, TerrainShade). A few
// world tiles a call, on the thread that owns the terrain (it is not safe to
// read elsewhere); the map is then built on a worker thread (6 ms on the
// Steam Deck).
class FogFloorSampler {
public:
    // A map about a place (MSTS world metres), its corner on the cells' grid.
    void start(double east, double north);
    bool active() const { return running; }
    // Sampling tiles (not waiting for the map to be built).
    bool sampling() const { return running && !building.valid(); }
    double centreEast() const { return east; }
    double centreNorth() const { return north; }
    // Samples up to `tiles` world tiles; once all are, builds the map, and
    // once it is built moves it into `out` and returns true.
    bool step(TerrainLib &terrain, int tiles, FogFloor &out);
    // World tiles sampled from detailed and from distant terrain, and without.
    int detailedTiles = 0, distantTiles = 0, missingTiles = 0;

private:
    bool running = false;
    double east = 0.0, north = 0.0;
    double west = 0.0, northEdge = 0.0;
    std::vector<float> ground, water, lowest;
    std::vector<std::pair<int, int>> pending;  // world tiles (x, z as the terrain counts them)
    size_t next = 0;
    std::future<FogFloor> building;
};

#endif
