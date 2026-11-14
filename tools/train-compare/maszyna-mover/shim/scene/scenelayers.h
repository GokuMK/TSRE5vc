// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

// Stand-in for MaSzyna's scene/scenelayers.h, for utilities/parser.cpp: scenery layer bookkeeping of the
// scenery editor, active only when a parser has sceneryLayers set (never for vehicle definitions). The
// declarations match the original's; the stubs (src/engine_stubs.cpp) do nothing.

#pragma once

#include "utilities/Classes.h"
#include <ios>
#include <string>

#ifndef null_handle
#define null_handle (0)
#endif

namespace scene {

struct source_span {
	std::streamoff begin{-1};
	std::streamoff end{-1};
	bool valid() const { return begin >= 0 && end > begin; }
};

struct include_site {
	layer_handle parent{null_handle};
	source_span span;
	bool parameters{false};
	bool fixed{false};
};

struct basic_layer {
	std::string name;
	bool binary{false};
};

class node_layers {
public:
	layer_handle open(std::string const &Name, include_site Site);
	layer_handle close();
	basic_layer &layer(layer_handle const Layer);
	void template_used(std::string const &Name);
	void instance_begin(std::string const &File, source_span const &Span);
	void instance_end();
	instance_handle instance() const;
};

extern node_layers Layers;

} // namespace scene
