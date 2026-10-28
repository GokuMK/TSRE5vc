#pragma once

namespace TsreTests {
// The Consist Editor's load of every ENG/WAG file under the game folder
// (EngLib::loadAll): its time and the process's memory before and after
// (task driving 01, measured on main and the branch alike).
int runVehicleLoadBench(bool verbose);
}
