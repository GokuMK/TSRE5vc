#pragma once
#include <deque>
#include <memory>
#include <vector>
#include <tsre/sim/SimulationInput.h>

namespace Sim {

enum class TrainStatus { Waiting, Running, Stopped, Finished };
// Why a train stands. Blocked: at the end of its authority, the track ahead
// held by another train or a standing consist.
enum class StopKind { None, Station, Wait, Reversal, End, Blocked };

// Who holds a section: a train's id, or these.
constexpr int NoHolder = -1;
constexpr int StandingConsist = -2;

// A train's state in the simulation.
struct TrainState {
    TrainStatus status = TrainStatus::Waiting;
    // The part of its route it is on, and its front's route distance there.
    int part = 0;
    double front = 0.0;
    // m/s.
    float speed = 0.0f;
    // The train runs the other way from how its consist lists its vehicles
    // (after an odd number of reversals): the last vehicle leads.
    bool reversed = false;
    // Standing: why, and until when (seconds after midnight; a train at its
    // path's end stands for good).
    StopKind stoppedAt = StopKind::None;
    double until = 0.0;
    // The next station stop and waiting point (indexes in its plan).
    int nextStop = 0;
    int nextWait = 0;
    // Metres travelled.
    double travelled = 0.0;
    // The route distance (on its part) up to which its track is reserved.
    double authority = 0.0;
    // Waiting for track (to start, or at the end of its authority): who
    // holds the section it waits for (a train, StandingConsist), and the
    // section; NoHolder when it does not wait.
    int blockedBy = NoHolder;
    int waitSection = -1;
    // The stop signal it waits at (a signal of the network), -1 none.
    int atSignal = -1;
    // It is in a deadlock (reported once).
    bool deadlocked = false;
};

// Something that happened to a train, for the editor (task simulation 01,
// section 6).
struct TrainEvent {
    // Blocked: waiting for track held by `index` (a train, StandingConsist);
    // Deadlock: in a ring of trains waiting for each other, waiting for
    // train `index`.
    enum Kind { Started, Arrived, Departed, Waiting, Reversed, Horn, Finished, Blocked, Deadlock };
    double time = 0.0;
    int train = 0;
    Kind kind = Started;
    // The station stop or waiting point (index in the train's plan), or -1.
    int index = -1;
};

// The simulation's state and its stepping: plain C++, no QObject, nothing
// read but its input, so tests call it directly and the host runs it on its
// own thread.
class SimulationCore {
public:
    // The longest step taken at once: longer steps are cut into these.
    static constexpr double MaxStep = 0.1;
    // A train stops this far short of the end of its authority, and this
    // far short of a stop signal not cleared for it (Open Rails' AI keeps
    // 30 m to the stopping point).
    static constexpr double AuthorityMargin = 10.0;
    static constexpr double SignalMargin = 30.0;
    // Where no signal limits it, a train reserves this far ahead, or as far
    // as two minutes at its top speed (Open Rails' Auto Node).
    static constexpr double AutoNodeDistance = 5000.0;
    static constexpr double AutoNodeTime = 120.0;

    // Trains that started before the activity are run up to its start (the
    // pre-run).
    explicit SimulationCore(std::shared_ptr<const SimulationInput> input);

    const SimulationInput &input() const { return *in; }
    std::shared_ptr<const SimulationInput> inputPointer() const { return in; }
    // Seconds after midnight.
    double time() const { return now; }
    // Steps taken since the activity's start (the pre-run not counted).
    unsigned long long steps() const { return stepCount; }
    // Advances the simulation by `seconds`, in steps of at most MaxStep.
    void step(double seconds);
    const std::vector<TrainState> &trains() const { return states; }
    // The events since the last call.
    std::vector<TrainEvent> takeEvents();
    // Who holds each section of the network (a train, StandingConsist,
    // NoHolder).
    const std::vector<int> &holders() const { return holder; }
    // Each junction section's switch position (output pin 1 or 2); 0 for
    // other sections.
    const std::vector<int> &switches() const { return switchAt; }
    // Each signal of the network: the train it is cleared for, NoHolder.
    const std::vector<int> &cleared() const { return clearedFor; }
    // The two ends of each of a train's vehicles, in the consist's order:
    // the vehicle's front (as the consist lists it) and its rear, their
    // roll looking from the rear to the front. False for a train not on the
    // track (not started, or an AI train gone at its path's end).
    bool vehicleEnds(int train, std::vector<TrackPose> &fronts, std::vector<TrackPose> &rears) const;
    // Where a route distance of a train's current part is, its roll
    // looking along the route.
    bool pose(int train, double routeDistance, TrackPose &out) const;
    // Every train has finished its path.
    bool finished() const;

private:
    std::shared_ptr<const SimulationInput> in;
    double now = 0.0;
    unsigned long long stepCount = 0;
    std::vector<TrainState> states;
    std::vector<TrainEvent> events;
    // Per network section.
    std::vector<int> holder;
    std::vector<int> switchAt;
    // Per signal of the network.
    std::vector<int> clearedFor;
    // Per train, what the frames need not see: the sections it holds in its
    // route's order (where it leaves each), and the next projected section
    // it would reserve on its part.
    struct Held {
        int section = -1;
        double leave = 0.0;
    };
    // Not yet reported waiting.
    static constexpr int Unreported = -100;
    struct Runtime {
        std::deque<Held> held;
        size_t next = 0;
        // On its part's stop signals: the next whose block it has not
        // reserved, and the first its front has not passed.
        size_t nextSignal = 0;
        size_t passed = 0;
        // Its authority ends at a stop signal not cleared for it.
        bool endsAtSignal = false;
        // Whom it was last reported waiting for.
        int reported = Unreported;
    };
    std::vector<Runtime> runtime;

    // All trains from `now` on by `seconds`, in steps of at most MaxStep.
    void run(double seconds);
    // One train from `from` to `to` (seconds after midnight, at most
    // MaxStep apart).
    void advance(int train, double from, double to);
    void record(double time, int train, TrainEvent::Kind kind, int index = -1);
    // Reservation (task 02): the sections under a train standing at its
    // start, all or nothing; the track ahead up to its lookahead, a section
    // (with the junctions before it and the crossings on it) at a time;
    // giving back what its rear has left, or everything.
    bool occupy(int train);
    void extendAuthority(int train);
    void releaseBehind(int train);
    void releaseAll(int train);
    // Reserves projected sections [from, to) for the train: holds them,
    // sets their junctions its way.
    void take(int train, size_t from, size_t to);
    // How many stop signals from `first` on it may clear (Open Rails' legacy
    // and Open Rails modes of SignalNumClearAhead).
    size_t clearable(const Projection &way, size_t first) const;
    // A Blocked event, once for each holder it waits for.
    void blocked(int train, double time);
    // Rings of trains each waiting for track the next one holds.
    void findDeadlocks(double time);
};

}
