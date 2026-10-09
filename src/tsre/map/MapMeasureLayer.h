/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPMEASURELAYER_H
#define MAPMEASURELAYER_H

#include "MapLabelLayer.h"
#include "MapView.h"
#include <QString>
#include <cstdint>
#include <memory>

class GeoWorldCoordinateConverter;
class OglObj;
class RenderQueue;
struct MapPalette;

// The distance measured with the map's Measure Distance tool (MapMeasureTool; not
// the 3D Ruler object): a line between two ground points and its length, labelled
// at its end. The map has no heights, so lengths are across the ground. The game
// length is in route coordinates; the geo length between the points' latitudes and
// longitudes on the WGS84 ellipsoid. They differ where the route's projection
// scales distances (the legacy MSTS projection).
class MapMeasureLayer {
public:
    // Under the labels (805 to 820), over the route's data.
    static constexpr float HaloHeight = 789.0f;
    static constexpr float LineHeight = 790.0f;
    static constexpr float LinePixels = 2.0f;
    static constexpr float HaloPixels = 2.0f;
    // Above every other label.
    static constexpr double LabelPriority = 5e12;

    MapMeasureLayer();
    ~MapMeasureLayer();
    void set(const MapGroundPoint &from, const MapGroundPoint &to);
    void clear();
    // There is a line to show (its ends differ).
    bool shown() const;
    // Changes with every set and clear.
    uint64_t version() const { return changes; }
    // Metres in route coordinates; geo metres, or a negative value without a
    // geographic reference.
    double gameLength() const;
    double geoLength(GeoWorldCoordinateConverter *converter) const;
    // "123 m", or both lengths when they differ by a metre or more.
    static QString text(double gameMetres, double geoMetres);
    // The label at the line's end.
    MapLabel label(GeoWorldCoordinateConverter *converter) const;
    void pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette, float pixelRatio);

    // The distance between two latitudes and longitudes (degrees) on the WGS84
    // ellipsoid (Vincenty's inverse formula), in metres.
    static double geodesicMetres(double lat1, double lon1, double lat2, double lon2);

private:
    MapGroundPoint from;
    MapGroundPoint to;
    bool active = false;
    uint64_t changes = 0;
    std::unique_ptr<OglObj> line;
    std::unique_ptr<OglObj> halo;
};

#endif
