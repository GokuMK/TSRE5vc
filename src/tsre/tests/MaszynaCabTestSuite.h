#pragma once

namespace TsreTests {
// MaSzyna's cabs in .mmd (task maszyna 04) on small files built here: the
// cabs' ranges, the header's order, both syntaxes, lamps, the special
// labels, entries cut short.
int runMmdCabSuite(bool verbose);
// Every vehicle's cabs under the game root's dynamic/: no problems, the
// review's counts, ET22's cabs, the time.
int runMmdCabCorpusSuite(bool verbose);
// ET22's cabs from the pack in the game root's dynamic/: its definition's
// viewpoints and controls, and its loco script's values, stops and
// commands through maszyna.cab; ST44's fuel pump switch and start button.
int runMmdCabScriptSuite(bool verbose);
}
