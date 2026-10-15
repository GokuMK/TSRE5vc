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
// - the AI trains shown from the Timetable window (Activity::shownAITrains)
//   likewise, their bands in the AI colours under the player's;
// - consists (loose ones and the player's) as the footprints of their
//   vehicles, engines darker than wagons;
// - speed zones as a line along the track with circles at the ends;
// - failed signals as circles;
// - location events as squares, with a ring at their trigger radius;
// - while the activity is simulated: its trains where they are, the track
//   they stand on and have reserved, and the signals' aspects (arrowheads
//   pointing the way the trains they face go).
// Each part answers for itself what the map draws (getMapFeatures).
class ActivityMapLayer {
public:
    enum Group { Path, PathStart, PathEnd, PathBroken, Vehicles, Engines, SpeedZone, FailedSignal,
                 Event, GroupCount };
    static constexpr float PathPixels = 8.0f;
    static constexpr float LinePixels = 3.0f;
    static constexpr float MarkerPixels = 9.0f;
    static constexpr float BorderPixels = 1.5f;
    // The smallest a vehicle is drawn, so trains stay visible zoomed out.
    static constexpr float MinVehiclePixels = 3.0f;
    // Under the track lines (TrackMapLayer: road 100, track 200).
    static constexpr float PathHeight = 150.0f;
    static constexpr float AIPathHeight = 140.0f;
    static constexpr float ChosenPathHeight = 145.0f;
    // Above the track objects (600, 610) and under the pointer (900).
    // Selected objects get a halo of this many pixels around the border.
    static constexpr float HaloPixels = 3.0f;
    static constexpr float HaloHeight = 695.0f;
    static constexpr float BorderHeight = 700.0f;
    static constexpr float AreaHeight = 705.0f;
    static constexpr float FillHeight = 710.0f;
    // A simulation's occupied and reserved track over the track lines, its
    // signals' aspects over the vehicles.
    static constexpr float SimulatedTrackHeight = 300.0f;
    static constexpr float AspectBorderHeight = 730.0f;
    static constexpr float AspectHeight = 735.0f;

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
    // The vehicles' borders and fills (wagons and engines) at the map's
    // scale, and halos around them.
    static void appendVehicles(const MapFeatures &features, float metresPerPixel, std::vector<float> &border,
                               std::vector<float> &vehicleFill, std::vector<float> &engineFill);
    static void appendHalos(const MapFeatures &features, float metresPerPixel, std::vector<float> &out);

private:
    void build(const MapView &view, const MapPalette &palette, Route *route, bool activity,
               bool paths);
    std::vector<const void *> sources(Route *route, bool activity, bool paths) const;
    // The trains of a simulation of the activity (task simulation 01), its
    // track and signals (task simulation 02).
    void pushSimulated(RenderQueue &queue, const MapView &view, const MapPalette &palette, Route *route,
                       bool activity);
    void buildSimulatedTrack(const MapView &view, const MapPalette &palette);

    bool valid = false;
    int builtTileX = 0;
    int builtTileZ = 0;
    float builtMetresPerPixel = 0.0f;
    QString builtPalette;
    std::vector<const void *> builtSources;
    std::unique_ptr<OglObj> pathBand;
    std::unique_ptr<OglObj> aiBand;
    std::unique_ptr<OglObj> chosenBand;
    std::unique_ptr<OglObj> halos;
    std::unique_ptr<OglObj> borders;
    std::unique_ptr<OglObj> areas;
    std::unique_ptr<OglObj> fills[GroupCount];
    // The simulated trains, built for this frame and view.
    const void *simulatedFrame = nullptr;
    int simulatedChosen = -1;
    int simulatedTileX = 0;
    int simulatedTileZ = 0;
    float simulatedMetresPerPixel = 0.0f;
    QString simulatedPalette;
    std::unique_ptr<OglObj> simulatedHalos;
    std::unique_ptr<OglObj> simulatedBorders;
    std::unique_ptr<OglObj> simulatedFills[2];
    // Its track (occupied, reserved) and signals (borders; stop, caution,
    // clear), built when they or the view change.
    const void *trackUses = nullptr;
    const void *trackAspects = nullptr;
    int trackTileX = 0;
    int trackTileZ = 0;
    float trackMetresPerPixel = 0.0f;
    QString trackPalette;
    std::unique_ptr<OglObj> simulatedTrack[2];
    std::unique_ptr<OglObj> aspectBorders;
    std::unique_ptr<OglObj> aspectFills[3];
    // Where each signal (the network's signal groups) is, and the way the
    // trains it faces go there (a unit vector on the map), once per input.
    struct Place {
        bool known = false;
        int tileX = 0;
        int tileZ = 0;
        float x = 0.0f;
        float z = 0.0f;
        float dx = 0.0f;
        float dz = 1.0f;
    };
    unsigned int placesRun = 0;
    std::vector<Place> signalPlaces;
};

#endif
