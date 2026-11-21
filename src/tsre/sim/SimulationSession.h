#pragma once
#include <QElapsedTimer>
#include <QString>
#include <functional>
#include <memory>
#include <vector>
#include <tsre/sim/SimulationFrame.h>

class Activity;
class QThread;

namespace Sim {

class SimulationHost;

// What a run is made with besides the editor's data (task server 04): the
// values a session reads from outside, given with the start, so that a
// view in another process can make the same input.
struct RunOptions {
    // The world's date (yyyy-MM-dd): the run's first day (an MSTS
    // activity's season moves it), and so its outside air.
    QString date;
    // AI trains wait at station stops for their people to board
    // (core.simulation.boardingHolds).
    bool boardingHolds = true;
    // Drive beside Play: the player's train driven from its start.
    bool driving = false;
    // A run with no trains at all is one too (task explore 04: walking a
    // route explored without a train); else none starts without a train.
    bool trainless = false;
    // The editor's now: the world's date and the setting.
    static RunOptions Current(bool driving = false);
    bool operator==(const RunOptions &other) const {
        return date == other.date && boardingHolds == other.boardingHolds && driving == other.driving
               && trainless == other.trainless;
    }
};

// The run's input for `activity`, made from the editor's data with
// `options` (task server 05: a session, and a view in another process
// checking it by its digest, make it alike); null, with why, when it has no
// train to run (or, driving, no engine drives the player's train).
std::shared_ptr<const SimulationInput> MakeRunInput(Activity *activity, const RunOptions &options,
                                                    QString *error = nullptr);

// The simulation's owner (task server 04): it makes a run's input from the
// editor's data, owns the host and its thread, sends the commands with
// their sequence numbers and keeps the run's state (the mode asked, the
// speed, a run to a time, its events). It knows no camera, consist copy or
// widget, and runs without a window: here on the main thread beside the
// views (SimulationController), later in the server's process. While a run
// exists the activity's data is not edited (ActivityNotifier).
class SimulationSession {
public:
    SimulationSession();
    ~SimulationSession();

    bool exists() const { return host != nullptr; }
    Activity *activity() const { return current; }
    const RunOptions &options() const { return runOptions; }
    // The run's input, its id (the frames', the input's; 0 none) and how
    // long the input took to make (ms).
    std::shared_ptr<const SimulationInput> input() const { return in; }
    unsigned int runId() const;
    qint64 inputMilliseconds() const { return inputMs; }
    // Running, running to a time or paused, as last asked (or as the host
    // ended a run to a time); the speed asked, kept between runs.
    RunMode mode() const { return asked; }
    double speed() const { return pace; }
    // The sequence number of the last command sent.
    unsigned long long lastCommand() const { return sequence; }

    // A new run of the activity, paused at its start: its input made from
    // the editor's data now with `options`, kept for the run (its reset, a
    // run back to a time); false, with why, when it has no train to run
    // (or, driving, no engine drives the player's train). `started` is
    // called once the run exists, before the activity's editing is locked:
    // a view takes the run on there, so the lock's listeners find it.
    bool start(Activity *activity, const RunOptions &options, QString *error = nullptr,
               const std::function<void()> &started = nullptr);
    void play();
    void pause();
    void setSpeed(double speed);
    // As fast as possible up to `target` (seconds after the start day's
    // midnight), then paused there or running on (`thenRun`); a time behind
    // the run's is reached from the start again (a new run). With
    // `untilOnTrack`, already at the first step that train is on the track.
    bool runTo(double target, bool thenRun, int untilOnTrack, QString *error = nullptr);
    // A new run from the editor's data, back at the start, running on when
    // it ran.
    bool reset(QString *error = nullptr);
    // `stopped` is called once the run is gone, before the activity's
    // editing is unlocked.
    void stop(const std::function<void()> &stopped = nullptr);
    void drive(int train);
    // Commands name their train (task server 06: several driven at once).
    void leave(int train);
    void command(int train, int vehicle, const Message &message);
    // The trains a view hears (task sound 02), and the level crossings'
    // values from its loaded objects (task traffic 01), as it sends them.
    void hear(const std::vector<int> &trains);
    void setCrossings(const std::vector<SimulationCore::CrossingParameters> &list);

    // The host's latest frame of this run, null before its first.
    std::shared_ptr<const SimulationFrame> frame() const;
    // Takes the host's latest frame and its events: the events kept, in
    // order; a run to a time the host had passed when it took it started
    // again from the start (a new run: the frame taken is the old run's);
    // once every command is taken, the mode asked is the host's. The frame
    // taken, null before the run's first.
    std::shared_ptr<const SimulationFrame> update();
    // The run's events so far, in order.
    const std::vector<TrainEvent> &events() const { return history; }
    // The messages since the last call: the driven train's, the heard
    // trains' sound triggers.
    std::vector<TrainMessage> takeMessages();

private:
    SimulationHost *host = nullptr;
    QThread *thread = nullptr;
    Activity *current = nullptr;
    RunOptions runOptions;
    std::shared_ptr<const SimulationInput> in;
    std::shared_ptr<const SimulationFrame> latest;
    std::vector<TrainEvent> history;
    qint64 inputMs = 0;
    unsigned long long sequence = 0;
    double pace = 1.0;
    RunMode asked = RunMode::Paused;
    // The last run to a time until the host takes it: its command, target,
    // whether it runs on after and the train it waits for (update: a time
    // the host has passed by then starts again from the start).
    unsigned long long runToSeq = 0;
    double runToTarget = 0.0;
    bool runToThenRun = false;
    int runToTrain = -1;

    // A new input for `activity`; null with why.
    std::shared_ptr<const SimulationInput> makeInput(Activity *activity, QString *error);
    // The host starts `input`: a new run, at the speed asked.
    void sendStart(std::shared_ptr<const SimulationInput> input);
    // The run's input made again and started, its state as new.
    bool restart(QString *error);
};

}
