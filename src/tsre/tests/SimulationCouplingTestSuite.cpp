#include <tsre/tests/SimulationTestSuite.h>

#include <QDebug>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
#include <functional>
#include <tsre/Game.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/SimulationTexts.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrackNetwork.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/PathTrack.h>

namespace {

using Sim::TrainEvent;

// A vehicle of `length` metres: an engine (type 4) or a wagon, its car id.
Sim::Vehicle vehicle(float length, bool engine, unsigned int id) {
    Sim::Vehicle v{length, false, engine ? 4 : 2};
    v.id = id;
    return v;
}

std::vector<unsigned int> idsOf(const std::vector<Sim::Vehicle> &vehicles) {
    std::vector<unsigned int> ids;
    for (const Sim::Vehicle &v : vehicles)
        ids.push_back(v.id);
    return ids;
}

const TrainEvent *firstOf(const std::vector<TrainEvent> &all, TrainEvent::Kind kind) {
    for (const TrainEvent &e : all)
        if (e.kind == kind)
            return &e;
    return nullptr;
}

}

int TsreTests::runSimulationCouplingSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:simulation-coupling] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:simulation-coupling] FAIL" << name; }
    };

    // The passing loop: track end 1, track 10 (100 m) to junction 2, its main
    // track 11 (200 m) and the loop 12 to junction 3, track 13 (100 m) to
    // track end 4.
    TestTrack::Layout loop;
    TestTrack::passingLoop(loop);
    TDB *db = &loop.db;
    const QScopedValueRollback<TDB *> live(Game::trackDB, db);
    const auto geometry = std::make_shared<const Sim::TrackGeometry>(db);
    const auto network = std::make_shared<const Sim::TrackNetwork>(db, *geometry, nullptr, false);
    const int t0 = 8 * 3600;
    // From track 10 at 20 m over the main track to track 13 at 90 m, or
    // reversing at track 13 at 60 m back to track 10 at 30 m.
    const auto east = [&] {
        PathTrack::Result made = PathTrack::startAt(db, {10, 20.0f});
        made = PathTrack::endAt(db, made.nodes, {13, 90.0f});
        return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 40.0);
    }();
    const auto outAndBack = [&] {
        PathTrack::Result made = PathTrack::startAt(db, {10, 20.0f});
        made = PathTrack::reverseAt(db, made.nodes, {13, 60.0f});
        made = PathTrack::endAt(db, made.nodes, {10, 30.0f});
        return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 40.0);
    }();
    const auto trainOn = [&](const Sim::TrainRoute &route, std::vector<Sim::Vehicle> vehicles, bool player) {
        Sim::TrainPlan p;
        p.player = player;
        p.service = player ? "Player" : "Goods";
        p.vehicles = std::move(vehicles);
        for (const Sim::Vehicle &v : p.vehicles)
            p.length += v.length;
        p.maxSpeed = 10.0f;
        Sim::SetRates(p);
        p.startTime = t0;
        p.route = route;
        p.startFront = std::min(route.pathStart + p.length, route.parts.front().length());
        for (const Sim::RouteTrack &part : route.parts) {
            p.projection.push_back(network->project(part));
            p.limits.push_back({});
        }
        return p;
    };
    // Cars of loose consist 3 (ids 3 × 65536 + n) standing on a track.
    const auto loose = [](int track, float from, std::vector<unsigned int> ids) {
        Sim::StaticConsist c;
        c.name = "Loose";
        for (unsigned int id : ids) {
            c.vehicles.push_back(vehicle(10.0f, false, id));
            c.length += 10.0f;
        }
        c.track.push_back(Sim::RouteInterval{track, from, from + c.length, 0.0});
        return c;
    };
    const auto inputOf = [&](std::vector<Sim::TrainPlan> trains, std::vector<Sim::StaticConsist> consists,
                             std::vector<Sim::EventPlan> events = {}) {
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->geometry = geometry;
        made->network = network;
        for (size_t i = 0; i < trains.size(); ++i) {
            trains[i].id = int(i);
            made->trains.push_back(trains[i]);
        }
        made->staticConsists = std::move(consists);
        for (size_t e = 0; e < events.size(); ++e)
            events[e].index = int(e);
        made->events = std::move(events);
        return std::shared_ptr<const Sim::SimulationInput>(made);
    };
    struct Run {
        std::vector<TrainEvent> events;
        float speedAtContact = -1.0f;
    };
    // Runs `seconds`; the events, and the train's speed in the step it
    // coupled; `each` after every step.
    const auto runFor = [](Sim::SimulationCore &core, double seconds,
                           const std::function<void()> &each = nullptr) {
        Run out;
        float lastSpeed = 0.0f;
        for (int k = 0; k < int(seconds * 10); ++k) {
            core.step(Sim::SimulationCore::MaxStep);
            if (each)
                each();
            for (const TrainEvent &e : core.takeEvents()) {
                if (e.kind == TrainEvent::Coupled && out.speedAtContact < 0.0f)
                    out.speedAtContact = lastSpeed;
                out.events.push_back(e);
            }
            lastSpeed = core.trains()[0].speed;
        }
        return out;
    };
    const unsigned int a = 3u * 65536u + 1u, b = 3u * 65536u + 2u;

    check(east.parts.size() == 1 && outAndBack.parts.size() == 2, "the test paths: one part, and out and back");

    // The player's train takes a loose consist on its way (Open Rails' AI:
    // a loose consist is always attached to), coming in at the coupling
    // speed; the cars join at its front, turned; it goes on to its path's
    // end with them.
    {
        Sim::EventPlan assembled;
        assembled.kind = Sim::EventPlan::AssembleTrain;
        assembled.activation = 1;
        assembled.cars = {b, a, 5, 6};
        // A DropOff of car a on the loop, which the train never takes.
        Sim::EventPlan elsewhere;
        elsewhere.kind = Sim::EventPlan::DropOffWagonsAtLocation;
        elsewhere.activation = 1;
        elsewhere.cars = {a};
        elsewhere.siding = true;
        elsewhere.sidingNode = 12;
        elsewhere.sidingFrom = 10.0f;
        elsewhere.sidingTo = 190.0f;
        Sim::SimulationCore core(inputOf({trainOn(east, {vehicle(10.0f, true, 5), vehicle(10.0f, false, 6)}, true)},
                                         {loose(11, 100.0f, {a, b})}, {assembled, elsewhere}));
        // Closing on the cars no faster than Open Rails' AI follows a
        // standing train: √(distance × deceleration) / 2, the creep speed
        // at least.
        double contact = -1.0, over = 0.0;
        east.parts[0].find(11, 100.0f, contact);
        const double deceleration = core.input().trains[0].deceleration;
        const Run run = runFor(core, 600.0, [&] {
            const Sim::TrainState &s = core.trains()[0];
            const double distance = contact - s.front;
            if (s.part == 0 && distance > 0.0 && core.standingTrains()[0].gone == false)
                over = std::max(over, double(s.speed) - std::max(0.5 * std::sqrt(distance * deceleration),
                                                                  Sim::SimulationCore::CreepSpeed));
        });
        check(contact > 0.0 && over < 0.3, QString("it closes on the cars as Open Rails' AI follows a standing train "
                                                   "(at most %1 m/s over)").arg(over, 0, 'f', 2));
        const Sim::CarsNow carsNow = Sim::CarsOf(core);
        const QString where = Sim::WhyNotFired(core.input(), 1, core.eventStates()[1], core.activityEnd(),
                                               core.activityEndedAt(), &carsNow);
        const QString atStart = Sim::WhyNotFired(core.input(), 1, core.eventStates()[1], core.activityEnd(),
                                                 core.activityEndedAt());
        check(core.eventStates()[1].fired == 0 && where.contains(Sim::TrainName(core.input(), 0)) && !where.contains("Loose")
                  && atStart.contains("Loose"),
              "why a wagon event did not fire names where the run left the cars: " + where);
        const TrainEvent *coupled = firstOf(run.events, TrainEvent::Coupled);
        const std::vector<unsigned int> ids = idsOf(core.vehiclesOf(0));
        check(coupled != nullptr && coupled->signal == 2 && core.standingTrains()[0].gone
                  && ids == std::vector<unsigned int>({b, a, 5, 6}) && std::fabs(core.lengthOf(0) - 40.0) < 1e-6,
              QString("a train takes a loose consist on its way: %1 cars, the loose consist gone").arg(ids.size()));
        check(run.speedAtContact > 0.0f && run.speedAtContact <= Sim::SimulationCore::CouplingSpeed + 0.05f,
              QString("it comes in at the coupling speed (%1 m/s)").arg(run.speedAtContact));
        check(core.trains()[0].stoppedAt == Sim::StopKind::End && core.eventStates()[0].fired == 1,
              "it goes on to its path's end; AssembleTrain fires on the train it made");
    }

    // Placed with a loose consist in the section its front stands in (Open
    // Rails' CanPlaceTrain: clear of it): ahead, the train starts and
    // attaches to it (the player's train: an AI train this near its path's
    // end would join it); behind, it starts and leaves it there.
    {
        Sim::SimulationCore ahead(inputOf({trainOn(east, {vehicle(10.0f, true, 5), vehicle(10.0f, false, 6)}, true)},
                                          {loose(10, 60.0f, {a, b})}));
        const Run run = runFor(ahead, 600.0);
        check(firstOf(run.events, TrainEvent::Blocked) == nullptr && firstOf(run.events, TrainEvent::Coupled) != nullptr
                  && idsOf(ahead.vehiclesOf(0)) == std::vector<unsigned int>({b, a, 5, 6})
                  && ahead.trains()[0].stoppedAt == Sim::StopKind::End,
              "placed with a loose consist ahead in its section: it starts and takes it along");
        Sim::SimulationCore behind(inputOf({trainOn(east, {vehicle(10.0f, true, 5), vehicle(10.0f, false, 6)}, false)},
                                           {loose(10, 0.0f, {a})}));
        const Run left = runFor(behind, 600.0);
        check(firstOf(left.events, TrainEvent::Blocked) == nullptr && firstOf(left.events, TrainEvent::Coupled) == nullptr
                  && behind.vehiclesOf(0).size() == 2 && !behind.standingTrains()[0].gone
                  && behind.trains()[0].stoppedAt == Sim::StopKind::End,
              "placed with a loose consist behind it in its section: it starts and leaves it");
    }

    // An AI train at its path's end joins the loose consist it meets there
    // and is gone; the standing train is all the cars.
    {
        const auto shortPath = [&] {
            PathTrack::Result made = PathTrack::startAt(db, {10, 20.0f});
            made = PathTrack::endAt(db, made.nodes, {11, 150.0f});
            return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 40.0);
        }();
        Sim::SimulationCore core(inputOf({trainOn(shortPath, {vehicle(10.0f, true, Sim::NoCar), vehicle(10.0f, false, Sim::NoCar)}, false)},
                                         {loose(11, 100.0f, {a, b})}));
        const Run run = runFor(core, 600.0);
        const Sim::SimulationCore::Standing &st = core.standingTrains()[0];
        check(firstOf(run.events, TrainEvent::Joined) != nullptr && core.trains()[0].status == Sim::TrainStatus::Finished
                  && !st.gone && st.vehicles.size() == 4 && std::fabs(st.length - 40.0) < 1e-6,
              QString("an AI train at its path's end joins the loose consist: %1 cars standing").arg(st.vehicles.size()));
    }

    // Waiting point 40205: the first 2 cars kept, the third a standing
    // train where it was; a DropOff of that car in the siding there fires
    // (it is a train of exactly that car). 40105 with the engine last: no
    // engine in the kept part, no cut.
    {
        Sim::TrainPlan cutting = trainOn(east, {vehicle(10.0f, true, 5), vehicle(10.0f, false, 6), vehicle(10.0f, false, 7)}, true);
        double at = -1.0;
        cutting.route.parts[0].find(11, 80.0f, at);
        cutting.waits = {Sim::WaitPlan{0, at, 40205}};
        Sim::EventPlan drop;
        drop.kind = Sim::EventPlan::DropOffWagonsAtLocation;
        drop.activation = 1;
        drop.cars = {7};
        drop.siding = true;
        drop.sidingNode = 11;
        drop.sidingFrom = 40.0f;
        drop.sidingTo = 90.0f;
        Sim::SimulationCore core(inputOf({cutting}, {}, {drop}));
        const Run run = runFor(core, 600.0);
        const TrainEvent *cut = firstOf(run.events, TrainEvent::Uncoupled);
        check(cut != nullptr && cut->signal == 1 && idsOf(core.vehiclesOf(0)) == std::vector<unsigned int>({5, 6})
                  && core.standingTrains().size() == 1 && idsOf(core.standingTrains()[0].vehicles) == std::vector<unsigned int>({7})
                  && core.trains()[0].stoppedAt == Sim::StopKind::End,
              "4NNSS: the first cars kept, the rest a standing train; the train goes on");
        check(core.eventStates()[0].fired == 1, "DropOff fires on the cars cut off in the siding");
        Sim::EventPlan assemble;
        assemble.kind = Sim::EventPlan::AssembleTrain;
        assemble.activation = 1;
        assemble.cars = {5, 7};
        const Sim::CarsNow left = Sim::CarsOf(core);
        Sim::EventState never;
        never.activeSince = t0;
        auto withAssemble = std::make_shared<Sim::SimulationInput>(core.input());
        withAssemble->events = {assemble};
        const QString why = Sim::WhyNotFired(*withAssemble, 0, never, Sim::ActivityEnd::None, -1.0, &left);
        check(why.contains("standing where Player left it"), "cars cut off: where the train left them: " + why);
        Sim::TrainPlan refused = trainOn(east, {vehicle(10.0f, false, 5), vehicle(10.0f, false, 6), vehicle(10.0f, true, 7)}, true);
        refused.waits = {Sim::WaitPlan{0, at, 40105}};
        Sim::SimulationCore none(inputOf({refused}, {}));
        const Run kept = runFor(none, 600.0);
        check(firstOf(kept.events, TrainEvent::Uncoupled) == nullptr && none.vehiclesOf(0).size() == 3,
              "no cut that leaves the moving part without an engine");
    }

    // Waiting point 50105 before a reversal: the last car (the engine) kept,
    // the cars ahead of it a standing train; its own cut-off cars on the
    // rest of the part, the train reverses where it stands and runs back.
    {
        Sim::TrainPlan cutting = trainOn(outAndBack, {vehicle(10.0f, false, 5), vehicle(10.0f, false, 6), vehicle(10.0f, true, 7)}, true);
        double at = -1.0;
        cutting.route.parts[0].find(13, 45.0f, at);
        cutting.waits = {Sim::WaitPlan{0, at, 50105}};
        Sim::SimulationCore core(inputOf({cutting}, {}));
        const Run run = runFor(core, 900.0);
        const TrainEvent *cut = firstOf(run.events, TrainEvent::Uncoupled);
        const TrainEvent *reversed = firstOf(run.events, TrainEvent::Reversed);
        check(cut != nullptr && cut->signal == 2 && reversed != nullptr && reversed->time >= cut->time
                  && reversed->travelled - cut->travelled < 1.0
                  && idsOf(core.vehiclesOf(0)) == std::vector<unsigned int>({7}) && core.trains()[0].stoppedAt == Sim::StopKind::End
                  && firstOf(run.events, TrainEvent::Coupled) == nullptr,
              QString("5NNSS: the engine kept, the cars ahead left; it reverses where it stands (%1 m on) and runs back "
                      "without them").arg(reversed != nullptr && cut != nullptr ? reversed->travelled - cut->travelled : -1.0));
    }

    // A train cuts a car off, runs on, reverses and comes back the same
    // way: it attaches to its own car again (more than 5 m away by then).
    {
        Sim::TrainPlan cutting = trainOn(outAndBack, {vehicle(10.0f, true, 5), vehicle(10.0f, false, 6), vehicle(10.0f, false, 7)}, true);
        double at = -1.0;
        cutting.route.parts[0].find(11, 80.0f, at);
        cutting.waits = {Sim::WaitPlan{0, at, 40205}};
        Sim::SimulationCore core(inputOf({cutting}, {}));
        const Run run = runFor(core, 900.0);
        const TrainEvent *cut = firstOf(run.events, TrainEvent::Uncoupled);
        const TrainEvent *coupled = firstOf(run.events, TrainEvent::Coupled);
        check(cut != nullptr && coupled != nullptr && coupled->time > cut->time && core.vehiclesOf(0).size() == 3
                  && core.standingTrains()[0].gone && core.trains()[0].stoppedAt == Sim::StopKind::End,
              "coming back, it attaches to the car it cut off and takes it to its path's end");
    }

    // Living trains (Open Rails' AI). The goods train h1 h2 h3 (its engine
    // leading east) from track 11 at 120 m stands at a waiting point (its
    // front at track 13 at 60 m) for 15 minutes. A train whose path part
    // ends in the section h's rear stands in attaches to it: its engine
    // leading, it takes h's cars up to h's engine; a car leading, it leaves
    // h its cars up to its own engine; on its path's last part it joins h.
    const unsigned int h1 = 1u * 65536u, h2 = h1 + 1u, h3 = h1 + 2u, b1 = 2u * 65536u, b2 = b1 + 1u;
    const auto goods = [&](const std::vector<Sim::WaitPlan> &waits, float from) {
        PathTrack::Result made = PathTrack::startAt(db, {11, from});
        made = PathTrack::endAt(db, made.nodes, {13, 90.0f});
        Sim::TrainPlan p = trainOn(Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 40.0),
                                   {vehicle(10.0f, true, h1), vehicle(10.0f, false, h2), vehicle(10.0f, false, h3)}, false);
        for (Sim::WaitPlan wait : waits) {
            double at = -1.0;
            p.route.parts[0].find(wait.part, float(wait.front), at);
            wait.part = 0;
            wait.front = at;
            p.waits.push_back(wait);
        }
        return p;
    };
    // (Waits given by track node and distance there.)
    const Sim::TrainPlan standingGoods = goods({Sim::WaitPlan{13, 60.0, 900}}, 120.0f);
    const auto visitor = [&](const Sim::TrainRoute &route, std::vector<Sim::Vehicle> vehicles) {
        Sim::TrainPlan p = trainOn(route, std::move(vehicles), false);
        p.startTime = t0 + 60;
        return p;
    };
    const auto toAndBack = [&] {
        PathTrack::Result made = PathTrack::startAt(db, {10, 20.0f});
        made = PathTrack::reverseAt(db, made.nodes, {13, 50.0f});
        made = PathTrack::endAt(db, made.nodes, {10, 30.0f});
        return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 40.0);
    }();
    const auto toThere = [&] {
        PathTrack::Result made = PathTrack::startAt(db, {10, 20.0f});
        made = PathTrack::endAt(db, made.nodes, {13, 50.0f});
        return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 40.0);
    }();
    const auto eventOf = [](const std::vector<TrainEvent> &all, int train, TrainEvent::Kind kind) -> const TrainEvent * {
        for (const TrainEvent &e : all)
            if (e.train == train && e.kind == kind)
                return &e;
        return nullptr;
    };
    {
        Sim::SimulationCore core(inputOf({standingGoods, visitor(toAndBack, {vehicle(10.0f, true, b1), vehicle(10.0f, false, b2)})}, {}));
        const Run run = runFor(core, 1500.0);
        const TrainEvent *took = eventOf(run.events, 1, TrainEvent::TookCars);
        check(took != nullptr && took->signal == 2 && took->index == 0 && eventOf(run.events, 0, TrainEvent::LeftCars) != nullptr
                  && idsOf(core.vehiclesOf(1)) == std::vector<unsigned int>({h2, h3, b1, b2})
                  && idsOf(core.vehiclesOf(0)) == std::vector<unsigned int>({h1}),
              QString("its engine leading, it takes the goods train's cars up to that train's engine (%1 cars)")
                  .arg(core.vehiclesOf(1).size()));
        const TrainEvent *back = eventOf(run.events, 1, TrainEvent::Reversed);
        check(core.trains()[1].stoppedAt == Sim::StopKind::End && core.trains()[1].reversed
                  && back != nullptr && took != nullptr && back->travelled - took->travelled < 1.0
                  && core.trains()[0].status == Sim::TrainStatus::Finished,
              "both go on: it reverses where it stands and runs back with them, the goods train to its path's end");
    }
    {
        Sim::SimulationCore core(inputOf({standingGoods, visitor(toAndBack, {vehicle(10.0f, false, b2), vehicle(10.0f, true, b1)})}, {}));
        const Run run = runFor(core, 1500.0);
        const TrainEvent *left = eventOf(run.events, 1, TrainEvent::LeftCars);
        const TrainEvent *back = eventOf(run.events, 1, TrainEvent::Reversed);
        check(left != nullptr && left->signal == 1 && eventOf(run.events, 0, TrainEvent::TookCars) != nullptr
                  && back != nullptr && back->travelled - left->travelled < 1.0
                  && idsOf(core.vehiclesOf(1)) == std::vector<unsigned int>({b1})
                  && idsOf(core.vehiclesOf(0)) == std::vector<unsigned int>({h1, h2, h3, b2})
                  && core.trains()[1].stoppedAt == Sim::StopKind::End,
              "a car leading, it leaves the goods train its cars up to its own engine, reverses short of it and runs back");
    }
    {
        Sim::SimulationCore core(inputOf({standingGoods, visitor(toThere, {vehicle(10.0f, true, b1), vehicle(10.0f, false, b2)})}, {}));
        const Run run = runFor(core, 1500.0);
        check(eventOf(run.events, 1, TrainEvent::JoinedTrain) != nullptr && core.trains()[1].status == Sim::TrainStatus::Finished
                  && core.trains()[1].joinedTo == 0
                  && idsOf(core.vehiclesOf(0)) == std::vector<unsigned int>({h1, h2, h3, b1, b2})
                  && core.trains()[0].status == Sim::TrainStatus::Finished,
              "on its path's last part it joins the goods train, which takes it along to its path's end");
    }
    // A train whose path goes on past the goods train (standing on track 11
    // for two minutes) does not attach: it waits behind it and follows.
    {
        const auto past = [&] {
            PathTrack::Result made = PathTrack::startAt(db, {10, 20.0f});
            made = PathTrack::endAt(db, made.nodes, {13, 95.0f});
            return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 40.0);
        }();
        Sim::SimulationCore core(inputOf({goods({Sim::WaitPlan{11, 150.0, 120}}, 50.0f),
                                          visitor(past, {vehicle(10.0f, true, b1), vehicle(10.0f, false, b2)})}, {}));
        const Run run = runFor(core, 1500.0);
        check(eventOf(run.events, 1, TrainEvent::TookCars) == nullptr && eventOf(run.events, 1, TrainEvent::JoinedTrain) == nullptr
                  && eventOf(run.events, 1, TrainEvent::Blocked) != nullptr && core.vehiclesOf(1).size() == 2
                  && eventOf(run.events, 1, TrainEvent::Finished) != nullptr,
              "its path going on past a standing train, it waits behind it and follows");
    }
    // Waiting point 60001 (Open Rails' join and split): the train attaches
    // to the goods train standing ahead (on track 11, not at its path's
    // end) and joins it; the goods train's 40305 (keep its first 3 cars)
    // cuts it off again, and it runs on along its own path.
    {
        const auto past = [&] {
            PathTrack::Result made = PathTrack::startAt(db, {10, 20.0f});
            made = PathTrack::endAt(db, made.nodes, {13, 95.0f});
            return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 40.0);
        }();
        Sim::TrainPlan joining = visitor(past, {vehicle(10.0f, true, b1), vehicle(10.0f, false, b2)});
        double at = -1.0;
        joining.route.parts[0].find(10, 60.0f, at);
        joining.waits = {Sim::WaitPlan{0, at, 60001}};
        Sim::SimulationCore core(inputOf({goods({Sim::WaitPlan{11, 150.0, 300}, Sim::WaitPlan{13, 60.0, 40305}}, 50.0f), joining}, {}));
        const Run run = runFor(core, 1500.0);
        const TrainEvent *joined = eventOf(run.events, 1, TrainEvent::JoinedTrain);
        const TrainEvent *parted = eventOf(run.events, 1, TrainEvent::Parted);
        const TrainEvent *finished = eventOf(run.events, 1, TrainEvent::Finished);
        check(joined != nullptr && joined->index == 0 && eventOf(run.events, 0, TrainEvent::TookCars) != nullptr,
              "after 60001 it joins the goods train standing ahead, not at its path's end");
        check(parted != nullptr && parted->time > joined->time && eventOf(run.events, 0, TrainEvent::LeftCars) != nullptr
                  && idsOf(core.vehiclesOf(1)) == std::vector<unsigned int>({b1, b2})
                  && idsOf(core.vehiclesOf(0)) == std::vector<unsigned int>({h1, h2, h3})
                  && finished != nullptr && finished->time > parted->time,
              "the goods train's cut gives it back its cars; it runs on to its own path's end");
    }

    // Cars standing across the junction onto the loop, the train's path on
    // the main track: taken, they stand off its path and it cannot go on
    // with them; it is removed (Open Rails: out of control, OUT_OF_PATH).
    {
        Sim::StaticConsist across;
        across.name = "Across";
        for (unsigned int k = 0; k < 4; ++k) {
            across.vehicles.push_back(vehicle(10.0f, false, a + 10 + k));
            across.length += 10.0f;
        }
        across.track = {Sim::RouteInterval{10, 80.0f, 100.0f, 0.0}, Sim::RouteInterval{12, 220.0f, 200.0f, 0.0}};
        Sim::SimulationCore core(inputOf({trainOn(outAndBack, {vehicle(20.0f, true, 7)}, false)}, {across}));
        const Run run = runFor(core, 300.0);
        check(firstOf(run.events, TrainEvent::Coupled) != nullptr && firstOf(run.events, TrainEvent::Removed) != nullptr
                  && core.trains()[0].status == Sim::TrainStatus::Finished && core.standingTrains()[0].gone,
              "a train taking cars that stand off its path is removed (Open Rails' OUT_OF_PATH)");
    }

    // A run with no player's train (task 08: a timetable set, or an
    // activity without a player service, runs every train as AI): it runs
    // to its end; an event looking at the player's train never fires.
    {
        Sim::EventPlan place;
        place.kind = Sim::EventPlan::Location;
        place.places = {Sim::TrackPlace{13, 50.0f}};
        place.radius = 10.0f;
        place.activation = 1;
        Sim::SimulationCore core(inputOf({trainOn(east, {vehicle(20.0f, true, 7)}, false)}, {}, {place}));
        runFor(core, 300.0);
        check(core.trains()[0].status == Sim::TrainStatus::Finished && core.eventStates()[0].fired == 0,
              "a run with no player's train: to its end, the player's event not fired");
    }

    qInfo().noquote() << QString("simulation-coupling: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
