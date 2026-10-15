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
#include <tsre/trains/ActivityNotifier.h>
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

// A pose as getDrawPositionOnTrNode gives it.
void drawPosition(const TrackPose &pose, float *out) {
    out[0] = pose.x;
    out[1] = pose.y;
    out[2] = pose.z;
    out[3] = pose.yaw;
    out[4] = pose.pitch;
    out[5] = float(pose.tileX);
    out[6] = float(pose.tileZ);
}

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
    std::shared_ptr<const SimulationInput> input = MakeInput(activity, Game::trackDB, limit);
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

void SimulationController::setWorldTime(int seconds) {
    const double hours = std::fmod(seconds / 3600.0, 24.0);
    QString error;
    if (!SettingsManager::instance().setSessionValue("core.rendering.timeOfDay.time", hours, &error))
        qWarning() << "Simulation:" << error;
    Game::timeOfDayHours = float(hours);
}

bool SimulationController::start(Activity *activity, QString *error) {
    stop();
    std::shared_ptr<const SimulationInput> input = makeInput(activity, error);
    if (input == nullptr)
        return false;
    // An edit still open (a field typed in) closes as its own step.
    Undo::StateEnd();
    current = activity;
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
    sendStart(input);
    startDate = Game::timeOfDayDate;
    worldTime = input->startTime;
    setWorldTime(input->startTime);
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
    if (target < frame()->time) {
        // Back: from the start again (MSAE's way).
        std::shared_ptr<const SimulationInput> input = makeInput(current, error);
        if (input == nullptr)
            return false;
        in = input;
        latest.reset();
        history.clear();
        sendStart(input);
        emit eventsAdded(0);
    }
    asked = RunMode::RunningTo;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, target, seq] { h->runTo(target, seq); }, Qt::QueuedConnection);
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
    double hours = 0.0, rate = 0.0;
    QString date;
    if (worldClock(hours, date, rate)) {
        SettingsManager &settings = SettingsManager::instance();
        if (!date.isEmpty())
            settings.setSessionValue("core.rendering.timeOfDay.date", date);
        settings.setSessionValue("core.rendering.timeOfDay.time", hours);
    }
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

bool SimulationController::worldClock(double &hours, QString &date, double &rate) const {
    if (!exists() || in == nullptr)
        return false;
    rate = 0.0;
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
    return true;
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
    const std::shared_ptr<const SimulationFrame> now = frame();
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
    if (in == nullptr || train < 0 || size_t(train) >= now->trains.size() || size_t(train) >= in->trains.size())
        return QString();
    const TrainPlan &plan = in->trains[size_t(train)];
    const TrainState &s = now->trains[size_t(train)].state;
    switch (s.status) {
    case TrainStatus::Waiting:
        //% "starts at %1"
        return qtTrId("simulation.state.waiting").arg(clock(plan.startTime));
    case TrainStatus::Running:
        //% "running %1 km/h"
        return qtTrId("simulation.state.running").arg(int(std::lround(s.speed * 3.6f)));
    case TrainStatus::Finished:
        //% "finished"
        return qtTrId("simulation.state.finished");
    case TrainStatus::Stopped:
        break;
    }
    switch (s.stoppedAt) {
    case StopKind::Station: {
        const int stop = s.nextStop - 1;
        const QString platform = stop >= 0 && size_t(stop) < plan.stops.size() ? plan.stops[size_t(stop)].platform
                                                                              : QString();
        //% "at %1 until %2"
        return qtTrId("simulation.state.station").arg(platform, clock(s.until));
    }
    case StopKind::Wait:
        //% "at a waiting point until %1"
        return qtTrId("simulation.state.wait").arg(clock(s.until));
    case StopKind::Reversal:
        //% "reversing"
        return qtTrId("simulation.state.reversal");
    case StopKind::End:
        //% "at its path's end"
        return qtTrId("simulation.state.end");
    case StopKind::None:
        break;
    }
    return QString();
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
    if (in != nullptr && !in->warnings.isEmpty())
        //% "Not simulated: %1."
        text += " " + qtTrId("simulation.status.warnings").arg(in->warnings.join("; "));
    return text;
}

void SimulationController::pushRenderItems(RenderQueue &queue, float *playerT, int renderMode) {
    if (!exists() || in == nullptr || Game::currentEngLib == nullptr)
        return;
    const std::shared_ptr<const SimulationFrame> now = frame();
    // Beyond the objects' distance, not drawn (tiles, with one to spare).
    const float reach = Game::objectLod / 2048.0f + 1.0f;
    const std::unordered_map<int, Eng *> &engs = Game::currentEngLib->eng;
    for (size_t t = 0; t < now->trains.size() && t < in->trains.size(); ++t) {
        const TrainFrame &train = now->trains[t];
        const TrainPlan &plan = in->trains[t];
        const int service = serviceId(int(t));
        for (size_t v = 0; v < plan.vehicles.size() && v < train.fronts.size() && v < train.rears.size(); ++v) {
            const TrackPose &a = train.fronts[v];
            if (std::fabs(a.tileX - playerT[0]) > reach || std::fabs(-a.tileZ - playerT[1]) > reach)
                continue;
            auto found = engs.find(plan.vehicles[v].eng);
            if (found == engs.end() || found->second == nullptr)
                continue;
            float front[7], rear[7];
            drawPosition(a, front);
            drawPosition(train.rears[v], rear);
            const quint32 id = renderMode == RenderQueue::RENDER_SELECTION
                    ? SelectionIdCodec::activityService(service, int(v)) : 0;
            const float roll = Game::useSuperelevation ? 0.5f * (a.roll + train.rears[v].roll) : 0.0f;
            found->second->pushRenderItemBetween(queue, playerT, id, int(t) == chosenTrain, front, rear, roll,
                                                 plan.vehicles[v].flip);
        }
    }
}

void SimulationController::getMapFeatures(MapFeatures &vehicles, int tileX, int tileZ, int only) const {
    if (!exists() || in == nullptr || Game::currentEngLib == nullptr)
        return;
    const std::shared_ptr<const SimulationFrame> now = frame();
    const std::unordered_map<int, Eng *> &engs = Game::currentEngLib->eng;
    for (size_t t = 0; t < now->trains.size() && t < in->trains.size(); ++t) {
        if (only >= 0 && int(t) != only)
            continue;
        const TrainFrame &train = now->trains[t];
        const TrainPlan &plan = in->trains[t];
        for (size_t v = 0; v < plan.vehicles.size() && v < train.fronts.size() && v < train.rears.size(); ++v) {
            auto found = engs.find(plan.vehicles[v].eng);
            if (found == engs.end() || found->second == nullptr)
                continue;
            float front[7], rear[7];
            drawPosition(train.fronts[v], front);
            drawPosition(train.rears[v], rear);
            found->second->getMapFeaturesBetween(vehicles, tileX, tileZ, front, rear);
        }
    }
}

int SimulationController::serviceId(int train) const {
    if (in == nullptr || train < 0 || size_t(train) >= in->trains.size())
        return -1;
    const TrainPlan &plan = in->trains[size_t(train)];
    return plan.player ? 0 : plan.traffic + 1;
}

}
