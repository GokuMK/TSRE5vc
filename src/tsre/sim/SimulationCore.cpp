#include <tsre/sim/SimulationCore.h>

#include <algorithm>
#include <cmath>
#include <tsre/trains/WaitingPoint.h>

namespace Sim {

namespace {

// Closer than this to its target, a train is there.
constexpr double AtTarget = 0.01;
// A stop or waiting point more than this behind the front was passed.
constexpr double Passed = 0.5;

bool stops(const WaitPlan &wait) {
    const WaitingPoint::Kind kind = WaitingPoint::decode(wait.value).kind;
    return kind != WaitingPoint::Horn && kind != WaitingPoint::CrossingHorn;
}

// How long a waiting point holds a train arriving at `time`: until when.
double waitUntil(const WaitPlan &wait, double time) {
    const WaitingPoint::Point point = WaitingPoint::decode(wait.value);
    switch (point.kind) {
    case WaitingPoint::UntilTime:
        return std::max(time, double(point.hour * 3600 + point.minute * 60));
    default:
        // Seconds, and the uncoupling kinds' seconds (no uncoupling yet).
        return time + point.seconds;
    }
}

}

SimulationCore::SimulationCore(std::shared_ptr<const SimulationInput> input) : in(std::move(input)) {
    now = in->startTime;
    states.resize(in->trains.size());
    for (size_t i = 0; i < states.size(); ++i)
        states[i].front = in->trains[i].startFront;
    // The pre-run: trains that started before the activity, each alone (they
    // do not meet in this task), up to its start.
    for (size_t i = 0; i < states.size(); ++i) {
        double t = in->trains[i].startTime;
        while (t < now) {
            const double h = std::min(MaxStep, now - t);
            advance(int(i), t, t + h);
            t += h;
        }
    }
}

void SimulationCore::step(double seconds) {
    double left = seconds;
    while (left > 1e-9) {
        const double h = std::min(MaxStep, left);
        for (size_t i = 0; i < states.size(); ++i)
            advance(int(i), now, now + h);
        now += h;
        left -= h;
    }
}

std::vector<TrainEvent> SimulationCore::takeEvents() {
    std::vector<TrainEvent> taken;
    taken.swap(events);
    return taken;
}

void SimulationCore::record(double time, int train, TrainEvent::Kind kind, int index) {
    TrainEvent event;
    event.time = time;
    event.train = train;
    event.kind = kind;
    event.index = index;
    events.push_back(event);
}

bool SimulationCore::finished() const {
    for (const TrainState &state : states)
        if (state.status != TrainStatus::Finished && state.stoppedAt != StopKind::End)
            return false;
    return true;
}

void SimulationCore::advance(int train, double from, double to) {
    const TrainPlan &plan = in->trains[size_t(train)];
    TrainState &s = states[size_t(train)];
    if (s.status == TrainStatus::Finished)
        return;
    double t = from;
    if (s.status == TrainStatus::Waiting) {
        if (to < plan.startTime)
            return;
        t = std::max(from, double(plan.startTime));
        s.status = TrainStatus::Running;
        s.speed = 0.0f;
        record(t, train, TrainEvent::Started);
        // Starting at its first station (within a train's length of the
        // stop): it stands there until its departure.
        if (!plan.stops.empty() && plan.stops.front().part == s.part
                && std::fabs(plan.stops.front().front - s.front) <= plan.length) {
            const StopPlan &stop = plan.stops.front();
            s.status = TrainStatus::Stopped;
            s.stoppedAt = StopKind::Station;
            s.until = stop.departure >= 0 ? std::max(t, double(stop.departure)) : t + stop.boarding;
            s.nextStop = 1;
            record(t, train, TrainEvent::Arrived, 0);
        }
    }
    const std::vector<RouteTrack> &parts = plan.route.parts;
    while (t < to - 1e-9) {
        if (s.status == TrainStatus::Stopped) {
            if (s.stoppedAt == StopKind::End || s.until > to)
                return;
            t = std::max(t, s.until);
            if (s.stoppedAt == StopKind::Reversal) {
                // The old rear leads, the train's length along the next part.
                ++s.part;
                s.reversed = !s.reversed;
                s.front = std::min(double(plan.length), parts[size_t(s.part)].length());
                record(t, train, TrainEvent::Reversed);
            } else if (s.stoppedAt == StopKind::Station) {
                record(t, train, TrainEvent::Departed, s.nextStop - 1);
            }
            s.status = TrainStatus::Running;
            s.stoppedAt = StopKind::None;
            continue;
        }

        // The next target on this part: a station stop, a waiting point
        // that stops the train, else the part's end (a reversal or the path's
        // end). Stops and points left behind were passed.
        while (s.nextStop < int(plan.stops.size())) {
            const StopPlan &stop = plan.stops[size_t(s.nextStop)];
            if (stop.part > s.part || (stop.part == s.part && stop.front >= s.front - Passed))
                break;
            ++s.nextStop;
        }
        while (s.nextWait < int(plan.waits.size())) {
            const WaitPlan &wait = plan.waits[size_t(s.nextWait)];
            if (wait.part > s.part || (wait.part == s.part && wait.front >= s.front - Passed))
                break;
            ++s.nextWait;
        }
        double target = parts[size_t(s.part)].length();
        StopKind kind = s.part + 1 < int(parts.size()) ? StopKind::Reversal : StopKind::End;
        int index = -1;
        if (s.nextStop < int(plan.stops.size())) {
            const StopPlan &stop = plan.stops[size_t(s.nextStop)];
            if (stop.part == s.part && stop.front < target) {
                target = stop.front;
                kind = StopKind::Station;
                index = s.nextStop;
            }
        }
        for (int w = s.nextWait; w < int(plan.waits.size()); ++w) {
            const WaitPlan &wait = plan.waits[size_t(w)];
            if (wait.part != s.part)
                break;
            if (!stops(wait))
                continue;
            if (wait.front < target) {
                target = wait.front;
                kind = StopKind::Wait;
                index = w;
            }
            break;
        }

        const double distance = std::max(0.0, target - s.front);
        double arrival = -1.0;
        if (distance < AtTarget) {
            arrival = t;
        } else {
            // Up to the speed limit (the route's, and the train's top speed ×
            // its efficiency on this run), braking at constant deceleration
            // to stand at the target.
            const float efficiency = s.nextStop < int(plan.stops.size())
                    ? plan.stops[size_t(s.nextStop)].efficiency : plan.efficiency;
            const double limit = std::min(double(in->routeSpeedLimit), double(plan.maxSpeed * efficiency));
            const double dt = to - t;
            const double v = s.speed;
            double next = v > limit ? std::max(v - plan.deceleration * dt, limit)
                                    : std::min(v + plan.acceleration * dt, limit);
            // The fastest speed at the step's end from which it still stops
            // at the target: v'^2 = 2 d (distance - (v + v') dt / 2), so the
            // train follows the braking curve exactly.
            const double d = plan.deceleration;
            const double b = d * dt;
            const double c = d * dt * v - 2.0 * d * distance;
            const double disc = b * b - 4.0 * c;
            next = std::min(next, disc > 0.0 ? std::max(0.0, 0.5 * (-b + std::sqrt(disc))) : 0.0);
            const double moved = 0.5 * (v + next) * dt;
            if (moved >= distance - AtTarget) {
                arrival = t + (moved > 0.0 ? dt * std::min(1.0, distance / moved) : dt);
            } else {
                s.front += moved;
                s.travelled += moved;
                s.speed = float(next);
                t = to;
            }
        }
        // Horn waiting points passed: the horn sounds; the train runs on.
        const double reached = arrival >= 0.0 ? target : s.front;
        while (s.nextWait < int(plan.waits.size())) {
            const WaitPlan &wait = plan.waits[size_t(s.nextWait)];
            if (wait.part != s.part || wait.front > reached + 1e-6 || stops(wait))
                break;
            record(arrival >= 0.0 ? arrival : t, train, TrainEvent::Horn, s.nextWait);
            ++s.nextWait;
        }
        if (arrival < 0.0)
            continue;

        // At the target.
        s.travelled += target - s.front;
        s.front = target;
        s.speed = 0.0f;
        t = arrival;
        s.status = TrainStatus::Stopped;
        s.stoppedAt = kind;
        switch (kind) {
        case StopKind::Station: {
            const StopPlan &stop = plan.stops[size_t(index)];
            // It stands at least its boarding time, and leaves at the booked
            // departure when that is later (Open Rails).
            s.until = t + stop.boarding;
            if (stop.departure >= 0)
                s.until = std::max(s.until, double(stop.departure));
            s.nextStop = index + 1;
            record(t, train, TrainEvent::Arrived, index);
            break;
        }
        case StopKind::Wait:
            s.until = waitUntil(plan.waits[size_t(index)], t);
            s.nextWait = index + 1;
            record(t, train, TrainEvent::Waiting, index);
            break;
        case StopKind::Reversal:
            s.until = t;
            break;
        case StopKind::End:
            record(t, train, TrainEvent::Finished);
            // AI trains leave; the player's train stands at its path's end.
            if (!plan.player)
                s.status = TrainStatus::Finished;
            return;
        case StopKind::None:
            break;
        }
    }
}

bool SimulationCore::pose(int train, double routeDistance, TrackPose &out) const {
    if (train < 0 || size_t(train) >= states.size())
        return false;
    const TrainPlan &plan = in->trains[size_t(train)];
    const TrainState &state = states[size_t(train)];
    if (state.part < 0 || size_t(state.part) >= plan.route.parts.size())
        return false;
    const RoutePoint point = plan.route.parts[size_t(state.part)].locate(routeDistance);
    return in->geometry->pose(point.node, point.distance, out);
}

bool SimulationCore::vehicleEnds(int train, std::vector<TrackPose> &fronts, std::vector<TrackPose> &rears) const {
    fronts.clear();
    rears.clear();
    if (train < 0 || size_t(train) >= states.size())
        return false;
    const TrainPlan &plan = in->trains[size_t(train)];
    const TrainState &state = states[size_t(train)];
    if (state.status == TrainStatus::Waiting || state.status == TrainStatus::Finished)
        return false;
    double before = 0.0;
    for (const Vehicle &vehicle : plan.vehicles) {
        // Not reversed: the vehicle's front is `before` behind the train's
        // front. Reversed: the consist runs backwards, its first vehicle at
        // the train's rear.
        const double frontAt = state.reversed ? state.front - plan.length + before : state.front - before;
        const double rearAt = state.reversed ? frontAt + vehicle.length : frontAt - vehicle.length;
        TrackPose front, rear;
        pose(train, frontAt, front);
        pose(train, rearAt, rear);
        fronts.push_back(front);
        rears.push_back(rear);
        before += vehicle.length;
    }
    return true;
}

}
