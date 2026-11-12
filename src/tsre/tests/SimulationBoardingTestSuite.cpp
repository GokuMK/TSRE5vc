/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// What boarding in the simulation (task humans 06) changes on a route (the
// user, 2026-11-12: "see if this new sim addition matters at all"): every
// activity and Open Rails timetable set of the route (--game-root, --route)
// run to its end twice, without the stops' people and with them
// (Sim::FillPassengers, as a run from the editor or Play has them), and the
// two compared: stops left later, trains ending later, trains stuck, the
// people counted. Not a pass or fail on timings: a measure, told in the log.

#include "SimulationBoardingTestSuite.h"
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
#include <map>
#include <tsre/Game.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/EngLib.h>
#include <tsre/world/Route.h>
#include <tsre/world/Trk.h>

namespace {

struct RunResult {
    // By (train, stop): when it left; by train: when it finished (-1 not).
    std::map<std::pair<int, int>, double> departed;
    std::map<std::pair<int, int>, double> arrived;
    std::vector<double> finished;
    int stuck = 0;
    int deadlocks = 0;
    int boarded = 0;
    int missed = 0;
    int alighted = 0;
    int stopsCounted = 0;
    double simulated = 0.0;
    qint64 ms = 0;
};

RunResult runToEnd(const std::shared_ptr<const Sim::SimulationInput> &input) {
    RunResult result;
    QElapsedTimer timer;
    timer.start();
    Sim::SimulationCore core(input);
    result.finished.assign(input->trains.size(), -1.0);
    for (int s = 0; s < 24 * 3600 && !core.settled(); ++s) {
        core.step(1.0);
        for (const Sim::TrainEvent &event : core.takeEvents()) {
            const std::pair<int, int> key{event.train, event.index};
            switch (event.kind) {
            case Sim::TrainEvent::Arrived:
                result.arrived[key] = event.time;
                break;
            case Sim::TrainEvent::Departed:
                result.departed[key] = event.time;
                break;
            case Sim::TrainEvent::Finished:
                if (event.train >= 0 && size_t(event.train) < result.finished.size())
                    result.finished[size_t(event.train)] = event.time;
                break;
            case Sim::TrainEvent::Deadlock:
                ++result.deadlocks;
                break;
            case Sim::TrainEvent::Passengers:
                ++result.stopsCounted;
                result.boarded += event.signal;
                result.missed += event.count;
                break;
            default:
                break;
            }
        }
    }
    for (size_t t = 0; t < core.trains().size(); ++t) {
        const Sim::TrainState &state = core.trains()[t];
        const bool done = state.status == Sim::TrainStatus::Finished || state.stoppedAt == Sim::StopKind::End;
        if (!done && (state.deadlocked || state.blockedBy != Sim::NoHolder))
            ++result.stuck;
        result.alighted += state.passengers.alightedTotal;
    }
    result.simulated = core.time() - input->startTime;
    result.ms = timer.elapsed();
    return result;
}

} // namespace

namespace TsreTests {

int runSimulationBoardingSuite(bool verbose) {
    const QDir sourceRoute(Game::root + "/ROUTES/" + Game::route);
    const auto databases = sourceRoute.entryInfoList({"*.tdb"}, QDir::Files);
    if (databases.size() != 1) {
        qWarning() << "simulation-boarding requires a route with one TDB (--game-root, --route)";
        return 1;
    }
    const QScopedValueRollback<QString> routeName(Game::routeName, databases.first().completeBaseName());
    TSectionDAT sections(false, false);
    if (QFileInfo::exists(sourceRoute.filePath("tsection.dat")))
        sections.loadRoute(false);
    TDB database(&sections, false);
    database.loadTdb();
    if (!database.loaded)
        return 1;
    const QScopedValueRollback<TDB *> trackDB(Game::trackDB, &database);
    EngLib engines;
    const QScopedValueRollback<EngLib *> engineLibrary(Game::currentEngLib,
                                                       Game::currentEngLib != nullptr ? Game::currentEngLib : &engines);
    Route *content = new Route();
    content->loadServices();
    content->loadTraffic();
    content->loadPaths();
    content->loadActivities();
    content->loadTimetableSets();
    const float speedLimit = content->trk != nullptr ? content->trk->speedLimit : 44.4444f;
    const float restricted = content->trk != nullptr ? content->trk->tempRestrictedSpeed : -1.0f;
    QVector<Activity *> runs;
    for (int id : content->activityId)
        runs << ActLib::Act.value(id);
    for (int id : content->timetableSetId)
        runs << ActLib::TimetableSets.value(id);

    int ran = 0, totalStops = 0, heldStops = 0, laterTrains = 0, totalTrains = 0, boarded = 0, missed = 0;
    double worstHold = 0.0, worstEnd = 0.0, holdSum = 0.0;
    for (Activity *activity : runs) {
        if (activity == nullptr || activity->loaded != 1)
            continue;
        auto plain = Sim::MakeInput(activity, &database, speedLimit, nullptr, nullptr, restricted, false);
        if (plain == nullptr || plain->trains.empty())
            continue;
        auto people = Sim::MakeInput(activity, &database, speedLimit, plain->geometry, plain->network, restricted, false);
        Sim::FillPassengers(*people, activity, &database);
        int waiting = 0, alighting = 0;
        for (const Sim::TrainPlan &plan : people->trains)
            for (const Sim::StopPlan &stop : plan.stops) {
                waiting += stop.passengers;
                alighting += stop.alighting;
            }
        const RunResult a = runToEnd(plain), b = runToEnd(people);
        ++ran;
        // Stops left later with the people, and by how much.
        int stops = 0, held = 0;
        double worst = 0.0, sum = 0.0;
        for (const auto &[key, time] : a.departed) {
            const auto other = b.departed.find(key);
            if (other == b.departed.end())
                continue;
            ++stops;
            const double later = other->second - time;
            if (later > 0.5) {
                ++held;
                sum += later;
                worst = std::max(worst, later);
            }
        }
        int later = 0, notFinished[2] = {0, 0};
        double worstLater = 0.0;
        for (size_t t = 0; t < a.finished.size() && t < b.finished.size(); ++t) {
            notFinished[0] += a.finished[t] < 0.0 ? 1 : 0;
            notFinished[1] += b.finished[t] < 0.0 ? 1 : 0;
            if (a.finished[t] >= 0.0 && b.finished[t] >= 0.0 && b.finished[t] - a.finished[t] > 0.5) {
                ++later;
                worstLater = std::max(worstLater, b.finished[t] - a.finished[t]);
            }
        }
        qInfo().noquote()
                << QString("[tests:simulation-boarding] %1: %2 trains; %3 waiting, %4 getting off in the input; "
                           "%5 of %6 stops left later (mean %7 s, at most %8 s); %9 trains ended later (at most "
                           "%10 s); not finished %11 -> %12, stuck %13 -> %14, deadlocks %15 -> %16; counted at %17 "
                           "stops: %18 on, %19 off, %20 left behind; %21 + %22 ms")
                       .arg(activity->name).arg(plain->trains.size()).arg(waiting).arg(alighting).arg(held).arg(stops)
                       .arg(held > 0 ? sum / held : 0.0, 0, 'f', 1).arg(worst, 0, 'f', 1).arg(later)
                       .arg(worstLater, 0, 'f', 0).arg(notFinished[0]).arg(notFinished[1]).arg(a.stuck).arg(b.stuck)
                       .arg(a.deadlocks).arg(b.deadlocks).arg(b.stopsCounted).arg(b.boarded).arg(b.alighted)
                       .arg(b.missed).arg(a.ms).arg(b.ms);
        if (verbose)
            for (const auto &[key, time] : a.departed) {
                const auto other = b.departed.find(key);
                if (other != b.departed.end() && other->second - time > 0.5)
                    qInfo().noquote() << QString("[tests:simulation-boarding]   %1 %2 stop %3 (%4): left %5 s later")
                                         .arg(people->trains[size_t(key.first)].service)
                                         .arg(people->trains[size_t(key.first)].serviceTime).arg(key.second)
                                         .arg(people->trains[size_t(key.first)].stops[size_t(key.second)].platform)
                                         .arg(other->second - time, 0, 'f', 1);
            }
        totalStops += stops;
        heldStops += held;
        holdSum += sum;
        worstHold = std::max(worstHold, worst);
        laterTrains += later;
        totalTrains += int(plain->trains.size());
        worstEnd = std::max(worstEnd, worstLater);
        boarded += b.boarded;
        missed += b.missed;
    }
    qInfo().noquote() << QString("[tests:simulation-boarding] %1 runs on %2: %3 of %4 stops left later (mean %5 s, at "
                                 "most %6 s), %7 of %8 trains ended later (at most %9 s); %10 got on, %11 left behind")
                         .arg(ran).arg(Game::route).arg(heldStops).arg(totalStops)
                         .arg(heldStops > 0 ? holdSum / heldStops : 0.0, 0, 'f', 1).arg(worstHold, 0, 'f', 1)
                         .arg(laterTrains).arg(totalTrains).arg(worstEnd, 0, 'f', 0).arg(boarded).arg(missed);
    qInfo().noquote() << "[tests:simulation-boarding] passed" << (ran > 0 ? 1 : 0) << "failed" << (ran > 0 ? 0 : 1);
    return ran > 0 ? 0 : 1;
}

} // namespace TsreTests
