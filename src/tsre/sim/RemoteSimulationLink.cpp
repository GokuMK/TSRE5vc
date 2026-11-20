#include <tsre/sim/RemoteSimulationLink.h>

#include <QElapsedTimer>
#include <tsre/Game.h>
#include <tsre/fileFunctions/RouteFiles.h>
#include <tsre/sim/RunData.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/SimulationTexts.h>
#include <tsre/trains/ActivityNotifier.h>
#include <tsre/trains/Eng.h>
#include <tsre/world/Route.h>

namespace Sim {

namespace {

QString consoleRuns() {
    //% "The server's console runs the run."
    return qtTrId("simulation.remote.console");
}

}

RemoteSimulationLink::RemoteSimulationLink(Send send, Notice notice) : send(std::move(send)), notice(std::move(notice)) {
    retry.setInterval(250);
    connect(&retry, &QTimer::timeout, this, &RemoteSimulationLink::prepare);
}

RemoteSimulationLink::~RemoteSimulationLink() = default;

void RemoteSimulationLink::sendRecord(Wire::Kind kind, const QByteArray &body, bool request) {
    if (send)
        send(Wire::Record(kind, body), request);
}

RunMode RemoteSimulationLink::mode() const {
    const std::shared_ptr<const SimulationFrame> now = frame();
    return now != nullptr ? now->mode : RunMode::Paused;
}

double RemoteSimulationLink::speed() const {
    const std::shared_ptr<const SimulationFrame> now = frame();
    return now != nullptr ? now->speed : 1.0;
}

bool RemoteSimulationLink::start(Activity *, const RunOptions &, QString *error, const std::function<void()> &) {
    if (error != nullptr)
        *error = consoleRuns();
    return false;
}

bool RemoteSimulationLink::runTo(double, bool, int, QString *error) {
    if (error != nullptr)
        *error = consoleRuns();
    return false;
}

bool RemoteSimulationLink::reset(QString *error) {
    if (error != nullptr)
        *error = consoleRuns();
    return false;
}

void RemoteSimulationLink::stop(const std::function<void()> &stopped) {
    if (!exists())
        return;
    detached = true;
    // The server sends this view no more of the run, nor counts what it
    // heard (the review).
    events.clear();
    messages.clear();
    heard.clear();
    sendRecord(Wire::Kind::Ready, Wire::EncodeReady(run, false, QString("this view let the run go")), false);
    if (stopped)
        stopped();
    ActivityNotifier::instance()->setEditingLocked(false);
}

void RemoteSimulationLink::drive(int train) {
    if (exists())
        sendRecord(Wire::Kind::Drive, Wire::EncodeTrains(run, {train}), true);
}

void RemoteSimulationLink::leave() {
    if (exists())
        sendRecord(Wire::Kind::Leave, Wire::EncodeTrains(run, {}), true);
}

void RemoteSimulationLink::command(int vehicle, const Message &message) {
    if (exists())
        sendRecord(Wire::Kind::Command, Wire::EncodeCommand(run, vehicle, message), true);
}

void RemoteSimulationLink::hear(const std::vector<int> &trains) {
    if (!exists() || trains == heard)
        return;
    heard = trains;
    sendRecord(Wire::Kind::Hear, Wire::EncodeTrains(run, trains), false);
}

void RemoteSimulationLink::forgetRoute() {
    // Its activity is freed with the route (ActLib::Clear): a run with the
    // same digest later makes its input again (the review: the freed
    // activity was taken on).
    in.reset();
    act = nullptr;
    inputDigest.clear();
}

int RemoteSimulationLink::drivenHere(const SimulationFrame &frame) const {
    return frame.driven >= 0 && frame.driven == driving ? frame.driven : -1;
}

std::shared_ptr<const SimulationFrame> RemoteSimulationLink::frame() const {
    return exists() ? decoder.frame() : nullptr;
}

SimulationLink::Taken RemoteSimulationLink::take() {
    Taken taken;
    if (!exists())
        return taken;
    taken.frame = decoder.frame();
    taken.events.swap(events);
    return taken;
}

std::vector<TrainMessage> RemoteSimulationLink::takeMessages() {
    std::vector<TrainMessage> taken;
    taken.swap(messages);
    return taken;
}

void RemoteSimulationLink::received(Wire::Kind kind, const QByteArray &body) {
    received_ += body.size();
    switch (kind) {
    case Wire::Kind::Run: {
        QString error;
        Wire::Announcement announced;
        if (!Wire::DecodeAnnouncement(body, announced, &error)) {
            notice("The server's run cannot be read: " + error);
            return;
        }
        pending = std::move(announced);
        hasPending = true;
        prepare();
        return;
    }
    case Wire::Kind::Frame: {
        if (!ready)
            return;
        QString error;
        if (decoder.decode(body, SteadyNs(), &error) != nullptr) {
            ++frames;
            return;
        }
        // A frame that does not follow on from this view's (none should on
        // one connection): the server is asked for a whole one.
        if (error.contains("follow"))
            sendRecord(Wire::Kind::Ready, Wire::EncodeReady(run, true, QString()), false);
        return;
    }
    case Wire::Kind::Events: {
        std::vector<TrainEvent> fresh;
        if (ready && !detached && Wire::DecodeEvents(body, fresh))
            events.insert(events.end(), fresh.begin(), fresh.end());
        return;
    }
    case Wire::Kind::Messages: {
        std::vector<TrainMessage> fresh;
        if (ready && !detached && Wire::DecodeMessages(body, fresh))
            messages.insert(messages.end(), fresh.begin(), fresh.end());
        return;
    }
    case Wire::Kind::End: {
        unsigned int ended = 0;
        QString why;
        if (!Wire::DecodeEnd(body, ended, why))
            return;
        // A run announced while the route still loaded, ended before it
        // was taken on: not taken on (the review: a dead run was shown).
        if (hasPending && pending.runId == ended) {
            hasPending = false;
            retry.stop();
        }
        if (ended != run)
            return;
        hasPending = false;
        driving = -1;
        // The view lets the run go while this link still shows it (its stop
        // comes back here and needs the run).
        if (exists() && runEnded)
            runEnded(why);
        ready = false;
        decoder.reset();
        events.clear();
        messages.clear();
        heard.clear();
        return;
    }
    case Wire::Kind::Driving: {
        unsigned int about = 0;
        std::vector<int> trains;
        if (Wire::DecodeTrains(body, about, trains) && about == run)
            driving = trains.empty() ? -1 : trains[0];
        return;
    }
    case Wire::Kind::Taken: {
        unsigned long long seq = 0;
        if (Wire::DecodeTaken(body, seq))
            sequence = std::max(sequence, seq);
        return;
    }
    default:
        return;
    }
}

void RemoteSimulationLink::prepare() {
    if (!hasPending)
        return;
    // The route loaded first: the run's data is the route's.
    if (Game::currentRoute == nullptr || !Game::currentRoute->loaded || Game::trackDB == nullptr) {
        retry.start();
        return;
    }
    retry.stop();
    hasPending = false;
    const Wire::Announcement announced = pending;
    const bool taken = exists();
    // The same run made again (a reset, a run back to a time; a
    // reconnect): this view's input is still the run's.
    if (in != nullptr && act != nullptr && announced.digest == inputDigest) {
        // What it heard is heard again (a reconnect: the server's new
        // connection hears nothing; the review: a view still showing the run
        // did not send it again).
        const std::vector<int> hearing = taken ? heard : std::vector<int>();
        run = announced.runId;
        ready = true;
        detached = false;
        driving = -1;
        decoder.reset();
        decoder.setInput(in);
        events.clear();
        heard.clear();
        sendRecord(Wire::Kind::Ready, Wire::EncodeReady(run, true, QString()), false);
        if (!hearing.empty())
            hear(hearing);
        if (!taken && runBegun)
            runBegun();
        return;
    }
    // A run shown before this one: let go first.
    if (taken && runEnded)
        runEnded(QString("another run"));
    ready = false;
    // Its files where the route's are read from: the server's.
    const QString routeFolder = Game::root + "/ROUTES/" + Game::route;
    for (auto it = announced.data.files.constBegin(); it != announced.data.files.constEnd(); ++it)
        RouteFiles::putServed("ROUTES/" + Game::route + "/" + it.key(), it.value());
    Activity *activity = LoadRunData(announced.data, routeFolder);
    QString why;
    std::shared_ptr<const SimulationInput> made;
    if (activity == nullptr) {
        why = "its activity " + announced.data.activity + " does not load here";
    } else {
        QElapsedTimer timer;
        timer.start();
        QString error;
        made = MakeRunInput(activity, announced.options, &error);
        inputMs = timer.elapsed();
        if (made == nullptr)
            why = "no input here: " + error;
    }
    if (made != nullptr && made->digest() != announced.digest) {
        // The trains that differ, and the vehicles this client lacks.
        const std::vector<QByteArray> mine = made->trainDigests();
        QStringList trains, missing;
        for (size_t t = 0; t < std::max(mine.size(), announced.trainDigests.size()); ++t)
            if (t >= mine.size() || t >= announced.trainDigests.size() || mine[t] != announced.trainDigests[t]) {
                trains << (t < made->trains.size() ? TrainName(*made, int(t)) : QString::number(t));
                if (t < made->trains.size())
                    for (const Vehicle &v : made->trains[t].vehicles)
                        if ((v.file.isEmpty() || EngOfFile(v.file) == nullptr) && !missing.contains(v.file))
                            missing << (v.file.isEmpty() ? QString("(a vehicle not found)") : v.file);
            }
        why = trains.isEmpty()
                ? QString("its input differs here (not in its trains: its standing trains, events or work orders)")
                : QString("%1 of its trains differ here: %2").arg(trains.size()).arg(trains.mid(0, 5).join(", "))
                          + (missing.isEmpty() ? QString() : "; missing here: " + missing.mid(0, 5).join(", "));
        made.reset();
    }
    run = announced.runId;
    if (made == nullptr) {
        sendRecord(Wire::Kind::Ready, Wire::EncodeReady(run, false, why), false);
        notice("The server's run is not shown here: " + why);
        in.reset();
        act = nullptr;
        inputDigest.clear();
        return;
    }
    in = made;
    act = activity;
    inputDigest = announced.digest;
    options = announced.options;
    ready = true;
    detached = false;
    driving = -1;
    decoder.reset();
    decoder.setInput(in);
    events.clear();
    messages.clear();
    heard.clear();
    sendRecord(Wire::Kind::Ready, Wire::EncodeReady(run, true, QString()), false);
    if (runBegun)
        runBegun();
}

}
