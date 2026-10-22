#include <tsre/sim/SimulationHost.h>

#include <QThread>
#include <QTimer>
#include <algorithm>

namespace Sim {

std::shared_ptr<SimulationFrame> MakeFrame(const SimulationCore &core, const SimulationFrame *before) {
    auto frame = std::make_shared<SimulationFrame>();
    frame->runId = core.input().runId;
    frame->steps = core.steps();
    frame->time = core.time();
    frame->finished = core.finished();
    const std::vector<TrainState> &states = core.trains();
    if (before != nullptr && before->runId != frame->runId)
        before = nullptr;
    frame->trains.resize(states.size());
    for (size_t i = 0; i < states.size(); ++i) {
        TrainFrame &train = frame->trains[i];
        train.state = states[i];
        core.vehicleEnds(int(i), train.fronts, train.rears);
        train.carsVersion = core.carsVersion(int(i));
        if (before != nullptr && i < before->trains.size() && before->trains[i].vehicles != nullptr
                && before->trains[i].carsVersion == train.carsVersion)
            train.vehicles = before->trains[i].vehicles;
        else
            train.vehicles = std::make_shared<const std::vector<Vehicle>>(core.vehiclesOf(int(i)));
    }
    frame->standingSerial = core.standingSerial();
    if (before != nullptr && before->standing != nullptr && before->standingSerial == frame->standingSerial) {
        frame->standing = before->standing;
    } else {
        auto standing = std::make_shared<std::vector<StandingFrame>>();
        for (size_t k = 0; k < core.standingTrains().size(); ++k) {
            const SimulationCore::Standing &st = core.standingTrains()[k];
            StandingFrame made;
            made.name = st.name;
            made.vehicles = st.vehicles;
            made.gone = st.gone;
            made.loose = st.object >= 0;
            core.standingEnds(int(k), made.fronts, made.rears);
            standing->push_back(std::move(made));
        }
        frame->standing = std::move(standing);
    }
    frame->drawSerial = core.drawSerial();
    if (before != nullptr && before->drawStates != nullptr && before->drawSerial == frame->drawSerial) {
        frame->aspects = before->aspects;
        frame->drawStates = before->drawStates;
    } else {
        frame->aspects = std::make_shared<const std::vector<int>>(core.aspects());
        frame->drawStates = std::make_shared<const std::vector<int>>(core.drawStates());
    }
    frame->holdSerial = core.holdSerial();
    frame->standsOn.resize(states.size());
    for (size_t i = 0; i < states.size(); ++i)
        frame->standsOn[i] = core.standsOn(int(i));
    if (before != nullptr && before->sectionUses != nullptr && before->holdSerial == frame->holdSerial
            && before->standsOn == frame->standsOn)
        frame->sectionUses = before->sectionUses;
    else
        frame->sectionUses = std::make_shared<const std::vector<SimulationCore::SectionUse>>(core.sectionUses());
    frame->eventSerial = core.eventSerial();
    frame->activityEnd = core.activityEnd();
    frame->activityEndedAt = core.activityEndedAt();
    if (before != nullptr && before->eventStates != nullptr && before->eventSerial == frame->eventSerial)
        frame->eventStates = before->eventStates;
    else
        frame->eventStates = std::make_shared<const std::vector<EventState>>(core.eventStates());
    return frame;
}

SimulationHost::SimulationHost() {
    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, &SimulationHost::tick);
    latest = std::make_shared<const SimulationFrame>();
}

SimulationHost::~SimulationHost() = default;

std::shared_ptr<const SimulationFrame> SimulationHost::frame() const {
    std::lock_guard<std::mutex> lock(mailbox);
    return latest;
}

std::vector<TrainEvent> SimulationHost::takeEvents(size_t *dropped) {
    std::lock_guard<std::mutex> lock(mailbox);
    std::vector<TrainEvent> taken(events.begin(), events.end());
    events.clear();
    if (dropped != nullptr)
        *dropped = droppedEvents;
    droppedEvents = 0;
    return taken;
}

std::shared_ptr<const SimulationFrame> SimulationHost::take(std::vector<TrainEvent> &taken, size_t *dropped) {
    std::lock_guard<std::mutex> lock(mailbox);
    taken.assign(events.begin(), events.end());
    events.clear();
    if (dropped != nullptr)
        *dropped = droppedEvents;
    droppedEvents = 0;
    return latest;
}

void SimulationHost::start(std::shared_ptr<const SimulationInput> input, unsigned long long seq) {
    core = input != nullptr ? std::make_unique<SimulationCore>(std::move(input)) : nullptr;
    mode = RunMode::Paused;
    acknowledged = seq;
    {
        // The new run's events only.
        std::lock_guard<std::mutex> lock(mailbox);
        events.clear();
        droppedEvents = 0;
    }
    if (core != nullptr)
        core->takeEvents();
    rate = 0.0;
    schedule();
    publish();
}

void SimulationHost::play(unsigned long long seq) {
    if (core != nullptr)
        mode = RunMode::Running;
    acknowledged = seq;
    schedule();
    publish();
}

void SimulationHost::pause(unsigned long long seq) {
    mode = RunMode::Paused;
    acknowledged = seq;
    schedule();
    publish();
}

void SimulationHost::setSpeed(double value, unsigned long long seq) {
    speed = std::max(0.0, value);
    acknowledged = seq;
    schedule();
    publish();
}

bool SimulationHost::trainPlaced(int train) const {
    return core != nullptr && train >= 0 && size_t(train) < core->trains().size()
            && core->trains()[size_t(train)].status != TrainStatus::Waiting;
}

void SimulationHost::runTo(double time, unsigned long long seq, bool thenRun, int untilOnTrack) {
    acknowledged = seq;
    runOn = thenRun;
    untilTrain = untilOnTrack;
    if (core != nullptr && time > core->time() && !(untilTrain >= 0 && trainPlaced(untilTrain))) {
        // The simulation steps whole steps: to the first at or after it.
        target = core->onStep(time);
        mode = RunMode::RunningTo;
    } else {
        mode = core != nullptr && thenRun ? RunMode::Running : RunMode::Paused;
    }
    schedule();
    publish();
}

void SimulationHost::stop(unsigned long long seq, QThread *owner) {
    timer->stop();
    core.reset();
    mode = RunMode::Paused;
    acknowledged = seq;
    publish();
    if (owner != nullptr)
        moveToThread(owner);
    QThread::currentThread()->quit();
}

void SimulationHost::schedule() {
    const bool runs = core != nullptr && mode != RunMode::Paused;
    if (!runs) {
        timer->stop();
        return;
    }
    // A pace ticks; as fast as possible ticks again as soon as the queued
    // commands are taken.
    const bool fastest = mode == RunMode::RunningTo || speed <= 0.0;
    const int interval = fastest ? 0 : TickMs;
    if (!timer->isActive() || timer->interval() != interval) {
        timer->start(interval);
        wall.start();
        rateWall.start();
        rateSimulated = 0.0;
    }
}

void SimulationHost::stepFor(double seconds, const QElapsedTimer &batch) {
    while (seconds > 1e-9 && batch.elapsed() < BatchMs) {
        const double h = std::min(SimulationCore::MaxStep, seconds);
        core->step(h);
        seconds -= h;
        if (mode == RunMode::RunningTo && untilTrain >= 0 && trainPlaced(untilTrain))
            break;
    }
}

void SimulationHost::tick() {
    if (core == nullptr)
        return;
    const double elapsed = std::min(wall.nsecsElapsed() * 1e-9, LongestTick);
    wall.start();
    QElapsedTimer batch;
    batch.start();
    const double before = core->time();
    if (mode == RunMode::Running) {
        // At a pace: the wall time since the last tick × the speed, in steps
        // of at most 0.1 s; what a batch does not manage is not made up.
        stepFor(speed > 0.0 ? elapsed * speed : 1e12, batch);
    } else if (mode == RunMode::RunningTo) {
        stepFor(target - core->time(), batch);
        if (core->time() >= target - 1e-6 || (untilTrain >= 0 && trainPlaced(untilTrain))) {
            mode = runOn ? RunMode::Running : RunMode::Paused;
            untilTrain = -1;
            schedule();
        }
    }
    rateSimulated += core->time() - before;
    const qint64 rateNs = rateWall.nsecsElapsed();
    if (rateNs >= 500000000LL) {
        rate = rateSimulated / (rateNs * 1e-9);
        rateSimulated = 0.0;
        rateWall.start();
    }
    publish();
}

void SimulationHost::publish() {
    // Only this thread sets the latest frame: read without the lock.
    std::shared_ptr<SimulationFrame> frame = core != nullptr ? MakeFrame(*core, latest.get())
                                                             : std::make_shared<SimulationFrame>();
    frame->acknowledged = acknowledged;
    frame->mode = mode;
    frame->speed = speed;
    frame->rate = mode == RunMode::Paused ? 0.0 : rate;
    frame->target = target;
    std::vector<TrainEvent> fresh;
    if (core != nullptr)
        fresh = core->takeEvents();
    std::lock_guard<std::mutex> lock(mailbox);
    latest = std::move(frame);
    for (const TrainEvent &event : fresh)
        events.push_back(event);
    while (events.size() > MaxEvents) {
        events.pop_front();
        ++droppedEvents;
    }
}

}
