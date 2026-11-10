#pragma once

namespace TsreTests {
// MaSzyna's .fiz (task maszyna 03) on small files built here: sections, keys,
// tables and their ends, includes, comments, the SI conversion.
int runFizFileSuite(bool verbose);
// Every .fiz under the game root's dynamic/ read: no problems, the review's
// engine counts, the time.
int runFizCorpusSuite(bool verbose);
// MaSzyna's locos on their Lua scripts (task maszyna 03), from the pack in
// the game root's dynamic/: ET22 (series motors: start, the force against
// MaSzyna's), EU44 (induction motors), an EN57 unit.
int runMaszynaScriptsSuite(bool verbose);
}
