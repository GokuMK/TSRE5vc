/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef FOGFLOOR_H
#define FOGFLOOR_H

#include <vector>

// Where fog lies (task renderer 33): a map around the camera of the ground
// cold air drains to. Fog fills valleys from their floor to a level top,
// thins on hills and plateaus that stand above the low ground around them
// (they shed their cold air), and is thicker over water.
//
// Cells are 64 m, 256 to a side (16 km); row 0 lies along the north edge,
// column 0 along the west edge (MSTS world metres: east, north).
class FogFloor {
public:
    static constexpr int Size = 256;
    static constexpr double CellSize = 64.0;
    // The floor: the lowest ground within this distance, smoothed.
    static constexpr double FloorRadius = 1500.0;
    // The region: the lowest floor within this distance.
    static constexpr double RegionRadius = 5000.0;

    // A place's values.
    struct Sample {
        bool known = false;
        float ground = 0.0f;
        float floor = 0.0f;        // m above sea level
        float aboveRegion = 0.0f;  // m the floor stands above the region's lowest
        float moisture = 0.0f;     // 0-1, near water
    };

    // From the cells' ground (m; NaN where no terrain is loaded) and water
    // (1 under water, else 0), Size x Size each, row by row.
    void build(double west, double north, const std::vector<float> &ground, const std::vector<float> &water);

    bool valid() const { return isValid; }
    double west() const { return westEdge; }
    double north() const { return northEdge; }
    // The cells without terrain.
    int unknownCells() const { return unknown; }

    // A cell's values; unknown outside the map.
    Sample cell(int column, int row) const;
    // The values at a place: the nearest cell.
    Sample at(double east, double north) const;

    // How much of the weather's fog's depth pools above a place's floor
    // (0-1): all in valleys and on low plains, little where the floor stands
    // high above the region.
    static float share(const Sample &s);
    // The fog's top at a place (m above sea level) for a fog of a depth: the
    // pooled fog, or up to half the depth lying on water and wet ground.
    static float top(const Sample &s, float depth);

private:
    bool isValid = false;
    double westEdge = 0.0, northEdge = 0.0;
    int unknown = 0;
    std::vector<float> groundCells, floorCells, regionCells, moistureCells;
};

#endif
