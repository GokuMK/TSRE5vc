#pragma once
#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>
#include <vector>
#include <tsre/sim/SimulationLink.h>
#include <tsre/sim/SimulationWire.h>

namespace Sim {

// A view's link to a run on its server (task server 05). The server
// announces a run with its data; this link puts the data in the route files
// the server sent, loads it with the editor's loaders, makes its own input
// with the run's options and compares its digest with the server's: the
// same, the view takes the run on; else the run is not shown here, the
// trains that differ named. Frames, events and messages arrive as records;
// hearing, Drive, Leave and the driven train's commands go back. The run is
// controlled from the server's console only.
class RemoteSimulationLink : public QObject, public SimulationLink {
    Q_OBJECT
public:
    // A record to the server (`request`: numbered, its reply comes back as
    // a record); a notice for the user.
    using Send = std::function<void(const QByteArray &payload, bool request)>;
    using Notice = std::function<void(const QString &text)>;
    RemoteSimulationLink(Send send, Notice notice);
    ~RemoteSimulationLink() override;

    // A simulation record from the server.
    void received(Wire::Kind kind, const QByteArray &body);

    bool exists() const override { return ready && !detached; }
    unsigned int runId() const override { return exists() ? run : 0; }
    RunMode mode() const override;
    double speed() const override;
    unsigned long long lastCommand() const override { return sequence; }
    qint64 inputMilliseconds() const override { return inputMs; }
    std::shared_ptr<const SimulationInput> input() const override { return exists() ? in : nullptr; }
    Activity *activity() const override { return exists() ? act : nullptr; }
    bool controlsRun() const override { return false; }
    int drivenHere(const SimulationFrame &frame) const override;
    void forgetRoute() override;
    std::vector<Crew> crews() const override { return exists() ? crewList : std::vector<Crew>(); }
    void share(int train, bool shared) override;

    bool start(Activity *activity, const RunOptions &options, QString *error,
               const std::function<void()> &started) override;
    void play() override {}
    void pause() override {}
    void setSpeed(double) override {}
    bool runTo(double target, bool thenRun, int untilOnTrack, QString *error) override;
    bool reset(QString *error) override;
    // The view lets the run go (the run on the server goes on).
    void stop(const std::function<void()> &stopped) override;
    void drive(int train) override;
    void leave(int train) override;
    void command(int train, int vehicle, const Message &message) override;
    void hear(const std::vector<int> &trains) override;
    // The server's own crossings are the run's.
    void setCrossings(const std::vector<SimulationCore::CrossingParameters> &) override {}

    std::shared_ptr<const SimulationFrame> frame() const override;
    Taken take() override;
    std::vector<TrainMessage> takeMessages() override;

    // The run's options, as the server made its input with them.
    const RunOptions &runOptions() const { return options; }
    // What the run's messages and frames cost (the measurements): bytes
    // received and frames decoded.
    qint64 bytesReceived() const { return received_; }
    int framesDecoded() const { return frames; }
    // The bytes of the run's messages as they came (compressed).
    void countWire(qint64 bytes) { wire += bytes; }
    qint64 wireBytes() const { return wire; }

private:
    Send send;
    Notice notice;
    // The run announced, until its input is made (the route loaded first).
    Wire::Announcement pending;
    bool hasPending = false;
    QTimer retry;
    // The run shown: its id, its input made here (and its digest), its
    // activity; a run the view let go is not taken on again until the
    // server announces one.
    unsigned int run = 0;
    bool ready = false;
    bool detached = false;
    std::shared_ptr<const SimulationInput> in;
    QByteArray inputDigest;
    RunOptions options;
    Activity *act = nullptr;
    qint64 inputMs = 0;
    Wire::FrameDecoder decoder;
    std::vector<TrainEvent> events;
    std::vector<TrainMessage> messages;
    unsigned long long sequence = 0;
    // The train the server says this client drives (Wire::Kind::Driving).
    int driving = -1;
    // The run's crews as the server last told them.
    std::vector<Crew> crewList;
    std::vector<int> heard;
    qint64 received_ = 0;
    qint64 wire = 0;
    int frames = 0;

    // The announced run's input made and checked (once the route has
    // loaded); the server told.
    void prepare();
    void sendRecord(Wire::Kind kind, const QByteArray &body, bool request);
};

}
