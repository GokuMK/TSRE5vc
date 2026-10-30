// A driven train in the simulation core (task driving 02): its vehicles'
// models give the forces, TrainPhysics moves its cars, the core keeps doing
// what it does for every train (the track ahead reserved, signals, stops,
// events) without stopping it anywhere.
#include <tsre/sim/SimulationCore.h>
#include <QDebug>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <tsre/sim/AirBrake.h>
#include <tsre/sim/LocoScript.h>
#include <tsre/sim/SteamHeating.h>
#include <tsre/sim/TrainPhysics.h>
#include <tsre/sim/VehicleModel.h>
#include <tsre/trains/CarExhaust.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/trains/WaitingPoint.h>

namespace Sim {

namespace {

// The rail's adhesion, dry, for now (weather later).
constexpr double Adhesion = 0.33;
constexpr double Gravity = 9.81;
// Within this of its part's end, standing, a driven train reverses when its
// driver sets the other direction (Open Rails' reversal point zone).
constexpr double ReversalReach = 100.0;
// A stop counts as passed this far beyond its platform.
constexpr double PassedStop = 0.5;
// Moving again after a stop: faster than this.
constexpr double Moving = 0.1;
// A mass where a file gives none: kg a metre of vehicle.
constexpr double MassPerMetre = 2000.0;
// A coupler where a file gives none: N/m, and its free play (m) all told.
constexpr double DefaultStiffness = 7.5e6;
constexpr double DefaultSlack = 0.01;

// A heard train's driver (task sound 02): its levers in steps of this,
// moved at most once in this many seconds; the train brake held standing
// (a share of full service); the dynamic brake used above this speed (m/s);
// a steam loco's cutoff from a stand and at speed (m/s), its cylinder cocks
// open over the first metres from a stand; the crossing horn's seconds (the
// middle of Open Rails' single blast, 2 to 5 s at random).
constexpr double LeverStep = 0.05;
constexpr double LeverPace = 1.0;
constexpr double HeldBrake = 0.5;
constexpr double DynamicFrom = 5.0;
constexpr double FullCutoff = 0.75;
constexpr double RunningCutoff = 0.25;
constexpr double NotchedUpAt = 20.0;
constexpr double CocksOpenFor = 30.0;
constexpr double CrossingHornSeconds = 3.0;
// A shadow's messages kept for the host at most.
constexpr size_t ShadowMessages = 1000;
// Below this outside air (°C) a train with heated cars starts heated (task
// driving 06, the user's).
constexpr double WarmBelow = 15.0;
// Steam's volume at 100 °C and one atmosphere (m³/kg): what shows of the
// heating's leaks and traps; their jets' speeds (m/s).
constexpr double SteamVolume = 1.673;
constexpr double LeakSpeed = 450.0, TrapSpeed = 10.0;
// A heated car's air too cold for its passengers below this (°C), warm
// again above that: the driven train's messages carTooCold and carWarm (the
// user's, 2026-10-30; Open Rails' 7.2 and 18.3 °C, for the whole train).
constexpr double TooCold = 12.0, WarmAgain = 16.0;

// The vehicle faces the way its train runs (+1) or the other (-1).
int facing(const Vehicle &vehicle, bool reversed) {
    return vehicle.flip != reversed ? -1 : 1;
}

double numberOf(const MessageValue &value) {
    return Message{QString(), value}.number();
}

double lever(double value) {
    return std::round(std::clamp(value, 0.0, 1.0) / LeverStep) * LeverStep;
}

} // namespace

struct Driven {
    // Its cars in the order they run (the leading one first).
    TrainPhysics physics;
    // Its vehicles' models and their last outputs, in the consist's order.
    std::vector<std::unique_ptr<VehicleModel>> models;
    std::vector<VehicleOutputs> outputs;
    // The buses: each vehicle's air brake (consist order), the brake pipe
    // (running order) and the EP wire's demand (-1 none).
    std::vector<AirBrake> brakes;
    BrakePipe pipe;
    // The main reservoir pipe (running order): joined between twin pipe
    // vehicles only.
    BrakePipe mainPipe;
    double ep = -1.0;
    // The vehicle (consist order) whose cab is driven.
    int cab = -1;
    // Messages sent, not yet taken.
    std::vector<std::pair<int, Message>> messages;
    bool endReported = false;
    bool authorityReported = false;
    // The stop it stands at, its arrival recorded; -1 none.
    int arrivedAt = -1;
    // The forces on its cars in the last step (running order).
    std::vector<TrainPhysics::Forces> forces;
    // Its steam heating (task driving 06, consist order), stepped every
    // SteamHeating::Step: the time since its last step.
    SteamHeating heating;
    double heatingClock = 0.0;
    std::vector<double> heatingSupply;
    // Each heated car too cold (told), consist order.
    std::vector<char> heatingCold;
};

// A heard AI train's shadow (task sound 02): its vehicles as a driven
// train's, its cars placed along the AI train at its speed, their forces
// unused; and the driver the AI's motion implies.
struct Shadow {
    Driven d;
    // When it last stepped, the way the train ran then and the vehicles
    // (their version, task 07) it was made for.
    double last = 0.0;
    double lastSpeed = 0.0;
    bool reversed = false;
    unsigned int carsVersion = 0;
    unsigned long long version = 0;
    // Its levers as last set (-1 not yet), its train brake's demand (a
    // share of full service), and when they last moved; its cab has a
    // dynamic brake.
    double throttle = -1.0;
    double dynamicBrake = -1.0;
    double brakeDemand = -1.0;
    double reverser = -2.0;
    double moved = -1e9;
    bool dynamic = false;
    // A steam loco: its cylinder cocks open, from where.
    bool steam = false;
    bool standing = true;
    bool cocks = false;
    double cocksFrom = 0.0;
    // The horn sounding until (-1 not).
    double hornUntil = -1.0;
    bool told = false;
};

namespace {

// A train reversed: what goes in running order turned round (its cars,
// couplers and pipes), standing.
void turnRound(Driven &d) {
    std::reverse(d.physics.cars.begin(), d.physics.cars.end());
    std::reverse(d.physics.couplers.begin(), d.physics.couplers.end());
    d.pipe.reverse();
    d.mainPipe.reverse();
    for (TrainPhysics::Car &car : d.physics.cars)
        car.speed = 0.0;
}

// A driven or heard train's vehicles, null for neither.
const Driven *vehiclesOfTrain(const std::shared_ptr<Driven> &driven, const std::shared_ptr<Shadow> &shadow) {
    return driven ? driven.get() : shadow ? &shadow->d : nullptr;
}

} // namespace

bool SimulationCore::drive(int train) {
    if (train < 0 || size_t(train) >= states.size())
        return false;
    bool engine = false;
    for (const Vehicle &vehicle : runtime[size_t(train)].vehicles)
        engine = engine || vehicle.type() >= 4;
    if (!engine)
        return false;
    if (driven >= 0 && driven != train)
        leave(driven);
    driven = train;
    setStepLength(std::min(stepSeconds, DrivenStep));
    // A train not yet on the track is set up once placed (stepOnce).
    if (states[size_t(train)].status != TrainStatus::Waiting)
        takeOver(train);
    return true;
}

void SimulationCore::setUpVehicles(int train, Driven &d, const std::shared_ptr<LocoScripts> &scripts) {
    const TrainState &s = states[size_t(train)];
    const Runtime &rt = runtime[size_t(train)];
    const size_t n = rt.vehicles.size();
    int cabRun = -1;
    for (size_t v = 0; v < n; ++v) {
        const Vehicle &vehicle = rt.vehicles[v];
        // Its script's model, else the C++ one.
        std::unique_ptr<VehicleModel> model = scripts ? scripts->make(vehicle.definition) : nullptr;
        d.models.push_back(model ? std::move(model) : MakeVehicleModel(*vehicle.definition));
        d.brakes.emplace_back(*vehicle.definition);
        const int run = int(s.reversed ? n - 1 - v : v);
        if (d.models.back()->drives() && (cabRun < 0 || run < cabRun)) {
            cabRun = run;
            d.cab = int(v);
        }
    }
    d.outputs.resize(n);
    const double speed = std::max(0.0, double(s.speed));
    for (size_t r = 0; r < n; ++r) {
        const Vehicle &vehicle = rt.vehicles[s.reversed ? n - 1 - r : r];
        TrainPhysics::Car car;
        car.mass = vehicle.mass() > 0.0 ? vehicle.mass() : MassPerMetre * vehicle.length();
        car.length = vehicle.length();
        car.speed = speed;
        d.physics.cars.push_back(car);
        if (r + 1 < n) {
            // The coupler at its back: its file's spring, damping and free
            // play (r0's second value), else defaults.
            const VehicleDefinition &def = *vehicle.definition;
            TrainPhysics::Coupler c;
            c.stiffness = def.number("wagon(coupling(spring(stiffness", 0, 0.0);
            if (c.stiffness <= 0.0)
                c.stiffness = DefaultStiffness;
            const double nextMass = rt.vehicles[s.reversed ? n - 2 - r : r + 1].mass();
            c.damping = def.number("wagon(coupling(spring(damping", 0, 0.0);
            if (c.damping <= 0.0)
                c.damping = TrainPhysics::defaultDamping(c.stiffness, car.mass,
                                                         nextMass > 0.0 ? nextMass : car.mass);
            const double play = !def.couplings.isEmpty() && def.couplings[0].r0[1] > 0.0f
                    ? std::min(0.2, double(def.couplings[0].r0[1])) : DefaultSlack;
            c.tension = c.compression = 0.5 * play;
            d.physics.couplers.push_back(c);
        }
    }
    d.physics.placeFrom(s.front);
    // The brake pipe charged to its cab's full pressure, the brakes off.
    const VehicleDefinition *cabDef = d.cab >= 0 ? rt.vehicles[size_t(d.cab)].definition.get() : nullptr;
    const double full = cabDef ? BrakeSystemPressure(*cabDef) : BrakeSystemPressure(*VehicleDefinition::empty());
    d.pipe.timeFactor = cabDef ? BrakePipeTimeFactor(*cabDef) : BrakePipeTimeFactor(*VehicleDefinition::empty());
    // The main reservoir pipe at its locos' pressure along the twin pipe
    // vehicles (the first twin pipe loco's; Open Rails' 130 psi without one).
    double mainFull = 0.0;
    for (size_t v = 0; v < n && mainFull <= 0.0; ++v)
        if (rt.vehicles[v].type() >= 4 && d.brakes[v].twinPipe())
            mainFull = MainPipePressure(*rt.vehicles[v].definition);
    if (mainFull <= 0.0)
        mainFull = MainPipePressure(*VehicleDefinition::empty());
    d.mainPipe.timeFactor = d.pipe.timeFactor;
    for (size_t r = 0; r < n; ++r) {
        AirBrake &brake = d.brakes[s.reversed ? n - 1 - r : r];
        brake.charge(full);
        d.pipe.volume.push_back(brake.pipeVolume());
        d.pipe.pressure.push_back(full);
        d.mainPipe.volume.push_back(brake.pipeVolume());
        d.mainPipe.pressure.push_back(brake.twinPipe() ? mainFull : 0.0);
        if (r + 1 < n)
            d.mainPipe.joined.push_back(brake.twinPipe() && d.brakes[s.reversed ? n - 2 - r : r + 1].twinPipe());
    }
    // Its steam heating (task driving 06): in the cold, a train with heated
    // cars and an engine fitted for it starts as one in service, its
    // compartments warm and that engine's valve open (the cab's first; "on":
    // its controller's default, else fully).
    std::vector<HeatingSpec> specs;
    specs.reserve(n);
    for (size_t v = 0; v < n; ++v)
        specs.push_back(ReadHeating(*rt.vehicles[v].definition));
    int feeding = -1;
    if (d.cab >= 0 && specs[size_t(d.cab)].maxPressure > 0.0)
        feeding = d.cab;
    for (size_t v = 0; v < n && feeding < 0; ++v)
        if (specs[v].pipe && specs[v].maxPressure > 0.0)
            feeding = int(v);
    const bool heated = std::any_of(specs.begin(), specs.end(), [](const HeatingSpec &h) { return h.heated; });
    TrackPose head;
    const double air = in->air.at(now, n > 0 && pose(train, d.physics.cars[0].front, head) ? head.y : 0.0);
    const bool warm = heated && feeding >= 0 && air < WarmBelow;
    d.heating.setUp(std::move(specs), air, warm, unsigned(train));
    d.heatingSupply.assign(n, 0.0);
    d.heatingCold.assign(n, 0);
    if (warm)
        d.models[size_t(feeding)]->command({"steamHeat", QStringLiteral("on")});
}

void SimulationCore::takeOver(int train) {
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    // Heard until now: driven from here.
    rt.shadowState.reset();
    if (!scripts && !in->locoScripts.isEmpty())
        scripts = std::make_shared<LocoScripts>(in->locoScripts);
    auto d = std::make_shared<Driven>();
    setUpVehicles(train, *d, scripts);
    rt.drivenState = d;
    // Its cab set to run the way the train runs; throttle and brakes off: a
    // running train coasts.
    if (d->cab >= 0)
        d->models[size_t(d->cab)]->command(
                {"direction", double(facing(rt.vehicles[size_t(d->cab)], s.reversed))});
    // The commands sent before.
    for (const auto &[vehicle, message] : drivenPending) {
        const int to = vehicle >= 0 ? vehicle : d->cab;
        if (to >= 0 && size_t(to) < d->models.size())
            d->models[size_t(to)]->command(message);
    }
    drivenPending.clear();
    s.status = TrainStatus::Running;
    s.stoppedAt = StopKind::None;
    s.blockedBy = NoHolder;
    s.deadlocked = false;
    record(now, train, TrainEvent::DrivingStarted);
}

void SimulationCore::setStepLength(double seconds) {
    // TSRE_SIM_STEP holds every run at its step.
    if (qEnvironmentVariableIsSet("TSRE_SIM_STEP") || std::fabs(seconds - stepSeconds) < 1e-12)
        return;
    origin = now;
    ticks = 0;
    stepSeconds = std::clamp(seconds, 0.001, MaxStep);
}

void SimulationCore::leave(int train) {
    if (train < 0 || train != driven)
        return;
    driven = -1;
    drivenPending.clear();
    setStepLength(in->step);
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    if (rt.drivenState) {
        s.front = rt.drivenState->physics.cars.front().front;
        s.speed = float(std::max(0.0, rt.drivenState->physics.cars.front().speed));
        rt.drivenState.reset();
        // The AI drives on from here: its stops and waiting points behind
        // it are passed (advance skips them).
        s.status = TrainStatus::Running;
        s.stoppedAt = StopKind::None;
        record(now, train, TrainEvent::DrivingLeft);
    }
}

void SimulationCore::command(int train, int vehicle, const Message &message) {
    if (train < 0 || size_t(train) >= runtime.size())
        return;
    // To be driven, not yet taken over: kept for then.
    if (train == driven && !runtime[size_t(train)].drivenState) {
        drivenPending.push_back({vehicle, message});
        return;
    }
    if (!runtime[size_t(train)].drivenState)
        return;
    Driven &d = *runtime[size_t(train)].drivenState;
    const int to = vehicle >= 0 ? vehicle : d.cab;
    if (to >= 0 && size_t(to) < d.models.size())
        d.models[size_t(to)]->command(message);
}

const VehicleOutputs *SimulationCore::outputsOf(int train, int vehicle) const {
    if (train < 0 || size_t(train) >= runtime.size())
        return nullptr;
    const Driven *d = vehiclesOfTrain(runtime[size_t(train)].drivenState, runtime[size_t(train)].shadowState);
    if (d == nullptr)
        return nullptr;
    const int of = vehicle >= 0 ? vehicle : d->cab;
    return of >= 0 && size_t(of) < d->outputs.size() ? &d->outputs[size_t(of)] : nullptr;
}

int SimulationCore::cabOf(int train) const {
    if (train < 0 || size_t(train) >= runtime.size())
        return -1;
    const Driven *d = vehiclesOfTrain(runtime[size_t(train)].drivenState, runtime[size_t(train)].shadowState);
    return d != nullptr ? d->cab : -1;
}

std::vector<std::pair<int, Message>> SimulationCore::takeMessages(int train) {
    std::vector<std::pair<int, Message>> taken;
    if (train < 0 || size_t(train) >= runtime.size())
        return taken;
    Runtime &rt = runtime[size_t(train)];
    if (rt.drivenState)
        taken.swap(rt.drivenState->messages);
    else if (rt.shadowState)
        taken.swap(rt.shadowState->d.messages);
    return taken;
}

bool SimulationCore::drivenEnds(int train, std::vector<TrackPose> &fronts, std::vector<TrackPose> &rears) const {
    const Runtime &rt = runtime[size_t(train)];
    if (!rt.drivenState)
        return false;
    const TrainState &s = states[size_t(train)];
    const std::vector<TrainPhysics::Car> &cars = rt.drivenState->physics.cars;
    const size_t n = cars.size();
    for (size_t v = 0; v < n; ++v) {
        // Not reversed: the vehicle's front is its car's; reversed, the
        // consist runs backwards: its front at its car's back.
        const TrainPhysics::Car &car = cars[s.reversed ? n - 1 - v : v];
        const double frontAt = s.reversed ? car.front - car.length : car.front;
        const double rearAt = s.reversed ? car.front : car.front - car.length;
        TrackPose front, rear;
        pose(train, frontAt, front);
        pose(train, rearAt, rear);
        if (s.reversed) {
            front.roll = -front.roll;
            rear.roll = -rear.roll;
        }
        fronts.push_back(front);
        rears.push_back(rear);
    }
    return true;
}

void SimulationCore::stepVehicles(int train, Driven &d, double dt) {
    const TrainState &s = states[size_t(train)];
    const Runtime &rt = runtime[size_t(train)];
    const size_t n = rt.vehicles.size();
    std::vector<TrainPhysics::Forces> &forces = d.forces;
    forces.assign(n, TrainPhysics::Forces());
    // The outside air at its front's height (task driving 06).
    double air = in->air.at(now, 0.0);
    if (TrackPose head; n > 0 && pose(train, d.physics.cars[0].front, head))
        air = in->air.at(now, head.y);
    // Its vehicles' models, with the brake pipe at each.
    for (size_t r = 0; r < n; ++r) {
        const size_t v = s.reversed ? n - 1 - r : r;
        const Vehicle &vehicle = rt.vehicles[v];
        TrainPhysics::Car &car = d.physics.cars[r];
        const int face = facing(vehicle, s.reversed);
        // The slope under it, from the heights at its ends (+ rising the way
        // the train runs).
        double slope = 0.0;
        TrackPose ahead, behind;
        if (car.length > 0.1 && pose(train, car.front, ahead) && pose(train, car.front - car.length, behind))
            slope = std::clamp(double(ahead.y - behind.y) / car.length, -0.2, 0.2);
        VehicleInputs input;
        input.dt = dt;
        input.speed = car.speed * face;
        input.gradient = slope * face;
        input.adhesionLimit = car.mass * Gravity * Adhesion;
        input.lineVoltage = in->lineVoltage;
        input.airTemperature = air;
        if (d.heating.active()) {
            input.heatingPipe = d.heating.vehicles[v].pressure;
            input.heatingSteam = d.heating.vehicles[v].supplied;
        }
        input.brakePipe = d.pipe.pressure[r];
        input.brakePipeVolume = d.pipe.volume[r];
        const bool twin = d.brakes[v].twinPipe();
        input.mainPipe = twin ? d.mainPipe.pressure[r] : 0.0;
        input.brakeCylinder = d.brakes[v].cylinder();
        input.cab = int(v) == d.cab;
        VehicleOutputs &out = d.outputs[v];
        out.events.clear();
        out.line.clear();
        out.mainPipeAir = 0.0;
        out.heatingSupply = 0.0;
        d.models[v]->step(input, out);
        d.heatingSupply[v] = out.heatingSupply;
        // What Open Rails' sounds read of an engine's axles (task sound 02):
        // its force pulling the way it runs (- braking dynamically) and its
        // power.
        if (d.models[v]->drives()) {
            const double sign = input.speed > 0.0 ? 1.0 : input.speed < 0.0 ? -1.0 : 0.0;
            out.values.insert("tractiveEffort", out.motiveForce * sign);
            out.values.insert("tractivePower", out.motiveForce * input.speed);
        }
        for (const Message &m : out.events)
            d.messages.push_back({int(v), m});
        d.pipe.pressure[r] = std::max(0.0, d.pipe.pressure[r] + out.brakePipeAir / d.pipe.volume[r]);
        if (twin)
            d.mainPipe.pressure[r] = std::max(0.0, d.mainPipe.pressure[r] + out.mainPipeAir / d.mainPipe.volume[r]);
        forces[r].drive = out.motiveForce * face - car.mass * Gravity * slope;
        // Its resistance as its file gives it (Davis), the leading car's air
        // drag the larger.
        const double speed = std::fabs(car.speed);
        const double drag = r == 0 ? vehicle.davisLeading() : vehicle.davis(2);
        const double resistance = vehicle.davis(0) + vehicle.davis(1) * speed + drag * speed * speed;
        forces[r].friction = std::max(0.0, out.brakeForce) + std::max(0.0, resistance);
    }
    // The control line: each unit's messages to the others, the EP wire's
    // demand kept. A direction is the sender's: a unit facing the other way
    // gets it turned.
    for (size_t v = 0; v < n; ++v)
        for (const Message &m : d.outputs[v].line) {
            if (m.name == "epBrake")
                d.ep = m.number(-1.0);
            for (size_t w = 0; w < n; ++w) {
                if (w == v)
                    continue;
                if (m.name == "direction" && rt.vehicles[w].flip != rt.vehicles[v].flip)
                    d.models[w]->fromLine({m.name, -m.number()});
                else
                    d.models[w]->fromLine(m);
            }
        }
    // The air brakes on the pipe (the EP wire only with an EP cab, as Open
    // Rails has it), then the air along the pipe.
    const double ep = d.cab >= 0 && d.brakes[size_t(d.cab)].ep() ? d.ep : -1.0;
    for (size_t r = 0; r < n; ++r) {
        const size_t v = s.reversed ? n - 1 - r : r;
        AirBrake &brake = d.brakes[v];
        brake.step(dt, d.pipe.pressure[r], ep, brake.twinPipe() ? &d.mainPipe.pressure[r] : nullptr);
        brake.independent(dt, d.outputs[v].independentBrake, d.outputs[v].bailOff);
        forces[r].friction += brake.force(d.physics.cars[r].speed);
    }
    d.pipe.step(dt);
    d.mainPipe.step(dt);
    for (size_t r = 0; r < n; ++r) {
        const size_t v = s.reversed ? n - 1 - r : r;
        d.outputs[v].values.insert("brakePipe", d.pipe.pressure[r]);
        if (d.brakes[v].twinPipe())
            d.outputs[v].values.insert("mainPipe", d.mainPipe.pressure[r]);
        d.outputs[v].values.insert("brakeCylinder", d.brakes[v].cylinder());
        d.outputs[v].values.insert("auxiliaryReservoir", d.brakes[v].auxiliary());
        if (d.brakes[v].valve() == AirBrake::Valve::Distributor)
            d.outputs[v].values.insert("controlReservoir", d.brakes[v].controlReservoir());
    }
    stepHeating(train, d, dt, air);
}

void SimulationCore::stepHeating(int train, Driven &d, double dt, double air) {
    d.heatingClock += dt;
    if (d.heatingClock < SteamHeating::Step - 1e-9)
        return;
    const double step = d.heatingClock;
    d.heatingClock = 0.0;
    if (d.cab >= 0)
        d.outputs[size_t(d.cab)].values.insert(QStringLiteral("airTemperature"), air);
    SteamHeating &h = d.heating;
    if (!h.active())
        return;
    const TrainState &s = states[size_t(train)];
    const bool doorsOpen = s.status == TrainStatus::Stopped && s.stoppedAt == StopKind::Station;
    h.step(step, d.heatingSupply, air, std::fabs(double(s.speed)), doorsOpen);
    const bool driven = runtime[size_t(train)].drivenState.get() == &d;
    // Its values: the pipe's pressure on each vehicle it reaches, each
    // heated car's air and valve; on the cab's vehicle the pressure at the
    // last heated car the pipe feeds (else its end) and the coldest car; the
    // steam the feeding vehicle gives; what shows of the leaks and traps.
    static const QString pipe = QStringLiteral("heatingPipe"), compartment = QStringLiteral("compartmentTemperature"),
                         valve = QStringLiteral("heatingValve"), steam = QStringLiteral("heatingSteam");
    static const QString fx[3][4] = {
            {exhaustValueName("heatinghose", "flow"), exhaustValueName("heatinghose", "speed"),
             exhaustValueName("heatinghose", "temperature"), exhaustValueName("heatinghose", "steam")},
            {exhaustValueName("heatingmainpipesteamtrap", "flow"), exhaustValueName("heatingmainpipesteamtrap", "speed"),
             exhaustValueName("heatingmainpipesteamtrap", "temperature"),
             exhaustValueName("heatingmainpipesteamtrap", "steam")},
            {exhaustValueName("heatingcompartmentsteamtrap", "flow"),
             exhaustValueName("heatingcompartmentsteamtrap", "speed"),
             exhaustValueName("heatingcompartmentsteamtrap", "temperature"),
             exhaustValueName("heatingcompartmentsteamtrap", "steam")}};
    const auto show = [](QHash<QString, MessageValue> &values, const QString (&names)[4], double kgps, double speed) {
        values.insert(names[0], kgps * SteamVolume);
        values.insert(names[1], kgps > 0.0 ? speed : 0.0);
        values.insert(names[2], kgps > 0.0 ? 100.0 : 0.0);
        values.insert(names[3], kgps > 0.0 ? 1.0 : 0.0);
    };
    double coldest = 1e9, end = 0.0;
    for (size_t v = 0; v < h.vehicles.size(); ++v) {
        const SteamHeating::Vehicle &hv = h.vehicles[v];
        QHash<QString, MessageValue> &values = d.outputs[v].values;
        if (hv.spec.pipe)
            values.insert(pipe, hv.pressure);
        if (hv.spec.heated) {
            values.insert(compartment, hv.air);
            values.insert(valve, hv.valve);
            coldest = std::min(coldest, hv.air);
            // Told on the driven train, by the car's number in it.
            char &cold = d.heatingCold[v];
            if (!cold && hv.air < TooCold) {
                cold = 1;
                if (driven)
                    d.messages.push_back({int(v), {QStringLiteral("carTooCold"), double(v + 1)}});
            } else if (cold && hv.air > WarmAgain) {
                cold = 0;
                if (driven)
                    d.messages.push_back({int(v), {QStringLiteral("carWarm"), double(v + 1)}});
            }
        }
        if (int(v) == h.source)
            values.insert(steam, hv.supplied);
        else if (values.contains(steam))
            values.insert(steam, 0.0);
        if (hv.spec.hoseEmitter)
            show(values, fx[0], hv.hoseLeak, LeakSpeed);
        if (hv.spec.pipeTrapEmitter)
            show(values, fx[1], hv.pipeTrap, TrapSpeed);
        if (hv.spec.radiatorTrapEmitter)
            show(values, fx[2], hv.radiatorTrap, TrapSpeed);
    }
    if (h.source >= 0) {
        // Away from the feeding vehicle: its last heated car, else its end.
        const int away = h.last - h.source >= h.source - h.first ? h.last : h.first;
        const int dir = away >= h.source ? -1 : 1;
        int at = away;
        while (at != h.source && !h.vehicles[size_t(at)].spec.heated)
            at += dir;
        end = h.vehicles[size_t(at == h.source ? away : at)].pressure;
    }
    if (d.cab >= 0) {
        QHash<QString, MessageValue> &values = d.outputs[size_t(d.cab)].values;
        values.insert(QStringLiteral("heatingEnd"), end);
        if (coldest < 1e9)
            values.insert(QStringLiteral("coldestCompartment"), coldest);
    }
}

void SimulationCore::advanceDriven(int train, double from, double to) {
    TrainState &s = states[size_t(train)];
    if (s.status == TrainStatus::Finished || s.status == TrainStatus::Waiting)
        return;
    Runtime &rt = runtime[size_t(train)];
    Driven &d = *rt.drivenState;
    const double dt = to - from;
    if (dt <= 0.0)
        return;
    extendAuthority(train);
    stepVehicles(train, d, dt);
    d.physics.step(dt, d.forces);
    std::vector<TrainPhysics::Car> &cars = d.physics.cars;
    const double partLength = rt.parts[size_t(s.part)].length();
    // The end of its path: its track ends there for it (the track beyond
    // comes later); it stops there.
    if (cars.front().front > partLength) {
        const double back = cars.front().front - partLength;
        for (TrainPhysics::Car &car : cars) {
            car.front -= back;
            car.speed = 0.0;
        }
        if (!d.endReported)
            record(to, train, TrainEvent::EndOfPath);
        d.endReported = true;
    } else if (cars.front().front < partLength - 1.0) {
        d.endReported = false;
    }
    // Backing beyond its part's start: held there.
    const double rearMost = cars.back().front - cars.back().length;
    if (rearMost < 0.0)
        for (TrainPhysics::Car &car : cars) {
            car.front -= rearMost;
            car.speed = std::max(0.0, car.speed);
        }
    const double before = s.front;
    const float speedBefore = s.speed;
    s.front = cars.front().front;
    s.speed = float(cars.front().speed);
    s.acceleration = float((s.speed - speedBefore) / dt);
    if (s.front > before)
        sweep(train, before, s.front);
    s.travelled += std::fabs(s.front - before);
    // Past the end of its authority: told once (it is not stopped).
    if (s.front > s.authority + 1.0) {
        if (!d.authorityReported)
            record(to, train, TrainEvent::PassedAuthority);
        d.authorityReported = true;
    } else {
        d.authorityReported = false;
    }
    drivenStops(train, to);
    drivenReversal(train, to);
    drivenContact(train, to);
    releaseBehind(train);
}

void SimulationCore::drivenStops(int train, double t) {
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    Driven &d = *rt.drivenState;
    // Leaving the stop it stood at.
    if (d.arrivedAt >= 0 && std::fabs(s.speed) > Moving) {
        const int left = d.arrivedAt;
        d.arrivedAt = -1;
        record(t, train, TrainEvent::Departed, left);
        activate(train, TrainPlan::Activation::Departure, left, t);
        s.nextStop = std::max(s.nextStop, left + 1);
    }
    // Stops it ran past.
    while (s.nextStop < int(rt.stops.size())) {
        const StopPlan &stop = rt.stops[size_t(s.nextStop)];
        if (stop.part > s.part || (stop.part == s.part && stop.platformEnd >= s.front - PassedStop))
            break;
        if (d.arrivedAt == s.nextStop)
            break;
        ++s.nextStop;
    }
    // Standing at the next stop's platform: arrived.
    if (d.arrivedAt < 0 && std::fabs(s.speed) <= TrainPhysics::Standing && s.nextStop < int(rt.stops.size())) {
        const StopPlan &stop = rt.stops[size_t(s.nextStop)];
        if (stop.part == s.part && s.front >= stop.platformStart && s.front <= stop.platformEnd + PassedStop) {
            d.arrivedAt = s.nextStop;
            record(t, train, TrainEvent::Arrived, s.nextStop);
            activate(train, TrainPlan::Activation::Arrival, s.nextStop, t);
        }
    }
}

void SimulationCore::drivenReversal(int train, double t) {
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    Driven &d = *rt.drivenState;
    if (d.cab < 0 || s.part + 1 >= int(rt.parts.size()) || std::fabs(s.speed) > TrainPhysics::Standing)
        return;
    const double partLength = rt.parts[size_t(s.part)].length();
    if (partLength - s.front > ReversalReach)
        return;
    // Its driver set the other direction.
    const auto direction = d.outputs[size_t(d.cab)].values.constFind("direction");
    if (direction == d.outputs[size_t(d.cab)].values.cend())
        return;
    if (numberOf(*direction) * facing(rt.vehicles[size_t(d.cab)], s.reversed) >= 0.0)
        return;
    // The old rear leads on the next part (as advance reverses a train).
    const double shortOfEnd = std::max(0.0, partLength - s.front);
    releaseAll(train);
    ++s.part;
    s.reversed = !s.reversed;
    s.front = std::min(rt.length + shortOfEnd, rt.parts[size_t(s.part)].length());
    record(t, train, TrainEvent::Reversed);
    occupy(train);
    turnRound(d);
    d.physics.placeFrom(s.front);
    d.endReported = false;
}

void SimulationCore::drivenContact(int train, double t) {
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    if (size_t(s.part) >= rt.ways.size() || crash.time >= 0.0)
        return;
    Driven &d = *rt.drivenState;
    // The trains and standing cars in the sections at its front and the two
    // beyond: their ends on its way.
    const std::vector<ProjectedSection> &sections = rt.ways[size_t(s.part)].sections;
    size_t at = 0;
    while (at < sections.size() && sections[at].leave < s.front)
        ++at;
    double contact = -1.0;
    int other = -1;
    bool standingOther = false;
    for (size_t k = at; k < sections.size() && k < at + 3; ++k) {
        const int section = sections[k].section;
        if (section < 0 || size_t(section) >= holder.size())
            continue;
        const int holding = holder[size_t(section)];
        if (holding >= 0 && holding != train && size_t(holding) < states.size()) {
            const double c = contactWith(train, holding);
            if (c >= 0.0 && (contact < 0.0 || c < contact)) {
                contact = c;
                other = holding;
                standingOther = false;
            }
        }
        for (int k2 : standingIn(section)) {
            const double c = contactOn(train, k2);
            if (c >= 0.0 && (contact < 0.0 || c < contact)) {
                contact = c;
                other = k2;
                standingOther = true;
            }
        }
    }
    if (contact < 0.0 || s.front < contact - 0.01)
        return;
    const double speed = std::fabs(s.speed)
            + (standingOther || other < 0 ? 0.0 : std::fabs(double(states[size_t(other)].speed)));
    if (speed > CouplingSpeed) {
        // Run into: the run ends here.
        crash.time = t;
        crash.train = train;
        crash.other = other;
        crash.standing = standingOther;
        crash.speed = speed;
        record(t, train, TrainEvent::Collided);
    }
    // It stands at the contact (coupling a driven train comes later).
    const double back = s.front - contact;
    for (TrainPhysics::Car &car : d.physics.cars) {
        car.front -= back;
        car.speed = 0.0;
    }
    s.front = contact;
    s.speed = 0.0f;
}

void SimulationCore::hear(const std::vector<int> &trains) {
    heard.assign(states.size(), 0);
    for (int t : trains)
        if (t >= 0 && size_t(t) < heard.size())
            heard[size_t(t)] = 1;
}

std::vector<int> SimulationCore::shadowed() const {
    std::vector<int> trains;
    for (size_t t = 0; t < runtime.size(); ++t)
        if (runtime[t].shadowState)
            trains.push_back(int(t));
    return trains;
}

unsigned long long SimulationCore::shadowVersion(int train) const {
    if (train < 0 || size_t(train) >= runtime.size() || !runtime[size_t(train)].shadowState)
        return 0;
    return runtime[size_t(train)].shadowState->version;
}

void SimulationCore::stepShadows(double t) {
    for (size_t i = 0; i < runtime.size(); ++i) {
        Runtime &rt = runtime[i];
        const TrainStatus status = states[i].status;
        const bool wanted = i < heard.size() && heard[i] && !prerunning && int(i) != driven
                && (status == TrainStatus::Running || status == TrainStatus::Stopped);
        // Not heard, or its vehicles changed (task 07): its shadow goes.
        if (rt.shadowState && (!wanted || rt.shadowState->carsVersion != rt.carsVersion))
            rt.shadowState.reset();
        if (!wanted)
            continue;
        if (!rt.shadowState) {
            if (!shadowScripts && !in->locoScripts.isEmpty())
                shadowScripts = std::make_shared<LocoScripts>(in->locoScripts, 1);
            auto shadow = std::make_shared<Shadow>();
            setUpVehicles(int(i), shadow->d, shadowScripts);
            shadow->last = now;
            shadow->lastSpeed = std::max(0.0, double(states[i].speed));
            shadow->reversed = states[i].reversed;
            shadow->carsVersion = rt.carsVersion;
            shadow->standing = shadow->lastSpeed <= TrainPhysics::Standing;
            Driven &d = shadow->d;
            if (d.cab >= 0) {
                const VehicleDefinition &cabDef = *rt.vehicles[size_t(d.cab)].definition;
                shadow->steam = cabDef.wagonTypeId == 6;
                // A dynamic brake lever, as its script reads one (a step).
                shadow->dynamic = cabDef.number("engine(enginecontrollers(brake_dynamic", 2, 0.0) > 0.0;
                d.models[size_t(d.cab)]->command(
                        {"direction", double(facing(rt.vehicles[size_t(d.cab)], states[i].reversed))});
            }
            rt.shadowState = shadow;
        }
        stepShadow(int(i), t);
    }
}

void SimulationCore::stepShadow(int train, double t) {
    const TrainState &s = states[size_t(train)];
    const Runtime &rt = runtime[size_t(train)];
    Shadow &sh = *rt.shadowState;
    const double dt = t - sh.last;
    if (dt < ShadowStep - 1e-9)
        return;
    Driven &d = sh.d;
    VehicleModel *cab = d.cab >= 0 ? d.models[size_t(d.cab)].get() : nullptr;
    const int face = d.cab >= 0 ? facing(rt.vehicles[size_t(d.cab)], s.reversed) : 1;
    // Reversed by the AI: its cab set to run the other way.
    if (s.reversed != sh.reversed) {
        turnRound(d);
        sh.reversed = s.reversed;
        if (cab != nullptr)
            cab->command({"direction", double(face)});
    }
    // Its cars along the AI train, at its speed.
    const double speed = std::max(0.0, double(s.speed));
    for (TrainPhysics::Car &car : d.physics.cars)
        car.speed = speed;
    d.physics.placeFrom(s.front);
    if (cab != nullptr) {
        // The driver: the force the AI's motion over the step needs (its
        // mass × acceleration, its resistance and the gradient), as a share
        // of the full force at the speed, or of its braking.
        const Performance &p = rt.performance;
        const double a = (speed - sh.lastSpeed) / dt;
        const double full = fullForce(train, speed);
        double pull = 0.0, braking = 0.0;
        if (p.mass > 0.0 && full > 0.0) {
            const double needed = p.mass * a + full - p.mass * fullAcceleration(train, speed);
            if (needed > 0.0)
                pull = needed / full;
            else
                braking = -needed / (p.mass * std::max(0.1, double(rt.deceleration)));
        } else if (a > 0.0) {
            pull = a / std::max(0.1, double(rt.acceleration));
        } else {
            braking = -a / std::max(0.1, double(rt.deceleration));
        }
        const bool standing = speed <= TrainPhysics::Standing && a <= 0.0;
        const bool dynamic = sh.dynamic && speed > DynamicFrom;
        double throttle = standing ? 0.0 : lever(pull);
        double dynamicBrake = 0.0, brakeDemand = 0.0;
        // Braking: the dynamic brake first where it has one, for the first
        // half; standing, the train brake held.
        if (standing) {
            brakeDemand = HeldBrake;
        } else if (dynamic) {
            dynamicBrake = lever(2.0 * braking);
            brakeDemand = lever(2.0 * braking - 1.0);
        } else {
            brakeDemand = lever(braking);
        }
        if (throttle > 0.0)
            dynamicBrake = brakeDemand = 0.0;
        // A steam loco: full gear from a stand, notched up with speed.
        const double cutoff = face * std::round((FullCutoff - (FullCutoff - RunningCutoff)
                * std::min(1.0, speed / NotchedUpAt)) / LeverStep) * LeverStep;
        // Moved as a driver moves them: at most once a pace.
        const bool changed = throttle != sh.throttle || brakeDemand != sh.brakeDemand
                || (sh.dynamic && dynamicBrake != sh.dynamicBrake) || (sh.steam && cutoff != sh.reverser);
        if (changed && t - sh.moved >= LeverPace - 1e-9) {
            if (sh.steam && cutoff != sh.reverser)
                cab->command({"reverser", cutoff});
            if (throttle != sh.throttle)
                cab->command({"throttle", throttle});
            if (sh.dynamic && dynamicBrake != sh.dynamicBrake)
                cab->command({"dynamicBrake", dynamicBrake});
            if (brakeDemand != sh.brakeDemand)
                cab->command({"brakeDemand", brakeDemand});
            sh.throttle = throttle;
            sh.dynamicBrake = dynamicBrake;
            sh.brakeDemand = brakeDemand;
            sh.reverser = cutoff;
            sh.moved = t;
        }
        // Its cylinder cocks open starting from a stand, for the first
        // metres.
        if (sh.steam) {
            if (sh.standing && speed > TrainPhysics::Standing) {
                sh.standing = false;
                sh.cocks = true;
                sh.cocksFrom = s.travelled;
                cab->command({"cylinderCocks", true});
            } else if (sh.cocks && s.travelled - sh.cocksFrom > CocksOpenFor) {
                sh.cocks = false;
                cab->command({"cylinderCocks", false});
            }
            if (standing)
                sh.standing = true;
        }
        if (sh.hornUntil >= 0.0 && t >= sh.hornUntil - 1e-9) {
            sh.hornUntil = -1.0;
            cab->command({"horn", QString("off")});
        }
    }
    stepVehicles(train, d, dt);
    // Its sound triggers for its cars; a script's error told once.
    std::vector<std::pair<int, Message>> sent;
    sent.swap(d.messages);
    for (std::pair<int, Message> &m : sent) {
        if (m.second.name == QLatin1String("sound")) {
            d.messages.push_back(std::move(m));
        } else if (m.second.name == QLatin1String("scriptError") && !sh.told) {
            sh.told = true;
            qWarning().noquote() << "Simulation: a heard train's script:" << m.second.text();
        }
    }
    if (d.messages.size() > ShadowMessages)
        d.messages.erase(d.messages.begin(), d.messages.end() - ShadowMessages);
    sh.last = t;
    sh.lastSpeed = speed;
    sh.version = ++shadowSerial;
}

void SimulationCore::shadowHorn(int train, int value, double t) {
    Shadow &sh = *runtime[size_t(train)].shadowState;
    if (sh.d.cab < 0)
        return;
    // Open Rails' horn waiting points: 60010 + N sounds it N seconds.
    const WaitingPoint::Point point = WaitingPoint::decode(value);
    const double seconds = point.kind == WaitingPoint::Horn ? double(point.seconds) : CrossingHornSeconds;
    sh.d.models[size_t(sh.d.cab)]->command({"horn", QString("on")});
    sh.hornUntil = t + seconds;
}

}
