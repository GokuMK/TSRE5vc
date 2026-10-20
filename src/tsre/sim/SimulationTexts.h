#pragma once
#include <QString>
#include <tsre/sim/SimulationCore.h>

namespace Sim {

// Texts about a simulation's trains for the editor, from its input: what a
// train is doing ("running 72 km/h", "at Settle until 08:31") at `time`; a
// train as the Timetable window names it ("Local 09:30"); a section's
// holder (a train, a standing consist); a signal as the editor names it
// (its TDB item); why a train waits ("at signal 1234: the track beyond is
// held by ...").
QString StateText(const SimulationInput &in, const TrainState &state, double time, int train);
QString TrainName(const SimulationInput &in, int train);
QString HolderName(const SimulationInput &in, int holder);
QString SignalName(const SimulationInput &in, int signal);
QString WaitText(const SimulationInput &in, WaitFor waitFor, int signal, int holder);
// Why the input's event `event` did not fire in a run (task 06, section
// 7), from its state where the run ended and the activity's end.
QString WhyNotFired(const SimulationInput &in, int event, const EventState &state, ActivityEnd end, double endedAt);

}
