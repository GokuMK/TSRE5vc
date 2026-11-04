#include <tsre/sim/SimulationHost.h>

#include <QFile>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <algorithm>

namespace Sim {

namespace {
// A line a tick (TSRE_SIM_TICK_LOG=<file>), on the host's thread: when it
// ran, the wall time since the tick before, the stepping's and the frame's
// cost, the steps taken and the simulated seconds; off, nothing.
struct TickLog {
    bool checked = false;
    QFile file;
    QTextStream out;
    long long startNs = 0;
    int lines = 0;
    ~TickLog() {
        if (file.isOpen())
            out.flush();
    }
    bool on() {
        if (!checked) {
            checked = true;
            const QString path = qEnvironmentVariable("TSRE_SIM_TICK_LOG");
            if (!path.isEmpty() && (file.setFileName(path), file.open(QIODevice::WriteOnly | QIODevice::Truncate
                                                                      | QIODevice::Text))) {
                out.setDevice(&file);
                out << "t_ms,interval_ms,step_ms,steps,publish_ms,simulated_s,mode,step_s,trains\n";
                startNs = SteadyNs();
            }
        }
        return file.isOpen();
    }
};

TickLog &tickLog() {
    static TickLog log;
    return log;
}
}

std::shared_ptr<SimulationFrame> MakeFrame(const SimulationCore &core, const SimulationFrame *before) {
    auto frame = std::make_shared<SimulationFrame>();
    frame->runId = core.input().runId;
    frame->steps = core.steps();
    frame->time = core.time();
    frame->askedTime = core.time() + core.pendingTime();
    frame->step = core.stepLength();
    frame->finished = core.finished();
    const std::vector<TrainState> &states = core.trains();
    if (before != nullptr && before->runId != frame->runId)
        before = nullptr;
    frame->trains.resize(states.size());
    // Trains where the steps have them; the views move them on to the time
    // of their own tick (the time not yet stepped and the frame's age),
    // along the junctions of their route part (SimulationController::
    // placeTrains), so they move every paint, not every step or tick.
    for (size_t i = 0; i < states.size(); ++i) {
        TrainFrame &train = frame->trains[i];
        train.state = states[i];
        core.vehicleEnds(int(i), train.fronts, train.rears);
        train.drawnTravelled = states[i].travelled;
        train.keepsToAuthority = core.drivenTrain() != int(i);
        train.carsVersion = core.carsVersion(int(i));
        // Its route part's nodes, shared with the frame before while it is
        // on the same part.
        if (const RouteTrack *part = core.routePart(int(i))) {
            train.pathPart = states[i].part;
            if (before != nullptr && i < before->trains.size() && before->trains[i].pathPart == train.pathPart
                    && before->trains[i].pathNodes != nullptr)
                train.pathNodes = before->trains[i].pathNodes;
            else {
                auto nodes = std::make_shared<std::vector<int>>();
                for (const RouteInterval &interval : part->intervals())
                    if (nodes->empty() || nodes->back() != interval.node)
                        nodes->push_back(interval.node);
                train.pathNodes = std::move(nodes);
            }
        }
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
    frame->driven = core.drivenTrain();
    frame->collision = core.collision();
    if (frame->driven >= 0 && core.cabOf(frame->driven) >= 0) {
        auto driven = std::make_shared<DrivenFrame>();
        driven->train = frame->driven;
        driven->cab = core.cabOf(frame->driven);
        const size_t n = core.vehiclesOf(frame->driven).size();
        driven->values.resize(n);
        for (size_t v = 0; v < n; ++v)
            if (const VehicleOutputs *out = core.outputsOf(frame->driven, int(v)))
                driven->values[v] = out->values;
        frame->drivenValues = std::move(driven);
    }
    // The heard trains' values: shared with their shadows until they step.
    for (int train : core.shadowed()) {
        HeardFrame heard;
        heard.train = train;
        heard.cab = core.cabOf(train);
        heard.version = core.shadowVersion(train);
        const size_t n = core.vehiclesOf(train).size();
        heard.values.resize(n);
        for (size_t v = 0; v < n; ++v)
            if (const VehicleOutputs *out = core.outputsOf(train, int(v)))
                heard.values[v] = out->values;
        frame->heard.push_back(std::move(heard));
    }
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

std::vector<TrainMessage> SimulationHost::takeMessages() {
    std::lock_guard<std::mutex> lock(mailbox);
    std::vector<TrainMessage> taken(messages.begin(), messages.end());
    messages.clear();
    return taken;
}

void SimulationHost::hear(const std::vector<int> &trains, unsigned long long seq) {
    acknowledged = seq;
    heardTrains = trains;
    hearing();
    publish();
}

void SimulationHost::hearing() {
    if (core != nullptr)
        core->hear(mode == RunMode::RunningTo ? std::vector<int>() : heardTrains);
}

void SimulationHost::drive(int train, unsigned long long seq) {
    acknowledged = seq;
    if (core != nullptr)
        core->drive(train);
    publish();
}

void SimulationHost::leave(unsigned long long seq) {
    acknowledged = seq;
    if (core != nullptr)
        core->leave(core->drivenTrain());
    publish();
}

void SimulationHost::command(int vehicle, const Message &message, unsigned long long seq) {
    acknowledged = seq;
    if (core != nullptr)
        core->command(core->drivenTrain(), vehicle, message);
    publish();
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
        messages.clear();
    }
    // Nothing heard until the main thread says.
    heardTrains.clear();
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
        // At the fast step from here (schedule), its whole steps: to the
        // first at or after it.
        core->setRealTime(false);
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
    // Every mode change comes here: the trains heard, none while running to
    // a time.
    hearing();
    const bool runs = core != nullptr && mode != RunMode::Paused;
    if (!runs) {
        timer->stop();
        return;
    }
    // Played in real time, every train at the real-time step; faster (the
    // multipliers, as fast as possible, running to a time) at the input's
    // (the user's decision of 2026-10-23, done 2026-11-03).
    core->setRealTime(mode == RunMode::Running && speed > 0.0 && speed <= 1.0);
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
    const qint64 sinceTick = wall.nsecsElapsed();
    const double elapsed = std::min(sinceTick * 1e-9, LongestTick);
    wall.start();
    QElapsedTimer batch;
    batch.start();
    const double before = core->time();
    const unsigned long long stepsBefore = core->steps();
    if (mode == RunMode::Running) {
        // At a pace: the wall time since the last tick × the speed, in steps
        // of at most 0.1 s; what a batch does not manage is not made up.
        stepFor(speed > 0.0 ? elapsed * speed : 1e12, batch);
    } else if (mode == RunMode::RunningTo) {
        stepFor(target - core->time(), batch);
        const bool placed = untilTrain >= 0 && trainPlaced(untilTrain);
        if (core->time() >= target - 1e-6 || placed) {
            // A train waited for that is not there by the time: paused
            // there (what it waits for is in its state).
            mode = runOn && (untilTrain < 0 || placed) ? RunMode::Running : RunMode::Paused;
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
    const qint64 stepped = batch.nsecsElapsed();
    publish();
    TickLog &log = tickLog();
    if (log.on()) {
        auto f = [](double value) { return QString::number(value, 'f', 3); };
        log.out << f((SteadyNs() - log.startNs) / 1e6) << ',' << f(sinceTick / 1e6) << ',' << f(stepped / 1e6) << ','
                << (core->steps() - stepsBefore) << ',' << f((batch.nsecsElapsed() - stepped) / 1e6) << ','
                << f(core->time() - before) << ',' << int(mode) << ',' << f(core->stepLength()) << ','
                << core->trains().size() << '\n';
        if (++log.lines % 120 == 0)
            log.out.flush();
    }
}

void SimulationHost::publish() {
    // Only this thread sets the latest frame: read without the lock.
    std::shared_ptr<SimulationFrame> frame = core != nullptr ? MakeFrame(*core, latest.get())
                                                             : std::make_shared<SimulationFrame>();
    // The driven cab's controls (task driving 08): its script's, so here
    // (MakeFrame reads the run only).
    if (core != nullptr && frame->drivenValues != nullptr) {
        std::vector<double> cab;
        if (core->cabValues(frame->driven, cab)) {
            auto driven = std::make_shared<DrivenFrame>(*frame->drivenValues);
            driven->cabControls = std::move(cab);
            frame->drivenValues = std::move(driven);
        }
    }
    frame->acknowledged = acknowledged;
    frame->tick = ++ticks;
    frame->publishedNs = SteadyNs();
    frame->mode = mode;
    frame->speed = speed;
    frame->rate = mode == RunMode::Paused ? 0.0 : rate;
    frame->target = target;
    std::vector<TrainEvent> fresh;
    std::vector<TrainMessage> sent;
    if (core != nullptr) {
        fresh = core->takeEvents();
        std::vector<int> trains = core->shadowed();
        if (core->drivenTrain() >= 0)
            trains.push_back(core->drivenTrain());
        for (int train : trains)
            for (std::pair<int, Message> &m : core->takeMessages(train))
                sent.push_back({train, m.first, std::move(m.second)});
    }
    std::lock_guard<std::mutex> lock(mailbox);
    latest = std::move(frame);
    for (const TrainEvent &event : fresh)
        events.push_back(event);
    while (events.size() > MaxEvents) {
        events.pop_front();
        ++droppedEvents;
    }
    for (TrainMessage &m : sent)
        messages.push_back(std::move(m));
    while (messages.size() > MaxMessages)
        messages.pop_front();
}

}
