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
    states.resize(in->trains.size());
    runtime.resize(in->trains.size());
    for (size_t i = 0; i < states.size(); ++i)
        states[i].front = in->trains[i].startFront;
    const TrackNetwork *network = in->network.get();
    holder.assign(network != nullptr ? network->sections().size() : 0, NoHolder);
    // Loose consists hold the track they stand on for the whole run.
    if (network != nullptr)
        for (const StaticConsist &consist : in->staticConsists) {
            const RouteInterval *previous = nullptr;
            for (const RouteInterval &interval : consist.track) {
                if (previous != nullptr && previous->node != interval.node) {
                    const int junction = network->endSection(previous->node, previous->forward() ? 1 : 0);
                    if (junction >= 0)
                        holder[size_t(junction)] = StandingConsist;
                }
                const float low = std::min(interval.from, interval.to), high = std::max(interval.from, interval.to);
                const auto [first, count] = network->nodeSections(interval.node);
                for (int k = first; k < first + count; ++k) {
                    const TrackSection &section = network->sections()[size_t(k)];
                    if (section.to > low + 1e-3f && section.from < high - 1e-3f)
                        holder[size_t(k)] = StandingConsist;
                }
                previous = &interval;
            }
        }
    // The pre-run: all trains together from the first one's start up to the
    // activity's (Open Rails' pre-run of AI trains).
    now = in->startTime;
    double first = now;
    for (const TrainPlan &plan : in->trains)
        first = std::min(first, double(plan.startTime));
    if (first < now) {
        const double start = now;
        now = first;
        run(start - first);
        now = start;
    }
    stepCount = 0;
}

void SimulationCore::step(double seconds) {
    run(seconds);
}

void SimulationCore::run(double seconds) {
    double left = seconds;
    while (left > 1e-9) {
        const double h = std::min(MaxStep, left);
        for (size_t i = 0; i < states.size(); ++i)
            advance(int(i), now, now + h);
        now += h;
        left -= h;
        ++stepCount;
        findDeadlocks(now);
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
        // Its whole length on track no one else holds, else it waits.
        if (!occupy(train)) {
            blocked(train, t);
            return;
        }
        s.status = TrainStatus::Running;
        s.speed = 0.0f;
        s.blockedBy = NoHolder;
        runtime[size_t(train)].reported = Unreported;
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
            if (s.stoppedAt == StopKind::End)
                return;
            if (s.stoppedAt == StopKind::Blocked) {
                // Goes on when the track ahead is its own again.
                extendAuthority(train);
                if (s.authority < parts[size_t(s.part)].length() - 1e-6
                        && s.authority - AuthorityMargin <= s.front + AtTarget) {
                    blocked(train, t);
                    break;
                }
                s.status = TrainStatus::Running;
                s.stoppedAt = StopKind::None;
                s.blockedBy = NoHolder;
                s.deadlocked = false;
                runtime[size_t(train)].reported = Unreported;
                continue;
            }
            if (s.until > to)
                break;
            t = std::max(t, s.until);
            if (s.stoppedAt == StopKind::Reversal) {
                // The old rear leads, the train's length along the next part,
                // on the track it holds (given back and taken again).
                releaseAll(train);
                ++s.part;
                s.reversed = !s.reversed;
                s.front = std::min(double(plan.length), parts[size_t(s.part)].length());
                record(t, train, TrainEvent::Reversed);
                if (!occupy(train)) {
                    s.stoppedAt = StopKind::Blocked;
                    blocked(train, t);
                    break;
                }
            } else if (s.stoppedAt == StopKind::Station) {
                record(t, train, TrainEvent::Departed, s.nextStop - 1);
            }
            s.status = TrainStatus::Running;
            s.stoppedAt = StopKind::None;
            continue;
        }

        // The track ahead, as far as it can be had.
        extendAuthority(train);

        // The next target on this part: a station stop, a waiting point
        // that stops the train, else the part's end (a reversal or the path's
        // end), or the end of its authority short of that. Stops and points
        // left behind were passed.
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
        if (s.authority < parts[size_t(s.part)].length() - 1e-6) {
            const double limit = std::max(s.front, s.authority - AuthorityMargin);
            if (limit < target) {
                target = limit;
                kind = StopKind::Blocked;
                index = -1;
            }
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
            // AI trains leave, giving back their track; the player's train
            // stands at its path's end.
            if (!plan.player) {
                s.status = TrainStatus::Finished;
                releaseAll(train);
            }
            releaseBehind(train);
            return;
        case StopKind::Blocked:
            s.until = t;
            blocked(train, t);
            break;
        case StopKind::None:
            break;
        }
    }
    releaseBehind(train);
}

bool SimulationCore::occupy(int train) {
    const TrainPlan &plan = in->trains[size_t(train)];
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    const double partLength = plan.route.parts[size_t(s.part)].length();
    if (in->network == nullptr || size_t(s.part) >= plan.projection.size()) {
        s.authority = partLength;
        return true;
    }
    const std::vector<ProjectedSection> &way = plan.projection[size_t(s.part)].sections;
    const double rear = s.front - plan.length;
    size_t k = 0;
    while (k < way.size() && way[k].leave < rear - 1e-6)
        ++k;
    const size_t begin = k;
    double reach = s.front;
    while (k < way.size() && way[k].enter <= s.front + 1e-6) {
        const int h = holder[size_t(way[k].section)];
        if (h != NoHolder && h != train) {
            s.blockedBy = h;
            s.waitSection = way[k].section;
            return false;
        }
        reach = std::max(reach, way[k].leave);
        ++k;
    }
    for (size_t i = begin; i < k; ++i) {
        holder[size_t(way[i].section)] = train;
        rt.held.push_back({way[i].section, way[i].leave});
    }
    rt.next = k;
    s.authority = k >= way.size() ? partLength : reach;
    s.blockedBy = NoHolder;
    s.waitSection = -1;
    return true;
}

void SimulationCore::extendAuthority(int train) {
    const TrainPlan &plan = in->trains[size_t(train)];
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    const double partLength = plan.route.parts[size_t(s.part)].length();
    if (in->network == nullptr || size_t(s.part) >= plan.projection.size()) {
        s.authority = partLength;
        return;
    }
    const std::vector<ProjectedSection> &way = plan.projection[size_t(s.part)].sections;
    const std::vector<TrackSection> &sections = in->network->sections();
    const double lookahead = std::max(AutoNodeDistance,
                                      AutoNodeTime * std::min(double(plan.maxSpeed), double(in->routeSpeedLimit)));
    const double want = std::min(s.front + lookahead, partLength);
    s.blockedBy = NoHolder;
    s.waitSection = -1;
    while (s.authority < want - 1e-6 && rt.next < way.size()) {
        // A track section with the junctions before it and the crossings on
        // it, all or nothing.
        size_t end = rt.next;
        bool track = false;
        double reach = s.authority;
        while (end < way.size()) {
            if (sections[size_t(way[end].section)].kind == TrackSection::Track) {
                if (track)
                    break;
                track = true;
            }
            reach = std::max(reach, way[end].leave);
            ++end;
        }
        for (size_t k = rt.next; k < end; ++k) {
            const int h = holder[size_t(way[k].section)];
            if (h != NoHolder && h != train) {
                s.blockedBy = h;
                s.waitSection = way[k].section;
                return;
            }
        }
        for (size_t k = rt.next; k < end; ++k) {
            holder[size_t(way[k].section)] = train;
            rt.held.push_back({way[k].section, way[k].leave});
        }
        s.authority = std::max(s.authority, reach);
        rt.next = end;
    }
    if (rt.next >= way.size())
        s.authority = partLength;
}

void SimulationCore::releaseBehind(int train) {
    const TrainPlan &plan = in->trains[size_t(train)];
    const TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    const double rear = s.front - plan.length;
    while (!rt.held.empty() && rt.held.front().leave < rear - 1e-6) {
        const int section = rt.held.front().section;
        rt.held.pop_front();
        // Not while it holds it again further on (a loop passed twice).
        bool again = false;
        for (const Held &other : rt.held)
            again = again || other.section == section;
        if (!again && holder[size_t(section)] == train)
            holder[size_t(section)] = NoHolder;
    }
}

void SimulationCore::releaseAll(int train) {
    Runtime &rt = runtime[size_t(train)];
    for (const Held &held : rt.held)
        if (holder[size_t(held.section)] == train)
            holder[size_t(held.section)] = NoHolder;
    rt.held.clear();
    rt.next = 0;
    states[size_t(train)].authority = 0.0;
}

void SimulationCore::blocked(int train, double time) {
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    if (s.blockedBy == NoHolder || rt.reported == s.blockedBy)
        return;
    rt.reported = s.blockedBy;
    record(time, train, TrainEvent::Blocked, s.blockedBy);
}

void SimulationCore::findDeadlocks(double time) {
    auto waiting = [this](int train) {
        const TrainState &s = states[size_t(train)];
        return s.blockedBy >= 0
                && (s.status == TrainStatus::Waiting || (s.status == TrainStatus::Stopped && s.stoppedAt == StopKind::Blocked));
    };
    for (size_t i = 0; i < states.size(); ++i) {
        if (states[i].deadlocked || !waiting(int(i)))
            continue;
        // Follow who waits for whom; a ring back to this train is a deadlock.
        std::vector<int> ring = {int(i)};
        int next = states[i].blockedBy;
        while (next >= 0 && waiting(next) && ring.size() <= states.size()
               && std::find(ring.begin(), ring.end(), next) == ring.end()) {
            ring.push_back(next);
            next = states[size_t(next)].blockedBy;
        }
        if (next != int(i))
            continue;
        for (int member : ring) {
            states[size_t(member)].deadlocked = true;
            record(time, member, TrainEvent::Deadlock, states[size_t(member)].blockedBy);
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
    if (!in->geometry->pose(point.node, point.distance, out))
        return false;
    if (!point.forward)
        out.roll = -out.roll;
    return true;
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
        if (state.reversed) {
            front.roll = -front.roll;
            rear.roll = -rear.roll;
        }
        fronts.push_back(front);
        rears.push_back(rear);
        before += vehicle.length;
    }
    return true;
}

}
