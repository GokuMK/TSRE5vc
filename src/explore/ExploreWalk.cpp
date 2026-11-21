/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <explore/ExploreWalk.h>
#include <tsre/characters/RouteCollision.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/tdb/StationPlatforms.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>

#include <QDebug>
#include <QHash>
#include <algorithm>
#include <cmath>

namespace Explore {

namespace {

// The rail's top above the track database's height (the track's base).
constexpr float RailTop = 0.325f;

// The editor's tile of a global coordinate and the place in it.
int tileOf(double global, float &local) {
    const int tile = int(std::floor((global + 1024.0) / 2048.0));
    local = float(global - double(tile) * 2048.0);
    return tile;
}

// A platform's length along its track (its items' distance when they lie on
// two nodes).
double lengthOf(const StationPlatforms::Platform &p) {
    if (p.node > 0)
        return p.to - p.from;
    return std::hypot(p.ends[0] - p.ends[3], p.ends[2] - p.ends[5]);
}

}

int walkTrain(const Sim::SimulationInput &in, const QString &timetableTrain) {
    for (const Sim::TrainPlan &plan : in.trains) {
        if (!timetableTrain.isEmpty()) {
            if (plan.timetableTrain && plan.service == timetableTrain)
                return plan.id;
        } else if (plan.player) {
            return plan.id;
        }
    }
    return -1;
}

bool walkPlatform(const Sim::SimulationInput &in, TDB &tdb, const QString &station, int train, WalkStart &out) {
    std::vector<StationPlatforms::Platform> all = StationPlatforms::read(tdb);
    std::vector<int> mine;
    for (int i = 0; i < int(all.size()); ++i)
        if (all[size_t(i)].station.compare(station, Qt::CaseInsensitive) == 0)
            mine.push_back(i);
    if (mine.empty())
        return false;
    // A platform of the station by one of its items.
    const auto platformOf = [&](int item) {
        for (int i : mine)
            if (all[size_t(i)].start == item || all[size_t(i)].end == item)
                return i;
        return -1;
    };
    int chosen = -1;
    // The train's stop there.
    for (const Sim::TrainPlan &plan : in.trains) {
        if (plan.id != train)
            continue;
        for (const Sim::StopPlan &stop : plan.stops)
            if ((chosen = platformOf(stop.platformItem)) >= 0) {
                out.why = WalkStart::Why::TrainStop;
                break;
            }
    }
    // The one most stops use.
    if (chosen < 0) {
        QHash<int, int> uses;
        for (const Sim::TrainPlan &plan : in.trains)
            for (const Sim::StopPlan &stop : plan.stops) {
                const int p = platformOf(stop.platformItem);
                if (p >= 0)
                    ++uses[p];
            }
        int most = 0;
        for (int i : mine)
            if (uses.value(i) > most) {
                most = uses.value(i);
                chosen = i;
            }
        if (chosen >= 0)
            out.why = WalkStart::Why::Busiest;
    }
    // Its longest.
    if (chosen < 0) {
        for (int i : mine)
            if (chosen < 0 || lengthOf(all[size_t(i)]) > lengthOf(all[size_t(chosen)]))
                chosen = i;
        out.why = WalkStart::Why::Longest;
    }
    StationPlatforms::Platform &platform = all[size_t(chosen)];
    StationPlatforms::sample(tdb, platform, 2.0);
    const std::vector<double> &p = platform.points;
    const int count = int(p.size() / 3);
    if (count < 2)
        return false;
    // The middle (where Open Rails centres a stop) and the way there; the
    // points in MSTS's sense (z north), the editor's z south.
    const int m = count / 2;
    const int a = std::max(0, m - 1), b = std::min(count - 1, m + 1);
    const double x = p[size_t(m) * 3], y = p[size_t(m) * 3 + 1], z = -p[size_t(m) * 3 + 2];
    double dx = p[size_t(b) * 3] - p[size_t(a) * 3], dz = -(p[size_t(b) * 3 + 2] - p[size_t(a) * 3 + 2]);
    const double length = std::hypot(dx, dz);
    if (length < 1e-6) {
        dx = 0.0;
        dz = 1.0;
    } else {
        dx /= length;
        dz /= length;
    }
    out.station = platform.station;
    out.platformStart = platform.start;
    out.platformEnd = platform.end;
    out.platformName = platform.name;
    out.tile[0] = tileOf(x, out.at[0]);
    out.tile[1] = tileOf(z, out.at[2]);
    out.at[1] = float(y);
    out.along[0] = float(dx);
    out.along[1] = float(dz);
    return true;
}

double walkArrival(const std::shared_ptr<const Sim::SimulationInput> &in, TDB &tdb, int train,
                   const QString &station) {
    if (in == nullptr)
        return -1.0;
    const Sim::TrainPlan *plan = nullptr;
    for (const Sim::TrainPlan &p : in->trains)
        if (p.id == train)
            plan = &p;
    if (plan == nullptr)
        return -1.0;
    std::vector<bool> there(plan->stops.size(), false);
    bool any = false;
    for (size_t i = 0; i < plan->stops.size(); ++i) {
        const auto found = tdb.trackItems.find(plan->stops[i].platformItem);
        there[i] = found != tdb.trackItems.end() && found->second != nullptr
                && found->second->stationName.trimmed().compare(station, Qt::CaseInsensitive) == 0;
        any = any || there[i];
    }
    if (!any)
        return -1.0;
    // Starting at a platform there (its front on it): its start.
    for (size_t i = 0; i < plan->stops.size(); ++i) {
        const Sim::StopPlan &stop = plan->stops[i];
        if (there[i] && plan->startFront >= std::min(stop.platformStart, stop.platformEnd) - 5.0
                && plan->startFront <= std::max(stop.platformStart, stop.platformEnd) + 5.0)
            return double(plan->startTime);
    }
    Sim::SimulationCore core(in);
    const double end = in->startTime + 24.0 * 3600.0;
    while (true) {
        for (const Sim::TrainEvent &event : core.takeEvents())
            if (event.kind == Sim::TrainEvent::Arrived && event.train == train && event.index >= 0
                    && size_t(event.index) < there.size() && there[size_t(event.index)])
                return event.time;
        if (core.settled() || core.time() >= end)
            return -1.0;
        core.step(10.0);
    }
}

WalkPlace walkPlace(const WalkStart &start, Characters::RouteCollision &probe, bool waitForObjects, int *tile,
                    float *at, float &yaw) {
    probe.update(start.tile, start.at);
    if (waitForObjects && (probe.stats().unready > 0 || probe.stats().pending > 0))
        return WalkPlace::Waiting;
    const Characters::CollisionWorld &world = probe.world();
    // The probe's frame is its tile's (the start's: it was gathered there).
    const float rail = start.at[1] + RailTop;
    struct Side {
        float sign = 1.0f;
        int platformHits = 0;
        bool any = false;
        float best = 0.0f;
        float bestOut = 0.0f;
        float bestFloor = -1e9f;
    } sides[2];
    sides[1].sign = -1.0f;
    for (Side &side : sides) {
        // Its normal: the track's way turned a quarter round.
        const float nx = -start.along[1] * side.sign, nz = start.along[0] * side.sign;
        for (float out : {2.5f, 3.0f, 3.5f, 4.0f, 4.5f}) {
            const float x = start.at[0] + nx * out, z = start.at[2] + nz * out;
            Characters::CollisionWorld::Floor floor;
            if (!world.floorAt(x, z, rail + 2.5f, rail - 3.0f, floor) || !probe.allowed(x, z))
                continue;
            const float above = floor.height - rail;
            const bool platform = above >= 0.15f && above <= 1.8f;
            if (platform)
                ++side.platformHits;
            // The place: on the platform 3.5 m out (nearest it), else any floor.
            const float score = (platform ? 100.0f : 0.0f) - std::fabs(out - 3.5f);
            if (!side.any || score > side.best) {
                side.any = true;
                side.best = score;
                side.bestOut = out;
                side.bestFloor = floor.height;
            }
        }
    }
    const Side *on = nullptr;
    if (sides[0].platformHits > 0 || sides[1].platformHits > 0)
        on = sides[0].platformHits >= sides[1].platformHits ? &sides[0] : &sides[1];
    else if (sides[0].any || sides[1].any)
        // A halt with no platform's floor (a ground strip): the higher side.
        on = !sides[1].any || (sides[0].any && sides[0].bestFloor >= sides[1].bestFloor) ? &sides[0] : &sides[1];
    if (on == nullptr)
        return WalkPlace::None;
    const float nx = -start.along[1] * on->sign, nz = start.along[0] * on->sign;
    tile[0] = start.tile[0];
    tile[1] = start.tile[1];
    at[0] = start.at[0] + nx * on->bestOut;
    at[1] = on->bestFloor + 1.0f;
    at[2] = start.at[2] + nz * on->bestOut;
    // Facing the track: back along the normal.
    yaw = std::atan2(-nx, -nz);
    qInfo().noquote() << QString("Explore: walking from %1 (%2, %3): the side %4 (%5 of 5 on a platform's floor), "
                                 "%6 m out, the floor %7 m over the rail")
                                 .arg(start.platformName, start.station).arg(start.platformStart)
                                 .arg(on->sign > 0 ? "left" : "right").arg(on->platformHits)
                                 .arg(double(on->bestOut), 0, 'f', 1).arg(double(on->bestFloor - rail), 0, 'f', 2);
    return WalkPlace::Found;
}

}
