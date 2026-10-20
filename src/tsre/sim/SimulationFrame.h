#pragma once
#include <memory>
#include <vector>
#include <tsre/sim/SimulationCore.h>

namespace Sim {

// How the host runs the core.
enum class RunMode { Paused, Running, RunningTo };

// A train in a frame: its state and its vehicles' two ends
// (SimulationCore::vehicleEnds), none when it is not on the track.
struct TrainFrame {
    TrainState state;
    std::vector<TrackPose> fronts;
    std::vector<TrackPose> rears;
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
    ActivityEnd activityEnd = ActivityEnd::None;
    double activityEndedAt = -1.0;
};

// The core's state now, with its vehicles' poses; what has not changed
// since `before` (of the same run) shared with it.
std::shared_ptr<SimulationFrame> MakeFrame(const SimulationCore &core, const SimulationFrame *before = nullptr);

}
