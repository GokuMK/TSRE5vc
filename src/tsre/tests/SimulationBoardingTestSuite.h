#ifndef SIMULATIONBOARDINGTESTSUITE_H
#define SIMULATIONBOARDINGTESTSUITE_H

namespace TsreTests {
// What boarding in the simulation (task humans 06) changes on a route's
// activities and timetables: each run without the stops' people and with
// them, compared (--game-root, --route).
int runSimulationBoardingSuite(bool verbose);
}

#endif
