#pragma once

namespace TsreTests {
// Task server 01: the server and client mode on a tiny route made for the
// test: a server process, scripted clients and a real client.
int runServerSuite(bool verbose);
// Task server 05: a run on the server (a server process on a route given by
// --game-root and --route, the activity or timetable set named in
// TSRE_SIM_BASELINE_ACTIVITY), shown on a real client and a scripted one:
// the client's frames against a local run's, driving, hearing, joining,
// reconnecting, the run's end, the bytes a second.
int runServerSimulationSuite(bool verbose);
}
