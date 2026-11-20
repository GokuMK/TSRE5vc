/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SIMULATIONSERVER_H
#define SIMULATIONSERVER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <map>
#include <functional>
#include <vector>
#include <tsre/sim/SimulationSession.h>
#include <tsre/sim/SimulationWire.h>

class Activity;
class ClientInfo;
class Route;

// A run on the server (task server 05): started and controlled from the
// server's console; its frames (the same for every client, 30 a second by
// default), events and messages to every logged-in client that made the
// same input (its digest); each client's heard trains, all of them heard;
// one client at a time driving a train.
class SimulationServer : public QObject {
    Q_OBJECT
public:
    // A record to a client, as the reply to `request` (0 none): the bytes
    // sent (compressed), 0 when not.
    // A message (a frame of the protocol, NetworkMessage::frame) to a client:
    // the bytes sent.
    using Send = std::function<qint64(ClientInfo *client, const QByteArray &message)>;
    // A refusal to a client's request, why.
    using Refuse = std::function<void(ClientInfo *client, const QString &what, const QString &why)>;
    using Name = std::function<QString(ClientInfo *client)>;

    SimulationServer(Route *route, Send send, Refuse refuse, Name name, QObject *parent = nullptr);
    ~SimulationServer() override;

    // A console command of the run's (run, play, pause, speed, runto, reset,
    // stop, trains); false for another command.
    bool console(const QString &command, const QString &argument);
    void status() const;
    void help() const;

    // A client logged in (it gets the run's announcement), gone.
    void loggedIn(ClientInfo *client);
    void disconnected(ClientInfo *client);
    // A client's record (permission checked).
    void received(ClientInfo *client, Sim::Wire::Kind kind, const QByteArray &body, quint32 request);

    bool runs() const { return session.exists(); }
    Sim::SimulationSession &simulation() { return session; }

private:
    struct Client {
        // The run it made its input for and found the same.
        unsigned int run = 0;
        bool ready = false;
        // The last frame it got (its tick), none after a skipped one.
        bool hasFrame = false;
        unsigned long long frameTick = 0;
        // The events it has.
        size_t events = 0;
        // The trains it hears.
        std::vector<int> heard;
        // Each driven train's values it got last: their frame's tick (task
        // server 06).
        std::map<int, unsigned long long> drivenTicks;
        // Bytes sent it (as sent: compressed), and lately a second.
        qint64 bytes = 0;
        qint64 bytesLately = 0;
        double rate = 0.0;
    };
    Route *route = nullptr;
    Send send;
    Refuse refuse;
    Name name;
    Sim::SimulationSession session;
    QTimer timer;
    QHash<ClientInfo *, Client> clients;
    // The run as announced: its id and the announcement's record.
    unsigned int announcedRun = 0;
    QByteArray announcement;
    Sim::RunData data;
    Sim::Wire::FrameEncoder encoder;
    unsigned long long sentTick = 0;
    // A driven train's crew (task server 06): its players in the order they
    // came (the first shares its cab or not), and whether others may join.
    struct CrewState {
        std::vector<ClientInfo *> players;
        bool shared = false;
        // Each player's keys held (a lever moved while held, a push button,
        // DrivingKeys): by vehicle and command, the value its press sent.
        // Let go for a player that leaves while others drive on (task server
        // 06's review: a disconnect sends no release, so the lever kept
        // moving for the rest of the crew).
        std::map<ClientInfo *, std::map<std::pair<int, QString>, Sim::MessageValue>> held;
    };
    std::map<int, CrewState> crews;
    // Each driven train's values sent last: their frame's tick (changes go
    // against them).
    std::map<int, std::pair<unsigned long long, std::shared_ptr<const Sim::DrivenFrame>>> drivenSent;
    // The train a client's crew drives, -1 none.
    int trainOf(ClientInfo *client) const;
    // A client out of its crew; the last gives the train back to the AI.
    // `gone`: it disconnected (nothing sent it).
    void leaveCrew(ClientInfo *client, bool gone);
    std::vector<Sim::Crew> crewList() const;
    // The crews to every client that shows the run (or one).
    void sendCrews(ClientInfo *only = nullptr);
    // Nothing driven: a new run, a stop.
    void clearCrews();
    std::vector<int> heardSent;
    unsigned long long crossingsSent = ~0ull;
    QElapsedTimer rates;

    void tick();
    bool start(const QString &name);
    void stop(const QString &why);
    // The run's announcement made again (a new run: started, reset, run
    // back to a time) and sent to every client.
    void announce();
    void sendTo(ClientInfo *client, Sim::Wire::Kind kind, const QByteArray &body, quint32 request = 0);
    void sendFramed(ClientInfo *client, const QByteArray &message);
    // The run's records compressed whatever their size, above this.
    static constexpr int CompressAbove = 64;
    void sendCrossings();
    void hearing();
    Activity *activityNamed(const QString &file) const;
};

#endif // SIMULATIONSERVER_H
