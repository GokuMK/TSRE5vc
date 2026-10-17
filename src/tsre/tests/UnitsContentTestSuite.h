#pragma once

namespace TsreTests {
// Task core si-units: every ENG, WAG, CON and activity under the game root
// read through TSRE's libraries (with TSRE_UNITS_AUDIT, the numbers they
// read compared with the old reader's).
int runUnitsContentSuite(bool verbose);
}
