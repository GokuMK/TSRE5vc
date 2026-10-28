#pragma once
#include <memory>
#include <vector>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/VehicleModel.h>

namespace Sim {

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
    // Metres travelled where its vehicles are drawn (the state's and the
    // way on between steps): its wheels turn by it.
    double drawnTravelled = 0.0;
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

// The driven train (task driving 02): which, its cab, and each vehicle's
// values as its model and air brake give them (VehicleOutputs::values, in
// the consist's order): the pantographs, the headlight, the levers, the
// pressures, and whatever its script publishes.
struct DrivenFrame {
    int train = -1;
    int cab = -1;
    std::vector<QHash<QString, MessageValue>> values;
};

// The simulation at one moment, as the host publishes it for the main
// thread (task simulation 01, section 6): immutable once made, the latest
// replacing the one before.
struct SimulationFrame {
    unsigned int runId = 0;
    // The last command the host took (its sequence number).
    unsigned long long acknowledged = 0;
    unsigned long long steps = 0;
    // Seconds after midnight.
    double time = 0.0;
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
    // frame before while they have not changed; what tells they have.
    std::shared_ptr<const std::vector<int>> aspects;
    std::shared_ptr<const std::vector<int>> drawStates;
    unsigned long long drawSerial = 0;
    std::shared_ptr<const std::vector<SimulationCore::SectionUse>> sectionUses;
    unsigned long long holdSerial = 0;
    std::vector<size_t> standsOn;
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
    // Driving (task driving 02): the train driven, -1 none; its values once
    // it is on the track; the collision that ended the run.
    int driven = -1;
    std::shared_ptr<const DrivenFrame> drivenValues;
    SimulationCore::Collision collision;
};

// The core's state now, with its vehicles' poses; what has not changed
// since `before` (of the same run) shared with it.
std::shared_ptr<SimulationFrame> MakeFrame(const SimulationCore &core, const SimulationFrame *before = nullptr);

}
