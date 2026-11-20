/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */
#ifndef PEOPLESTEERINGBENCHMARK_H
#define PEOPLESTEERINGBENCHMARK_H

namespace TsreTests {
// The station people's steering (task humans 13): Detour's crowd on a
// made-up 300 m platform with people standing and walking as StationPeople
// steers them, a step's time by how many and how (CPU only).
int runPeopleSteeringBenchmark(bool verbose);
}

#endif
