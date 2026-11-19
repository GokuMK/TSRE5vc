#include <tsre/sim/SimulationLink.h>

namespace Sim {

SimulationLink::Taken LocalSimulationLink::take() {
    Taken taken;
    taken.frame = own.update();
    // A new run: its events from its first.
    if (own.runId() != takenRun) {
        takenRun = own.runId();
        takenEvents = 0;
    }
    const std::vector<TrainEvent> &events = own.events();
    if (takenEvents > events.size())
        takenEvents = 0;
    taken.events.assign(events.begin() + std::ptrdiff_t(takenEvents), events.end());
    takenEvents = events.size();
    return taken;
}

}
