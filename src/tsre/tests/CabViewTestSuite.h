#pragma once

namespace TsreTests {
// Open Rails' 3D cabs' .cvf files (task driving 08): the controls read,
// the file found as Open Rails finds it, the cab shape's parts matched to
// the controls; on fixtures.
int runCabViewSuite(bool verbose);
// The same over the game folder's 3D cabs (--game-root): each ENG's cab,
// its .cvf and its shape's parts, counted.
int runCabViewCorpusSuite(bool verbose);
}
