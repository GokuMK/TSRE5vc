#pragma once
#include <deque>
#include <functional>
#include <memory>
#include <vector>
#include <tsre/sim/SimulationInput.h>

namespace Sim {

enum class TrainStatus { Waiting, Running, Stopped, Finished };
// Why a train stands. Blocked: at the end of its authority, the track ahead
// held by another train or a standing consist.
enum class StopKind { None, Station, Wait, Reversal, End, Blocked };

// What a waiting train waits for: track held by another train (or a
// consist), the single track ahead claimed for an opposing train (task 02,
// section 4), the passing track it would take held.
enum class WaitFor { Track, SingleTrack, PassingTrack };

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
    WaitFor waitFor = WaitFor::Track;
    // The stop signal it waits at (a signal of the network), -1 none.
    int atSignal = -1;
    // The speed the last stop signal it passed gave it (its aspect's), m/s;
    // negative none.
    float signalSpeed = -1.0f;
    // It is in a deadlock (reported once).
    bool deadlocked = false;
};

// Something that happened to a train, for the editor (task simulation 01,
// section 6).
struct TrainEvent {
    // Blocked: waiting for `index` (a train, StandingConsist) as `waitFor`
    // says, at `signal` (a signal of the network, -1 none); Deadlock: in a
    // ring of trains waiting for each other, waiting for train `index`.
    enum Kind { Started, Arrived, Departed, Waiting, Reversed, Horn, Finished, Blocked, Deadlock };
    double time = 0.0;
    int train = 0;
    Kind kind = Started;
    // The station stop or waiting point (index in the train's plan), or -1.
    int index = -1;
    WaitFor waitFor = WaitFor::Track;
    int signal = -1;
    // Metres the train had travelled.
    double travelled = 0.0;
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
    // A train waiting for track tries again at least this often (seconds),
    // and whenever what it waits for changes.
    static constexpr double RetryEvery = 5.0;
    // Open Rails' forced red at station stops: a stop's exit signal is the
    // first stop signal beyond the platform on the platform's node, within
    // this distance (or further on when its block has a junction or
    // crossing), the train stopping short of it or mostly at the platform;
    // a train clears it only when standing at the stop with its departure
    // this near (seconds).
    static constexpr double ExitSignalReach = 150.0;
    static constexpr double HoldRelease = 120.0;
    // Open Rails' clearing distance at station stops: its player train and
    // trains of fewer than StationClearingCars vehicles the short one.
    static constexpr double StationClearing = 30.0;
    static constexpr double StationClearingShort = 15.0;
    static constexpr size_t StationClearingCars = 10;
    // Where no signal limits it, a train reserves this far ahead, or as far
    // as two minutes at its top speed (Open Rails' Auto Node).
    static constexpr double AutoNodeDistance = 5000.0;
    static constexpr double AutoNodeTime = 120.0;
    // Physics driving (task 03), as Open Rails' AI: it plans its braking at
    // this share of its deceleration (allowing for the brakes' delay),
    // comes down to the creep speed where it stops at a station, LimitCreep
    // short of a reversal, its path's end or a waiting point, SignalCreep
    // short of a stop signal and AuthorityCreep short of its authority's
    // end (creeping on from there at half the creep speed), and stops from
    // the creep speed at its full deceleration. It is down to a lower limit
    // ahead LimitCreep before it. Short of a station's stop by 1.5 × its top
    // speed² / its deceleration, and short of anything else it stops at or
    // slows for by its braking distance and BrakingZone (Open Rails' AI
    // braking state), it accelerates at full power up to that curve. Braking
    // to stop, it holds its speed between half and one m/s above the curve
    // (its bands): BrakingMargin.
    static constexpr double BrakingShare = 0.22;
    static constexpr double BrakingMargin = 0.75;
    static constexpr double CreepSpeed = 2.5;
    static constexpr double SignalCreep = 50.0;
    static constexpr double AuthorityCreep = 30.0;
    static constexpr double LimitCreep = 10.0;
    static constexpr double BrakingZone = 90.0;

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
    // Single-track stretches (task 02, section 4): track two trains' routes
    // use in opposite directions, between the places where they part and
    // one of them fits clear, as projected section ranges [from, to) of each
    // one's part.
    struct Stretch {
        int train[2] = {-1, -1};
        int part[2] = {0, 0};
        size_t from[2] = {0, 0};
        size_t to[2] = {0, 0};
        // The route distance where each leaves it.
        double leave[2] = {0.0, 0.0};
    };
    const std::vector<Stretch> &stretches() const { return single; }
    // Each stretch's claimant (a train), NoHolder.
    const std::vector<int> &claims() const { return claimedBy; }
    // Each head's aspect (SignalType::Aspect) and draw state, from the
    // built-in rule (no scripts, task 02).
    const std::vector<int> &aspects() const { return headAspect; }
    const std::vector<int> &drawStates() const { return headDraw; }
    // Changes whenever a head's aspect or draw state does.
    unsigned long long drawSerial() const { return drawChanges; }
    // For the map: each section's holder (a train, a standing consist,
    // NoHolder) and whether it is stood on (under a train or a consist).
    struct SectionUse {
        int holder = NoHolder;
        bool occupied = false;
    };
    std::vector<SectionUse> sectionUses() const;
    // Changes whenever a holder does.
    unsigned long long holdSerial() const { return holdChanges; }
    // How many of the sections a train holds it stands on (they change as
    // its front passes into the next).
    size_t standsOn(int train) const;
    // What it has done (the scaling workloads, task 02 phase 7): authority
    // extensions, sections looked at for their holders, aspect evaluations
    // and the heads they visited.
    struct Work {
        unsigned long long authority = 0;
        unsigned long long sectionChecks = 0;
        unsigned long long aspectRuns = 0;
        unsigned long long aspectHeads = 0;
    };
    const Work &work() const { return workDone; }
    // The two ends of each of a train's vehicles, in the consist's order:
    // the vehicle's front (as the consist lists it) and its rear, their
    // roll looking from the rear to the front. False for a train not on the
    // track (not started, or an AI train gone at its path's end).
    bool vehicleEnds(int train, std::vector<TrackPose> &fronts, std::vector<TrackPose> &rears) const;
    // Where a route distance of a train's current part is, its roll
    // looking along the route.
    bool pose(int train, double routeDistance, TrackPose &out) const;
    // The next stop signal ahead of a train's front on its route part and
    // its least restrictive NORMAL head's aspect, how far ahead (m); -1
    // none (comparisons with Open Rails, task 05).
    int nextSignalAhead(int train, int *aspect = nullptr, double *distance = nullptr) const;
    // Every train has finished its path.
    bool finished() const;
    // Nothing more will happen: every train has finished, stands at its
    // path's end, is in a deadlock or waits for track a standing consist
    // holds (those never move).
    bool settled() const;

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
    // Per head; evaluated again after a step that changed reservations or
    // switches.
    std::vector<int> headAspect;
    std::vector<int> headDraw;
    unsigned long long drawChanges = 0;
    unsigned long long holdChanges = 0;
    // Per section, holdChanges when its holder last changed; claims changed.
    std::vector<unsigned long long> heldStamp;
    unsigned long long claimChanges = 0;
    Work workDone;
    // Aspects are evaluated again where something changed: every head the
    // first time; signals cleared or given back; switches set (task 02,
    // phase 7, the scaling workloads).
    bool aspectsAll = true;
    bool aspectsReady = false;
    std::vector<int> dirtyGroups;
    std::vector<int> dirtyJunctions;
    bool aspectsPending() const { return aspectsAll || !dirtyGroups.empty() || !dirtyJunctions.empty(); }
    // Kept from one evaluation to the next: each signal's next stop signal
    // (its NORMAL heads read it), each other head's (-2 not known); per
    // reader (a signal, or head h as -(h + 1); slot: signals, then heads)
    // the facing junctions on the way and the signal it reads; per
    // junction section the readers through it and the heads linked to it;
    // per signal its readers.
    std::vector<int> nextOfGroup;
    std::vector<int> nextOfHead;
    std::vector<std::vector<int>> facingOf;
    std::vector<int> readsFrom;
    std::vector<std::vector<int>> watchers;
    std::vector<std::vector<int>> linkedTo;
    std::vector<std::vector<int>> readers;
    std::vector<char> queued;
    // Signal scripts (task 05): per head, the signals and junction sections
    // its last run read beyond its signal's next stop signal (kept above);
    // per signal and junction section, the heads to run again when it
    // changes.
    std::vector<std::vector<int>> scriptSignals;
    std::vector<std::vector<int>> scriptJunctions;
    std::vector<std::vector<int>> signalScriptReaders;
    std::vector<std::vector<int>> junctionScriptReaders;
    std::vector<Stretch> single;
    std::vector<int> claimedBy;
    // Per train and part: its stretches (stretch, its side) by where it
    // enters them.
    std::vector<std::vector<std::vector<std::pair<int, int>>>> stretchesOf;
    // Per train, what the frames need not see: the sections it holds in its
    // route's order (where it leaves each), and the next projected section
    // it would reserve on its part.
    struct Held {
        int section = -1;
        double enter = 0.0;
        double leave = 0.0;
    };
    // Not yet reported waiting.
    static constexpr int Unreported = -100;
    struct Runtime {
        // Its route as it runs it: the plan's, a part changed where it took
        // a passing path (stops, waits and passing paths further on moved
        // with it).
        std::vector<RouteTrack> parts;
        std::vector<Projection> ways;
        std::vector<std::vector<SpeedLimit>> limits;
        std::vector<StopPlan> stops;
        // Each stop's exit signal: an index in its part's stop signals, -1
        // none; its hold let go (dropHold).
        std::vector<int> exits;
        std::vector<bool> letGo;
        std::vector<WaitPlan> waits;
        std::vector<PassingPlan> passing;
        // Each passing path's own track: its track sections, sorted.
        std::vector<std::vector<int>> passingSections;
        // Each passing path's route part made once (until one is taken).
        struct Side {
            bool made = false;
            RouteTrack route;
            Projection way;
        };
        std::vector<Side> sides;
        std::deque<Held> held;
        size_t next = 0;
        // On its part's stop signals: the next whose block it has not
        // reserved, and the first its front has not passed.
        size_t nextSignal = 0;
        size_t passed = 0;
        // Its authority ends at a stop signal not cleared for it.
        bool endsAtSignal = false;
        // On its part's speed limits: the first its rear has not left.
        size_t limit = 0;
        // Physics: starting away from rest, until near its limit (Open
        // Rails' accelerating state).
        bool startingAway = false;
        // Whom it was last reported waiting for.
        int reported = Unreported;
        // Waiting for track: when it last tried (-1 not), and what the
        // holders and claims were then.
        double triedAt = -1.0;
        unsigned long long triedHold = 0;
        unsigned long long triedClaims = 0;
    };
    std::vector<Runtime> runtime;

    // All trains from `now` on by `seconds`, in steps of at most MaxStep.
    void run(double seconds);
    // One train from `from` to `to` (seconds after midnight, at most
    // MaxStep apart), once it has started.
    void advance(int train, double from, double to);
    // A train due by `to` takes its place: its whole length on track no one
    // else holds, else it waits.
    void start(int train, double from, double to);
    // A single-track stretch it would stand in at its start is claimed for
    // another train, or the other is in it: who.
    bool startClaimed(int train, int *blocker) const;
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
    // Where a reservation unit from projected section `from` ends (before
    // `stop`): a track section with the junctions before it and the
    // crossings on it.
    size_t chunkEnd(const std::vector<ProjectedSection> &way, size_t from, size_t stop) const;
    // Its stops' exit signals (on their parts as they are now); whether the
    // stop signal at `signal` on its part is one held for a stop ahead.
    void findExits(int train);
    bool heldAtStop(int train, size_t signal) const;
    bool holdDroppable(int train, size_t signal) const;
    bool dropHold(int train, size_t signal);
    // The heads' aspects and draw states (task 02, section 6), where they
    // may have changed.
    void evaluateAspects();
    void clearedChanged(int signal);
    void switchChanged(int junction);
    // The stretches of each pair of trains; those of two trains' parts.
    void findStretches();
    void addStretches(int a, int pa, int b, int pb);
    // The junction section a route reaches within a metre of a route
    // distance, from index `start` (the way's size: none); the way indices
    // of the main route through the loops a train's passing paths on a part
    // go round, where it fits on them.
    size_t junctionNear(const std::vector<ProjectedSection> &way, double at, size_t start) const;
    // `usedByOther`: the other train passes that section (a passing path on
    // its track is no place to pass it).
    void loopsOf(int train, int part, std::vector<char> &marks, const std::function<bool(int)> &usedByOther) const;
    // A train's stretches on its current part again (its route changed),
    // keeping claims on the same track.
    void refreshStretches(int train);
    // Whether the train may go on into projected sections [from, to), as
    // mayEnter, without claiming; who it would wait for.
    bool enterable(int train, size_t from, size_t to, int *blocker) const;
    // Open Rails' path-based passing paths (task 02, section 5): where the
    // unit about to be reserved holds the junction a passing path leaves the
    // route by and the main route through to where it rejoins is claimed for
    // an opposing train or held (a train, a standing consist), the passing
    // path is taken when it is free.
    enum class Passing { None, Taken, Wait };
    Passing choosePassing(int train, size_t from, size_t to);
    void takePassing(int train, size_t index);
    // Whether the train may go on into projected sections [from, to): any
    // stretch it enters there is claimed for it unless the other train is in
    // it or has claimed it (then it waits for that train).
    bool mayEnter(int train, size_t from, size_t to);
    // The other train has left the stretch behind (or has gone).
    bool passed(const Stretch &stretch, int side) const;
    // The speed a stop signal gives a train passing it: its least
    // restrictive NORMAL head's aspect's; negative none.
    float signalSpeedOf(int signal) const;
    // The speed a train may have at the end of a step from `v` over `dt` to
    // come down to `target` within `distance` at deceleration `d` (exact on
    // the braking curve).
    static double brakingSpeed(double v, double dt, double d, double distance, double target);
    // When a train ready to leave a station at `ready` leaves: rounded up
    // to the input's departure step.
    double departureAt(double ready) const;
    // Physics driving: the acceleration at full power at speed `v` (the
    // engines' force, limited by their power, less the train's resistance
    // and the gradient under it), m/s².
    double fullAcceleration(int train, double v) const;
    // Physics driving: the speed a train may have at the end of a step from
    // `v` over `dt` to stop at a target of kind `kind` `distance` ahead,
    // where its limit is `limit`.
    double stoppingSpeed(int train, double v, double dt, double distance, StopKind kind, double limit) const;
    // A Blocked event, once for each holder it waits for.
    void blocked(int train, double time);
    // Rings of trains each waiting for track the next one holds.
    void findDeadlocks(double time);
};

}
