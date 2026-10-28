#include <tsre/tests/DrivingTestSuite.h>

#include <QElapsedTimer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <QString>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/sim/AirBrake.h>
#include <tsre/sim/LocoScript.h>
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

// An electric loco with most of what Open Rails reads: two pantographs
// raising in 3 s, a manual breaker closing in 2 s, power 1 s after it and
// auxiliary power 2 s after it, a notched throttle, a train brake with
// notches, engine and dynamic brakes, a main reservoir and compressor.
QString electricFile() {
    return engineFile(AirBrakes)
            .replace("MaxBrakeForce ( 80kN )", "MaxBrakeForce ( 80kN )\n\tORTSPantographs (\n"
                     "\t\tPantograph ( Delay ( 3s ) )\n\t\tPantograph ( Delay ( 3s ) )\n\t)")
            .replace("\tMaxVelocity ( 144kmh )", R"(	MaxVelocity ( 144kmh )
	MaxContinuousForce ( 200kN )
	MaxCurrent ( 1000A )
	ORTSCircuitBreaker ( Manual )
	ORTSCircuitBreakerClosingDelay ( 2s )
	ORTSPowerOnDelay ( 1s )
	ORTSAuxPowerOnDelay ( 2s )
	AirBrakesMainResVolume ( 20 )
	AirBrakesMainMaxAirPressure ( 140 )
	AirBrakesCompressorRestartPressure ( 125 )
	TrainBrakesControllerMaxApplicationRate ( 5 )
	TrainBrakesControllerMaxReleaseRate ( 10 )
	DynamicBrakesMaximumForce ( 150kN )
	DynamicBrakesMinUsableSpeed ( 5 )
	DynamicBrakesFadingSpeed ( 20 )
	DynamicBrakesMaximumEffectiveSpeed ( 80 )
	DynamicBrakesDelayTimeBeforeEngaging ( 1 )
	EngineControllers (
		Throttle ( 0 1 0.125 0
			NumNotches ( 9 Notch ( 0 0 Dummy ) Notch ( 0.125 0 Dummy ) Notch ( 0.25 0 Dummy )
				Notch ( 0.375 0 Dummy ) Notch ( 0.5 0 Dummy ) Notch ( 0.625 0 Dummy ) Notch ( 0.75 0 Dummy )
				Notch ( 0.875 0 Dummy ) Notch ( 1 0 Dummy ) )
		)
		Brake_Train ( 0 1 0.05 0
			NumNotches ( 5
				Notch ( 0 0 TrainBrakesControllerReleaseStart )
				Notch ( 0.2 0 TrainBrakesControllerRunningStart )
				Notch ( 0.4 1 TrainBrakesControllerGraduatedSelfLapLimitedStart )
				Notch ( 0.9 0 TrainBrakesControllerFullServiceStart )
				Notch ( 1 0 TrainBrakesControllerEmergencyStart )
			)
		)
		Brake_Engine ( 0 1 0.1 0 NumNotches ( 0 ) )
		Brake_Dynamic ( 0 1 0.1 0 NumNotches ( 0 ) )
	))");
}

// A loco script for the host's checks: its file's values, commands, sends,
// the control line, an error and a loop on demand.
const char *HostScript = R"(
function init(state, vehicle)
    state.maxForce = vehicle.engine.maxforce
    state.mass = vehicle.number("wagon(mass")
    state.maxSpeed = vehicle.engine.maxvelocity
    state.brakes = vehicle.text("wagon(brakesystemtype")
    state.missing = vehicle.number("engine(nosuchkey", 1, -1)
    state.throttle = 0
    state.steps = 0
end
function command(name, value, state, from)
    if name == "throttle" then
        state.throttle = value
        if from ~= "line" then line("throttle", value) end
    elseif name == "fail" then state.fail = true
    elseif name == "spin" then state.spin = true
    else send("echo:" .. name, value) end
end
function update(dt, inputs, state, out)
    if state.fail then error("asked to fail") end
    if state.spin then while true do end end
    state.steps = state.steps + 1
    out.motiveForce = state.throttle * state.maxForce
    out.values.maxForce = state.maxForce
    out.values.mass = state.mass
    out.values.maxSpeed = state.maxSpeed
    out.values.brakes = state.brakes
    out.values.missing = state.missing
    out.values.steps = state.steps
    out.values.lineVoltage = inputs.lineVoltage
end
)";

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
    // Loco scripts, the host's side: a script reads its file in SI, takes
    // commands, sends events and line messages, publishes values; an error
    // or an endless loop stops only its locos' force, told once; its state
    // saved and restored into a new loco steps the same.
    {
        QTemporaryDir scripts;
        QFile file(scripts.path() + "/electric.lua");
        const bool written = file.open(QIODevice::WriteOnly) && file.write(HostScript) > 0;
        file.close();
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->geometry = geometry;
        made->trains.push_back(planOf({engine, engine, wagon}));
        made->step = 0.01;
        made->driven = 0;
        made->lineVoltage = 3000.0;
        made->locoScripts = scripts.path();
        Sim::SimulationCore core(made);
        core.step(0.05);
        core.command(0, -1, {"throttle", 0.5});
        core.command(0, -1, {"CAB_AMBIENT_LIGHT", QString("disable")});
        core.step(0.02);
        std::vector<std::pair<int, Sim::Message>> sent = core.takeMessages(0);
        const bool echoed = std::any_of(sent.begin(), sent.end(), [](const auto &m) {
            return m.first == 0 && m.second.name == "echo:CAB_AMBIENT_LIGHT" && m.second.text() == "disable";
        });
        const bool read = valueOf(core, 0, 0, "maxForce") == 300000.0 && valueOf(core, 0, 0, "mass") == 80000.0
                && equal(valueOf(core, 0, 0, "maxSpeed"), 40.0, 1e-9) && valueOf(core, 0, 0, "missing") == -1.0
                && core.outputsOf(0, 0)->values.value("brakes") == Sim::MessageValue(QString("Air_single_pipe"));
        test.check(written && read && echoed && valueOf(core, 0, 1, "steps") > 0.0
                           && valueOf(core, 0, 0, "lineVoltage") == 3000.0
                           && core.outputsOf(0, 1) && valueOf(core, 0, 1, "maxForce") == 300000.0,
                   QString("loco script: its file in SI (%1 N, %2 kg, %3 m/s), a message of its own echoed, the line "
                           "voltage %4 V")
                           .arg(valueOf(core, 0, 0, "maxForce")).arg(valueOf(core, 0, 0, "mass"))
                           .arg(valueOf(core, 0, 0, "maxSpeed")).arg(valueOf(core, 0, 0, "lineVoltage")));
        core.step(2.0);
        // Both engines at half throttle (the second from the line, once).
        const double speedBefore = core.trains()[0].speed;
        test.check(valueOf(core, 0, 1, "steps") > 100.0 && speedBefore > 0.0
                           && std::fabs(speedBefore - 2.0 * 2.0 * 150000.0 / 200000.0) < 0.2,
                   QString("loco script: both engines pull at the cab's throttle: %1 m/s after 2 s").arg(speedBefore));
        // Saved and restored into a new loco: the same state.
        QByteArray saved;
        Sim::LocoScripts other(scripts.path());
        std::unique_ptr<Sim::VehicleModel> copy = other.make(engine.definition);
        Sim::LocoScripts host(scripts.path());
        std::unique_ptr<Sim::VehicleModel> original = host.make(engine.definition);
        original->command({"throttle", 0.25});
        Sim::VehicleInputs in;
        in.dt = 0.01;
        Sim::VehicleOutputs a, b;
        for (int i = 0; i < 10; ++i)
            original->step(in, a);
        const bool savedOk = original->save(saved) && copy->restore(saved);
        original->step(in, a);
        copy->step(in, b);
        test.check(savedOk && a.motiveForce == 75000.0 && b.motiveForce == a.motiveForce
                           && a.values.value("steps") == b.values.value("steps")
                           && a.values.value("steps") == Sim::MessageValue(11.0),
                   QString("loco script: its state saved (%1 bytes) and restored, a new loco steps the same")
                           .arg(saved.size()));
        // An error stops its locos' force, told once; the run goes on.
        core.command(0, -1, {"fail", true});
        core.step(0.05);
        sent = core.takeMessages(0);
        const auto errors = std::count_if(sent.begin(), sent.end(),
                                          [](const auto &m) { return m.second.name == "scriptError"; });
        core.step(1.0);
        test.check(errors >= 1 && core.outputsOf(0, 0)->motiveForce == 0.0 && core.outputsOf(0, 1)->motiveForce == 0.0
                           && core.trains()[0].speed > 0.0f && !core.finished(),
                   QString("loco script: an error stops its locos' force, told (%1 times, once each), the run going on")
                           .arg(errors));
        // An endless loop: stopped at its call's time limit.
        Sim::LocoScripts spinning(scripts.path());
        std::unique_ptr<Sim::VehicleModel> spinner = spinning.make(engine.definition);
        spinner->command({"spin", true});
        Sim::VehicleOutputs spun;
        QElapsedTimer timer;
        timer.start();
        spinner->step(in, spun);
        const qint64 took = timer.elapsed();
        spinner->step(in, spun);
        test.check(took < 2000 && spun.motiveForce == 0.0, QString("loco script: an endless loop stopped after %1 ms").arg(took));
    }
    // The electric loco script (appdata/<version>/scripts/loco/electric.lua,
    // run from the checkout's folder).
    const QString locoFolder = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/scripts/loco");
    writeUtf16(files.path() + "/electric.eng", electricFile());
    const Sim::Vehicle electric = vehicle("electric.eng");
    const bool scriptThere = QFileInfo::exists(locoFolder + "/electric.lua");
    test.check(scriptThere, "electric.lua is in " + locoFolder);
    // Its force against Open Rails' formulas: throttle × MaxForce, at most
    // MaxPower × throttle² / v, fading over the last 0.05 m/s to
    // MaxVelocity; the motors' heat holding it to MaxContinuousForce in the
    // long run.
    if (scriptThere) {
        Sim::LocoScripts lua(locoFolder);
        const auto forceAt = [&](double throttle, double speed, int steps = 2) {
            std::unique_ptr<Sim::VehicleModel> m = lua.make(electric.definition);
            Sim::VehicleInputs in;
            in.dt = 0.01;
            in.speed = speed;
            in.cab = true;
            in.brakePipe = 90.0 * Psi;
            in.brakePipeVolume = 0.0169;
            Sim::VehicleOutputs out;
            m->step(in, out);
            m->command({"direction", 1.0});
            m->command({"throttle", throttle});
            for (int i = 0; i < steps; ++i)
                m->step(in, out);
            return out.motiveForce;
        };
        const double a = forceAt(0.5, 10.0), b = forceAt(1.0, 30.0), c = forceAt(1.0, 39.98), d = forceAt(1.0, 41.0),
                     e = forceAt(0.25, 1.0);
        const double heat = forceAt(1.0, 1.0, 360000);
        test.check(equal(a, 125000.0, 1e-9) && equal(b, 5e6 / 30.0, 1e-9) && equal(c, 120000.0, 1e-6) && d == 0.0
                           && equal(e, 75000.0, 1e-9) && equal(heat, 200000.0, 0.01),
                   QString("electric.lua force: %1 N (125000), %2 (166667), %3 (120000), %4 (0), %5 (75000); "
                           "after an hour at full force %6 N (MaxContinuousForce 200000)")
                           .arg(a).arg(b).arg(c).arg(d).arg(e).arg(heat));
        // The dynamic brake: engaged after its delay, the force from its
        // curve (flat 20 to 80 mph, rising from 5 to 20 mph).
        std::unique_ptr<Sim::VehicleModel> m = lua.make(electric.definition);
        Sim::VehicleInputs in;
        in.dt = 0.01;
        in.speed = 20.0;
        in.cab = true;
        in.brakePipe = 90.0 * Psi;
        in.brakePipeVolume = 0.0169;
        Sim::VehicleOutputs out;
        m->step(in, out);
        m->command({"direction", 1.0});
        m->command({"dynamicBrake", 1.0});
        double early = 0.0;
        for (int i = 0; i < 150; ++i) {
            m->step(in, out);
            if (i == 50)
                early = out.motiveForce;
        }
        const double flat = out.motiveForce;
        m->command({"throttle", 0.5});
        m->step(in, out);
        const double throttleRefused = Sim::Message{QString(), out.values.value("throttle")}.number();
        in.speed = 6.0;
        m->step(in, out);
        const double slow = out.motiveForce;
        const double expectedSlow = -150000.0 * (6.0 - 5 * 0.44704) / (20 * 0.44704 - 5 * 0.44704);
        test.check(early == 0.0 && equal(flat, -150000.0, 1e-9) && equal(slow, expectedSlow, 1e-6)
                           && throttleRefused == 0.0,
                   QString("electric.lua dynamic brake: none in its first second, %1 N at 20 m/s, %2 N at 6 m/s "
                           "(%3); no throttle with it on").arg(flat).arg(slow).arg(expectedSlow));
        // Saved and restored into a new loco: the same steps.
        std::unique_ptr<Sim::VehicleModel> copy = lua.make(electric.definition);
        QByteArray saved;
        const bool savedOk = m->save(saved) && copy->restore(saved);
        Sim::VehicleOutputs one, two;
        for (int i = 0; i < 100; ++i) {
            m->step(in, one);
            copy->step(in, two);
        }
        test.check(savedOk && one.motiveForce == two.motiveForce && one.values == two.values,
                   QString("electric.lua: its state saved (%1 bytes) and restored, the same steps after")
                           .arg(saved.size()));
    }
    // Driven: the hot start's timings (a pantograph raising in 3 s, then
    // the manual breaker closing in 2 s, power 1 s and auxiliary power 2 s
    // later); the train brake's notches; the engine brake; the compressor;
    // a pantograph lowered cutting the power at once.
    if (scriptThere) {
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->geometry = geometry;
        made->trains.push_back(planOf({electric, wagon, wagon}));
        made->step = 0.01;
        made->driven = 0;
        made->lineVoltage = 25000.0;
        made->locoScripts = locoFolder;
        Sim::SimulationCore core(made);
        core.step(0.01);
        core.command(0, -1, {"throttle", 1.0});
        const auto text = [&](const char *name) {
            const Sim::VehicleOutputs *o = core.outputsOf(0, 0);
            return o ? Sim::Message{QString(), o->values.value(name, QString())}.text() : QString();
        };
        double upAt = -1, closingAt = -1, closedAt = -1, powerAt = -1, auxAt = -1, movedAt = -1;
        for (int i = 1; i <= 1000; ++i) {
            core.step(0.01);
            const double at = i * 0.01;
            if (upAt < 0 && text("pantograph1State") == "up")
                upAt = at;
            if (closingAt < 0 && text("circuitBreaker") == "closing")
                closingAt = at;
            if (closedAt < 0 && text("circuitBreaker") == "closed")
                closedAt = at;
            if (powerAt < 0 && text("mainPower") == "true")
                powerAt = at;
            if (auxAt < 0 && text("auxPower") == "true")
                auxAt = at;
            if (movedAt < 0 && core.trains()[0].speed > 0.0f)
                movedAt = at;
        }
        const double volts = valueOf(core, 0, 0, "lineVoltage");
        test.check(std::fabs(upAt - 3.0) < 0.025 && std::fabs(closingAt - upAt - 0.01) < 0.015
                           && std::fabs(closedAt - closingAt - 2.01) < 0.015 && std::fabs(powerAt - closedAt - 1.0) < 0.015
                           && std::fabs(auxAt - closedAt - 2.0) < 0.015 && std::fabs(movedAt - powerAt) < 0.025
                           && std::fabs(volts - 25000.0) < 1.0,
                   QString("electric.lua hot start: pantograph up at %1 s, breaker closing %2, closed %3, power %4, "
                           "auxiliary %5, moving %6; line %7 V")
                           .arg(upAt).arg(closingAt).arg(closedAt).arg(powerAt).arg(auxAt).arg(movedAt).arg(volts));
        // The train brake: full service brings the equalising reservoir
        // and the pipe 26 psi down; the graduated notch half way between
        // its limits, 74 psi; release back to 90; the emergency notch vents
        // the pipe.
        core.command(0, -1, {"throttle", 0.0});
        core.command(0, -1, {"trainBrake", 0.9});
        core.step(15.0);
        const double fullService = valueOf(core, 0, 0, "equalisingReservoir") / Psi;
        const double pipeAtLoco = valueOf(core, 0, 0, "brakePipe") / Psi;
        const QString notch = text("trainBrakeNotch");
        core.command(0, -1, {"trainBrake", 0.0});
        core.step(20.0);
        core.command(0, -1, {"trainBrake", 0.65});
        core.step(20.0);
        const double graduated = valueOf(core, 0, 0, "equalisingReservoir") / Psi;
        core.command(0, -1, {"trainBrake", 0.0});
        core.step(20.0);
        const double released = valueOf(core, 0, 0, "equalisingReservoir") / Psi;
        test.check(notch == "FullServ" && std::fabs(fullService - 64.0) < 0.05 && std::fabs(pipeAtLoco - 64.0) < 0.5
                           && std::fabs(graduated - 74.0) < 0.05 && std::fabs(released - 90.0) < 0.05,
                   QString("electric.lua train brake: full service %1 psi (64), the pipe %2; graduated %3 (74); "
                           "released %4 (90)").arg(fullService).arg(pipeAtLoco).arg(graduated).arg(released));
        // The compressor: the main reservoir drawn below its restart
        // pressure by the brakes' release, filled again to its maximum.
        bool compressed = false;
        double lowest = 1e9;
        for (int i = 0; i < 30000; ++i) {
            core.step(0.01);
            lowest = std::min(lowest, valueOf(core, 0, 0, "mainReservoir") / Psi);
            compressed = compressed || text("compressor") == "true";
            if (compressed && text("compressor") == "false")
                break;
        }
        const double filled = valueOf(core, 0, 0, "mainReservoir") / Psi;
        test.check(lowest < 125.0 && compressed && filled >= 140.0 && filled < 140.1,
                   QString("electric.lua compressor: the main reservoir down to %1 psi, filled again to %2 (140)")
                           .arg(lowest).arg(filled));
        core.command(0, -1, {"emergency", true});
        core.step(1.0);
        const double vented = valueOf(core, 0, 0, "brakePipe") / Psi;
        core.command(0, -1, {"emergency", false});
        // The engine brake: the loco's cylinder to 64 psi at 12.5 psi/s.
        core.step(30.0);
        core.command(0, -1, {"engineBrake", 1.0});
        core.step(2.0);
        const double rising = valueOf(core, 0, 0, "brakeCylinder") / Psi;
        core.step(6.0);
        const double engineFull = valueOf(core, 0, 0, "brakeCylinder") / Psi;
        const double wagonCylinder = valueOf(core, 0, 1, "brakeCylinder") / Psi;
        test.check(vented < 5.0 && std::fabs(rising - 25.0) < 1.0 && std::fabs(engineFull - 64.0) < 0.1
                           && wagonCylinder < 0.1,
                   QString("electric.lua: emergency vents the pipe to %1 psi in 1 s; the engine brake %2 psi after "
                           "2 s, %3 (64) after 8, the wagons' %4")
                           .arg(vented).arg(rising).arg(engineFull).arg(wagonCylinder));
        // A pantograph lowered: the breaker opens, power off at once, told
        // by its sounds.
        core.command(0, -1, {"engineBrake", 0.0});
        core.step(10.0);
        core.takeMessages(0);
        core.command(0, -1, {"pantograph1", QString("down")});
        core.step(0.01);
        const std::vector<std::pair<int, Sim::Message>> sent = core.takeMessages(0);
        QStringList sounds;
        for (const auto &[v, m] : sent)
            if (m.name == "sound")
                sounds << m.text();
        test.check(text("mainPower") == "false" && text("circuitBreaker") == "open" && sounds.join(' ') == "46 47 150 24 228",
                   QString("electric.lua: a pantograph lowered, power off at once; sounds %1").arg(sounds.join(' ')));
    }
    // Two electric locos, the second turned: the cab's throttle and
    // direction reach it on the control line, the direction turned, and
    // both pull the train forward.
    if (scriptThere) {
        Sim::Vehicle turned = vehicle("engine.eng");
        turned.flip = true;
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->geometry = geometry;
        made->trains.push_back(planOf({engine, turned, wagon}));
        made->step = 0.01;
        made->driven = 0;
        made->locoScripts = locoFolder;
        Sim::SimulationCore core(made);
        core.step(0.05);
        core.command(0, -1, {"throttle", 0.5});
        core.step(2.0);
        const double speed = core.trains()[0].speed;
        // 150 kN each on 200 t for about 1.95 s (power came on in a step or
        // two).
        test.check(valueOf(core, 0, 1, "direction") == -1.0 && valueOf(core, 0, 1, "throttle") == 0.5
                           && valueOf(core, 0, 1, "force") == -150000.0 && speed > 2.9 && speed < 3.0,
                   QString("electric.lua multiple unit: the turned loco at direction %1, throttle %2, pulling %3 N "
                           "(in its own frame, toward its back); "
                           "%4 m/s after 2 s")
                           .arg(valueOf(core, 0, 1, "direction")).arg(valueOf(core, 0, 1, "throttle"))
                           .arg(valueOf(core, 0, 1, "force")).arg(speed));
    }
    // The cost of a scripted loco a step.
    if (scriptThere) {
        Sim::LocoScripts lua(locoFolder);
        std::unique_ptr<Sim::VehicleModel> m = lua.make(electric.definition);
        Sim::VehicleInputs in;
        in.dt = 0.01;
        in.speed = 10.0;
        in.cab = true;
        in.brakePipe = 90.0 * Psi;
        in.brakePipeVolume = 0.0169;
        Sim::VehicleOutputs out;
        m->step(in, out);
        m->command({"direction", 1.0});
        m->command({"throttle", 0.5});
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 20000; ++i)
            m->step(in, out);
        const double us = timer.nsecsElapsed() / 1000.0 / 20000.0;
        test.check(us < 1000.0, QString("electric.lua: %1 µs a step").arg(us, 0, 'f', 1));
    }
    return test.finish();
}

int TsreTests::runDrivingCorpusSuite(bool verbose) {
    TokenTest::Suite test{"[tests:driving-corpus]", verbose};
    const QString root = QDir::cleanPath(Game::root);
    const QString locoFolder = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/scripts/loco");
    QStringList files;
    QDirIterator it(root + "/TRAINS/TRAINSET", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (QFileInfo(path).suffix().compare("eng", Qt::CaseInsensitive) == 0 && !path.contains("/OPENRAILS/"))
            files << path;
    }
    files.sort();
    Sim::LocoScripts lua(locoFolder);
    int electrics = 0, failed = 0, unpowered = 0, forceless = 0;
    QStringList listed;
    QElapsedTimer timer;
    timer.start();
    for (const QString &path : files) {
        const QFileInfo info(path);
        const std::shared_ptr<const VehicleDefinition> def =
                VehicleDefinition::read(info.path(), info.fileName(), root, true);
        if (!def || !def->ok() || def->wagonTypeId != 4)
            continue;
        ++electrics;
        std::unique_ptr<Sim::VehicleModel> model = lua.make(def);
        Sim::VehicleInputs in;
        in.dt = 0.01;
        in.cab = true;
        in.lineVoltage = 25000.0;
        in.brakePipe = 90.0 * 6894.757293168;
        in.brakePipeVolume = 0.0169;
        Sim::VehicleOutputs out;
        QString problem;
        const auto run = [&](int steps) {
            for (int i = 0; i < steps; ++i) {
                out.events.clear();
                model->step(in, out);
                for (const Sim::Message &m : out.events)
                    if (m.name == "scriptError" && problem.isEmpty())
                        problem = m.text();
            }
        };
        // Standing: its hot start, full throttle; then at 5 m/s (half its
        // top speed where lower).
        run(1);
        model->command({"direction", 1.0});
        model->command({"throttle", 1.0});
        // Powered within two minutes (some breakers close in 40 s).
        for (int waited = 0; waited < 120 && out.values.value("mainPower") != Sim::MessageValue(true); ++waited)
            run(100);
        run(100);
        in.speed = def->maxSpeedMps > 0.0 ? std::min(5.0, 0.5 * def->maxSpeedMps) : 5.0;
        run(10);
        const bool powered = out.values.value("mainPower") == Sim::MessageValue(true);
        const QString name = QDir(root).relativeFilePath(path);
        // TSRE_VEHICLE_FILE=<part of a path>: that loco's values.
        const QString shown = qEnvironmentVariable("TSRE_VEHICLE_FILE");
        if (!shown.isEmpty() && name.contains(shown, Qt::CaseInsensitive)) {
            QStringList values;
            for (auto i = out.values.cbegin(); i != out.values.cend(); ++i)
                values << i.key() + " = " + Sim::Message{i.key(), i.value()}.text();
            values.sort();
            qInfo().noquote() << name << "\n  " + values.join("\n  ");
        }
        if (!problem.isEmpty()) {
            ++failed;
            listed << name + ": " + problem;
        } else if (!powered) {
            ++unpowered;
            listed << name + ": no power after 2 minutes";
        } else if (out.motiveForce <= 0.0 && def->forceN > 0.0) {
            ++forceless;
            listed << QString("%1: no force at %2 m/s (MaxForce %3 N, MaxPower %4 W)")
                              .arg(name).arg(in.speed).arg(def->forceN).arg(def->powerW);
        }
    }
    if (verbose)
        for (const QString &line : listed)
            qInfo().noquote() << "  " << line;
    test.check(failed == 0,
               QString("%1 electric engines of %2 run electric.lua in %3 s: %4 script errors, %5 without power, "
                       "%6 without force (with a MaxForce)")
                       .arg(electrics).arg(files.size()).arg(timer.elapsed() / 1000.0).arg(failed).arg(unpowered)
                       .arg(forceless));
    return test.finish();
}
