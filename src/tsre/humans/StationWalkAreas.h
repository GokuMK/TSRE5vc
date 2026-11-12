/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef HUMANS_STATIONWALKAREAS_H
#define HUMANS_STATIONWALKAREAS_H

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>
#include <tsre/humans/WalkArea.h>
#include <tsre/tdb/StationPlatforms.h>

class OglObj;
class RenderQueue;

namespace Humans {

// The walk areas of the route's stations (task humans 04): a station is the
// platforms of one station name (a platform without one is a station of its
// own). Its walk area is gathered on the main thread once the world tiles
// around it are loaded (its platforms' sides, the objects in its corridors
// with their roles, the tracks, the terrain), then built on a worker
// (WalkArea).
//
// Roles by object type: static objects and collide objects are floor;
// signals, speed posts, gantries, level crossings, pickups and hazards
// solid; terrain ground; the rest (track, forests, transfers, car spawners,
// ...) not in the walk area. A shape's .sd (ESD_TSREWalk) or glTF scene
// extras (TSRE_walk) change its role: Floor, Solid, Ground or Ignore.
//
// The frame of a station's walk area: metres from its origin (editor
// coordinates: x east, z south, y up), y the rail's top 0.325 m above the
// track database's height.
class StationWalkAreas {
public:
    static StationWalkAreas &instance();

    // The rail's top above the track database's height (the track shape's
    // origin): MSTS track and TSRE's dynamic track.
    static constexpr float RailTop = 0.325f;

    struct Station {
        QString name;
        // Global editor coordinates: its middle (x, z) and its frame's origin.
        double middle[2] = {0.0, 0.0};
        double origin[3] = {0.0, 0.0, 0.0};
        enum class State { Waiting, Building, Built, Failed } state = State::Waiting;
        std::shared_ptr<const WalkArea::Built> built;
        // What the build found wrong, for the editor's error list.
        QStringList problems;
        int objects = 0;
        int triangles = 0;
        double gatherMs = 0.0;
        double buildMs = 0.0;
        // Its walk platforms once built, in the build's order (each side of
        // a platform with people; WalkArea::Seed::platform): the platform
        // (an index of platformList()), its track items, its side as used
        // (WalkArea::Platform::side, after a swap) and its track's line in
        // the station's frame (x, the rail's top, z every 2 m).
        struct Side {
            int platform = -1;
            int start = -1;
            int end = -1;
            float side = 1.0f;
            std::vector<float> line;
        };
        std::vector<Side> sides;
    };

    // How far from the camera (m, to a station's middle) walk areas are
    // built and drawn. Built ones are kept when the camera goes further.
    static constexpr float Reach = 1500.0f;

    // Once a frame while walk areas are wanted: reads the route's stations
    // (again after a route change), takes finished builds, and starts the
    // next station within `reach` metres of the camera (editor tile and
    // place) whose tiles are loaded. One build at a time.
    void frame(const float *playerT, const float *playerW, float reach = Reach);
    // The built walk areas within `reach` metres of the camera, as the
    // editor's overlay.
    void pushOverlay(RenderQueue &queue, const float *playerT, const float *playerW, float reach = Reach);
    // Every station built synchronously on this thread (tests); its tiles
    // must be loaded. False when there is no such station.
    bool buildNow(const QString &station);
    const std::vector<Station> &stations() const { return list; }
    const std::vector<StationPlatforms::Platform> &platformList() const { return platforms; }
    // Everything forgotten (a route closed).
    void clear();
    // Whether a build is running.
    bool busy() const;

    // A station's gathered input and its build (the .cpp's).
    struct Job;

private:
    StationWalkAreas() = default;
    struct Overlay;
    struct Entry {
        std::vector<int> platforms;
        std::shared_ptr<Job> job;
        std::shared_ptr<Overlay> overlay;
    };
    std::vector<Station> list;
    std::vector<Entry> entries;
    std::vector<StationPlatforms::Platform> platforms;
    const void *routeRead = nullptr;
    int itemsRead = -1;
    void readStations();
    // The station's input on the main thread; false while its tiles wait.
    bool gather(int station, Job &job);
    void finish(int station);
};

}

#endif
