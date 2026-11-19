#pragma once
class QString;
namespace TestTrack {
struct Layout;
}
namespace Sim {
struct SimulationFrame;
}
namespace TsreTests {
// simulation-ui's route (SimulationHostTestSuite.cpp): its files under the
// game root `root` (ROUTES/R: the activity "Simulated" starting at 08:00,
// the player's train and an AI train a minute later on the next line;
// TRAINS: an engine and a carriage), false when one is not written; its
// track: two 2 km lines with a platform each, a line through a junction.
// The paths are fitted to the track once loaded.
bool WriteSimulationUiRoute(const QString &root);
void MakeSimulationUiLayout(TestTrack::Layout &layout);
// The simulation's session (task server 04): the frames of runs through
// the controller at fixed times, written as text (TSRE_SIM_BASELINE_OUT)
// to compare a change with; with --game-root and --route, the route's
// activities and timetable sets named in TSRE_SIM_BASELINE_ACTIVITY
// (file names, commas between) instead of simulation-ui's route.
int runSimulationSessionSuite(bool verbose);
// Everything a frame says of the run, as text (simulation-session's
// baseline): but what a run's place in the process gives it (its id, the
// host's ticks, when it was published, the commands taken) and the rate.
QString SimulationFrameText(const Sim::SimulationFrame &frame);
// The simulation on the wire (task server 05): a run's data written by
// Save's writers and read back as a client reads it makes the same input
// (simulation-ui's route; with --game-root and --route, the activities and
// timetable sets named in TSRE_SIM_BASELINE_ACTIVITY); frames encoded and
// decoded.
int runSimulationWireSuite(bool verbose);
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
// A timetable set's trains (task simulation 08, phase 3) made and run on
// hand-made track: paths, consists, stops, a static train.
int runTimetableTrainsSuite(bool verbose);
// The Open Rails timetable commands TSRE runs (task simulation 08, phase
// 4), each on hand-made track: holds and signal waits, $create, $static,
// $triggers, $follow, $acc and $dec, midnight.
int runTimetableCommandsSuite(bool verbose);
int runTimetableMoreCommandsSuite(bool verbose);
// The scaling workloads (task simulation 02): the route enlarged with the
// same traffic, more traffic, more trains at one junction, on hand-made
// track; measured.
int runSimulationScalingSuite(bool verbose);
// Level crossings (task traffic 01): the core closing and opening them by
// Open Rails' rule on hand-made track, their values, standing consists,
// frames; and one lane's cars (LaneTraffic) queuing at a closed one.
int runLevelCrossingsSuite(bool verbose);
int runRoadCarsSuite(bool verbose);
}
