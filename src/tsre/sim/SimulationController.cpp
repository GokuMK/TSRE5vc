#include <tsre/sim/SimulationController.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QDate>
#include <QDir>
#include <QTime>
#include <algorithm>
#include <cmath>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/map/MapFeatures.h>
#include <tsre/renderer/RenderQueue.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/map/MapLabelLayer.h>
#include <tsre/sim/EventResults.h>
#include <tsre/sim/SimulationHost.h>
#include <tsre/sim/SimulationTexts.h>
#include <tsre/sim/WorkPlanning.h>
#include <tsre/sound/SoundSystem.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityEvent.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/Consist.h>
#include <tsre/trains/Eng.h>
#include <tsre/trains/EngLib.h>
#include <tsre/trains/WaitingPoint.h>
#include <tsre/world/Route.h>
#include <tsre/world/Tile.h>
#include <tsre/world/Trk.h>
#include <tsre/world/objects/PlatformObj.h>
#include <tsre/tdb/TRitem.h>

namespace Sim {

namespace {

QString clock(double seconds) {
    const int s = (int(std::floor(seconds)) % 86400 + 86400) % 86400;
    return QTime::fromMSecsSinceStartOfDay(s * 1000).toString("HH:mm:ss");
}

void refit(Consist &consist, const std::vector<Vehicle> &vehicles);

// A copy of a train's consist for the editor (SimulationController::
// consistOf), each vehicle its own Eng as a consist placed on the track has.
// Where the name is no consist file (an Open Rails timetable's consist of
// several parts or turned, task 08): made from the train's vehicles.
std::unique_ptr<Consist> copyConsist(const TrainPlan &plan) {
    if (Game::currentEngLib == nullptr || plan.consist.isEmpty())
        return nullptr;
    const int id = ConLib::addCon(Game::root + "/TRAINS/CONSISTS/", plan.consist + ".con");
    auto found = ConLib::con.find(id);
    if (found == ConLib::con.end() || found->second == nullptr || found->second->loaded != 1) {
        if (plan.vehicles.empty())
            return nullptr;
        std::unique_ptr<Consist> made = std::make_unique<Consist>();
        made->displayName = plan.consist;
        made->showName = plan.consist;
        made->isOnTrack = false;
        refit(*made, plan.vehicles);
        return made;
    }
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

// A consist copy made again from a train's vehicles (task 07: they change
// as it couples and cuts): each its Eng, flipped as the vehicle is. The
// copy itself stays (the camera rides it, the selection holds it).
void refit(Consist &consist, const std::vector<Vehicle> &vehicles) {
    consist.silenceSounds();
    for (Consist::EngItem &item : consist.engItems)
        delete item.engPointer;
    consist.engItems.clear();
    for (const Vehicle &vehicle : vehicles) {
        Consist::EngItem item;
        item.eng = vehicle.eng;
        item.flip = vehicle.flip;
        item.uid = int(vehicle.id % 65536u);
        Eng *eng = nullptr;
        if (Game::currentEngLib != nullptr) {
            auto found = Game::currentEngLib->eng.find(vehicle.eng);
            if (found != Game::currentEngLib->eng.end())
                eng = found->second;
        }
        if (eng != nullptr) {
            item.type = eng->definition->wagonTypeId;
            item.ename = eng->name;
            item.engPointer = new Eng(eng);
            item.engPointer->flip = vehicle.flip;
        }
        consist.engItems.push_back(item);
    }
    if (consist.cameraVehicle >= int(consist.engItems.size()))
        consist.cameraVehicle = 0;
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
    std::shared_ptr<SimulationInput> input = MakeInput(activity, Game::trackDB, limit, nullptr, nullptr, restricted, false);
    // The line's voltage, as Open Rails reads it: every track electrified.
    if (route != nullptr && route->trk != nullptr)
        input->lineVoltage = route->trk->maxLineVoltage;
    input->locoScripts = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/scripts/loco");
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
    // The player's work orders (task 07): planned off the main thread.
    if (!PlanWorkOrders(*input, error))
        return nullptr;
    // Driving from the start (task driving 02): the player's train, at the
    // driving step.
    if (drivingRun) {
        for (const TrainPlan &plan : input->trains)
            if (plan.player && std::any_of(plan.vehicles.begin(), plan.vehicles.end(),
                                           [](const Vehicle &v) { return v.type() >= 4; })) {
                input->driven = plan.id;
                input->step = SimulationCore::DrivenStep;
                break;
            }
        if (input->driven < 0) {
            if (error != nullptr)
                *error = activity->isTimetables()
                        //% "Choose the player's train first: a train with an engine."
                        ? qtTrId("simulation.error.drive.timetable")
                        //% "The activity has no player's train with an engine to drive."
                        : qtTrId("simulation.error.drive.player");
            return nullptr;
        }
    }
    inputMs = timer.elapsed();
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

bool SimulationController::start(Activity *activity, QString *error, bool driving) {
    stop();
    drivingRun = driving;
    std::shared_ptr<const SimulationInput> input = makeInput(activity, error);
    if (input == nullptr)
        return false;
    // An edit still open (a field typed in) closes as its own step.
    Undo::StateEnd();
    current = activity;
    dropTrains();
    in = input;
    EventResultStore::instance()->recordInput(current, input->warnings);
    latest.reset();
    history.clear();
    messages.clear();
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

bool SimulationController::playDriving(Activity *activity, QString *error) {
    if (!start(activity, error, true))
        return false;
    return play(activity, error);
}

bool SimulationController::drivable(int train) const {
    if (in == nullptr || train < 0 || size_t(train) >= in->trains.size())
        return false;
    const std::vector<Vehicle> &vehicles = in->trains[size_t(train)].vehicles;
    return std::any_of(vehicles.begin(), vehicles.end(), [](const Vehicle &v) { return v.type() >= 4; });
}

bool SimulationController::drive(int train, QString *error) {
    if (!exists() || !drivable(train)) {
        if (error != nullptr)
            *error =
                //% "No engine drives this train."
                qtTrId("simulation.error.drive.engine");
        return false;
    }
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, train, seq] { h->drive(train, seq); }, Qt::QueuedConnection);
    return true;
}

void SimulationController::leave() {
    if (!exists())
        return;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, seq] { h->leave(seq); }, Qt::QueuedConnection);
}

int SimulationController::driven() const {
    return exists() && latest != nullptr ? latest->driven : -1;
}

std::shared_ptr<const DrivenFrame> SimulationController::drivenValues() const {
    const std::shared_ptr<const SimulationFrame> now = viewFrame();
    return now != nullptr ? now->drivenValues : nullptr;
}

void SimulationController::command(const QString &name, const MessageValue &value, int vehicle) {
    if (!exists())
        return;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    const Sim::Message message{name, value};
    QMetaObject::invokeMethod(host, [h, vehicle, message, seq] { h->command(vehicle, message, seq); },
                              Qt::QueuedConnection);
}

std::vector<std::pair<int, Sim::Message>> SimulationController::takeDrivenMessages() {
    if (!exists())
        return {};
    std::vector<std::pair<int, Sim::Message>> taken = host->takeMessages();
    // Its sound triggers on its cars (Open Rails' numbers, the SMS files').
    const int train = driven();
    Consist *consist = train >= 0 ? consistOf(train) : nullptr;
    if (consist != nullptr)
        for (const auto &[vehicle, m] : taken)
            if (m.name == QLatin1String("sound"))
                consist->soundEvent(vehicle, int(m.number(-1.0)));
    return taken;
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
    return runToTime(timeOfRunTo(timeOfDay), false, error);
}

double SimulationController::timeOfRunTo(int timeOfDay) const {
    // Before the start: the next day's.
    return in != nullptr && timeOfDay < in->startTime ? timeOfDay + 86400.0 : double(timeOfDay);
}

bool SimulationController::runToTime(double target, bool thenRun, QString *error, int untilOnTrack) {
    if (!exists())
        return false;
    if (target < frame()->time) {
        // Back: from the start again (MSAE's way).
        std::shared_ptr<const SimulationInput> input = makeInput(current, error);
        if (input == nullptr)
            return false;
        dropTrains();
        in = input;
        EventResultStore::instance()->recordInput(current, input->warnings);
        latest.reset();
        history.clear();
        messages.clear();
        sendStart(input);
        emit eventsAdded(0);
    }
    asked = RunMode::RunningTo;
    const unsigned long long seq = ++sequence;
    runToSeq = seq;
    runToTarget = target;
    runToThenRun = thenRun;
    runToTrain = untilOnTrack;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, target, seq, thenRun, untilOnTrack] { h->runTo(target, seq, thenRun, untilOnTrack); },
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
    EventResultStore::instance()->recordInput(current, input->warnings);
    latest.reset();
    history.clear();
    messages.clear();
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
    // The events as the run left them (task 06).
    if (recordedTime != (latest != nullptr ? latest->time : -1.0))
        recordEvents();
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
    messages.clear();
    ++messageChanges;
    asked = RunMode::Paused;
    chosenTrain = -1;
    drivingRun = false;
    if (drivenTrain != -1) {
        drivenTrain = -1;
        emit drivenChanged(-1);
    }
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
        // Events with a message: shown beside the train that fired them.
        for (const TrainEvent &event : fresh) {
            if (event.kind != TrainEvent::EventFired)
                continue;
            const QString text = messageOf(event.index);
            if (text.isEmpty())
                continue;
            int train = event.train;
            if (train < 0)
                for (const TrainPlan &plan : in->trains)
                    if (plan.player)
                        train = plan.id;
            if (train < 0)
                continue;
            if (!messageClock.isValid())
                messageClock.start();
            messages.erase(std::remove_if(messages.begin(), messages.end(),
                                          [train](const Message &m) { return m.train == train; }), messages.end());
            const ActivityEvent *fired = activityEvent(event.index);
            messages.push_back({train, (fired != nullptr ? fired->name + QStringLiteral(": ") : QString()) + text,
                                messageClock.elapsed()});
            ++messageChanges;
        }
        emit eventsAdded(first);
    }
    if (!changed)
        return;
    // The activity's events as the run has them (task 06), for the Event
    // window and the map; the time alone when they have not changed.
    if (latest->eventStates != nullptr) {
        if (recordedRun != in->runId || recordedSerial != latest->eventSerial || recordedCars != carsSerial(*latest)
                || (latest->mode == RunMode::Paused && recordedTime != latest->time))
            recordEvents();
        else
            EventResultStore::instance()->advance(current, latest->time, latest->finished);
    }
    // A run to a time the host had passed when it took the command (running
    // as fast as possible, a day can go by while the command waits): from
    // the start again to that time, as for a time behind.
    if (runToSeq != 0 && latest->acknowledged >= runToSeq) {
        // The host stops at the first whole step at or after the target.
        const bool passed = latest->acknowledged == runToSeq && latest->time > runToTarget + SimulationCore::MaxStep;
        runToSeq = 0;
        if (passed && runToTime(runToTarget, runToThenRun, nullptr, runToTrain))
            return;
    }
    // Every command taken: the host's mode is the one (a run to a time ends
    // paused).
    if (latest->acknowledged >= sequence && latest->mode != asked) {
        asked = latest->mode;
        emit stateChanged();
    }
    if (latest->driven != drivenTrain) {
        drivenTrain = latest->driven;
        emit drivenChanged(drivenTrain);
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

bool SimulationController::followFromStart(int train, QString *error) {
    if (!exists() || in == nullptr || train < 0 || size_t(train) >= in->trains.size())
        return false;
    follow(train);
    // On the track already (or gone at its end): followed as it is. Not
    // yet: as fast as possible until the step it is put on the track
    // (created, ready to start), FollowWithin after its start time at the
    // latest, paused there.
    const std::shared_ptr<const SimulationFrame> now = frame();
    if (size_t(train) < now->trains.size() && now->trains[size_t(train)].state.status != TrainStatus::Waiting)
        return true;
    const double latest = in->trains[size_t(train)].startTime + FollowWithin;
    if (now->time >= latest)
        return true;
    return runToTime(latest, true, error, train);
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

bool SimulationController::trainHead(int train, int *tile, float *position, int *vehicle) const {
    const std::shared_ptr<const SimulationFrame> now = viewFrame();
    if (train < 0 || size_t(train) >= now->trains.size())
        return false;
    const TrainFrame &t = now->trains[size_t(train)];
    if (t.fronts.empty() || t.rears.empty())
        return false;
    const TrackPose &head = t.state.reversed ? t.rears.back() : t.fronts.front();
    if (vehicle != nullptr)
        *vehicle = t.state.reversed ? int(t.rears.size()) - 1 : 0;
    tile[0] = head.tileX;
    tile[1] = -head.tileZ;
    position[0] = head.x;
    position[1] = head.y;
    position[2] = -head.z;
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
    case TrainEvent::EventFired: {
        const ActivityEvent *fired = activityEvent(event.index);
        const QString name = fired == nullptr ? QString::number(event.index)
                           : fired->name.isEmpty() ? QString::number(fired->id) : fired->name;
        QString text = event.signal == 1
                //% "event %1 (by the player's train: its triggering train had not started)"
                ? qtTrId("simulation.event.activity.standin").arg(name)
                //% "event %1"
                : qtTrId("simulation.event.activity").arg(name);
        const QString message = messageOf(event.index);
        if (!message.isEmpty())
            text += QStringLiteral(": ") + message;
        return text;
    }
    case TrainEvent::ActivityEnded:
        return event.index == 1
                //% "the activity ends: success"
                ? qtTrId("simulation.event.activity.success") :
                //% "the activity ends without success"
                qtTrId("simulation.event.activity.failure");
    case TrainEvent::Coupled:
        //% "coupled to %1 cars"
        return qtTrId("simulation.event.coupled").arg(event.signal);
    case TrainEvent::Joined:
        //% "joined the cars standing ahead"
        return qtTrId("simulation.event.joined");
    case TrainEvent::Uncoupled:
        //% "cut off %1 cars"
        return qtTrId("simulation.event.uncoupled").arg(event.signal);
    case TrainEvent::TookCars:
        //% "took %1 cars from %2"
        return qtTrId("simulation.event.took").arg(event.signal).arg(holderName(event.index));
    case TrainEvent::LeftCars:
        //% "left %1 cars to %2"
        return qtTrId("simulation.event.left").arg(event.signal).arg(holderName(event.index));
    case TrainEvent::JoinedTrain:
        //% "joined %1"
        return qtTrId("simulation.event.joined.train").arg(holderName(event.index));
    case TrainEvent::Parted:
        //% "runs on its own again, parted from %1"
        return qtTrId("simulation.event.parted").arg(holderName(event.index));
    case TrainEvent::Removed:
        //% "removed: the cars it took stand off its path (as Open Rails does)"
        return qtTrId("simulation.event.removed");
    case TrainEvent::WorkSkipped: {
        const ActivityEvent *skipped = activityEvent(event.index);
        const QString name = skipped == nullptr ? QString::number(event.index)
                           : skipped->name.isEmpty() ? QString::number(skipped->id) : skipped->name;
        return WorkSkippedText(*in, event.index, name);
    }
    case TrainEvent::DrivingStarted:
        //% "driven from here"
        return qtTrId("simulation.event.driving.started");
    case TrainEvent::DrivingLeft:
        //% "given back to the AI"
        return qtTrId("simulation.event.driving.left");
    case TrainEvent::PassedAtDanger:
        //% "passed a signal at danger"
        return qtTrId("simulation.event.passed.danger");
    case TrainEvent::PassedAuthority:
        //% "ran past the end of the track given to it"
        return qtTrId("simulation.event.passed.authority");
    case TrainEvent::EndOfPath:
        //% "stopped at the end of its path"
        return qtTrId("simulation.event.path.end");
    case TrainEvent::Collided:
        //% "ran into another train: the run ends"
        return qtTrId("simulation.event.collided");
    case TrainEvent::Restarted: {
        const ActivityEvent *by = activityEvent(event.index);
        //% "restarted by event %1"
        return qtTrId("simulation.event.restarted")
                .arg(by == nullptr ? QString::number(event.index) : by->name.isEmpty() ? QString::number(by->id) : by->name);
    }
    }
    return QString();
}

void SimulationController::recordEvents() {
    if (in == nullptr || latest == nullptr || latest->eventStates == nullptr || latest->runId != in->runId)
        return;
    recordedRun = in->runId;
    recordedSerial = latest->eventSerial;
    recordedTime = latest->time;
    recordedCars = carsSerial(*latest);
    const CarsNow cars = CarsOf(*latest);
    EventResultStore::instance()->record(current, *in, *latest->eventStates, latest->activityEnd,
                                         latest->activityEndedAt, latest->time, latest->finished, &cars);
}

unsigned long long SimulationController::carsSerial(const SimulationFrame &frame) {
    unsigned long long serial = frame.standingSerial;
    for (const TrainFrame &train : frame.trains)
        serial += train.carsVersion;
    return serial;
}

QString SimulationController::messageOf(int event) const {
    const ActivityEvent *fired = activityEvent(event);
    if (fired != nullptr)
        for (const ActivityEvent::Outcome *outcome : fired->outcomes)
            if (outcome != nullptr && outcome->type == ActivityEvent::Outcome::TypeDisplayMessage
                    && !outcome->value.toString().isEmpty())
                return outcome->value.toString().simplified();
    return QString();
}

void SimulationController::messageLabels(std::vector<MapLabel> &out) const {
    if (!exists() || !messageClock.isValid())
        return;
    const qint64 now = messageClock.elapsed();
    for (const Message &message : messages) {
        int tile[2];
        float position[3], heading[3];
        if (now - message.at > MessageMs || !trainPlace(message.train, tile, position, heading))
            continue;
        MapLabel label;
        label.tileX = tile[0];
        label.tileZ = tile[1];
        label.x = position[0];
        label.z = position[2];
        // A long message is cut: the Timetable window lists it whole.
        label.text = message.text.size() > 80 ? message.text.left(79) + QChar(0x2026) : message.text;
        label.kind = MapLabelKind::Event;
        // Before every other label.
        label.priority = 1e15;
        out.push_back(std::move(label));
    }
}

unsigned long long SimulationController::messagesVersion() const {
    // Changes as messages come and as they go.
    unsigned long long shown = 0;
    if (messageClock.isValid()) {
        const qint64 now = messageClock.elapsed();
        for (const Message &message : messages)
            shown += now - message.at <= MessageMs ? 1 : 0;
    }
    return messageChanges * 1000 + shown;
}

const ActivityEvent *SimulationController::activityEvent(int event) const {
    if (in == nullptr || current == nullptr || event < 0 || size_t(event) >= in->events.size())
        return nullptr;
    const int index = in->events[size_t(event)].index;
    return index >= 0 && index < current->event.size() ? current->event[index] : nullptr;
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
            ? qtTrId("simulation.status.finished") :
            //% "Trains: %1 waiting, %2 running, %3 standing, %4 finished."
            qtTrId("simulation.status.trains").arg(waiting).arg(moving).arg(standing).arg(finished);
    // Trains waiting for each other for good: the user changes their times
    // or paths.
    QStringList deadlocked;
    for (size_t t = 0; t < now->trains.size(); ++t)
        if (now->trains[t].state.deadlocked)
            deadlocked << trainName(int(t));
    if (!deadlocked.isEmpty())
        //% "Deadlock: %1 wait for each other."
        text += " " + qtTrId("simulation.status.deadlock").arg(deadlocked.join(", "));
    return text;
}

void SimulationController::pushRenderItems(RenderQueue &queue, float *playerT, int renderMode) {
    if (!exists() || in == nullptr)
        return;
    placeTrains();
    // Doors due at a platform whose tile was not loaded: again, frame or no
    // new frame (a paused simulation sends none).
    for (size_t t = 0; t < doorStops.size() && t < placedFrame->trains.size(); ++t)
        if (doorStops[t].pending)
            applyDoors(t, placedFrame->trains[t], placedFrame->time);
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
    for (size_t k = 0; k < standingCopies.size() && placedFrame->standing != nullptr && k < placedFrame->standing->size(); ++k) {
        Consist *consist = standingCopies[k].get();
        const StandingFrame &st = (*placedFrame->standing)[k];
        if (consist == nullptr || st.fronts.empty())
            continue;
        const TrackPose &a = st.fronts.front();
        if (std::fabs(a.tileX - playerT[0]) > reach || std::fabs(-a.tileZ - playerT[1]) > reach)
            continue;
        consist->pushRenderItemsOnTrack(queue, playerT, 0, false, true);
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
    if (only < 0)
        for (const std::unique_ptr<Consist> &consist : standingCopies)
            if (consist != nullptr)
                consist->getMapFeatures(vehicles, tileX, tileZ);
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
    if (consists.size() != in->trains.size()) {
        consists.resize(in->trains.size());
        consistVersions.resize(in->trains.size(), 0);
    }
    for (size_t t = 0; t < in->trains.size(); ++t) {
        const TrainFrame *train = t < now->trains.size() ? &now->trains[t] : nullptr;
        if (train == nullptr || train->fronts.empty() || train->rears.size() != train->fronts.size()) {
            if (consists[t] != nullptr)
                consists[t]->isOnTrack = false;
            continue;
        }
        const TrainPlan &plan = in->trains[t];
        if (consists[t] == nullptr) {
            // The plan's vehicles: the core's version 0.
            consists[t] = copyConsist(plan);
            consistVersions[t] = 0;
        }
        Consist *consist = consists[t].get();
        if (consist == nullptr)
            continue;
        // Its vehicles changed (task 07): the copy made again from them.
        if (train->vehicles != nullptr && train->carsVersion != consistVersions[t]) {
            refit(*consist, *train->vehicles);
            consistVersions[t] = train->carsVersion;
        }
        // Driven (task driving 02): its vehicles' values from its models.
        const DrivenFrame *driven = now->drivenValues != nullptr && now->drivenValues->train == int(t)
                ? now->drivenValues.get() : nullptr;
        const auto valueOf = [driven](int vehicle, const QString &name, double otherwise) {
            if (driven == nullptr || vehicle < 0 || size_t(vehicle) >= driven->values.size())
                return otherwise;
            const auto found = driven->values[size_t(vehicle)].constFind(name);
            return found == driven->values[size_t(vehicle)].cend() ? otherwise
                                                                    : Sim::Message{QString(), *found}.number(otherwise);
        };
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
            // Its wheels turned by the way gone; an electric locomotive's
            // pantograph 1 up, as Open Rails raises an AI train's (task
            // shapes 12); a driven one's as its model has them.
            eng->setTravelled(train->drawnTravelled, reversed);
            // Its sounds from its model's values (task sound 02); a train
            // nobody drives has the stand-in's.
            if (driven != nullptr && size_t(v) < driven->values.size()) {
                const CarSoundInput sound = Sim::carSoundInput(driven->values[size_t(v)]);
                eng->setSoundInput(&sound);
            } else {
                eng->setSoundInput(nullptr);
            }
            if (driven != nullptr && size_t(v) < driven->values.size()) {
                for (int n = 1; n <= 2; ++n) {
                    const double up = valueOf(v, QString("pantograph%1").arg(n), -1.0);
                    if (up >= 0.0)
                        eng->setPantograph(n, up > 0.0);
                }
            } else if (eng->definition->wagonTypeId == 4) {
                eng->setPantograph(1, true);
            }
        }
        applyDoors(t, *train, now->time);
        consist->setSoundCab(driven != nullptr && driven->cab >= 0 ? driven->cab : 0);
        float throttle = 0.0f, brake = 1.0f;
        if (driven != nullptr && driven->cab >= 0) {
            // Its cab's lever and its brake cylinder (Eng::updateSounds
            // takes 50 psi as full); its headlight.
            throttle = float(std::clamp(valueOf(driven->cab, "throttle", 0.0), 0.0, 1.0));
            brake = float(std::clamp(valueOf(driven->cab, "brakeCylinder", 0.0) / (50.0 * 6894.757293168), 0.0, 1.0));
            consist->setHeadlight(int(valueOf(driven->cab, "headlight", -1.0)));
        } else {
            SoundControls(train->state.speed, train->state.acceleration, plan.acceleration, plan.deceleration,
                          throttle, brake);
            consist->setHeadlight(-1);
        }
        consist->setSimulated(train->state.speed, float(train->state.travelled), throttle, brake);
    }
    // The standing trains (task 07), made again whenever they change; each
    // vehicle facing as in a train running the other way (standingEnds).
    if (now->standing != nullptr && now->standingSerial != standingPlaced) {
        standingPlaced = now->standingSerial;
        for (std::unique_ptr<Consist> &copy : standingCopies)
            if (copy != nullptr)
                refit(*copy, {});
        standingCopies.clear();
        standingCopies.resize(now->standing->size());
        for (size_t k = 0; k < now->standing->size(); ++k) {
            const StandingFrame &st = (*now->standing)[k];
            if (st.gone || st.fronts.size() != st.vehicles.size() || st.rears.size() != st.vehicles.size())
                continue;
            auto copy = std::make_unique<Consist>();
            copy->name = st.name;
            copy->displayName = st.name;
            refit(*copy, st.vehicles);
            for (size_t v = 0; v < st.vehicles.size() && v < size_t(copy->engItems.size()); ++v) {
                Eng *eng = copy->engItems[int(v)].engPointer;
                const TrackPose &front = st.fronts[v];
                const TrackPose &rear = st.rears[v];
                if (eng == nullptr || front.node < 0 || rear.node < 0)
                    continue;
                eng->placeOnTrack(front.node, front.distance, front.forward ? 1 : 0,
                                  rear.node, rear.distance, rear.forward ? 1 : 0);
            }
            copy->isOnTrack = true;
            standingCopies[k] = std::move(copy);
        }
    }
}

// Its doors at a station stop, on the platform's side (task shapes 12): a
// car's own left and right, swapped where it faces the other way from the
// way the train runs.
void SimulationController::applyDoors(size_t train, const TrainFrame &frame, double now) const {
    Consist *consist = train < consists.size() ? consists[train].get() : nullptr;
    if (consist == nullptr)
        return;
    bool left = false, right = false;
    stationDoors(train, frame, now, left, right);
    const bool reversed = frame.state.reversed;
    for (int v = 0; v < consist->engItems.size(); ++v) {
        Eng *eng = consist->engItems[v].engPointer;
        if (eng == nullptr)
            continue;
        const bool turned = (consist->engItems[v].flip != 0) != reversed;
        eng->setAnimated(CarAnimation::DoorLeft, turned ? right : left);
        eng->setAnimated(CarAnimation::DoorRight, turned ? left : right);
    }
}

void SimulationController::stationDoors(size_t train, const TrainFrame &frame, double now, bool &left,
                                        bool &right) const {
    left = right = false;
    if (doorStops.size() != in->trains.size())
        doorStops.assign(in->trains.size(), DoorStop());
    DoorStop &door = doorStops[train];
    door.pending = false;
    const TrainState &state = frame.state;
    const TrainPlan &plan = in->trains[train];
    // At a station stop, it stands at the stop before its next.
    const int stop = state.stoppedAt == StopKind::Station ? state.nextStop - 1 : -1;
    if (stop < 0 || size_t(stop) >= plan.stops.size() || plan.stops[size_t(stop)].platformItem < 0) {
        door.stop = -1;
        return;
    }
    if (door.stop != stop) {
        door.stop = stop;
        door.arrived = now;
    }
    // Open Rails' AI trains: open 4 s after arriving, closed 10 s before
    // leaving; when that leaves under 6 s open, at once and until 3 s
    // before leaving.
    double open = door.arrived + 4.0, close = state.until - 10.0;
    if (close - 6.0 < open) {
        open = door.arrived;
        close = std::max(state.until - 3.0, door.arrived);
    }
    if (now < open || now >= close)
        return;
    const int item = plan.stops[size_t(stop)].platformItem;
    const int side = platformSide(item);
    // Its tile not loaded: looked for again on the next draws while due.
    door.pending = side < 0;
    if (side <= 0)
        return;
    // The platform's sides as seen from its track item marked as its front
    // (ffff0000) the other way round (Open Rails' AITrain).
    bool frontIsFront = false;
    if (Game::trackDB != nullptr) {
        const auto found = Game::trackDB->trackItems.find(item);
        frontIsFront = found != Game::trackDB->trackItems.end() && found->second != nullptr
                && found->second->platformTrItemData != nullptr && found->second->platformTrItemData[0] == 0xFFFF0000u;
    }
    if (side & 1)
        (frontIsFront ? right : left) = true;
    if (side & 2)
        (frontIsFront ? left : right) = true;
}

int SimulationController::platformSide(int item) const {
    const auto known = platformSides.constFind(item);
    if (known != platformSides.constEnd())
        return known.value();
    // Looked for in vain lately: not again within a second (its tile may
    // load meanwhile, the simulation running or not).
    static QElapsedTimer clock;
    if (!clock.isValid())
        clock.start();
    const qint64 wall = clock.elapsed();
    const auto missed = platformMisses.constFind(item);
    if (missed != platformMisses.constEnd() && wall - missed.value() < 1000)
        return -1;
    // Its world object, in a loaded tile only.
    if (Game::currentRoute != nullptr)
        for (Tile *tile : Game::currentRoute->tile) {
            if (tile == nullptr || tile->loaded != 1)
                continue;
            for (const auto &entry : tile->obiekty) {
                WorldObj *object = entry.second;
                if (object == nullptr || object->typeID != WorldObj::platform)
                    continue;
                PlatformObj *platform = static_cast<PlatformObj *>(object);
                const int begin = platform->getTrackBegItemId(), end = platform->getTrackEndItemId();
                if (begin != item && end != item)
                    continue;
                const int side = (platform->getSideLeft() ? 1 : 0) | (platform->getSideRight() ? 2 : 0);
                if (begin >= 0)
                    platformSides.insert(begin, side);
                if (end >= 0)
                    platformSides.insert(end, side);
                return side;
            }
        }
    platformMisses.insert(item, wall);
    return -1;
}

Consist *SimulationController::standingConsist(int standingTrain) const {
    placeTrains();
    if (standingTrain < 0 || size_t(standingTrain) >= standingCopies.size())
        return nullptr;
    return standingCopies[size_t(standingTrain)].get();
}

size_t SimulationController::standingCount() const {
    placeTrains();
    return standingCopies.size();
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
    consistVersions.clear();
    for (std::unique_ptr<Consist> &copy : standingCopies)
        if (copy != nullptr)
            refit(*copy, {});
    standingCopies.clear();
    standingPlaced = ~0ull;
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
