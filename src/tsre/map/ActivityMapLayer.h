/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ACTIVITYMAPLAYER_H
#define ACTIVITYMAPLAYER_H

#include <QColor>
#include <QString>
#include <memory>
#include <vector>
#include "MapFeatures.h"

class MapSelection;
class MapView;
class OglObj;
class RenderQueue;
class Route;
struct MapPalette;

// The activity and the paths selected in the activity tools, in map mode
// (the Map menu's Activity and Paths):
// - paths as a wide band under the track, with their nodes as circles;
// - consists (loose ones and the player's) as the footprints of their
//   vehicles, engines darker than wagons;
// - speed zones as a line along the track with circles at the ends;
// - failed signals as circles;
// - location events as squares, with a ring at their trigger radius.
// Each part answers for itself what the map draws (getMapFeatures).
class ActivityMapLayer {
public:
    enum Group { Path, Vehicles, Engines, SpeedZone, FailedSignal, Event, GroupCount };
    static constexpr float PathPixels = 8.0f;
    static constexpr float LinePixels = 3.0f;
    static constexpr float MarkerPixels = 9.0f;
    static constexpr float BorderPixels = 1.5f;
    // The smallest a vehicle is drawn, so trains stay visible zoomed out.
    static constexpr float MinVehiclePixels = 3.0f;
    // Under the track lines (TrackMapLayer: road 100, track 200).
    static constexpr float PathHeight = 150.0f;
    // Above the track objects (600, 610) and under the pointer (900).
    // Selected objects get a halo of this many pixels around the border.
    static constexpr float HaloPixels = 3.0f;
    static constexpr float HaloHeight = 695.0f;
    static constexpr float BorderHeight = 700.0f;
    static constexpr float AreaHeight = 705.0f;
    static constexpr float FillHeight = 710.0f;

    ActivityMapLayer();
    ~ActivityMapLayer();
    void pushRenderItems(RenderQueue &queue, const MapView &view, const MapPalette &palette,
                         Route *route, bool activity, bool paths);
    void invalidate() { valid = false; }
    // The activity's selectable parts with the 3D view's selection IDs:
    // each vehicle of a consist, speed zone ends, failed signals.
    void pushSelection(MapSelection &selection, const MapView &view, Route *route,
                       bool activity) const;

    static QColor colour(const MapPalette &palette, int group);
    // A vehicle's footprint as two triangles, grown by a margin all round
    // and at least minimum long and wide. Public for tests.
    static void appendVehicle(std::vector<float> &out, const float *vehicle, float y, float margin,
                              float minimum);

private:
    void build(const MapView &view, const MapPalette &palette, Route *route, bool activity,
               bool paths);
    std::vector<const void *> sources(Route *route, bool activity, bool paths) const;

    bool valid = false;
    int builtTileX = 0;
    int builtTileZ = 0;
    float builtMetresPerPixel = 0.0f;
    QString builtPalette;
    std::vector<const void *> builtSources;
    std::unique_ptr<OglObj> pathBand;
    std::unique_ptr<OglObj> halos;
    std::unique_ptr<OglObj> borders;
    std::unique_ptr<OglObj> areas;
    std::unique_ptr<OglObj> fills[GroupCount];
};

#endif
