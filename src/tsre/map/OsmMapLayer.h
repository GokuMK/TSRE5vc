/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef OSMMAPLAYER_H
#define OSMMAPLAYER_H

#include <QString>
#include <functional>
#include <tsre/geo/osm/OsmTypes.h>
#include "MapLabelLayer.h"
#include <cstdint>
#include <memory>
#include <vector>

class GeoWorldCoordinateConverter;
class MapView;
class OglObj;
class RenderQueue;
struct MapPalette;

// OSM data in map mode (docs/tasks/geo/osm-rendering-design.md): the user's OSM
// directory drawn as vector geometry (Osm::MapGeometry) between the terrain and the
// Faded Overlay, from the detail file or an overview level by the view's scale.
//
// Geometry is built on a worker thread for the view plus a margin, and drawn until
// the next build is ready. It is kept relative to the tile it was built for and
// drawn shifted to the view's tile, so panning rebuilds only past the margin. Zoom
// steps rebuild only the strokes (line widths); a scale range, an overview level or
// a factor of two in scale reloads. The coarsest overview level loads whole, once.
class OsmMapLayer {
public:
    // The band of heights it draws in: above the terrain textures (35), under the
    // terrain aids (81, TerrainMapLayer).
    static constexpr float BaseHeight = 50.0f;
    static constexpr float HeightStep = 0.7f;
    // The built area: the view plus this much of its size on each side.
    static constexpr float Margin = 0.25f;
    // Scale changes past these factors rebuild the strokes, or reload.
    static constexpr double RestrokeScale = 1.25;
    static constexpr double ReloadScale = 2.0;
    // The coarsest overview level loads whole, in squares of this size (culled off screen).
    static constexpr float WholeChunkMeters = 128000.0f;

    OsmMapLayer();
    ~OsmMapLayer();
    // Called on the worker thread when a build is ready (the view should draw again).
    void setReadyCallback(std::function<void()> callback);
    // Draws what is built and asks for a new build when the view needs one. directory is
    // the OSM directory setting; transparentAreas draws fills at the palette's
    // osmAreaAlpha. The dark palette draws the dark styles; switching palettes loads
    // again.
    void pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                         const QString &directory, bool transparentAreas);
    // Loads again on the next draw (the directory's files changed).
    void invalidate();
    // A build is running or waiting.
    bool busy() const;
    // Named places and railway stations of the drawn data, for the map's labels; the
    // version changes with them.
    const std::vector<MapLabel> &labels() const;
    uint64_t labelsVersion() const;

    // The ground position (metres, relative to a tile in the editor's convention) of a
    // latitude and longitude, and back, through a route's converter.
    static void toGround(GeoWorldCoordinateConverter *converter, double lat, double lon, int tileX,
                         int tileZ, float &x, float &z);
    static void toLatLon(GeoWorldCoordinateConverter *converter, int tileX, int tileZ, double x,
                         double z, double &lat, double &lon);
    // The latitude and longitude box of a ground rectangle (min x, max x, min z, max z
    // relative to a tile), from points around and across it.
    static Osm::Box areaOf(GeoWorldCoordinateConverter *converter, int tileX, int tileZ,
                           const float *rect);
    // The ground rectangle a view shows, relative to its tile: min x, max x, min z, max z.
    static void viewRect(const MapView &view, float *rect);

private:
    struct Job;
    struct Result;
    class Worker;
    struct Drawn;
    void apply(Result &result, const MapPalette &palette, bool transparentAreas);
    void request(const MapView &view, const QString &directory, bool dark);

    std::unique_ptr<Worker> worker;
    std::unique_ptr<Drawn> drawn;
    // What was last asked for, so a build is asked once.
    bool requested = false;
    bool invalid = true;
    QString requestedDirectory;
    bool requestedDark = false;
    int requestedTile[2] = {0, 0};
    float requestedRect[4] = {0, 0, 0, 0};
    double requestedScale = 0;
    int requestedLevel = -1;
    double requestedStrokeScale = 0;
    uint64_t requestedLoad = 0;
    bool requestedWhole = false;
    bool wholeNext = false;  // the view's load runs; the whole level follows it
};

#endif
