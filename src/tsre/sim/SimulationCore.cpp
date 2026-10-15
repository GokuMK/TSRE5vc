#include <tsre/sim/SimulationCore.h>

#include <algorithm>

namespace Sim {

namespace {

// Phase 1 of the task: every train runs at this speed (the driver follows).
constexpr float ConstantSpeed = 10.0f;

}

SimulationCore::SimulationCore(std::shared_ptr<const SimulationInput> input) : in(std::move(input)) {
    now = in->startTime;
    states.resize(in->trains.size());
    for (size_t i = 0; i < states.size(); ++i)
        states[i].front = in->trains[i].startFront;
}

void SimulationCore::step(double seconds) {
    now += seconds;
    for (size_t i = 0; i < states.size(); ++i) {
        const TrainPlan &plan = in->trains[i];
        TrainState &state = states[i];
        if (state.status == TrainStatus::Finished)
            continue;
        // A train starting within the step moves for the rest of it.
        double moving = seconds;
        if (state.status == TrainStatus::Waiting) {
            if (now < plan.startTime)
                continue;
            moving = std::min(seconds, now - plan.startTime);
            state.status = TrainStatus::Running;
        }
        state.speed = ConstantSpeed;
        double left = state.speed * moving;
        while (left > 0.0 && state.status == TrainStatus::Running) {
            const double end = plan.route.parts[size_t(state.part)].length();
            if (state.front + left < end) {
                state.front += left;
                break;
            }
            left -= end - state.front;
            state.front = end;
            if (state.part + 1 >= int(plan.route.parts.size())) {
                state.status = TrainStatus::Finished;
                state.speed = 0.0f;
                break;
            }
            // A reversal: the old rear leads, the train's length along the
            // next part.
            ++state.part;
            state.reversed = !state.reversed;
            state.front = std::min(double(plan.length), plan.route.parts[size_t(state.part)].length());
        }
    }
}

bool SimulationCore::pose(int train, double routeDistance, TrackPose &out) const {
    if (train < 0 || size_t(train) >= states.size())
        return false;
    const TrainPlan &plan = in->trains[size_t(train)];
    const TrainState &state = states[size_t(train)];
    if (state.part < 0 || size_t(state.part) >= plan.route.parts.size())
        return false;
    const RoutePoint point = plan.route.parts[size_t(state.part)].locate(routeDistance);
    return in->geometry->pose(point.node, point.distance, out);
}

bool SimulationCore::vehicleEnds(int train, std::vector<TrackPose> &fronts, std::vector<TrackPose> &rears) const {
    fronts.clear();
    rears.clear();
    if (train < 0 || size_t(train) >= states.size())
        return false;
    const TrainPlan &plan = in->trains[size_t(train)];
    const TrainState &state = states[size_t(train)];
    if (state.status == TrainStatus::Waiting || state.status == TrainStatus::Finished)
        return false;
    double before = 0.0;
    for (const Vehicle &vehicle : plan.vehicles) {
        // Not reversed: the vehicle's front is `before` behind the train's
        // front. Reversed: the consist runs backwards, its first vehicle at
        // the train's rear.
        const double frontAt = state.reversed ? state.front - plan.length + before : state.front - before;
        const double rearAt = state.reversed ? frontAt + vehicle.length : frontAt - vehicle.length;
        TrackPose front, rear;
        pose(train, frontAt, front);
        pose(train, rearAt, rear);
        fronts.push_back(front);
        rears.push_back(rear);
        before += vehicle.length;
    }
    return true;
}

}
