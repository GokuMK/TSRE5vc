#pragma once

namespace TsreTests {
// MaSzyna's brakes behind TSRE's interface (task driving 10, step 4): the
// factory's choice per vehicle, each ported distributor on a test stand
// (charged, a 1.5 bar reduction, release, emergency, the R relay by speed,
// the local brake, EP), the brake force against MaSzyna's formula, and the
// pipe's MaSzyna law (air conserved, its linearisation against MaSzyna's
// explicit flow). Its vehicles from MaSzyna's pack in the game root's
// dynamic/: skipped without it.
int runMaszynaBrakesSuite(bool verbose);
}
