#pragma once

namespace TsreTests {
// TSRE's Lua states (task scripting 01): the sandbox, each script's own
// globals, errors with file and line, endless loops stopped (also when the
// script catches the error), the memory limit, values saved and restored
// exactly, `require`, `print`, a state on another thread, the simulation's
// repeatable random numbers, state tables kept between calls, numbers
// written with a point whatever the system's language.
int runScriptSuite(bool verbose);
}
