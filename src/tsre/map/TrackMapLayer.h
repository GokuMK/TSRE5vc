/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef TRACKMAPLAYER_H
#define TRACKMAPLAYER_H

#include <QColor>
#include <QString>
#include <memory>
#include <vector>

class MapView;
class OglObj;
class RenderQueue;
class TDB;
struct MapPalette;

// The track and road databases in map mode: lines of a fixed screen width
// and junction and end markers of a fixed screen size, in the palette's
// colours. The whole route is drawn as the straight chords between vector
// sections; zoomed in (DetailMetresPerPixel or less), the track nodes with
// sections in view follow their curves.
class TrackMapLayer {
public:
    static constexpr float DetailMetresPerPixel = 1.0f;
    static constexpr float LinePixels = 2.0f;
    static constexpr float MarkerPixels = 7.0f;
    // Heights the map draws its layers at, whatever the ground's height, so
    // the depth test orders the layers (MapView's eye is above them).
    static constexpr float RoadHeight = 100.0f;
    static constexpr float TrackHeight = 200.0f;
    static constexpr float EndHeight = 300.0f;
    static constexpr float JunctionHeight = 400.0f;
    static constexpr float PointerHeight = 500.0f;

    TrackMapLayer();
    ~TrackMapLayer();
    // Draws the databases for the view, rebuilding the geometry when the
    // view moved to another tile or zoomed enough to change line widths.
    void pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                         TDB *track, TDB *road);
    // Builds again on the next draw (the databases changed).
    void invalidate() { valid = false; }

    // Ribbons of a width on the ground around line segments (pairs of x, y,
    // z points): two triangles a segment, x, y, z a vertex. Public for tests.
    static void appendRibbons(std::vector<float> &out, const float *segments, int segmentCount,
                              float width);
    // A square of a side on the ground, aligned to the screen (right and up
    // directions in x and z), as two triangles.
    static void appendSquare(std::vector<float> &out, float x, float y, float z, float side,
                             float rx, float rz, float ux, float uz);
    // An octagon of a width on the ground, as six triangles: a marker that
    // looks the same however the map is turned, so turning needs no new
    // geometry.
    static void appendOctagon(std::vector<float> &out, float x, float y, float z, float width);

private:
    struct Geometry;
    void build(const MapView &view, const MapPalette &palette, TDB *track, TDB *road);
    void buildDatabase(Geometry &geometry, const MapView &view, TDB *database, bool detail,
                       int minTileX, int maxTileX, int minTileZ, int maxTileZ, float lineHeight);

    bool valid = false;
    int builtTileX = 0;
    int builtTileZ = 0;
    float builtMetresPerPixel = 0.0f;
    bool builtDetail = false;
    int builtTiles[4] = {0, 0, 0, 0};
    QString builtPalette;
    std::unique_ptr<OglObj> trackLines;
    std::unique_ptr<OglObj> roadLines;
    std::unique_ptr<OglObj> junctions;
    std::unique_ptr<OglObj> ends;
};

#endif
