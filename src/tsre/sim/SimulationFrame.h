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
};

// The core's state now, with its vehicles' poses.
std::shared_ptr<SimulationFrame> MakeFrame(const SimulationCore &core);

}
