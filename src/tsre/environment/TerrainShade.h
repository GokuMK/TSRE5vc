/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TERRAINSHADE_H
#define TERRAINSHADE_H

#include <tsre/environment/FogFloor.h>

#include <vector>

// The terrain's shade of a low sun or moon past the shadow maps, which hold
// what stands within 700 m of the camera (user 2026-10-14: at 20:00 the sun
// set behind a hill 1 km off and still lit the haze and the fields in front
// of it). Over the fog floor's grid (16 km, cells of 64 m), the height under
// which the ground towards the light hides it.
//
// Built from each cell's lowest ground, so a narrow valley is not walled in
// by its sides: lines of cells from the light's side, each cell's top the
// higher of the ground and the top one cell towards the light, less the
// light's fall over that step.
class TerrainShade {
public:
    static constexpr int Size = FogFloor::Size;
    static constexpr double CellSize = FogFloor::CellSize;
    // The light fades over this height (m) under the top: the sun's disc
    // seen past a ridge about a kilometre away.
    static constexpr float Band = 4.0f;

    // From a floor map and the direction towards the light (x east, y up,
    // z south).
    void build(const FogFloor &floor, const float towards[3]);
    // From a floor map's north-west corner and lowest ground in its cells.
    void build(double west, double north, const std::vector<float> &lowest, const float towards[3]);

    bool valid() const { return isValid; }
    double west() const { return westEdge; }
    double north() const { return northEdge; }
    // Each cell's top (m above sea level), row 0 along the north edge: the
    // light is hidden under it. A lit cell's is the lowest ground about it,
    // under whatever stands there.
    const std::vector<float> &tops() const { return topCells; }
    // A height near the middle's ground, for storing the tops as half floats.
    float reference() const { return referenceHeight; }

    // The light at a place (MSTS world metres) and height: 1 lit, 0 hidden;
    // between cells' middles, their tops blended.
    float light(double east, double north, float height) const;

private:
    bool isValid = false;
    double westEdge = 0.0, northEdge = 0.0;
    float referenceHeight = 0.0f;
    std::vector<float> topCells;
};

#endif
