#pragma once

namespace TsreTests {
// Task explore 02: runs started from the Explore window's data model (no
// clicks), each with its train driven and the camera riding it, and back
// to the main menu: on --route EUROPE1 its activity lclsrvce, then
// exploring it on a player path on a winter day (the run's first day that
// date, the season winter); on --route PRUDNIK a PSK_2026 train. Needs
// --game-root and --route (none: nothing checked) and a GL surface.
int runExploreStartSuite(bool verbose);
}
