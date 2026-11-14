// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

// Stand-in for MaSzyna's scene/scenenodegroups.h, for utilities/parser.cpp: the parser opens a node
// group around the content of an included *.inc file (scenery editor bookkeeping). Vehicle definitions
// don't use it; the stubs (src/engine_stubs.cpp) do nothing.

#pragma once

#include "utilities/Classes.h"
#include "utilities/Globals.h" // the original reaches the settings and helpers through its own includes; parser.cpp relies on that

#ifndef null_handle
#define null_handle (0)
#endif

namespace scene {

class node_groups {
public:
	group_handle create(bool const Separate = false);
	group_handle close();
};

extern node_groups Groups;

} // namespace scene
