#pragma once
#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>
#include <QStringList>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/VehicleModel.h>

namespace Sim {

// A monotonic clock any thread reads the same (ns): when a frame was
// published, and when a paint drew it (the frame log's diagnostics).
inline long long SteadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
}

// How the host runs the core.
enum class RunMode { Paused, Running, RunningTo };

// A train in a frame: its state and its vehicles' two ends
// (SimulationCore::vehicleEnds), none when it is not on the track; its
// vehicles as they are now and their version (task 07: they change as it
// couples and cuts; shared with the frame before while the same).
struct TrainFrame {
    TrainState state;
    std::vector<TrackPose> fronts;
    std::vector<TrackPose> rears;
    std::shared_ptr<const std::vector<Vehicle>> vehicles;
    unsigned int carsVersion = 0;
    // Metres travelled where its vehicles are (the stepped state's): its
    // wheels turn by it. The views move it on to their tick's time
    // (SimulationController::placeTrains), as its vehicles.
    double drawnTravelled = 0.0;
    // The vector nodes of the route part it runs on, in its order (shared
    // while it stays on that part): the views take the junctions it takes
    // when they move its vehicles on. Null: none (not on its route).
    std::shared_ptr<const std::vector<int>> pathNodes;
    int pathPart = -1;
    // Held at the end of its authority (an AI train); a driven train may
    // run past it.
    bool keepsToAuthority = true;
};

// A standing train (task 07: the activity's loose consists, cars cut off):
// its name, vehicles and their two ends (SimulationCore::standingEnds),
// none for one gone.
struct StandingFrame {
    QString name;
    std::vector<Vehicle> vehicles;
    std::vector<TrackPose> fronts;
    std::vector<TrackPose> rears;
    bool gone = false;
    // One of the activity's loose consists (else cars a train cut off).
    bool loose = false;
};

// A driven train's crew on a server's run (task server 06): its players by
// name in the order they came (the first sets whether others may join), and
// whether its cab is shared.
struct Crew {
    int train = -1;
    bool shared = false;
    QStringList players;
};

// The driven train (task driving 02): which, its cab, and each vehicle's
// values as its model and air brake give them (VehicleOutputs::values, in
// the consist's order): the pantographs, the headlight, the levers, the
// pressures, and whatever its script publishes.
struct DrivenFrame {
    int train = -1;
    int cab = -1;
    std::vector<QHash<QString, MessageValue>> values;
    // The cab's 3D cab's controls' values (task driving 08), one per control
    // of `cabControlsFor` (the driven end's, task maszyna 04) in the
    // control's units, NaN where none; empty without one. The stops its
    // script gives the controls, null for none.
    std::vector<double> cabControls;
    std::shared_ptr<const CabControls> cabControlsFor;
    std::shared_ptr<const CabStops> cabStops;
};

// A heard train (task sound 02): an AI train whose vehicles' models run as
// a shadow; its cab and each vehicle's values, as a driven train's, and
// what changes each time they do (SimulationCore::shadowVersion).
struct HeardFrame {
    int train = -1;
    int cab = -1;
    unsigned long long version = 0;
    std::vector<QHash<QString, MessageValue>> values;
};

// The simulation at one moment, as the host publishes it for the main
// thread (task simulation 01, section 6): immutable once made, the latest
// replacing the one before.
struct SimulationFrame {
    // The run's step now (s): the input's, a driven train's or a run
    // played in real time (SimulationCore::stepLength).
    double step = 0.0;
    unsigned int runId = 0;
    // The last command the host took (its sequence number).
    unsigned long long acknowledged = 0;
    unsigned long long steps = 0;
    // The host's tick that published it (one a tick): with the run's id,
    // which frame it is (SameFrame). When the view got it, on the view's
    // clock (SteadyNs): the host's stamp in this process; a view elsewhere
    // stamps its arrival (task server 05).
    unsigned long long tick = 0;
    long long publishedNs = 0;
    // The longest age its trains are moved on for by a view (s): a local
    // frame's 50 ms; a remote one's two frame intervals and the delays'
    // spread (task server 05: frames 30 a second).
    double drawnAgeMost = 0.05;
    // Seconds after midnight: the stepped time, and the time asked for by
    // the frame's tick (the stepped and the time not yet a whole step).
    double time = 0.0;
    double askedTime = 0.0;
    RunMode mode = RunMode::Paused;
    // Simulated seconds a second: asked for (0 as fast as possible), and
    // reached lately.
    double speed = 1.0;
    double rate = 0.0;
    // Where a run to a time stops.
    double target = 0.0;
    // Every train has finished its path.
    bool finished = false;
    // By train id (the input's order).
    std::vector<TrainFrame> trains;
    // Each signal head's aspect and draw state (SimulationCore::aspects,
    // drawStates) and each section's use (sectionUses), shared with the
    // frame before while they have not changed; what tells they have (the
    // section uses' serial counts their changes within the run: they change
    // with the holders and with the sections a train stands on).
    std::shared_ptr<const std::vector<int>> aspects;
    std::shared_ptr<const std::vector<int>> drawStates;
    unsigned long long drawSerial = 0;
    std::shared_ptr<const std::vector<SimulationCore::SectionUse>> sectionUses;
    unsigned long long sectionSerial = 0;
    unsigned long long holdSerial = 0;
    std::vector<size_t> standsOn;
    // Each level crossing closed (1) or open (task traffic 01,
    // SimulationCore::levelCrossingStates), shared with the frame before
    // while unchanged; what tells it has.
    std::shared_ptr<const std::vector<unsigned char>> levelCrossings;
    unsigned long long levelCrossingSerial = 0;
    // The activity's events (task 06): their states (the input's list),
    // shared with the frame before while unchanged; the activity's end and
    // when (seconds after midnight, -1 not yet).
    std::shared_ptr<const std::vector<EventState>> eventStates;
    unsigned long long eventSerial = 0;
    // The standing trains, shared with the frame before while unchanged.
    std::shared_ptr<const std::vector<StandingFrame>> standing;
    unsigned long long standingSerial = 0;
    ActivityEnd activityEnd = ActivityEnd::None;
    double activityEndedAt = -1.0;
    // Driving (task driving 02; several trains: task server 06): the
    // trains driven, in train order; each one's values once it is on the
    // track (its train in it); the collision that ended the run.
    std::vector<int> driven;
    std::vector<std::shared_ptr<const DrivenFrame>> drivenValues;
    SimulationCore::Collision collision;
    bool isDriven(int train) const { return std::binary_search(driven.begin(), driven.end(), train); }
    // A driven train's values, null for none.
    const DrivenFrame *drivenOf(int train) const {
        for (const std::shared_ptr<const DrivenFrame> &d : drivenValues)
            if (d != nullptr && d->train == train)
                return d.get();
        return nullptr;
    }
    // The heard trains' values (task sound 02), in train order.
    std::vector<HeardFrame> heard;
};

// The core's state now, with its vehicles' poses; what has not changed
// since `before` (of the same run) shared with it.
std::shared_ptr<SimulationFrame> MakeFrame(const SimulationCore &core, const SimulationFrame *before = nullptr);

// The same frame (task server 04): of the same run and the host's same
// tick, wherever it is held (a frame made again elsewhere, a new frame where
// a freed one was); both none is the same too.
inline bool SameFrame(const SimulationFrame *a, const SimulationFrame *b) {
    if (a == nullptr || b == nullptr)
        return a == b;
    return a->runId == b->runId && a->tick == b->tick;
}

}
