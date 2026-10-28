#include <tsre/sim/SimulationCore.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <tsre/trains/WaitingPoint.h>

namespace Sim {

namespace {

// Closer than this to its target, a train is there.
constexpr double AtTarget = 0.01;
// A stop or waiting point more than this behind the front was passed.
constexpr double Passed = 0.5;
// Route distances of a follow point this close are the same place.
constexpr double Same = 1.0;

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
    stepSeconds = std::clamp(in->step, 0.001, MaxStep);
    // TSRE_SIM_STEP=<seconds>: every run at that step (measuring a driven
    // run's 0.01 s for AI trains, task driving 02).
    bool stepSet = false;
    const double stepAsked = qEnvironmentVariable("TSRE_SIM_STEP").toDouble(&stepSet);
    if (stepSet)
        stepSeconds = std::clamp(stepAsked, 0.001, MaxStep);
    driven = in->driven >= 0 && size_t(in->driven) < in->trains.size() ? in->driven : -1;
    states.resize(in->trains.size());
    runtime.resize(in->trains.size());
    // Each train's own copy of its route: a part changes when it takes a
    // passing path.
    for (size_t i = 0; i < runtime.size(); ++i) {
        const TrainPlan &plan = in->trains[i];
        Runtime &rt = runtime[i];
        rt.parts = plan.route.parts;
        rt.ways = plan.projection;
        rt.vehicles = plan.vehicles;
        rt.length = plan.length;
        rt.performance = plan.performance;
        rt.passenger = plan.passenger;
        rt.acceleration = plan.acceleration;
        rt.deceleration = plan.deceleration;
        rt.driving = plan.driving;
        rt.limits = plan.limits;
        rt.stops = plan.stops;
        rt.waits = plan.waits;
        rt.passing = plan.passing;
        rt.sides.resize(rt.passing.size());
        for (const PassingPlan &passing : rt.passing) {
            std::vector<int> own;
            if (in->network != nullptr)
                for (const ProjectedSection &pass : in->network->project(passing.track).sections)
                    if (in->network->sections()[size_t(pass.section)].kind == TrackSection::Track)
                        own.push_back(pass.section);
            std::sort(own.begin(), own.end());
            rt.passingSections.push_back(own);
        }
        findExits(int(i));
    }
    for (size_t i = 0; i < states.size(); ++i) {
        states[i].front = in->trains[i].startFront;
        if (in->trains[i].activatedBy >= 0)
            activated.push_back(int(i));
    }
    const TrackNetwork *network = in->network.get();
    holder.assign(network != nullptr ? network->sections().size() : 0, NoHolder);
    heldStamp.assign(holder.size(), 0);
    // Switches in their main position until a train sets them.
    switchAt.assign(holder.size(), 0);
    if (network != nullptr)
        for (size_t k = 0; k < holder.size(); ++k)
            if (network->sections()[k].kind == TrackSection::Junction)
                switchAt[k] = network->sections()[k].mainPin;
    clearedFor.assign(network != nullptr ? network->signalGroups().size() : 0, NoHolder);
    occupiedBlock.assign(clearedFor.size(), 0);
    headAspect.assign(network != nullptr ? network->heads().size() : 0, 0);
    headDraw.assign(headAspect.size(), 0);
    // Loose consists are standing trains, holding the track they stand on.
    standingAt.assign(holder.size(), -1);
    for (const StaticConsist &consist : in->staticConsists) {
        Standing made;
        made.name = consist.name;
        made.object = consist.object;
        made.vehicles = consist.vehicles;
        made.length = consist.length;
        made.track = consist.track;
        standing.push_back(made);
        holdStanding(int(standing.size()) - 1);
    }
    findStretches();
    // Activity events (task 06): their levels from the file; the trains
    // location events look at (the player's, and triggering trains) know
    // where the events' points lie on their routes.
    eventState.resize(in->events.size());
    for (size_t e = 0; e < in->events.size(); ++e)
        eventState[e].level = in->events[e].activation;
    for (const TrainPlan &plan : in->trains)
        if (plan.player && playerTrain < 0)
            playerTrain = plan.id;
    // The work orders the autopilot does not do (task 07, part B), in the
    // run's log from its start.
    for (const SimulationInput::WorkOrder &order : in->workOrders)
        if (!order.planned && order.active && playerTrain >= 0)
            record(double(in->startTime), playerTrain, TrainEvent::WorkSkipped, order.event);
    for (const EventPlan &event : in->events)
        if (event.kind == EventPlan::Location) {
            if (playerTrain >= 0)
                runtime[size_t(playerTrain)].watched = true;
            if (event.trigger >= 0 && size_t(event.trigger) < runtime.size())
                runtime[size_t(event.trigger)].watched = true;
        }
    for (size_t i = 0; i < runtime.size(); ++i)
        if (runtime[i].watched)
            for (size_t part = 0; part < runtime[i].parts.size(); ++part)
                findSpots(int(i), int(part));
    // The pre-run: all trains together from the first one's start up to the
    // activity's (Open Rails' pre-run of AI trains), in whole steps (start
    // times are whole seconds).
    now = in->startTime;
    double first = now;
    for (const TrainPlan &plan : in->trains)
        first = std::min(first, double(plan.createTime >= 0 ? std::min(plan.createTime, plan.startTime) : plan.startTime));
    if (first < now) {
        const double start = now;
        origin = first;
        ticks = 0;
        now = first;
        prerunning = true;
        const long long count = std::llround((start - first) / stepSeconds);
        for (long long k = 0; k < count; ++k)
            stepOnce();
        prerunning = false;
        now = start;
    }
    origin = now;
    ticks = 0;
    if (aspectsPending())
        evaluateAspects();
    stepCount = 0;
}

void SimulationCore::refreshConsist(int train) {
    const TrainPlan &plan = in->trains[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    ++rt.carsVersion;
    rt.length = 0.0;
    for (const Vehicle &vehicle : rt.vehicles)
        rt.length += vehicle.length();
    rt.performance = PerformanceOf(rt.vehicles);
    const Rates rates = RatesOf(rt.vehicles, std::min(in->routeSpeedLimit, plan.maxSpeed * plan.efficiency));
    rt.passenger = rates.passenger;
    rt.acceleration = rates.acceleration * plan.accelerationFactor;
    rt.deceleration = rates.deceleration * plan.decelerationFactor;
    rt.driving = DrivingOf(rt.vehicles);
}

void SimulationCore::step(double seconds) {
    pending += std::max(0.0, seconds);
    while (pending >= stepSeconds - 1e-9) {
        pending -= stepSeconds;
        stepOnce();
    }
    pending = std::max(0.0, pending);
}

double SimulationCore::onStep(double time) const {
    return origin + std::ceil((time - origin) / stepSeconds - 1e-6) * stepSeconds;
}

void SimulationCore::stepOnce() {
    // Time is the origin plus whole steps: no drift over a long run.
    const double next = origin + double(ticks + 1) * stepSeconds;
    for (Runtime &rt : runtime)
        if (rt.watched) {
            rt.swept.clear();
            rt.rests.clear();
        }
    // Trains due take their places first, in the order they are due, all
    // before any reserves ahead: those an activity puts on the track at
    // the same time are there together. One waits while a stretch it
    // would stand in was claimed for another train before.
    std::vector<int> due;
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].status == TrainStatus::Waiting && placeTime(int(i)) <= next)
            due.push_back(int(i));
    std::stable_sort(due.begin(), due.end(), [this](int a, int b) {
        return placeTime(a) < placeTime(b);
    });
    std::vector<int> free;
    for (int i : due) {
        int blocker = NoHolder;
        if (startClaimed(i, &blocker)) {
            TrainState &s = states[size_t(i)];
            s.blockedBy = blocker;
            s.waitFor = WaitFor::SingleTrack;
            blocked(i, std::max(now, placeTime(i)));
        } else {
            free.push_back(i);
        }
    }
    for (int i : free)
        start(i, now, next);
    // The driven train (task driving 02) set up once on the track.
    if (driven >= 0 && !runtime[size_t(driven)].drivenState && states[size_t(driven)].status != TrainStatus::Waiting
            && states[size_t(driven)].status != TrainStatus::Finished)
        takeOver(driven);
    for (size_t i = 0; i < states.size(); ++i) {
        if (int(i) == driven && runtime[i].drivenState)
            advanceDriven(int(i), now, next);
        else
            advance(int(i), now, next);
    }
    now = next;
    ++ticks;
    ++stepCount;
    findDeadlocks(now);
    if (aspectsPending())
        evaluateAspects();
    if (!prerunning) {
        checkEvents();
        noteMisses();
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
    if (train >= 0 && size_t(train) < states.size())
        event.travelled = states[size_t(train)].travelled;
    events.push_back(event);
}

bool SimulationCore::finished() const {
    if (crash.time >= 0.0)
        return true;
    for (const TrainState &state : states)
        if (state.status != TrainStatus::Finished && state.stoppedAt != StopKind::End)
            return false;
    return true;
}

int SimulationCore::nextSignalAhead(int train, int *aspect, double *distance) const {
    if (train < 0 || size_t(train) >= states.size() || in->network == nullptr)
        return -1;
    const TrainState &s = states[size_t(train)];
    const Runtime &rt = runtime[size_t(train)];
    if (s.part < 0 || size_t(s.part) >= rt.ways.size())
        return -1;
    const std::vector<ProjectedSignal> &stops = rt.ways[size_t(s.part)].stopSignals;
    size_t next = rt.passed;
    while (next < stops.size() && stops[next].at < s.front)
        ++next;
    if (next >= stops.size())
        return -1;
    const int signal = stops[next].signal;
    if (aspect != nullptr) {
        *aspect = 0;
        for (int head : in->network->signalGroups()[size_t(signal)].heads)
            if (in->network->isNormal(in->network->heads()[size_t(head)]))
                *aspect = std::max(*aspect, headAspect[size_t(head)]);
    }
    if (distance != nullptr)
        *distance = stops[next].at - s.front;
    return signal;
}

bool SimulationCore::settled() const {
    for (const TrainState &state : states) {
        const bool held = state.blockedBy == StandingConsist
                && (state.status == TrainStatus::Waiting || state.status == TrainStatus::Stopped);
        if (state.status != TrainStatus::Finished && state.stoppedAt != StopKind::End && !state.deadlocked && !held)
            return false;
    }
    return true;
}

double SimulationCore::departureAt(double ready) const {
    if (in->departureStep <= 0)
        return ready;
    const double step = double(in->departureStep);
    return std::ceil(ready / step - 1e-9) * step;
}

double SimulationCore::placeTime(int train) const {
    const TrainPlan &plan = in->trains[size_t(train)];
    const double never = std::numeric_limits<double>::infinity();
    double place = plan.createTime >= 0 ? double(std::min(plan.createTime, plan.startTime)) : double(plan.startTime);
    if (plan.formedBy >= 0 && size_t(plan.formedBy) < runtime.size()) {
        const double ended = runtime[size_t(plan.formedBy)].endedAt;
        place = ended < 0.0 ? never : ended;
    }
    // Phase 8: `$activated`, not before another train activates it (one put
    // on its track before, `$create` or formed of another, stands there
    // until then); `$create /ahead`, once that train is on its track.
    if (plan.activatedBy >= 0 && plan.createTime < 0 && plan.formedBy < 0)
        place = runtime[size_t(train)].activatedAt < 0.0 ? never
                                                          : std::max(place, runtime[size_t(train)].activatedAt);
    if (plan.createAhead >= 0 && size_t(plan.createAhead) < states.size()
            && states[size_t(plan.createAhead)].status == TrainStatus::Waiting)
        place = never;
    return place;
}

void SimulationCore::activate(int train, TrainPlan::Activation when, int stop, double t) {
    for (int i : activated) {
        const TrainPlan &plan = in->trains[size_t(i)];
        Runtime &rt = runtime[size_t(i)];
        if (plan.activatedBy != train || plan.activation != when || rt.activatedAt >= 0.0
                || ((when == TrainPlan::Activation::Arrival || when == TrainPlan::Activation::Departure)
                    && plan.activationStop != stop))
            continue;
        rt.activatedAt = t;
        // One put on its track standing (`$create`): from its start time.
        TrainState &s = states[size_t(i)];
        if (s.status == TrainStatus::Stopped && s.stoppedAt == StopKind::Created)
            s.until = std::max(double(plan.startTime), t);
    }
}

bool SimulationCore::delayOf(int train, double t, double &delay) const {
    const Runtime &rt = runtime[size_t(train)];
    const TrainState &s = states[size_t(train)];
    bool known = rt.delayKnown;
    delay = rt.delay;
    // Past its next stop's booked arrival, not there yet: at least so late
    // (Open Rails, under 12 hours).
    if (s.stoppedAt != StopKind::Station && s.nextStop >= 0 && size_t(s.nextStop) < rt.stops.size()
            && rt.stops[size_t(s.nextStop)].arrival >= 0) {
        const double late = t - double(rt.stops[size_t(s.nextStop)].arrival);
        if (late > 0.0 && late < 12 * 3600.0 && (!known || late > delay)) {
            delay = late;
            known = true;
        }
    }
    return known;
}

float SimulationCore::topSpeedOf(int train, double t) const {
    const TrainPlan &plan = in->trains[size_t(train)];
    if (plan.cruiseSpeed <= 0.0f)
        return plan.maxSpeed;
    double delay = 0.0;
    if (plan.cruiseMaxDelay >= 0 && delayOf(train, t, delay) && delay > double(plan.cruiseMaxDelay))
        return plan.maxSpeed;
    return plan.cruiseSpeed;
}

bool SimulationCore::aheadFront(int train, double &front) const {
    const TrainPlan &plan = in->trains[size_t(train)];
    const int other = plan.createAhead;
    if (other < 0 || size_t(other) >= states.size() || states[size_t(other)].status == TrainStatus::Waiting
            || states[size_t(other)].status == TrainStatus::Finished)
        return false;
    const TrainState &o = states[size_t(other)];
    const Runtime &theirs = runtime[size_t(other)];
    const Runtime &rt = runtime[size_t(train)];
    if (size_t(o.part) >= theirs.parts.size() || rt.parts.empty())
        return false;
    // Its rear 2.5 m beyond that train's front (TTTrain.GetPositionAheadOfTrain,
    // keepDistanceCloseupM), where its route runs that train's way.
    const RoutePoint at = theirs.parts[size_t(o.part)].locate(o.front);
    double there = 0.0;
    if (!rt.parts.front().find(at.node, at.distance, there) || rt.parts.front().locate(there).forward != at.forward)
        return false;
    front = there + 2.5 + rt.length;
    return front <= rt.parts.front().length();
}

void SimulationCore::start(int train, double from, double to) {
    const TrainPlan &plan = in->trains[size_t(train)];
    TrainState &s = states[size_t(train)];
    const double place = placeTime(train);
    if (s.status != TrainStatus::Waiting || to < place)
        return;
    const double t = std::max(from, place);
    // Phase 8: of the vehicles of the train it is formed of (`$forms`); ahead
    // of that train (`$create /ahead`), where its route runs that way.
    if (plan.formedOfVehicles && plan.formedBy >= 0)
        takeFormingVehicles(train);
    double ahead = 0.0;
    const bool paired = plan.createAhead >= 0 && aheadFront(train, ahead);
    if (paired) {
        // In that train's section (Open Rails places it there): partners as
        // trains parted (task 07), sharing the sections they stand in, the
        // one behind short of this one.
        s.front = ahead;
        runtime[size_t(train)].partedFrom = plan.createAhead;
        runtime[size_t(plan.createAhead)].partedFrom = train;
        releaseAll(plan.createAhead);
    }
    if (!occupy(train)) {
        if (paired && !occupy(plan.createAhead))
            blocked(plan.createAhead, t);
        blocked(train, t);
        return;
    }
    if (paired && !occupy(plan.createAhead))
        blocked(plan.createAhead, t);
    s.speed = 0.0f;
    s.acceleration = 0.0f;
    s.blockedBy = NoHolder;
    runtime[size_t(train)].reported = Unreported;
    record(t, train, TrainEvent::Started);
    // Put on its track before its start (task 08): it stands there, holding
    // only that, until then; and until another train activates it
    // (`$activated`, phase 8).
    if (t < double(plan.startTime) - 1e-9 || (plan.activatedBy >= 0 && runtime[size_t(train)].activatedAt < 0.0)) {
        s.status = TrainStatus::Stopped;
        s.stoppedAt = StopKind::Created;
        s.until = double(plan.startTime);
        return;
    }
    beginRun(train, t);
}

void SimulationCore::beginRun(int train, double t) {
    TrainState &s = states[size_t(train)];
    s.status = TrainStatus::Running;
    s.stoppedAt = StopKind::None;
    activate(train, TrainPlan::Activation::Start, -1, t);
    // Starting at its first station (within a train's length of the stop):
    // it stands there its boarding time, and until its departure.
    if (!runtime[size_t(train)].stops.empty() && runtime[size_t(train)].stops.front().part == s.part
            && std::fabs(runtime[size_t(train)].stops.front().front - s.front) <= runtime[size_t(train)].length) {
        const StopPlan &stop = runtime[size_t(train)].stops.front();
        s.status = TrainStatus::Stopped;
        s.stoppedAt = StopKind::Station;
        s.until = t + stop.boarding;
        if (stop.departure >= 0 && !stop.departsEarly)
            s.until = std::max(s.until, double(stop.departure));
        s.until = departureAt(s.until);
        s.nextStop = 1;
        record(t, train, TrainEvent::Arrived, 0);
        activate(train, TrainPlan::Activation::Arrival, 0, t);
    }
}

bool SimulationCore::startClaimed(int train, int *blocker) const {
    const TrainState &s = states[size_t(train)];
    if (in->network == nullptr || size_t(s.part) >= runtime[size_t(train)].ways.size())
        return false;
    const std::vector<ProjectedSection> &way = runtime[size_t(train)].ways[size_t(s.part)].sections;
    const double rear = s.front - runtime[size_t(train)].length;
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
                // Goes on when the track ahead is its own again: tried again
                // when what it waits for has changed (the section it waits
                // for, or any section for the single track; any claim), and
                // every RetryEvery seconds whatever.
                Runtime &rt = runtime[size_t(train)];
                const auto stamp = [&] {
                    return s.waitFor != WaitFor::SingleTrack && s.waitSection >= 0
                            ? heldStamp[size_t(s.waitSection)] : holdChanges;
                };
                if (rt.triedAt >= 0.0 && t < rt.triedAt + RetryEvery && rt.triedClaims == claimChanges
                        && rt.triedHold == stamp())
                    break;
                extendAuthority(train);
                const double margin = rt.endsAtSignal ? SignalMargin : AuthorityMargin;
                if (s.authority < parts[size_t(s.part)].length() - 1e-6
                        && s.authority - margin <= s.front + AtTarget) {
                    rt.triedAt = t;
                    rt.triedClaims = claimChanges;
                    rt.triedHold = stamp();
                    blocked(train, t);
                    break;
                }
                rt.triedAt = -1.0;
                s.status = TrainStatus::Running;
                s.stoppedAt = StopKind::None;
                s.blockedBy = NoHolder;
                s.deadlocked = false;
                runtime[size_t(train)].reported = Unreported;
                continue;
            }
            // Put on its track before its start (task 08): its run begins
            // at its start time; `$activated`, once activated (phase 8).
            if (s.stoppedAt == StopKind::Created) {
                if (s.until > to || (in->trains[size_t(train)].activatedBy >= 0
                                     && runtime[size_t(train)].activatedAt < 0.0))
                    break;
                t = std::max(t, s.until);
                beginRun(train, t);
                continue;
            }
            // At a stop-here event (task 06): until it has fired (the events
            // are checked after each step), at most EventStandLimit.
            if (s.stoppedAt == StopKind::Event) {
                if (wanted(s.eventStop)) {
                    if (s.until > to)
                        break;
                    t = std::max(t, s.until);
                }
                s.eventStop = -1;
                s.status = TrainStatus::Running;
                s.stoppedAt = StopKind::None;
                continue;
            }
            // Standing at a station, its exit signal is cleared a while
            // before it leaves; a signal next held for a stop further on is
            // let go at once.
            if (s.stoppedAt == StopKind::Station && runtime[size_t(train)].endsAtSignal
                    && (to >= s.until - HoldRelease || holdDroppable(train, runtime[size_t(train)].nextSignal)))
                extendAuthority(train);
            if (s.until > to)
                break;
            // An Open Rails timetable's train (task 08) leaves only once its
            // exit signal shows more than STOP: it asks for the track beyond
            // (again when what it waits for has changed, and every
            // RetryEvery seconds), and leaves in a step after it clears.
            if (s.stoppedAt == StopKind::Station && waitsAtExit(train)) {
                Runtime &rt = runtime[size_t(train)];
                if (rt.triedAt < 0.0 || to >= rt.triedAt + RetryEvery || rt.triedClaims != claimChanges
                        || rt.triedHold != holdChanges) {
                    extendAuthority(train);
                    rt.triedAt = to;
                    rt.triedClaims = claimChanges;
                    rt.triedHold = holdChanges;
                }
                break;
            }
            runtime[size_t(train)].triedAt = -1.0;
            t = std::max(t, s.until);
            if (s.stoppedAt == StopKind::Reversal) {
                // The old rear leads, the train's length along the next part
                // (more where it reverses short of its part's end: task 07,
                // its own cut-off cars beyond), on the track it holds (given
                // back and taken again).
                const double shortOfEnd = std::max(0.0, parts[size_t(s.part)].length() - s.front);
                releaseAll(train);
                ++s.part;
                s.reversed = !s.reversed;
                s.front = std::min(double(runtime[size_t(train)].length) + shortOfEnd, parts[size_t(s.part)].length());
                record(t, train, TrainEvent::Reversed);
                if (!occupy(train)) {
                    s.stoppedAt = StopKind::Blocked;
                    blocked(train, t);
                    break;
                }
            } else if (s.stoppedAt == StopKind::Station) {
                const int left = s.nextStop - 1;
                record(t, train, TrainEvent::Departed, left);
                activate(train, TrainPlan::Activation::Departure, left, t);
                // Its delay on leaving a stop with times (Open Rails).
                Runtime &rt = runtime[size_t(train)];
                if (left >= 0 && size_t(left) < rt.stops.size() && rt.stops[size_t(left)].arrival >= 0
                        && rt.stops[size_t(left)].departure >= 0) {
                    rt.delay = t - double(rt.stops[size_t(left)].departure);
                    rt.delayKnown = true;
                }
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
            // A request stop no one needs is given up within RequestSight of
            // it (Open Rails' AI: it brakes for it until then).
            const bool givenUp = stop.request && !stop.needed && stop.part == s.part
                    && stop.front - s.front < RequestSight;
            if (!givenUp && (stop.part > s.part || (stop.part == s.part && stop.front >= s.front - Passed)))
                break;
            ++s.nextStop;
        }
        while (s.nextWait < int(runtime[size_t(train)].waits.size())) {
            const WaitPlan &wait = runtime[size_t(train)].waits[size_t(s.nextWait)];
            if (wait.part > s.part || (wait.part == s.part && wait.front >= s.front - Passed))
                break;
            ++s.nextWait;
        }
        Runtime &own = runtime[size_t(train)];
        // The player's train (task 06): its next station stop moves to an
        // active stop-here event along the platform, where it can still
        // stop there.
        if (train == playerTrain && own.watched && in->eventStops != SimulationInput::EventStops::None
                && s.nextStop < int(own.stops.size())) {
            StopPlan &stop = own.stops[size_t(s.nextStop)];
            if (stop.part == s.part && stop.front > s.front + AtTarget)
                for (const Runtime::Spot &spot : own.spots) {
                    if (spot.part != stop.part || std::fabs(spot.at - stop.front) < AtTarget
                            || spot.at < stop.platformStart || spot.at > stop.platformEnd || spot.at <= s.front + AtTarget
                            || !in->events[size_t(spot.event)].onStop || !wanted(spot.event))
                        continue;
                    if (spot.at < stop.front && double(s.speed) * s.speed / (2.0 * runtime[size_t(train)].deceleration) > spot.at - s.front)
                        continue;
                    stop.front = spot.at;
                    break;
                }
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
            // (The autopilot's cut at the part's end too: before it reverses.)
            if (wait.front < target || (wait.keep >= 0 && wait.front <= target + AtTarget)) {
                target = std::min(wait.front, target);
                kind = StopKind::Wait;
                index = w;
            }
            break;
        }
        // The player's train stops at the nearest active stop-here event
        // ahead (task 06): one it brakes for already, or one it can still
        // stop at; a train that cannot passes it, as a player told too late.
        if (train == playerTrain && own.watched && in->eventStops == SimulationInput::EventStops::All) {
            int chosen = -1;
            for (const Runtime::Spot &spot : own.spots) {
                if (spot.part != s.part || spot.at <= s.front + AtTarget || spot.at >= target - AtTarget
                        || !in->events[size_t(spot.event)].onStop || !wanted(spot.event))
                    continue;
                if (spot.event != s.eventStop && double(s.speed) * s.speed / (2.0 * runtime[size_t(train)].deceleration) > spot.at - s.front)
                    continue;
                target = spot.at;
                kind = StopKind::Event;
                index = spot.event;
                chosen = spot.event;
            }
            s.eventStop = chosen;
        }
        // Attaching to a standing train or a living one (task 07): it comes
        // to the contact.
        if ((own.attachTo >= 0 || own.attachTrain >= 0) && own.contactAt >= s.front - AtTarget
                && own.contactAt <= target + AtTarget) {
            target = std::max(s.front, own.contactAt);
            kind = StopKind::Couple;
            index = own.attachTo >= 0 ? own.attachTo : own.attachTrain;
        }
        if (kind != StopKind::Couple && s.authority < parts[size_t(s.part)].length() - 1e-6) {
            const double margin = runtime[size_t(train)].endsAtSignal ? SignalMargin : AuthorityMargin;
            const double limit = std::max(s.front, s.authority - margin);
            // A station stop short of where its authority ends is kept: Open
            // Rails' AI favours it over the signal or the authority's end.
            const bool station = kind == StopKind::Station && target <= s.authority - 1.0;
            if (limit < target && !station) {
                target = limit;
                kind = StopKind::Blocked;
                index = -1;
            }
            // Its own cut-off cars on the rest of the part (task 07): it
            // reverses where it stands.
            const int ahead = s.blockedBy == StandingConsist && s.waitSection >= 0
                    ? standingAhead(train, s.waitSection) : -1;
            const bool ownCars = ahead >= 0 && standing[size_t(ahead)].uncoupledFrom == train;
            // So too short of the train it just parted from.
            const bool parted = s.blockedBy >= 0 && s.blockedBy == own.partedFrom;
            if (kind == StopKind::Blocked && (ownCars || parted) && s.part + 1 < int(parts.size())) {
                target = s.front;
                kind = StopKind::Reversal;
            } else if (kind == StopKind::Blocked && ownCars) {
                // On its last part: its run ends there (the autopilot's work
                // ending with cars set out).
                target = s.front;
                kind = StopKind::End;
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
            // lower limit ahead where it begins. Physics driving brakes as
            // Open Rails' AI plans it (stoppingSpeed).
            const float efficiency = s.nextStop < int(runtime[size_t(train)].stops.size())
                    ? runtime[size_t(train)].stops[size_t(s.nextStop)].efficiency : plan.efficiency;
            const double top = std::min(double(in->routeSpeedLimit), double(topSpeedOf(train, t) * efficiency));
            double limit = top;
            if (s.signalSpeed > 0.0f)
                limit = std::min(limit, double(s.signalSpeed));
            const double dt = to - t;
            const double v = s.speed;
            const bool physics = runtime[size_t(train)].driving == Driving::Physics;
            const double d = physics ? BrakingShare * runtime[size_t(train)].deceleration : runtime[size_t(train)].deceleration;
            const double rear = s.front - runtime[size_t(train)].length;
            const double fastest = physics ? std::max(v, limit) : v;
            const double reach = fastest * fastest / (2.0 * d) + 2.0 * v * dt + 50.0
                    + (physics ? LimitCreep + BrakingZone : 0.0);
            // Physics: within reach of what it next stops at or slows for,
            // Open Rails' AI is in its braking state.
            bool braking = false;
            // The speed at the step's end from which it is down to `speed`
            // `distance` ahead.
            const auto downTo = [&](double distance, double speed) {
                if (!physics)
                    return brakingSpeed(v, dt, d, distance, speed);
                return std::max(speed, brakingSpeed(v, dt, d, distance - LimitCreep, speed));
            };
            double ahead = 1e9;
            if (size_t(s.part) < runtime[size_t(train)].limits.size()) {
                const std::vector<SpeedLimit> &limits = runtime[size_t(train)].limits[size_t(s.part)];
                Runtime &rt = runtime[size_t(train)];
                while (rt.limit < limits.size() && limits[rt.limit].to <= rear)
                    ++rt.limit;
                for (size_t k = rt.limit; k < limits.size() && limits[k].from < s.front + reach; ++k) {
                    if (limits[k].from <= s.front) {
                        limit = std::min(limit, double(limits[k].speed));
                    } else {
                        ahead = std::min(ahead, downTo(limits[k].from - s.front, limits[k].speed));
                        braking = braking || (limits[k].speed < limit && limits[k].from - s.front
                                <= (limit * limit - limits[k].speed * limits[k].speed) / (2.0 * d) + BrakingZone);
                    }
                }
            }
            // The next stop signal's speed, where its aspect has one.
            if (size_t(s.part) < runtime[size_t(train)].ways.size()) {
                const std::vector<ProjectedSignal> &stops = runtime[size_t(train)].ways[size_t(s.part)].stopSignals;
                const size_t next = runtime[size_t(train)].passed;
                if (next < stops.size() && stops[next].at < s.front + reach) {
                    const float speed = signalSpeedOf(stops[next].signal);
                    if (speed > 0.0f) {
                        ahead = std::min(ahead, downTo(stops[next].at - s.front, speed));
                        braking = braking || (speed < limit && stops[next].at - s.front
                                <= (limit * limit - speed * speed) / (2.0 * d) + BrakingZone);
                    }
                    // Cleared for it at STOP_AND_PROCEED or RESTRICTING (task
                    // 07): down to the creep speed SignalApproach short of it.
                    const double approach = stops[next].at - SignalApproach - s.front;
                    if (approach > 0.0 && clearedFor[size_t(stops[next].signal)] == train && restricted(stops[next].signal)) {
                        ahead = std::min(ahead, downTo(approach, CreepSpeed));
                        braking = braking || (CreepSpeed < limit
                                && approach <= (limit * limit - CreepSpeed * CreepSpeed) / (2.0 * d) + BrakingZone);
                    }
                }
            }
            // Coupling (task 07): closing on the train it attaches to as Open
            // Rails' AI follows a standing train, at most half the speed it
            // could stop from over the distance, never below the creep speed
            // (at the coupling speed over the last metres, below).
            if (kind == StopKind::Couple)
                limit = std::min(limit, std::max(0.5 * std::sqrt(distance * runtime[size_t(train)].deceleration),
                                                  double(CreepSpeed)));
            // A station's stop from 1.5 × its top speed² / its deceleration
            // (Open Rails' trigger from full speed), anything else from its
            // braking distance and BrakingZone.
            if (kind == StopKind::Station)
                braking = braking || distance <= 1.5 * top * top / runtime[size_t(train)].deceleration;
            else
                braking = braking || distance <= limit * limit / (2.0 * d) + BrakingZone;
            // The fastest speed at the step's end from which it still stops
            // at the target, so the train follows the braking curve exactly.
            double cap = std::min(ahead, physics ? stoppingSpeed(train, v, dt, distance, kind, limit)
                                                 : brakingSpeed(v, dt, d, distance, 0.0));
            if (kind == StopKind::Couple && distance < CouplingReach)
                cap = std::max(cap, CouplingSpeed);
            // Physics: away from rest it is starting away until within
            // (9 - 6 × Efficiency) half m/s of its limit or in its braking
            // state (Open Rails' accelerating state).
            Runtime &driven = runtime[size_t(train)];
            if (v <= 0.0)
                driven.startingAway = true;
            if (braking || v > limit - (9.0 - 6.0 * plan.drivingEfficiency) * 0.5)
                driven.startingAway = false;
            // Physics: it holds its speed in Open Rails' running band below
            // the limit, (1.5 - Efficiency) to (3 - 2 × Efficiency) half m/s
            // under it: at the band's middle, easing down to it from above.
            const double hold = physics && limit > CreepSpeed
                    ? limit - (4.5 - 3.0 * plan.drivingEfficiency) * 0.25 : limit;
            double next;
            if (v > limit) {
                next = std::max(v - runtime[size_t(train)].deceleration * dt, limit);
            } else if (v > hold) {
                next = std::max(v - d * dt, hold);
            } else if (v >= std::min(hold, cap)) {
                next = hold;
            } else {
                // Physics: starting away, Open Rails' AI raises its throttle
                // until the train gains half its target acceleration; where
                // full power gives less, it gains Efficiency × that half
                // instead. Else (running, or in its braking state up to its
                // braking curve) it opens the throttle fully, gaining at
                // least half the target. Before the activity (its pre-run)
                // the same, unlike Open Rails' (task 03).
                double a = runtime[size_t(train)].acceleration;
                if (physics && !driven.startingAway)
                    a = std::max(fullAcceleration(train, v), 0.5 * runtime[size_t(train)].acceleration);
                else if (physics)
                    a = fullAcceleration(train, v) >= 0.5 * runtime[size_t(train)].acceleration ? 0.5 * runtime[size_t(train)].acceleration
                        : plan.drivingEfficiency * 0.5 * runtime[size_t(train)].acceleration;
                next = std::min(v + a * dt, hold);
            }
            next = std::min(next, cap);
            const double moved = 0.5 * (v + next) * dt;
            if (moved >= distance - AtTarget) {
                arrival = t + (moved > 0.0 ? dt * std::min(1.0, distance / moved) : dt);
            } else {
                sweep(train, s.front, s.front + moved);
                s.front += moved;
                s.travelled += moved;
                s.acceleration = dt > 0.0 ? float((next - v) / dt) : 0.0f;
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
        sweep(train, s.front, target);
        if (runtime[size_t(train)].watched)
            runtime[size_t(train)].rests.push_back({s.part, target, target});
        s.front = target;
        s.acceleration = arrival > t ? float(-s.speed / (arrival - t)) : 0.0f;
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
            if (stop.departure >= 0 && !stop.departsEarly)
                s.until = std::max(s.until, double(stop.departure));
            s.until = departureAt(s.until);
            // Late at its booked arrival: its delay, kept while it stands
            // (Open Rails keeps the delay it ran up).
            if (stop.arrival >= 0 && t > double(stop.arrival) && t - double(stop.arrival) < 12 * 3600.0) {
                Runtime &late = runtime[size_t(train)];
                if (!late.delayKnown || t - double(stop.arrival) > late.delay) {
                    late.delay = t - double(stop.arrival);
                    late.delayKnown = true;
                }
            }
            s.nextStop = index + 1;
            record(t, train, TrainEvent::Arrived, index);
            activate(train, TrainPlan::Activation::Arrival, index, t);
            break;
        }
        case StopKind::Wait: {
            const WaitPlan &wait = runtime[size_t(train)].waits[size_t(index)];
            s.until = waitUntil(wait, t);
            s.nextWait = index + 1;
            // The autopilot's cut (task 07, part B).
            if (wait.keep >= 0) {
                cut(train, wait.keep, wait.keepFront, t);
                break;
            }
            record(t, train, TrainEvent::Waiting, index);
            // Open Rails' uncoupling waiting points (task 07): 4NNSS keeps the
            // first NN cars, 5NNSS the last.
            const WaitingPoint::Point point = WaitingPoint::decode(wait.value);
            if (point.kind == WaitingPoint::KeepFirst || point.kind == WaitingPoint::KeepLast)
                cut(train, point.cars, point.kind == WaitingPoint::KeepFirst, t);
            // 60001: it attaches to the next train standing ahead and joins
            // it; 60002: it may pass the stop signal at the end of the
            // section it stands in, into an occupied block (Open Rails'
            // permission request).
            if (point.kind == WaitingPoint::Attach)
                runtime[size_t(train)].uncondAttach = true;
            if (point.kind == WaitingPoint::Permission && size_t(s.part) < runtime[size_t(train)].ways.size()) {
                Runtime &own = runtime[size_t(train)];
                const Projection &way = own.ways[size_t(s.part)];
                size_t at = 0;
                while (at < way.sections.size() && way.sections[at].leave < s.front - 1e-6)
                    ++at;
                if (own.passed < way.stopSignals.size() && way.stopSignals[own.passed].section <= at + 1)
                    own.permission = way.stopSignals[own.passed].signal;
            }
            break;
        }
        case StopKind::Couple:
            couple(train, t);
            if (s.status == TrainStatus::Finished)
                return;
            s.until = t;
            break;
        case StopKind::Reversal:
            s.until = t;
            break;
        case StopKind::Event:
            s.until = t + EventStandLimit;
            break;
        case StopKind::End:
            record(t, train, TrainEvent::Finished);
            runtime[size_t(train)].endedAt = t;
            activate(train, TrainPlan::Activation::End, -1, t);
            // AI trains leave, giving back their track; the player's train
            // (the one it joined, task 07) stands at its path's end, and so
            // does an Open Rails timetable's train made static there (task
            // 08).
            if (train != playerTrain && !plan.staysAtEnd) {
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
        case StopKind::Created:
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
    const double rear = s.front - runtime[size_t(train)].length;
    size_t k = 0;
    while (k < way.size() && way[k].leave < rear - 1e-6)
        ++k;
    const size_t begin = k;
    double reach = s.front;
    int attach = -1;
    double contact = -1.0;
    rt.barrier = -1.0;
    rt.barrierBy = NoHolder;
    rt.barrierSection = -1;
    while (k < way.size() && way[k].enter <= s.front + 1e-6) {
        const int h = holder[size_t(way[k].section)];
        // Those cars ahead in a section it shares: its authority ends at
        // their near end.
        const auto barrier = [&](double near, int by) {
            if (near >= s.front - AtTarget && (rt.barrier < 0.0 || near < rt.barrier)) {
                rt.barrier = near;
                rt.barrierBy = by;
                rt.barrierSection = way[k].section;
            }
        };
        // A section the train it just parted from stands in is shared
        // (task 07).
        bool shared = h >= 0 && h == rt.partedFrom;
        if (shared)
            barrier(contactWith(train, h), h);
        // So is one standing trains stand in (several may, task 07), each
        // its own cut-off cars, or clear of it (Open Rails' CanPlaceTrain:
        // behind it, or off its part), or attached to when ahead (a loose
        // consist always).
        if (h == StandingConsist) {
            shared = true;
            for (int under : standingIn(way[k].section)) {
                const double ahead = contactOn(train, under);
                if (standing[size_t(under)].uncoupledFrom == train) {
                    barrier(ahead, StandingConsist);
                } else if (ahead >= s.front - AtTarget && attaches(train, under, ahead - s.front)) {
                    if (contact < 0.0 || ahead < contact) {
                        attach = under;
                        contact = ahead;
                    }
                } else if (!behind(train, under, rear) && (ahead >= 0.0 || onPart(train, under, rear))) {
                    shared = false;
                }
            }
        }
        if (h != NoHolder && h != train && !shared) {
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
    if (rt.barrier >= 0.0)
        s.authority = std::min(s.authority, rt.barrier);
    if (attach >= 0) {
        rt.attachTo = attach;
        rt.contactAt = contact;
        s.authority = contact;
    }
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
    // Attaching to a standing train (task 07): its authority ends at the
    // contact. To a living train while that one stands; else it stands
    // where it is, short of that train.
    if (rt.attachTo >= 0) {
        s.authority = rt.contactAt;
        return;
    }
    if (rt.attachTrain >= 0) {
        const TrainState &other = states[size_t(rt.attachTrain)];
        if (other.status == TrainStatus::Stopped && std::fabs(other.speed) < AtRestSpeed) {
            s.authority = rt.contactAt;
            return;
        }
        rt.attachTrain = -1;
        s.authority = std::max(s.front, rt.next > 0 && rt.next <= way.size() ? way[rt.next - 1].leave : s.front);
    }
    // Short of cars it parted from ahead in a section they share, while
    // they stand in it.
    if (rt.barrier >= 0.0) {
        const int section = rt.barrierSection;
        const bool there = rt.barrierBy == StandingConsist
                ? !standingIn(section).empty()
                : rt.barrierBy >= 0 && states[size_t(rt.barrierBy)].status != TrainStatus::Finished
                        && holds(rt.barrierBy, section);
        if (there) {
            s.authority = rt.barrier;
            s.blockedBy = rt.barrierBy;
            s.waitSection = section;
            return;
        }
        rt.barrier = -1.0;
        s.authority = std::max(s.front, rt.next > 0 && rt.next <= way.size() ? way[rt.next - 1].leave : s.front);
    }
    // Short of the first stop signal ahead (or where there is none), it
    // reserves as Open Rails' Auto Node does; past it, the blocks of the
    // signals it may clear.
    const double lookahead = std::max(AutoNodeDistance,
                                      AutoNodeTime * std::min(double(plan.maxSpeed), double(in->routeSpeedLimit)));
    const double autoLimit = std::min(s.front + lookahead, partLength);
    const size_t allowed = rt.passed + clearable(projection, rt.passed);
    ++workDone.authority;
    auto blockedAt = [&](size_t k) {
        ++workDone.sectionChecks;
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
            // One cleared for it on an occupied block is asked again beyond
            // those it may clear now: Open Rails keeps a signal's request
            // until the train passes it, its block reserved once free (task
            // 08, phase 7: Jasiona's 149, asked from further back).
            const bool askedOccupied = clearedFor[size_t(signal.signal)] == train && occupiedBlock[size_t(signal.signal)];
            if ((rt.nextSignal >= allowed && !askedOccupied)
                    || (heldAtStop(train, rt.nextSignal) && !dropHold(train, rt.nextSignal))) {
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
            // Task 08's `$follow`: a block holding a section the train it
            // follows has not passed is not clear (Open Rails' forced wait).
            if (followWait(train, rt.next, blockEnd) >= 0) {
                rt.endsAtSignal = true;
                s.atSignal = signal.signal;
                break;
            }
            bool free = true;
            for (size_t k = rt.next; k < blockEnd && free; ++k)
                free = !blockedAt(k);
            if (!free || !mayEnter(train, rt.next, blockEnd)) {
                // A block another train stands or runs in (task 07, Open
                // Rails): the signal is cleared for this train with
                // BLOCK_OCCUPIED; where it then shows more than STOP (a
                // script's STOP_AND_PROCEED or RESTRICTING), the train takes
                // the block up to that train (attaching to a standing one
                // there as anywhere).
                const int asked = signal.signal;
                const long first = free ? -1 : occupiedFrom(train, rt.next, blockEnd);
                const int clearedNow = clearedFor[size_t(asked)];
                if (first >= 0 && (clearedNow == NoHolder || clearedNow == train)) {
                    if (clearedNow != train || !occupiedBlock[size_t(asked)]) {
                        clearedFor[size_t(asked)] = train;
                        occupiedBlock[size_t(asked)] = 1;
                        clearedChanged(asked);
                        evaluateAspects();
                    }
                    // Or with permission asked at waiting point 60002, or on
                    // the autopilot's work (task 07, part B).
                    const size_t part = size_t(s.part);
                    const bool work = part < plan.workFrom.size() && plan.workFrom[part] >= 0.0
                            && signal.at >= plan.workFrom[part]
                            && (part >= plan.workTo.size() || plan.workTo[part] < 0.0 || signal.at <= plan.workTo[part]);
                    if ((permissive(asked) || rt.permission == asked || work) && mayEnter(train, rt.next, size_t(first))) {
                        take(train, rt.next, size_t(first));
                        if (size_t(first) > rt.next)
                            s.authority = std::max(s.authority, way[size_t(first) - 1].leave);
                        rt.next = size_t(first);
                        s.blockedBy = NoHolder;
                        s.waitSection = -1;
                        ++rt.nextSignal;
                        continue;
                    }
                } else if (clearedNow == train && occupiedBlock[size_t(asked)]) {
                    unclear(asked);
                }
                rt.endsAtSignal = true;
                s.atSignal = asked;
                break;
            }
            take(train, rt.next, blockEnd);
            if (blockEnd > rt.next)
                s.authority = std::max(s.authority, way[blockEnd - 1].leave);
            rt.next = blockEnd;
            clearedFor[size_t(signal.signal)] = train;
            occupiedBlock[size_t(signal.signal)] = 0;
            clearedChanged(signal.signal);
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
        // Task 08's `$follow`: up to a section the train it follows has not
        // passed.
        const long follow = followWait(train, rt.next, end);
        if (follow >= 0) {
            const int waitFor = s.blockedBy, waitIn = s.waitSection;
            if (size_t(follow) > rt.next && mayEnter(train, rt.next, size_t(follow))) {
                take(train, rt.next, size_t(follow));
                s.authority = std::max(s.authority, way[size_t(follow) - 1].leave);
                rt.next = size_t(follow);
            }
            s.blockedBy = waitFor;
            s.waitSection = waitIn;
            s.waitFor = WaitFor::Follow;
            break;
        }
        size_t k = rt.next;
        while (k < end && !blockedAt(k))
            ++k;
        if (k < end) {
            // A standing train it attaches to (task 07, Open Rails' AI: a
            // loose consist always): the sections up to it and the one it
            // stands in taken (shared with it), the authority to the contact.
            const int section = way[k].section;
            const bool standingThere = holder[size_t(section)] == StandingConsist;
            double contact = -1.0;
            const int other = standingThere ? standingAhead(train, section, &contact) : -1;
            // Standing trains there all off the rest of its part (it ends
            // short of them: a set-out beside cars, task 07): the section
            // is shared and it goes on.
            bool clear = standingThere && other < 0;
            const double rear = s.front - rt.length;
            for (int under : clear ? standingIn(section) : std::vector<int>())
                clear = clear && (behind(train, under, rear) || !onPart(train, under, rear));
            if (clear && mayEnter(train, rt.next, k + 1)) {
                take(train, rt.next, k + 1);
                rt.next = k + 1;
                s.authority = std::max(s.authority, way[k].leave);
                s.blockedBy = NoHolder;
                s.waitSection = -1;
                continue;
            }
            if (other >= 0 && attaches(train, other, contact - s.front) && mayEnter(train, rt.next, k + 1)) {
                take(train, rt.next, k + 1);
                rt.next = k + 1;
                rt.attachTo = other;
                rt.contactAt = contact;
                s.authority = contact;
                s.blockedBy = NoHolder;
                s.waitSection = -1;
            }
            // A living train standing there (Open Rails' AI, above): the
            // sections up to it taken (not its own), the authority to the
            // contact in it.
            const int living = holder[size_t(section)];
            if (other < 0 && living >= 0 && living != train) {
                const double meet = contactWith(train, living);
                if (meet >= s.front - AtTarget && meet <= way[k].leave + 1e-3 && attachesTrain(train, living, meet - s.front)
                        && mayEnter(train, rt.next, k)) {
                    take(train, rt.next, k);
                    rt.next = k;
                    rt.attachTrain = living;
                    rt.contactAt = meet;
                    s.authority = meet;
                    s.blockedBy = NoHolder;
                    s.waitSection = -1;
                }
            }
            break;
        }
        if (!mayEnter(train, rt.next, end))
            break;
        take(train, rt.next, end);
        if (end > rt.next)
            s.authority = std::max(s.authority, way[end - 1].leave);
        rt.next = end;
        // The rest of a block it went into occupied is its own now: the
        // signal's block is clear (Open Rails: reserved).
        if (end == stop && rt.nextSignal > 0) {
            const int behindSignal = stopSignals[rt.nextSignal - 1].signal;
            if (clearedFor[size_t(behindSignal)] == train && occupiedBlock[size_t(behindSignal)]) {
                occupiedBlock[size_t(behindSignal)] = 0;
                clearedChanged(behindSignal);
            }
        }
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

double SimulationCore::fullAcceleration(int train, double v) const {
    const TrainPlan &plan = in->trains[size_t(train)];
    const Performance &p = runtime[size_t(train)].performance;
    if (p.mass <= 0.0)
        return 0.0;
    double force = p.force > 0.0 ? p.force : p.power;
    if (p.power > 0.0)
        force = std::min(force, p.power / std::max(v, 1.0));
    const double resistance = p.davisA + p.davisB * v + p.davisC * v * v;
    // The gradient: the rise from the train's rear to its front.
    double gradient = 0.0;
    TrackPose front, rear;
    const TrainState &s = states[size_t(train)];
    if (runtime[size_t(train)].length > 0.0f && pose(train, s.front, front) && pose(train, s.front - runtime[size_t(train)].length, rear))
        gradient = double(front.y - rear.y) / double(runtime[size_t(train)].length);
    return (force - resistance - p.mass * 9.81 * gradient) / p.mass;
}

double SimulationCore::stoppingSpeed(int train, double v, double dt, double distance, StopKind kind,
                                     double limit) const {
    const TrainPlan &plan = in->trains[size_t(train)];
    double creep = LimitCreep;
    bool slower = false;
    if (kind == StopKind::Station) {
        creep = 0.0;
    } else if (kind == StopKind::Blocked) {
        creep = runtime[size_t(train)].endsAtSignal ? SignalCreep : AuthorityCreep;
        slower = true;
    }
    // At the creep speed (half that within the creep distance where it
    // creeps slower), stopping from it at the full deceleration.
    const double lowest = std::min(CreepSpeed, limit);
    double speed = std::min(lowest, brakingSpeed(v, dt, runtime[size_t(train)].deceleration, distance, 0.0));
    if (slower)
        speed = std::min(speed, std::max(0.5 * lowest,
                                         brakingSpeed(v, dt, runtime[size_t(train)].deceleration, distance - creep, 0.5 * lowest)));
    // Above it, the planned braking curve down to the creep distance, held
    // BrakingMargin above it (v - margin on the curve, exact over the step).
    if (distance > creep) {
        const double u = std::max(0.0, v - BrakingMargin);
        speed = std::max(speed, BrakingMargin + brakingSpeed(u, dt, BrakingShare * runtime[size_t(train)].deceleration,
                                                             distance - creep - BrakingMargin * dt, 0.0));
    }
    return speed;
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

bool SimulationCore::permissive(int signal) const {
    if (in->network == nullptr || signal < 0)
        return false;
    const TrackNetwork &network = *in->network;
    for (int head : network.signalGroups()[size_t(signal)].heads)
        if (network.isNormal(network.heads()[size_t(head)]) && headAspect[size_t(head)] > 0)
            return true;
    return false;
}

bool SimulationCore::restricted(int signal) const {
    if (in->network == nullptr || signal < 0)
        return false;
    const TrackNetwork &network = *in->network;
    int best = -1;
    bool reduce = true;
    for (int head : network.signalGroups()[size_t(signal)].heads) {
        const SignalHead &h = network.heads()[size_t(head)];
        if (!network.isNormal(h) || headAspect[size_t(head)] <= best)
            continue;
        best = headAspect[size_t(head)];
        reduce = true;
        for (const SignalKind::Aspect &aspect : network.kinds()[size_t(h.kind)].aspects)
            if (aspect.aspect == best)
                reduce = !aspect.noSpeedReduction;
    }
    // STOP_AND_PROCEED or RESTRICTING.
    return (best == 1 || best == 2) && reduce;
}

void SimulationCore::unclear(int signal) {
    clearedFor[size_t(signal)] = NoHolder;
    occupiedBlock[size_t(signal)] = 0;
    clearedChanged(signal);
}

long SimulationCore::occupiedFrom(int train, size_t from, size_t to) const {
    const TrainState &s = states[size_t(train)];
    const std::vector<ProjectedSection> &way = runtime[size_t(train)].ways[size_t(s.part)].sections;
    const std::vector<TrackSection> &sections = in->network->sections();
    long first = -1;
    for (size_t k = from; k < to && k < way.size(); ++k) {
        const ProjectedSection &mine = way[k];
        const int h = holder[size_t(mine.section)];
        if (h == NoHolder || h == train)
            continue;
        const TrackSection &section = sections[size_t(mine.section)];
        if (section.kind == TrackSection::Crossing)
            return -1;
        if (section.kind == TrackSection::Junction) {
            const int pin = mine.pinIn == 0 ? mine.pinOut : mine.pinIn;
            if ((pin == 1 || pin == 2) && switchAt[size_t(mine.section)] != pin)
                return -1;
        }
        if (h != StandingConsist) {
            // Another train: standing on it (its rear to its front), at
            // rest or running this way there; else reserved for it.
            const TrainState &o = states[size_t(h)];
            const Runtime &other = runtime[size_t(h)];
            const double rear = o.front - other.length;
            const Held *on = nullptr;
            for (const Held &held : other.held)
                if (held.section == mine.section && held.leave >= rear - 1e-6 && held.enter <= o.front + 1e-6) {
                    on = &held;
                    break;
                }
            if (on == nullptr)
                return -1;
            const bool same = section.kind == TrackSection::Junction ? (on->pinIn == 0) == (mine.pinIn == 0)
                                                                     : on->forward == mine.forward;
            if (std::fabs(o.speed) > 0.5f && !same)
                return -1;
        }
        if (first < 0)
            first = long(k);
    }
    return first;
}

void SimulationCore::evaluateAspects() {
    ++workDone.aspectRuns;
    if (in->network == nullptr) {
        aspectsAll = false;
        dirtyGroups.clear();
        dirtyJunctions.clear();
        return;
    }
    const TrackNetwork &network = *in->network;
    const std::vector<SignalHead> &heads = network.heads();
    const std::vector<SignalGroup> &groups = network.signalGroups();
    const std::vector<SignalKind> &kinds = network.kinds();
    enum { Stop = 0, StopAndProceed = 1, Restricting = 2, Approach1 = 3, Approach2 = 4, Approach3 = 5, Clear2 = 7 };
    constexpr double Reach = 20000.0;
    if (!aspectsReady) {
        aspectsReady = true;
        nextOfGroup.assign(groups.size(), -2);
        nextOfHead.assign(heads.size(), -2);
        facingOf.assign(groups.size() + heads.size(), {});
        readsFrom.assign(groups.size() + heads.size(), -1);
        watchers.assign(network.sections().size(), {});
        readers.assign(groups.size(), {});
        linkedTo.assign(network.sections().size(), {});
        queued.assign(heads.size(), 0);
        scriptSignals.assign(heads.size(), {});
        scriptJunctions.assign(heads.size(), {});
        signalScriptReaders.assign(groups.size(), {});
        junctionScriptReaders.assign(network.sections().size(), {});
        for (size_t h = 0; h < heads.size(); ++h) {
            const int junction = network.junctionSection(heads[h].junction);
            if (heads[h].junction >= 0 && junction >= 0)
                linkedTo[size_t(junction)].push_back(int(h));
        }
        aspectsAll = true;
    }
    // A reader: a signal (its NORMAL heads read the next signal), or a head
    // of another function (it reads the next from its own place); its slot
    // in facingOf and readsFrom.
    const auto slotOf = [&](int reader) { return reader >= 0 ? size_t(reader) : groups.size() + size_t(-reader - 1); };
    // Forgets where a reader's next signal is: off its junctions' and its
    // next's lists.
    const auto forget = [&](int reader) {
        const size_t slot = slotOf(reader);
        for (int junction : facingOf[slot]) {
            std::vector<int> &list = watchers[size_t(junction)];
            list.erase(std::remove(list.begin(), list.end(), reader), list.end());
        }
        facingOf[slot].clear();
        if (readsFrom[slot] >= 0) {
            std::vector<int> &list = readers[size_t(readsFrom[slot])];
            list.erase(std::remove(list.begin(), list.end(), reader), list.end());
            readsFrom[slot] = -1;
        }
        if (reader >= 0)
            nextOfGroup[size_t(reader)] = -2;
        else
            nextOfHead[size_t(-reader - 1)] = -2;
    };
    // The next stop signal a reader reads, found once until a switch on the
    // way changes.
    const auto nextOf = [&](int reader) {
        int &known = reader >= 0 ? nextOfGroup[size_t(reader)] : nextOfHead[size_t(-reader - 1)];
        if (known != -2)
            return known;
        const size_t slot = slotOf(reader);
        if (reader >= 0) {
            const SignalGroup &group = groups[size_t(reader)];
            known = network.nextStopSignal(group.node, group.distance, group.forward, switchAt, Reach, &facingOf[slot],
                                           reader);
        } else {
            const SignalHead &head = heads[size_t(-reader - 1)];
            known = network.nextStopSignal(head.node, head.distance, head.forward, switchAt, Reach, &facingOf[slot],
                                           head.signal);
        }
        for (int junction : facingOf[slot])
            watchers[size_t(junction)].push_back(reader);
        if (known >= 0) {
            readers[size_t(known)].push_back(reader);
            readsFrom[slot] = known;
        }
        return known;
    };
    std::vector<int> work;
    const auto push = [&](int h) {
        if (!queued[size_t(h)]) {
            queued[size_t(h)] = 1;
            work.push_back(h);
        }
    };
    const auto pushReader = [&](int reader) {
        if (reader >= 0) {
            for (int h : groups[size_t(reader)].heads)
                push(h);
        } else {
            push(-reader - 1);
        }
    };
    // What changed: every head the first time; the heads of signals cleared
    // or given back; those reading through a switch set, or linked to it.
    if (aspectsAll)
        for (size_t h = 0; h < heads.size(); ++h)
            push(int(h));
    aspectsAll = false;
    for (int junction : dirtyJunctions) {
        const std::vector<int> through = watchers[size_t(junction)];
        for (int reader : through) {
            forget(reader);
            pushReader(reader);
        }
        for (int h : linkedTo[size_t(junction)])
            push(h);
        const std::vector<int> scripted = junctionScriptReaders[size_t(junction)];
        for (int h : scripted)
            push(h);
    }
    for (int signal : dirtyGroups)
        for (int h : groups[size_t(signal)].heads)
            push(h);
    dirtyJunctions.clear();
    dirtyGroups.clear();
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
        // A caution beyond the type's last one is its clearest aspect (as
        // the stock scripts have it: a three-aspect signal before a second
        // caution clears), so cautions do not repeat signal after signal.
        int lastCaution = -1;
        bool has = false;
        for (const SignalKind::Aspect &a : kind.aspects) {
            if (a.aspect >= Approach1 && a.aspect <= Approach3)
                lastCaution = std::max(lastCaution, a.aspect);
            has = has || a.aspect == wanted;
        }
        if (wanted <= Stop) {
            chosen = &kind.aspects.front();
        } else if (!has && wanted <= Approach3 && lastCaution >= Approach1 && wanted > lastCaution) {
            chosen = &kind.aspects.back();
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
    // A signal's least restrictive NORMAL head (next_sig_lr), STOP for none.
    auto leastRestrictive = [&](int signal) {
        int best = Stop;
        if (signal < 0)
            return best;
        for (int head : groups[size_t(signal)].heads)
            if (network.isNormal(heads[size_t(head)]))
                best = std::max(best, headAspect[size_t(head)]);
        return best;
    };
    // Signal scripts (task 05): what a head's script reads is recorded, so
    // that it runs again when that changes; its signal's next stop signal
    // through nextOf (kept, its readers told), any other search here.
    const auto unregister = [&](int h) {
        for (int signal : scriptSignals[size_t(h)]) {
            std::vector<int> &list = signalScriptReaders[size_t(signal)];
            list.erase(std::remove(list.begin(), list.end(), h), list.end());
        }
        for (int junction : scriptJunctions[size_t(h)]) {
            std::vector<int> &list = junctionScriptReaders[size_t(junction)];
            list.erase(std::remove(list.begin(), list.end(), h), list.end());
        }
        scriptSignals[size_t(h)].clear();
        scriptJunctions[size_t(h)].clear();
    };
    struct Host final : SignalScriptHost {
        SimulationCore &core;
        const TrackNetwork &network;
        const int h;
        const SignalHead &head;
        const SignalKind &kind;
        const std::function<int(int)> nextStop;
        Host(SimulationCore &core, const TrackNetwork &network, int h, std::function<int(int)> nextStop)
            : core(core), network(network), h(h), head(network.heads()[size_t(h)]),
              kind(network.kinds()[size_t(head.kind)]), nextStop(std::move(nextStop)) {}
        void read(int signal) {
            if (signal < 0)
                return;
            std::vector<int> &list = core.scriptSignals[size_t(h)];
            if (std::find(list.begin(), list.end(), signal) != list.end())
                return;
            list.push_back(signal);
            core.signalScriptReaders[size_t(signal)].push_back(h);
        }
        void readThrough(const std::vector<int> &junctions) {
            std::vector<int> &list = core.scriptJunctions[size_t(h)];
            for (int junction : junctions)
                if (std::find(list.begin(), list.end(), junction) == list.end()) {
                    list.push_back(junction);
                    core.junctionScriptReaders[size_t(junction)].push_back(h);
                }
        }
        // A signal's heads of a function: their least (most) restrictive
        // aspect; -1 none.
        int aspectOf(int signal, int function, bool least) const {
            int found = -1;
            for (int g : network.signalGroups()[size_t(signal)].heads) {
                const SignalHead &other = network.heads()[size_t(g)];
                if (other.kind < 0 || int(network.kinds()[size_t(other.kind)].function) != function)
                    continue;
                const int a = core.headAspect[size_t(g)];
                found = found < 0 ? a : least ? std::max(found, a) : std::min(found, a);
            }
            return found;
        }
        // The next signal from a signal's place with a head of `function`
        // (facing the other way: opposite).
        int after(int signal, int function, bool opposite) {
            const SignalGroup &group = network.signalGroups()[size_t(signal)];
            std::vector<int> facing;
            const int found = network.nextSignalWhere(
                group.node, group.distance, group.forward, core.switchAt, 20000.0, &facing,
                [this, function](const SignalHead &other) {
                    return other.kind >= 0 && int(network.kinds()[size_t(other.kind)].function) == function;
                }, opposite, signal);
            readThrough(facing);
            read(found);
            return found;
        }
        int enabled() override { return core.clearedFor[size_t(head.signal)] != NoHolder ? 1 : 0; }
        // Clear while cleared for a train (its block reserved for it),
        // occupied while cleared into a block another train is in (task
        // 07); else, as Open Rails without a train's request, obstructed.
        int blockState() override {
            if (!enabled())
                return BlockJunctionObstructed;
            return core.occupiedBlock[size_t(head.signal)] ? BlockOccupied : BlockClear;
        }
        int routeSet() override {
            const int junction = network.junctionSection(head.junction);
            return head.junction < 0 || junction < 0 || core.switchAt[size_t(junction)] == head.junctionPin ? 1 : 0;
        }
        int nextSignal(int function, bool least, int nth) override {
            nth = std::max(1, nth);
            int signal = head.signal;
            int aspect = 0;
            for (int i = 0; i < nth; ++i) {
                const int found = function == int(SignalFunction::Normal) && i == 0 ? nextStop(signal)
                                                                                    : after(signal, function, false);
                if (found < 0)
                    return 0;
                aspect = aspectOf(found, function, least);
                // Open Rails' next_nsig_lr stops at a signal at STOP.
                if (aspect <= 0)
                    return 0;
                signal = found;
            }
            return aspect;
        }
        int thisSignal(int function, bool least) override {
            read(head.signal);
            const int found = aspectOf(head.signal, function, least);
            // Open Rails: none of the function gives CLEAR_2 for NORMAL,
            // else STOP.
            return found >= 0 ? found : function == int(SignalFunction::Normal) ? 7 : 0;
        }
        int oppositeSignal(int function, bool least) override {
            if (!enabled())
                return 0;
            const int found = after(head.signal, function, true);
            return found >= 0 ? std::max(0, aspectOf(found, function, least)) : 0;
        }
        // The most restrictive `function1` head up to (and on) the next
        // signal with a `function2` head; STOP for none.
        int distantMulti(int function1, int function2) override {
            int signal = head.signal, worst = -1;
            for (int i = 0; i < 64; ++i) {
                const SignalGroup &group = network.signalGroups()[size_t(signal)];
                std::vector<int> facing;
                const int found = network.nextSignalWhere(
                    group.node, group.distance, group.forward, core.switchAt, 20000.0, &facing,
                    [this, function1, function2](const SignalHead &other) {
                        if (other.kind < 0)
                            return false;
                        const int f = int(network.kinds()[size_t(other.kind)].function);
                        return f == function1 || f == function2;
                    }, false, signal);
                readThrough(facing);
                if (found < 0)
                    break;
                read(found);
                const int here = aspectOf(found, function1, false);
                if (here >= 0)
                    worst = worst < 0 ? here : std::min(worst, here);
                if (aspectOf(found, function2, false) >= 0)
                    break;
                signal = found;
            }
            return std::max(0, worst);
        }
        int feature(int feature) override {
            return feature >= 0 && feature < 6 ? int((head.flags >> (4 + feature)) & 1u) : 0;
        }
        int drawStateOf(int aspect) override {
            for (const SignalKind::Aspect &a : kind.aspects)
                if (a.aspect == aspect)
                    return a.drawState;
            return -1;
        }
    };
    // Each head from the aspects it reads now; a head that changes puts
    // those reading it on the list again (a ring of signals is cut off).
    bool changed = false;
    size_t i = 0;
    const size_t limit = 8 * heads.size() + 64;
    for (; i < work.size() && i < limit; ++i) {
        const int h = work[i];
        queued[size_t(h)] = 0;
        ++workDone.aspectHeads;
        const SignalHead &head = heads[size_t(h)];
        int aspect = Stop, draw = 0;
        if (head.kind >= 0 && kinds[size_t(head.kind)].rule == SignalKind::Rule::Script
                && kinds[size_t(head.kind)].script >= 0) {
            // Its script (task 05): state and draw_state kept from its last
            // run; a draw state left out (or none) is the aspect's.
            const SignalKind &kind = kinds[size_t(head.kind)];
            unregister(h);
            Host host(*this, network, h, [&](int signal) { return nextOf(signal); });
            int state = headAspect[size_t(h)], drawState = headDraw[size_t(h)];
            RunSignalScript(network.scripts[size_t(kind.script)], host, state, drawState);
            aspect = state;
            draw = drawState >= 0 ? drawState : host.drawStateOf(state);
            if (draw < 0)
                draw = kind.defaultDrawState;
        } else if (head.kind >= 0 && kinds[size_t(head.kind)].rule == SignalKind::Rule::Basic) {
            // A type without a script on a route with scripts: Open Rails'
            // least restrictive aspect while cleared (its block clear), else
            // its most.
            const SignalKind &kind = kinds[size_t(head.kind)];
            const bool cleared = clearedFor[size_t(head.signal)] != NoHolder && !occupiedBlock[size_t(head.signal)];
            if (kind.aspects.empty()) {
                aspect = Stop;
                draw = kind.defaultDrawState;
            } else {
                aspect = cleared ? kind.aspects.back().aspect : kind.aspects.front().aspect;
                draw = cleared ? kind.aspects.back().drawState : kind.aspects.front().drawState;
            }
        } else if (head.kind >= 0) {
            const SignalKind &kind = kinds[size_t(head.kind)];
            int wanted = -1;
            switch (kind.function) {
            case SignalFunction::Normal: {
                const bool cleared = clearedFor[size_t(head.signal)] != NoHolder && !occupiedBlock[size_t(head.signal)];
                const int junction = network.junctionSection(head.junction);
                const bool routeSet = head.junction < 0 || junction < 0 || switchAt[size_t(junction)] == head.junctionPin;
                wanted = !cleared || !routeSet ? Stop : progression(leastRestrictive(nextOf(head.signal)));
                break;
            }
            case SignalFunction::Distant: {
                const int next = leastRestrictive(nextOf(-h - 1));
                wanted = next <= Stop ? Stop : progression(next);
                break;
            }
            case SignalFunction::Repeater:
                wanted = leastRestrictive(nextOf(-h - 1));
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
        if (aspect == headAspect[size_t(h)] && draw == headDraw[size_t(h)])
            continue;
        const bool aspectChanged = aspect != headAspect[size_t(h)];
        headAspect[size_t(h)] = aspect;
        headDraw[size_t(h)] = draw;
        changed = true;
        if (aspectChanged && network.isNormal(head)) {
            const std::vector<int> reading = readers[size_t(head.signal)];
            for (int reader : reading)
                pushReader(reader);
        }
        if (aspectChanged) {
            const std::vector<int> scripted = signalScriptReaders[size_t(head.signal)];
            for (int reader : scripted)
                push(reader);
        }
    }
    for (; i < work.size(); ++i)
        queued[size_t(work[i])] = 0;
    if (changed)
        ++drawChanges;
}

void SimulationCore::clearedChanged(int signal) {
    dirtyGroups.push_back(signal);
}

void SimulationCore::switchChanged(int junction) {
    dirtyJunctions.push_back(junction);
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
    // Only parts that pass a section in common can meet: the trains' parts
    // by section; for each train the later trains' parts it meets, once,
    // in the order of trains and parts.
    struct Part {
        int train;
        int part;
    };
    std::vector<std::vector<Part>> through(in->network->sections().size());
    std::vector<size_t> firstSlot(in->trains.size() + 1, 0);
    for (size_t t = 0; t < in->trains.size(); ++t) {
        firstSlot[t + 1] = firstSlot[t] + runtime[t].ways.size();
        for (size_t p = 0; p < runtime[t].ways.size(); ++p)
            for (const ProjectedSection &pass : runtime[t].ways[p].sections) {
                std::vector<Part> &list = through[size_t(pass.section)];
                if (list.empty() || list.back().train != int(t) || list.back().part != int(p))
                    list.push_back({int(t), int(p)});
            }
    }
    std::vector<size_t> seen(firstSlot.back(), 0);
    size_t visit = 0;
    std::vector<std::array<int, 3>> met;
    for (size_t a = 0; a < in->trains.size(); ++a) {
        met.clear();
        for (size_t pa = 0; pa < runtime[a].ways.size(); ++pa) {
            ++visit;
            for (const ProjectedSection &pass : runtime[a].ways[pa].sections)
                for (const Part &other : through[size_t(pass.section)]) {
                    size_t &mark = seen[firstSlot[size_t(other.train)] + size_t(other.part)];
                    if (other.train > int(a) && mark != visit) {
                        mark = visit;
                        met.push_back({other.train, int(pa), other.part});
                    }
                }
        }
        std::sort(met.begin(), met.end());
        for (const std::array<int, 3> &pair : met)
            addStretches(int(a), pair[1], pair[0], pair[2]);
    }
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
    // Loops where one of them has a passing path it fits on are places to
    // pass too (Open Rails' path-based rule): the main route between the
    // junctions the path leaves and rejoins by is not single track for them.
    std::unordered_set<int> onA;
    for (const ProjectedSection &pass : wayA)
        onA.insert(pass.section);
    std::vector<char> loopA, loopB;
    loopsOf(a, pa, loopA, [&onB](int section) { return onB.count(section) > 0; });
    loopsOf(b, pb, loopB, [&onA](int section) { return onA.count(section) > 0; });
    auto shared = [&](size_t i) -> long {
        if (loopA[i])
            return -1;
        auto found = onB.find(wayA[i].section);
        if (found == onB.end() || loopB[found->second])
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
            // A loop with a passing path is one whatever the main track's
            // length (the passing path's was checked).
            bool loop = false;
            for (size_t k = last.to; k < run.from && !loop; ++k)
                loop = loopA[k] != 0;
            for (size_t k = run.high; apart && k < last.low && !loop; ++k)
                loop = loopB[k] != 0;
            if (!loop && (!open || (gapA < runtime[size_t(a)].length + SignalMargin
                                    && gapB < runtime[size_t(b)].length + SignalMargin))) {
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

size_t SimulationCore::junctionNear(const std::vector<ProjectedSection> &way, double at, size_t start) const {
    const std::vector<TrackSection> &sections = in->network->sections();
    auto first = std::lower_bound(way.begin() + long(std::min(start, way.size())), way.end(), at - 1.0,
                                  [](const ProjectedSection &p, double d) { return p.enter < d; });
    for (auto i = first; i != way.end() && i->enter <= at + 1.0; ++i)
        if (sections[size_t(i->section)].kind == TrackSection::Junction)
            return size_t(i - way.begin());
    return way.size();
}

void SimulationCore::loopsOf(int train, int part, std::vector<char> &marks,
                            const std::function<bool(int)> &usedByOther) const {
    const Runtime &rt = runtime[size_t(train)];
    const std::vector<ProjectedSection> &way = rt.ways[size_t(part)].sections;
    marks.assign(way.size(), 0);
    for (size_t p = 0; p < rt.passing.size(); ++p) {
        const PassingPlan &passing = rt.passing[p];
        if (passing.part != part || passing.track.length() < runtime[size_t(train)].length + SignalMargin)
            continue;
        bool theirs = false;
        for (int section : rt.passingSections[p])
            theirs = theirs || usedByOther(section);
        if (theirs)
            continue;
        const size_t leaves = junctionNear(way, passing.from, 0);
        const size_t rejoins = junctionNear(way, passing.to, leaves < way.size() ? leaves : 0);
        if (leaves >= way.size() || rejoins >= way.size() || rejoins <= leaves)
            continue;
        for (size_t k = leaves + 1; k < rejoins; ++k)
            marks[k] = 1;
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
        ++claimChanges;
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
    return s.status != TrainStatus::Waiting && s.front - runtime[size_t(train)].length > stretch.leave[side] - 1e-6;
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
    if (!claim.empty())
        ++claimChanges;
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
        const size_t leaves = junctionNear(way, passing.from, from);
        if (leaves < from || leaves >= to)
            continue;
        const size_t rejoins = junctionNear(way, passing.to, leaves);
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
    rt.limits[part] = SpeedLimits(rt.ways[part], *in->network, runtime[size_t(train)].passenger, {}, 0.0f, rt.parts[part].length());
    rt.limit = 0;
    for (StopPlan &stop : rt.stops)
        if (size_t(stop.part) == part) {
            stop.front = moved(stop.front);
            stop.platformEnd = moved(stop.platformEnd);
            stop.platformStart = moved(stop.platformStart);
        }
    for (WaitPlan &wait : rt.waits)
        if (size_t(wait.part) == part)
            wait.front = moved(wait.front);
    rt.passing.erase(rt.passing.begin() + long(index));
    rt.passingSections.erase(rt.passingSections.begin() + long(index));
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
    findExits(train);
    if (rt.watched)
        findSpots(train, int(part));
}

void SimulationCore::findExits(int train) {
    Runtime &rt = runtime[size_t(train)];
    rt.exits.assign(rt.stops.size(), -1);
    rt.exitSignals.assign(rt.stops.size(), -1);
    if (in->network == nullptr)
        return;
    const std::vector<TrackSection> &sections = in->network->sections();
    for (size_t k = 0; k < rt.stops.size(); ++k) {
        const StopPlan &stop = rt.stops[k];
        if (size_t(stop.part) >= rt.ways.size())
            continue;
        const Projection &way = rt.ways[size_t(stop.part)];
        // The first stop signal beyond the platform.
        const auto beyond = std::lower_bound(way.stopSignals.begin(), way.stopSignals.end(), stop.platformEnd - 1.0,
                                             [](const ProjectedSignal &p, double at) { return p.at < at; });
        if (beyond == way.stopSignals.end())
            continue;
        const size_t index = size_t(beyond - way.stopSignals.begin());
        // Open Rails' end signal: on the platform's node (crossings
        // between), within 150 m, or further on with a junction or crossing
        // in its block (a signal without a fixed route).
        auto at = std::upper_bound(way.sections.begin(), way.sections.end(), stop.platformEnd,
                                   [](double d, const ProjectedSection &p) { return d < p.enter; });
        size_t from = at == way.sections.begin() ? 0 : size_t(at - way.sections.begin()) - 1;
        const int node = from < way.sections.size() ? sections[size_t(way.sections[from].section)].node : -1;
        bool sameNode = from < way.sections.size() && sections[size_t(way.sections[from].section)].kind == TrackSection::Track;
        for (size_t i = from; i < beyond->section && i < way.sections.size() && sameNode; ++i) {
            const TrackSection &section = sections[size_t(way.sections[i].section)];
            sameNode = section.kind == TrackSection::Crossing
                    || (section.kind == TrackSection::Track && section.node == node);
        }
        // Open Rails' timetable mode: on the platform's node at any
        // distance; `$forcehold`, `$forcewait`: wherever it is (task 08).
        if (!sameNode && stop.exit != StopPlan::Exit::First)
            continue;
        bool held = stop.exit != StopPlan::Exit::Activity || beyond->at - stop.platformEnd <= ExitSignalReach;
        const size_t blockEnd = index + 1 < way.stopSignals.size() ? way.stopSignals[index + 1].section
                                                                  : way.sections.size();
        for (size_t i = beyond->section; i < blockEnd && i < way.sections.size() && !held; ++i)
            held = sections[size_t(way.sections[i].section)].kind != TrackSection::Track;
        const double clearing = stop.signalClearing >= 0.0 ? stop.signalClearing : StationClearing;
        // Its exit signal when the train stops short of it, or stopping at it
        // most of the train (60 %) stands at the platform; held not again
        // once let go.
        if (!held || (stop.front >= beyond->at
                      && beyond->at - clearing - stop.platformStart <= 0.6 * runtime[size_t(train)].length))
            continue;
        rt.exitSignals[k] = int(index);
        if (k < rt.letGo.size() && rt.letGo[k])
            continue;
        // Short of it, as Open Rails stops a train at its platform's end
        // signal; held there when the stop holds it. The platform's end
        // just short of it (`$extendplatformtosignal`), the stop centred on
        // that.
        if (stop.holdsExit)
            rt.exits[k] = int(index);
        StopPlan &own = rt.stops[k];
        if (own.extendToSignal && beyond->at - CloseupClearing - 1.0 > own.platformEnd) {
            own.platformEnd = beyond->at - CloseupClearing - 1.0;
            own.front = std::max(own.front, own.platformEnd - 0.5 * (own.platformEnd - own.platformStart
                                                                     - runtime[size_t(train)].length));
        }
        own.front = std::min(own.front, beyond->at - clearing - 1.0);
    }
}

bool SimulationCore::heldAtStop(int train, size_t signal) const {
    const TrainState &s = states[size_t(train)];
    const Runtime &rt = runtime[size_t(train)];
    const bool standing = s.status == TrainStatus::Stopped && s.stoppedAt == StopKind::Station;
    // Open Rails lists the stops' exit signals, a signal once per stop: a
    // stop takes it off the list a while before its departure and, still
    // listed (another stop's), again as the train leaves. Held while listed
    // and the stop it stands at or one ahead on this part has it.
    const int at = standing ? s.nextStop - 1 : s.nextStop;
    if (at < 0 || at >= int(rt.stops.size()))
        return false;
    int listed = 0;
    bool ahead = false;
    for (size_t k = 0; k < rt.stops.size(); ++k)
        if (rt.stops[k].part == s.part && rt.exits[k] == int(signal)) {
            listed -= int(k) < at ? 1 : -1;
            ahead = ahead || int(k) >= at;
        }
    if (standing && rt.exits[size_t(at)] == int(signal))
        listed -= (now >= s.until - HoldRelease - stepSeconds ? 1 : 0) + (now >= s.until - stepSeconds ? 1 : 0);
    return ahead && listed > 0;
}

bool SimulationCore::holdDroppable(int train, size_t signal) const {
    // Open Rails lets a hold go when the train stands with the held signal
    // next and the stop it stands at, or comes to next, is not the signal's.
    const TrainState &s = states[size_t(train)];
    const Runtime &rt = runtime[size_t(train)];
    if (s.status != TrainStatus::Stopped || signal != rt.passed || !heldAtStop(train, signal))
        return false;
    const int at = s.stoppedAt == StopKind::Station ? s.nextStop - 1 : s.nextStop;
    return at >= 0 && size_t(at) < rt.stops.size()
            && (rt.stops[size_t(at)].part != s.part || rt.exits[size_t(at)] != int(signal));
}

long SimulationCore::followWait(int train, size_t from, size_t to) {
    const TrainPlan &plan = in->trains[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    TrainState &s = states[size_t(train)];
    const std::vector<ProjectedSection> &way = rt.ways[size_t(s.part)].sections;
    while (rt.nextFollow < plan.follows.size() && from < to) {
        const FollowPlan &follow = plan.follows[rt.nextFollow];
        // Passed (an earlier part, or behind what it holds), or further on.
        if (follow.part < s.part || (follow.part == s.part && follow.at < way[from].enter - Same)) {
            ++rt.nextFollow;
            continue;
        }
        if (follow.part > s.part || follow.at > way[to - 1].leave + Same)
            return -1;
        size_t k = from;
        while (k < to && way[k].section != follow.section)
            ++k;
        if (k >= to) {
            ++rt.nextFollow;
            continue;
        }
        // Open Rails (TTTrain.CheckWaitCondition): it waits while the other
        // train runs the part of its route with the section and its rear has
        // not left it; while it has not started or runs an earlier part only
        // with `/notstarted`; else the point is let go for good. Phase 8's
        // qualifiers: `/owndelay` (only while this train is so much later
        // than the other, its own delay known), else `/maxdelay` (only while
        // the other, its delay known, is less late than that plus this one's
        // delay).
        const TrainState &other = states[size_t(follow.train)];
        const Runtime &theirs = runtime[size_t(follow.train)];
        double ownDelay = 0.0, otherDelay = 0.0;
        const bool ownKnown = delayOf(train, now, ownDelay);
        const bool otherKnown = other.status != TrainStatus::Waiting && delayOf(follow.train, now, otherDelay);
        const bool ownLateEnough = follow.ownDelay < 0 || (ownKnown && ownDelay > double(follow.ownDelay));
        bool wait = false;
        if (other.status == TrainStatus::Waiting
                || (other.status != TrainStatus::Finished && other.part < follow.otherPart)) {
            wait = follow.notStarted && ownLateEnough;
        } else if (other.status != TrainStatus::Finished && other.part == follow.otherPart
                   && other.front - theirs.length < follow.otherLeave - Same) {
            if (follow.ownDelay >= 0)
                wait = ownLateEnough && ownDelay - (otherKnown ? otherDelay : 0.0) > double(follow.ownDelay);
            else
                wait = follow.maxDelay < 0 || !otherKnown
                        || otherDelay < double(follow.maxDelay) + (ownKnown ? ownDelay : 0.0);
        }
        // Not where the other train would have to pass this one first (Open
        // Rails' check against a deadlock): track this train holds lies on
        // the other's way before the section.
        if (wait && size_t(other.part) < theirs.ways.size()) {
            const std::vector<ProjectedSection> &ahead = theirs.ways[size_t(other.part)].sections;
            for (size_t j = theirs.next; j < ahead.size() && wait; ++j) {
                if (other.part == follow.otherPart && ahead[j].enter >= follow.otherLeave - Same)
                    break;
                wait = holder[size_t(ahead[j].section)] != train;
            }
        }
        if (!wait) {
            ++rt.nextFollow;
            continue;
        }
        s.blockedBy = follow.train;
        s.waitSection = follow.section;
        s.waitFor = WaitFor::Follow;
        return long(k);
    }
    return -1;
}

bool SimulationCore::waitsAtExit(int train) const {
    const TrainState &s = states[size_t(train)];
    const Runtime &rt = runtime[size_t(train)];
    const int stop = s.nextStop - 1;
    if (in->network == nullptr || s.stoppedAt != StopKind::Station || stop < 0 || size_t(stop) >= rt.stops.size()
            || !rt.stops[size_t(stop)].waitsForSignal || size_t(stop) >= rt.exitSignals.size()
            || rt.stops[size_t(stop)].part != s.part || size_t(s.part) >= rt.ways.size())
        return false;
    // Its exit signal, the next it has not passed (Open Rails: the next
    // signal is the stop's exit signal), at STOP.
    const int exit = rt.exitSignals[size_t(stop)];
    if (exit < 0 || size_t(exit) != rt.passed)
        return false;
    const int signal = rt.ways[size_t(s.part)].stopSignals[size_t(exit)].signal;
    int aspect = 0;
    for (int head : in->network->signalGroups()[size_t(signal)].heads)
        if (in->network->isNormal(in->network->heads()[size_t(head)]))
            aspect = std::max(aspect, headAspect[size_t(head)]);
    return aspect == 0;
}

bool SimulationCore::dropHold(int train, size_t signal) {
    if (!holdDroppable(train, signal))
        return false;
    Runtime &rt = runtime[size_t(train)];
    rt.letGo.resize(rt.stops.size(), false);
    for (size_t k = 0; k < rt.stops.size(); ++k)
        if (rt.stops[k].part == states[size_t(train)].part && rt.exits[k] == int(signal)) {
            rt.exits[k] = -1;
            rt.letGo[k] = true;
        }
    return true;
}

void SimulationCore::take(int train, size_t from, size_t to) {
    const TrainPlan &plan = in->trains[size_t(train)];
    const std::vector<ProjectedSection> &way = runtime[size_t(train)].ways[size_t(states[size_t(train)].part)].sections;
    const std::vector<TrackSection> &sections = in->network->sections();
    Runtime &rt = runtime[size_t(train)];
    for (size_t k = from; k < to; ++k) {
        const ProjectedSection &pass = way[k];
        holder[size_t(pass.section)] = train;
        heldStamp[size_t(pass.section)] = ++holdChanges;
        rt.held.push_back({pass.section, pass.enter, pass.leave, pass.forward, pass.pinIn});
        // A junction set the train's way: its output where it goes out by
        // one, the output it comes in by trailing.
        if (sections[size_t(pass.section)].kind == TrackSection::Junction) {
            const int pin = pass.pinIn == 0 ? pass.pinOut : pass.pinIn;
            if ((pin == 1 || pin == 2) && switchAt[size_t(pass.section)] != pin) {
                switchAt[size_t(pass.section)] = pin;
                switchChanged(pass.section);
            }
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
            // A driven train past a signal showing STOP: at danger (task
            // driving 02; it is not stopped). Its aspect as the train sees it:
            // its normal heads' clearest (nextSignalAhead's).
            int shown = 0;
            if (rt.drivenState && in->network != nullptr)
                for (int head : in->network->signalGroups()[size_t(stopSignals[rt.passed].signal)].heads)
                    if (in->network->isNormal(in->network->heads()[size_t(head)]))
                        shown = std::max(shown, headAspect[size_t(head)]);
            if (rt.drivenState && shown == 0) {
                TrainEvent passedAtDanger;
                passedAtDanger.time = now;
                passedAtDanger.train = train;
                passedAtDanger.kind = TrainEvent::PassedAtDanger;
                passedAtDanger.signal = stopSignals[rt.passed].signal;
                passedAtDanger.travelled = s.travelled;
                events.push_back(passedAtDanger);
            }
            if (clearedFor[size_t(stopSignals[rt.passed].signal)] == train)
                unclear(stopSignals[rt.passed].signal);
            if (rt.permission == stopSignals[rt.passed].signal)
                rt.permission = -1;
            ++rt.passed;
        }
    }
    // A driven train keeps some track behind it, to back up on.
    const double rear = s.front - runtime[size_t(train)].length
            - (rt.drivenState ? DrivenKeepBehind : 0.0);
    // Stretches its rear has left are no longer its own.
    if (size_t(train) < stretchesOf.size() && size_t(s.part) < stretchesOf[size_t(train)].size())
        for (const auto &[stretch, side] : stretchesOf[size_t(train)][size_t(s.part)])
            if (claimedBy[size_t(stretch)] == train && rear > single[size_t(stretch)].leave[side] - 1e-6) {
                claimedBy[size_t(stretch)] = NoHolder;
                ++claimChanges;
            }
    while (!rt.held.empty() && rt.held.front().leave < rear - 1e-6) {
        const int section = rt.held.front().section;
        rt.held.pop_front();
        // Not while it holds it again further on (a loop passed twice).
        bool again = false;
        for (const Held &other : rt.held)
            again = again || other.section == section;
        if (!again && holder[size_t(section)] == train)
            freeSection(section, train);
    }
}

void SimulationCore::releaseAll(int train) {
    Runtime &rt = runtime[size_t(train)];
    const std::deque<Held> given = std::move(rt.held);
    rt.held.clear();
    for (const Held &held : given)
        if (holder[size_t(held.section)] == train)
            freeSection(held.section, train);
    rt.next = 0;
    rt.nextSignal = 0;
    rt.passed = 0;
    rt.endsAtSignal = false;
    rt.barrier = -1.0;
    for (size_t signal = 0; signal < clearedFor.size(); ++signal)
        if (clearedFor[signal] == train)
            unclear(int(signal));
    for (int &claim : claimedBy)
        if (claim == train) {
            claim = NoHolder;
            ++claimChanges;
        }
    rt.limit = 0;
    states[size_t(train)].authority = 0.0;
    states[size_t(train)].signalSpeed = -1.0f;
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
        uses[k].occupied = holder[k] == StandingConsist || (k < standingAt.size() && standingAt[k] >= 0);
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
    if (train < 0 || size_t(train) >= states.size() || !in->geometry)
        return false;
    const TrainPlan &plan = in->trains[size_t(train)];
    const TrainState &state = states[size_t(train)];
    if (state.part < 0 || size_t(state.part) >= runtime[size_t(train)].parts.size())
        return false;
    const RoutePoint point = runtime[size_t(train)].parts[size_t(state.part)].locate(routeDistance);
    if (!in->geometry->pose(point.node, point.distance, out))
        return false;
    out.node = point.node;
    out.distance = point.distance;
    out.forward = point.forward;
    if (!point.forward)
        out.roll = -out.roll;
    return true;
}

bool SimulationCore::standingEnds(int standingTrain, std::vector<TrackPose> &fronts, std::vector<TrackPose> &rears) const {
    fronts.clear();
    rears.clear();
    if (standingTrain < 0 || size_t(standingTrain) >= standing.size() || !in->geometry)
        return false;
    const Standing &st = standing[size_t(standingTrain)];
    if (st.gone || st.track.empty())
        return false;
    RouteTrack track;
    for (const RouteInterval &interval : st.track)
        track.append(interval.node, interval.from, interval.to);
    const auto at = [&](double distance, TrackPose &out) {
        const RoutePoint point = track.locate(std::clamp(distance, 0.0, track.length()));
        if (!in->geometry->pose(point.node, point.distance, out))
            return;
        out.node = point.node;
        out.distance = point.distance;
        out.forward = point.forward;
        // As a train running the other way (vehicleEnds, reversed).
        if (point.forward)
            out.roll = -out.roll;
    };
    double before = 0.0;
    for (const Vehicle &vehicle : st.vehicles) {
        TrackPose front, rear;
        at(before, front);
        at(before + vehicle.length(), rear);
        fronts.push_back(front);
        rears.push_back(rear);
        before += vehicle.length();
    }
    return true;
}

bool SimulationCore::vehicleEnds(int train, std::vector<TrackPose> &fronts, std::vector<TrackPose> &rears,
                                 double ahead) const {
    fronts.clear();
    rears.clear();
    if (train < 0 || size_t(train) >= states.size())
        return false;
    const TrainPlan &plan = in->trains[size_t(train)];
    const TrainState &state = states[size_t(train)];
    if (state.status == TrainStatus::Waiting || state.status == TrainStatus::Finished)
        return false;
    if (runtime[size_t(train)].drivenState)
        return drivenEnds(train, fronts, rears);
    const double trainFront = ahead > 0.0 ? std::min(state.front + ahead, std::max(state.front, state.authority))
                                          : state.front;
    double before = 0.0;
    for (const Vehicle &vehicle : runtime[size_t(train)].vehicles) {
        // Not reversed: the vehicle's front is `before` behind the train's
        // front. Reversed: the consist runs backwards, its first vehicle at
        // the train's rear.
        const double frontAt = state.reversed ? trainFront - runtime[size_t(train)].length + before : trainFront - before;
        const double rearAt = state.reversed ? frontAt + vehicle.length() : frontAt - vehicle.length();
        TrackPose front, rear;
        pose(train, frontAt, front);
        pose(train, rearAt, rear);
        if (state.reversed) {
            front.roll = -front.roll;
            rear.roll = -rear.roll;
        }
        fronts.push_back(front);
        rears.push_back(rear);
        before += vehicle.length();
    }
    return true;
}

void SimulationCore::sweep(int train, double from, double to) {
    Runtime &rt = runtime[size_t(train)];
    if (!rt.watched)
        return;
    const int part = states[size_t(train)].part;
    if (!rt.swept.empty() && rt.swept.back().part == part && std::fabs(rt.swept.back().to - from) < 1e-9)
        rt.swept.back().to = to;
    else
        rt.swept.push_back({part, from, to});
}

void SimulationCore::findSpots(int train, int part) {
    Runtime &rt = runtime[size_t(train)];
    rt.spots.erase(std::remove_if(rt.spots.begin(), rt.spots.end(),
                                  [part](const Runtime::Spot &spot) { return spot.part == part; }),
                   rt.spots.end());
    if (size_t(part) >= rt.parts.size())
        return;
    // The player's train looks at every location event (a triggering
    // train's too, until that train starts); a triggering train at its own.
    const bool player = train == playerTrain;
    for (size_t e = 0; e < in->events.size(); ++e) {
        const EventPlan &event = in->events[e];
        if (event.kind != EventPlan::Location || (!player && event.trigger != train))
            continue;
        for (const TrackPlace &place : event.places)
            for (const RouteInterval &interval : rt.parts[size_t(part)].intervals()) {
                const float low = std::min(interval.from, interval.to), high = std::max(interval.from, interval.to);
                if (interval.node == place.node && place.distance >= low - 1e-3f && place.distance <= high + 1e-3f)
                    rt.spots.push_back({int(e), part, interval.start + std::fabs(double(place.distance - interval.from))});
            }
    }
    std::sort(rt.spots.begin(), rt.spots.end(), [](const Runtime::Spot &a, const Runtime::Spot &b) {
        return a.event != b.event ? a.event < b.event : a.part != b.part ? a.part < b.part : a.at < b.at;
    });
}

bool SimulationCore::inSiding(const EventPlan &plan, int train) const {
    if (!plan.siding)
        return true;
    const TrainState &s = states[size_t(train)];
    const RouteTrack &part = runtime[size_t(train)].parts[size_t(s.part)];
    for (double at : {s.front, s.front - double(runtime[size_t(train)].length)}) {
        const RoutePoint point = part.locate(at);
        if (point.node == plan.sidingNode && point.distance >= plan.sidingFrom - 1e-3f
                && point.distance <= plan.sidingTo + 1e-3f)
            return true;
    }
    return false;
}

bool SimulationCore::inSiding(const EventPlan &plan, const Standing &consist) const {
    if (!plan.siding)
        return true;
    if (consist.track.empty())
        return false;
    const RouteInterval &first = consist.track.front(), &last = consist.track.back();
    const std::pair<int, float> ends[2] = {{first.node, first.from}, {last.node, last.to}};
    for (const auto &[node, distance] : ends)
        if (node == plan.sidingNode && distance >= plan.sidingFrom - 1e-3f && distance <= plan.sidingTo + 1e-3f)
            return true;
    return false;
}

bool SimulationCore::onTrack(int train) const {
    return train >= 0 && size_t(train) < states.size() && states[size_t(train)].status != TrainStatus::Waiting
            && states[size_t(train)].status != TrainStatus::Finished;
}

int SimulationCore::lookedAt(const EventPlan &plan, bool *standIn) const {
    // The triggering train once it has started (gone at its path's end it
    // fires nothing); before, the player's train (Open Rails).
    *standIn = false;
    if (plan.trigger >= 0 && size_t(plan.trigger) < states.size()) {
        if (states[size_t(plan.trigger)].status != TrainStatus::Waiting)
            return plan.trigger;
        *standIn = true;
    }
    return playerTrain;
}

bool SimulationCore::isNearEvent(int event, int train, bool standing) const {
    const EventPlan &plan = in->events[size_t(event)];
    const TrainState &s = states[size_t(train)];
    const Runtime &rt = runtime[size_t(train)];
    const double r = plan.radius;
    const auto first = std::lower_bound(rt.spots.begin(), rt.spots.end(), event,
                                        [](const Runtime::Spot &spot, int e) { return spot.event < e; });
    for (auto it = first; it != rt.spots.end() && it->event == event; ++it) {
        const Runtime::Spot &spot = *it;
        if (standing) {
            // Standing within the radius now, or brought to rest there in
            // this step.
            if (s.part == spot.part && std::fabs(s.speed) <= StandingSpeed && std::fabs(s.front - spot.at) < r)
                return true;
            for (const Runtime::Covered &rest : rt.rests)
                if (rest.part == spot.part && std::fabs(rest.to - spot.at) < r)
                    return true;
        } else {
            // The front within the radius at any moment of the step.
            if (s.part == spot.part && std::fabs(s.front - spot.at) < r)
                return true;
            for (const Runtime::Covered &c : rt.swept)
                if (c.part == spot.part && std::min(c.from, c.to) < spot.at + r && std::max(c.from, c.to) > spot.at - r)
                    return true;
        }
    }
    return false;
}

void SimulationCore::noteMisses() {
    if (ended != ActivityEnd::None)
        return;
    for (size_t e = 0; e < in->events.size(); ++e) {
        EventState &state = eventState[e];
        if (state.fired > 0)
            continue;
        const EventPlan &plan = in->events[e];
        const bool active = state.level > 0;
        if (active && state.activeSince < 0.0) {
            state.activeSince = now;
            ++eventChanges;
        }
        switch (plan.kind) {
        case EventPlan::Location: {
            // Passed: first while not active, or while active (stop-here
            // events passed without standing).
            if (state.passedAt >= 0.0 && (state.passedActive || !active))
                break;
            bool standIn = false;
            const int train = lookedAt(plan, &standIn);
            if (onTrack(train) && isNearEvent(int(e), train, false)) {
                state.passedAt = now;
                state.passedActive = active;
                ++eventChanges;
            }
            break;
        }
        case EventPlan::ReachSpeed:
            if (active && onTrack(playerTrain)) {
                const float speed = std::fabs(states[size_t(playerTrain)].speed);
                if (speed > state.bestSpeed) {
                    // Told by whole m/s: not every step.
                    if (int(speed) != int(state.bestSpeed))
                        ++eventChanges;
                    state.bestSpeed = speed;
                }
            }
            break;
        case EventPlan::AssembleTrainAtLocation:
            if (active && !state.reachedSiding && onTrack(playerTrain) && inSiding(plan, playerTrain)) {
                state.reachedSiding = true;
                ++eventChanges;
            }
            break;
        default:
            break;
        }
    }
}

bool SimulationCore::holds(int event, int *by, bool *standIn) const {
    const EventPlan &plan = in->events[size_t(event)];
    *by = -1;
    *standIn = false;
    const auto onTrack = [this](int train) { return this->onTrack(train); };
    // Exactly the listed cars, in order or reversed; all the listed cars,
    // others besides; the listed cars and no others, in any order.
    const auto exactly = [&plan](const std::vector<Vehicle> &cars) {
        if (cars.size() != plan.cars.size())
            return false;
        bool forward = true, backward = true;
        for (size_t i = 0; i < cars.size(); ++i) {
            forward = forward && cars[i].id == plan.cars[i];
            backward = backward && cars[cars.size() - 1 - i].id == plan.cars[i];
        }
        return forward || backward;
    };
    const auto includes = [&plan](const std::vector<Vehicle> &cars) {
        for (unsigned int car : plan.cars)
            if (std::none_of(cars.begin(), cars.end(), [car](const Vehicle &vehicle) { return vehicle.id == car; }))
                return false;
        return true;
    };
    const auto matches = [&](const std::vector<Vehicle> &cars) {
        return cars.size() == plan.cars.size() && includes(cars);
    };
    // Some train holds exactly the listed cars: one on the track (an AI
    // train's own cars cannot be listed, those it took up can), or a
    // standing train.
    const auto assembled = [&](int *train) {
        for (size_t t = 0; t < runtime.size(); ++t)
            if (onTrack(int(t)) && exactly(runtime[t].vehicles)) {
                *train = int(t);
                return true;
            }
        for (const Standing &consist : standing)
            if (!consist.gone && exactly(consist.vehicles))
                return true;
        return false;
    };
    switch (plan.kind) {
    case EventPlan::Time:
        // Whole seconds since the start, as Open Rails counts them.
        return std::floor(now + 1e-6) - in->startTime >= plan.time;
    case EventPlan::Location: {
        bool stand = false;
        const int train = lookedAt(plan, &stand);
        if (!onTrack(train) || !isNearEvent(event, train, plan.onStop))
            return false;
        *by = train;
        *standIn = stand;
        return true;
    }
    case EventPlan::AllStops: {
        // The player's last station stop done: it has left it (or passed
        // it); never for a train without stops.
        if (playerTrain < 0 || !onTrack(playerTrain) || runtime[size_t(playerTrain)].stops.empty())
            return false;
        const TrainState &s = states[size_t(playerTrain)];
        if (s.nextStop < int(runtime[size_t(playerTrain)].stops.size())
                || (s.status == TrainStatus::Stopped && s.stoppedAt == StopKind::Station))
            return false;
        *by = playerTrain;
        return true;
    }
    case EventPlan::ReachSpeed:
        if (!onTrack(playerTrain) || std::fabs(states[size_t(playerTrain)].speed) < plan.speed)
            return false;
        *by = playerTrain;
        return true;
    case EventPlan::AssembleTrain:
        return assembled(by);
    case EventPlan::AssembleTrainAtLocation:
        if (!onTrack(playerTrain) || !inSiding(plan, playerTrain))
            return false;
        *by = playerTrain;
        {
            int train = -1;
            return assembled(&train);
        }
    case EventPlan::DropOffWagonsAtLocation:
        // TSRE's rule (task 07, the user, 2026-10-21): a train that is the
        // listed cars, in any order (a loose consist, the player's or an AI
        // train: cars to set out from a longer train are cut off first; a
        // list of the whole train matches it), with an end in the siding
        // (Open Rails' end rule; Open Rails wants them a standing consist,
        // MSTS every listed car in the siding, coupled or not).
        for (size_t t = 0; t < runtime.size(); ++t)
            if (onTrack(int(t)) && matches(runtime[t].vehicles) && inSiding(plan, int(t))) {
                *by = int(t);
                return true;
            }
        for (const Standing &consist : standing)
            if (!consist.gone && matches(consist.vehicles) && inSiding(plan, consist))
                return true;
        return false;
    case EventPlan::PickUpWagons:
        if (!onTrack(playerTrain) || !includes(runtime[size_t(playerTrain)].vehicles)
                || std::fabs(states[size_t(playerTrain)].speed) > PickUpSpeed)
            return false;
        *by = playerTrain;
        return true;
    case EventPlan::PickUpPassengers:
    case EventPlan::Other:
        return false;
    }
    return false;
}

bool SimulationCore::actOn(int event) {
    const EventPlan &plan = in->events[size_t(event)];
    EventState &state = eventState[size_t(event)];
    // Not looked at again; a reversible event until its condition has
    // stopped holding.
    if (plan.reversible)
        state.disabled = true;
    else
        state.level = 0;
    // A reversible event's outcomes act the first time only.
    if (state.fired > 1)
        return false;
    for (int k : plan.activate)
        if (k >= 0)
            eventState[size_t(k)].level = 1;
    for (int k : plan.restore)
        if (k >= 0)
            eventState[size_t(k)].level = in->events[size_t(k)].activation;
    for (int k : plan.decrease)
        if (k >= 0)
            --eventState[size_t(k)].level;
    for (int k : plan.increase)
        if (k >= 0)
            ++eventState[size_t(k)].level;
    if (plan.failure || plan.success) {
        ended = plan.failure ? ActivityEnd::Failure : ActivityEnd::Success;
        endedAt = now;
        return true;
    }
    if (plan.restartTrain >= 0 && size_t(plan.restartTrain) < states.size())
        restart(plan.restartTrain, event);
    return false;
}

void SimulationCore::restart(int train, int event) {
    const EventPlan &plan = in->events[size_t(event)];
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    const double wanted = now + plan.restartDelay;
    // An "until" waiting point to the next whole minute.
    const double minute = std::ceil(wanted / 60.0 - 1e-9) * 60.0;
    // Standing at the waiting point of that value (as the path has it).
    if (s.status == TrainStatus::Stopped && s.stoppedAt == StopKind::Wait && s.nextWait >= 1
            && size_t(s.nextWait - 1) < in->trains[size_t(train)].waits.size()
            && in->trains[size_t(train)].waits[size_t(s.nextWait - 1)].value == plan.restartWait) {
        const bool until = WaitingPoint::decode(rt.waits[size_t(s.nextWait - 1)].value).kind == WaitingPoint::UntilTime;
        s.until = until ? minute : wanted;
        record(now, train, TrainEvent::Restarted, event);
        return;
    }
    // Still coming to it: its next stopping waiting point, if of that value.
    for (size_t w = size_t(std::max(0, s.nextWait)); w < rt.waits.size(); ++w) {
        if (!stops(rt.waits[w]))
            continue;
        if (rt.waits[w].value != plan.restartWait)
            break;
        WaitingPoint::Point point = WaitingPoint::decode(rt.waits[w].value);
        if (point.kind == WaitingPoint::UntilTime) {
            const long long minutes = std::llround(minute / 60.0);
            point.hour = int(minutes / 60 % 24);
            point.minute = int(minutes % 60);
        } else {
            point = WaitingPoint::Point();
            point.kind = WaitingPoint::Seconds;
            point.seconds = std::clamp(plan.restartDelay, 0, 29999);
        }
        rt.waits[w].value = WaitingPoint::encode(point);
        record(now, train, TrainEvent::Restarted, event);
        break;
    }
}

bool SimulationCore::wanted(int event) const {
    if (event < 0 || size_t(event) >= eventState.size() || ended != ActivityEnd::None)
        return false;
    const EventState &state = eventState[size_t(event)];
    return state.level > 0 && (state.fired < 1 || (in->events[size_t(event)].reversible && !state.disabled));
}

void SimulationCore::checkEvents() {
    if (in->events.empty() || ended != ActivityEnd::None || now < holdUntil - 1e-9)
        return;
    // Open Rails checks the list each frame and fires one event, then the
    // next frame (once its message is closed: a paused game) checks again
    // from the start: here all at this time, each event at most once.
    // Fired in this step (made when one fires: most steps fire none).
    std::vector<char> done;
    for (bool again = true; again;) {
        again = false;
        for (size_t e = 0; e < in->events.size(); ++e) {
            if (!done.empty() && done[e])
                continue;
            const EventPlan &plan = in->events[e];
            EventState &state = eventState[e];
            if (state.level <= 0 || (state.fired >= 1 && !plan.reversible))
                continue;
            int by = -1;
            bool standIn = false;
            if (!holds(int(e), &by, &standIn)) {
                if (plan.reversible && state.disabled) {
                    state.disabled = false;
                    ++eventChanges;
                }
                continue;
            }
            if (state.disabled)
                continue;
            ++state.fired;
            state.firedAt = now;
            state.firedBy = by;
            state.standIn = standIn;
            done.resize(in->events.size(), 0);
            done[e] = 1;
            ++eventChanges;
            record(now, by, TrainEvent::EventFired, int(e));
            events.back().signal = standIn ? 1 : -1;
            if (actOn(int(e))) {
                record(now, -1, TrainEvent::ActivityEnded, ended == ActivityEnd::Success ? 1 : 0);
                return;
            }
            // A message staying while the game runs holds the others.
            if (plan.message && plan.autoContinue > 0) {
                holdUntil = now + plan.autoContinue;
                return;
            }
            again = true;
            break;
        }
    }
}

void SimulationCore::freeSection(int section, int releasing) {
    const bool standingThere = size_t(section) < standingAt.size() && standingAt[size_t(section)] >= 0;
    holder[size_t(section)] = standingThere ? StandingConsist : NoHolder;
    // To the train the releasing one parted from where that one holds it
    // too (a section both stand in, task 07).
    const int partner = releasing >= 0 ? runtime[size_t(releasing)].partedFrom : -1;
    if (partner >= 0 && partner != releasing && states[size_t(partner)].status != TrainStatus::Finished
            && holds(partner, section))
        holder[size_t(section)] = partner;
    heldStamp[size_t(section)] = ++holdChanges;
}

void SimulationCore::holdStanding(int standingTrain) {
    ++standingChanges;
    const TrackNetwork *network = in->network.get();
    if (network == nullptr)
        return;
    std::vector<int> &marked = standing[size_t(standingTrain)].sections;
    marked.clear();
    const auto mark = [&](int section) {
        if (std::find(marked.begin(), marked.end(), section) == marked.end())
            marked.push_back(section);
        standingAt[size_t(section)] = standingTrain;
        if (holder[size_t(section)] == NoHolder)
            holder[size_t(section)] = StandingConsist;
        heldStamp[size_t(section)] = ++holdChanges;
    };
    const RouteInterval *previous = nullptr;
    for (const RouteInterval &interval : standing[size_t(standingTrain)].track) {
        if (previous != nullptr && previous->node != interval.node) {
            const int junction = network->endSection(previous->node, previous->forward() ? 1 : 0);
            if (junction >= 0)
                mark(junction);
        }
        const float low = std::min(interval.from, interval.to), high = std::max(interval.from, interval.to);
        const auto [first, count] = network->nodeSections(interval.node);
        for (int k = first; k < first + count; ++k) {
            const TrackSection &section = network->sections()[size_t(k)];
            if (section.to > low + 1e-3f && section.from < high - 1e-3f)
                mark(k);
        }
        previous = &interval;
    }
}

bool SimulationCore::attaches(int train, int standingTrain, double gap) {
    Standing &other = standing[size_t(standingTrain)];
    if (other.gone)
        return false;
    // Not to cars it cut off until it has been away from them (Open Rails'
    // UncoupledFrom).
    if (other.uncoupledFrom == train) {
        if (gap <= UncoupledApart)
            return false;
        other.uncoupledFrom = -1;
    }
    return true;
}

double SimulationCore::contactOn(int train, int standingTrain) const {
    const Standing &other = standing[size_t(standingTrain)];
    const TrainState &s = states[size_t(train)];
    if (other.track.empty() || size_t(s.part) >= runtime[size_t(train)].parts.size())
        return -1.0;
    const RouteTrack &part = runtime[size_t(train)].parts[size_t(s.part)];
    double contact = -1.0;
    for (const auto &[node, distance] : {std::make_pair(other.track.front().node, other.track.front().from),
                                         std::make_pair(other.track.back().node, other.track.back().to)}) {
        double at = -1.0;
        if (part.find(node, distance, at) && at >= s.front - UncoupledApart && (contact < 0.0 || at < contact))
            contact = at;
    }
    return contact;
}

std::vector<int> SimulationCore::standingIn(int section) const {
    std::vector<int> out;
    if (size_t(section) >= standingAt.size() || standingAt[size_t(section)] < 0)
        return out;
    for (size_t k = 0; k < standing.size(); ++k) {
        const Standing &st = standing[k];
        if (!st.gone && std::find(st.sections.begin(), st.sections.end(), section) != st.sections.end())
            out.push_back(int(k));
    }
    return out;
}

int SimulationCore::standingAhead(int train, int section, double *contact) const {
    const double front = states[size_t(train)].front;
    int nearest = -1;
    double at = -1.0;
    for (int other : standingIn(section)) {
        const double here = contactOn(train, other);
        if (here >= front - AtTarget && (nearest < 0 || here < at)) {
            nearest = other;
            at = here;
        }
    }
    if (contact != nullptr)
        *contact = at;
    return nearest;
}

bool SimulationCore::onPart(int train, int standingTrain, double from) const {
    const Standing &other = standing[size_t(standingTrain)];
    const TrainState &s = states[size_t(train)];
    if (size_t(s.part) >= runtime[size_t(train)].parts.size())
        return true;
    const RouteTrack &part = runtime[size_t(train)].parts[size_t(s.part)];
    for (const RouteInterval &mine : part.intervals()) {
        const double ahead = mine.start + mine.length() - from - AtTarget;
        if (ahead <= 0.0)
            continue;
        // The interval's part beyond `from`.
        const float sign = mine.forward() ? 1.0f : -1.0f;
        const float start = mine.to - sign * float(std::min(ahead, double(mine.length())));
        const float lo = std::min(start, mine.to), hi = std::max(start, mine.to);
        for (const RouteInterval &cars : other.track) {
            if (cars.node != mine.node)
                continue;
            const float a = std::min(cars.from, cars.to), b = std::max(cars.from, cars.to);
            if (std::min(hi, b) - std::max(lo, a) > 0.01f)
                return true;
        }
    }
    return false;
}

bool SimulationCore::behind(int train, int standingTrain, double rear) const {
    const Standing &other = standing[size_t(standingTrain)];
    const TrainState &s = states[size_t(train)];
    if (other.track.empty() || size_t(s.part) >= runtime[size_t(train)].parts.size())
        return false;
    const RouteTrack &part = runtime[size_t(train)].parts[size_t(s.part)];
    for (const auto &[node, distance] : {std::make_pair(other.track.front().node, other.track.front().from),
                                         std::make_pair(other.track.back().node, other.track.back().to)}) {
        double at = -1.0;
        if (!part.find(node, distance, at) || at > rear + AtTarget)
            return false;
    }
    return true;
}

namespace {

// Vehicles in the other order: each turned (its flip changed).
std::vector<Vehicle> turned(const std::vector<Vehicle> &vehicles) {
    std::vector<Vehicle> out(vehicles.rbegin(), vehicles.rend());
    for (Vehicle &vehicle : out)
        vehicle.flip = !vehicle.flip;
    return out;
}

}

void SimulationCore::takeFormingVehicles(int train) {
    const TrainPlan &plan = in->trains[size_t(train)];
    if (plan.formedBy < 0 || size_t(plan.formedBy) >= runtime.size())
        return;
    const Runtime &from = runtime[size_t(plan.formedBy)];
    const TrainState &old = states[size_t(plan.formedBy)];
    Runtime &rt = runtime[size_t(train)];
    if (from.vehicles.empty() || rt.parts.empty() || size_t(old.part) >= from.parts.size())
        return;
    // Front first as that train stood; turned where this one's front is at
    // that one's rear (Open Rails turns the formed train's consist).
    std::vector<Vehicle> vehicles = old.reversed ? turned(from.vehicles) : from.vehicles;
    const RoutePoint front = rt.parts.front().locate(states[size_t(train)].front);
    const RoutePoint oldFront = from.parts[size_t(old.part)].locate(old.front);
    const RoutePoint oldRear = from.parts[size_t(old.part)].locate(old.front - from.length);
    const auto apart = [](const RoutePoint &a, const RoutePoint &b) {
        return a.node == b.node ? std::fabs(double(a.distance) - double(b.distance)) : 1e12;
    };
    if (apart(front, oldRear) < apart(front, oldFront))
        vehicles = turned(vehicles);
    rt.vehicles = vehicles;
    refreshConsist(train);
}

namespace {

// Whether a track runs along a route part from route distance `at` for
// `length` metres (each 2 m within 1.5 m of where the part has it).
bool runsAlong(const RouteTrack &part, double at, const std::vector<RouteInterval> &track, double length) {
    double walked = 0.0;
    for (const RouteInterval &interval : track) {
        const double span = std::fabs(double(interval.to - interval.from));
        const float sign = interval.forward() ? 1.0f : -1.0f;
        for (double k = 0.0; k < span && walked + k <= length; k += 2.0) {
            double here = -1.0;
            if (!part.find(interval.node, interval.from + sign * float(k), here) || std::fabs(here - (at + walked + k)) > 1.5)
                return false;
        }
        walked += span;
        if (walked > length)
            break;
    }
    return true;
}

// Track intervals the other way.
std::vector<RouteInterval> backwards(const std::vector<RouteInterval> &track) {
    std::vector<RouteInterval> out;
    for (auto it = track.rbegin(); it != track.rend(); ++it)
        out.push_back(RouteInterval{it->node, it->to, it->from, 0.0});
    return out;
}

}

std::vector<RouteInterval> SimulationCore::slice(const RouteTrack &part, double from, double to) {
    std::vector<RouteInterval> out;
    for (const RouteInterval &interval : part.intervals()) {
        const double a = std::max(from, interval.start), b = std::min(to, interval.start + interval.length());
        if (b - a < 1e-3)
            continue;
        const float sign = interval.forward() ? 1.0f : -1.0f;
        out.push_back(RouteInterval{interval.node, interval.from + sign * float(a - interval.start),
                                    interval.from + sign * float(b - interval.start), a});
    }
    return out;
}

void SimulationCore::reprojectPart(int train, int part) {
    Runtime &rt = runtime[size_t(train)];
    if (in->network == nullptr || size_t(part) >= rt.ways.size())
        return;
    rt.ways[size_t(part)] = in->network->project(rt.parts[size_t(part)]);
    if (rt.limits.size() <= size_t(part))
        rt.limits.resize(size_t(part) + 1);
    rt.limits[size_t(part)] = SpeedLimits(rt.ways[size_t(part)], *in->network, rt.passenger, {}, 0.0f,
                                          rt.parts[size_t(part)].length());
    rt.limit = 0;
    refreshStretches(train);
    findExits(train);
    if (rt.watched)
        findSpots(train, part);
}

void SimulationCore::couple(int train, double time) {
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    if (rt.attachTrain >= 0) {
        coupleTrain(train, time);
        return;
    }
    const int other = rt.attachTo;
    rt.attachTo = -1;
    rt.uncondAttach = false;
    if (other < 0 || standing[size_t(other)].gone)
        return;
    Standing &st = standing[size_t(other)];
    RouteTrack &part = rt.parts[size_t(s.part)];
    // Which of its ends the train met: its first vehicle's (the track's
    // start) or its last's.
    double first = -1.0, last = -1.0;
    part.find(st.track.front().node, st.track.front().from, first);
    part.find(st.track.back().node, st.track.back().to, last);
    const bool atFirst = last < 0.0 || (first >= 0.0 && std::fabs(first - s.front) <= std::fabs(last - s.front));
    // The train's vehicles from its front backwards.
    const std::vector<Vehicle> fromFront = s.reversed ? turned(rt.vehicles) : rt.vehicles;
    const double rear = s.front - rt.length;
    // An AI train at its path's end joins the standing train (Open Rails:
    // with fewer than five sections of its path left); the player's train
    // takes it.
    size_t left = 0;
    if (size_t(s.part) < rt.ways.size())
        for (const ProjectedSection &section : rt.ways[size_t(s.part)].sections)
            left += section.enter > s.front + 1e-3 ? 1 : 0;
    if (train != playerTrain && s.part + 1 >= int(rt.parts.size()) && left < JoinSections) {
        const std::vector<RouteInterval> under = slice(part, std::max(0.0, rear), s.front);
        if (atFirst) {
            std::vector<Vehicle> vehicles = turned(fromFront);
            vehicles.insert(vehicles.end(), st.vehicles.begin(), st.vehicles.end());
            st.vehicles = vehicles;
            std::vector<RouteInterval> track = under;
            track.insert(track.end(), st.track.begin(), st.track.end());
            st.track = track;
        } else {
            st.vehicles.insert(st.vehicles.end(), fromFront.begin(), fromFront.end());
            const std::vector<RouteInterval> back = backwards(under);
            st.track.insert(st.track.end(), back.begin(), back.end());
        }
        st.length += rt.length;
        releaseAll(train);
        holdStanding(other);
        s.status = TrainStatus::Finished;
        s.speed = 0.0f;
        record(time, train, TrainEvent::Joined, other);
        return;
    }
    // The train takes the standing train's cars: they join at its front.
    const std::vector<Vehicle> outward = atFirst ? st.vehicles : turned(st.vehicles);
    std::vector<Vehicle> vehicles;
    if (s.reversed) {
        vehicles = rt.vehicles;
        vehicles.insert(vehicles.end(), outward.begin(), outward.end());
    } else {
        vehicles = turned(outward);
        vehicles.insert(vehicles.end(), rt.vehicles.begin(), rt.vehicles.end());
    }
    st.gone = true;
    ++standingChanges;
    // Its sections: another standing train's, or free.
    for (int k : st.sections) {
        const std::vector<int> left = standingIn(k);
        standingAt[size_t(k)] = left.empty() ? -1 : left.front();
        if (left.empty() && holder[size_t(k)] == StandingConsist)
            holder[size_t(k)] = NoHolder;
        heldStamp[size_t(k)] = ++holdChanges;
    }
    st.sections.clear();
    const double contact = s.front;
    // The cars taken off the rest of its part (standing across a junction
    // onto another way): it cannot go on with them. Open Rails removes
    // such a train (out of control, OUT_OF_PATH); the player's train stays.
    const std::vector<RouteInterval> away = atFirst ? st.track : backwards(st.track);
    const double along = std::min(double(st.length), part.length() - contact) - 1.0;
    const bool offPath = along > 0.0 && !runsAlong(part, contact, away, along);
    rt.vehicles = vehicles;
    refreshConsist(train);
    s.front = contact + st.length;
    // Its route part goes on over the cars taken where it ended among them
    // (a reversal or its path's end there).
    extendPart(train, away, contact);
    releaseAll(train);
    if (offPath) {
        record(time, train, TrainEvent::Coupled, other);
        events.back().signal = int(st.vehicles.size());
        s.front = contact;
        s.speed = 0.0f;
        if (train == playerTrain) {
            s.status = TrainStatus::Stopped;
            s.stoppedAt = StopKind::End;
            occupy(train);
        } else {
            s.status = TrainStatus::Finished;
        }
        record(time, train, TrainEvent::Removed, other);
        return;
    }
    if (!occupy(train))
        blocked(train, time);
    record(time, train, TrainEvent::Coupled, other);
    events.back().signal = int(st.vehicles.size());
}

bool SimulationCore::cut(int train, int keep, bool keepFront, double time) {
    const TrainPlan &plan = in->trains[size_t(train)];
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    const size_t count = rt.vehicles.size();
    keep = std::min(keep, int(count) - 1);
    if (keep <= 0)
        return false;
    // By place from the front in the direction of travel.
    std::vector<Vehicle> kept, off;
    bool engine = false;
    for (size_t c = 0; c < count; ++c) {
        const size_t fromFront = s.reversed ? count - 1 - c : c;
        const bool keepIt = keepFront ? fromFront < size_t(keep) : fromFront >= count - size_t(keep);
        (keepIt ? kept : off).push_back(rt.vehicles[c]);
        engine = engine || (keepIt && rt.vehicles[c].type() >= 4);
    }
    // Open Rails: no cut leaving the moving part without an engine.
    if (!engine)
        return false;
    double offLength = 0.0;
    for (const Vehicle &vehicle : off)
        offLength += vehicle.length();
    const double rear = s.front - rt.length;
    const double from = keepFront ? rear : s.front - offLength;
    const double to = keepFront ? rear + offLength : s.front;
    Standing made;
    made.name = plan.service;
    // In the order of the route (from its rear to its front).
    made.vehicles = s.reversed ? off : turned(off);
    made.length = offLength;
    made.track = slice(rt.parts[size_t(s.part)], std::max(0.0, from), to);
    made.uncoupledFrom = train;
    rt.vehicles = kept;
    refreshConsist(train);
    if (!keepFront)
        s.front -= offLength;
    releaseAll(train);
    // The train it carries since 60001 runs again with these cars (Open
    // Rails' join and split), where its path finds them.
    const int carried = rt.carries;
    rt.carries = -1;
    if (carried >= 0 && revive(carried, train, made.track, made.vehicles, time)) {
        if (!occupy(train))
            blocked(train, time);
        record(time, train, TrainEvent::LeftCars, carried);
        events.back().signal = int(off.size());
        return true;
    }
    standing.push_back(made);
    const int index = int(standing.size()) - 1;
    holdStanding(index);
    if (!occupy(train))
        blocked(train, time);
    record(time, train, TrainEvent::Uncoupled, index);
    events.back().signal = int(off.size());
    return true;
}

std::vector<RouteInterval> SimulationCore::trackOf(int train) const {
    const TrainState &s = states[size_t(train)];
    const Runtime &rt = runtime[size_t(train)];
    if (size_t(s.part) >= rt.parts.size())
        return {};
    return slice(rt.parts[size_t(s.part)], std::max(0.0, s.front - rt.length), s.front);
}

bool SimulationCore::holds(int train, int section) const {
    for (const Held &held : runtime[size_t(train)].held)
        if (held.section == section)
            return true;
    return false;
}

bool SimulationCore::endIn(int other, int section) const {
    const TrainState &o = states[size_t(other)];
    const Runtime &ort = runtime[size_t(other)];
    const double rear = o.front - ort.length;
    for (const Held &held : ort.held)
        if (held.section == section
                && ((held.enter <= o.front + 1e-3 && held.leave >= o.front - 1e-3)
                    || (held.enter <= rear + 1e-3 && held.leave >= rear - 1e-3)))
            return true;
    return false;
}

double SimulationCore::contactWith(int train, int other) const {
    const TrainState &s = states[size_t(train)];
    const std::vector<RouteInterval> track = trackOf(other);
    if (track.empty() || size_t(s.part) >= runtime[size_t(train)].parts.size())
        return -1.0;
    const RouteTrack &part = runtime[size_t(train)].parts[size_t(s.part)];
    double contact = -1.0;
    for (const auto &[node, distance] : {std::make_pair(track.front().node, track.front().from),
                                         std::make_pair(track.back().node, track.back().to)}) {
        double at = -1.0;
        if (part.find(node, distance, at) && at >= s.front - UncoupledApart && (contact < 0.0 || at < contact))
            contact = at;
    }
    return contact;
}

bool SimulationCore::attachesTrain(int train, int other, double gap) {
    const TrainState &o = states[size_t(other)];
    if (o.status != TrainStatus::Stopped || std::fabs(o.speed) >= AtRestSpeed || gap > AttachReach)
        return false;
    const Runtime &rt = runtime[size_t(train)];
    // Not to the train it just parted from until it has been away from it.
    if (rt.partedFrom == other && gap <= UncoupledApart)
        return false;
    if (rt.uncondAttach)
        return true;
    // Open Rails: the other train's front or rear in the last section of
    // this train's path part (it ends there: a reversal or its path's end).
    const TrainState &s = states[size_t(train)];
    if (size_t(s.part) >= rt.ways.size() || rt.ways[size_t(s.part)].sections.empty())
        return false;
    return endIn(other, rt.ways[size_t(s.part)].sections.back().section);
}

std::vector<Vehicle> SimulationCore::fromFront(int train) const {
    const Runtime &rt = runtime[size_t(train)];
    return states[size_t(train)].reversed ? turned(rt.vehicles) : rt.vehicles;
}

void SimulationCore::setFromFront(int train, std::vector<Vehicle> vehicles) {
    runtime[size_t(train)].vehicles = states[size_t(train)].reversed ? turned(vehicles) : std::move(vehicles);
    refreshConsist(train);
}

void SimulationCore::extendPart(int train, const std::vector<RouteInterval> &away, double contact) {
    TrainState &s = states[size_t(train)];
    RouteTrack &part = runtime[size_t(train)].parts[size_t(s.part)];
    if (s.front > part.length() + 1e-3) {
        double skip = part.length() - contact;
        for (const RouteInterval &interval : away) {
            const double length = std::fabs(double(interval.to - interval.from));
            if (skip >= length - 1e-3) {
                skip -= length;
                continue;
            }
            const float sign = interval.forward() ? 1.0f : -1.0f;
            part.append(interval.node, interval.from + sign * float(std::max(0.0, skip)), interval.to);
            skip = 0.0;
        }
        reprojectPart(train, s.part);
    }
    s.front = std::min(s.front, part.length());
}

void SimulationCore::coupleTrain(int train, double time) {
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    const int other = rt.attachTrain;
    rt.attachTrain = -1;
    const bool uncond = rt.uncondAttach;
    rt.uncondAttach = false;
    if (other < 0 || states[size_t(other)].status != TrainStatus::Stopped)
        return;
    TrainState &os = states[size_t(other)];
    Runtime &ort = runtime[size_t(other)];
    const std::vector<RouteInterval> otherTrack = trackOf(other);
    if (otherTrack.empty())
        return;
    RouteTrack &part = rt.parts[size_t(s.part)];
    // Which of the other's ends it met: its front or its rear.
    double atRear = -1.0, atFront = -1.0;
    part.find(otherTrack.front().node, otherTrack.front().from, atRear);
    part.find(otherTrack.back().node, otherTrack.back().to, atFront);
    const bool metFront = atFront >= 0.0 && (atRear < 0.0 || std::fabs(atFront - s.front) <= std::fabs(atRear - s.front));
    // Both trains' vehicles from where they met: this one's from its front
    // backwards, the other's from the end met into it; the other's track
    // from the contact on, this train's way.
    const std::vector<Vehicle> mine = fromFront(train);
    const std::vector<Vehicle> theirs = metFront ? fromFront(other) : turned(fromFront(other));
    const std::vector<RouteInterval> beyond = metFront ? backwards(otherTrack) : otherTrack;
    const auto engine = [](const Vehicle &vehicle) { return vehicle.type() >= 4; };
    const double contact = s.front;
    const double otherFront = os.front;
    // The other train's vehicles from the end met, and its front where it
    // grew there (its part going on over this train's track, `under` this
    // train's way up to the contact).
    const auto give = [&](const std::vector<Vehicle> &cars, double length) {
        std::vector<Vehicle> grown = turned(cars);
        grown.insert(grown.end(), theirs.begin(), theirs.end());
        setFromFront(other, metFront ? grown : turned(grown));
        if (metFront) {
            os.front = otherFront + length;
            extendPart(other, backwards(slice(part, std::max(0.0, contact - length), contact)), otherFront);
        }
    };
    const bool lastPart = s.part + 1 >= int(rt.parts.size());
    if (!lastPart && !uncond) {
        // Open Rails: its coupling end an engine, it takes the other's cars
        // up to that train's engine; else it leaves the other its own cars
        // up to its engine; one car stays at least. Both go on.
        size_t count = 0;
        double length = 0.0;
        if (engine(mine.front())) {
            while (count + 1 < theirs.size() && !engine(theirs[count]))
                length += theirs[count++].length();
            if (count > 0) {
                std::vector<Vehicle> grown = turned(std::vector<Vehicle>(theirs.begin(), theirs.begin() + long(count)));
                grown.insert(grown.end(), mine.begin(), mine.end());
                setFromFront(train, grown);
                const std::vector<Vehicle> rest(theirs.begin() + long(count), theirs.end());
                setFromFront(other, metFront ? rest : turned(rest));
                if (metFront)
                    os.front = otherFront - length;
                s.front = contact + length;
                extendPart(train, beyond, contact);
            }
        } else {
            while (count + 1 < mine.size() && !engine(mine[count]))
                length += mine[count++].length();
            if (count > 0) {
                give(std::vector<Vehicle>(mine.begin(), mine.begin() + long(count)), length);
                setFromFront(train, std::vector<Vehicle>(mine.begin() + long(count), mine.end()));
                s.front = contact - length;
            }
        }
        rt.partedFrom = other;
        ort.partedFrom = train;
        releaseAll(train);
        releaseAll(other);
        if (!occupy(other))
            blocked(other, time);
        if (!occupy(train))
            blocked(train, time);
        if (count > 0) {
            const bool took = engine(mine.front());
            record(time, train, took ? TrainEvent::TookCars : TrainEvent::LeftCars, other);
            events.back().signal = int(count);
            record(time, other, took ? TrainEvent::LeftCars : TrainEvent::TookCars, train);
            events.back().signal = int(count);
        }
        return;
    }
    // At its path's last part, or after 60001: it joins the other train,
    // its cars added at the end it met, and is gone (carried along after
    // 60001, to run again with the cars the other next cuts off). The
    // player's train: the one it joined is the player's now (Open Rails).
    give(mine, rt.length);
    releaseAll(train);
    releaseAll(other);
    if (!occupy(other))
        blocked(other, time);
    s.status = TrainStatus::Finished;
    s.speed = 0.0f;
    s.joinedTo = other;
    if (uncond && ort.carries < 0)
        ort.carries = train;
    if (train == playerTrain) {
        playerTrain = other;
        if (!ort.watched) {
            ort.watched = true;
            findSpots(other, os.part);
        }
    }
    record(time, train, TrainEvent::JoinedTrain, other);
    record(time, other, TrainEvent::TookCars, train);
    events.back().signal = int(mine.size());
}

bool SimulationCore::revive(int train, int from, const std::vector<RouteInterval> &track,
                            const std::vector<Vehicle> &vehicles, double time) {
    if (track.empty())
        return false;
    TrainState &s = states[size_t(train)];
    Runtime &rt = runtime[size_t(train)];
    double length = 0.0;
    for (const Vehicle &vehicle : vehicles)
        length += vehicle.length();
    // Its first part from where it joined on with the cars on it, end to
    // end.
    for (size_t p = size_t(std::max(0, s.part)); p < rt.parts.size(); ++p) {
        double first = -1.0, last = -1.0;
        if (!rt.parts[p].find(track.front().node, track.front().from, first)
                || !rt.parts[p].find(track.back().node, track.back().to, last)
                || std::fabs(std::fabs(last - first) - length) > 1.0)
            continue;
        // Its way along `from`'s: they lead with the last of them.
        const bool along = last > first;
        s.part = int(p);
        s.front = std::max(first, last);
        s.reversed = false;
        rt.vehicles = along ? turned(vehicles) : vehicles;
        refreshConsist(train);
        s.status = TrainStatus::Running;
        s.stoppedAt = StopKind::None;
        s.speed = 0.0f;
        s.joinedTo = -1;
        rt.limit = 0;
        s.nextStop = 0;
        while (s.nextStop < int(rt.stops.size())
               && (rt.stops[size_t(s.nextStop)].part < int(p)
                   || (rt.stops[size_t(s.nextStop)].part == int(p) && rt.stops[size_t(s.nextStop)].front < s.front - Passed)))
            ++s.nextStop;
        s.nextWait = 0;
        while (s.nextWait < int(rt.waits.size())
               && (rt.waits[size_t(s.nextWait)].part < int(p)
                   || (rt.waits[size_t(s.nextWait)].part == int(p) && rt.waits[size_t(s.nextWait)].front < s.front - Passed)))
            ++s.nextWait;
        rt.partedFrom = from;
        runtime[size_t(from)].partedFrom = train;
        // The player's train before it joined: the player's again.
        if (in->trains[size_t(train)].player && playerTrain == from)
            playerTrain = train;
        if (rt.watched)
            findSpots(train, s.part);
        record(time, train, TrainEvent::Parted, from);
        if (!occupy(train)) {
            s.status = TrainStatus::Stopped;
            s.stoppedAt = StopKind::Blocked;
            blocked(train, time);
        }
        return true;
    }
    return false;
}

}
