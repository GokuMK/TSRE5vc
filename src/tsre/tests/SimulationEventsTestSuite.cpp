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
#include <tsre/sim/TrainRoute.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/trains/WaitingPoint.h>

namespace {

using Sim::EventPlan;
using Sim::TrainEvent;

// A run's events, and its events' states and the activity's end after it.
struct Run {
    std::vector<TrainEvent> events;
    std::vector<Sim::EventState> states;
    Sim::ActivityEnd end = Sim::ActivityEnd::None;
    double endedAt = -1.0;
};

// Runs `seconds` step by step; `each` after every step.
Run runFor(std::shared_ptr<const Sim::SimulationInput> input, double seconds,
           const std::function<void(const Sim::SimulationCore &)> &each = nullptr) {
    Sim::SimulationCore core(std::move(input));
    Run out;
    const long long steps = std::llround(seconds / Sim::SimulationCore::MaxStep);
    for (long long k = 0; k < steps; ++k) {
        core.step(Sim::SimulationCore::MaxStep);
        for (const TrainEvent &event : core.takeEvents())
            out.events.push_back(event);
        if (each)
            each(core);
    }
    out.states = core.eventStates();
    out.end = core.activityEnd();
    out.endedAt = core.activityEndedAt();
    return out;
}

// When the event fired first (-1 never), and how often.
double firedAt(const Run &run, int event, int *train = nullptr, bool *standIn = nullptr) {
    for (const TrainEvent &e : run.events)
        if (e.kind == TrainEvent::EventFired && e.index == event) {
            if (train != nullptr)
                *train = e.train;
            if (standIn != nullptr)
                *standIn = e.signal == 1;
            return e.time;
        }
    return -1.0;
}

int firings(const Run &run, int event) {
    int count = 0;
    for (const TrainEvent &e : run.events)
        count += e.kind == TrainEvent::EventFired && e.index == event ? 1 : 0;
    return count;
}

EventPlan timeEvent(int time, int level = 1) {
    EventPlan e;
    e.kind = EventPlan::Time;
    e.time = time;
    e.activation = level;
    return e;
}

EventPlan placeEvent(int node, float distance, float radius, bool onStop = false, int level = 1) {
    EventPlan e;
    e.kind = EventPlan::Location;
    e.places = {Sim::TrackPlace{node, distance}};
    e.radius = radius;
    e.onStop = onStop;
    e.activation = level;
    return e;
}

EventPlan carsEvent(EventPlan::Kind kind, std::vector<unsigned int> cars) {
    EventPlan e;
    e.kind = kind;
    e.cars = std::move(cars);
    e.activation = 1;
    return e;
}

}

int TsreTests::runSimulationEventsSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:simulation-events] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:simulation-events] FAIL" << name; }
    };

    // A straight line of 2 km: track end 1, track 10, track end 2.
    TestTrack::Layout line;
    line.end(1, 10);
    line.track(10, 1, 2, 20, true);
    line.end(2, 10);
    TDB *db = &line.db;
    const QScopedValueRollback<TDB *> live(Game::trackDB, db);
    const auto geometry = std::make_shared<const Sim::TrackGeometry>(db);
    const int t0 = 8 * 3600;
    const auto routeOf = [db](float from, float to) {
        PathTrack::Result made = PathTrack::startAt(db, {10, from});
        made = PathTrack::endAt(db, made.nodes, {10, to});
        return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 31.0);
    };
    const Sim::TrainRoute east = routeOf(100.0f, 1900.0f);
    // The route distance of a place on track 10.
    const auto at = [&east](float distance) {
        double d = -1.0;
        if (!east.parts.empty())
            east.parts[0].find(10, distance, d);
        return d;
    };
    // Two 20 m cars, 20 m/s at most; the player's cars are 5 and 6 of the
    // wagon lists.
    const auto trainOn = [](const Sim::TrainRoute &route, int start, bool player) {
        Sim::TrainPlan p;
        p.player = player;
        p.service = player ? "Player" : "Goods";
        p.serviceTime = player ? -1 : start;
        p.vehicles = {Sim::Vehicle{20.0f, false, 4}, Sim::Vehicle{20.0f, false, 1}};
        if (player) {
            p.vehicles[0].id = 5;
            p.vehicles[1].id = 6;
        }
        p.length = 40.0f;
        p.maxSpeed = 20.0f;
        Sim::SetRates(p);
        p.startTime = start;
        p.route = route;
        p.startFront = route.pathStart + 10.0;
        return p;
    };
    const auto inputOf = [&](std::vector<Sim::TrainPlan> trains, std::vector<EventPlan> events,
                             std::vector<Sim::StaticConsist> consists = {},
                             Sim::SimulationInput::EventStops stops = Sim::SimulationInput::EventStops::None) {
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->eventStops = stops;
        made->geometry = geometry;
        for (size_t i = 0; i < trains.size(); ++i) {
            trains[i].id = int(i);
            made->trains.push_back(trains[i]);
        }
        for (size_t e = 0; e < events.size(); ++e)
            events[e].index = int(e);
        made->events = std::move(events);
        made->staticConsists = std::move(consists);
        return std::shared_ptr<const Sim::SimulationInput>(made);
    };
    const Sim::TrainPlan player = trainOn(east, t0, true);
    check(at(700.0f) > 0.0 && std::fabs(at(1000.0f) - at(700.0f) - 300.0) < 1e-3, "places on track 10 are on the route");

    // The fixed step (decided 2026-10-17): whole steps of 0.1 s, the rest
    // kept for the next call, time without drift.
    {
        Sim::SimulationCore core(inputOf({player}, {}));
        core.step(0.05);
        const bool waited = core.time() == double(t0) && core.steps() == 0;
        core.step(0.05);
        const bool stepped = std::fabs(core.time() - (t0 + 0.1)) < 1e-9 && core.steps() == 1;
        check(waited && stepped, "half a step waits for the other half");
        for (int k = 0; k < 36000; ++k)
            core.step(0.1);
        check(std::fabs(core.time() - (t0 + 3600.1)) < 1e-9 && core.steps() == 36001,
              QString("an hour of steps ends on the second (%1)").arg(core.time() - t0, 0, 'f', 9));
        check(std::fabs(core.onStep(t0 + 10.03) - (t0 + 10.1)) < 1e-9 && std::fabs(core.onStep(t0 + 20.0) - (t0 + 20.0)) < 1e-9,
              "a time is reached at the first whole step at or after it");
        Sim::SimulationCore twice(inputOf({player}, {}));
        for (int k = 0; k < 300; ++k)
            twice.step(0.016);
        Sim::SimulationCore once(inputOf({player}, {}));
        once.step(4.8);
        check(std::fabs(twice.time() - once.time()) < 1e-9 && twice.trains()[0].front == once.trains()[0].front,
              "small and large calls step alike");
    }

    // Time events, levels and a chain: event 0 at 30 s activates 1,
    // raises 2 to 1 and lowers 3 to 0; all of 0, 1 and 2 fire at once. A
    // time event at 0 fires at the first step.
    {
        EventPlan first = timeEvent(30);
        first.activate = {1};
        first.increase = {2};
        first.decrease = {3};
        const Run run = runFor(inputOf({player}, {first, timeEvent(0, 0), timeEvent(0, 0), timeEvent(40), timeEvent(0)}), 60.0);
        check(std::fabs(firedAt(run, 0) - (t0 + 30.0)) < 1e-9, QString("a time event at its time (%1)").arg(firedAt(run, 0) - t0));
        check(firedAt(run, 1) == firedAt(run, 0) && firedAt(run, 2) == firedAt(run, 0) && firedAt(run, 3) < 0.0,
              "activated and raised events fire with it, the lowered one never");
        check(std::fabs(firedAt(run, 4) - (t0 + 0.1)) < 1e-9 && run.states[0].level == 0 && run.states[3].level == 0,
              "a time event at 0 fires at the first step; fired events are no longer active");
        // Restored to their level in the file.
        EventPlan restore = timeEvent(10);
        restore.restore = {1};
        EventPlan lower = timeEvent(5);
        lower.decrease = {1};
        const Run restored = runFor(inputOf({player}, {restore, timeEvent(20), lower}), 30.0);
        check(std::fabs(firedAt(restored, 1) - (t0 + 20.0)) < 1e-9, "a lowered event restored to its level fires");
    }

    // No events in the pre-run: a train started before the activity, an
    // event at 0 fires at the activity's start.
    {
        Sim::TrainPlan early = trainOn(east, t0 - 600, false);
        const Run run = runFor(inputOf({player, early}, {timeEvent(0)}), 5.0);
        check(firedAt(run, 0) >= t0, "events are not checked in the pre-run");
    }

    // Location events: the player's front within the radius at any moment
    // of a step (a 0.5 m radius passed at 20 m/s too); a point the route
    // does not pass never.
    {
        const double place = at(700.0f), small = at(1300.0f);
        bool within = false, smallWithin = false;
        double lastFront = 0.0;
        const Run run = runFor(inputOf({player}, {placeEvent(10, 700.0f, 10.0f), placeEvent(10, 1300.0f, 0.5f),
                                                  placeEvent(99, 700.0f, 10.0f)}),
                               200.0, [&](const Sim::SimulationCore &core) {
                                   const double front = core.trains()[0].front;
                                   const Sim::EventState &state = core.eventStates()[0];
                                   if (state.firedAt == core.time())
                                       within = lastFront < place + 10.0 && front > place - 10.0;
                                   if (core.eventStates()[1].firedAt == core.time())
                                       smallWithin = lastFront < small + 0.5 && front > small - 0.5 && front - lastFront > 1.0;
                                   lastFront = front;
                               });
        int by = -2;
        check(firedAt(run, 0, &by) > 0.0 && within && by == 0, "a location event fires as the player's front reaches its radius");
        check(firedAt(run, 1) > 0.0 && smallWithin, "a radius smaller than a step's travel is not missed");
        check(firedAt(run, 2) < 0.0 && firings(run, 0) == 1, "a point off the route never fires; an event fires once");
    }

    // Standing: at a station stop within the radius it fires while the
    // train stands; at the path's end too. By default the player's train
    // passes a stop-here event (decided 2026-10-21); stopping at every one
    // (an option), it stops there and goes on once it has fired.
    {
        Sim::TrainPlan stopping = player;
        Sim::StopPlan stop;
        stop.part = 0;
        stop.front = at(1000.0f);
        stop.platformEnd = stop.front;
        stop.platformStart = stop.front - 100.0;
        stop.boarding = 20;
        stop.platform = "Middle";
        stopping.stops = {stop};
        const Run passing = runFor(inputOf({stopping}, {placeEvent(10, 600.0f, 10.0f, true)}), 300.0);
        check(firedAt(passing, 0) < 0.0 && passing.states[0].passedAt > 0.0 && passing.states[0].passedActive,
              "by default the train passes a stop-here event without stopping: not fired");
        double arrived = -1.0, departed = -1.0, frontThen = -1.0, leftAt = -1.0;
        const Run run = runFor(inputOf({stopping}, {placeEvent(10, 1000.0f, 10.0f, true), placeEvent(10, 600.0f, 10.0f, true),
                                                    placeEvent(10, 1900.0f, 5.0f, true)}, {},
                                       Sim::SimulationInput::EventStops::All), 300.0,
                               [&](const Sim::SimulationCore &core) {
                                   const Sim::TrainState &s = core.trains()[0];
                                   if (core.eventStates()[1].firedAt == core.time())
                                       frontThen = s.stoppedAt == Sim::StopKind::Event && s.speed == 0.0f ? s.front : -1.0;
                                   if (frontThen > 0.0 && leftAt < 0.0 && s.front > frontThen)
                                       leftAt = core.time();
                               });
        for (const TrainEvent &e : run.events) {
            if (e.kind == TrainEvent::Arrived)
                arrived = e.time;
            if (e.kind == TrainEvent::Departed)
                departed = e.time;
        }
        const double fired = firedAt(run, 0);
        check(fired >= arrived && fired <= departed && arrived > 0.0,
              QString("a stop-here event fires while the train stands within its radius (%1 to %2: %3)")
                  .arg(arrived - t0).arg(departed - t0).arg(fired - t0));
        check(std::fabs(frontThen - at(600.0f)) < 1e-6 && leftAt > firedAt(run, 1) && leftAt - firedAt(run, 1) < 0.2 + 1e-9,
              QString("the player's train stops at a stop-here event (front %1 m from it), leaves once it fired")
                  .arg(frontThen - at(600.0f)));
        check(firedAt(run, 2) > departed, "standing at the path's end fires it");
    }

    // Stopping at every stop-here event (the option): one activated too
    // late to stop for is passed, not fired; one along the platform of the
    // next station stop moves the stop there (the train stands its boarding
    // time); one active from the start but not along the platform is a stop
    // of its own before the station.
    {
        const auto all = Sim::SimulationInput::EventStops::All;
        EventPlan late = placeEvent(10, 600.0f, 10.0f, true, 0);
        EventPlan wake = timeEvent(32);
        wake.activate = {0};
        double speedAtWake = -1.0, gap = -1.0;
        const Run passed = runFor(inputOf({player}, {late, wake}, {}, all), 200.0, [&](const Sim::SimulationCore &core) {
            if (core.eventStates()[1].firedAt == core.time()) {
                speedAtWake = core.trains()[0].speed;
                gap = at(600.0f) - core.trains()[0].front;
            }
        });
        check(firedAt(passed, 1) > 0.0 && gap > 0.0 && gap < speedAtWake * speedAtWake / 2.0 && firedAt(passed, 0) < 0.0,
              QString("activated %1 m before it at %2 m/s (too close to stop): passed, not fired").arg(gap).arg(speedAtWake));
        Sim::TrainPlan stopping = player;
        Sim::StopPlan stop;
        stop.part = 0;
        stop.platformStart = at(900.0f);
        stop.platformEnd = at(1100.0f);
        stop.front = at(1020.0f);
        stop.boarding = 20;
        stop.platform = "Long";
        stopping.stops = {stop};
        double frontAtArrival = -1.0, standingFrom = -1.0, firstStand = -1.0;
        const Run moved = runFor(inputOf({stopping}, {placeEvent(10, 1080.0f, 3.0f, true), placeEvent(10, 800.0f, 3.0f, true)}, {}, all),
                                 300.0, [&](const Sim::SimulationCore &core) {
                                     const Sim::TrainState &s = core.trains()[0];
                                     if (s.stoppedAt == Sim::StopKind::Station && frontAtArrival < 0.0) {
                                         frontAtArrival = s.front;
                                         standingFrom = core.time();
                                     }
                                     if (s.stoppedAt == Sim::StopKind::Event && firstStand < 0.0)
                                         firstStand = s.front;
                                 });
        double departed = -1.0;
        for (const TrainEvent &e : moved.events)
            if (e.kind == TrainEvent::Departed)
                departed = e.time;
        check(std::fabs(frontAtArrival - at(1080.0f)) < 1e-6 && firedAt(moved, 0) >= standingFrom - 0.1
                  && departed - standingFrom > 19.8,
              QString("a stop-here event along the platform: the stop moves to it (front %1 m from it), boarding there")
                  .arg(frontAtArrival - at(1080.0f)));
        check(std::fabs(firstStand - at(800.0f)) < 1e-6 && firedAt(moved, 1) > 0.0 && firedAt(moved, 1) < firedAt(moved, 0),
              "one before the platform is a stop of its own first");
    }

    // A reversal: the train stands at the reversal point for an instant
    // (it reverses within the step): a stop-here event there fires. A
    // reversible event on track 10, passed out and back, fires twice, its
    // outcome (event 2 raised) acting once.
    {
        TestTrack::Layout loop;
        TestTrack::passingLoop(loop);
        TDB *tdb = &loop.db;
        const QScopedValueRollback<TDB *> loopDb(Game::trackDB, tdb);
        PathTrack::Result path = PathTrack::startAt(tdb, {10, 20.0f});
        path = PathTrack::reverseAt(tdb, path.nodes, {13, 60.0f});
        path = PathTrack::endAt(tdb, path.nodes, {10, 50.0f});
        const Sim::TrainRoute there = Sim::BuildRoute(tdb, path.nodes, PathTrack::legs(tdb, path.nodes), 100.0);
        Sim::TrainPlan back = trainOn(there, t0, true);
        back.startFront = std::max(there.pathStart + 10.0, 41.0);
        EventPlan reversible = placeEvent(10, 80.0f, 5.0f);
        reversible.reversible = true;
        reversible.increase = {2};
        auto made = std::make_shared<Sim::SimulationInput>(*inputOf({back}, {placeEvent(13, 60.0f, 5.0f, true), reversible,
                                                                             timeEvent(0, -5)}));
        made->geometry = std::make_shared<const Sim::TrackGeometry>(tdb);
        int reversals = 0;
        const Run run = runFor(made, 600.0);
        for (const TrainEvent &e : run.events)
            reversals += e.kind == TrainEvent::Reversed ? 1 : 0;
        check(there.parts.size() == 2 && reversals == 1 && firedAt(run, 0) > 0.0,
              "a stop-here event at a reversal point fires as the train reverses");
        check(firings(run, 1) == 2 && run.states[2].level == -4,
              QString("a reversible event fires on each pass (%1), its outcomes once (level %2)")
                  .arg(firings(run, 1)).arg(run.states[2].level));
    }

    // The activity's end: success at 10 s, nothing after; a failure wins
    // over success in one event.
    {
        EventPlan done = timeEvent(10);
        done.success = true;
        const Run run = runFor(inputOf({player}, {done, timeEvent(20)}), 30.0);
        bool ended = false;
        for (const TrainEvent &e : run.events)
            ended = ended || (e.kind == TrainEvent::ActivityEnded && e.index == 1 && std::fabs(e.time - (t0 + 10.0)) < 1e-9);
        check(run.end == Sim::ActivityEnd::Success && std::fabs(run.endedAt - (t0 + 10.0)) < 1e-9 && ended
                  && firedAt(run, 1) < 0.0,
              "success ends the activity; no event fires after");
        EventPlan both = timeEvent(10);
        both.success = true;
        both.failure = true;
        check(runFor(inputOf({player}, {both}), 20.0).end == Sim::ActivityEnd::Failure, "a failure wins over success");
    }

    // ORTSContinue: a message staying 30 s while the game runs holds the
    // other events; 0 and none hold nothing.
    {
        for (int stays : {30, 0, -1}) {
            EventPlan message = timeEvent(10);
            message.message = true;
            message.autoContinue = stays;
            const Run run = runFor(inputOf({player}, {message, timeEvent(20)}), 60.0);
            const double expected = t0 + (stays > 0 ? 40.0 : 20.0);
            check(std::fabs(firedAt(run, 1) - expected) < 1e-9,
                  QString("a message staying %1 s: the next event at %2 s").arg(stays).arg(firedAt(run, 1) - t0));
        }
    }

    // A triggering AI train: before it starts the player's train stands in
    // (Open Rails); after, the AI train itself.
    {
        Sim::TrainPlan goods = trainOn(east, t0 + 300, false);
        EventPlan early = placeEvent(10, 700.0f, 10.0f);
        early.trigger = 1;
        EventPlan late = placeEvent(10, 700.0f, 10.0f, false, 0);
        late.trigger = 1;
        EventPlan wake = timeEvent(200);
        wake.activate = {1};
        const Run run = runFor(inputOf({player, goods}, {early, late, wake}), 600.0);
        int by = -2, lateBy = -2;
        bool standIn = false, lateStandIn = true;
        const double first = firedAt(run, 0, &by, &standIn), second = firedAt(run, 1, &lateBy, &lateStandIn);
        check(first > 0.0 && first < t0 + 200.0 && by == 0 && standIn && run.states[0].standIn,
              "before its triggering train starts, the player's train fires the event");
        check(second > t0 + 300.0 && lateBy == 1 && !lateStandIn, "after it starts, the triggering train does");
    }

    // AllStops: when the player leaves its last stop; never without stops.
    {
        Sim::TrainPlan stopping = player;
        for (float place : {800.0f, 1200.0f}) {
            Sim::StopPlan stop;
            stop.part = 0;
            stop.front = at(place);
            stop.platformEnd = stop.front;
            stop.platformStart = stop.front - 100.0;
            stop.boarding = 30;
            stopping.stops.push_back(stop);
        }
        EventPlan all;
        all.kind = EventPlan::AllStops;
        all.activation = 1;
        const Run run = runFor(inputOf({stopping}, {all}), 400.0);
        double lastDeparture = -1.0;
        for (const TrainEvent &e : run.events)
            if (e.kind == TrainEvent::Departed && e.index == 1)
                lastDeparture = e.time;
        check(lastDeparture > 0.0 && firedAt(run, 0) >= lastDeparture && firedAt(run, 0) - lastDeparture <= 0.1 + 1e-9,
              QString("AllStops as the last stop is done (left %1, fired %2)").arg(lastDeparture - t0).arg(firedAt(run, 0) - t0));
        check(firedAt(runFor(inputOf({player}, {all}), 200.0), 0) < 0.0, "AllStops never for a train without stops");
    }

    // ReachSpeed: the player's train reaching the speed; a speed above its
    // top never.
    {
        EventPlan fast;
        fast.kind = EventPlan::ReachSpeed;
        fast.activation = 1;
        fast.speed = 15.0f;
        EventPlan faster = fast;
        faster.speed = 25.0f;
        float speedThen = -1.0f;
        const Run run = runFor(inputOf({player}, {fast, faster}), 200.0, [&](const Sim::SimulationCore &core) {
            if (core.eventStates()[0].firedAt == core.time())
                speedThen = core.trains()[0].speed;
        });
        check(speedThen >= 15.0f && speedThen < 15.2f && firedAt(run, 1) < 0.0,
              QString("ReachSpeed at the speed (%1 m/s); above the train's top speed never").arg(speedThen));
    }

    // Wagon lists: the player's cars 5 and 6, a loose consist of cars 1 and
    // 2 of object 3 standing 1500 to 1540 m along track 10.
    {
        const unsigned int a = 3u * 65536u + 1u, b = 3u * 65536u + 2u;
        Sim::StaticConsist loose;
        loose.track.push_back(Sim::RouteInterval{10, 1500.0f, 1540.0f, 0.0});
        loose.cars = {a, b};
        const auto siding = [](EventPlan e, float from, float to) {
            e.siding = true;
            e.sidingNode = 10;
            e.sidingFrom = from;
            e.sidingTo = to;
            return e;
        };
        const std::vector<EventPlan> events = {
            carsEvent(EventPlan::AssembleTrain, {5, 6}),
            carsEvent(EventPlan::AssembleTrain, {6, 5}),
            carsEvent(EventPlan::AssembleTrain, {5}),
            carsEvent(EventPlan::AssembleTrain, {a, b}),
            siding(carsEvent(EventPlan::DropOffWagonsAtLocation, {b, a}), 1490.0f, 1560.0f),
            siding(carsEvent(EventPlan::DropOffWagonsAtLocation, {a, b}), 100.0f, 200.0f),
            siding(carsEvent(EventPlan::DropOffWagonsAtLocation, {5}), 0.0f, 2000.0f),
            carsEvent(EventPlan::PickUpWagons, {5}),
            carsEvent(EventPlan::PickUpWagons, {5, a}),
            siding(carsEvent(EventPlan::AssembleTrainAtLocation, {5, 6}), 1850.0f, 1950.0f),
            carsEvent(EventPlan::PickUpPassengers, {}),
            carsEvent(EventPlan::DropOffWagonsAtLocation, {a}),
        };
        double frontThen = -1.0;
        const Run run = runFor(inputOf({player}, events, {loose}), 200.0, [&](const Sim::SimulationCore &core) {
            if (core.eventStates()[9].firedAt == core.time())
                frontThen = core.trains()[0].front;
        });
        const double first = t0 + 0.1;
        int byPlayer = -2, byLoose = -2;
        check(std::fabs(firedAt(run, 0, &byPlayer) - first) < 1e-9 && std::fabs(firedAt(run, 1) - first) < 1e-9
                  && firedAt(run, 2) < 0.0 && byPlayer == 0,
              "AssembleTrain: the player's train holding exactly the cars, in order or reversed");
        check(std::fabs(firedAt(run, 3, &byLoose) - first) < 1e-9 && byLoose == -1, "AssembleTrain: a loose consist holding them");
        check(firedAt(run, 4) > 0.0 && firedAt(run, 5) < 0.0 && firedAt(run, 6) < 0.0 && firedAt(run, 11) > 0.0,
              "DropOff: the cars standing with an end in the siding (any order; no siding: anywhere); still in the player's "
              "train not");
        check(std::fabs(firedAt(run, 7) - first) < 1e-9 && firedAt(run, 8) < 0.0,
              "PickUpWagons: the player's train holding the cars, standing");
        check(frontThen >= at(1850.0f) - 1e-6 && frontThen < at(1850.0f) + 2.5 && firedAt(run, 10) < 0.0,
              QString("AssembleTrainAtLocation as the front enters the siding (%1 m in); PickUpPassengers never")
                  .arg(frontThen - at(1850.0f)));
    }

    // Restarting a waiting AI train (ORTSRestartWaitingTrain): standing at
    // its waiting point of 600 s, it leaves 30 s after the event; still
    // coming to it, it waits 30 s there; another value is left alone.
    {
        Sim::TrainPlan goods = trainOn(east, t0, false);
        Sim::WaitPlan wait;
        wait.part = 0;
        wait.front = at(600.0f);
        wait.value = 600;
        goods.waits = {wait};
        const auto restartAt = [&](int time, int value) {
            EventPlan restart = timeEvent(time);
            restart.restartTrain = 0;
            restart.restartWait = value;
            restart.restartDelay = 30;
            return restart;
        };
        const auto leaves = [&](const EventPlan &restart, int *restarted) {
            double arrived = -1.0, left = -1.0;
            const Run run = runFor(inputOf({goods}, {restart}), 900.0, [&](const Sim::SimulationCore &core) {
                const Sim::TrainState &s = core.trains()[0];
                if (arrived < 0.0 && s.stoppedAt == Sim::StopKind::Wait)
                    arrived = core.time();
                if (arrived > 0.0 && left < 0.0 && s.status == Sim::TrainStatus::Running)
                    left = core.time();
            });
            *restarted = 0;
            for (const TrainEvent &e : run.events)
                *restarted += e.kind == TrainEvent::Restarted && e.train == 0 ? 1 : 0;
            return std::make_pair(arrived, left);
        };
        int restarted = 0;
        const auto standing = leaves(restartAt(100, 600), &restarted);
        check(standing.first > 0.0 && standing.first < t0 + 100.0 && standing.second >= t0 + 130.0 - 1e-6
                  && standing.second <= t0 + 130.1 + 1e-6
                  && restarted == 1,
              QString("restarted standing: it leaves 30 s after the event (arrived %1, left %2)")
                  .arg(standing.first - t0).arg(standing.second - t0));
        const auto coming = leaves(restartAt(2, 600), &restarted);
        check(coming.first > t0 + 2.0 && std::fabs(coming.second - coming.first - 30.0) < 0.21 && restarted == 1,
              QString("restarted still coming: it waits 30 s there (%1 s)").arg(coming.second - coming.first));
        const auto other = leaves(restartAt(100, 300), &restarted);
        check(std::fabs(other.second - other.first - 600.0) < 0.21 && restarted == 0, "a waiting point of another value is left alone");
    }

    // Why events did not fire (section 7): a location event passed before
    // it was active (activated at 60 s); one never active; ReachSpeed
    // above the train's top speed (its best kept); AssembleTrainAtLocation
    // with the siding reached but other cars.
    {
        EventPlan passed = placeEvent(10, 600.0f, 10.0f, false, 0);
        EventPlan wake = timeEvent(60);
        wake.activate = {0};
        EventPlan faster;
        faster.kind = EventPlan::ReachSpeed;
        faster.activation = 1;
        faster.speed = 25.0f;
        EventPlan other = carsEvent(EventPlan::AssembleTrainAtLocation, {5, 99});
        other.siding = true;
        other.sidingNode = 10;
        other.sidingFrom = 1850.0f;
        other.sidingTo = 1950.0f;
        const Run run = runFor(inputOf({player}, {passed, wake, faster, other, timeEvent(0, 0)}), 300.0);
        const Sim::EventState &p = run.states[0];
        check(p.fired == 0 && p.passedAt > t0 && p.passedAt < t0 + 60.0 && !p.passedActive
                  && std::fabs(p.activeSince - (t0 + 60.0)) < 1e-6,
              QString("passed at %1 s before it was active (from %2 s)").arg(p.passedAt - t0).arg(p.activeSince - t0));
        check(run.states[4].activeSince < 0.0 && run.states[2].fired == 0 && std::fabs(run.states[2].bestSpeed - 20.0f) < 0.01f
                  && run.states[3].fired == 0 && run.states[3].reachedSiding,
              QString("never active; the best speed %1 m/s of 25 asked; the siding reached with other cars")
                  .arg(run.states[2].bestSpeed));
    }

    // Frames share the events' states while they do not change.
    {
        // The first step notes it active.
        Sim::SimulationCore core(inputOf({player}, {timeEvent(1)}));
        core.step(0.1);
        auto first = Sim::MakeFrame(core);
        core.step(0.4);
        auto same = Sim::MakeFrame(core, first.get());
        core.step(1.0);
        auto fired = Sim::MakeFrame(core, same.get());
        check(same->eventStates == first->eventStates && fired->eventStates != same->eventStates
                  && (*fired->eventStates)[0].fired == 1,
              "frames share event states until one changes");
    }

    // A location event's point on the track: within 2.5 m of the track's
    // centre line by x and z only (Open Rails' MaximumCenterlineOffset). The
    // place as the activity's files give it (z the files' way, as
    // getDrawPositionOnTrNode gives it), turned as MakeInput turns it.
    {
        float a[7], b[7];
        db->getDrawPositionOnTrNode(a, 10, 703.0f);
        db->getDrawPositionOnTrNode(b, 10, 707.0f);
        const float dx = b[0] - a[0], dz = b[2] - a[2], length = std::hypot(dx, dz);
        const float side[2] = {-dz / length, dx / length};
        const auto placesAt = [&](float offset, float height) {
            float posT[2] = {a[5], -a[6]};
            float pos[3] = {a[0] + side[0] * offset, a[1] + height, -(a[2] + side[1] * offset)};
            QVector<TDB::IntersectionPoint> points;
            db->findNearestPositionsOnTDB(posT, pos, points, 2.5f, false);
            return points;
        };
        const QVector<TDB::IntersectionPoint> near = placesAt(2.0f, 40.0f);
        check(!near.isEmpty() && int(near[0].idx) == 10 && std::fabs(near[0].m - 703.0f) < 0.01f && placesAt(3.0f, 0.0f).isEmpty(),
              QString("a point 2 m beside the track (40 m above) is on it at %1 m; 3 m beside is not")
                  .arg(near.isEmpty() ? -1.0f : near[0].m));
    }

    qInfo().noquote() << QString("simulation-events: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
