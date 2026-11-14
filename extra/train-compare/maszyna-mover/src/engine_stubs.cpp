// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

// Stand-ins for the parts of the MaSzyna engine its physics links against but this tool doesn't run:
// settings storage, the log, scenery layer bookkeeping of the parser. The same set MaSzyna's own unit
// tests stub (MaSzyna tests/unit/engine_stubs.cpp).

#include "stdafx.h"
#include "utilities/Globals.h"
#include "utilities/Logs.h"
#include "scene/scenenodegroups.h"
#include "scene/scenelayers.h"

#include <cstdio>
#include <cstdlib>

global_settings &GetGlobalSettings() {
	// the game's defaults, as before reading eu07.ini
	static global_settings settings;
	return settings;
}

// the engine log: the game writes it to log.txt; here to stderr when MASZYNA_MOVER_LOG is set, else only
// errors
std::deque<std::string> log_scrollback;

namespace {
bool log_enabled() {
	static bool const enabled{std::getenv("MASZYNA_MOVER_LOG") != nullptr};
	return enabled;
}
} // namespace

void WriteLog(const char *str, logtype const, bool const isError) {
	if (str != nullptr && (log_enabled() || isError)) {
		std::fprintf(stderr, "%s%s\n", isError ? "[maszyna error] " : "[maszyna] ", str);
	}
}

void WriteLog(const std::string &str, logtype const Type) {
	WriteLog(str.c_str(), Type, false);
}

void ErrorLog(const std::string &str, logtype const Type) {
	WriteLog(str.c_str(), Type, true);
}

void Error(const std::string &asMessage, bool) {
	WriteLog(asMessage.c_str(), logtype::generic, true);
}

void Error(const char *&asMessage, bool) {
	WriteLog(asMessage, logtype::generic, true);
}

void CommLog(const char *) {}
void CommLog(const std::string &) {}
void LogService() {}

namespace scene {

node_groups Groups;
node_layers Layers;

group_handle node_groups::create(bool const) {
	return null_handle;
}

group_handle node_groups::close() {
	return null_handle;
}

layer_handle node_layers::open(std::string const &, include_site) {
	return null_handle;
}

layer_handle node_layers::close() {
	return null_handle;
}

basic_layer &node_layers::layer(layer_handle const) {
	static basic_layer none;
	return none;
}

void node_layers::template_used(std::string const &) {}

void node_layers::instance_begin(std::string const &, source_span const &) {}

void node_layers::instance_end() {}

instance_handle node_layers::instance() const {
	return 0;
}

} // namespace scene
