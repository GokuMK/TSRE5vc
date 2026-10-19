#pragma once

namespace TsreTests {
// Task core si-units: every ENG, WAG, CON and activity under the game root
// read through TSRE's libraries. (Until phase 2's audit was removed, with
// TSRE_UNITS_AUDIT set the numbers were compared with the old reader's:
// commit 0649fec.)
int runUnitsContentSuite(bool verbose);
}
