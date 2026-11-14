// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

// Stand-in for MaSzyna's vehicle/DynObj.h. Of the vehicle object (TDynamicObject: model, sounds, track
// followers, animations) the physics (McZapkie/Mover.cpp) reaches only for its mover, through the
// neighbour data (Neighbours[end].vehicle->MoverParameters). The harness (src/train.h) derives its
// vehicle from this and replicates there what TDynamicObject does to the mover each frame.

#pragma once

#include "MOVER.h"

class TDynamicObject {
public:
	virtual ~TDynamicObject() = default;
	TMoverParameters *MoverParameters{nullptr};
};
