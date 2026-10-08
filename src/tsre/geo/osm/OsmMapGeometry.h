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

// Vector geometry of OSM data for a top-down view (the Route Editor's map mode,
// docs/tasks/geo/osm-rendering-design.md), free of any renderer: triangles,
// triangle strips and lines in the view's ground coordinates, x, y, z a vertex,
// batched by colour and draw order as the tile map draws them (MapDataOSM::paint).
//
// load() reads an area and keeps what is independent of the scale: features whose
// style is visible at the scale and that are at least MinPixels across, simplified and
// projected, with their areas triangulated (earcut).
// strokes() builds the scale-dependent lines from that: a stroke at least StripPixels
// wide is a triangle strip of its width in metres with mitred joins, a thinner one a
// one-pixel line.

#include <tsre/geo/osm/OsmClasses.h>
#include <tsre/geo/osm/OsmStore.h>
#include <QString>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

namespace Osm {

// Projects count locations into the view's ground coordinates: x, z pairs in metres.
using MapProjection = std::function<void(const Location *in, size_t count, float *xz)>;
// A ring of x, z points without the repeated closing point.
using MapRing = std::vector<std::array<float, 2>>;

struct MapBatch {
    enum Primitive : uint8_t { Triangles, TriangleStrip, Lines };
    // Draw order: higher draws over lower. Per slot (bridges and OSM layers, as the tile
    // map orders them): fills, outlines, casings, lines.
    float order = 0;
    Rgb color = 0;
    Primitive primitive = Triangles;
    std::vector<float> vertices;  // x, y, z; y = baseHeight + order * heightStep
};

class MapGeometry {
public:
    static constexpr int Slots = 10;
    static constexpr float OrdersPerSlot = 4;
    // Strokes this many pixels wide or more are strips; thinner ones one-pixel lines.
    static constexpr float StripPixels = 1.5f;
    // Joins sharper than this miter length (in half widths) get two vertex pairs.
    static constexpr float MiterLimit = 2.0f;
    // Areas and closed ways smaller than this across (pixels at the load's scale) are left
    // out: specks that cost as many vertices as visible features. Open ways stay, as they
    // may be short pieces of long roads and rivers.
    static constexpr float MinPixels = 2.0f;
    // Geometry is simplified to this many pixels (Douglas-Peucker) when that is at least
    // SimplifyFromMeters: detail no pixel shows, and most of the vertices at coarse scales.
    static constexpr float SimplifyPixels = 0.5f;
    static constexpr float SimplifyFromMeters = 0.5f;

    struct Options {
        float baseHeight = 0;
        float heightStep = 1;
        int threads = 0;  // for simplifying, projecting and triangulating; 0: all hardware threads
    };
    struct Stats {
        uint64_t ways = 0, relations = 0, polygons = 0, triangles = 0;
        uint64_t polylines = 0, points = 0;
        uint64_t culled = 0;  // features under MinPixels
        uint64_t pointsRead = 0;  // before simplification
        // Reading (both passes), multipolygon assembly, and the parallel part: simplifying,
        // projecting and triangulating.
        double readSeconds = 0, assembleSeconds = 0, processSeconds = 0;
    };

    explicit MapGeometry(const FeatureClasses &classes = FeatureClasses::standard());

    // Reads area from store and keeps the features drawn at metersPerPixel. Replaces the
    // previous content; false on a store error or when cancelled.
    bool load(const OsmStore &store, const Box &area, double metersPerPixel, const MapProjection &project,
              const Options &options, QString &error, const std::atomic_bool *cancel = nullptr);
    // Whether a load at metersPerPixel would keep the same styles, so strokes() is enough.
    bool sameStyles(double metersPerPixel) const;
    double loadedMetersPerPixel() const { return loadedMetersPerPixel_; }

    // Area fills from load(), one batch per slot and colour.
    const std::vector<MapBatch> &fills() const { return out_.fills; }
    // Moves the fills out (they are uploaded once; strokes() does not need them).
    std::vector<MapBatch> takeFills() { out_.fillIndex.clear(); return std::move(out_.fills); }
    // Outlines, casings and lines for a scale.
    void strokes(double metersPerPixel, std::vector<MapBatch> &out) const;
    const Stats &stats() const { return stats_; }

    // Building blocks, public for tests. Strokes append to out: a strip continues out's
    // strip with degenerate triangles; points are x, z pairs.
    static void appendStrip(std::vector<float> &out, const float *xz, size_t count, bool closed, float width, float y);
    static void appendLines(std::vector<float> &out, const float *xz, size_t count, float y);
    // Triangulates rings (the first outer, the rest holes) and appends triangles wound as
    // the map's ribbons, so its face culling keeps them. Returns the number of triangles.
    static size_t appendFill(std::vector<float> &out, const std::vector<MapRing> &rings, float y);

private:
    // Features of one slot and style: their rings and ways as polylines.
    struct Group {
        int slot = 0;
        const Style *style = nullptr;
        std::vector<float> points;      // x, z pairs
        std::vector<uint32_t> starts;   // first point of each polyline; points.size() / 2 ends the last
        std::vector<uint8_t> closed;    // per polyline
    };
    // What load() builds; each worker thread builds one for its share, merged in order.
    struct Output {
        std::vector<Group> groups;
        std::map<std::pair<int, const Style *>, size_t> groupIndex;
        std::vector<MapBatch> fills;
        std::unordered_map<uint64_t, size_t> fillIndex;
        Stats stats;  // counts
        Group &group(int slot, const Style *style);
        void addPolyline(Group &g, const float *xz, size_t count, bool closed);
        MapBatch &fillBatch(int slot, Rgb color);
        void append(Output &&other);
    };

    const FeatureClasses &classes_;
    const std::vector<float> scaleRanges_;
    Options options_;
    double loadedMetersPerPixel_ = 0;
    Output out_;
    Stats stats_;
};

}
