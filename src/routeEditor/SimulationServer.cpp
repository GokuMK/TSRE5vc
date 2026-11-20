/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "SimulationServer.h"

#include <QCoreApplication>
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
#include <tsre/fileFunctions/NetworkMessage.h>
#include <tsre/sim/DrivingKeys.h>
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
    // The driven trains' values go apart, to the clients that need them
    // (task server 06).
    encoder.setDrivenValues(false);
    // The run's thread ends before the application (the review: the server
    // is not destroyed at a quit, and the thread went on stepping).
    if (QCoreApplication::instance() != nullptr)
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
            timer.stop();
            session.stop();
        });
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
            const auto crew = crews.find(int(t));
            QStringList players;
            if (crew != crews.end())
                for (ClientInfo *player : crew->second.players)
                    players << name(player);
            out() << "  " << t << "  " << Sim::TrainName(*in, int(t)) << ": " << state
                  << (players.isEmpty() ? QString() : ", driven by " + players.join(", ")) << "\n";
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
          << "\n";
    // The crews (task server 06).
    for (const Sim::Crew &crew : crewList())
        out() << "  " << (session.input() != nullptr ? Sim::TrainName(*session.input(), crew.train) : QString::number(crew.train))
              << " driven by " << crew.players.join(", ") << (crew.shared ? " (shared)" : "") << "\n";
    if (crews.empty())
        out() << "  nothing driven\n";
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
    clearCrews();
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
    clearCrews();
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
    clearCrews();
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        it->ready = false;
        it->run = announcedRun;
        it->hasFrame = false;
        it->events = 0;
        sendTo(it.key(), Sim::Wire::Kind::Run, announcement);
    }
}

void SimulationServer::sendTo(ClientInfo *client, Sim::Wire::Kind kind, const QByteArray &body, quint32 request) {
    sendFramed(client, NetworkMessage::frame(Sim::Wire::Record(kind, body), request, 0, CompressAbove));
}

void SimulationServer::sendFramed(ClientInfo *client, const QByteArray &message) {
    const qint64 sent = send(client, message);
    auto found = clients.find(client);
    if (found != clients.end()) {
        found->bytes += sent;
        found->bytesLately += sent;
    }
}

int SimulationServer::trainOf(ClientInfo *client) const {
    for (const auto &[train, crew] : crews)
        if (std::find(crew.players.begin(), crew.players.end(), client) != crew.players.end())
            return train;
    return -1;
}

std::vector<Sim::Crew> SimulationServer::crewList() const {
    std::vector<Sim::Crew> list;
    for (const auto &[train, crew] : crews) {
        Sim::Crew c;
        c.train = train;
        c.shared = crew.shared;
        for (ClientInfo *player : crew.players)
            c.players << player->username;
        list.push_back(std::move(c));
    }
    return list;
}

void SimulationServer::sendCrews(ClientInfo *only) {
    const QByteArray body = Sim::Wire::EncodeCrews(announcedRun, crewList());
    for (auto it = clients.begin(); it != clients.end(); ++it)
        if (it->ready && (only == nullptr || it.key() == only))
            sendTo(it.key(), Sim::Wire::Kind::Crews, body);
}

namespace {

// The commands a key sends while held (a lever moved while held, a push
// button: DrivingKeys' defaults), by name: the value its release sends.
const std::map<QString, Sim::MessageValue> &Releases() {
    static const std::map<QString, Sim::MessageValue> releases = [] {
        std::map<QString, Sim::MessageValue> made;
        const Sim::DrivingKeys keys;
        for (const Sim::DrivingKeys::Binding &binding : keys.bindings())
            for (const Sim::DrivingKeys::Command &command : binding.commands)
                if (command.release.has_value())
                    made.emplace(command.name, *command.release);
        return made;
    }();
    return releases;
}

}

void SimulationServer::leaveCrew(ClientInfo *client, bool gone) {
    const int train = trainOf(client);
    if (train < 0)
        return;
    CrewState &crew = crews[train];
    crew.players.erase(std::remove(crew.players.begin(), crew.players.end(), client), crew.players.end());
    std::map<std::pair<int, QString>, Sim::MessageValue> held;
    const auto heldBy = crew.held.find(client);
    if (heldBy != crew.held.end()) {
        held = std::move(heldBy->second);
        crew.held.erase(heldBy);
    }
    const QString trainName = session.input() != nullptr ? Sim::TrainName(*session.input(), train) : QString::number(train);
    // Its keys still held let go, the train driven on by others.
    if (!crew.players.empty())
        for (const auto &[key, pressed] : held) {
            Q_UNUSED(pressed);
            session.command(train, key.first, Sim::Message{key.second, Releases().at(key.second)});
        }
    if (crew.players.empty()) {
        crews.erase(train);
        session.leave(train);
        out() << stamp() << " " << name(client)
              << (gone ? " gone: its train given back to the AI" : " gave its train back to the AI") << "\n";
    } else {
        out() << stamp() << " " << name(client) << (gone ? " gone: " : " ") << "left the cab of " << trainName << " ("
              << name(crew.players.front()) << " drives on)\n";
    }
    out().flush();
    if (!gone)
        sendTo(client, Sim::Wire::Kind::Driving, Sim::Wire::EncodeTrains(announcedRun, {}));
    sendCrews();
}

void SimulationServer::clearCrews() {
    crews.clear();
    drivenSent.clear();
    for (Client &c : clients)
        c.drivenTicks.clear();
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
    leaveCrew(client, true);
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
        // Asked again (a frame that did not follow on): a whole frame; the
        // events it has are not sent again (the review: they came twice).
        if (!(c.ready && same))
            c.events = 0;
        c.ready = same;
        c.hasFrame = false;
        c.drivenTicks.clear();
        if (same)
            sendCrews(client);
        else
            // The run no longer shown (another input, the view letting it
            // go): its cab left (task server 06's review).
            leaveCrew(client, false);
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
        // A driven train newly heard: the frame sent again with its values
        // (task server 06's review: while paused none came until a new
        // frame).
        for (int t : trains)
            if (crews.count(t) != 0 && c.drivenTicks.count(t) == 0) {
                c.hasFrame = false;
                break;
            }
        hearing();
        return;
    }
    case Kind::Drive: {
        unsigned int run = 0;
        std::vector<int> trains;
        // The run announced still the session's (task server 06's review: a
        // Drive between a reset and its announcement drove the new run's
        // train for nobody).
        if (!Sim::Wire::DecodeTrains(body, run, trains) || trains.size() != 1 || run != announcedRun || !session.exists()
                || session.runId() != announcedRun) {
            refuse(client, "drive", "no such run");
            return;
        }
        if (!c.ready) {
            refuse(client, "drive", "the run not shown (its input not the server's)");
            return;
        }
        const std::shared_ptr<const Sim::SimulationInput> in = session.input();
        const int train = trains[0];
        if (in == nullptr || train < 0 || size_t(train) >= in->trains.size()) {
            refuse(client, "drive", "no such train");
            return;
        }
        // As the view checks it (the review: a train the core does not take
        // made its sender the driver all the same).
        if (!Sim::Drivable(*in, train)) {
            refuse(client, "drive", "no engine drives " + Sim::TrainName(*in, train));
            return;
        }
        // One train a player (task server 06); a crew's cab joined when it
        // is shared.
        const int own = trainOf(client);
        if (own == train) {
            sendTo(client, Kind::Driving, Sim::Wire::EncodeTrains(announcedRun, {train}));
            sendTo(client, Kind::Taken, Sim::Wire::EncodeTaken(session.lastCommand()), request);
            return;
        }
        const auto driven = crews.find(train);
        if (driven != crews.end() && !driven->second.shared) {
            QStringList players;
            for (ClientInfo *player : driven->second.players)
                players << name(player);
            refuse(client, "drive", "its cab is not shared (driven by " + players.join(", ") + ")");
            return;
        }
        if (own >= 0)
            leaveCrew(client, false);
        CrewState &crew = crews[train];
        if (crew.players.empty()) {
            session.drive(train);
            out() << stamp() << " " << name(client) << " drives " << Sim::TrainName(*in, train) << "\n";
        } else {
            out() << stamp() << " " << name(client) << " joins the cab of " << Sim::TrainName(*in, train) << " (driven by "
                  << name(crew.players.front()) << ")\n";
        }
        crew.players.push_back(client);
        out().flush();
        // Only its crew is told: every client's frame has the train driven.
        sendTo(client, Kind::Driving, Sim::Wire::EncodeTrains(announcedRun, {train}));
        sendTo(client, Kind::Taken, Sim::Wire::EncodeTaken(session.lastCommand()), request);
        sendCrews();
        // The frame sent again with the train's values (task server 06's
        // review: a cab joined while paused had none until a new frame).
        clients[client].hasFrame = false;
        return;
    }
    case Kind::Leave: {
        unsigned int run = 0;
        std::vector<int> trains;
        const int own = trainOf(client);
        if (!Sim::Wire::DecodeTrains(body, run, trains) || run != announcedRun || session.runId() != announcedRun
                || own < 0 || (!trains.empty() && trains.front() != own)) {
            refuse(client, "leave", "not driving");
            return;
        }
        leaveCrew(client, false);
        sendTo(client, Kind::Taken, Sim::Wire::EncodeTaken(session.lastCommand()), request);
        return;
    }
    case Kind::Share: {
        unsigned int run = 0;
        int train = -1;
        bool shared = false;
        const auto crew = Sim::Wire::DecodeShare(body, run, train, shared) && run == announcedRun
                        && session.runId() == announcedRun
                ? crews.find(train) : crews.end();
        if (crew == crews.end() || crew->second.players.front() != client) {
            refuse(client, "share", "not its first player");
            return;
        }
        crew->second.shared = shared;
        out() << stamp() << " " << name(client) << (shared ? " shares" : " no longer shares") << " the cab of "
              << Sim::TrainName(*session.input(), train) << "\n";
        out().flush();
        sendTo(client, Kind::Taken, Sim::Wire::EncodeTaken(session.lastCommand()), request);
        sendCrews();
        return;
    }
    case Kind::Command: {
        unsigned int run = 0;
        int train = -1, vehicle = -1;
        Sim::Message message;
        if (!Sim::Wire::DecodeCommand(body, run, train, vehicle, message) || run != announcedRun || !session.exists()
                || session.runId() != announcedRun) {
            refuse(client, "command", "no such run");
            return;
        }
        if (train < 0 || trainOf(client) != train || !c.ready) {
            refuse(client, "command " + message.name, "not in its cab");
            return;
        }
        session.command(train, vehicle, message);
        // A key held, remembered until its release (let go for it if it
        // leaves while others drive on).
        const auto release = Releases().find(message.name);
        if (release != Releases().end()) {
            std::map<std::pair<int, QString>, Sim::MessageValue> &held = crews[train].held[client];
            if (message.value == release->second)
                held.erase({vehicle, message.name});
            else
                held[{vehicle, message.name}] = message.value;
        }
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
        // Each made and compressed once, for every client that gets it (the
        // review: compressed again for each).
        QByteArray wholeMessage, changesMessage;
        // Each driven train's values (task server 06): to its crew and to
        // the clients that hear it, before their frame; whole, or by the
        // changes since the values sent with the frame before.
        struct Values {
            const Sim::DrivenFrame *now = nullptr;
            const std::pair<unsigned long long, std::shared_ptr<const Sim::DrivenFrame>> *before = nullptr;
            QByteArray whole, changes;
        };
        std::map<int, Values> values;
        for (const std::shared_ptr<const Sim::DrivenFrame> &d : frame->drivenValues) {
            Values &v = values[d->train];
            v.now = d.get();
            const auto sent = drivenSent.find(d->train);
            if (sent != drivenSent.end() && sent->second.first != frame->tick)
                v.before = &sent->second;
        }
        const auto valuesFor = [&](ClientInfo *client, Client &c) {
            for (auto &[train, v] : values) {
                if (trainOf(client) != train && !std::binary_search(c.heard.begin(), c.heard.end(), train))
                    continue;
                const auto got = c.drivenTicks.find(train);
                const bool follows = v.before != nullptr && got != c.drivenTicks.end() && got->second == v.before->first;
                QByteArray &message = follows ? v.changes : v.whole;
                if (message.isEmpty())
                    message = NetworkMessage::frame(
                            Sim::Wire::Record(Sim::Wire::Kind::DrivenValues,
                                              Sim::Wire::EncodeDriven(announcedRun, frame->tick, *v.now,
                                                                      follows ? v.before->second.get() : nullptr,
                                                                      follows ? v.before->first : 0, *frame)),
                            0, 0, CompressAbove);
                sendFramed(client, message);
                c.drivenTicks[train] = frame->tick;
            }
        };
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            Client &c = it.value();
            if (!c.ready || c.run != announcedRun || (c.hasFrame && c.frameTick == frame->tick))
                continue;
            ClientInfo *client = it.key();
            if (client->socket != nullptr && client->socket->bytesToWrite() > MostUnsent) {
                c.hasFrame = false;
                c.drivenTicks.clear();
                continue;
            }
            valuesFor(client, c);
            const bool follows = fresh && c.hasFrame && c.frameTick == before && before != 0;
            QByteArray &message = follows ? changesMessage : wholeMessage;
            if (message.isEmpty())
                message = NetworkMessage::frame(Sim::Wire::Record(Sim::Wire::Kind::Frame,
                                                                  follows ? encoder.changes() : encoder.whole()),
                                                0, 0, CompressAbove);
            sendFramed(client, message);
            c.hasFrame = true;
            c.frameTick = frame->tick;
        }
        if (fresh) {
            drivenSent.clear();
            for (const std::shared_ptr<const Sim::DrivenFrame> &d : frame->drivenValues)
                drivenSent[d->train] = {frame->tick, d};
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
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            const Client &c = it.value();
            if (!c.ready)
                continue;
            std::vector<Sim::TrainMessage> theirs;
            for (const Sim::TrainMessage &m : messages)
                if (std::binary_search(c.heard.begin(), c.heard.end(), m.train)
                        || trainOf(it.key()) == m.train)
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
