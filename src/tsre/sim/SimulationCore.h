#pragma once
#include <memory>
#include <vector>
#include <tsre/sim/SimulationInput.h>

namespace Sim {

enum class TrainStatus { Waiting, Running, Stopped, Finished };
// Why a train stands.
enum class StopKind { None, Station, Wait, Reversal, End };

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
};

// Something that happened to a train, for the editor (task simulation 01,
// section 6).
struct TrainEvent {
    enum Kind { Started, Arrived, Departed, Waiting, Reversed, Horn, Finished };
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

    // Trains that started before the activity are run up to its start (the
    // pre-run).
    explicit SimulationCore(std::shared_ptr<const SimulationInput> input);

    const SimulationInput &input() const { return *in; }
    std::shared_ptr<const SimulationInput> inputPointer() const { return in; }
    // Seconds after midnight.
    double time() const { return now; }
    // Advances the simulation by `seconds`, in steps of at most MaxStep.
    void step(double seconds);
    const std::vector<TrainState> &trains() const { return states; }
    // The events since the last call.
    std::vector<TrainEvent> takeEvents();
    // The two ends of each of a train's vehicles, in the consist's order:
    // the vehicle's front (as the consist lists it) and its rear. False for
    // a train not on the track (not started, or an AI train gone at its
    // path's end).
    bool vehicleEnds(int train, std::vector<TrackPose> &fronts, std::vector<TrackPose> &rears) const;
    // Where a route distance of a train's current part is.
    bool pose(int train, double routeDistance, TrackPose &out) const;
    // Every train has finished its path.
    bool finished() const;

private:
    std::shared_ptr<const SimulationInput> in;
    double now = 0.0;
    std::vector<TrainState> states;
    std::vector<TrainEvent> events;

    // One train from `from` to `to` (seconds after midnight, at most
    // MaxStep apart).
    void advance(int train, double from, double to);
    void record(double time, int train, TrainEvent::Kind kind, int index = -1);
};

}
