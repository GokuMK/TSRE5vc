/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "SimulationServer.h"

#include <QDateTime>
#include <QFileInfo>
#include <QTextStream>
#include <QTime>
#include <QWebSocket>
#include <algorithm>
#include <cmath>
#include <set>
#include <settings/SettingsAccess.h>
#include <tsre/ClientInfo.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/SimulationTexts.h>
#include <tsre/traffic/LevelCrossings.h>
#include <tsre/trains/ActLib.h>
#include <tsre/trains/Activity.h>
#include <tsre/world/Route.h>
#include <tsre/world/objects/LevelCrObj.h>

namespace {

QTextStream &out() {
    static QTextStream stream(stdout);
    return stream;
}

QString stamp() {
    return QDateTime::currentDateTime().toString("HH:mm:ss");
}

QString clock(double seconds) {
    const int s = (int(std::floor(seconds)) % 86400 + 86400) % 86400;
    return QTime::fromMSecsSinceStartOfDay(s * 1000).toString("HH:mm:ss");
}

const char *modeName(Sim::RunMode mode) {
    switch (mode) {
    case Sim::RunMode::Running: return "running";
    case Sim::RunMode::RunningTo: return "running to a time";
    default: return "paused";
    }
}

// A client whose socket holds more than this unsent skips frames.
constexpr qint64 MostUnsent = 256 * 1024;

}

SimulationServer::SimulationServer(Route *route, Send send, Refuse refuse, Name name, QObject *parent)
    : QObject(parent), route(route), send(std::move(send)), refuse(std::move(refuse)), name(std::move(name)) {
    const int frames = std::clamp(Settings::integer("core.network.serverSimulationFrames"), 1, 100);
    timer.setInterval(std::max(1, 1000 / frames));
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, &SimulationServer::tick);
    rates.start();
}

SimulationServer::~SimulationServer() {
    timer.stop();
    session.stop();
}

Activity *SimulationServer::activityNamed(const QString &file) const {
    if (route == nullptr)
        return nullptr;
    for (int id : route->timetableSetId) {
        Activity *set = ActLib::TimetableSets.value(id);
        if (set != nullptr && set->name.compare(file, Qt::CaseInsensitive) == 0)
            return set;
    }
    for (int id : route->activityId) {
        Activity *act = ActLib::Act.value(id);
        if (act != nullptr && (QFileInfo(act->pathid).fileName().compare(file, Qt::CaseInsensitive) == 0
                               || act->nameid.compare(file, Qt::CaseInsensitive) == 0))
            return act;
    }
    return nullptr;
}

bool SimulationServer::console(const QString &command, const QString &argument) {
    if (command == "run") {
        if (argument.isEmpty()) {
            out() << "run <activity or timetable set>, by its file (lclsrvce.act, PSK_2026.timetable_or)\n";
            out().flush();
            return true;
        }
        start(argument);
        return true;
    }
    if (command != "play" && command != "pause" && command != "speed" && command != "runto" && command != "reset"
            && command != "stop" && command != "trains")
        return false;
    if (!session.exists()) {
        out() << "No run: run <activity or timetable set> starts one.\n";
        out().flush();
        return true;
    }
    QString error;
    if (command == "play") {
        session.play();
        out() << "Playing at " << (session.speed() > 0.0 ? QString::number(session.speed()) + "x" : QString("as fast as possible"))
              << ".\n";
    } else if (command == "pause") {
        session.pause();
        out() << "Paused.\n";
    } else if (command == "speed") {
        bool ok = false;
        const double speed = argument.toDouble(&ok);
        if (!ok || speed < 0.0) {
            out() << "speed <simulated seconds a second>, 0 as fast as possible\n";
        } else {
            session.setSpeed(speed);
            out() << "Speed " << (speed > 0.0 ? QString::number(speed) + "x" : QString("as fast as possible")) << ".\n";
        }
    } else if (command == "runto") {
        QTime time = QTime::fromString(argument, "HH:mm:ss");
        if (!time.isValid())
            time = QTime::fromString(argument, "HH:mm");
        if (!time.isValid()) {
            out() << "runto <HH:MM[:SS]>\n";
        } else {
            double target = time.msecsSinceStartOfDay() / 1000;
            // Before the run's start: the next day's.
            if (target < session.input()->startTime)
                target += 86400.0;
            if (session.runTo(target, false, -1, &error))
                out() << "Running to " << clock(target) << ".\n";
            else
                out() << "Not run to " << clock(target) << ": " << error << "\n";
        }
    } else if (command == "reset") {
        if (session.reset(&error))
            out() << "Reset: back at the start.\n";
        else
            out() << "Not reset: " << error << "\n";
    } else if (command == "stop") {
        stop("stopped from the server's console");
        out() << "Stopped.\n";
    } else if (command == "trains") {
        const std::shared_ptr<const Sim::SimulationFrame> frame = session.frame();
        const std::shared_ptr<const Sim::SimulationInput> in = session.input();
        for (size_t t = 0; in != nullptr && t < in->trains.size(); ++t) {
            const QString state = frame != nullptr && t < frame->trains.size()
                    ? Sim::StateText(*in, frame->trains[t].state, frame->time, int(t)) : QString();
            out() << "  " << t << "  " << Sim::TrainName(*in, int(t)) << ": " << state << "\n";
        }
    }
    out().flush();
    return true;
}

void SimulationServer::status() const {
    if (!session.exists()) {
        out() << "Run: none\n";
        out().flush();
        return;
    }
    const std::shared_ptr<const Sim::SimulationFrame> frame = session.frame();
    const Activity *activity = session.activity();
    int running = 0;
    if (frame != nullptr)
        for (const Sim::TrainFrame &train : frame->trains)
            running += train.state.status == Sim::TrainStatus::Running ? 1 : 0;
    out() << "Run: " << (activity != nullptr ? QFileInfo(activity->pathid).fileName() : QString("?")) << " at "
          << (frame != nullptr ? clock(frame->time) : QString("-")) << ", " << modeName(session.mode()) << ", speed "
          << (session.speed() > 0.0 ? QString::number(session.speed()) + "x" : QString("as fast as possible")) << ", "
          << (session.input() != nullptr ? session.input()->trains.size() : 0) << " trains (" << running << " running)"
          << ", driven by " << (driver != nullptr ? name(driver) : QString("nobody")) << "\n";
    for (auto it = clients.constBegin(); it != clients.constEnd(); ++it)
        out() << "  " << name(it.key()) << ": " << (it->ready ? "watching" : "making its input") << ", "
              << QString::number(it->rate / 1024.0, 'f', 1) << " KB/s as sent, " << it->heard.size() << " trains heard\n";
    out().flush();
}

void SimulationServer::help() const {
    out() << "  run <file>    starts a run of an activity or timetable set, paused at its start\n"
          << "  play          plays the run (in real time unless sped up)\n"
          << "  pause         pauses it\n"
          << "  speed <x>     simulated seconds a second, 0 as fast as possible\n"
          << "  runto <time>  as fast as possible to a time of day (HH:MM[:SS]), paused there\n"
          << "  reset         back at the run's start\n"
          << "  stop          ends the run\n"
          << "  trains        the run's trains and what each is doing\n";
    out().flush();
}

bool SimulationServer::start(const QString &file) {
    Activity *activity = activityNamed(file);
    if (activity == nullptr) {
        out() << "No activity or timetable set " << file << " on " << Game::route << "\n";
        out().flush();
        return false;
    }
    if (session.exists())
        stop("a new run");
    QString error;
    // The server's options: its world date, its setting.
    if (!session.start(activity, Sim::RunOptions::Current(false), &error)) {
        out() << "No run of " << file << ": " << error << "\n";
        out().flush();
        return false;
    }
    data = Sim::GatherRunData(activity);
    driver = nullptr;
    heardSent.clear();
    for (Client &c : clients)
        c.heard.clear();
    crossingsSent = ~0ull;
    sendCrossings();
    announce();
    timer.start();
    out() << stamp() << " Run of " << file << ": " << session.input()->trains.size() << " trains, input made in "
          << session.inputMilliseconds() << " ms, its data " << data.files.size() << " files; paused at "
          << clock(session.input()->startTime) << "\n";
    out().flush();
    return true;
}

void SimulationServer::stop(const QString &why) {
    if (!session.exists())
        return;
    timer.stop();
    const unsigned int run = announcedRun;
    session.stop();
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        sendTo(it.key(), Sim::Wire::Kind::End, Sim::Wire::EncodeEnd(run, why));
        it->ready = false;
        it->run = 0;
        it->hasFrame = false;
        it->events = 0;
        it->heard.clear();
    }
    announcedRun = 0;
    announcement.clear();
    data = Sim::RunData();
    driver = nullptr;
    heardSent.clear();
    encoder.next(nullptr);
    sentTick = 0;
}

void SimulationServer::announce() {
    const std::shared_ptr<const Sim::SimulationInput> in = session.input();
    if (in == nullptr)
        return;
    Sim::Wire::Announcement run;
    run.runId = session.runId();
    run.options = session.options();
    run.digest = in->digest();
    run.trainDigests = in->trainDigests();
    run.data = data;
    announcement = Sim::Wire::EncodeAnnouncement(run);
    announcedRun = run.runId;
    // A new run: nothing driven, every client to make its input again.
    driver = nullptr;
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        it->ready = false;
        it->run = announcedRun;
        it->hasFrame = false;
        it->events = 0;
        sendTo(it.key(), Sim::Wire::Kind::Run, announcement);
    }
}

void SimulationServer::sendTo(ClientInfo *client, Sim::Wire::Kind kind, const QByteArray &body, quint32 request) {
    const qint64 sent = send(client, Sim::Wire::Record(kind, body), request);
    auto found = clients.find(client);
    if (found != clients.end()) {
        found->bytes += sent;
        found->bytesLately += sent;
    }
}

void SimulationServer::loggedIn(ClientInfo *client) {
    Client &c = clients[client];
    c = Client();
    if (session.exists() && !announcement.isEmpty()) {
        c.run = announcedRun;
        sendTo(client, Sim::Wire::Kind::Run, announcement);
    }
}

void SimulationServer::disconnected(ClientInfo *client) {
    if (!clients.contains(client))
        return;
    clients.remove(client);
    if (driver == client) {
        driver = nullptr;
        session.leave();
        out() << stamp() << " " << name(client) << " gone: its train given back to the AI\n";
        out().flush();
    }
    hearing();
}

void SimulationServer::hearing() {
    std::set<int> all;
    for (const Client &c : std::as_const(clients))
        if (c.ready)
            all.insert(c.heard.begin(), c.heard.end());
    const std::vector<int> trains(all.begin(), all.end());
    if (trains == heardSent || !session.exists())
        return;
    heardSent = trains;
    session.hear(trains);
}

void SimulationServer::sendCrossings() {
    crossingsSent = LevelCrossings::version();
    if (!session.exists())
        return;
    std::vector<Sim::SimulationCore::CrossingParameters> list;
    QVector<int> items;
    for (LevelCrObj *object : LevelCrossings::objects()) {
        if (!object->loaded)
            continue;
        items.clear();
        object->getTrackItemIds(items, 0);
        for (int item : items) {
            Sim::SimulationCore::CrossingParameters p;
            p.item = item;
            p.warning = object->getSensitivityActivateLevel();
            p.minimum = object->getSensitivityMinimunDistance();
            list.push_back(p);
        }
    }
    session.setCrossings(list);
}

void SimulationServer::received(ClientInfo *client, Sim::Wire::Kind kind, const QByteArray &body, quint32 request) {
    if (!clients.contains(client))
        clients[client] = Client();
    Client &c = clients[client];
    using Sim::Wire::Kind;
    switch (kind) {
    case Kind::Ready: {
        unsigned int run = 0;
        bool same = false;
        QString why;
        if (!Sim::Wire::DecodeReady(body, run, same, why) || run != announcedRun)
            return;
        c.ready = same;
        c.hasFrame = false;
        c.events = 0;
        if (!same) {
            out() << stamp() << " " << name(client) << " cannot show the run: " << why << "\n";
            out().flush();
        }
        hearing();
        return;
    }
    case Kind::Hear: {
        unsigned int run = 0;
        std::vector<int> trains;
        if (!Sim::Wire::DecodeTrains(body, run, trains) || run != announcedRun)
            return;
        std::sort(trains.begin(), trains.end());
        trains.erase(std::unique(trains.begin(), trains.end()), trains.end());
        c.heard = trains;
        hearing();
        return;
    }
    case Kind::Drive: {
        unsigned int run = 0;
        std::vector<int> trains;
        if (!Sim::Wire::DecodeTrains(body, run, trains) || trains.size() != 1 || run != announcedRun || !session.exists()) {
            refuse(client, "drive", "no such run");
            return;
        }
        if (driver != nullptr && driver != client) {
            refuse(client, "drive", "driven by " + name(driver));
            return;
        }
        const std::shared_ptr<const Sim::SimulationInput> in = session.input();
        const int train = trains[0];
        if (in == nullptr || train < 0 || size_t(train) >= in->trains.size()) {
            refuse(client, "drive", "no such train");
            return;
        }
        driver = client;
        session.drive(train);
        out() << stamp() << " " << name(client) << " drives " << Sim::TrainName(*in, train) << "\n";
        out().flush();
        sendTo(client, Kind::Taken, Sim::Wire::EncodeTaken(session.lastCommand()), request);
        return;
    }
    case Kind::Leave: {
        if (driver != client) {
            refuse(client, "leave", "not driving");
            return;
        }
        driver = nullptr;
        session.leave();
        out() << stamp() << " " << name(client) << " gave its train back to the AI\n";
        out().flush();
        sendTo(client, Kind::Taken, Sim::Wire::EncodeTaken(session.lastCommand()), request);
        return;
    }
    case Kind::Command: {
        unsigned int run = 0;
        int vehicle = -1;
        Sim::Message message;
        if (!Sim::Wire::DecodeCommand(body, run, vehicle, message) || run != announcedRun) {
            refuse(client, "command", "no such run");
            return;
        }
        if (driver != client) {
            refuse(client, "command " + message.name, "not driving");
            return;
        }
        session.command(vehicle, message);
        sendTo(client, Kind::Taken, Sim::Wire::EncodeTaken(session.lastCommand()), request);
        return;
    }
    default:
        refuse(client, "simulation record", "not one a client sends");
        return;
    }
}

void SimulationServer::tick() {
    if (!session.exists())
        return;
    // Crossing objects loaded or edited since: their values to the run.
    if (LevelCrossings::version() != crossingsSent)
        sendCrossings();
    session.update();
    // A new run (a reset, a run back to a time): announced again.
    if (session.runId() != announcedRun) {
        encoder.next(nullptr);
        sentTick = 0;
        announce();
        hearing();
    }
    // The newest frame: to every client that made the same input and lacks
    // it (a paused run's too, for a client that has just joined or skipped
    // frames); the changes when it has the frame before, else whole.
    const std::shared_ptr<const Sim::SimulationFrame> frame = session.frame();
    if (frame != nullptr && frame->runId == announcedRun) {
        const bool fresh = frame->tick != sentTick;
        const unsigned long long before = sentTick;
        if (fresh) {
            encoder.next(frame);
            sentTick = frame->tick;
        }
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            Client &c = it.value();
            if (!c.ready || c.run != announcedRun || (c.hasFrame && c.frameTick == frame->tick))
                continue;
            ClientInfo *client = it.key();
            if (client->socket != nullptr && client->socket->bytesToWrite() > MostUnsent) {
                c.hasFrame = false;
                continue;
            }
            const bool follows = fresh && c.hasFrame && c.frameTick == before && before != 0;
            sendTo(client, Sim::Wire::Kind::Frame, follows ? encoder.changes() : encoder.whole());
            c.hasFrame = true;
            c.frameTick = frame->tick;
        }
    }
    // The events each client lacks.
    const std::vector<Sim::TrainEvent> &events = session.events();
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        Client &c = it.value();
        if (!c.ready || c.events >= events.size())
            continue;
        const std::vector<Sim::TrainEvent> fresh(events.begin() + std::ptrdiff_t(c.events), events.end());
        sendTo(it.key(), Sim::Wire::Kind::Events, Sim::Wire::EncodeEvents(fresh));
        c.events = events.size();
    }
    // The messages, to the clients that hear their train or drive it.
    const std::vector<Sim::TrainMessage> messages = session.takeMessages();
    if (!messages.empty()) {
        const int driven = frame != nullptr ? frame->driven : -1;
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            const Client &c = it.value();
            if (!c.ready)
                continue;
            std::vector<Sim::TrainMessage> theirs;
            for (const Sim::TrainMessage &m : messages)
                if (std::binary_search(c.heard.begin(), c.heard.end(), m.train)
                        || (it.key() == driver && m.train == driven))
                    theirs.push_back(m);
            if (!theirs.empty())
                sendTo(it.key(), Sim::Wire::Kind::Messages, Sim::Wire::EncodeMessages(theirs));
        }
    }
    // Each client's bytes a second, lately.
    if (rates.elapsed() >= 1000) {
        const double seconds = rates.restart() / 1000.0;
        for (Client &c : clients) {
            c.rate = c.bytesLately / seconds;
            c.bytesLately = 0;
        }
    }
}
