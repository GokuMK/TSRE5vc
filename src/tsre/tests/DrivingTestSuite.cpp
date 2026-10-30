#include <tsre/tests/DrivingTestSuite.h>

#include <QDate>
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
#include <functional>
#include <tsre/Game.h>
#include <tsre/sim/AirBrake.h>
#include <tsre/script/LuaApi.h>
#include <tsre/script/ScriptState.h>
#include <tsre/sim/LocoScript.h>
#include <tsre/sim/OutsideAir.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationFrame.h>
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
// Twin pipe distributors (the Acela family's).
const char *TwinPipeBrakes = R"(
	BrakeSystemType ( "Air_twin_pipe" )
	BrakeEquipmentType ( "Air_twin_pipe, Distributor" )
	MaxApplicationRate ( 20 )
	MaxReleaseRate ( 20 )
	MaxAuxilaryChargingRate ( 25 )
	TripleValveRatio ( 2.5 )
	BrakeCylinderPressureForMaxBrakeBrakeForce ( 50 )
	EmergencyResCapacity ( 14 ))";
// The same on a single pipe.
const char *SinglePipeDistributors = R"(
	BrakeSystemType ( "Air_single_pipe" )
	BrakeEquipmentType ( "Distributor" )
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

// A legacy diesel as MSTS's Dash 9 (the diesel spec's §3 example): no
// ORTSDieselEngines; idle 315 rpm, 900 at full power, 40 rpm/s; 3263 kW,
// 650.65 kN, 550 kN continuous, RunUpTimeToMaxForce 30 s; 10 and 60 gal/h;
// a mechanical compressor; `extra` keys after these.
QString dieselFile(const QString &extra = QString()) {
    return engineFile(AirBrakes)
            .replace("\tType ( Electric )\n\tMaxPower ( 5000kW )\n\tMaxForce ( 300kN )\n",
                     "\tType ( Diesel )\n\tMaxPower ( 3263kW )\n\tMaxForce ( 650.65kN )\n"
                     "\tMaxContinuousForce ( 550kN )\n\tDieselEngineIdleRPM ( 315 )\n\tDieselEngineMaxRPM ( 900 )\n"
                     "\tDieselEngineMaxRPMChangeRate ( 40 )\n\tRunUpTimeToMaxForce ( 30 )\n"
                     "\tDieselUsedPerHourAtMaxPower ( 60gal )\n\tDieselUsedPerHourAtIdle ( 10gal )\n"
                     "\tAirBrakesIsCompressorElectricOrMechanical ( 1 )\n"
                     + extra);
}

// A diesel model stepped on its own: its sounds ("n@seconds"), values and
// script error.
struct DieselRun {
    std::unique_ptr<Sim::VehicleModel> model;
    Sim::VehicleInputs in;
    Sim::VehicleOutputs out;
    double t = 0.0;
    QStringList sounds, events;
    QString problem;

    DieselRun(Sim::LocoScripts &scripts, const Sim::Vehicle &vehicle) : model(scripts.make(vehicle.definition)) {
        in.dt = 0.01;
        in.cab = true;
        in.brakePipe = 90.0 * 6894.757293168;
        in.brakePipeVolume = 0.0169;
    }
    void step(int steps = 1) {
        for (int i = 0; i < steps; ++i) {
            out.events.clear();
            model->step(in, out);
            t += in.dt;
            for (const Sim::Message &e : out.events) {
                if (e.name == "sound")
                    sounds << QString("%1@%2").arg(e.text()).arg(t, 0, 'f', 2);
                else if (e.name == "scriptError" && problem.isEmpty())
                    problem = e.text();
                else
                    events << e.name;
            }
        }
    }
    void command(const QString &name, const Sim::MessageValue &value) { model->command({name, value}); }
    double number(const char *name) const { return Sim::Message{QString(), out.values.value(name, 0.0)}.number(); }
    QString text(const char *name) const { return Sim::Message{QString(), out.values.value(name, QString())}.text(); }
    // Stepped until `done`, at most `limit` s: the seconds it took, -1 for
    // never.
    double until(const std::function<bool()> &done, double limit) {
        const double start = t;
        while (t - start < limit) {
            step();
            if (done())
                return t - start;
        }
        return -1.0;
    }
    // The sounds from `from` on, numbers only.
    QStringList soundsSince(double from) const {
        QStringList numbers;
        for (const QString &s : sounds)
            if (s.section('@', 1).toDouble() >= from - 1e-9)
                numbers << s.section('@', 0, 0);
        return numbers;
    }
};

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
    out.values.airTemperature = inputs.airTemperature
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
    // A car's exhaust from its values (task driving 05): by its kinds, in
    // their order; missing values off, shares clamped; the test state on.
    {
        const QHash<QString, Sim::MessageValue> values{{"fx.stack.flow", 2.5},
                                                       {"fx.stack.steam", 1.5},
                                                       {"fx.stack.beats", 6.0},
                                                       {"fx.exhaust.speed", -3.0},
                                                       {"fx.exhaust.soot", 0.4},
                                                       {"fx.exhaust.color", 4282400832.0}};
        const CarExhaust ex = Sim::carExhaust(values, {"exhaust", "stack", "whistle"});
        const CarExhaust test0 = testExhaust({"exhaust", "stack", "cylinders"});
        const auto off = [](const ExhaustOutput &o) {
            return o.flow == 0.0f && o.speed == 0.0f && o.temperature == 0.0f && o.steam == 0.0f && o.soot == 0.0f
                   && o.beats == 0.0f && o.color == 0;
        };
        test.check(ex.outputs.size() == 3 && ex.outputs[0].speed == 0.0f && equal(ex.outputs[0].soot, 0.4, 1e-6)
                           && ex.outputs[0].color == 4282400832u && ex.outputs[1].flow == 2.5f
                           && ex.outputs[1].steam == 1.0f && ex.outputs[1].beats == 6.0f && off(ex.outputs[2])
                           && test0.outputs.size() == 3 && test0.outputs[1].beats > 0.0f
                           && test0.outputs[2].flow > 0.0f && test0.outputs[2].steam == 1.0f,
                   "a car's exhaust from its values (fx.<kind>.<field>), and the test state");
        // Its cost on the main thread: a steam loco's five kinds, 60 other
        // values beside them (a driven train's each frame, a heard one's
        // each 0.1 s).
        QHash<QString, Sim::MessageValue> many;
        const QStringList kinds{"whistle", "safetyvalves", "stack", "drainpipe", "cylinders"};
        for (const QString &kind : kinds)
            for (const char *field : {"flow", "speed", "temperature", "steam", "soot", "beats"})
                many.insert(exhaustValueName(kind, QLatin1String(field)), 1.0);
        for (int i = 0; i < 60; ++i)
            many.insert(QString("value%1").arg(i), double(i));
        QElapsedTimer timer;
        timer.start();
        double sum = 0.0;
        for (int i = 0; i < 10000; ++i)
            sum += Sim::carExhaust(many, kinds).outputs[2].flow;
        const double us = timer.nsecsElapsed() / 1000.0 / 10000.0;
        test.check(sum == 10000.0, QString("a steam loco's exhaust from its values: %1 µs").arg(us, 0, 'f', 2));
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
    const auto valueOf2 = [](Sim::SimulationCore &core) {
        const Sim::VehicleOutputs *out = core.outputsOf(0, -1);
        return out ? Sim::Message{QString(), out->values.value("throttle", -1.0)}.number() : -1.0;
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
    // A command before the train is taken over (the run not yet stepped):
    // given to it then.
    {
        Sim::SimulationCore core(inputOf(0));
        core.command(0, -1, {"throttle", 1.0});
        core.step(1.0);
        test.check(core.trains()[0].speed > 0.5f && valueOf2(core) == 1.0,
                   QString("a command before the takeover kept for it: %1 m/s after 1 s").arg(core.trains()[0].speed));
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
    // A native module (task driving 04): the loco's definition from
    // inputs.vehicle, anything else refused.
    {
        QTemporaryDir scripts;
        QFile file(scripts.path() + "/electric.lua");
        const bool written = file.open(QIODevice::WriteOnly)
                && file.write("local probe = require 'test.probe'\n"
                              "function update(dt, inputs, state, out)\n"
                              "    out.values.mass = probe.mass(inputs.vehicle)\n"
                              "    out.values.refused = select(2, pcall(probe.mass, {}))\n"
                              "end\n") > 0;
        file.close();
        Sim::LocoScripts lua(scripts.path());
        lua.lua().addNativeModule("test.probe", [](lua_State *L) -> int {
            lua_newtable(L);
            lua_pushcfunction(L, [](lua_State *L) -> int {
                lua_pushnumber(L, Sim::LocoScripts::definitionAt(L, 1).massKg);
                return 1;
            });
            lua_setfield(L, -2, "mass");
            return 1;
        });
        std::unique_ptr<Sim::VehicleModel> m = lua.make(engine.definition);
        Sim::VehicleInputs in;
        in.dt = 0.01;
        Sim::VehicleOutputs out;
        m->step(in, out);
        const double mass = Sim::Message{QString(), out.values.value("mass", 0.0)}.number();
        const QString refused = Sim::Message{QString(), out.values.value("refused", QString())}.text();
        test.check(written && mass == 80000.0 && refused.contains("not a loco's inputs.vehicle"),
                   QString("a native module: the loco's definition from inputs.vehicle (%1 kg), else refused (%2)")
                           .arg(mass).arg(refused));
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
        // The outside air (task driving 06): the climate at 52° N on 15
        // January, the track at sea level.
        made->air.latitude = 52.0;
        made->air.date = QDate(2026, 1, 15);
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
                           && equal(valueOf(core, 0, 0, "airTemperature"), Sim::ClimateTemperature(52.0, 15.0, 8.0), 1e-3)
                           && core.outputsOf(0, 1) && valueOf(core, 0, 1, "maxForce") == 300000.0,
                   QString("loco script: its file in SI (%1 N, %2 kg, %3 m/s), a message of its own echoed, the line "
                           "voltage %4 V, the outside air %5 °C (%6)")
                           .arg(valueOf(core, 0, 0, "maxForce")).arg(valueOf(core, 0, 0, "mass"))
                           .arg(valueOf(core, 0, 0, "maxSpeed")).arg(valueOf(core, 0, 0, "lineVoltage"))
                           .arg(valueOf(core, 0, 0, "airTemperature")).arg(Sim::ClimateTemperature(52.0, 15.0, 8.0)));
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
    // The main reservoir pipe: twin pipe distributors keep their reservoirs
    // full from it, so after a reduction the brake pipe refills only itself
    // and the train releases in seconds; on a single pipe, the reservoirs
    // refilled through the brake pipe, in minutes.
    {
        writeUtf16(files.path() + "/twin.eng", engineFile(TwinPipeBrakes));
        writeUtf16(files.path() + "/twin.wag", wagonFile(TwinPipeBrakes));
        writeUtf16(files.path() + "/single.eng", engineFile(SinglePipeDistributors));
        writeUtf16(files.path() + "/single.wag", wagonFile(SinglePipeDistributors));
        const auto releaseOf = [&](const char *engineName, const char *wagonName, double *mainPipe) {
            std::vector<Sim::Vehicle> cars{vehicle(engineName)};
            for (int i = 0; i < 10; ++i)
                cars.push_back(vehicle(wagonName));
            auto made = std::make_shared<Sim::SimulationInput>();
            made->startTime = t0;
            made->geometry = geometry;
            made->trains.push_back(planOf(cars));
            made->step = 0.01;
            made->driven = 0;
            Sim::SimulationCore core(made);
            core.step(0.05);
            core.command(0, -1, {"trainBrake", 0.5});
            core.step(40.0);
            const double applied = valueOf(core, 0, 10, "brakeCylinder");
            core.command(0, -1, {"trainBrake", 0.0});
            double released = -1.0;
            for (int i = 0; i < 60000 && released < 0.0; ++i) {
                core.step(0.01);
                if (valueOf(core, 0, 10, "brakeCylinder") <= 0.0)
                    released = i * 0.01;
            }
            if (mainPipe != nullptr)
                *mainPipe = valueOf(core, 0, 10, "mainPipe");
            return std::make_pair(applied, released);
        };
        double mainPipe = 0.0;
        const auto [twinApplied, twinReleased] = releaseOf("twin.eng", "twin.wag", &mainPipe);
        const auto [singleApplied, singleReleased] = releaseOf("single.eng", "single.wag", nullptr);
        test.check(std::fabs(twinApplied / Psi - 32.5) < 1.0 && twinReleased > 0.0 && twinReleased < 20.0
                           && singleReleased > 3.0 * twinReleased && mainPipe > 120.0 * Psi,
                   QString("main reservoir pipe: twin pipe distributors at %1 psi release in %2 s (single pipe: "
                           "%3 psi, %4 s); the pipe at %5 psi")
                           .arg(twinApplied / Psi, 0, 'f', 1).arg(twinReleased).arg(singleApplied / Psi, 0, 'f', 1)
                           .arg(singleReleased).arg(mainPipe / Psi, 0, 'f', 1));
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
        // Its sounds (task sound 02): the loco's sound input from its
        // values, Open Rails' Variable1 (the throttle in %) and Variable2
        // (the pull in % of MaxForce), the host's tractive effort and power
        // (at the speed its step began with, a step's gain before this).
        {
            const Sim::VehicleOutputs *o = core.outputsOf(0, 0);
            const CarSoundInput sound = Sim::carSoundInput(o->values);
            const double pull = valueOf(core, 0, 0, "tractionForce") / 300000.0 * 100.0;
            const double power = o->motiveForce * core.trains()[0].speed;
            const float variable1 = sound.variable1.empty() ? -1.0f : sound.variable1[0];
            test.check(sound.variable1.size() == 1 && variable1 == 100.0f && std::fabs(sound.variable2 - pull) < 0.5 && sound.variable3 == 0.0f
                               && sound.tractiveEffort > 0.0f && std::fabs(sound.tractiveEffort - o->motiveForce) < 1.0
                               && std::fabs(sound.tractivePower - power) < 0.01 * power + 1.0 && sound.brakeCylinder == 0.0f,
                       QString("electric.lua's sounds: Variable1 %1 (100), Variable2 %2 (the pull, %3), tractive effort "
                               "%4 N (%5) and power %6 kW (%7) from the host")
                               .arg(variable1).arg(sound.variable2).arg(pull).arg(sound.tractiveEffort)
                               .arg(o->motiveForce).arg(sound.tractivePower / 1000.0).arg(power / 1000.0));
        }
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
        // A wagon's sounds hear its own brake cylinder, in psi.
        const double wagonHeldPsi = valueOf(core, 0, 1, "brakeCylinder") / Psi;
        const float wagonHeard = Sim::carSoundInput(core.outputsOf(0, 1)->values).brakeCylinder;
        test.check(wagonHeldPsi > 10.0 && std::fabs(wagonHeard - wagonHeldPsi) < 1e-3,
                   QString("a wagon's sounds hear its brake cylinder: %1 psi (%2)").arg(wagonHeard).arg(wagonHeldPsi));
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
    // The diesel loco script (appdata/<version>/scripts/loco/diesel.lua,
    // task driving 03) against the diesel spec
    // (docs/tasks/driving/reports/or-diesel-loco-spec.md) and its formulas.
    const bool dieselThere = QFileInfo::exists(locoFolder + "/diesel.lua");
    test.check(dieselThere, "diesel.lua is in " + locoFolder);
    if (dieselThere) {
        Sim::LocoScripts lua(locoFolder);
        // With an exhaust emitter: its script publishes the exhaust only for
        // emitters the vehicle has (task driving 05).
        writeUtf16(files.path() + "/diesel.eng",
                   dieselFile("\tEffects ( DieselSpecialEffects ( Exhaust1 ( 0 4.83 -3.683  0 1 0  0.2 ) ) )\n"));
        const Sim::Vehicle legacy = vehicle("diesel.eng");
        const auto state = [](DieselRun &d, const char *wanted) {
            return [&d, wanted] { return d.text("engineState") == wanted; };
        };
        // Its engine built as Open Rails builds a legacy one (spec §3): at
        // idle 282 kW from the standard curve, (0.2 / 0.81) × 3263 kW ×
        // 315 / 900; fuel at idle as DieselUsedPerHourAtIdle, 37.85 l/h.
        {
            DieselRun d(lua, legacy);
            d.step(300);
            const double idle = d.number("engineRpm"), available = d.number("availablePower");
            const double before = d.number("fuel");
            d.step(60000);
            const double litres = (before - d.number("fuel")) * 1000.0 / 600.0 * 3600.0;
            test.check(d.problem.isEmpty() && idle == 315.0 && std::fabs(available - 281987.654) < 1.0
                               && std::fabs(litres - 37.854) < 0.7,
                       QString("diesel.lua, a legacy engine: idle %1 rpm (315), %2 W there (281988), %3 l/h (37.85) %4")
                               .arg(idle).arg(available, 0, 'f', 1).arg(litres, 0, 'f', 2).arg(d.problem));
        }
        // Its sounds (task sound 02): Variable1 the throttle (0 to 1);
        // Variable2 the engine's RPM share, following at the legacy keys'
        // rate, 40 rpm/s over 585 (Open Rails' 0.0684 a second); each
        // engine's torque its power over its speed.
        {
            DieselRun d(lua, legacy);
            d.step(300);
            d.command("direction", 1.0);
            d.command("throttle", 1.0);
            d.in.speed = 10.0;
            d.step(500);
            const double at5 = d.number("variable2"), share5 = (d.number("engineRpm") - 315.0) / 585.0;
            d.step(2500);
            const double at30 = d.number("variable2");
            const double torque = d.number("engineTorque");
            const double expected = d.number("enginePower") / (d.number("engineRpm") * 2.0 * 3.14159265358979 / 60.0);
            test.check(d.problem.isEmpty() && d.number("variable1") == 1.0 && std::fabs(at5 - std::min(share5, 5.0 * 40.0 / 585.0)) < 0.01
                               && at30 == 1.0 && std::fabs(torque - expected) < 1e-6 * expected,
                       QString("diesel.lua's sounds: Variable1 %1; Variable2 %2 after 5 s at full throttle (the RPM "
                               "share %3, at most %4), %5 after 30 s; torque %6 N·m")
                               .arg(d.number("variable1")).arg(at5).arg(share5).arg(5.0 * 40.0 / 585.0).arg(at30)
                               .arg(torque, 0, 'f', 0));
        }
        // Starting and stopping (spec §4's timings, at 60 steps a second):
        // running 9.2 s after a start, idle to full RPM 15.6 s, back 15.6 s,
        // stopped 8.8 s. 23 at the start and again at main power; 24 twice
        // at the stop. With the throttle open it does not stop.
        {
            DieselRun d(lua, legacy);
            d.step(10);
            d.command("power", false);
            d.until(state(d, "stopped"), 60.0);
            const double from = d.t;
            d.command("engine", true);
            const double running = d.until(state(d, "running"), 60.0);
            d.until([&] { return d.text("mainPower") == "true"; }, 10.0);
            d.step(2000);
            d.command("throttle", 1.0);
            const double up = d.until([&] { return d.number("engineRpm") >= 900.0; }, 60.0);
            d.command("engine", false);
            d.step();
            const bool locked = d.events.contains("engineLocked") && d.text("engineState") == "running";
            d.command("throttle", 0.0);
            const double down = d.until([&] { return d.number("engineRpm") <= 315.0; }, 60.0);
            d.step(200);
            const double stopFrom = d.t;
            d.command("engine", false);
            const double stopped = d.until(state(d, "stopped"), 60.0);
            const QStringList started = d.soundsSince(from), stopping = d.soundsSince(stopFrom);
            test.check(std::fabs(running - 9.2) < 0.3 && std::fabs(up - 15.6) < 0.3 && std::fabs(down - 15.6) < 0.3
                               && std::fabs(stopped - 8.8) < 0.3 && locked && started.count("23") == 2
                               && stopping.count("24") == 2,
                       QString("diesel.lua, starting: running %1 s (9.2), idle to full %2 s (15.6), back %3 s (15.6), "
                               "stopped %4 s (8.8)%5; sounds %6, at the stop %7")
                               .arg(running).arg(up).arg(down).arg(stopped).arg(locked ? "; locked with the throttle open" : "")
                               .arg(started.join(' '), stopping.join(' ')));
        }
        // A restart while it stops, between the starter's RPM and the
        // confirm RPM: running again (Open Rails leaves it starting for
        // ever; task 03, choice 3).
        {
            DieselRun d(lua, legacy);
            d.step(10);
            d.command("engine", false);
            d.step(20);
            const double rpm = d.number("engineRpm");
            d.command("engine", true);
            const double back = d.until(state(d, "running"), 30.0);
            test.check(rpm > 210.0 && rpm < 346.5 && back > 0.0,
                       QString("diesel.lua: restarted at %1 rpm while stopping, running again in %2 s").arg(rpm).arg(back));
        }
        // A Manual relay closing in 6 s, main power 5 s after it, auxiliary
        // power 10 s after the start (OR_PSK's SP42): from its hot start the
        // relay's order (217), closing (151), closed (152) at 6.03 s, 227 at
        // 10.01 s, 23 at 11.04 s (a step later where the timer's sum of steps
        // falls short); the RPM at idle until main power though the throttle
        // is open.
        writeUtf16(files.path() + "/manual.eng",
                   dieselFile("\tORTSTractionCutOffRelay ( Manual )\n\tORTSTractionCutOffRelayClosingDelay ( 6s )\n"
                              "\tORTSPowerOnDelay ( 5s )\n\tORTSAuxPowerOnDelay ( 10s )\n"));
        {
            DieselRun d(lua, vehicle("manual.eng"));
            d.command("direction", 1.0);
            d.command("throttle", 1.0);
            double closedAt = -1, auxAt = -1, powerAt = -1, idleRpm = -1;
            while (d.t < 13.0) {
                d.step();
                if (closedAt < 0 && d.text("tractionCutOffRelay") == "closed")
                    closedAt = d.t;
                if (auxAt < 0 && d.text("auxPower") == "true")
                    auxAt = d.t;
                if (powerAt < 0 && d.text("mainPower") == "true") {
                    powerAt = d.t;
                    idleRpm = d.number("engineRpm");
                }
            }
            QStringList power = d.soundsSince(0);
            for (const char *lever : {"103", "15", "16"})
                power.removeAll(lever);
            test.check(std::fabs(closedAt - 6.03) < 0.025 && std::fabs(auxAt - 10.01) < 0.025
                               && std::fabs(powerAt - 11.04) < 0.025 && idleRpm == 315.0 && d.number("engineRpm") > 330.0
                               && power.join(' ') == "217 151 152 227 23",
                       QString("diesel.lua, a Manual relay: closed %1 s (6.03), auxiliary power %2 (10.01), main power %3 "
                               "(11.04) at %4 rpm (315); its sounds %5")
                               .arg(closedAt).arg(auxAt).arg(powerAt).arg(idleRpm).arg(power.join(' ')));
        }
        // Its exhaust (task driving 05), at 20 m/s: the air and fuel through
        // the engine, the stack hotter and faster with the load; dark while
        // the RPM rises fast (Open Rails' transient), light as it falls;
        // none stopped.
        {
            DieselRun d(lua, legacy);
            d.in.speed = 20.0;
            d.step(300);
            struct Fx { double flow, speed, temperature, steam, soot; };
            const auto fx = [&] {
                const auto n = [&](const char *field) {
                    return d.number(QString("fx.exhaust.%1").arg(field).toUtf8().constData());
                };
                return Fx{n("flow"), n("speed"), n("temperature"), n("steam"), n("soot")};
            };
            const Fx idle = fx();
            d.command("direction", 1.0);
            d.command("throttle", 1.0);
            d.step(300);
            const Fx rising = fx();
            d.until([&] { return d.number("engineRpm") >= 900.0; }, 60.0);
            d.step(1000);
            const Fx full = fx();
            d.command("throttle", 0.0);
            d.step(300);
            const Fx falling = fx();
            d.command("power", false);
            d.until(state(d, "stopped"), 60.0);
            const Fx stopped = fx();
            const auto show = [](const Fx &f) {
                return QString("%1 m³/s %2 m/s %3 °C steam %4 soot %5")
                        .arg(f.flow, 0, 'f', 2).arg(f.speed, 0, 'f', 1).arg(f.temperature, 0, 'f', 0)
                        .arg(f.steam, 0, 'f', 3).arg(f.soot, 0, 'f', 2);
            };
            test.check(d.problem.isEmpty() && idle.flow > 0.4 && idle.flow < 2.0 && idle.temperature < 250.0
                               && idle.soot < 0.3 && rising.soot > 0.55 && full.flow > 8.0 && full.flow < 16.0
                               && full.speed > 30.0 && full.speed <= 41.0 && full.temperature > 400.0
                               && full.steam > 0.03 && full.steam < 0.07 && full.soot < 0.3
                               && std::fabs(falling.soot - 0.05) < 1e-9 && stopped.flow == 0.0,
                       QString("diesel.lua's exhaust: idle %1; rising %2; full %3; falling %4; stopped %5 %6")
                               .arg(show(idle), show(rising), show(full), show(falling), show(stopped), d.problem));
        }
        // Its force: Open Rails' diesel law, MaxPower × t / v (163.15 kN at
        // t = 0.5 and 10 m/s; an electric's t² would give 81.6); the
        // engines' power capping force × speed while the RPM rises.
        {
            DieselRun d(lua, legacy);
            d.in.speed = 20.0;
            d.step(10);
            d.command("direction", 1.0);
            d.command("throttle", 1.0);
            double worst = -1e9;
            int capped = 0;
            for (int i = 0; i < 2000; ++i) {
                d.step();
                const double over = d.number("tractionForce") * 20.0 - d.number("availablePower");
                worst = std::max(worst, over);
                capped += std::fabs(over) < 1.0 ? 1 : 0;
            }
            d.in.speed = 10.0;
            d.command("throttle", 0.5);
            d.step(6000);
            const double force = d.number("tractionForce");
            test.check(worst < 1e-3 && capped > 100 && std::fabs(force - 163150.0) < 1.0,
                       QString("diesel.lua force: %1 N at t 0.5 and 10 m/s (163150); force × speed above the engine's "
                               "power by %2 W at most, held to it %3 steps of 2000")
                               .arg(force, 0, 'f', 1).arg(worst).arg(capped));
        }
        // Two engines (ORTSDieselEngines, each read on its own): idle 400 and
        // 600 rpm; the helper stopped (spinning down from 1200 rpm at 40
        // rpm/s), half the power, so half the force at 20 m/s (163.15 kN,
        // then 81.575).
        writeUtf16(files.path() + "/twin.eng",
                   dieselFile("\tORTSDieselEngines ( 2\n"
                              "\t\tDiesel ( IdleRPM ( 400 ) MaxRPM ( 1000 ) MaximalPower ( 1631.5kW ) )\n"
                              "\t\tDiesel ( IdleRPM ( 600 ) MaxRPM ( 1200 ) MaximalPower ( 1631.5kW ) )\n\t)\n"));
        {
            DieselRun d(lua, vehicle("twin.eng"));
            d.in.speed = 20.0;
            d.step(10);
            const double first = d.number("engineRpm"), second = d.number("engineRpm2");
            d.command("direction", 1.0);
            d.command("throttle", 1.0);
            d.step(6000);
            const double both = d.number("tractionForce");
            d.command("helperEngines", false);
            d.step(5000);
            const double one = d.number("tractionForce");
            test.check(first == 400.0 && second == 600.0 && std::fabs(both - 163150.0) < 1.0
                               && std::fabs(one - 81575.0) < 1.0 && d.text("helperEngines") == "stopped",
                       QString("diesel.lua, two engines: idle %1 and %2 rpm; %3 N, the helper stopped %4 N")
                               .arg(first).arg(second).arg(both, 0, 'f', 1).arg(one, 0, 'f', 1));
        }
        // The dynamic brake needs main power on a diesel.
        writeUtf16(files.path() + "/dynamic.eng",
                   dieselFile("\tDynamicBrakesMaximumForce ( 100kN )\n\tDynamicBrakesMinUsableSpeed ( 5 )\n"
                              "\tDynamicBrakesMaximumEffectiveSpeed ( 25 )\n"
                              "\tEngineControllers ( Brake_Dynamic ( 0 1 0.1 0 NumNotches ( 0 ) ) )\n"));
        {
            DieselRun d(lua, vehicle("dynamic.eng"));
            d.in.speed = 15.0;
            d.step(10);
            d.command("power", false);
            d.command("direction", 1.0);
            d.command("dynamicBrake", 1.0);
            d.step(500);
            const double unpowered = d.number("dynamicBrakeForce");
            d.command("power", true);
            d.until([&] { return d.text("mainPower") == "true"; }, 30.0);
            d.step(1000);
            const double powered = d.number("dynamicBrakeForce");
            test.check(unpowered == 0.0 && powered > 0.0,
                       QString("diesel.lua dynamic brake: %1 N without main power, %2 N 10 s after it")
                               .arg(unpowered).arg(powered));
        }
        // Fuel: 0.2 l at 37.85 l/h empty after 19.0 s; the engine stops, 24
        // once for the tank; it does not start empty; refuelled, it does.
        writeUtf16(files.path() + "/tank.eng", dieselFile("\tMaxDieselLevel ( 0.2l )\n"));
        {
            DieselRun d(lua, vehicle("tank.eng"));
            const double empty = d.until([&] { return d.number("fuel") <= 0.0; }, 60.0);
            d.step(1000);
            const int offs = int(d.soundsSince(0).count("24"));
            d.command("engine", true);
            d.step(1500);
            const QString emptyStart = d.text("engineState");
            d.command("refuel", true);
            d.command("engine", true);
            const double refuelled = d.until(state(d, "running"), 30.0);
            test.check(std::fabs(empty - 19.02) < 0.1 && offs == 3 && emptyStart != "running" && refuelled > 0.0,
                       QString("diesel.lua fuel: empty after %1 s (19.02), 24 %2 times (the tank, main power, the "
                               "engine), started empty %3, refuelled running in %4 s")
                               .arg(empty).arg(offs).arg(emptyStart).arg(refuelled));
        }
        // Saved and restored: the same steps after.
        {
            DieselRun a(lua, legacy);
            a.in.speed = 10.0;
            a.step(10);
            a.command("direction", 1.0);
            a.command("throttle", 0.5);
            a.step(500);
            QByteArray saved;
            const bool savedOk = a.model->save(saved);
            DieselRun b(lua, legacy);
            b.in = a.in;
            const bool restored = savedOk && b.model->restore(saved);
            a.step(300);
            b.step(300);
            test.check(restored && a.number("engineRpm") == b.number("engineRpm")
                               && a.number("tractionForce") == b.number("tractionForce") && a.number("fuel") == b.number("fuel"),
                       QString("diesel.lua: its state saved (%1 bytes) and restored, the same steps after").arg(saved.size()));
        }
    }
    // Heard trains (task sound 02): AI trains run their vehicles' models as
    // shadows, their levers from the AI's motion, their forces unused. Two
    // straight 2 km lines, an AI electric with wagons on each.
    if (scriptThere) {
        TestTrack::Layout two;
        two.end(1, 10);
        two.track(10, 1, 2, 200);
        two.end(2, 10);
        two.end(3, 11);
        two.track(11, 3, 4, 200);
        two.end(4, 11);
        QScopedValueRollback<TDB *> restoreLines(Game::trackDB, &two.db);
        const auto lines = std::make_shared<const Sim::TrackGeometry>(&two.db);
        const auto planOn = [&](int id, int track, int wagons, std::vector<Sim::WaitPlan> waits = {},
                                const Sim::Vehicle *loco = nullptr) {
            const PathTrack::Result path = PathTrack::startAt(&two.db, {track, 100.0f});
            Sim::TrainPlan p;
            p.id = id;
            p.vehicles = {loco != nullptr ? *loco : electric};
            for (int i = 0; i < wagons; ++i)
                p.vehicles.push_back(wagon);
            for (const Sim::Vehicle &v : p.vehicles)
                p.length += float(v.length());
            p.maxSpeed = 30.0f;
            Sim::SetRates(p);
            p.performance = Sim::PerformanceOf(p.vehicles);
            p.driving = Sim::DrivingOf(p.vehicles);
            p.startTime = t0;
            p.route = Sim::BuildRoute(&two.db, path.nodes, PathTrack::legs(&two.db, path.nodes), 31.0);
            p.startFront = p.route.pathStart + p.length + 1.0;
            for (Sim::WaitPlan &w : waits)
                w.front += p.startFront;
            p.waits = waits;
            return p;
        };
        const auto inputWith = [&](std::vector<Sim::TrainPlan> plans, int drivenTrain) {
            auto made = std::make_shared<Sim::SimulationInput>();
            made->startTime = t0;
            made->geometry = lines;
            made->trains = std::move(plans);
            made->step = drivenTrain >= 0 ? 0.01 : 0.1;
            made->driven = drivenTrain;
            made->lineVoltage = 25000.0;
            made->locoScripts = locoFolder;
            return std::shared_ptr<const Sim::SimulationInput>(made);
        };
        // One AI train run twice, heard the second time: the same motion
        // to the metre's millionth; its throttle up while it starts, its
        // brakes on as it stops at the end of its path; a new version each
        // 0.1 s step.
        {
            struct Sample {
                double front, speed;
            };
            std::vector<Sample> alone, heard;
            double throttleMost = 0.0, brakeMost = 0.0, cylinderMost = 0.0;
            bool shadowed = true, versions = true;
            unsigned long long version = 0;
            for (int pass = 0; pass < 2; ++pass) {
                Sim::SimulationCore core(inputWith({planOn(0, 10, 10)}, -1));
                if (pass == 1)
                    core.hear({0});
                for (int i = 0; i < 3000; ++i) {
                    core.step(0.1);
                    const Sim::TrainState &st = core.trains()[0];
                    (pass == 0 ? alone : heard).push_back({st.front, st.speed});
                    if (pass == 0 || st.status != Sim::TrainStatus::Running)
                        continue;
                    shadowed = shadowed && core.shadowed() == std::vector<int>{0};
                    const unsigned long long now = core.shadowVersion(0);
                    versions = versions && now > version;
                    version = now;
                    if (st.acceleration > 0.0f)
                        throttleMost = std::max(throttleMost, valueOf(core, 0, -1, "throttle"));
                    if (st.acceleration < 0.0f) {
                        brakeMost = std::max(brakeMost, valueOf(core, 0, -1, "brakeDemand"));
                        cylinderMost = std::max(cylinderMost, valueOf(core, 0, 5, "brakeCylinder"));
                    }
                }
            }
            double apart = 0.0;
            for (size_t i = 0; i < alone.size() && i < heard.size(); ++i)
                apart = std::max(apart, std::fabs(alone[i].front - heard[i].front) + std::fabs(alone[i].speed - heard[i].speed));
            test.check(alone.size() == heard.size() && apart == 0.0 && shadowed && versions && throttleMost > 0.0
                               && brakeMost > 0.0 && cylinderMost > 1.0 * Psi,
                       QString("heard: the AI's motion the same (%1 apart), its shadow stepped each 0.1 s; starting, "
                               "the throttle up to %2; stopping, the brake demand up to %3 of full service, a wagon's "
                               "cylinder %4 psi")
                               .arg(apart).arg(throttleMost).arg(brakeMost).arg(cylinderMost / Psi, 0, 'f', 1));
        }
        // Heard, not heard, heard again: its shadow made, gone, made anew
        // (its version never the same twice).
        {
            Sim::SimulationCore core(inputWith({planOn(0, 10, 2)}, -1));
            core.hear({0});
            core.step(5.0);
            const bool first = core.shadowed() == std::vector<int>{0};
            const unsigned long long before = core.shadowVersion(0);
            core.hear({});
            core.step(0.1);
            const bool gone = core.shadowed().empty() && core.outputsOf(0, -1) == nullptr;
            core.hear({0});
            core.step(0.1);
            test.check(first && gone && core.shadowed() == std::vector<int>{0} && core.shadowVersion(0) > before,
                       "heard: a shadow made, gone when not heard, made again");
        }
        // A horn waiting point (60013, 3 s) 300 m on: the horn on, off 3 s
        // later.
        {
            Sim::WaitPlan horn;
            horn.front = 300.0;
            horn.value = 60013;
            Sim::SimulationCore core(inputWith({planOn(0, 10, 2, {horn})}, -1));
            core.hear({0});
            double on = -1.0, off = -1.0;
            for (int i = 0; i < 1200 && off < 0.0; ++i) {
                core.step(0.1);
                for (const auto &[vehicle, m] : core.takeMessages(0)) {
                    if (m.name == "sound" && m.number() == 8.0 && on < 0.0)
                        on = core.time();
                    if (m.name == "sound" && m.number() == 9.0 && on >= 0.0)
                        off = core.time();
                }
            }
            test.check(on >= 0.0 && off - on > 2.85 && off - on < 3.25,
                       QString("heard: the horn at its waiting point, on at %1 s, off %2 s later")
                               .arg(on - t0, 0, 'f', 1).arg(off - on, 0, 'f', 1));
        }
        // A heard diesel with a dynamic brake: its engine above idle as it
        // starts away (the throttle the AI's motion needs), its dynamic
        // brake braking above 5 m/s.
        {
            writeUtf16(files.path() + "/heard-diesel.eng",
                       dieselFile("\tDynamicBrakesMaximumForce ( 100kN )\n\tDynamicBrakesMinUsableSpeed ( 5 )\n"
                                  "\tDynamicBrakesMaximumEffectiveSpeed ( 25 )\n"
                                  "\tEngineControllers ( Brake_Dynamic ( 0 1 0.1 0 NumNotches ( 0 ) ) )\n"));
            const Sim::Vehicle diesel = vehicle("heard-diesel.eng");
            Sim::SimulationCore core(inputWith({planOn(0, 10, 10, {}, &diesel)}, -1));
            core.hear({0});
            double rpmMost = 0.0, dynamicMost = 0.0;
            for (int i = 0; i < 3000; ++i) {
                core.step(0.1);
                const Sim::TrainState &st = core.trains()[0];
                if (st.status != Sim::TrainStatus::Running)
                    continue;
                if (st.acceleration > 0.0f)
                    rpmMost = std::max(rpmMost, valueOf(core, 0, -1, "engineRpm"));
                if (st.acceleration < 0.0f && st.speed > 5.0f)
                    dynamicMost = std::max(dynamicMost, valueOf(core, 0, -1, "dynamicBrake"));
            }
            test.check(rpmMost > 600.0 && dynamicMost > 0.0,
                       QString("heard: a diesel starting away at %1 rpm (idle 315); braking, its dynamic brake at %2")
                               .arg(rpmMost, 0, 'f', 0).arg(dynamicMost));
        }
        // A driven train beside a heard one: its run the same with or without
        // the other heard (the shadows' scripts in a state of their own);
        // the frames give the heard train's values, a new version each
        // 0.1 s, not each 0.01 s step.
        {
            std::vector<double> runs[2];
            int versions = 0;
            bool framed = true;
            for (int pass = 0; pass < 2; ++pass) {
                Sim::SimulationCore core(inputWith({planOn(0, 10, 2), planOn(1, 11, 10)}, 0));
                if (pass == 1)
                    core.hear({0, 1});
                core.step(0.01);
                core.command(0, -1, {"throttle", 0.5});
                std::shared_ptr<Sim::SimulationFrame> frame;
                unsigned long long last = 0;
                for (int i = 0; i < 3000; ++i) {
                    core.step(0.01);
                    runs[pass].push_back(core.trains()[0].front);
                    runs[pass].push_back(core.trains()[0].speed);
                    if (pass == 0 || i < 2000)
                        continue;
                    frame = Sim::MakeFrame(core, frame.get());
                    framed = framed && frame->heard.size() == 1 && frame->heard[0].train == 1
                            && frame->heard[0].values.size() == 11 && frame->heard[0].cab == 0;
                    if (!frame->heard.empty() && frame->heard[0].version != last) {
                        versions += last != 0 ? 1 : 0;
                        last = frame->heard[0].version;
                    }
                }
            }
            test.check(runs[0] == runs[1] && framed && versions == 100,
                       QString("heard: a driven train's run the same beside a heard one%1; the frames give the "
                               "heard one's values%2 (not the driven one's), %3 new versions in 10 s of 0.01 s steps")
                               .arg(runs[0] == runs[1] ? "" : " (NOT)", framed ? "" : " (NOT)").arg(versions));
        }
        // The cost of a heard train a shadow step: an electric and 40 wagons.
        {
            qint64 spent[2] = {0, 0};
            for (int pass = 0; pass < 2; ++pass) {
                Sim::SimulationCore core(inputWith({planOn(0, 10, 40)}, -1));
                if (pass == 1)
                    core.hear({0});
                core.step(10.0);
                QElapsedTimer timer;
                timer.start();
                core.step(60.0);
                spent[pass] = timer.nsecsElapsed();
            }
            const double us = (spent[1] - spent[0]) / 1000.0 / 600.0;
            test.check(us < 10000.0, QString("heard: an electric and 40 wagons, %1 µs a shadow step").arg(us, 0, 'f', 1));
        }
    }
    // The cost of a scripted loco a step: the electric, the diesel, the
    // steam loco (MSTS's Royal Scot, air braked), the last two with the
    // Scotsman's and a Dash 9's emitters (task driving 05).
    writeUtf16(files.path() + "/steam.eng",
               engineFile(AirBrakes)
                       .replace("\tType ( Electric )\n\tMaxPower ( 5000kW )\n\tMaxForce ( 300kN )\n",
                                "\tType ( Steam )\n\tWheelRadius ( 1.03m )\n\tNumWheels ( 1 )\n\tNumCylinders ( 3 )\n"
                                "\tCylinderDiameter ( 18in )\n\tCylinderStroke ( 26in )\n\tMaxBoilerPressure ( 250 )\n"
                                "\tBoilerVolume ( \"160*(ft^3)\" )\n\tSuperHeater ( 1.2 )\n\tInjectorTypes ( 0 1 )\n"
                                "\tMaxTenderCoalMass ( 11200lb )\n\tMaxTenderWaterMass ( 40000lb )\n"
                                "\tEffects ( SteamSpecialEffects ( StackFX ( 0 4.25 4.5  0 1 0  0.2 )\n"
                                "\t\tCylindersFX ( -1.223 0.71 8.217  0 0 1  0.05 )\n"
                                "\t\tCylindersFX ( 1.223 0.71 8.217  0 0 1  0.05 )\n"
                                "\t\tSafetyValvesFX ( 0 3.885 -4.062  0 1 0  0.2 )\n"
                                "\t\tWhistleFX ( -0.66 3.59 -4.49  0 1 0  0.05 ) ) )\n"));
    for (const auto &[script, loco] : {std::pair{"electric.lua", electric}, std::pair{"diesel.lua", vehicle("diesel.eng")},
                                       std::pair{"steam.lua", vehicle("steam.eng")}}) {
        if (!QFileInfo::exists(locoFolder + "/" + script))
            continue;
        Sim::LocoScripts lua(locoFolder);
        std::unique_ptr<Sim::VehicleModel> m = lua.make(loco.definition);
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
        test.check(us < 1000.0, QString("%1: %2 µs a step").arg(script).arg(us, 0, 'f', 1));
    }
    // The cost of every value of every driven vehicle in each frame (the
    // copy MakeFrame makes, shared, and the copy the next step makes when it
    // writes them), the whole frame's, and the cars' sound inputs from the
    // values (the main thread's, each frame): an electric loco and 40
    // wagons, a frame each step (the host makes one a tick).
    if (scriptThere) {
        std::vector<Sim::Vehicle> cars{electric};
        for (int i = 0; i < 40; ++i)
            cars.push_back(wagon);
        auto made = std::make_shared<Sim::SimulationInput>();
        made->startTime = t0;
        made->geometry = geometry;
        made->trains.push_back(planOf(cars));
        made->step = 0.01;
        made->driven = 0;
        made->lineVoltage = 25000.0;
        made->locoScripts = locoFolder;
        Sim::SimulationCore core(made);
        core.step(1.0);
        size_t count = 0;
        for (size_t v = 0; v < cars.size(); ++v)
            if (const Sim::VehicleOutputs *out = core.outputsOf(0, int(v)))
                count += size_t(out->values.size());
        std::vector<QHash<QString, Sim::MessageValue>> kept(cars.size());
        std::shared_ptr<Sim::SimulationFrame> frame = Sim::MakeFrame(core);
        const int rounds = 20, steps = 200;
        qint64 bare = 0, copied = 0, framed = 0, sound = 0;
        QElapsedTimer timer;
        float heard = 0.0f;
        for (int r = 0; r < rounds; ++r) {
            timer.start();
            for (int i = 0; i < steps; ++i)
                core.step(0.01);
            bare += timer.nsecsElapsed();
            timer.start();
            for (int i = 0; i < steps; ++i) {
                core.step(0.01);
                for (size_t v = 0; v < cars.size(); ++v)
                    if (const Sim::VehicleOutputs *out = core.outputsOf(0, int(v)))
                        kept[v] = out->values;
            }
            copied += timer.nsecsElapsed();
            timer.start();
            for (int i = 0; i < steps; ++i) {
                core.step(0.01);
                frame = Sim::MakeFrame(core, frame.get());
            }
            framed += timer.nsecsElapsed();
            timer.start();
            for (int i = 0; i < steps; ++i)
                for (const QHash<QString, Sim::MessageValue> &values : frame->drivenValues->values)
                    heard += Sim::carSoundInput(values).brakeCylinder;
            sound += timer.nsecsElapsed();
        }
        const double n = double(rounds) * steps;
        const double values = (copied - bare) / 1000.0 / n, whole = (framed - bare) / 1000.0 / n;
        test.check(frame->drivenValues != nullptr && frame->drivenValues->values.size() == cars.size() && heard >= 0.0f,
                   QString("frames: %1 values of 41 vehicles: %2 µs a frame (the whole frame %3 µs, a step %4 µs); "
                           "their sound inputs %5 µs")
                           .arg(count).arg(values, 0, 'f', 1).arg(whole, 0, 'f', 1).arg(bare / 1000.0 / n, 0, 'f', 1)
                           .arg(sound / 1000.0 / n, 0, 'f', 1));
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
    // Each kind of engine through its script.
    struct Kind {
        int id;
        const char *name, *script;
        int count = 0, failed = 0, unpowered = 0, forceless = 0;
        qint64 ms = 0;
    };
    Kind kinds[] = {{4, "electric", "electric.lua"}, {5, "diesel", "diesel.lua"}, {6, "steam", "steam.lua"}};
    QStringList listed;
    for (const QString &path : files) {
        const QFileInfo info(path);
        const std::shared_ptr<const VehicleDefinition> def =
                VehicleDefinition::read(info.path(), info.fileName(), root, true);
        if (!def || !def->ok())
            continue;
        Kind *kind = nullptr;
        for (Kind &k : kinds)
            if (def->wagonTypeId == k.id)
                kind = &k;
        if (!kind)
            continue;
        ++kind->count;
        QElapsedTimer timer;
        timer.start();
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
        kind->ms += timer.elapsed();
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
            ++kind->failed;
            listed << name + ": " + problem;
        } else if (!powered && def->forceN > 0.0) {
            // (OR_PSK's dead locos, "nieczynna", have no force and no RPM.)
            ++kind->unpowered;
            listed << name + ": no power after 2 minutes";
        } else if (out.motiveForce <= 0.0 && def->forceN > 0.0) {
            ++kind->forceless;
            listed << QString("%1: no force at %2 m/s (MaxForce %3 N, MaxPower %4 W)")
                              .arg(name).arg(in.speed).arg(def->forceN).arg(def->powerW);
        }
    }
    if (verbose)
        for (const QString &line : listed)
            qInfo().noquote() << "  " << line;
    for (const Kind &k : kinds)
        test.check(k.failed == 0,
                   QString("%1 %2 engines of %3 run %4 in %5 s: %6 script errors; of those with a MaxForce, "
                           "%7 without power, %8 without force")
                           .arg(k.count).arg(k.name).arg(files.size()).arg(k.script).arg(k.ms / 1000.0)
                           .arg(k.failed).arg(k.unpowered).arg(k.forceless));
    return test.finish();
}
