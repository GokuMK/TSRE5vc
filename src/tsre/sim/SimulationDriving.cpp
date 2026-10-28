// A driven train in the simulation core (task driving 02): its vehicles'
// models give the forces, TrainPhysics moves its cars, the core keeps doing
// what it does for every train (the track ahead reserved, signals, stops,
// events) without stopping it anywhere.
#include <tsre/sim/SimulationCore.h>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <tsre/sim/AirBrake.h>
#include <tsre/sim/LocoScript.h>
#include <tsre/sim/TrainPhysics.h>
#include <tsre/sim/VehicleModel.h>
#include <tsre/trains/VehicleDefinition.h>

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

// The vehicle faces the way its train runs (+1) or the other (-1).
int facing(const Vehicle &vehicle, bool reversed) {
    return vehicle.flip != reversed ? -1 : 1;
}

double numberOf(const MessageValue &value) {
    return Message{QString(), value}.number();
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
    double ep = -1.0;
    // The vehicle (consist order) whose cab is driven.
    int cab = -1;
    // Messages sent, not yet taken.
    std::vector<std::pair<int, Message>> messages;
    bool endReported = false;
    bool authorityReported = false;
    // The stop it stands at, its arrival recorded; -1 none.
    int arrivedAt = -1;
};

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

void SimulationCore::takeOver(int train) {
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    auto d = std::make_shared<Driven>();
    const size_t n = rt.vehicles.size();
    int cabRun = -1;
    for (size_t v = 0; v < n; ++v) {
        const Vehicle &vehicle = rt.vehicles[v];
        // Its script's model, else the C++ one.
        if (!scripts && !in->locoScripts.isEmpty())
            scripts = std::make_shared<LocoScripts>(in->locoScripts);
        std::unique_ptr<VehicleModel> model = scripts ? scripts->make(vehicle.definition) : nullptr;
        d->models.push_back(model ? std::move(model) : MakeVehicleModel(*vehicle.definition));
        d->brakes.emplace_back(*vehicle.definition);
        const int run = int(s.reversed ? n - 1 - v : v);
        if (d->models.back()->drives() && (cabRun < 0 || run < cabRun)) {
            cabRun = run;
            d->cab = int(v);
        }
    }
    d->outputs.resize(n);
    const double speed = std::max(0.0, double(s.speed));
    for (size_t r = 0; r < n; ++r) {
        const Vehicle &vehicle = rt.vehicles[s.reversed ? n - 1 - r : r];
        TrainPhysics::Car car;
        car.mass = vehicle.mass() > 0.0 ? vehicle.mass() : MassPerMetre * vehicle.length();
        car.length = vehicle.length();
        car.speed = speed;
        d->physics.cars.push_back(car);
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
            d->physics.couplers.push_back(c);
        }
    }
    d->physics.placeFrom(s.front);
    // The brake pipe charged to its cab's full pressure, the brakes off.
    const VehicleDefinition *cabDef = d->cab >= 0 ? rt.vehicles[size_t(d->cab)].definition.get() : nullptr;
    const double full = cabDef ? BrakeSystemPressure(*cabDef) : BrakeSystemPressure(*VehicleDefinition::empty());
    d->pipe.timeFactor = cabDef ? BrakePipeTimeFactor(*cabDef) : BrakePipeTimeFactor(*VehicleDefinition::empty());
    for (size_t r = 0; r < n; ++r) {
        AirBrake &brake = d->brakes[s.reversed ? n - 1 - r : r];
        brake.charge(full);
        d->pipe.volume.push_back(brake.pipeVolume());
        d->pipe.pressure.push_back(full);
    }
    rt.drivenState = d;
    // Its cab set to run the way the train runs; throttle and brakes off: a
    // running train coasts.
    if (d->cab >= 0)
        d->models[size_t(d->cab)]->command(
                {"direction", double(facing(rt.vehicles[size_t(d->cab)], s.reversed))});
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
    if (train < 0 || size_t(train) >= runtime.size() || !runtime[size_t(train)].drivenState)
        return;
    Driven &d = *runtime[size_t(train)].drivenState;
    const int to = vehicle >= 0 ? vehicle : d.cab;
    if (to >= 0 && size_t(to) < d.models.size())
        d.models[size_t(to)]->command(message);
}

const VehicleOutputs *SimulationCore::outputsOf(int train, int vehicle) const {
    if (train < 0 || size_t(train) >= runtime.size() || !runtime[size_t(train)].drivenState)
        return nullptr;
    const Driven &d = *runtime[size_t(train)].drivenState;
    const int of = vehicle >= 0 ? vehicle : d.cab;
    return of >= 0 && size_t(of) < d.outputs.size() ? &d.outputs[size_t(of)] : nullptr;
}

int SimulationCore::cabOf(int train) const {
    if (train < 0 || size_t(train) >= runtime.size() || !runtime[size_t(train)].drivenState)
        return -1;
    return runtime[size_t(train)].drivenState->cab;
}

std::vector<std::pair<int, Message>> SimulationCore::takeMessages(int train) {
    std::vector<std::pair<int, Message>> taken;
    if (train >= 0 && size_t(train) < runtime.size() && runtime[size_t(train)].drivenState)
        taken.swap(runtime[size_t(train)].drivenState->messages);
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
    const size_t n = rt.vehicles.size();
    std::vector<TrainPhysics::Forces> forces(n);
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
        input.brakePipe = d.pipe.pressure[r];
        input.brakePipeVolume = d.pipe.volume[r];
        input.brakeCylinder = d.brakes[v].cylinder();
        input.cab = int(v) == d.cab;
        VehicleOutputs &out = d.outputs[v];
        out.events.clear();
        out.line.clear();
        d.models[v]->step(input, out);
        for (const Message &m : out.events)
            d.messages.push_back({int(v), m});
        d.pipe.pressure[r] = std::max(0.0, d.pipe.pressure[r] + out.brakePipeAir / d.pipe.volume[r]);
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
        brake.step(dt, d.pipe.pressure[r], ep);
        brake.independent(dt, d.outputs[v].independentBrake, d.outputs[v].bailOff);
        forces[r].friction += brake.force(d.physics.cars[r].speed);
    }
    d.pipe.step(dt);
    for (size_t r = 0; r < n; ++r) {
        const size_t v = s.reversed ? n - 1 - r : r;
        d.outputs[v].values.insert("brakePipe", d.pipe.pressure[r]);
        d.outputs[v].values.insert("brakeCylinder", d.brakes[v].cylinder());
    }
    d.physics.step(dt, forces);
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
    std::reverse(d.physics.cars.begin(), d.physics.cars.end());
    std::reverse(d.physics.couplers.begin(), d.physics.couplers.end());
    d.pipe.reverse();
    for (TrainPhysics::Car &car : d.physics.cars)
        car.speed = 0.0;
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

}
