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
// Activity events in the simulation (task simulation 06): the fixed step,
// each kind of event as Open Rails checks it, levels and chains, the
// activity's end, held messages, triggering trains, wagon lists, restarts.
int runSimulationEventsSuite(bool verbose);
// Trains that change (task simulation 07): loose consists taken along and
// joined, cuts at waiting points, reversing short of cut-off cars,
// attaching again to them.
int runSimulationCouplingSuite(bool verbose);
// The autopilot's work orders planned (task simulation 07, phase B0):
// pickups, set-outs and assembles on two small yards.
int runSimulationWorkSuite(bool verbose);
// Open Rails timetables read (task simulation 08): the rows, columns,
// cells and commands Open Rails knows, groups, the check.
int runTimetableReadSuite(bool verbose);
// The route's Open Rails timetables read and checked (--game-root,
// --route).
int runTimetableRouteSuite(bool verbose);
// The scaling workloads (task simulation 02): the route enlarged with the
// same traffic, more traffic, more trains at one junction, on hand-made
// track; measured.
int runSimulationScalingSuite(bool verbose);
}
