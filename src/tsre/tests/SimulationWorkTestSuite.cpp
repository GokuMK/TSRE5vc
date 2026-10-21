#include <tsre/tests/SimulationTestSuite.h>

#include <QDebug>
#include <QScopedValueRollback>
#include <algorithm>
#include <tsre/Game.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrackNetwork.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/sim/WorkPlanner.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/PathTrack.h>

namespace {

Sim::Vehicle car(float length, bool engine, unsigned int id) {
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

QString describe(const Sim::WorkPlan &plan) {
    QStringList goals;
    for (const Sim::WorkGoal &goal : plan.goals) {
        QStringList steps;
        for (const Sim::WorkStep &step : goal.steps)
            steps << QString("%1 legs, %2").arg(step.legs.size()).arg(step.what);
        goals << QString("%1: %2 (%3 m, %4 reversals) [%5]%6")
                         .arg(goal.name, goal.planned ? "planned" : "not planned")
                         .arg(goal.metres, 0, 'f', 0)
                         .arg(goal.reversals)
                         .arg(steps.join("; "), goal.why.isEmpty() ? QString() : " why: " + goal.why);
    }
    return goals.join(" | ");
}

}

int TsreTests::runSimulationWorkSuite(bool verbose) {
    int passed = 0, failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) { ++passed; if (verbose) qInfo().noquote() << "[tests:simulation-work] PASS" << name; }
        else { ++failed; qWarning().noquote() << "[tests:simulation-work] FAIL" << name; }
    };

    // Two lines. A siding facing an eastbound train: track end 1, track 10
    // (500 m) to junction 2, its main output track 11 (500 m) to track end
    // 3, its other output track 12 (200 m) to track end 4. A siding
    // trailing for it: track end 5, track 20 (500 m) to junction 6's main
    // output, its input track 21 (500 m) to track end 7, its other output
    // track 22 (200 m) to track end 8.
    TestTrack::Layout layout;
    layout.end(1, 10);
    layout.track(10, 1, 2, 50);
    layout.junction(2, 10, 11, 12);
    layout.track(11, 2, 3, 50);
    layout.end(3, 11);
    layout.track(12, 2, 4, 20);
    layout.end(4, 12);
    layout.end(5, 20);
    layout.track(20, 5, 6, 50);
    layout.junction(6, 21, 20, 22);
    layout.track(21, 6, 7, 50);
    layout.end(7, 21);
    layout.track(22, 6, 8, 20);
    layout.end(8, 22);
    // A third with a loop: track end 40, track 30 (200 m) to junction 31,
    // its outputs track 32 (main, 300 m) and track 33 (the loop, laid the
    // other way) to junction 34, its input track 35 (200 m) to junction 36,
    // its outputs track 37 (200 m, to track end 41) and track 38 (a siding
    // facing an eastbound train, 200 m, to track end 42).
    layout.end(40, 30);
    layout.track(30, 40, 31, 20);
    layout.junction(31, 30, 32, 33);
    layout.track(32, 31, 34, 30);
    layout.track(33, 34, 31, 32);
    layout.junction(34, 35, 32, 33);
    layout.track(35, 34, 36, 20);
    layout.junction(36, 35, 37, 38);
    layout.track(37, 36, 41, 20);
    layout.end(41, 37);
    layout.track(38, 36, 42, 20);
    layout.end(42, 38);
    TDB *db = &layout.db;
    const QScopedValueRollback<TDB *> live(Game::trackDB, db);
    const auto geometry = std::make_shared<const Sim::TrackGeometry>(db);
    const auto network = std::make_shared<const Sim::TrackNetwork>(db, *geometry, nullptr, false);
    // (Turned: starting against the node's way.)
    const auto routeOf = [&](PathTrack::TrackPoint from, PathTrack::TrackPoint to, bool turn = false) {
        PathTrack::Result made = PathTrack::startAt(db, from);
        if (turn)
            made = PathTrack::turnStart(db, made.nodes);
        made = PathTrack::endAt(db, made.nodes, to);
        return Sim::BuildRoute(db, made.nodes, PathTrack::legs(db, made.nodes), 60.0);
    };
    // The player's train on a route, its vehicles from its front back.
    const auto inputOf = [&](const Sim::TrainRoute &route, std::vector<Sim::Vehicle> vehicles,
                             std::vector<Sim::StaticConsist> loose, std::vector<Sim::EventPlan> events) {
        auto made = std::make_shared<Sim::SimulationInput>();
        made->geometry = geometry;
        made->network = network;
        Sim::TrainPlan p;
        p.player = true;
        p.service = "Player";
        p.vehicles = std::move(vehicles);
        for (const Sim::Vehicle &v : p.vehicles)
            p.length += v.length;
        p.route = route;
        p.startFront = std::min(route.pathStart + p.length, route.parts.front().length());
        made->trains.push_back(p);
        made->staticConsists = std::move(loose);
        for (size_t e = 0; e < events.size(); ++e)
            events[e].index = int(e);
        made->events = std::move(events);
        return std::shared_ptr<const Sim::SimulationInput>(made);
    };
    const auto looseOn = [](int track, float from, std::vector<unsigned int> ids) {
        Sim::StaticConsist c;
        c.name = "Loose";
        for (unsigned int id : ids) {
            c.vehicles.push_back(car(10.0f, false, id));
            c.length += 10.0f;
        }
        c.track.push_back(Sim::RouteInterval{track, from, from + c.length, 0.0});
        return c;
    };
    const auto wagonEvent = [](Sim::EventPlan::Kind kind, std::vector<unsigned int> cars, int sidingNode = -1) {
        Sim::EventPlan e;
        e.kind = kind;
        e.activation = 1;
        e.cars = std::move(cars);
        e.name = kind == Sim::EventPlan::PickUpWagons ? "pick up" : kind == Sim::EventPlan::DropOffWagonsAtLocation ? "set out" : "assemble";
        if (sidingNode >= 0) {
            e.siding = true;
            e.sidingNode = sidingNode;
            e.sidingFrom = 20.0f;
            e.sidingTo = 180.0f;
        }
        return e;
    };
    const unsigned int e1 = 1, w2 = 2, w3 = 3, w4 = 4, x = 9, a = 3u * 65536u + 1u, b = a + 1u, c = a + 2u;
    const Sim::TrainRoute east = routeOf({10, 100.0f}, {11, 400.0f});
    const Sim::TrainRoute eastTrailing = routeOf({20, 100.0f}, {21, 400.0f});
    const Sim::TrainRoute westTrailing = routeOf({21, 400.0f}, {20, 100.0f}, true);
    const Sim::TrainRoute throughLoop = routeOf({30, 50.0f}, {37, 150.0f});
    check(!east.parts.empty() && !eastTrailing.parts.empty() && !westTrailing.parts.empty(), "the test paths");
    if (east.parts.empty() || eastTrailing.parts.empty() || westTrailing.parts.empty() || throughLoop.parts.empty()) {
        qInfo().noquote() << QString("simulation-work: %1 passed, %2 failed").arg(passed).arg(failed);
        return 1;
    }

    // A pickup in the siding the train faces: in, couple, the cars its own.
    {
        const Sim::WorkPlan plan = Sim::PlanWork(*inputOf(east, {car(20.0f, true, e1), car(10.0f, false, w2)},
                                                          {looseOn(12, 100.0f, {a, b})},
                                                          {wagonEvent(Sim::EventPlan::PickUpWagons, {a, b})}));
        const Sim::WorkGoal *goal = plan.goals.empty() ? nullptr : &plan.goals[0];
        check(goal != nullptr && goal->planned && goal->couplings == 1 && goal->reversals == 0 && plan.train.vehicles.size() == 4
                  && plan.cuts.empty() && plan.back > 0.0,
              "a pickup in the siding ahead: in and couple, no reversal; back to the path's end: " + describe(plan));
    }
    // Cars not asked for beyond them: left standing after coupling.
    {
        const Sim::WorkPlan plan = Sim::PlanWork(*inputOf(east, {car(20.0f, true, e1)}, {looseOn(12, 100.0f, {a, b, c})},
                                                          {wagonEvent(Sim::EventPlan::PickUpWagons, {a, b})}));
        const Sim::WorkGoal *goal = plan.goals.empty() ? nullptr : &plan.goals[0];
        check(goal != nullptr && goal->planned && goal->couplings == 1 && goal->cuts == 1 && plan.cuts.size() == 1
                  && idsOf(plan.cuts[0].vehicles) == std::vector<unsigned int>({c}),
              "a pickup of the cars nearer: the one beyond left standing: " + describe(plan));
    }
    // A set-out into the siding behind the junction for an eastbound train,
    // the car at the train's rear: on past the junction, back into the
    // siding car first, cut. With the car at the front: no way to put it in
    // first there (no loop to turn the train). Westbound, the siding faces
    // the train: the car at its front straight in.
    {
        const Sim::WorkPlan plan = Sim::PlanWork(*inputOf(eastTrailing, {car(20.0f, true, e1), car(10.0f, false, w2), car(10.0f, false, x)},
                                                          {}, {wagonEvent(Sim::EventPlan::DropOffWagonsAtLocation, {x}, 22)}));
        const Sim::WorkGoal *goal = plan.goals.empty() ? nullptr : &plan.goals[0];
        check(goal != nullptr && goal->planned && goal->cuts == 1 && goal->reversals == 1 && plan.cuts.size() == 1
                  && plan.cuts[0].track.front().node == 22,
              "the rear car into the siding behind the junction, by a reversal past it: " + describe(plan));
        const Sim::WorkPlan wrong = Sim::PlanWork(*inputOf(eastTrailing, {car(10.0f, false, x), car(20.0f, true, e1), car(10.0f, false, w2)},
                                                           {}, {wagonEvent(Sim::EventPlan::DropOffWagonsAtLocation, {x}, 22)}));
        check(!wrong.goals.empty() && !wrong.goals[0].planned && !wrong.goals[0].why.isEmpty(),
              "the car at the train's front: no way to put it in first there: " + describe(wrong));
        const Sim::WorkPlan facing = Sim::PlanWork(*inputOf(westTrailing, {car(10.0f, false, x), car(20.0f, true, e1), car(10.0f, false, w2)},
                                                            {}, {wagonEvent(Sim::EventPlan::DropOffWagonsAtLocation, {x}, 22)}));
        check(!facing.goals.empty() && facing.goals[0].planned && facing.goals[0].reversals == 0,
              "westbound, the siding facing the train: the front car straight in: " + describe(facing));
    }
    // A car between others: the cars behind it left on the line, it set
    // out, the cars coupled again.
    {
        const Sim::WorkPlan plan = Sim::PlanWork(*inputOf(eastTrailing, {car(20.0f, true, e1), car(10.0f, false, x),
                                                                         car(10.0f, false, w3), car(10.0f, false, w4)},
                                                          {}, {wagonEvent(Sim::EventPlan::DropOffWagonsAtLocation, {x}, 22)}));
        const Sim::WorkGoal *goal = plan.goals.empty() ? nullptr : &plan.goals[0];
        check(goal != nullptr && goal->planned && goal->cuts == 2 && goal->couplings == 1 && plan.train.vehicles.size() == 3
                  && plan.cuts.size() == 1 && idsOf(plan.cuts[0].vehicles) == std::vector<unsigned int>({x}),
              "a car behind the engine: the rest left, the car set out, the rest coupled again: " + describe(plan));
    }
    // A car of the siding asked for between the train's second and third
    // cars (an assemble): the train cut there, the front part to the car
    // and back to the rest.
    {
        // (On the line with the siding behind the junction: the front part's
        // rear end, at the gap, gets to the car by a reversal past it.)
        const Sim::WorkPlan plan = Sim::PlanWork(*inputOf(eastTrailing, {car(20.0f, true, e1), car(10.0f, false, w2), car(10.0f, false, w3)},
                                                          {looseOn(22, 100.0f, {a})},
                                                          {wagonEvent(Sim::EventPlan::AssembleTrain, {e1, w2, a, w3})}));
        const Sim::WorkGoal *goal = plan.goals.empty() ? nullptr : &plan.goals[0];
        std::vector<unsigned int> made = idsOf(plan.train.vehicles);
        std::reverse(made.begin(), made.end());
        check(goal != nullptr && goal->planned && goal->couplings == 2 && goal->cuts == 1
                  && (made == std::vector<unsigned int>({e1, w2, a, w3}) || idsOf(plan.train.vehicles) == std::vector<unsigned int>({e1, w2, a, w3})),
              "a car put in between two of the train's: " + describe(plan));
    }
    // The car behind the engine, a siding it must go into first ahead: left
    // on the loop's main track, the engine round it by the loop, the car
    // pushed in.
    {
        const Sim::WorkPlan plan = Sim::PlanWork(*inputOf(throughLoop, {car(20.0f, true, e1), car(10.0f, false, x)}, {},
                                                          {wagonEvent(Sim::EventPlan::DropOffWagonsAtLocation, {x}, 38)}));
        const Sim::WorkGoal *goal = plan.goals.empty() ? nullptr : &plan.goals[0];
        bool round = false;
        for (const Sim::WorkStep &step : goal != nullptr ? goal->steps : std::vector<Sim::WorkStep>())
            round = round || step.what.contains("round the cars");
        check(goal != nullptr && goal->planned && round && plan.cuts.size() == 1 && plan.cuts[0].track.front().node == 38,
              "a run-around by the loop for the car to lead into the siding: " + describe(plan));
    }
    // The whole train asked for: into the siding as it is, no cut.
    {
        const Sim::WorkPlan plan = Sim::PlanWork(*inputOf(east, {car(20.0f, true, e1), car(10.0f, false, w2)}, {},
                                                          {wagonEvent(Sim::EventPlan::DropOffWagonsAtLocation, {e1, w2}, 12)}));
        const Sim::WorkGoal *goal = plan.goals.empty() ? nullptr : &plan.goals[0];
        check(goal != nullptr && goal->planned && goal->cuts == 0 && plan.train.track.back().node == 12,
              "the whole train asked for: into the siding as it is: " + describe(plan));
    }
    // The event chain: a pickup made active by another event, a failure
    // never taken to fire.
    {
        Sim::EventPlan first;
        first.kind = Sim::EventPlan::Time;
        first.activation = 1;
        first.name = "first";
        first.activate = {2};
        Sim::EventPlan trap;
        trap.kind = Sim::EventPlan::Location;
        trap.activation = 1;
        trap.name = "trap";
        trap.failure = true;
        trap.activate = {3};
        Sim::EventPlan pick = wagonEvent(Sim::EventPlan::PickUpWagons, {a, b});
        pick.activation = 0;
        Sim::EventPlan never = wagonEvent(Sim::EventPlan::PickUpWagons, {c});
        never.activation = 0;
        const Sim::WorkPlan plan = Sim::PlanWork(*inputOf(east, {car(20.0f, true, e1)}, {looseOn(12, 100.0f, {a, b})},
                                                          {first, trap, pick, never}));
        check(plan.goals.size() == 1 && plan.goals[0].event == 2 && plan.notes.size() == 1,
              "the goals in the order the event chain makes them active: " + describe(plan) + " / " + plan.notes.join("; "));
    }

    qInfo().noquote() << QString("simulation-work: %1 passed, %2 failed").arg(passed).arg(failed);
    return failed == 0 ? 0 : 1;
}
