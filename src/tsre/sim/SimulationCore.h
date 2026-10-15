#pragma once
#include <memory>
#include <vector>
#include <tsre/sim/SimulationInput.h>

namespace Sim {

enum class TrainStatus { Waiting, Running, Stopped, Finished };

// A train's state in the simulation.
struct TrainState {
    TrainStatus status = TrainStatus::Waiting;
    // The part of its route it is on, and its front's route distance there.
    int part = 0;
    double front = 0.0;
    float speed = 0.0f;
    // The train runs the other way from how its consist lists its vehicles
    // (after an odd number of reversals): the last vehicle leads.
    bool reversed = false;
};

// The simulation's state and its stepping: plain C++, no QObject, nothing
// but its input read, so tests call it directly and the host runs it on its
// own thread.
class SimulationCore {
public:
    explicit SimulationCore(std::shared_ptr<const SimulationInput> input);

    const SimulationInput &input() const { return *in; }
    // Seconds after midnight.
    double time() const { return now; }
    // Advances the simulation by `seconds`.
    void step(double seconds);
    const std::vector<TrainState> &trains() const { return states; }
    // The two ends of each of a train's vehicles, in the consist's order:
    // the vehicle's front (as the consist lists it) and its rear. False for
    // a train not on the track (waiting or finished).
    bool vehicleEnds(int train, std::vector<TrackPose> &fronts, std::vector<TrackPose> &rears) const;
    // Where a train's route distance is on its current part.
    bool pose(int train, double routeDistance, TrackPose &out) const;

private:
    std::shared_ptr<const SimulationInput> in;
    double now = 0.0;
    std::vector<TrainState> states;
};

}
