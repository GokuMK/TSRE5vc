#pragma once

namespace TsreTests {
// Task explore 02: the Explore window's lists read from a game root without
// a loaded route (Explore::Lists) and its trains' table (Explore::Panel on
// an RmlUi context: drivable only, sorting, the filter). Runs on what the
// game root has: EUROPE1 (activities, player paths, consists with an
// engine), PRUDNIK (PSK_2026's trains); neither, nothing checked.
int runExploreListsSuite(bool verbose);
}
