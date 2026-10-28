#pragma once

namespace TsreTests {
// Vehicle files as Open Rails reads them (task driving 01): SimisTree,
// SimisDocument (includes, roots, sharing) and the encodings, on fixtures.
int runVehicleFileSuite(bool verbose);
// Every ENG/WAG file under the game folder read so: problems counted by
// kind, includes, memory, time.
int runVehicleFileCorpusSuite(bool verbose);
}
