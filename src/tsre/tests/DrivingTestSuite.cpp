#include <tsre/tests/DrivingTestSuite.h>

#include <QFile>
#include <QScopedValueRollback>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <QString>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/sim/AirBrake.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrackNetwork.h>
#include <tsre/sim/TrainPhysics.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/sim/VehicleModel.h>
#include <tsre/tdb/SigCfg.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/trains/VehicleDefinition.h>

using Sim::TrainPhysics;

namespace {

bool equal(double a, double b, double relative = 1e-6) {
    return std::fabs(a - b) <= relative * std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
}

// A train of `count` cars of `mass` kg and `length` m, couplers of
// `stiffness` N/m with `slack` m free play each way.
TrainPhysics trainOf(int count, double mass, double length, double stiffness, double slack) {
    TrainPhysics t;
    for (int i = 0; i < count; ++i)
        t.cars.push_back({mass, 0.0, length, 0.0});
    for (int i = 0; i + 1 < count; ++i) {
        TrainPhysics::Coupler c;
        c.stiffness = stiffness;
        c.damping = TrainPhysics::defaultDamping(stiffness, mass, mass);
        c.tension = c.compression = slack;
        t.couplers.push_back(c);
    }
    t.placeFrom(1000.0);
    return t;
}

// A signal configuration of one type, a home signal: red or green.
const char *HomeSignals = R"(SIMISA@@@@@@@@@@JINX0G0t______

LightTextures ( 1
	LightTex ( "ltex" "SigLight.ace" 0 0 1 1 )
)
LightsTab ( 2
	LightsTabEntry ( "Red Light" colour ( 255 255 40 40 ) )
	LightsTabEntry ( "Green Light" colour ( 255 40 255 40 ) )
)
SignalTypes ( 1
	SignalType ( "Home"
		SignalFnType ( NORMAL )
		SignalLightTex ( "ltex" )
		SignalLights ( 2
			SignalLight ( 0 "Red Light" Position ( 0 0 0 ) Radius ( 0.1 ) )
			SignalLight ( 1 "Green Light" Position ( 0 0.3 0 ) Radius ( 0.1 ) )
		)
		SignalDrawStates ( 2
			SignalDrawState ( 0 "Red" DrawLights ( 1 DrawLight ( 0 ) ) )
			SignalDrawState ( 1 "Green" DrawLights ( 1 DrawLight ( 1 ) ) )
		)
		SignalAspects ( 2
			SignalAspect ( STOP "Red" )
			SignalAspect ( CLEAR_2 "Green" )
		)
		SignalNumClearAhead ( 2 )
	)
)
)";

bool writeUtf16(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QStringEncoder encoder(QStringEncoder::Utf16LE);
    return file.write("\xff\xfe" + QByteArray(encoder(text))) > 0;
}

// Brakes as most MSTS stock has them (psi a second, ft³).
const char *AirBrakes = R"(
	BrakeSystemType ( "Air_single_pipe" )
	BrakeEquipmentType ( "Handbrake, Triple_valve, Auxilary_reservoir" )
	MaxApplicationRate ( 20 )
	MaxReleaseRate ( 20 )
	MaxAuxilaryChargingRate ( 25 )
	TripleValveRatio ( 2.5 )
	BrakeCylinderPressureForMaxBrakeBrakeForce ( 50 )
	EmergencyResCapacity ( 14 ))";
const char *EpBrakes = R"(
	BrakeSystemType ( "EP" )
	BrakeEquipmentType ( "EP_brake" )
	MaxApplicationRate ( 20 )
	MaxReleaseRate ( 20 )
	BrakeCylinderPressureForMaxBrakeBrakeForce ( 70 ))";

// An engine of 80 t, 300 kN, 5 MW, 40 m/s, and a 40 t wagon, without
// resistance.
QString engineFile(const char *brakes) {
    return QString("SIMISA@@@@@@@@@@JINX0D0t______\n\nWagon ( TestEngine\n\tType ( Engine )\n\tSize ( 3m 4m 20m )\n"
                   "\tMass ( 80t )\n\tORTSDavis_A ( 0 ) ORTSDavis_B ( 0 ) ORTSDavis_C ( 0 )\n\tMaxBrakeForce ( 80kN )%1\n)\n"
                   "Engine ( TestEngine\n\tType ( Electric )\n\tMaxPower ( 5000kW )\n\tMaxForce ( 300kN )\n"
                   "\tMaxVelocity ( 144kmh )\n\tTrainBrakesControllerMaxSystemPressure ( 90 )\n"
                   "\tTrainBrakesControllerFullServicePressureDrop ( 26 )\n)\n")
            .arg(brakes);
}
QString wagonFile(const char *brakes) {
    return QString("SIMISA@@@@@@@@@@JINX0D0t______\n\nWagon ( TestWagon\n\tType ( Freight )\n\tSize ( 3m 4m 15m )\n"
                   "\tMass ( 40t )\n\tORTSDavis_A ( 0 ) ORTSDavis_B ( 0 ) ORTSDavis_C ( 0 )\n\tMaxBrakeForce ( 40kN )%1\n)\n")
            .arg(brakes);
}

constexpr double Psi = 6894.757293168;

} // namespace

int TsreTests::runDrivingSuite(bool verbose) {
    TokenTest::Suite test{"[tests:driving]", verbose};
    const double dt = 0.01;

    // One car: F / m.
    {
        TrainPhysics t = trainOf(1, 80000.0, 20.0, 1e7, 0.0);
        std::vector<TrainPhysics::Forces> f(1);
        f[0].drive = 80000.0;
        for (int i = 0; i < 1000; ++i)
            t.step(dt, f);
        test.check(equal(t.cars[0].speed, 10.0, 1e-9) && equal(t.cars[0].front - 1000.0, 50.0 + 0.05, 1e-6),
                   QString("one car: 1 m/s² for 10 s, 10 m/s: %1 m/s, %2 m").arg(t.cars[0].speed).arg(t.cars[0].front - 1000.0));
    }

    // Two cars: the force shared, the coupler's force settling at the
    // second car's share; the slack taken up first.
    {
        TrainPhysics t = trainOf(2, 50000.0, 20.0, 2e6, 0.05);
        std::vector<TrainPhysics::Forces> f(2);
        f[0].drive = 100000.0;
        bool secondStoodFirst = false;
        for (int i = 0; i < 3000; ++i) {
            t.step(dt, f);
            if (i == 5)
                secondStoodFirst = t.cars[1].speed == 0.0 && t.cars[0].speed > 0.0;
        }
        const double a = 100000.0 / 100000.0;
        test.check(secondStoodFirst && equal(t.cars[0].speed, t.cars[1].speed, 1e-3) && equal(t.cars[1].speed, a * 30.0 - 0.0, 0.02)
                           && equal(t.couplers[0].force, 50000.0, 0.01) && t.couplers[0].extension > 0.05,
                   QString("two cars: the second waits for the slack, then both 1 m/s², the coupler pulling 50 kN: "
                           "%1 / %2 m/s, %3 N, %4 m")
                           .arg(t.cars[0].speed).arg(t.cars[1].speed).arg(t.couplers[0].force).arg(t.couplers[0].extension));
    }

    // Fifty cars, stiff couplers: stable at 0.01 s, the coupler forces no
    // more than half again the pull (the slack run in, they overshoot it);
    // the slack taken up from the front, the last car moving last; the slack
    // run-in dying out.
    {
        TrainPhysics t = trainOf(50, 40000.0, 15.0, 1e7, 0.02);
        std::vector<TrainPhysics::Forces> f(50);
        f[0].drive = 400000.0;
        int lastMoved = -1;
        bool finite = true;
        double strongest = 0.0, spreadAt30 = 0.0;
        const auto spreadOf = [&] {
            double spread = 0.0;
            for (const auto &c : t.cars)
                spread = std::max(spread, std::fabs(c.speed - t.cars[0].speed));
            return spread;
        };
        for (int i = 0; i < 12000; ++i) {
            t.step(dt, f);
            if (lastMoved < 0 && t.cars[49].speed > 0.0)
                lastMoved = i;
            for (const auto &c : t.cars)
                finite = finite && std::isfinite(c.speed) && std::isfinite(c.front);
            for (const auto &c : t.couplers)
                strongest = std::max(strongest, std::fabs(c.force));
            if (i == 2999)
                spreadAt30 = spreadOf();
        }
        const double spread = spreadOf();
        const double expected = 400000.0 / (50 * 40000.0) * 120.0;
        test.check(finite && lastMoved > 0 && strongest < 1.5 * 400000.0 && spread < 0.01 && spread < spreadAt30
                           && equal(t.cars[25].speed, expected, 0.01),
                   QString("fifty cars at 0.01 s: stable, couplers at most %1 kN, the last moving after %2 steps; "
                           "speeds within %3 m/s at 30 s, %4 at 120 s; %5 m/s (expected %6)")
                           .arg(strongest / 1000.0).arg(lastMoved).arg(spreadAt30).arg(spread).arg(t.cars[25].speed).arg(expected));
    }

    // Friction holds a standing car up to its force, and stops a moving one
    // without turning it round.
    {
        TrainPhysics held = trainOf(1, 10000.0, 10.0, 1e7, 0.0);
        std::vector<TrainPhysics::Forces> f(1);
        f[0].drive = 5000.0;
        f[0].friction = 8000.0;
        for (int i = 0; i < 500; ++i)
            held.step(dt, f);
        TrainPhysics slides = trainOf(1, 10000.0, 10.0, 1e7, 0.0);
        f[0].drive = 12000.0;
        for (int i = 0; i < 500; ++i)
            slides.step(dt, f);
        TrainPhysics stops = trainOf(1, 10000.0, 10.0, 1e7, 0.0);
        stops.cars[0].speed = 3.0;
        f[0].drive = 0.0;
        f[0].friction = 10000.0;
        bool neverBack = true;
        for (int i = 0; i < 500; ++i) {
            stops.step(dt, f);
            neverBack = neverBack && stops.cars[0].speed >= 0.0;
        }
        test.check(held.cars[0].speed == 0.0 && held.cars[0].front == 1000.0 && equal(slides.cars[0].speed, 2.0, 1e-6)
                           && neverBack && stops.cars[0].speed == 0.0 && equal(stops.cars[0].front - 1000.0, 4.5, 0.01),
                   QString("friction holds (%1 m/s), is beaten (%2 m/s), stops a car without turning it (%3 m)")
                           .arg(held.cars[0].speed).arg(slides.cars[0].speed).arg(stops.cars[0].front - 1000.0));
    }

    // The same run twice: the same numbers.
    {
        const auto run = [&] {
            TrainPhysics t = trainOf(10, 50000.0, 18.0, 5e6, 0.03);
            std::vector<TrainPhysics::Forces> f(10);
            for (int i = 0; i < 2000; ++i) {
                f[0].drive = i < 1000 ? 300000.0 : 0.0;
                for (auto &g : f)
                    g.friction = i < 1000 ? 500.0 : 20000.0;
                t.step(dt, f);
            }
            return t;
        };
        const TrainPhysics a = run(), b = run();
        bool same = true;
        for (size_t i = 0; i < a.cars.size(); ++i)
            same = same && a.cars[i].front == b.cars[i].front && a.cars[i].speed == b.cars[i].speed;
        test.check(same, "a run repeats exactly");
    }

    // Messages: any name, a number, text or a flag, as text and back.
    {
        Sim::Message m{"BATTERY_CHARGE", 44.0};
        Sim::Message back;
        Sim::Message text;
        test.check(m.toText() == "BATTERY_CHARGE = 44" && Sim::Message::fromText("CAB_AMBIENT_LIGHT = disable", text)
                           && text.name == "CAB_AMBIENT_LIGHT" && text.text() == "disable" && !text.flag()
                           && Sim::Message::fromText(m.toText(), back) && back.number() == 44.0
                           && Sim::Message{"pantograph1", QString("up")}.flag(),
                   "messages: any name and value, as text and back");
    }
    // Vehicles read from files, as the simulation reads them.
    QTemporaryDir files;
    writeUtf16(files.path() + "/engine.eng", engineFile(AirBrakes));
    writeUtf16(files.path() + "/wagon.wag", wagonFile(AirBrakes));
    writeUtf16(files.path() + "/ep.eng", engineFile(EpBrakes));
    writeUtf16(files.path() + "/ep.wag", wagonFile(EpBrakes));
    const auto vehicle = [&](const QString &name) {
        Sim::Vehicle v;
        v.definition = VehicleDefinition::read(files.path(), name, files.path(), false);
        return v;
    };
    const Sim::Vehicle engine = vehicle("engine.eng"), wagon = vehicle("wagon.wag");
    // The core driving a train on a straight 2 km track: the engine and two
    // wagons, driven from its start.
    QScopedValueRollback<TDB *> restoreTrack(Game::trackDB);
    TestTrack::Layout straight;
    straight.end(1, 10);
    straight.track(10, 1, 2, 200);
    straight.end(2, 10);
    TDB *line = &straight.db;
    Game::trackDB = line;
    const PathTrack::Result run = PathTrack::startAt(line, {10, 100.0f});
    const Sim::TrainRoute route = Sim::BuildRoute(line, run.nodes, PathTrack::legs(line, run.nodes), 31.0);
    const int t0 = 8 * 3600;
    const auto geometry = std::make_shared<const Sim::TrackGeometry>(line);
    const auto planOf = [&](std::vector<Sim::Vehicle> vehicles = {}) {
        Sim::TrainPlan p;
        p.vehicles = vehicles.empty() ? std::vector<Sim::Vehicle>{engine, wagon, wagon} : vehicles;
        p.length = 0.0f;
        for (const Sim::Vehicle &v : p.vehicles)
            p.length += float(v.length());
        p.maxSpeed = 30.0f;
        Sim::SetRates(p);
        p.startTime = t0;
        p.route = route;
        p.startFront = route.pathStart + p.length + 1.0;
        return p;
    };
    const auto inputOf = [&](int drivenTrain) {
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->geometry = geometry;
        made->trains.push_back(planOf());
        made->step = drivenTrain >= 0 ? 0.01 : 0.1;
        made->driven = drivenTrain;
        return std::shared_ptr<const Sim::SimulationInput>(made);
    };
    const auto eventsOf = [](Sim::SimulationCore &core, std::vector<Sim::TrainEvent> &all) {
        for (const Sim::TrainEvent &e : core.takeEvents())
            all.push_back(e);
    };
    const auto has = [](const std::vector<Sim::TrainEvent> &all, Sim::TrainEvent::Kind kind) {
        return std::any_of(all.begin(), all.end(), [kind](const Sim::TrainEvent &e) { return e.kind == kind; });
    };
    {
        Sim::SimulationCore core(inputOf(0));
        std::vector<Sim::TrainEvent> all;
        core.step(0.05);
        eventsOf(core, all);
        const bool taken = core.drivenTrain() == 0 && core.cabOf(0) == 0 && has(all, Sim::TrainEvent::DrivingStarted)
                && core.outputsOf(0, -1) != nullptr;
        core.command(0, -1, {"throttle", 1.0});
        // Expected: the same step, the engine's force within the rail's
        // adhesion (80 t × 9.81 × 0.33) and its power, the train's mass.
        double v = 0.0;
        const double mass = 160000.0, adhesion = 80000.0 * 9.81 * 0.33;
        for (int i = 0; i < 2000; ++i) {
            double force = std::min(300000.0, adhesion);
            if (v > 0.0 && force * v > 5e6)
                force = 5e6 / v;
            v += 0.01 * force / mass;
        }
        const double frontBefore = core.trains()[0].front;
        core.step(20.0);
        eventsOf(core, all);
        const Sim::TrainState &s = core.trains()[0];
        std::vector<Sim::TrackPose> fronts, rears;
        test.check(taken && equal(s.speed, v, 0.01) && core.vehicleEnds(0, fronts, rears) && fronts.size() == 3,
                   QString("driven from its start, full throttle 20 s: %1 m/s (expected %2), %3 m on")
                           .arg(s.speed).arg(v).arg(s.front - frontBefore));
        // Its brakes: it stops, never rolling back.
        core.command(0, -1, {"throttle", 0.0});
        core.command(0, -1, {"trainBrake", 1.0});
        bool neverBack = true;
        for (int i = 0; i < 6000 && core.trains()[0].speed > 0.0f; ++i) {
            core.step(0.01);
            neverBack = neverBack && core.trains()[0].speed >= 0.0f;
        }
        core.step(5.0);
        test.check(neverBack && core.trains()[0].speed == 0.0f && core.trains()[0].status == Sim::TrainStatus::Running,
                   QString("braked to a stand, never rolling back: %1 m/s").arg(core.trains()[0].speed));
        // On to its path's end: it stops there, told.
        core.command(0, -1, {"trainBrake", 0.0});
        core.command(0, -1, {"throttle", 0.5});
        for (int i = 0; i < 4000 && !has(all, Sim::TrainEvent::EndOfPath); ++i) {
            core.step(0.1);
            eventsOf(core, all);
        }
        test.check(has(all, Sim::TrainEvent::EndOfPath) && core.trains()[0].speed == 0.0f
                           && core.trains()[0].front >= route.parts[0].length() - 0.01,
                   QString("at its path's end it stops, told: %1 of %2 m").arg(core.trains()[0].front).arg(route.parts[0].length()));
    }
    // Taken over from the AI at speed, its speed kept; given back, the AI
    // drives on to the end.
    {
        Sim::SimulationCore core(inputOf(-1));
        std::vector<Sim::TrainEvent> all;
        core.step(30.0);
        const float aiSpeed = core.trains()[0].speed;
        const bool tookOver = core.drive(0);
        core.step(0.01);
        const float kept = core.trains()[0].speed;
        core.step(10.0);
        core.leave(0);
        for (int i = 0; i < 2000 && !core.finished(); ++i) {
            core.step(1.0);
            eventsOf(core, all);
        }
        eventsOf(core, all);
        test.check(aiSpeed > 5.0f && tookOver && std::fabs(kept - aiSpeed) < 0.05f && core.drivenTrain() == -1
                           && has(all, Sim::TrainEvent::DrivingLeft) && has(all, Sim::TrainEvent::Finished),
                   QString("taken over from the AI at %1 m/s (then %2), given back, run to its end").arg(aiSpeed).arg(kept));
    }
    // Taking over a running AI train makes the run's step 0.01 s; leaving,
    // its own again.
    {
        Sim::SimulationCore core(inputOf(-1));
        core.step(10.0);
        const double before = core.stepLength();
        core.drive(0);
        const double driving = core.stepLength();
        core.step(1.0);
        core.leave(0);
        test.check(before == 0.1 && driving == 0.01 && core.stepLength() == 0.1,
                   QString("the run's step: %1 s, driven %2 s, left %3 s").arg(before).arg(driving).arg(core.stepLength()));
    }
    // Run into another train: the run ends, told. An AI train ahead runs to
    // its path's end and stays there; the driven one follows at full
    // throttle on the same track: its authority ends at the signal before
    // the other's block, which it passes at danger, told.
    {
        // The line cut into sections by home signals at 500, 1000 and 1500 m.
        QTemporaryDir folder;
        writeUtf16(folder.path() + "/sigcfg.dat", HomeSignals);
        const SigCfg signalConfig(folder.path() + "/sigcfg.dat");
        TestTrack::Layout signalled;
        signalled.end(1, 10);
        signalled.track(10, 1, 2, 200);
        signalled.end(2, 10);
        TestTrack::signalHead(signalled, 10, 801, 500.0f, true, "Home");
        TestTrack::signalHead(signalled, 10, 802, 1000.0f, true, "Home");
        TestTrack::signalHead(signalled, 10, 803, 1500.0f, true, "Home");
        TDB *db = &signalled.db;
        Game::trackDB = db;
        const auto signalledGeometry = std::make_shared<const Sim::TrackGeometry>(db);
        const PathTrack::Result signalledRun = PathTrack::startAt(db, {10, 100.0f});
        const Sim::TrainRoute signalledRoute =
                Sim::BuildRoute(db, signalledRun.nodes, PathTrack::legs(db, signalledRun.nodes), 31.0);
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->geometry = signalledGeometry;
        made->network = std::make_shared<const Sim::TrackNetwork>(db, *signalledGeometry, &signalConfig, false);
        made->step = 0.01;
        Sim::TrainPlan ahead = planOf();
        ahead.route = signalledRoute;
        ahead.startFront = signalledRoute.pathStart + 400.0;
        ahead.staysAtEnd = true;
        Sim::TrainPlan behind = planOf();
        behind.route = signalledRoute;
        behind.startFront = signalledRoute.pathStart + 51.0;
        behind.startTime = t0 + 120;
        // Its route on the network's sections, as MakeInput projects it.
        for (Sim::TrainPlan *plan : {&ahead, &behind})
            for (const Sim::RouteTrack &part : signalledRoute.parts)
                plan->projection.push_back(made->network->project(part));
        ahead.id = 0;
        behind.id = 1;
        made->trains = {ahead, behind};
        made->driven = 1;
        Sim::SimulationCore core(made);
        std::vector<Sim::TrainEvent> all;
        core.step(130.0);
        if (verbose) {
            int aspect = -1;
            double distance = -1.0;
            const int signal = core.nextSignalAhead(1, &aspect, &distance);
            qInfo() << "  next signal ahead of the driven train" << signal << "aspect" << aspect << "in" << distance
                    << "m; front" << core.trains()[1].front << "status" << int(core.trains()[1].status);
        }
        core.command(1, -1, {"throttle", 1.0});
        for (int i = 0; i < 3000 && !core.finished(); ++i) {
            core.step(0.1);
            eventsOf(core, all);
        }
        if (verbose) {
            for (const Sim::TrainEvent &e : all)
                qInfo() << "  event" << e.time << "train" << e.train << "kind" << int(e.kind);
            qInfo() << "  sections" << made->network->sections().size() << "heads" << made->network->heads().size()
                    << "holders" << core.holders();
            for (int t = 0; t < 2; ++t)
                qInfo() << "  train" << t << "status" << int(core.trains()[size_t(t)].status) << "front"
                        << core.trains()[size_t(t)].front << "speed" << core.trains()[size_t(t)].speed << "authority"
                        << core.trains()[size_t(t)].authority;
        }
        const Sim::SimulationCore::Collision &crash = core.collision();
        test.check(core.finished() && crash.time > 0.0 && crash.train == 1 && crash.other == 0 && !crash.standing
                           && crash.speed > 1.0 && has(all, Sim::TrainEvent::Collided)
                           && has(all, Sim::TrainEvent::PassedAuthority) && has(all, Sim::TrainEvent::PassedAtDanger),
                   QString("run into the train ahead at %1 m/s: the run ends, told").arg(crash.speed));
        Game::trackDB = line;
    }
    // A driven run repeats exactly.
    {
        const auto runOnce = [&] {
            Sim::SimulationCore core(inputOf(0));
            core.step(0.05);
            core.command(0, -1, {"throttle", 0.7});
            core.step(30.0);
            core.command(0, -1, {"trainBrake", 0.5});
            core.step(10.0);
            return std::make_pair(core.trains()[0].front, core.trains()[0].speed);
        };
        test.check(runOnce() == runOnce(), "a driven run repeats exactly");
    }

    // The brake pipe alone: air moves along it and none is lost; a vented
    // end pulls the rest down in order; any step is stable; at rest it
    // evens out.
    {
        Sim::BrakePipe pipe;
        for (int i = 0; i < 50; ++i) {
            pipe.volume.push_back(i % 2 ? 0.012 : 0.016);
            pipe.pressure.push_back(90.0 * Psi);
        }
        pipe.pressure[0] = 0.0;
        const auto air = [&] {
            double sum = 0.0;
            for (size_t i = 0; i < pipe.volume.size(); ++i)
                sum += pipe.volume[i] * pipe.pressure[i];
            return sum;
        };
        const double before = air();
        for (int i = 0; i < 20; ++i)
            pipe.step(0.01);
        bool ordered = pipe.pressure[1] > pipe.pressure[0];
        for (size_t i = 1; i < pipe.pressure.size(); ++i)
            ordered = ordered && pipe.pressure[i] >= pipe.pressure[i - 1];
        const double spreadAt = pipe.pressure[49] - pipe.pressure[0];
        Sim::BrakePipe coarse = pipe;
        for (int i = 0; i < 10; ++i)
            coarse.step(1.0);
        bool bounded = true;
        for (double p : coarse.pressure)
            bounded = bounded && p >= 0.0 && p <= 90.0 * Psi;
        for (int i = 0; i < 12000; ++i)
            pipe.step(0.01);
        const double even = (pipe.pressure[49] - pipe.pressure[0]) / Psi;
        test.check(equal(air(), before, 1e-9) && ordered && spreadAt > 0.0 && bounded && std::fabs(even) < 1e-6
                           && equal(pipe.pressure[25], before / (25 * 0.028), 1e-6),
                   QString("brake pipe: air kept, a vented end pulls it down in order, stable at 1 s, even at rest "
                           "(%1 psi apart)").arg(even));
    }
    const auto valueOf = [](Sim::SimulationCore &core, int train, int v, const char *name) {
        const Sim::VehicleOutputs *out = core.outputsOf(train, v);
        return out ? Sim::Message{QString(), out->values.value(name, -1.0)}.number() : -1.0;
    };
    // Fifty cars on the pipe, standing: half the full service drop applies
    // every cylinder to the drop × the triple valve ratio, the last car
    // after the first; released, the pipe charges again and every cylinder
    // empties.
    {
        std::vector<Sim::Vehicle> cars{engine};
        for (int i = 0; i < 49; ++i)
            cars.push_back(wagon);
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->geometry = geometry;
        made->trains.push_back(planOf(cars));
        made->step = 0.01;
        made->driven = 0;
        Sim::SimulationCore core(made);
        core.step(0.05);
        const bool charged = equal(valueOf(core, 0, 49, "brakePipe"), 90.0 * Psi, 1e-9)
                && valueOf(core, 0, 49, "brakeCylinder") == 0.0;
        core.command(0, -1, {"trainBrake", 0.5});
        double firstAt = -1.0, lastAt = -1.0;
        for (int i = 0; i < 6000; ++i) {
            core.step(0.01);
            if (firstAt < 0.0 && valueOf(core, 0, 1, "brakeCylinder") > Psi)
                firstAt = i * 0.01;
            if (lastAt < 0.0 && valueOf(core, 0, 49, "brakeCylinder") > Psi)
                lastAt = i * 0.01;
        }
        // 90 - 13 psi: the reservoir follows the pipe down, its air into a
        // cylinder 1 / 2.5 of its volume.
        double lowest = 1e9, highest = 0.0;
        for (int v = 0; v < 50; ++v) {
            const double c = valueOf(core, 0, v, "brakeCylinder") / Psi;
            lowest = std::min(lowest, c);
            highest = std::max(highest, c);
        }
        core.command(0, -1, {"trainBrake", 0.0});
        double releasedAt = -1.0, chargedAt = -1.0;
        for (int i = 0; i < 150000 && chargedAt < 0.0; ++i) {
            core.step(0.01);
            if (releasedAt < 0.0 && valueOf(core, 0, 49, "brakeCylinder") == 0.0)
                releasedAt = i * 0.01;
            if (chargedAt < 0.0 && valueOf(core, 0, 49, "brakePipe") > 89.5 * Psi)
                chargedAt = i * 0.01;
        }
        test.check(charged && firstAt >= 0.0 && lastAt > firstAt && lowest > 30.5 && highest < 34.5 && releasedAt > 0.0
                           && chargedAt > releasedAt,
                   QString("fifty cars: half service, cylinders %1 to %2 psi (32.5 expected), the first applying at "
                           "%3 s, the last at %4 s; released, the last empty at %5 s, its pipe charged at %6 s")
                           .arg(lowest, 0, 'f', 1).arg(highest, 0, 'f', 1).arg(firstAt).arg(lastAt).arg(releasedAt)
                           .arg(chargedAt));
    }
    // Stopping from 20 m/s in an emergency against the brake force's sum
    // integrated alone: each cylinder filling at 20 psi/s to 50 psi (the
    // full force), Open Rails' brake shoe friction from the speed.
    {
        Sim::SimulationCore core(inputOf(0));
        core.step(0.05);
        core.command(0, -1, {"throttle", 1.0});
        while (core.trains()[0].speed < 20.0f)
            core.step(0.01);
        core.command(0, -1, {"throttle", 0.0});
        core.command(0, -1, {"emergency", true});
        const double from = core.trains()[0].front, speed = core.trains()[0].speed;
        for (int i = 0; i < 10000 && core.trains()[0].speed > 0.0f; ++i)
            core.step(0.01);
        const double stopped = core.trains()[0].front - from;
        double v = speed, x = 0.0, t = 0.0;
        while (v > 0.0) {
            const double share = std::min(t * 20.0 / 50.0, 1.0);
            const double friction = std::clamp((7.6 / (v * 3.6 + 17.5) + 0.07) / 0.2, 0.01, 1.0);
            v -= 0.001 * 160000.0 * share * friction / 160000.0;
            x += 0.001 * std::max(v, 0.0);
            t += 0.001;
        }
        test.check(equal(stopped, x, 0.03),
                   QString("emergency stop from %1 m/s: %2 m (expected %3 m)").arg(speed).arg(stopped).arg(x));
    }
    // The control line: the cab's throttle and direction reach a second
    // engine, which pulls too; the line's voltage reaches the engines.
    {
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->geometry = geometry;
        made->trains.push_back(planOf({engine, engine, wagon}));
        made->step = 0.01;
        made->driven = 0;
        made->lineVoltage = 15000.0;
        Sim::SimulationCore core(made);
        core.step(0.05);
        core.command(0, -1, {"throttle", 0.6});
        core.step(1.0);
        test.check(core.cabOf(0) == 0 && valueOf(core, 0, 1, "throttle") == 0.6 && valueOf(core, 0, 1, "direction") == 1.0
                           && valueOf(core, 0, 1, "force") > 0.0 && valueOf(core, 0, 0, "lineVoltage") == 15000.0,
                   QString("control line: the second engine at throttle %1, direction %2, %3 N; line %4 V")
                           .arg(valueOf(core, 0, 1, "throttle")).arg(valueOf(core, 0, 1, "direction"))
                           .arg(valueOf(core, 0, 1, "force")).arg(valueOf(core, 0, 0, "lineVoltage")));
    }
    // EP brakes: the cab's brake on the EP wire fills every cylinder to its
    // share of the full pressure at once along the train.
    {
        const Sim::Vehicle epEngine = vehicle("ep.eng"), epWagon = vehicle("ep.wag");
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->geometry = geometry;
        made->trains.push_back(planOf({epEngine, epWagon, epWagon}));
        made->step = 0.01;
        made->driven = 0;
        Sim::SimulationCore core(made);
        core.step(0.05);
        core.command(0, -1, {"trainBrake", 0.5});
        core.step(2.0);
        const double last = valueOf(core, 0, 2, "brakeCylinder") / Psi;
        core.command(0, -1, {"trainBrake", 0.0});
        core.step(5.0);
        test.check(equal(last, 35.0, 0.01) && valueOf(core, 0, 2, "brakeCylinder") == 0.0,
                   QString("EP: half the brake, the last car at %1 psi in 2 s (35 expected), released after")
                           .arg(last));
    }
    return test.finish();
}
