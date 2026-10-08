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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Osm {

// OSM fixed-point coordinates: 1e-7 degree units, x = longitude, y = latitude.
constexpr double CoordinateScale = 1e7;

struct Location {
    int32_t x = std::numeric_limits<int32_t>::min();
    int32_t y = std::numeric_limits<int32_t>::min();

    Location() = default;
    Location(int32_t px, int32_t py) : x(px), y(py) {}
    static Location fromDegrees(double lon, double lat) {
        return {int32_t(std::lround(lon * CoordinateScale)), int32_t(std::lround(lat * CoordinateScale))};
    }
    bool valid() const { return x != std::numeric_limits<int32_t>::min(); }
    double lon() const { return x / CoordinateScale; }
    double lat() const { return y / CoordinateScale; }
    bool operator==(const Location &o) const { return x == o.x && y == o.y; }
    bool operator!=(const Location &o) const { return !(*this == o); }
};

struct Box {
    int32_t minX = std::numeric_limits<int32_t>::max();
    int32_t minY = std::numeric_limits<int32_t>::max();
    int32_t maxX = std::numeric_limits<int32_t>::min();
    int32_t maxY = std::numeric_limits<int32_t>::min();

    static Box fromDegrees(double minLon, double minLat, double maxLon, double maxLat) {
        Box b; b.extend(Location::fromDegrees(minLon, minLat)); b.extend(Location::fromDegrees(maxLon, maxLat));
        return b;
    }
    bool valid() const { return minX <= maxX && minY <= maxY; }
    void extend(Location l) {
        minX = std::min(minX, l.x); minY = std::min(minY, l.y);
        maxX = std::max(maxX, l.x); maxY = std::max(maxY, l.y);
    }
    void extend(const Box &b) {
        if (!b.valid()) return;
        minX = std::min(minX, b.minX); minY = std::min(minY, b.minY);
        maxX = std::max(maxX, b.maxX); maxY = std::max(maxY, b.maxY);
    }
    bool intersects(const Box &b) const {
        return valid() && b.valid() && minX <= b.maxX && b.minX <= maxX && minY <= b.maxY && b.minY <= maxY;
    }
    bool contains(Location l) const { return l.x >= minX && l.x <= maxX && l.y >= minY && l.y <= maxY; }
    bool containsBox(const Box &b) const { return valid() && b.valid() && b.minX >= minX && b.maxX <= maxX && b.minY >= minY && b.maxY <= maxY; }
    bool operator==(const Box &o) const { return minX == o.minX && minY == o.minY && maxX == o.maxX && maxY == o.maxY; }
};

// Values match the PBF Relation.MemberType enum.
enum class ItemType : uint8_t { Node = 0, Way = 1, Relation = 2 };

}
