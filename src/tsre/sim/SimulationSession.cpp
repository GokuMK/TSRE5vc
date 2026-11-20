#include <tsre/sim/SimulationSession.h>

#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <settings/SettingsAccess.h>
#include <tsre/Game.h>
#include <tsre/sim/SimulationHost.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/WorkPlanning.h>
#include <tsre/trains/Activity.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/VehiclePreload.h>
#include <tsre/world/Environment.h>
#include <tsre/world/Route.h>
#include <tsre/world/Trk.h>

namespace Sim {

RunOptions RunOptions::Current(bool driving) {
    RunOptions options;
    options.date = Game::timeOfDayDate;
    options.boardingHolds = Settings::variant("core.simulation.boardingHolds", SettingType::Bool).toBool();
    options.driving = driving;
    return options;
}

SimulationSession::SimulationSession() = default;

SimulationSession::~SimulationSession() {
    stop();
}

unsigned int SimulationSession::runId() const {
    return in != nullptr ? in->runId : 0;
}

std::shared_ptr<const SimulationInput> SimulationSession::makeInput(Activity *activity, QString *error) {
    QElapsedTimer timer;
    timer.start();
    std::shared_ptr<const SimulationInput> input = MakeRunInput(activity, runOptions, error);
    if (input != nullptr)
        inputMs = timer.elapsed();
    return input;
}

namespace {

// TSRE_SIM_INPUT_DUMP=<file>: the input's trains as text, to <file>.<pid>
// (task server 05: a server's and a client's inputs compared line by line
// when their digests differ). Off: nothing.
void DumpInput(const SimulationInput &input) {
    const QString path = qEnvironmentVariable("TSRE_SIM_INPUT_DUMP");
    if (path.isEmpty())
        return;
    QFile file(path + "." + QString::number(QCoreApplication::applicationPid()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream out(&file);
    out << input.digestText() << "\n";
    // Each train's vehicles' definitions: the files read, their resistance.
    for (const TrainPlan &t : input.trains)
        for (const Vehicle &v : t.vehicles)
            out << "definition " << t.id << " " << v.definition->path << " " << QString::number(v.mass(), 'g', 9) << " "
                << QString::number(v.davis(0), 'g', 9) << " " << QString::number(v.davis(1), 'g', 9) << " "
                << QString::number(v.davis(2), 'g', 9) << " " << QString::number(v.davisLeading(), 'g', 9) << "\n";
}

}

std::shared_ptr<const SimulationInput> MakeRunInput(Activity *activity, const RunOptions &runOptions, QString *error) {
    if (activity == nullptr || Game::trackDB == nullptr) {
        if (error != nullptr)
            *error =
                //% "Choose an activity first."
                qtTrId("simulation.error.no.activity");
        return nullptr;
    }
    // The timetable set's vehicles still being read ahead: finished first
    // (half a second at most for Prudnik's), else the run read them again
    // beside the workers (VehiclePreload).
    VehiclePreload::waitForDone(30000);
    Route *route = Game::currentRoute;
    // Task tracks 16: the superelevation computed again as a whole (a few
    // ms), so the run copies nothing an edit left behind.
    if (route != nullptr)
        route->computeTrackCant(true);
    const float limit = route != nullptr && route->trk != nullptr ? route->trk->speedLimit : 0.0f;
    const float restricted = route != nullptr && route->trk != nullptr ? route->trk->tempRestrictedSpeed : -1.0f;
    std::shared_ptr<SimulationInput> input = MakeInput(activity, Game::trackDB, limit, nullptr, nullptr, restricted, false);
    // The line's voltage, as Open Rails reads it: every track electrified.
    if (route != nullptr && route->trk != nullptr)
        input->lineVoltage = route->trk->maxLineVoltage;
    input->locoScripts = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/scripts/loco");
    // The outside air (task driving 06): the env file's own weather (a
    // derived one's presets say nothing of the place), else the climate at
    // the route's latitude. An MSTS activity's season moves the run's first
    // day into it; its snow keeps the climate's air at 0 °C or below.
    double latitude = 0.0, longitude = 0.0;
    if (route != nullptr && route->getStartLatitudeLongitude(latitude, longitude))
        input->air.latitude = latitude;
    QDate today = QDate::fromString(runOptions.date.trimmed(), "yyyy-MM-dd");
    if (!today.isValid())
        today = QDate(2026, 6, 21);
    const Activity::ActivityHeader *header = activity->header;
    input->air.date = SeasonDate(today, header != nullptr ? header->season : -1, input->air.latitude);
    input->air.snow = header != nullptr && header->weather == 1;
    input->air.rain = header != nullptr && header->weather == 2;
    if (route != nullptr && route->env != nullptr && route->env->file.weather.has_value()
            && !route->env->weather.keyframes.isEmpty()) {
        auto weather = std::make_shared<Weather>();
        weather->start = route->env->weather.start;
        weather->keyframes = route->env->weather.keyframes;
        input->air.weatherMidnight = weather->editorHours(input->air.date, 0.0);
        input->air.weather = std::move(weather);
    }
    qInfo().noquote() << QString("Simulation: the outside air from %1 at %2°, the first day %3%4: %5 °C at the start")
                                 .arg(input->air.weather != nullptr ? "the route's weather" : "the climate")
                                 .arg(input->air.latitude, 0, 'f', 1).arg(input->air.date.toString("yyyy-MM-dd"))
                                 .arg(input->air.snow ? ", snow" : "")
                                 .arg(input->air.at(input->startTime, 0.0), 0, 'f', 1);
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
    // Each stop's people (task humans 06): waiting, getting off; the stops
    // as the work orders left them.
    FillPassengers(*input, activity, Game::trackDB);
    input->boardingHolds = runOptions.boardingHolds;
    // Driving from the start (task driving 02): the player's train, at the
    // driving step.
    if (runOptions.driving) {
        for (const TrainPlan &plan : input->trains)
            if (plan.player && std::any_of(plan.vehicles.begin(), plan.vehicles.end(),
                                           [](const Vehicle &v) { return v.type() >= 4; })) {
                input->driven = plan.id;
                input->step = SimulationCore::DrivenStep;
                break;
            }
        if (input->driven < 0) {
            // As statements: lupdate takes no text after a ternary's colon.
            if (error != nullptr && activity->isTimetables())
                //% "Choose the player's train first: a train with an engine."
                *error = qtTrId("simulation.error.drive.timetable");
            else if (error != nullptr)
                //% "The activity has no player's train with an engine to drive."
                *error = qtTrId("simulation.error.drive.player");
            return nullptr;
        }
    }
    DumpInput(*input);
    return input;
}

void SimulationSession::sendStart(std::shared_ptr<const SimulationInput> input) {
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, input, seq] { h->start(input, seq); }, Qt::QueuedConnection);
    const double value = pace;
    const unsigned long long speedSeq = ++sequence;
    QMetaObject::invokeMethod(host, [h, value, speedSeq] { h->setSpeed(value, speedSeq); },
                              Qt::QueuedConnection);
}

bool SimulationSession::start(Activity *activity, const RunOptions &options, QString *error,
                              const std::function<void()> &started) {
    stop();
    const RunOptions before = runOptions;
    runOptions = options;
    std::shared_ptr<const SimulationInput> input = makeInput(activity, error);
    if (input == nullptr) {
        runOptions = before;
        return false;
    }
    current = activity;
    in = input;
    latest.reset();
    history.clear();
    asked = RunMode::Paused;
    runToSeq = 0;
    thread = new QThread();
    thread->setObjectName("Simulation");
    host = new SimulationHost();
    host->moveToThread(thread);
    thread->start();
    sendStart(input);
    if (started)
        started();
    ActivityNotifier::instance()->setEditingLocked(true);
    return true;
}

bool SimulationSession::restart(QString *error) {
    std::shared_ptr<const SimulationInput> input = makeInput(current, error);
    if (input == nullptr)
        return false;
    in = input;
    latest.reset();
    history.clear();
    runToSeq = 0;
    sendStart(input);
    return true;
}

void SimulationSession::play() {
    if (!exists())
        return;
    asked = RunMode::Running;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, seq] { h->play(seq); }, Qt::QueuedConnection);
}

void SimulationSession::pause() {
    if (!exists())
        return;
    asked = RunMode::Paused;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, seq] { h->pause(seq); }, Qt::QueuedConnection);
}

void SimulationSession::setSpeed(double value) {
    pace = std::max(0.0, value);
    if (!exists())
        return;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    const double speed = pace;
    QMetaObject::invokeMethod(host, [h, speed, seq] { h->setSpeed(speed, seq); }, Qt::QueuedConnection);
}

bool SimulationSession::runTo(double target, bool thenRun, int untilOnTrack, QString *error) {
    if (!exists())
        return false;
    // Back: from the start again (MSAE's way).
    const std::shared_ptr<const SimulationFrame> now = frame();
    const double time = now != nullptr ? now->time : latest != nullptr ? latest->time : 0.0;
    if (target < time && !restart(error))
        return false;
    asked = RunMode::RunningTo;
    const unsigned long long seq = ++sequence;
    runToSeq = seq;
    runToTarget = target;
    runToThenRun = thenRun;
    runToTrain = untilOnTrack;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, target, seq, thenRun, untilOnTrack] { h->runTo(target, seq, thenRun, untilOnTrack); },
                              Qt::QueuedConnection);
    return true;
}

bool SimulationSession::reset(QString *error) {
    if (!exists())
        return false;
    if (!restart(error))
        return false;
    if (asked != RunMode::Paused) {
        asked = RunMode::Running;
        const unsigned long long seq = ++sequence;
        SimulationHost *h = host;
        QMetaObject::invokeMethod(host, [h, seq] { h->play(seq); }, Qt::QueuedConnection);
    }
    return true;
}

void SimulationSession::stop(const std::function<void()> &stopped) {
    if (!exists())
        return;
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
    current = nullptr;
    in.reset();
    latest.reset();
    history.clear();
    asked = RunMode::Paused;
    runToSeq = 0;
    runOptions.driving = false;
    if (stopped)
        stopped();
    ActivityNotifier::instance()->setEditingLocked(false);
}

void SimulationSession::drive(int train) {
    if (!exists())
        return;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, train, seq] { h->drive(train, seq); }, Qt::QueuedConnection);
}

void SimulationSession::leave(int train) {
    if (!exists())
        return;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, train, seq] { h->leave(train, seq); }, Qt::QueuedConnection);
}

void SimulationSession::command(int train, int vehicle, const Message &message) {
    if (!exists())
        return;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, train, vehicle, message, seq] { h->command(train, vehicle, message, seq); },
                              Qt::QueuedConnection);
}

void SimulationSession::hear(const std::vector<int> &trains) {
    if (!exists())
        return;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, trains, seq] { h->hear(trains, seq); }, Qt::QueuedConnection);
}

void SimulationSession::setCrossings(const std::vector<SimulationCore::CrossingParameters> &list) {
    if (!exists() || list.empty())
        return;
    const unsigned long long seq = ++sequence;
    SimulationHost *h = host;
    QMetaObject::invokeMethod(host, [h, list, seq] { h->setCrossings(list, seq); }, Qt::QueuedConnection);
}

std::shared_ptr<const SimulationFrame> SimulationSession::frame() const {
    if (host == nullptr || in == nullptr)
        return nullptr;
    std::shared_ptr<const SimulationFrame> now = host->frame();
    // Not yet of the run started last: the one taken before.
    return now != nullptr && now->runId == in->runId ? now : latest;
}

std::shared_ptr<const SimulationFrame> SimulationSession::update() {
    if (!exists() || in == nullptr)
        return nullptr;
    std::vector<TrainEvent> fresh;
    size_t dropped = 0;
    std::shared_ptr<const SimulationFrame> taken = host->take(fresh, &dropped);
    // Not yet the run started last: its events are the old run's.
    if (taken == nullptr || taken->runId != in->runId)
        return latest;
    if (dropped > 0)
        qWarning() << "Simulation: events dropped" << dropped;
    const bool changed = taken != latest;
    latest = taken;
    history.insert(history.end(), fresh.begin(), fresh.end());
    if (!changed)
        return latest;
    // A run to a time the host had passed when it took the command (running
    // as fast as possible, a day can go by while the command waits): from
    // the start again to that time, as for a time behind.
    if (runToSeq != 0 && latest->acknowledged >= runToSeq) {
        // The host stops at the first whole step at or after the target.
        const bool passed = latest->acknowledged == runToSeq && latest->time > runToTarget + SimulationCore::MaxStep;
        runToSeq = 0;
        if (passed) {
            const std::shared_ptr<const SimulationFrame> old = latest;
            if (runTo(runToTarget, runToThenRun, runToTrain))
                return old;
        }
    }
    // Every command taken: the host's mode is the one (a run to a time ends
    // paused).
    if (latest->acknowledged >= sequence)
        asked = latest->mode;
    return latest;
}

std::vector<TrainMessage> SimulationSession::takeMessages() {
    return host != nullptr ? host->takeMessages() : std::vector<TrainMessage>();
}

}
