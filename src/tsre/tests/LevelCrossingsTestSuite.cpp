// Level crossings and the cars that wait at them (task traffic 01): the
// simulation core's crossings on hand-made track, and one lane's cars.
#include <tsre/tests/SimulationTestSuite.h>

#include <QDebug>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
#include <functional>
#include <tsre/Game.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrackNetwork.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/traffic/LaneTraffic.h>
#include <tsre/trains/PathTrack.h>

int TsreTests::runLevelCrossingsSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:level-crossings] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:level-crossings] FAIL" << name; }
    };

    // A straight line of 2 km: track end 1, track 10, track end 2, with
    // level crossings at 1000 m (item 501) and 1600 m (item 502).
    TestTrack::Layout line;
    line.end(1, 10);
    line.track(10, 1, 2, 20, true);
    line.end(2, 10);
    TestTrack::addItem(line, 10, TRitem::newLevelCrItem(501, 1000.0f));
    TestTrack::addItem(line, 10, TRitem::newLevelCrItem(502, 1600.0f));
    TDB *db = &line.db;
    const QScopedValueRollback<TDB *> live(Game::trackDB, db);
    const auto geometry = std::make_shared<const Sim::TrackGeometry>(db);
    const auto network = std::make_shared<const Sim::TrackNetwork>(db, *geometry, nullptr, false);
    const int t0 = 8 * 3600;
    // From one place on track 10 to another, either way.
    const auto routeOf = [db](float from, float to) {
        PathTrack::Result made = PathTrack::startAt(db, {10, from});
        if (to < from)
            made = PathTrack::turnStart(db, made.nodes);
        made = PathTrack::endAt(db, made.nodes, {10, to});
        return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 31.0);
    };
    // Two 20 m cars.
    const auto trainOn = [&](const Sim::TrainRoute &route, float maxSpeed) {
        Sim::TrainPlan p;
        p.player = false;
        p.service = "Goods";
        p.serviceTime = t0;
        p.vehicles = {Sim::Vehicle{20.0f, false, 4}, Sim::Vehicle{20.0f, false, 1}};
        p.length = 40.0f;
        p.maxSpeed = maxSpeed;
        Sim::SetRates(p);
        p.startTime = t0;
        p.route = route;
        p.startFront = route.pathStart + 40.0;
        for (const Sim::RouteTrack &part : route.parts) {
            p.projection.push_back(network->project(part));
            p.limits.push_back({});
        }
        return p;
    };
    const auto inputOf = [&](std::vector<Sim::TrainPlan> trains, std::vector<Sim::StaticConsist> consists = {}) {
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->geometry = geometry;
        made->network = network;
        for (size_t i = 0; i < trains.size(); ++i) {
            trains[i].id = int(i);
            made->trains.push_back(trains[i]);
        }
        made->staticConsists = std::move(consists);
        return std::shared_ptr<const Sim::SimulationInput>(made);
    };

    // The network's crossings, and the route's way past them.
    const Sim::TrainRoute east = routeOf(100.0f, 1900.0f);
    if (east.parts.empty()) {
        check(false, "a route along the line");
        return 1;
    }
    {
        const auto &crossings = network->levelCrossings();
        check(crossings.size() == 2 && network->levelCrossingOfItem(501) == 0 && network->levelCrossingOfItem(502) == 1
                  && network->levelCrossingOfItem(503) == -1 && crossings[0].node == 10
                  && std::fabs(crossings[0].distance - 1000.0f) < 1e-3f,
              "the network has the track's level crossing items");
        const Sim::Projection way = network->project(east.parts[0]);
        double at = -1.0;
        east.parts[0].find(10, 1000.0f, at);
        check(way.levelCrossings.size() == 2 && way.levelCrossings[0].index == 0
                  && std::fabs(way.levelCrossings[0].at - at) < 1e-3 && way.levelCrossings[1].index == 1,
              "a route's way lists the crossings it passes, in order");
        const Sim::TrainRoute west = routeOf(1900.0f, 100.0f);
        const Sim::Projection back = west.parts.empty() ? Sim::Projection() : network->project(west.parts[0]);
        check(back.levelCrossings.size() == 2 && back.levelCrossings[0].index == 1,
              "and either way round");
    }

    // A run step by step: each step's train front, speed and crossing
    // states; `each` after every step.
    struct Sample {
        double time = 0.0;
        double front = 0.0;
        double speed = 0.0;
        std::vector<unsigned char> closed;
    };
    const auto runFor = [](Sim::SimulationCore &core, double seconds) {
        std::vector<Sample> out;
        const long long steps = std::llround(seconds / Sim::SimulationCore::MaxStep);
        for (long long k = 0; k < steps; ++k) {
            core.step(Sim::SimulationCore::MaxStep);
            Sample s;
            s.time = core.time();
            s.front = core.trains()[0].front;
            s.speed = core.trains()[0].speed;
            s.closed = core.levelCrossingStates();
            out.push_back(s);
        }
        return out;
    };
    double atA = -1.0, atB = -1.0;
    east.parts[0].find(10, 1000.0f, atA);
    east.parts[0].find(10, 1600.0f, atB);

    // A train at line speed: the crossing closes where Open Rails' reach
    // (warning time × speed, half the way to its allowed speed, the minimum
    // distance and a metre) meets its front; closed while the train is on
    // it; open once its rear is the minimum distance past.
    {
        Sim::SimulationCore core(inputOf({trainOn(east, 20.0f)}));
        const std::vector<Sample> run = runFor(core, 200.0);
        size_t closing = run.size(), opening = run.size();
        for (size_t k = 1; k < run.size(); ++k) {
            if (closing == run.size() && run[k].closed[0] && !run[k - 1].closed[0])
                closing = k;
            if (closing < k && opening == run.size() && !run[k].closed[0] && run[k - 1].closed[0])
                opening = k;
        }
        check(!run.empty() && !run[0].closed[0], "open while the train is far");
        bool reachMet = false;
        if (closing < run.size()) {
            const Sample &s = run[closing];
            const double v = s.speed;
            const double reach = 30.0 * v + std::max(0.0, 30.0 * (20.0 - v) / 2.0) + 20.0 + 1.0;
            // Looked at every CrossingEvery seconds: within that time's
            // travel after the reach met it.
            const double late = Sim::SimulationCore::CrossingEvery * v + 1e-6;
            reachMet = atA - s.front <= reach + 1e-6 && atA - s.front > reach - late;
            if (verbose)
                qInfo().noquote() << QString("[tests:level-crossings] closed %1 m before at %2 m/s (reach %3 m), %4 s before")
                        .arg(atA - s.front, 0, 'f', 1).arg(v, 0, 'f', 1).arg(reach, 0, 'f', 1)
                        .arg((atA - s.front) / std::max(v, 0.1), 0, 'f', 1);
        }
        check(reachMet, "it closes where the train's reach meets it");
        bool heldOnIt = true;
        for (const Sample &s : run)
            if (s.front >= atA && s.front - 40.0 <= atA + 20.0 && !s.closed[0])
                heldOnIt = false;
        check(heldOnIt, "closed while the train is on it and until its rear is the minimum distance past");
        check(opening < run.size() && run[opening].front - 40.0 - atA > 20.0
                  && run[opening].front - 40.0 - atA <= 20.0 + Sim::SimulationCore::CrossingEvery * run[opening].speed + 1e-6,
              "it opens once the rear is the minimum distance past");
        check(run.back().closed[1] == 0 && run.back().front - 40.0 > atB + 20.0,
              "the second crossing opens behind the train too");
    }

    // A 30 km/h limit from 600 m: the train's allowed speed drops while it
    // runs towards the crossing, and its reach with it. The crossing it
    // closed stays closed until it has passed (Open Rails lets it go and
    // closes it again further on).
    {
        Sim::TrainPlan slowing = trainOn(east, 20.0f);
        double from = -1.0, to = -1.0;
        east.parts[0].find(10, 600.0f, from);
        east.parts[0].find(10, 1100.0f, to);
        slowing.limits[0] = {Sim::SpeedLimit{from, to, 30.0f / 3.6f}};
        Sim::SimulationCore core(inputOf({slowing}));
        const std::vector<Sample> run = runFor(core, 300.0);
        int closings = 0;
        for (size_t k = 1; k < run.size(); ++k)
            closings += run[k].closed[0] && !run[k - 1].closed[0] ? 1 : 0;
        check(closings == 1, QString("slowing for a limit on the way, it closes once (%1 times)").arg(closings));
    }

    // A crossing's values from its object: a shorter warning time closes it
    // later.
    {
        Sim::SimulationCore usual(inputOf({trainOn(east, 20.0f)}));
        Sim::SimulationCore quick(inputOf({trainOn(east, 20.0f)}));
        Sim::SimulationCore::CrossingParameters p;
        p.item = 501;
        p.warning = 10.0f;
        p.minimum = 5.0f;
        quick.setCrossingParameters({p, Sim::SimulationCore::CrossingParameters{999, 1.0f, 1.0f}});
        const std::vector<Sample> a = runFor(usual, 120.0);
        const std::vector<Sample> b = runFor(quick, 120.0);
        double closedA = -1.0, closedB = -1.0;
        for (size_t k = 0; k < a.size(); ++k) {
            if (closedA < 0.0 && a[k].closed[0])
                closedA = a[k].time;
            if (closedB < 0.0 && b[k].closed[0])
                closedB = b[k].time;
        }
        check(closedA > 0.0 && closedB > closedA + 5.0, "a shorter warning time closes it later");
        const unsigned long long serial = quick.levelCrossingSerial();
        quick.setCrossingParameters({p});
        check(quick.levelCrossingSerial() == serial, "the same values again change nothing");
    }

    // Standing (the player's train stands at its path's end for good; an AI
    // train is gone there): 50 m short of the crossing it lets it open (Open
    // Rails' timeout); 10 m short it keeps it closed. A crossing beyond the
    // track reserved for a train does not close for it.
    {
        Sim::TrainPlan farPlan = trainOn(routeOf(100.0f, 950.0f), 20.0f);
        farPlan.player = true;
        Sim::SimulationCore farCore(inputOf({farPlan}));
        const std::vector<Sample> far = runFor(farCore, 200.0);
        check(far.back().speed == 0.0 && !far.back().closed[0],
              QString("a train standing 50 m short leaves it open (front %1 m short)").arg(atA - far.back().front, 0, 'f', 1));
        Sim::TrainPlan nearPlan = trainOn(routeOf(100.0f, 990.0f), 20.0f);
        nearPlan.player = true;
        Sim::SimulationCore nearCore(inputOf({nearPlan}));
        const std::vector<Sample> near = runFor(nearCore, 200.0);
        check(near.back().speed == 0.0 && near.back().closed[0],
              QString("a train standing 10 m short keeps it closed (front %1 m short)").arg(atA - near.back().front, 0, 'f', 1));
        Sim::SimulationCore aiCore(inputOf({trainOn(routeOf(100.0f, 950.0f), 20.0f)}));
        const std::vector<Sample> ai = runFor(aiCore, 200.0);
        bool everClosed = false;
        for (const Sample &s : ai)
            everClosed = everClosed || s.closed[0];
        check(!everClosed, "a train whose track ends before it never closes it");
    }

    // Slowly (5 m/s or less): it closes only within the minimum distance and
    // 65 m.
    {
        Sim::SimulationCore core(inputOf({trainOn(east, 4.0f)}));
        const std::vector<Sample> run = runFor(core, 400.0);
        double closedAt = -1.0;
        for (const Sample &s : run)
            if (closedAt < 0.0 && s.closed[0])
                closedAt = atA - s.front;
        check(closedAt > 80.0 && closedAt <= 85.0 + 1e-6,
              QString("a slow train closes it 85 m before (%1 m)").arg(closedAt, 0, 'f', 1));
    }

    // A standing consist: within Open Rails' 6.5 m of a crossing (minimum
    // 20 m less 13.5) it closes it; further away it does not.
    {
        Sim::StaticConsist nearby;
        nearby.name = "Near";
        nearby.vehicles = {Sim::Vehicle{20.0f, false, 7}};
        nearby.length = 20.0;
        nearby.track.push_back(Sim::RouteInterval{10, 1005.0f, 1025.0f, 0.0});
        Sim::StaticConsist away = nearby;
        away.name = "Away";
        away.track[0] = Sim::RouteInterval{10, 1610.0f, 1630.0f, 0.0};
        Sim::SimulationCore core(inputOf({}, {nearby, away}));
        core.step(Sim::SimulationCore::MaxStep);
        check(core.levelCrossingStates().size() == 2 && core.levelCrossingStates()[0] == 1 && core.levelCrossingStates()[1] == 0,
              "a consist standing 5 m from a crossing closes it; one 10 m away does not");
    }

    // The frame shares the states while unchanged.
    {
        Sim::SimulationCore core(inputOf({trainOn(east, 20.0f)}));
        core.step(Sim::SimulationCore::MaxStep);
        const std::shared_ptr<Sim::SimulationFrame> first = Sim::MakeFrame(core);
        core.step(Sim::SimulationCore::MaxStep);
        const std::shared_ptr<Sim::SimulationFrame> second = Sim::MakeFrame(core, first.get());
        bool changedShared = false;
        std::shared_ptr<Sim::SimulationFrame> last = second;
        for (int k = 0; k < 1200 && !changedShared; ++k) {
            core.step(Sim::SimulationCore::MaxStep);
            std::shared_ptr<Sim::SimulationFrame> next = Sim::MakeFrame(core, last.get());
            if (next->levelCrossingSerial != last->levelCrossingSerial)
                changedShared = next->levelCrossings != last->levelCrossings && (*next->levelCrossings)[0] == 1;
            last = next;
        }
        check(second->levelCrossings == first->levelCrossings && first->levelCrossings->size() == 2,
              "a frame shares the states with the one before while unchanged");
        check(changedShared, "and has its own once one changes");
    }

    qInfo().noquote() << QString("level-crossings: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}

int TsreTests::runRoadCarsSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:road-cars] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:road-cars] FAIL" << name; }
    };
    using Car = LaneTraffic::Car;
    // Cars never overlap; none moves back.
    const auto apart = [](const LaneTraffic &lane) {
        for (size_t i = 1; i < lane.cars.size(); ++i)
            if (lane.cars[i].front() > lane.cars[i - 1].rear() + 1e-9)
                return false;
        return true;
    };

    // A 300 m lane with a crossing at 120 m, closed: cars arrive every 2 s
    // and queue before it; once the queue reaches the start no more enter.
    {
        LaneTraffic lane;
        lane.length = 300.0;
        lane.stops.push_back({120.0, true});
        int entered = 0, refused = 0, serial = 0;
        bool alwaysApart = true, forward = true;
        for (int k = 0; k < 900; ++k) {
            if (k % 20 == 0) {
                Car car;
                car.id = ++serial;
                car.length = 4.5;
                car.desired = 12.0 + (k % 3);
                if (lane.roomFor(car.length)) {
                    lane.enter(car);
                    ++entered;
                } else {
                    ++refused;
                }
            }
            std::vector<double> before;
            for (const Car &c : lane.cars)
                before.push_back(c.position);
            lane.step(0.1);
            alwaysApart = alwaysApart && apart(lane);
            for (size_t i = 0; i < before.size() && i < lane.cars.size(); ++i)
                forward = forward && lane.cars[i].position >= before[i] - 1e-9 && lane.cars[i].speed >= 0.0;
        }
        const double line = 120.0 - LaneTraffic::StopBefore;
        check(!lane.cars.empty() && lane.cars[0].front() <= line + 1e-6 && lane.cars[0].front() > line - 0.6
                  && lane.cars[0].speed == 0.0,
              QString("the first car stands at the line 10 m before the crossing (%1 m)").arg(lane.cars.empty() ? -1.0 : lane.cars[0].front(), 0, 'f', 2));
        check(alwaysApart, "no car ever runs into the car in front");
        check(forward, "no car moves back or has a negative speed");
        int standing = 0;
        for (const Car &c : lane.cars)
            standing += c.speed == 0.0 ? 1 : 0;
        check(standing >= 10, QString("a queue forms (%1 cars standing)").arg(standing));
        check(refused > 0 && !lane.roomFor(4.5), QString("a full lane takes no more cars (%1 entered, %2 refused)").arg(entered).arg(refused));
        bool gapsOk = true;
        for (size_t i = 1; i < lane.cars.size(); ++i)
            if (lane.cars[i].speed == 0.0 && lane.cars[i - 1].speed == 0.0) {
                const double gap = lane.cars[i - 1].rear() - lane.cars[i].front();
                gapsOk = gapsOk && gap > 1.0 && gap < 3.5;
            }
        check(gapsOk, "standing cars keep about the minimum gap");

        // Open: every car drives on and leaves.
        lane.stops[0].closed = false;
        int left = 0;
        for (int k = 0; k < 1200 && !lane.cars.empty(); ++k) {
            left += lane.step(0.1);
            alwaysApart = alwaysApart && apart(lane);
        }
        check(lane.cars.empty() && left == entered, QString("once open, every car leaves (%1 of %2)").arg(left).arg(entered));
        check(alwaysApart, "and none runs into another on the way");
    }

    // A car within a second of the crossing when it closes goes through.
    {
        LaneTraffic lane;
        lane.length = 200.0;
        lane.stops.push_back({100.0, false});
        Car car;
        car.id = 1;
        car.length = 4.5;
        car.desired = 15.0;
        lane.enter(car);
        while (!lane.cars.empty() && lane.cars[0].front() < 100.0 - LaneTraffic::StopBefore - 5.0)
            lane.step(0.1);
        lane.stops[0].closed = true;
        lane.step(2.0);
        check(!lane.cars.empty() && lane.cars[0].front() > 100.0,
              QString("a car 5 m before the line at 15 m/s goes through (%1 m)").arg(lane.cars.empty() ? -1.0 : lane.cars[0].front(), 0, 'f', 1));
    }

    // A car far before it stops in time, without braking harder than the
    // model allows.
    {
        LaneTraffic lane;
        lane.length = 400.0;
        lane.stops.push_back({300.0, true});
        Car car;
        car.id = 1;
        car.length = 4.5;
        car.desired = 20.0;
        lane.enter(car);
        // Its braking above 1 m/s (below it, it stands on the line within a
        // step). Its brake lights: off at its own speed, on while it slows
        // hard.
        double hardest = 0.0, last = lane.cars[0].speed;
        bool cruisingLit = false, hardDark = false;
        for (int k = 0; k < 600; ++k) {
            lane.step(0.1);
            const double slowing = (last - lane.cars[0].speed) / 0.1;
            if (last > 1.0)
                hardest = std::max(hardest, slowing);
            cruisingLit = cruisingLit || (k < 20 && lane.cars[0].braking);
            hardDark = hardDark || (slowing > LaneTraffic::BrakeLightsOn && !lane.cars[0].braking);
            last = lane.cars[0].speed;
        }
        check(lane.cars[0].speed == 0.0 && std::fabs(lane.cars[0].front() - 290.0) < 0.6,
              QString("a car stops at the line (%1 m)").arg(lane.cars[0].front(), 0, 'f', 2));
        check(hardest <= LaneTraffic::ComfortBraking + 0.1, QString("braking evenly, at most %1 m/s² (%2)").arg(LaneTraffic::ComfortBraking).arg(hardest, 0, 'f', 2));
        const bool standingLit = lane.cars[0].braking;
        lane.stops[0].closed = false;
        for (int k = 0; k < 30; ++k)
            lane.step(0.1);
        check(!cruisingLit && !hardDark && standingLit && !lane.cars[0].braking && lane.cars[0].speed > 1.0,
              QString("brake lights: off cruising, on braking and standing, off driving on (%1 m/s)")
              .arg(lane.cars[0].speed, 0, 'f', 1));
    }

    // A car entering behind a slow one is no faster than it can keep
    // behind it; a long step is cut in pieces.
    {
        LaneTraffic lane;
        lane.length = 1000.0;
        Car slow;
        slow.id = 1;
        slow.desired = 2.0;
        lane.enter(slow);
        lane.step(5.0);
        Car fast;
        fast.id = 2;
        fast.desired = 25.0;
        lane.enter(fast);
        check(lane.cars[1].speed < 10.0, QString("a car entering close behind a slow one comes in slow (%1 m/s)").arg(lane.cars[1].speed, 0, 'f', 1));
        lane.step(3.0);
        check(apart(lane), "a long step keeps them apart");
    }

    // A closed crossing near the lane's start: no car enters past its line
    // (it would drive through); one enters short of the line and stands
    // there.
    {
        LaneTraffic lane;
        lane.length = 200.0;
        lane.stops.push_back({LaneTraffic::StopBefore + 2.0, true});
        const bool refused = !lane.roomFor(4.5);
        lane.stops[0].closed = false;
        check(refused && lane.roomFor(4.5), "no car enters past the line of a closed crossing; one does once it opens");
        lane.stops[0] = {LaneTraffic::StopBefore + 6.0, true};
        Car car;
        car.id = 1;
        car.length = 4.5;
        car.desired = 15.0;
        const bool room = lane.roomFor(car.length);
        if (room)
            lane.enter(car);
        lane.step(5.0);
        check(room && !lane.cars.empty() && lane.cars[0].speed == 0.0 && lane.cars[0].front() <= 6.0 + 1e-6,
              QString("a car entering short of the line stands at it (%1 m)").arg(lane.cars.empty() ? -1.0 : lane.cars[0].front(), 0, 'f', 2));
    }

    qInfo().noquote() << QString("road-cars: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
