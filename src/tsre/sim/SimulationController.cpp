#include <tsre/sim/SimulationController.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QDate>
#include <QTime>
#include <algorithm>
#include <cmath>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/map/MapFeatures.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/sim/SimulationHost.h>
#include <tsre/sim/SimulationTexts.h>
#include <tsre/sound/SoundSystem.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/WaitingPoint.h>
#include <tsre/world/Route.h>
#include <tsre/world/Trk.h>

namespace Sim {

namespace {

QString clock(double seconds) {
    const int s = (int(std::floor(seconds)) % 86400 + 86400) % 86400;
    return QTime::fromMSecsSinceStartOfDay(s * 1000).toString("HH:mm:ss");
}

// A copy of a train's consist for the editor (SimulationController::
// consistOf), each vehicle its own Eng as a consist placed on the track has.
std::unique_ptr<Consist> copyConsist(const TrainPlan &plan) {
    if (Game::currentEngLib == nullptr || plan.consist.isEmpty())
        return nullptr;
    const int id = ConLib::addCon(Game::root + "/TRAINS/CONSISTS/", plan.consist + ".con");
    auto found = ConLib::con.find(id);
    if (found == ConLib::con.end() || found->second == nullptr || found->second->loaded != 1)
        return nullptr;
    std::unique_ptr<Consist> copy = std::make_unique<Consist>(found->second, true);
    for (Consist::EngItem &item : copy->engItems) {
        auto eng = Game::currentEngLib->eng.find(item.eng);
        if (eng == Game::currentEngLib->eng.end() || eng->second == nullptr)
            continue;
        item.engPointer = new Eng(eng->second);
        item.engPointer->flip = item.flip;
    }
    copy->isOnTrack = false;
    return copy;
}

// How far a train is heard, and how far it goes before it is silenced.
constexpr float HeardWithin = 1500.0f;
constexpr float SilencedBeyond = 2000.0f;

}

const std::vector<double> &SimulationController::Speeds() {
    static const std::vector<double> speeds = {1, 2, 4, 8, 16, 32, 64, 128, 0};
    return speeds;
}

SimulationController *SimulationController::instance() {
    static SimulationController *controller = nullptr;
    if (controller == nullptr) {
        controller = new SimulationController();
        // The thread ends before the application.
        if (QCoreApplication::instance() != nullptr)
            connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, controller,
                    [] { controller->stop(); });
    }
    return controller;
}

SimulationController::SimulationController() {
    pollTimer.setInterval(100);
    connect(&pollTimer, &QTimer::timeout, this, &SimulationController::poll);
    connect(this, &SimulationController::stateChanged, WorldClock::instance(), &WorldClock::sourceChanged);
}

SimulationController::~SimulationController() {
    stop();
}

std::shared_ptr<const SimulationInput> SimulationController::makeInput(Activity *activity, QString *error) {
    if (activity == nullptr || Game::trackDB == nullptr) {
        if (error != nullptr)
            *error =
                //% "Choose an activity first."
                qtTrId("simulation.error.no.activity");
        return nullptr;
    }
    QElapsedTimer timer;
    timer.start();
    Route *route = Game::currentRoute;
    const float limit = route != nullptr && route->trk != nullptr ? route->trk->speedLimit : 0.0f;
    const float restricted = route != nullptr && route->trk != nullptr ? route->trk->tempRestrictedSpeed : -1.0f;
    std::shared_ptr<const SimulationInput> input = MakeInput(activity, Game::trackDB, limit, nullptr, nullptr, restricted);
    inputMs = timer.elapsed();
    if (input->trains.empty()) {
        if (error != nullptr) {
            *error =
                //% "The activity has no train the simulation can run."
                qtTrId("simulation.error.no.trains");
            if (!input->warnings.isEmpty())
                *error += "\n" + input->warnings.join("\n");
        }
        return nullptr;
    }
    return input;
}

void SimulationController::sendStart(std::shared_ptr<const SimulationInput> input) {
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, input, seq] { h->start(input, seq); }, Qt::QueuedConnection);
    const double value = pace;
    const unsigned long long speedSeq = ++sequence;
    QMetaObject::invokeMethod(host, [h, value, speedSeq] { h->setSpeed(value, speedSeq); },
                              Qt::QueuedConnection);
}

bool SimulationController::start(Activity *activity, QString *error) {
    stop();
    std::shared_ptr<const SimulationInput> input = makeInput(activity, error);
    if (input == nullptr)
        return false;
    // An edit still open (a field typed in) closes as its own step.
    Undo::StateEnd();
    current = activity;
    dropTrains();
    in = input;
    latest.reset();
    history.clear();
    asked = RunMode::Paused;
    chosenTrain = -1;
    thread = new QThread();
    thread->setObjectName("Simulation");
    host = new SimulationHost();
    host->moveToThread(thread);
    thread->start();
    // The world's clock is the simulation's, from the activity's start, at
    // the clock's speed.
    if (WorldClock::instance()->source() == nullptr)
        pace = WorldClock::instance()->speed();
    sendStart(input);
    startDate = Game::timeOfDayDate;
    worldTime = input->startTime;
    WorldClock::instance()->setSource(this);
    SignalLights::setStateSource(this);
    ActivityNotifier::instance()->setEditingLocked(true);
    pollTimer.start();
    emit stateChanged();
    emit eventsAdded(0);
    return true;
}

bool SimulationController::play(Activity *activity, QString *error) {
    if (!exists() && !start(activity, error))
        return false;
    asked = RunMode::Running;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, seq] { h->play(seq); }, Qt::QueuedConnection);
    emit stateChanged();
    return true;
}

void SimulationController::pause() {
    if (!exists())
        return;
    asked = RunMode::Paused;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, seq] { h->pause(seq); }, Qt::QueuedConnection);
    emit stateChanged();
}

void SimulationController::setSpeed(double value) {
    pace = std::max(0.0, value);
    if (!exists())
        return;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    const double speed = pace;
    QMetaObject::invokeMethod(host, [h, speed, seq] { h->setSpeed(speed, seq); }, Qt::QueuedConnection);
    emit stateChanged();
}

bool SimulationController::runTo(Activity *activity, int timeOfDay, QString *error) {
    if (!exists() && !start(activity, error))
        return false;
    double target = timeOfDay;
    // Before the start: the next day's.
    if (target < in->startTime)
        target += 86400.0;
    return runToTime(target, false, error);
}

bool SimulationController::runToTime(double target, bool thenRun, QString *error) {
    if (!exists())
        return false;
    if (target < frame()->time) {
        // Back: from the start again (MSAE's way).
        std::shared_ptr<const SimulationInput> input = makeInput(current, error);
        if (input == nullptr)
            return false;
        dropTrains();
        in = input;
        latest.reset();
        history.clear();
        sendStart(input);
        emit eventsAdded(0);
    }
    asked = RunMode::RunningTo;
    const unsigned long long seq = ++sequence;
    runToSeq = seq;
    runToTarget = target;
    runToThenRun = thenRun;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, target, seq, thenRun] { h->runTo(target, seq, thenRun); },
                              Qt::QueuedConnection);
    emit stateChanged();
    return true;
}

void SimulationController::reset() {
    if (!exists())
        return;
    QString error;
    std::shared_ptr<const SimulationInput> input = makeInput(current, &error);
    if (input == nullptr) {
        qWarning() << "Simulation reset:" << error;
        return;
    }
    dropTrains();
    in = input;
    latest.reset();
    history.clear();
    sendStart(input);
    if (asked != RunMode::Paused) {
        asked = RunMode::Running;
        const unsigned long long seq = ++sequence;
        SimulationHost *h = host;
        QMetaObject::invokeMethod(host, [h, seq] { h->play(seq); }, Qt::QueuedConnection);
    }
    emit stateChanged();
    emit frameUpdated();
    emit eventsAdded(0);
}

void SimulationController::stop() {
    if (!exists())
        return;
    // The world's clock goes on from the simulation's time.
    if (WorldClock::instance()->source() == this)
        WorldClock::instance()->setSource(nullptr);
    SignalLights::setStateSource(nullptr);
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QThread *owner = QThread::currentThread();
    QMetaObject::invokeMethod(host, [h, seq, owner] { h->stop(seq, owner); }, Qt::QueuedConnection);
    // The host quits its thread when it has stopped.
    thread->wait();
    delete host;
    host = nullptr;
    delete thread;
    thread = nullptr;
    pollTimer.stop();
    current = nullptr;
    dropTrains();
    in.reset();
    latest.reset();
    history.clear();
    asked = RunMode::Paused;
    chosenTrain = -1;
    if (followedTrain != -1) {
        followedTrain = -1;
        emit followChanged(-1);
    }
    ActivityNotifier::instance()->setEditingLocked(false);
    emit stateChanged();
    emit frameUpdated();
    emit eventsAdded(0);
}

std::shared_ptr<const SimulationFrame> SimulationController::frame() const {
    static const std::shared_ptr<const SimulationFrame> none = std::make_shared<const SimulationFrame>();
    if (host != nullptr && in != nullptr) {
        std::shared_ptr<const SimulationFrame> now = host->frame();
        // Not yet of the run started last: the one taken before.
        if (now != nullptr && now->runId == in->runId)
            return now;
    }
    return latest != nullptr ? latest : none;
}

std::shared_ptr<const SimulationFrame> SimulationController::viewFrame() const {
    return pinnedFrame != nullptr && in != nullptr && pinnedFrame->runId == in->runId ? pinnedFrame : frame();
}

void SimulationController::pinViewFrame() {
    pinnedFrame = exists() ? frame() : nullptr;
}

void SimulationController::poll() {
    if (!exists() || in == nullptr)
        return;
    std::vector<TrainEvent> fresh;
    size_t dropped = 0;
    std::shared_ptr<const SimulationFrame> taken = host->take(fresh, &dropped);
    // Not yet the run started last: its events are the old run's.
    if (taken == nullptr || taken->runId != in->runId)
        return;
    if (dropped > 0)
        qWarning() << "Simulation: events dropped" << dropped;
    const bool changed = taken != latest;
    latest = taken;
    if (!fresh.empty()) {
        const int first = int(history.size());
        history.insert(history.end(), fresh.begin(), fresh.end());
        emit eventsAdded(first);
    }
    if (!changed)
        return;
    // A run to a time the host had passed when it took the command (running
    // as fast as possible, a day can go by while the command waits): from
    // the start again to that time, as for a time behind.
    if (runToSeq != 0 && latest->acknowledged >= runToSeq) {
        const bool passed = latest->acknowledged == runToSeq && latest->time > runToTarget + 1e-6;
        runToSeq = 0;
        if (passed && runToTime(runToTarget, runToThenRun, nullptr))
            return;
    }
    // Every command taken: the host's mode is the one (a run to a time ends
    // paused).
    if (latest->acknowledged >= sequence && latest->mode != asked) {
        asked = latest->mode;
        emit stateChanged();
    }
    emit frameUpdated();
}

bool SimulationController::waitFor(unsigned long long seq, int ms) {
    QElapsedTimer timer;
    timer.start();
    while (exists()) {
        poll();
        if (latest != nullptr && latest->acknowledged >= seq)
            return true;
        if (timer.elapsed() > ms)
            return false;
        QThread::msleep(1);
    }
    return false;
}

void SimulationController::clockNow(double &hours, QString &date, double &rate) const {
    rate = 0.0;
    if (!exists() || in == nullptr) {
        hours = Game::timeOfDayHours;
        date = Game::timeOfDayDate;
        return;
    }
    const std::shared_ptr<const SimulationFrame> now = frame();
    if (now->runId == in->runId) {
        const bool fastest = now->mode == RunMode::RunningTo
                || (now->mode == RunMode::Running && now->speed <= 0.0);
        if (!fastest) {
            // A frame is at most a host tick old.
            worldTime = now->time;
            rate = now->mode == RunMode::Running ? now->speed : 0.0;
        }
    }
    const double days = std::floor(worldTime / 86400.0);
    hours = (worldTime - days * 86400.0) / 3600.0;
    QDate day = QDate::fromString(startDate.trimmed(), "yyyy-MM-dd");
    date = day.isValid() ? day.addDays(qint64(days)).toString("yyyy-MM-dd") : startDate;
}

void SimulationController::setClockRunning(bool on) {
    if (on)
        play(current);
    else
        pause();
}

void SimulationController::setClockTime(double hours) {
    if (!exists())
        return;
    double shown = 0.0, rate = 0.0;
    QString date;
    clockNow(shown, date, rate);
    double target = std::floor(worldTime / 86400.0) * 86400.0 + hours * 3600.0;
    // Before the activity's start: the next day's.
    if (target < in->startTime)
        target += 86400.0;
    QString error;
    if (!runToTime(target, runs(), &error))
        qWarning() << "Simulation: the clock not set:" << error;
}

void SimulationController::setClockDate(const QString &date) {
    const QDate day = QDate::fromString(date.trimmed(), "yyyy-MM-dd");
    if (!exists() || !day.isValid())
        return;
    // The date shown becomes it: the start's moves as much.
    startDate = day.addDays(-qint64(std::floor(worldTime / 86400.0))).toString("yyyy-MM-dd");
    emit stateChanged();
}

unsigned long long SimulationController::stateSerial() const {
    return latest != nullptr ? (static_cast<unsigned long long>(latest->runId) << 32) ^ latest->drawSerial : 0;
}

QString SimulationController::drawStateOf(int item) const {
    if (latest == nullptr || in == nullptr || in->network == nullptr)
        return QString();
    const TrackNetwork &network = *in->network;
    const int head = network.headOfItem(item);
    if (head < 0 || latest->drawStates == nullptr || size_t(head) >= latest->drawStates->size())
        return QString();
    const int kind = network.heads()[size_t(head)].kind;
    if (kind < 0)
        return QString();
    return network.kinds()[size_t(kind)].drawStateNames.value((*latest->drawStates)[size_t(head)]);
}

int SimulationController::trainOf(bool player, int traffic) const {
    if (!exists() || in == nullptr)
        return -1;
    for (const TrainPlan &plan : in->trains)
        if (player ? plan.player : !plan.player && plan.traffic == traffic)
            return plan.id;
    return -1;
}

void SimulationController::choose(int train) {
    if (train == chosenTrain)
        return;
    chosenTrain = train;
    emit choiceChanged(train);
}

void SimulationController::follow(int train) {
    if (!exists())
        train = -1;
    if (train == followedTrain)
        return;
    followedTrain = train;
    emit followChanged(train);
}

bool SimulationController::trainPlace(int train, int *tile, float *position, float *heading, float *length) const {
    const std::shared_ptr<const SimulationFrame> now = viewFrame();
    if (train < 0 || size_t(train) >= now->trains.size())
        return false;
    const TrainFrame &t = now->trains[size_t(train)];
    if (t.fronts.empty() || t.rears.empty())
        return false;
    // The consist's first front and last rear, in the camera's space (tile
    // z and z the other way round), relative to the first's tile.
    const TrackPose &a = t.fronts.front();
    const TrackPose &b = t.rears.back();
    const float ax = a.x, az = -a.z;
    const float bx = b.x + 2048.0f * (b.tileX - a.tileX), bz = -b.z + 2048.0f * (-b.tileZ + a.tileZ);
    tile[0] = a.tileX;
    tile[1] = -a.tileZ;
    position[0] = 0.5f * (ax + bx);
    position[1] = 0.5f * (a.y + b.y);
    position[2] = 0.5f * (az + bz);
    // It heads the consist's way, or the other way when reversed.
    float dx = ax - bx, dy = a.y - b.y, dz = az - bz;
    if (t.state.reversed) {
        dx = -dx;
        dy = -dy;
        dz = -dz;
    }
    const float span = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length != nullptr)
        *length = in != nullptr && size_t(train) < in->trains.size() ? in->trains[size_t(train)].length : span;
    if (span > 0.001f) {
        heading[0] = dx / span;
        heading[1] = dy / span;
        heading[2] = dz / span;
    } else {
        heading[0] = 0.0f;
        heading[1] = 0.0f;
        heading[2] = 1.0f;
    }
    return true;
}

QString SimulationController::stateText(int train) const {
    const std::shared_ptr<const SimulationFrame> now = frame();
    if (in == nullptr || train < 0 || size_t(train) >= now->trains.size())
        return QString();
    return StateText(*in, now->trains[size_t(train)].state, now->time, train);
}

QString SimulationController::signalName(int signal) const {
    return in != nullptr ? SignalName(*in, signal) : QString();
}

QString SimulationController::waitText(WaitFor waitFor, int signal, int holder) const {
    return in != nullptr ? WaitText(*in, waitFor, signal, holder) : QString();
}

QString SimulationController::trainName(int train) const {
    return in != nullptr ? TrainName(*in, train) : QString();
}

QString SimulationController::holderName(int holder) const {
    return in != nullptr ? HolderName(*in, holder) : QString();
}

QString SimulationController::eventText(const TrainEvent &event) const {
    const TrainPlan *plan = in != nullptr && event.train >= 0 && size_t(event.train) < in->trains.size()
            ? &in->trains[size_t(event.train)] : nullptr;
    const StopPlan *stop = plan != nullptr && event.index >= 0 && size_t(event.index) < plan->stops.size()
            ? &plan->stops[size_t(event.index)] : nullptr;
    switch (event.kind) {
    case TrainEvent::Started:
        //% "started"
        return qtTrId("simulation.event.started");
    case TrainEvent::Arrived:
        if (stop != nullptr && stop->arrival >= 0)
            //% "arrived at %1, booked %2"
            return qtTrId("simulation.event.arrived.booked").arg(stop->platform, clock(stop->arrival));
        //% "arrived at %1"
        return qtTrId("simulation.event.arrived").arg(stop != nullptr ? stop->platform : QString());
    case TrainEvent::Departed:
        if (stop != nullptr && stop->departure >= 0)
            //% "departed from %1, booked %2"
            return qtTrId("simulation.event.departed.booked").arg(stop->platform, clock(stop->departure));
        //% "departed from %1"
        return qtTrId("simulation.event.departed").arg(stop != nullptr ? stop->platform : QString());
    case TrainEvent::Waiting: {
        const int value = plan != nullptr && event.index >= 0 && size_t(event.index) < plan->waits.size()
                ? plan->waits[size_t(event.index)].value : 0;
        //% "at a waiting point: %1"
        return qtTrId("simulation.event.waiting").arg(WaitingPoint::describe(value));
    }
    case TrainEvent::Reversed:
        //% "reversed"
        return qtTrId("simulation.event.reversed");
    case TrainEvent::Horn:
        //% "sounded the horn"
        return qtTrId("simulation.event.horn");
    case TrainEvent::Finished:
        //% "reached its path's end"
        return qtTrId("simulation.event.finished");
    case TrainEvent::Blocked:
        return waitText(event.waitFor, event.signal, event.index);
    case TrainEvent::Deadlock:
        //% "deadlocked: waits for %1, which waits for it in turn"
        return qtTrId("simulation.event.deadlock").arg(holderName(event.index));
    }
    return QString();
}

QString SimulationController::statusText() const {
    if (!exists())
        return QString();
    const std::shared_ptr<const SimulationFrame> now = frame();
    int waiting = 0, moving = 0, standing = 0, finished = 0;
    for (const TrainFrame &train : now->trains) {
        switch (train.state.status) {
        case TrainStatus::Waiting: ++waiting; break;
        case TrainStatus::Running: ++moving; break;
        case TrainStatus::Stopped: ++standing; break;
        case TrainStatus::Finished: ++finished; break;
        }
    }
    QString text = now->finished && !now->trains.empty()
            //% "Every train has finished its path."
            ? qtTrId("simulation.status.finished")
            //% "Trains: %1 waiting, %2 running, %3 standing, %4 finished."
            : qtTrId("simulation.status.trains").arg(waiting).arg(moving).arg(standing).arg(finished);
    // Trains waiting for each other for good: the user changes their times
    // or paths.
    QStringList deadlocked;
    for (size_t t = 0; t < now->trains.size(); ++t)
        if (now->trains[t].state.deadlocked)
            deadlocked << trainName(int(t));
    if (!deadlocked.isEmpty())
        //% "Deadlock: %1 wait for each other."
        text += " " + qtTrId("simulation.status.deadlock").arg(deadlocked.join(", "));
    if (in != nullptr && !in->warnings.isEmpty())
        //% "Not simulated: %1."
        text += " " + qtTrId("simulation.status.warnings").arg(in->warnings.join("; "));
    return text;
}

void SimulationController::pushRenderItems(RenderQueue &queue, float *playerT, int renderMode) {
    if (!exists() || in == nullptr)
        return;
    placeTrains();
    // Beyond the objects' distance, not drawn (tiles, with one to spare).
    const float reach = Game::objectLod / 2048.0f + 1.0f;
    for (size_t t = 0; t < consists.size() && t < placedFrame->trains.size(); ++t) {
        Consist *consist = consists[t].get();
        const TrainFrame &train = placedFrame->trains[t];
        if (consist == nullptr || !consist->isOnTrack || train.fronts.empty())
            continue;
        const TrackPose &a = train.fronts.front();
        if (std::fabs(a.tileX - playerT[0]) > reach || std::fabs(-a.tileZ - playerT[1]) > reach)
            continue;
        const quint32 id = renderMode == RenderQueue::RENDER_SELECTION
                ? SelectionIdCodec::activityService(serviceId(int(t))) : 0;
        consist->pushRenderItemsOnTrack(queue, playerT, id, int(t) == chosenTrain);
    }
}

void SimulationController::getMapFeatures(MapFeatures &vehicles, int tileX, int tileZ, int only) const {
    if (!exists() || in == nullptr)
        return;
    placeTrains();
    for (size_t t = 0; t < consists.size(); ++t) {
        if (only >= 0 && int(t) != only)
            continue;
        Consist *consist = consists[t].get();
        if (consist != nullptr && consist->isOnTrack)
            consist->getMapFeatures(vehicles, tileX, tileZ);
    }
}

Consist *SimulationController::consistOf(int train) const {
    placeTrains();
    if (train < 0 || size_t(train) >= consists.size())
        return nullptr;
    return consists[size_t(train)].get();
}

int SimulationController::trainOfObject(const GameObj *object) const {
    for (size_t t = 0; t < consists.size(); ++t)
        if (object != nullptr && consists[t].get() == object)
            return int(t);
    return -1;
}

int SimulationController::trainOfService(int service) const {
    for (size_t t = 0; in != nullptr && t < in->trains.size(); ++t)
        if (serviceId(int(t)) == service)
            return int(t);
    return -1;
}

void SimulationController::placeTrains() const {
    if (!exists() || in == nullptr)
        return;
    const std::shared_ptr<const SimulationFrame> now = viewFrame();
    if (now == placedFrame)
        return;
    placedFrame = now;
    if (consists.size() != in->trains.size())
        consists.resize(in->trains.size());
    for (size_t t = 0; t < in->trains.size(); ++t) {
        const TrainFrame *train = t < now->trains.size() ? &now->trains[t] : nullptr;
        if (train == nullptr || train->fronts.empty() || train->rears.size() != train->fronts.size()) {
            if (consists[t] != nullptr)
                consists[t]->isOnTrack = false;
            continue;
        }
        const TrainPlan &plan = in->trains[t];
        if (consists[t] == nullptr)
            consists[t] = copyConsist(plan);
        Consist *consist = consists[t].get();
        if (consist == nullptr)
            continue;
        // Each vehicle's two ends where the simulation has them, facing the
        // node's way where its front (as the consist lists it) does: the
        // way the train runs there, the other when it runs reversed.
        const bool reversed = train->state.reversed;
        for (int v = 0; v < consist->engItems.size() && size_t(v) < train->fronts.size(); ++v) {
            Eng *eng = consist->engItems[v].engPointer;
            const TrackPose &front = train->fronts[size_t(v)];
            const TrackPose &rear = train->rears[size_t(v)];
            if (eng == nullptr || front.node < 0 || rear.node < 0)
                continue;
            eng->placeOnTrack(front.node, front.distance, front.forward != reversed ? 0 : 1,
                              rear.node, rear.distance, rear.forward != reversed ? 0 : 1);
        }
        float throttle = 0.0f, brake = 1.0f;
        SoundControls(train->state.speed, train->state.acceleration, plan.acceleration, plan.deceleration,
                      throttle, brake);
        consist->setSimulated(train->state.speed, float(train->state.travelled), throttle, brake);
    }
}

void SimulationController::dropTrains() {
    if (consists.empty())
        return;
    emit trainsRemoving();
    for (std::unique_ptr<Consist> &consist : consists) {
        if (consist == nullptr)
            continue;
        for (Consist::EngItem &item : consist->engItems) {
            delete item.engPointer;
            item.engPointer = nullptr;
        }
    }
    consists.clear();
    placedFrame.reset();
    pinnedFrame.reset();
    heard.clear();
}

void SimulationController::updateSounds(float seconds, int tileX, int tileZ, const float *position) {
    if (!exists() || in == nullptr || !Game::soundEnabled || !SoundSystem::running())
        return;
    placeTrains();
    heard.resize(consists.size(), false);
    for (size_t t = 0; t < consists.size(); ++t) {
        Consist *consist = consists[t].get();
        if (consist == nullptr)
            continue;
        bool near = false;
        if (consist->isOnTrack && t < placedFrame->trains.size() && !placedFrame->trains[t].fronts.empty()) {
            // Its nearer end (the camera's space: tile z and z the other way).
            const TrainFrame &train = placedFrame->trains[t];
            float closest = 1e9f;
            for (const TrackPose *end : {&train.fronts.front(), &train.rears.back()}) {
                const float dx = 2048.0f * float(end->tileX - tileX) + end->x - position[0];
                const float dz = 2048.0f * float(-end->tileZ - tileZ) - end->z - position[2];
                closest = std::min(closest, std::sqrt(dx * dx + dz * dz));
            }
            near = closest < (heard[t] ? SilencedBeyond : HeardWithin);
        }
        if (near)
            consist->updateSimulatedSounds(seconds);
        else if (heard[t])
            consist->silenceSounds();
        heard[t] = near;
    }
}

void SimulationController::SoundControls(float speed, float acceleration, float maxAcceleration,
                                         float maxDeceleration, float &throttle, float &brake) {
    // As the stand-in driver: a third of the throttle to hold the speed,
    // more by the share of its acceleration it gains; braking by the share
    // of its deceleration; standing braked.
    const float gaining = maxAcceleration > 0.0f ? acceleration / maxAcceleration : 0.0f;
    const float losing = maxDeceleration > 0.0f ? -acceleration / maxDeceleration : 0.0f;
    if (std::fabs(speed) < 0.05f) {
        throttle = gaining > 0.02f ? std::clamp(0.3f + 0.7f * gaining, 0.0f, 1.0f) : 0.0f;
        brake = throttle > 0.0f ? 0.0f : 1.0f;
    } else if (losing <= 0.02f) {
        throttle = std::clamp(0.3f + 0.7f * std::max(gaining, 0.0f), 0.0f, 1.0f);
        brake = 0.0f;
    } else {
        throttle = 0.0f;
        brake = std::clamp(losing, 0.0f, 1.0f);
    }
}

int SimulationController::serviceId(int train) const {
    if (in == nullptr || train < 0 || size_t(train) >= in->trains.size())
        return -1;
    const TrainPlan &plan = in->trains[size_t(train)];
    return plan.player ? 0 : plan.traffic + 1;
}

}
