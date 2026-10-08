/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#pragma once

// Area generalization for coarse overview levels, as printed maps draw forests at
// country scale: the areas are drawn into a grid of cells, gaps narrower than a closing
// distance are closed, and the cells' outlines are traced back into simplified polygons
// with their holes. Neighbouring parcels and woods become one shape whether or not they
// share nodes; what is smaller than the minimum area after merging is left out.

#include <tsre/geo/osm/OsmTypes.h>
#include <vector>

namespace Osm {

struct GeneralizeOptions {
    double cellMeters = 100;
    double closeMeters = 100;   // gaps up to about twice this are closed (0: none)
    double minAreaKm2 = 0.25;   // merged shapes smaller than this are left out
    double minHoleKm2 = 0.05;   // and holes smaller than this are filled
    double toleranceMeters = 75;
};

// A ring is closed (first point == last); outers counter-clockwise, holes clockwise.
struct GeneralizedArea {
    std::vector<Location> outer;
    std::vector<std::vector<Location>> holes;
};

struct GeneralizeStats {
    uint64_t polygonsIn = 0, cells = 0, filledCells = 0, areas = 0, holes = 0, pointsOut = 0;
    double areaInKm2 = 0, areaOutKm2 = 0;  // areaIn counts overlaps twice
};

// Each input polygon is its rings (closed, the first outer, the rest holes, filled
// even-odd). Cells whose centre lies inside a polygon are filled.
std::vector<GeneralizedArea> generalizeAreas(const std::vector<std::vector<std::vector<Location>>> &polygons,
                                             const GeneralizeOptions &options, GeneralizeStats *stats = nullptr);

}
