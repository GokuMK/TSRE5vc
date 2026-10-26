/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MAPLAYERS_H
#define MAPLAYERS_H

// What the map mode draws (the Map menu). Separate from the 3D view's
// toggles, which live in Game.
enum class MapLayer {
    Track, Road, Junctions, Ends, TrackObjects, Paths, Activity, Terrain, FadedOverlay, Pointer,
    OsmData, OsmTransparentAreas, Markers, Imagery, ScaleBar, Weather, Count
};

// What of the activity the Activity layer draws (the Map menu's Activity
// Objects): the player's path, the AI trains' paths, the trains (at their
// starts, or where a simulation has them), the loose consists, speed zones,
// failed signals, events, and a simulation's track use and signal aspects.
enum class MapActivityKind {
    PlayerPath, AIPaths, Trains, LooseConsists, SpeedZones, FailedSignals, Events, SimulatedTrack, TrainNames, Count
};

struct MapLayers {
    // Faded Overlay is off: painting needs the true colours. OSM data is off: it needs
    // an OSM directory and may ask to convert files. Markers are off, as in 3D: a marker
    // set is shown for a purpose. Imagery is off: it downloads from a web service.
    // The scale bar is on.
    bool visible[int(MapLayer::Count)] = {true, true, true, true, true, true, true, true, false,
                                          true, false, false, false, false, true, false};
    bool shows(MapLayer layer) const { return visible[int(layer)]; }
    void set(MapLayer layer, bool show) { visible[int(layer)] = show; }
    // Within Track Objects and Activity, the kinds drawn: bits by
    // TrackItemMapLayer::Kind and MapActivityKind, all at first.
    unsigned trackObjectKinds = ~0u;
    unsigned activityKinds = ~0u;
    static bool has(unsigned kinds, int kind) { return (kinds >> kind) & 1u; }
    static void set(unsigned &kinds, int kind, bool show) {
        kinds = show ? kinds | (1u << kind) : kinds & ~(1u << kind);
    }
};

#endif
