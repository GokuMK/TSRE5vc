#pragma once
namespace TsreTests {
// The simulation core (task simulation 01) on hand-made track: its copy of
// the track geometry against the track database, train routes from paths,
// trains moving along them.
int runSimulationCoreSuite(bool verbose);
// The simulation on a route (--game-root, --route): the geometry copy at
// random places of the whole track database, every activity's input made
// and run to its end.
int runSimulationRouteSuite(bool verbose);
// The simulation's thread (SimulationHost): commands acknowledged in order,
// frames and events, its pace, the same events as the core run alone, stop
// with commands queued.
int runSimulationHostSuite(bool verbose);
// The simulation in the editor: the activity panel's controls, activity
// editing off while it exists, undo, the Timetable window's states, events
// and Follow, Run to, Stop.
int runSimulationUiSuite(bool verbose);
}
