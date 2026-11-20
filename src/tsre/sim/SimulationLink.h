#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationSession.h>

class Activity;

namespace Sim {

// A view's side of a simulation session (task server 04): the commands a
// view sends, and what it takes of the run. The view (SimulationController)
// holds one; LocalSimulationLink reaches a session in the same process,
// the remote link (task server 05) one in the server's.
class SimulationLink {
public:
    virtual ~SimulationLink() = default;

    // The run, as the session has it: whether one exists, its id (the
    // frames'; 0 none), the mode asked and the speed, the last command's
    // sequence number, how long its input took to make (ms).
    virtual bool exists() const = 0;
    virtual unsigned int runId() const = 0;
    virtual RunMode mode() const = 0;
    virtual double speed() const = 0;
    virtual unsigned long long lastCommand() const = 0;
    virtual qint64 inputMilliseconds() const = 0;
    // The run's input as this view has it: here the session's own; a view
    // elsewhere makes its own from the same files with the run's options,
    // checked by its digest (task server 05).
    virtual std::shared_ptr<const SimulationInput> input() const = 0;
    // The run's activity as this view has it, null none.
    virtual Activity *activity() const = 0;
    // Whether this view controls the run (start, play, pause, speed, run
    // to, reset, stop): a local run, not a server's (task server 05: its
    // console runs it).
    virtual bool controlsRun() const { return true; }
    // The train this view drives in a frame, -1 none: here the one a local
    // run's player drives (one player, the first driven); a server's run's
    // only the one this client drives (task server 05's review: every
    // watcher was taken into the driver's cab).
    virtual int drivenHere(const SimulationFrame &frame) const {
        return frame.driven.empty() ? -1 : frame.driven.front();
    }
    // The route left (another loaded): what the link holds of it dropped.
    virtual void forgetRoute() {}
    // A server's run's crews (task server 06): who drives which train, its
    // cab shared or not; none locally. A crew's first player shares its cab
    // or not.
    virtual std::vector<Crew> crews() const { return {}; }
    virtual void share(int, bool) {}

    // Told by the link (task server 05): a run begun elsewhere (a server's,
    // announced, this view's input the same) for the view to take on; a
    // run ended there, why.
    std::function<void()> runBegun;
    std::function<void(const QString &why)> runEnded;

    // The commands (SimulationSession's: `started` and `stopped` are the
    // view's part, before the activity's editing is locked and unlocked).
    virtual bool start(Activity *activity, const RunOptions &options, QString *error,
                       const std::function<void()> &started) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void setSpeed(double speed) = 0;
    virtual bool runTo(double target, bool thenRun, int untilOnTrack, QString *error) = 0;
    virtual bool reset(QString *error) = 0;
    virtual void stop(const std::function<void()> &stopped) = 0;
    virtual void drive(int train) = 0;
    // Commands name their train (task server 06: several driven at once).
    virtual void leave(int train) = 0;
    virtual void command(int train, int vehicle, const Message &message) = 0;
    virtual void hear(const std::vector<int> &trains) = 0;
    virtual void setCrossings(const std::vector<SimulationCore::CrossingParameters> &list) = 0;

    // The latest frame of the run, null before its first (drawn each time;
    // its publishedNs on this view's clock).
    virtual std::shared_ptr<const SimulationFrame> frame() const = 0;
    // The latest frame and the run's events since the last take; the
    // events are the run's runId() has now (from its first when that
    // changed), the frame may be of the run before.
    struct Taken {
        std::shared_ptr<const SimulationFrame> frame;
        std::vector<TrainEvent> events;
    };
    virtual Taken take() = 0;
    // The messages since the last call: the driven train's, the heard
    // trains' sound triggers.
    virtual std::vector<TrainMessage> takeMessages() = 0;
};

// The link to a session in this process, which it owns: calls on the main
// thread, frames, inputs and events shared as they are (nothing copied or
// encoded).
class LocalSimulationLink : public SimulationLink {
public:
    SimulationSession &session() { return own; }

    bool exists() const override { return own.exists(); }
    unsigned int runId() const override { return own.runId(); }
    RunMode mode() const override { return own.mode(); }
    double speed() const override { return own.speed(); }
    unsigned long long lastCommand() const override { return own.lastCommand(); }
    qint64 inputMilliseconds() const override { return own.inputMilliseconds(); }
    std::shared_ptr<const SimulationInput> input() const override { return own.input(); }
    Activity *activity() const override { return own.activity(); }

    bool start(Activity *activity, const RunOptions &options, QString *error,
               const std::function<void()> &started) override {
        return own.start(activity, options, error, started);
    }
    void play() override { own.play(); }
    void pause() override { own.pause(); }
    void setSpeed(double speed) override { own.setSpeed(speed); }
    bool runTo(double target, bool thenRun, int untilOnTrack, QString *error) override {
        return own.runTo(target, thenRun, untilOnTrack, error);
    }
    bool reset(QString *error) override { return own.reset(error); }
    void stop(const std::function<void()> &stopped) override { own.stop(stopped); }
    void drive(int train) override { own.drive(train); }
    void leave(int train) override { own.leave(train); }
    void command(int train, int vehicle, const Message &message) override { own.command(train, vehicle, message); }
    void hear(const std::vector<int> &trains) override { own.hear(trains); }
    void setCrossings(const std::vector<SimulationCore::CrossingParameters> &list) override {
        own.setCrossings(list);
    }

    std::shared_ptr<const SimulationFrame> frame() const override { return own.frame(); }
    Taken take() override;
    std::vector<TrainMessage> takeMessages() override { return own.takeMessages(); }

private:
    SimulationSession own;
    // The run whose events were taken, and how many.
    unsigned int takenRun = 0;
    size_t takenEvents = 0;
};

}
