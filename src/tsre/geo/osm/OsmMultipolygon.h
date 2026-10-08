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

// Multipolygon assembly: member ways of a type=multipolygon/boundary relation are
// joined into closed rings by their end node ids; inner rings go to the smallest
// outer ring that contains them. Broken rings are dropped, not repaired.

#include <tsre/geo/osm/OsmStore.h>
#include <functional>
#include <string>
#include <vector>

namespace Osm {

// A relation copied out of a Feature, so it outlives the query callback.
struct RelationData {
    struct MemberRef { int64_t ref; ItemType type; std::string role; };
    int64_t id = 0;
    Box extent;
    std::vector<std::pair<std::string, std::string>> tags;
    std::vector<MemberRef> members;

    static RelationData from(const Feature &relation);
    std::string_view value(std::string_view key) const;
};

struct WayGeometry {
    std::vector<int64_t> refs;
    std::vector<Location> locations;  // parallel to refs
};

// Rings are closed (first point == last point); outer rings counter-clockwise, inner clockwise.
struct Polygon {
    std::vector<Location> outer;
    std::vector<std::vector<Location>> inners;
};

struct MultipolygonResult {
    int64_t id = 0;
    std::vector<Polygon> polygons;
    uint32_t brokenRings = 0;     // open chains and missing member ways
    uint32_t orphanInners = 0;    // inner rings inside no outer ring (dropped)
    bool ok() const { return !polygons.empty(); }
};

// way(id) returns the member way's geometry or null when it is not available.
MultipolygonResult assembleMultipolygon(const RelationData &relation, const std::function<const WayGeometry *(int64_t)> &way);

// Fetches the member ways of all relations from the store in one query and assembles each.
bool assembleMultipolygons(const OsmStore &store, const std::vector<RelationData> &relations,
                           std::vector<MultipolygonResult> &results, QString &error);

// Signed area (shoelace) in coordinate units squared; positive when counter-clockwise.
double signedArea(const std::vector<Location> &ring);
// Even-odd point in polygon test against a closed ring.
bool pointInRing(Location p, const std::vector<Location> &ring);

}
