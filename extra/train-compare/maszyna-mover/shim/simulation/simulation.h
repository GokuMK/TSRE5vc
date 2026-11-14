// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

// Stand-in for MaSzyna's simulation/simulation.h. McZapkie/Mover.cpp includes it for the simulation's
// tables; of them it uses only simulation::Weights (load unit weights), which MOVER.h itself declares
// and Mover.cpp defines. Nothing else is needed here.

#pragma once

#include "utilities/Classes.h"
