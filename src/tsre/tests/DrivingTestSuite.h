#pragma once

namespace TsreTests {
// The driving harness (task driving 02): the motion of a driven train's cars
// and couplers (TrainPhysics), its vehicle models, the core driving a train
// by them, on fixtures.
int runDrivingSuite(bool verbose);
// Every electric and diesel engine of the game folder (Game::root) through
// TSRE's electric.lua and diesel.lua: errors, power, force.
int runDrivingCorpusSuite(bool verbose);
}
