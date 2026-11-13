/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef HUMANS_STATIONPEOPLE_H
#define HUMANS_STATIONPEOPLE_H

#include <QString>
#include <memory>

class Activity;
class RenderQueue;

namespace Humans {

// People at the stations (tasks humans 05 and 06), a view-side system like
// the displays and the doors: it reads the simulation's frames and its
// trains' cars as drawn, and never writes back.
//
// Task 05, people waiting: each train stopping at a platform has its own
// people waiting for it, as many as the platform's passengers waiting (the
// activity's figure, else the track database's; the user: per train, so two
// trains due together bring twice the people). They come in the ten minutes
// before its booked arrival (from when it heads for the stop without a
// booked time), one by one where the camera does not see them, at random
// places on the platform's walk area behind the edge zone, along the whole
// platform; they stand, and now and then walk a few steps. Random numbers
// come from the train, the stop and the platform, so runs repeat.
//
// Task 06, boarding and getting off: as its train comes within 500 m,
// most of its people step up to just behind the edge zone. Once it stands
// at the stop, its cars' doors on the platform's side (door parts, else a
// door near each end of a carriage) give where people go: while a door is
// open, people get off it first (walking out of the car from its middle
// over the track, through the door, onto the platform and away along it,
// gone where nobody sees them); then the waiting people walk to the nearest
// open doors and on into the car up to its middle over the track (the user:
// not vanishing in the doorway), gone there. The train's floor is taken at
// the door's threshold (the user: enough for now). Doors closing or the
// train leaving: those still on the platform walk away.
//
// Who: the route's humans (HumanSet, core.content.people). How they move:
// Detour's crowd on each station's walk area (StationWalkAreas, built for
// stations within its reach of the camera), drawn by a Crowd.
class StationPeople {
public:
    static StationPeople &instance();

    // Once a frame of the main view (RENDER_DEFAULT): the people moved on to
    // the simulation's time and pushed. `activity`: the route's activity the
    // simulation shows (none, nobody).
    void frame(RenderQueue &queue, const float *playerT, const float *playerW, Activity *activity);
    // Everybody gone (a route closed, the simulation stopped).
    void clear();

    struct Stats {
        int stations = 0;
        int groups = 0;
        int people = 0;
        int waiting = 0;
        int walking = 0;
        int boarding = 0;
        int alighting = 0;
        // Since the run started.
        int boarded = 0;
        int alighted = 0;
        // In cars with seats (task humans 09): all, and those sitting.
        int riders = 0;
        int sitting = 0;
        double updateMs = 0.0;
    };
    const Stats &stats() const;

    struct Impl;

private:
    StationPeople();
    ~StationPeople();
    std::unique_ptr<Impl> d;
};

}

#endif
