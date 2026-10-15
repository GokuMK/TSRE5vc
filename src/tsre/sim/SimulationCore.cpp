#include <tsre/sim/SimulationCore.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
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
    // Each train's own copy of its route: a part changes when it takes a
    // passing path.
    for (size_t i = 0; i < runtime.size(); ++i) {
        const TrainPlan &plan = in->trains[i];
        Runtime &rt = runtime[i];
        rt.parts = plan.route.parts;
        rt.ways = plan.projection;
        rt.limits = plan.limits;
        rt.stops = plan.stops;
        rt.waits = plan.waits;
        rt.passing = plan.passing;
        rt.sides.resize(rt.passing.size());
    }
    for (size_t i = 0; i < states.size(); ++i)
        states[i].front = in->trains[i].startFront;
    const TrackNetwork *network = in->network.get();
    holder.assign(network != nullptr ? network->sections().size() : 0, NoHolder);
    // Switches in their main position until a train sets them.
    switchAt.assign(holder.size(), 0);
    if (network != nullptr)
        for (size_t k = 0; k < holder.size(); ++k)
            if (network->sections()[k].kind == TrackSection::Junction)
                switchAt[k] = network->sections()[k].mainPin;
    clearedFor.assign(network != nullptr ? network->signalGroups().size() : 0, NoHolder);
    headAspect.assign(network != nullptr ? network->heads().size() : 0, 0);
    headDraw.assign(headAspect.size(), 0);
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
    findStretches();
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
    if (aspectsDirty)
        evaluateAspects();
    stepCount = 0;
}

void SimulationCore::step(double seconds) {
    run(seconds);
}

void SimulationCore::run(double seconds) {
    double left = seconds;
    while (left > 1e-9) {
        const double h = std::min(MaxStep, left);
        // Trains due take their places first, in the order they are due, all
        // before any reserves ahead: those an activity puts on the track at
        // the same time are there together. One waits while a stretch it
        // would stand in was claimed for another train before.
        std::vector<int> due;
        for (size_t i = 0; i < states.size(); ++i)
            if (states[i].status == TrainStatus::Waiting && in->trains[i].startTime <= now + h)
                due.push_back(int(i));
        std::stable_sort(due.begin(), due.end(), [this](int a, int b) {
            return in->trains[size_t(a)].startTime < in->trains[size_t(b)].startTime;
        });
        std::vector<int> free;
        for (int i : due) {
            int blocker = NoHolder;
            if (startClaimed(i, &blocker)) {
                TrainState &s = states[size_t(i)];
                s.blockedBy = blocker;
                s.waitFor = WaitFor::SingleTrack;
                blocked(i, std::max(now, double(in->trains[size_t(i)].startTime)));
            } else {
                free.push_back(i);
            }
        }
        for (int i : free)
            start(i, now, now + h);
        for (size_t i = 0; i < states.size(); ++i)
            advance(int(i), now, now + h);
        now += h;
        left -= h;
        ++stepCount;
        findDeadlocks(now);
        if (aspectsDirty)
            evaluateAspects();
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

void SimulationCore::start(int train, double from, double to) {
    const TrainPlan &plan = in->trains[size_t(train)];
    TrainState &s = states[size_t(train)];
    if (s.status != TrainStatus::Waiting || to < plan.startTime)
        return;
    const double t = std::max(from, double(plan.startTime));
    if (!occupy(train)) {
        blocked(train, t);
        return;
    }
    s.status = TrainStatus::Running;
    s.speed = 0.0f;
    s.blockedBy = NoHolder;
    runtime[size_t(train)].reported = Unreported;
    record(t, train, TrainEvent::Started);
    // Starting at its first station (within a train's length of the stop):
    // it stands there until its departure.
    if (!runtime[size_t(train)].stops.empty() && runtime[size_t(train)].stops.front().part == s.part
            && std::fabs(runtime[size_t(train)].stops.front().front - s.front) <= plan.length) {
        const StopPlan &stop = runtime[size_t(train)].stops.front();
        s.status = TrainStatus::Stopped;
        s.stoppedAt = StopKind::Station;
        s.until = stop.departure >= 0 ? std::max(t, double(stop.departure)) : t + stop.boarding;
        s.nextStop = 1;
        record(t, train, TrainEvent::Arrived, 0);
    }
}

bool SimulationCore::startClaimed(int train, int *blocker) const {
    const TrainState &s = states[size_t(train)];
    if (in->network == nullptr || size_t(s.part) >= runtime[size_t(train)].ways.size())
        return false;
    const std::vector<ProjectedSection> &way = runtime[size_t(train)].ways[size_t(s.part)].sections;
    const double rear = s.front - in->trains[size_t(train)].length;
    size_t begin = 0;
    while (begin < way.size() && way[begin].leave < rear - 1e-6)
        ++begin;
    size_t end = begin;
    while (end < way.size() && way[end].enter <= s.front + 1e-6)
        ++end;
    return !enterable(train, begin, end, blocker);
}

void SimulationCore::advance(int train, double from, double to) {
    const TrainPlan &plan = in->trains[size_t(train)];
    TrainState &s = states[size_t(train)];
    if (s.status == TrainStatus::Finished || s.status == TrainStatus::Waiting)
        return;
    double t = std::max(from, double(plan.startTime));
    const std::vector<RouteTrack> &parts = runtime[size_t(train)].parts;
    while (t < to - 1e-9) {
        if (s.status == TrainStatus::Stopped) {
            if (s.stoppedAt == StopKind::End)
                return;
            if (s.stoppedAt == StopKind::Blocked) {
                // Goes on when the track ahead is its own again.
                extendAuthority(train);
                const double margin = runtime[size_t(train)].endsAtSignal ? SignalMargin : AuthorityMargin;
                if (s.authority < parts[size_t(s.part)].length() - 1e-6
                        && s.authority - margin <= s.front + AtTarget) {
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
        while (s.nextStop < int(runtime[size_t(train)].stops.size())) {
            const StopPlan &stop = runtime[size_t(train)].stops[size_t(s.nextStop)];
            if (stop.part > s.part || (stop.part == s.part && stop.front >= s.front - Passed))
                break;
            ++s.nextStop;
        }
        while (s.nextWait < int(runtime[size_t(train)].waits.size())) {
            const WaitPlan &wait = runtime[size_t(train)].waits[size_t(s.nextWait)];
            if (wait.part > s.part || (wait.part == s.part && wait.front >= s.front - Passed))
                break;
            ++s.nextWait;
        }
        double target = parts[size_t(s.part)].length();
        StopKind kind = s.part + 1 < int(parts.size()) ? StopKind::Reversal : StopKind::End;
        int index = -1;
        if (s.nextStop < int(runtime[size_t(train)].stops.size())) {
            const StopPlan &stop = runtime[size_t(train)].stops[size_t(s.nextStop)];
            if (stop.part == s.part && stop.front < target) {
                target = stop.front;
                kind = StopKind::Station;
                index = s.nextStop;
            }
        }
        for (int w = s.nextWait; w < int(runtime[size_t(train)].waits.size()); ++w) {
            const WaitPlan &wait = runtime[size_t(train)].waits[size_t(w)];
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
            const double margin = runtime[size_t(train)].endsAtSignal ? SignalMargin : AuthorityMargin;
            const double limit = std::max(s.front, s.authority - margin);
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
            // Up to the speed limit (the route's, the train's top speed × its
            // efficiency on this run, the line's limits along its whole
            // length, the last signal's speed), braking at constant
            // deceleration to stand at the target and to be down to each
            // lower limit ahead where it begins.
            const float efficiency = s.nextStop < int(runtime[size_t(train)].stops.size())
                    ? runtime[size_t(train)].stops[size_t(s.nextStop)].efficiency : plan.efficiency;
            double limit = std::min(double(in->routeSpeedLimit), double(plan.maxSpeed * efficiency));
            if (s.signalSpeed > 0.0f)
                limit = std::min(limit, double(s.signalSpeed));
            const double dt = to - t;
            const double v = s.speed;
            const double d = plan.deceleration;
            const double rear = s.front - plan.length;
            const double reach = v * v / (2.0 * d) + 2.0 * v * dt + 50.0;
            double ahead = 1e9;
            if (size_t(s.part) < runtime[size_t(train)].limits.size()) {
                const std::vector<SpeedLimit> &limits = runtime[size_t(train)].limits[size_t(s.part)];
                Runtime &rt = runtime[size_t(train)];
                while (rt.limit < limits.size() && limits[rt.limit].to <= rear)
                    ++rt.limit;
                for (size_t k = rt.limit; k < limits.size() && limits[k].from < s.front + reach; ++k) {
                    if (limits[k].from <= s.front)
                        limit = std::min(limit, double(limits[k].speed));
                    else
                        ahead = std::min(ahead, brakingSpeed(v, dt, d, limits[k].from - s.front, limits[k].speed));
                }
            }
            // The next stop signal's speed, where its aspect has one.
            if (size_t(s.part) < runtime[size_t(train)].ways.size()) {
                const std::vector<ProjectedSignal> &stops = runtime[size_t(train)].ways[size_t(s.part)].stopSignals;
                const size_t next = runtime[size_t(train)].passed;
                if (next < stops.size() && stops[next].at < s.front + reach) {
                    const float speed = signalSpeedOf(stops[next].signal);
                    if (speed > 0.0f)
                        ahead = std::min(ahead, brakingSpeed(v, dt, d, stops[next].at - s.front, speed));
                }
            }
            double next = v > limit ? std::max(v - plan.deceleration * dt, limit)
                                    : std::min(v + plan.acceleration * dt, limit);
            next = std::min(next, ahead);
            // The fastest speed at the step's end from which it still stops
            // at the target, so the train follows the braking curve exactly.
            next = std::min(next, brakingSpeed(v, dt, d, distance, 0.0));
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
        while (s.nextWait < int(runtime[size_t(train)].waits.size())) {
            const WaitPlan &wait = runtime[size_t(train)].waits[size_t(s.nextWait)];
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
            const StopPlan &stop = runtime[size_t(train)].stops[size_t(index)];
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
            s.until = waitUntil(runtime[size_t(train)].waits[size_t(index)], t);
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
    const double partLength = runtime[size_t(train)].parts[size_t(s.part)].length();
    if (in->network == nullptr || size_t(s.part) >= runtime[size_t(train)].ways.size()) {
        s.authority = partLength;
        return true;
    }
    const std::vector<ProjectedSection> &way = runtime[size_t(train)].ways[size_t(s.part)].sections;
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
            s.waitFor = WaitFor::Track;
            return false;
        }
        reach = std::max(reach, way[k].leave);
        ++k;
    }
    // A stretch it stands in is its own unless claimed already (the other
    // train then waits for it, or they meet in a deadlock).
    mayEnter(train, begin, k);
    take(train, begin, k);
    rt.next = k;
    // Signals under it and behind it are passed; the next to clear is the
    // first whose block it has not taken.
    const std::vector<ProjectedSignal> &stopSignals = runtime[size_t(train)].ways[size_t(s.part)].stopSignals;
    rt.passed = 0;
    while (rt.passed < stopSignals.size() && stopSignals[rt.passed].at < s.front)
        ++rt.passed;
    rt.nextSignal = 0;
    while (rt.nextSignal < stopSignals.size() && stopSignals[rt.nextSignal].section < k)
        ++rt.nextSignal;
    s.authority = k >= way.size() ? partLength : reach;
    s.blockedBy = NoHolder;
    s.waitSection = -1;
    return true;
}

void SimulationCore::extendAuthority(int train) {
    const TrainPlan &plan = in->trains[size_t(train)];
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    const double partLength = runtime[size_t(train)].parts[size_t(s.part)].length();
    if (in->network == nullptr || size_t(s.part) >= runtime[size_t(train)].ways.size()) {
        s.authority = partLength;
        return;
    }
    const Projection &projection = runtime[size_t(train)].ways[size_t(s.part)];
    const std::vector<ProjectedSection> &way = projection.sections;
    const std::vector<ProjectedSignal> &stopSignals = projection.stopSignals;
    const std::vector<TrackSection> &sections = in->network->sections();
    s.blockedBy = NoHolder;
    s.waitSection = -1;
    s.atSignal = -1;
    rt.endsAtSignal = false;
    // Short of the first stop signal ahead (or where there is none), it
    // reserves as Open Rails' Auto Node does; past it, the blocks of the
    // signals it may clear.
    const double lookahead = std::max(AutoNodeDistance,
                                      AutoNodeTime * std::min(double(plan.maxSpeed), double(in->routeSpeedLimit)));
    const double autoLimit = std::min(s.front + lookahead, partLength);
    const size_t allowed = rt.passed + clearable(projection, rt.passed);
    auto blockedAt = [&](size_t k) {
        const int h = holder[size_t(way[k].section)];
        if (h == NoHolder || h == train)
            return false;
        s.blockedBy = h;
        s.waitSection = way[k].section;
        s.waitFor = WaitFor::Track;
        return true;
    };
    while (rt.next < way.size()) {
        if (rt.nextSignal < stopSignals.size() && stopSignals[rt.nextSignal].section <= rt.next) {
            // A stop signal: cleared with its whole block, or not passed.
            const ProjectedSignal &signal = stopSignals[rt.nextSignal];
            if (rt.nextSignal >= allowed) {
                rt.endsAtSignal = true;
                break;
            }
            size_t blockEnd = rt.nextSignal + 1 < stopSignals.size()
                    ? std::max(stopSignals[rt.nextSignal + 1].section, rt.next) : way.size();
            const Passing choice = choosePassing(train, rt.next, blockEnd);
            if (choice == Passing::Taken) {
                extendAuthority(train);
                return;
            }
            if (choice == Passing::Wait) {
                rt.endsAtSignal = true;
                s.atSignal = signal.signal;
                break;
            }
            bool free = true;
            for (size_t k = rt.next; k < blockEnd && free; ++k)
                free = !blockedAt(k);
            if (!free || !mayEnter(train, rt.next, blockEnd)) {
                rt.endsAtSignal = true;
                s.atSignal = signal.signal;
                break;
            }
            take(train, rt.next, blockEnd);
            if (blockEnd > rt.next)
                s.authority = std::max(s.authority, way[blockEnd - 1].leave);
            rt.next = blockEnd;
            clearedFor[size_t(signal.signal)] = train;
            aspectsDirty = true;
            ++rt.nextSignal;
            continue;
        }
        // No signal here: a track section at a time (with the junctions
        // before it and the crossings on it), as far as Auto Node reaches.
        if (s.authority >= autoLimit - 1e-6)
            break;
        const size_t stop = rt.nextSignal < stopSignals.size() ? stopSignals[rt.nextSignal].section : way.size();
        const size_t end = chunkEnd(way, rt.next, stop);
        const Passing choice = choosePassing(train, rt.next, end);
        if (choice == Passing::Taken) {
            extendAuthority(train);
            return;
        }
        if (choice == Passing::Wait)
            break;
        bool free = true;
        for (size_t k = rt.next; k < end && free; ++k)
            free = !blockedAt(k);
        if (!free || !mayEnter(train, rt.next, end))
            break;
        take(train, rt.next, end);
        if (end > rt.next)
            s.authority = std::max(s.authority, way[end - 1].leave);
        rt.next = end;
    }
    if (rt.next >= way.size()) {
        s.authority = partLength;
        rt.endsAtSignal = false;
    }
}

double SimulationCore::brakingSpeed(double v, double dt, double d, double distance, double target) {
    // v'^2 = target^2 + 2 d (distance - (v + v') dt / 2).
    const double b = d * dt;
    const double disc = b * b + 4.0 * (target * target + 2.0 * d * distance - d * dt * v);
    return disc > 0.0 ? std::max(0.0, 0.5 * (-b + std::sqrt(disc))) : 0.0;
}

float SimulationCore::signalSpeedOf(int signal) const {
    if (in->network == nullptr || signal < 0)
        return -1.0f;
    const TrackNetwork &network = *in->network;
    int best = -1;
    float speed = -1.0f;
    for (int head : network.signalGroups()[size_t(signal)].heads) {
        const SignalHead &h = network.heads()[size_t(head)];
        if (!network.isNormal(h) || headAspect[size_t(head)] <= best)
            continue;
        best = headAspect[size_t(head)];
        speed = -1.0f;
        for (const SignalKind::Aspect &aspect : network.kinds()[size_t(h.kind)].aspects)
            if (aspect.aspect == best)
                speed = aspect.speed;
    }
    return speed;
}

void SimulationCore::evaluateAspects() {
    aspectsDirty = false;
    if (in->network == nullptr)
        return;
    const TrackNetwork &network = *in->network;
    const std::vector<SignalHead> &heads = network.heads();
    const std::vector<SignalGroup> &groups = network.signalGroups();
    const std::vector<SignalKind> &kinds = network.kinds();
    enum { Stop = 0, StopAndProceed = 1, Restricting = 2, Approach1 = 3, Approach2 = 4, Approach3 = 5, Clear2 = 7 };
    constexpr double Reach = 20000.0;
    const std::vector<int> drawnBefore = headDraw;
    const std::vector<int> aspectsBefore = headAspect;
    std::vector<char> done(heads.size(), 0);
    std::vector<int> nextOf(groups.size(), -2);
    // The type's aspect for the one wanted: the most restrictive for stop;
    // otherwise the least restrictive it has no less restrictive than wanted
    // among its proceed aspects (approach on), else the nearest above.
    auto pick = [&](const SignalKind &kind, int wanted, int &aspect, int &draw) {
        if (kind.aspects.empty()) {
            aspect = Stop;
            draw = kind.defaultDrawState;
            return;
        }
        const SignalKind::Aspect *chosen = nullptr;
        if (wanted <= Stop) {
            chosen = &kind.aspects.front();
        } else {
            for (const SignalKind::Aspect &a : kind.aspects)
                if (a.aspect >= Approach1 && a.aspect <= wanted)
                    chosen = &a;
            if (chosen == nullptr)
                for (const SignalKind::Aspect &a : kind.aspects)
                    if (a.aspect >= Approach1) {
                        chosen = &a;
                        break;
                    }
            if (chosen == nullptr)
                chosen = &kind.aspects.back();
        }
        aspect = chosen->aspect;
        draw = chosen->drawState;
    };
    auto progression = [](int next) {
        return next <= Restricting ? Approach1 : next == Approach1 ? Approach2 : next == Approach2 ? Approach3 : Clear2;
    };
    std::function<int(int)> evaluate;
    // A signal's least restrictive NORMAL head (next_sig_lr), STOP for none.
    auto leastRestrictive = [&](int signal) {
        int best = Stop;
        if (signal < 0)
            return best;
        for (int head : groups[size_t(signal)].heads)
            if (network.isNormal(heads[size_t(head)]))
                best = std::max(best, evaluate(head));
        return best;
    };
    auto nextSignal = [&](int node, float distance, bool forward) {
        return network.nextStopSignal(node, distance, forward, switchAt, Reach);
    };
    evaluate = [&](int h) -> int {
        if (done[size_t(h)] == 2)
            return headAspect[size_t(h)];
        if (done[size_t(h)] == 1)
            return Stop;
        done[size_t(h)] = 1;
        const SignalHead &head = heads[size_t(h)];
        int aspect = Stop, draw = 0;
        if (head.kind >= 0) {
            const SignalKind &kind = kinds[size_t(head.kind)];
            int wanted = -1;
            switch (kind.function) {
            case SignalFunction::Normal: {
                const bool cleared = clearedFor[size_t(head.signal)] != NoHolder;
                const int junction = network.junctionSection(head.junction);
                const bool routeSet = head.junction < 0 || junction < 0 || switchAt[size_t(junction)] == head.junctionPin;
                if (!cleared || !routeSet) {
                    wanted = Stop;
                } else {
                    int &next = nextOf[size_t(head.signal)];
                    if (next == -2) {
                        const SignalGroup &group = groups[size_t(head.signal)];
                        next = nextSignal(group.node, group.distance, group.forward);
                    }
                    wanted = progression(leastRestrictive(next));
                }
                break;
            }
            case SignalFunction::Distant: {
                const int next = leastRestrictive(nextSignal(head.node, head.distance, head.forward));
                wanted = next <= Stop ? Stop : progression(next);
                break;
            }
            case SignalFunction::Repeater:
                wanted = leastRestrictive(nextSignal(head.node, head.distance, head.forward));
                break;
            default:
                break;
            }
            if (wanted >= 0) {
                pick(kind, wanted, aspect, draw);
            } else {
                aspect = kind.aspects.empty() ? Stop : kind.aspects.front().aspect;
                draw = kind.defaultDrawState;
            }
        }
        headAspect[size_t(h)] = aspect;
        headDraw[size_t(h)] = draw;
        done[size_t(h)] = 2;
        return aspect;
    };
    for (size_t h = 0; h < heads.size(); ++h)
        evaluate(int(h));
    if (headDraw != drawnBefore || headAspect != aspectsBefore)
        ++drawChanges;
}

size_t SimulationCore::chunkEnd(const std::vector<ProjectedSection> &way, size_t from, size_t stop) const {
    // The junctions before a track section, the section, and the crossings
    // on it (not the junction at its far end: that goes with the next).
    const std::vector<TrackSection> &sections = in->network->sections();
    size_t end = from;
    double trackLeave = -1.0;
    while (end < stop) {
        const TrackSection::Kind kind = sections[size_t(way[end].section)].kind;
        if (trackLeave >= 0.0) {
            if (kind != TrackSection::Crossing || way[end].enter >= trackLeave - 1e-6)
                break;
        } else if (kind == TrackSection::Track) {
            trackLeave = way[end].leave;
        }
        ++end;
    }
    return end;
}

void SimulationCore::findStretches() {
    if (in->network == nullptr)
        return;
    stretchesOf.assign(in->trains.size(), {});
    for (size_t t = 0; t < in->trains.size(); ++t)
        stretchesOf[t].resize(runtime[t].ways.size());
    for (size_t a = 0; a < in->trains.size(); ++a)
        for (size_t b = a + 1; b < in->trains.size(); ++b)
            for (size_t pa = 0; pa < runtime[a].ways.size(); ++pa)
                for (size_t pb = 0; pb < runtime[b].ways.size(); ++pb)
                    addStretches(int(a), int(pa), int(b), int(pb));
    claimedBy.assign(single.size(), NoHolder);
}

void SimulationCore::addStretches(int a, int pa, int b, int pb) {
    const std::vector<TrackSection> &sections = in->network->sections();
    const std::vector<ProjectedSection> &wayA = runtime[size_t(a)].ways[size_t(pa)].sections;
    const std::vector<ProjectedSection> &wayB = runtime[size_t(b)].ways[size_t(pb)].sections;
    // Where B passes each section first, and which way.
    std::unordered_map<int, size_t> onB;
    for (size_t k = 0; k < wayB.size(); ++k)
        onB.emplace(wayB[k].section, k);
    // A's sections B passes the other way (junctions and crossings: at all),
    // in maximal runs with track.
    auto shared = [&](size_t i) -> long {
        auto found = onB.find(wayA[i].section);
        if (found == onB.end())
            return -1;
        const ProjectedSection &other = wayB[found->second];
        if (sections[size_t(wayA[i].section)].kind == TrackSection::Track && other.forward == wayA[i].forward)
            return -1;
        return long(found->second);
    };
    struct Run {
        size_t from, to, low, high;
        bool track;
    };
    std::vector<Run> runs;
    size_t i = 0;
    while (i < wayA.size()) {
        if (shared(i) < 0) {
            ++i;
            continue;
        }
        Run run{i, i, wayB.size(), 0, false};
        while (run.to < wayA.size()) {
            const long k = shared(run.to);
            if (k < 0)
                break;
            run.track = run.track || sections[size_t(wayA[run.to].section)].kind == TrackSection::Track;
            run.low = std::min(run.low, size_t(k));
            run.high = std::max(run.high, size_t(k) + 1);
            ++run.to;
        }
        // Where they part between two runs is no place to pass unless one
        // of them fits there clear of both (stopped short of the next), and
        // no standing consist is on either one's track there.
        if (!runs.empty()) {
            Run &last = runs.back();
            const bool apart = run.high <= last.low;
            const double gapA = wayA[run.from].enter - wayA[last.to - 1].leave;
            const double gapB = apart ? wayB[last.low].enter - wayB[run.high - 1].leave : 0.0;
            bool open = apart;
            for (size_t k = last.to; k < run.from && open; ++k)
                open = holder[size_t(wayA[k].section)] != StandingConsist;
            for (size_t k = run.high; apart && k < last.low && open; ++k)
                open = holder[size_t(wayB[k].section)] != StandingConsist;
            if (!open || (gapA < in->trains[size_t(a)].length + SignalMargin
                          && gapB < in->trains[size_t(b)].length + SignalMargin)) {
                last.to = run.to;
                last.low = std::min(last.low, run.low);
                last.high = std::max(last.high, run.high);
                last.track = last.track || run.track;
                i = run.to;
                continue;
            }
        }
        runs.push_back(run);
        i = run.to;
    }
    for (const Run &run : runs) {
        if (!run.track)
            continue;
        Stretch stretch;
        stretch.train[0] = a;
        stretch.train[1] = b;
        stretch.part[0] = pa;
        stretch.part[1] = pb;
        stretch.from[0] = run.from;
        stretch.to[0] = run.to;
        stretch.from[1] = run.low;
        stretch.to[1] = run.high;
        stretch.leave[0] = wayA[run.to - 1].leave;
        stretch.leave[1] = wayB[run.high - 1].leave;
        const int id = int(single.size());
        single.push_back(stretch);
        claimedBy.push_back(NoHolder);
        auto insert = [this, id](std::vector<std::pair<int, int>> &list, int side) {
            list.push_back({id, side});
            std::sort(list.begin(), list.end(), [this](const auto &x, const auto &y) {
                return single[size_t(x.first)].from[x.second] < single[size_t(y.first)].from[y.second];
            });
        };
        insert(stretchesOf[size_t(a)][size_t(pa)], 0);
        insert(stretchesOf[size_t(b)][size_t(pb)], 1);
    }
}

void SimulationCore::refreshStretches(int train) {
    const int part = states[size_t(train)].part;
    // Its stretches on this part go (kept aside for their claims).
    std::vector<Stretch> old;
    std::vector<int> oldClaims;
    for (const auto &[id, side] : stretchesOf[size_t(train)][size_t(part)]) {
        Stretch stretch = single[size_t(id)];
        // Seen from the other train: side 1 is the other's.
        if (side == 0) {
            std::swap(stretch.train[0], stretch.train[1]);
            std::swap(stretch.part[0], stretch.part[1]);
            std::swap(stretch.from[0], stretch.from[1]);
            std::swap(stretch.to[0], stretch.to[1]);
        }
        old.push_back(stretch);
        oldClaims.push_back(claimedBy[size_t(id)]);
        const int other = single[size_t(id)].train[1 - side];
        auto &theirs = stretchesOf[size_t(other)][size_t(single[size_t(id)].part[1 - side])];
        theirs.erase(std::remove_if(theirs.begin(), theirs.end(), [id](const auto &x) { return x.first == id; }),
                     theirs.end());
        claimedBy[size_t(id)] = NoHolder;
        single[size_t(id)].train[0] = single[size_t(id)].train[1] = -1;
    }
    stretchesOf[size_t(train)][size_t(part)].clear();
    for (size_t other = 0; other < in->trains.size(); ++other) {
        if (int(other) == train)
            continue;
        for (size_t po = 0; po < runtime[other].ways.size(); ++po) {
            const size_t first = single.size();
            addStretches(train, part, int(other), int(po));
            // A claim stays on the other train's track it covered.
            for (size_t id = first; id < single.size(); ++id)
                for (size_t k = 0; k < old.size(); ++k)
                    if (oldClaims[k] != NoHolder && old[k].train[1] == int(other) && old[k].part[1] == int(po)
                            && single[id].from[1] < old[k].to[1] && single[id].to[1] > old[k].from[1])
                        claimedBy[id] = oldClaims[k];
        }
    }
}

bool SimulationCore::passed(const Stretch &stretch, int side) const {
    const int train = stretch.train[side];
    const TrainState &s = states[size_t(train)];
    if (s.status == TrainStatus::Finished)
        return true;
    if (s.part != stretch.part[side])
        return s.part > stretch.part[side];
    return s.status != TrainStatus::Waiting && s.front - in->trains[size_t(train)].length > stretch.leave[side] - 1e-6;
}

bool SimulationCore::mayEnter(int train, size_t from, size_t to) {
    TrainState &s = states[size_t(train)];
    if (size_t(train) >= stretchesOf.size() || size_t(s.part) >= stretchesOf[size_t(train)].size())
        return true;
    std::vector<int> claim;
    for (const auto &[stretch, side] : stretchesOf[size_t(train)][size_t(s.part)]) {
        const Stretch &st = single[size_t(stretch)];
        // Only stretches it enters in this range (or is in).
        if (st.train[0] < 0 || st.from[side] >= to || st.to[side] <= from || claimedBy[size_t(stretch)] == train)
            continue;
        const int otherSide = 1 - side;
        const int other = st.train[otherSide];
        if (passed(st, otherSide))
            continue;
        bool inside = claimedBy[size_t(stretch)] == other;
        if (!inside && size_t(st.part[otherSide]) < runtime[size_t(other)].ways.size()) {
            const std::vector<ProjectedSection> &way = runtime[size_t(other)].ways[size_t(st.part[otherSide])].sections;
            for (size_t k = st.from[otherSide]; k < st.to[otherSide] && !inside; ++k)
                inside = holder[size_t(way[k].section)] == other;
        }
        if (inside) {
            s.blockedBy = other;
            s.waitSection = runtime[size_t(train)].ways[size_t(s.part)].sections[st.from[side]].section;
            s.waitFor = WaitFor::SingleTrack;
            return false;
        }
        claim.push_back(stretch);
    }
    // All or none.
    for (int stretch : claim)
        claimedBy[size_t(stretch)] = train;
    return true;
}

bool SimulationCore::enterable(int train, size_t from, size_t to, int *blocker) const {
    const TrainState &s = states[size_t(train)];
    if (size_t(train) >= stretchesOf.size() || size_t(s.part) >= stretchesOf[size_t(train)].size())
        return true;
    for (const auto &[stretch, side] : stretchesOf[size_t(train)][size_t(s.part)]) {
        const Stretch &st = single[size_t(stretch)];
        if (st.train[0] < 0 || st.from[side] >= to || st.to[side] <= from || claimedBy[size_t(stretch)] == train)
            continue;
        const int otherSide = 1 - side;
        const int other = st.train[otherSide];
        if (passed(st, otherSide))
            continue;
        bool inside = claimedBy[size_t(stretch)] == other;
        const std::vector<ProjectedSection> &way = runtime[size_t(other)].ways[size_t(st.part[otherSide])].sections;
        for (size_t k = st.from[otherSide]; k < st.to[otherSide] && !inside; ++k)
            inside = holder[size_t(way[k].section)] == other;
        if (inside) {
            *blocker = other;
            return false;
        }
    }
    return true;
}

namespace {

// A route part with `side` in place of its track between route distances
// `from` and `to`.
RouteTrack splice(const RouteTrack &route, double from, double to, const RouteTrack &side) {
    RouteTrack out;
    auto piece = [&out](const RouteInterval &interval, double begin, double end) {
        const double sign = interval.forward() ? 1.0 : -1.0;
        out.append(interval.node, float(interval.from + sign * (begin - interval.start)),
                   float(interval.from + sign * (end - interval.start)));
    };
    for (const RouteInterval &interval : route.intervals())
        if (interval.start < from - 1e-6)
            piece(interval, interval.start, std::min(interval.start + interval.length(), from));
    for (const RouteInterval &interval : side.intervals())
        out.append(interval.node, interval.from, interval.to);
    for (const RouteInterval &interval : route.intervals())
        if (interval.start + interval.length() > to + 1e-6)
            piece(interval, std::max(interval.start, to), interval.start + interval.length());
    return out;
}

}

SimulationCore::Passing SimulationCore::choosePassing(int train, size_t from, size_t to) {
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    if (in->network == nullptr)
        return Passing::None;
    const std::vector<TrackSection> &sections = in->network->sections();
    for (size_t k = 0; k < rt.passing.size(); ++k) {
        const PassingPlan &passing = rt.passing[k];
        if (passing.part != s.part)
            continue;
        const std::vector<ProjectedSection> &way = rt.ways[size_t(s.part)].sections;
        // Only where the unit reaches the junction it leaves the route by.
        if (from >= to || to > way.size() || passing.from < way[from].enter - 1.0
                || passing.from > way[to - 1].leave + 1.0)
            continue;
        // The junctions where it leaves the route and rejoins it.
        auto junctionAt = [&](double at, size_t start) {
            auto first = std::lower_bound(way.begin() + long(start), way.end(), at - 1.0,
                                          [](const ProjectedSection &p, double d) { return p.enter < d; });
            for (auto i = first; i != way.end() && i->enter <= at + 1.0; ++i)
                if (sections[size_t(i->section)].kind == TrackSection::Junction)
                    return size_t(i - way.begin());
            return way.size();
        };
        const size_t leaves = junctionAt(passing.from, from);
        if (leaves < from || leaves >= to)
            continue;
        const size_t rejoins = junctionAt(passing.to, leaves);
        if (rejoins >= way.size())
            continue;
        // The main route through the loop is free of opposing claims and of
        // other trains and consists (Open Rails: its block not reservable).
        int blocker = NoHolder;
        bool mainFree = enterable(train, leaves, rejoins + 1, &blocker);
        for (size_t m = leaves; m <= rejoins && mainFree; ++m) {
            const int h = holder[size_t(way[m].section)];
            mainFree = h == NoHolder || h == train;
        }
        if (mainFree)
            continue;
        // The passing path, if free: taken; else it waits short of it.
        Runtime::Side &made = rt.sides[k];
        if (!made.made) {
            made.route = splice(rt.parts[size_t(s.part)], passing.from, passing.to, passing.track);
            made.way = in->network->project(made.route);
            made.made = true;
        }
        const double end = passing.from + passing.track.length();
        for (const ProjectedSection &pass : made.way.sections) {
            if (pass.enter < passing.from - 1e-6 || pass.enter > end + 1e-6)
                continue;
            const int h = holder[size_t(pass.section)];
            if (h != NoHolder && h != train) {
                s.blockedBy = h;
                s.waitSection = pass.section;
                s.waitFor = WaitFor::PassingTrack;
                return Passing::Wait;
            }
        }
        takePassing(train, k);
        return Passing::Taken;
    }
    return Passing::None;
}

void SimulationCore::takePassing(int train, size_t index) {
    const TrainPlan &plan = in->trains[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    const PassingPlan passing = rt.passing[index];
    const size_t part = size_t(passing.part);
    const double mainLength = passing.to - passing.from;
    const double sideLength = passing.track.length();
    // Route distances further on move by the difference; those along the
    // loop's main track go to the same share of the passing path.
    auto moved = [&](double at) {
        if (at >= passing.to)
            return at + sideLength - mainLength;
        if (at > passing.from)
            return passing.from + (at - passing.from) * sideLength / mainLength;
        return at;
    };
    Runtime::Side &made = rt.sides[index];
    if (!made.made) {
        made.route = splice(rt.parts[part], passing.from, passing.to, passing.track);
        made.way = in->network->project(made.route);
    }
    rt.parts[part] = std::move(made.route);
    rt.ways[part] = std::move(made.way);
    // Its limits from the speed posts on it (the activity's speed zones on
    // this part are not kept).
    rt.limits[part] = SpeedLimits(rt.ways[part], *in->network, plan.passenger, {}, 0.0f, rt.parts[part].length());
    rt.limit = 0;
    for (StopPlan &stop : rt.stops)
        if (size_t(stop.part) == part)
            stop.front = moved(stop.front);
    for (WaitPlan &wait : rt.waits)
        if (size_t(wait.part) == part)
            wait.front = moved(wait.front);
    rt.passing.erase(rt.passing.begin() + long(index));
    rt.sides.erase(rt.sides.begin() + long(index));
    // The others' routes were made on the route before.
    for (Runtime::Side &other : rt.sides)
        other.made = false;
    for (PassingPlan &other : rt.passing)
        if (size_t(other.part) == part) {
            other.from = moved(other.from);
            other.to = moved(other.to);
        }
    refreshStretches(train);
    aspectsDirty = true;
}

void SimulationCore::take(int train, size_t from, size_t to) {
    const TrainPlan &plan = in->trains[size_t(train)];
    const std::vector<ProjectedSection> &way = runtime[size_t(train)].ways[size_t(states[size_t(train)].part)].sections;
    const std::vector<TrackSection> &sections = in->network->sections();
    Runtime &rt = runtime[size_t(train)];
    if (to > from) {
        aspectsDirty = true;
        ++holdChanges;
    }
    for (size_t k = from; k < to; ++k) {
        const ProjectedSection &pass = way[k];
        holder[size_t(pass.section)] = train;
        rt.held.push_back({pass.section, pass.enter, pass.leave});
        // A junction set the train's way: its output where it goes out by
        // one, the output it comes in by trailing.
        if (sections[size_t(pass.section)].kind == TrackSection::Junction) {
            const int pin = pass.pinIn == 0 ? pass.pinOut : pass.pinIn;
            if (pin == 1 || pin == 2)
                switchAt[size_t(pass.section)] = pin;
        }
    }
}

size_t SimulationCore::clearable(const Projection &way, size_t first) const {
    const std::vector<ProjectedSignal> &stopSignals = way.stopSignals;
    if (first >= stopSignals.size() || stopSignals[first].kind < 0)
        return 1;
    const std::vector<SignalKind> &kinds = in->network->kinds();
    const SignalKind &leading = kinds[size_t(stopSignals[first].kind)];
    const int wanted = leading.clearAhead;
    // Legacy: NORMAL heads, the first signal's included. Open Rails mode:
    // signals; a type of 0 clears none beyond it, one of -1 is not counted.
    size_t allowed = 0;
    int counted = 0;
    for (size_t k = first; k < stopSignals.size(); ++k) {
        if (k > first && counted >= wanted)
            break;
        ++allowed;
        if (!leading.countsSignals) {
            counted += stopSignals[k].normalHeads;
            continue;
        }
        const int ahead = stopSignals[k].kind >= 0 ? kinds[size_t(stopSignals[k].kind)].clearAhead : 1;
        if (ahead != -1)
            ++counted;
        if (ahead == 0)
            break;
    }
    return std::max<size_t>(allowed, 1);
}

void SimulationCore::releaseBehind(int train) {
    const TrainPlan &plan = in->trains[size_t(train)];
    const TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    // Signals its front has passed are no longer cleared for it.
    if (size_t(s.part) < runtime[size_t(train)].ways.size()) {
        const std::vector<ProjectedSignal> &stopSignals = runtime[size_t(train)].ways[size_t(s.part)].stopSignals;
        while (rt.passed < stopSignals.size() && stopSignals[rt.passed].at < s.front) {
            // It takes the signal's speed until the next one.
            TrainState &passing = states[size_t(train)];
            passing.signalSpeed = signalSpeedOf(stopSignals[rt.passed].signal);
            int &cleared = clearedFor[size_t(stopSignals[rt.passed].signal)];
            if (cleared == train)
                cleared = NoHolder;
            ++rt.passed;
            aspectsDirty = true;
        }
    }
    const double rear = s.front - plan.length;
    // Stretches its rear has left are no longer its own.
    if (size_t(train) < stretchesOf.size() && size_t(s.part) < stretchesOf[size_t(train)].size())
        for (const auto &[stretch, side] : stretchesOf[size_t(train)][size_t(s.part)])
            if (claimedBy[size_t(stretch)] == train && rear > single[size_t(stretch)].leave[side] - 1e-6)
                claimedBy[size_t(stretch)] = NoHolder;
    while (!rt.held.empty() && rt.held.front().leave < rear - 1e-6) {
        const int section = rt.held.front().section;
        rt.held.pop_front();
        // Not while it holds it again further on (a loop passed twice).
        bool again = false;
        for (const Held &other : rt.held)
            again = again || other.section == section;
        if (!again && holder[size_t(section)] == train) {
            holder[size_t(section)] = NoHolder;
            aspectsDirty = true;
            ++holdChanges;
        }
    }
}

void SimulationCore::releaseAll(int train) {
    Runtime &rt = runtime[size_t(train)];
    for (const Held &held : rt.held)
        if (holder[size_t(held.section)] == train)
            holder[size_t(held.section)] = NoHolder;
    if (!rt.held.empty())
        ++holdChanges;
    rt.held.clear();
    rt.next = 0;
    rt.nextSignal = 0;
    rt.passed = 0;
    rt.endsAtSignal = false;
    for (int &cleared : clearedFor)
        if (cleared == train)
            cleared = NoHolder;
    for (int &claim : claimedBy)
        if (claim == train)
            claim = NoHolder;
    rt.limit = 0;
    states[size_t(train)].authority = 0.0;
    states[size_t(train)].signalSpeed = -1.0f;
    aspectsDirty = true;
}

void SimulationCore::blocked(int train, double time) {
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    if (s.blockedBy == NoHolder || rt.reported == s.blockedBy)
        return;
    rt.reported = s.blockedBy;
    record(time, train, TrainEvent::Blocked, s.blockedBy);
    events.back().waitFor = s.waitFor;
    events.back().signal = s.status == TrainStatus::Waiting ? -1 : s.atSignal;
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

size_t SimulationCore::standsOn(int train) const {
    const TrainState &s = states[size_t(train)];
    const std::deque<Held> &held = runtime[size_t(train)].held;
    size_t count = 0;
    while (count < held.size() && held[count].enter < s.front - 1e-6)
        ++count;
    return count;
}

std::vector<SimulationCore::SectionUse> SimulationCore::sectionUses() const {
    std::vector<SectionUse> uses(holder.size());
    for (size_t k = 0; k < holder.size(); ++k) {
        uses[k].holder = holder[k];
        uses[k].occupied = holder[k] == StandingConsist;
    }
    for (size_t t = 0; t < runtime.size(); ++t) {
        const std::deque<Held> &held = runtime[t].held;
        const size_t under = standsOn(int(t));
        for (size_t k = 0; k < under; ++k)
            uses[size_t(held[k].section)].occupied = true;
    }
    return uses;
}

bool SimulationCore::pose(int train, double routeDistance, TrackPose &out) const {
    if (train < 0 || size_t(train) >= states.size())
        return false;
    const TrainPlan &plan = in->trains[size_t(train)];
    const TrainState &state = states[size_t(train)];
    if (state.part < 0 || size_t(state.part) >= runtime[size_t(train)].parts.size())
        return false;
    const RoutePoint point = runtime[size_t(train)].parts[size_t(state.part)].locate(routeDistance);
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
