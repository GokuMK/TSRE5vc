/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPFEATURES_H
#define MAPFEATURES_H

#include <vector>

// What an activity or path part gives the map mode to draw (task editor
// 04), in metres relative to a tile in the editor's tile convention (x
// east, z south). The map picks colours and marker shapes by who gave
// them.
struct MapFeatures {
    // Footprint floats a vehicle: centre x and z, unit direction x and z,
    // length, width, and 1 for an engine or 0 for a wagon.
    static constexpr int VehicleFloats = 7;

    // Line segments along the track: pairs of x, y, z points.
    std::vector<float> lines;
    // Marker centres: x, y, z.
    std::vector<float> points;
    // Vehicle footprints, VehicleFloats each.
    std::vector<float> vehicles;
    // Circular areas: centre x, z and radius.
    std::vector<float> areas;

    bool empty() const {
        return lines.empty() && points.empty() && vehicles.empty() && areas.empty();
    }
};

#endif
