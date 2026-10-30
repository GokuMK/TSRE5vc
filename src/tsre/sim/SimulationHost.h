#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <tsre/sim/SimulationFrame.h>

class QThread;
class QTimer;

namespace Sim {

// The simulation's thread (task simulation 01, section 1): a QObject moved
// to a QThread of its own, which owns the core. Commands come as queued
// calls (SimulationController), each with a sequence number that the next
// frame acknowledges; a timer in the thread paces the core and publishes a
// frame each tick. Stepping is cut into batches of at most BatchMs, so the
// thread takes commands between them.
class SimulationHost : public QObject {
    Q_OBJECT
public:
    // Ticks while running at a pace; as fast as possible, back to back.
    static constexpr int TickMs = 16;
    static constexpr int BatchMs = 20;
    // The longest wall time one tick makes up for: after a stall the
    // simulation goes on from where it was rather than jumping.
    static constexpr double LongestTick = 0.25;
    // Events kept for the main thread; beyond, the oldest are dropped.
    static constexpr size_t MaxEvents = 100000;

    SimulationHost();
    ~SimulationHost() override;

    // Any thread.
    std::shared_ptr<const SimulationFrame> frame() const;
    // The events since the last call, in order; `dropped` how many were
    // lost to the bound since.
    std::vector<TrainEvent> takeEvents(size_t *dropped = nullptr);
    // Both at once: the events taken are those published up to the frame.
    std::shared_ptr<const SimulationFrame> take(std::vector<TrainEvent> &events, size_t *dropped = nullptr);

    // The commands, on the host's thread.
    // A new core for the input, paused at its start (start and reset).
    void start(std::shared_ptr<const SimulationInput> input, unsigned long long seq);
    void play(unsigned long long seq);
    void pause(unsigned long long seq);
    // Simulated seconds a second; 0 as fast as possible.
    void setSpeed(double speed, unsigned long long seq);
    // As fast as possible up to `time` (seconds after midnight), then
    // paused there, or running on at the speed when `thenRun`; with
    // `untilOnTrack`, already at the first step that train is on the track
    // (Follow, the user's 2026-10-22), paused at `time` when it is not
    // there by then.
    void runTo(double time, unsigned long long seq, bool thenRun = false, int untilOnTrack = -1);
    // The core gone; the host is handed back to `owner`'s thread and its
    // own thread quits.
    void stop(unsigned long long seq, QThread *owner);
    // Driving (task driving 02): a train taken over from the AI (or set
    // up once it is on the track), the driven one given back, a message to
    // a vehicle of the driven train (-1 its cab).
    void drive(int train, unsigned long long seq);
    void leave(unsigned long long seq);
    void command(int vehicle, const Message &message, unsigned long long seq);
    // The trains heard (task sound 02): their shadows run while the run
    // runs or is paused, not while running to a time.
    void hear(const std::vector<int> &trains, unsigned long long seq);
    // Any thread: the messages since the last call: the driven train's
    // models' events, the heard trains' sound triggers.
    std::vector<TrainMessage> takeMessages();
    static constexpr size_t MaxMessages = 10000;

private:
    std::unique_ptr<SimulationCore> core;
    QTimer *timer = nullptr;
    QElapsedTimer wall;
    RunMode mode = RunMode::Paused;
    double speed = 1.0;
    double target = 0.0;
    bool runOn = false;
    int untilTrain = -1;
    // That train is on the track (placed, or gone at its end).
    bool trainPlaced(int train) const;
    unsigned long long acknowledged = 0;
    // The rate reached: simulated seconds over wall time since `rateWall`.
    QElapsedTimer rateWall;
    double rateSimulated = 0.0;
    double rate = 0.0;

    mutable std::mutex mailbox;
    std::shared_ptr<const SimulationFrame> latest;
    std::deque<TrainEvent> events;
    size_t droppedEvents = 0;
    std::deque<TrainMessage> messages;
    std::vector<int> heardTrains;
    // The core told the trains heard, as the mode wants.
    void hearing();

    void tick();
    // Steps up to `seconds`, until the batch's time is out (or the train
    // waited for is on the track).
    void stepFor(double seconds, const QElapsedTimer &batch);
    // The timer as the mode wants it.
    void schedule();
    void publish();
};

}
