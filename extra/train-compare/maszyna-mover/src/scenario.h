// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

// The comparison scenario (../../README.md, ../../scenarios/*.json), read with a small JSON reader.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace scenario {

struct vehicle_entry {
	std::string folder; // under MaSzyna's dynamic/
	std::string fiz; // its .fiz
	std::string tsre; // its name on the TSRE side (unused here)
	int count{1};
	bool flipped{false};
};

enum class command_kind {
	direction,
	main_controller,
	second_controller,
	train_brake_position,
	local_brake_position,
};

struct command {
	double t{0.0};
	command_kind kind{command_kind::direction};
	double value{0.0}; // target position (or direction)
	std::optional<double> every; // ramp: one position every this many seconds, starting at t
};

struct definition {
	std::string name;
	std::string description;
	double line_voltage{3000.0}; // V at the pantographs
	double track_length{10000.0}; // m
	double track_grade{0.0}; // per mille, + up the way the train runs
	std::optional<double> track_friction; // not in the README format; optional extension, see main.cpp
	std::vector<vehicle_entry> consist;
	double start_speed{0.0}; // km/h
	std::vector<command> commands;
	double acknowledge{20.0}; // s between the driver's vigilance acknowledgements, 0 never
	double duration{60.0}; // s
	double sample{0.5}; // s
};

// throws std::runtime_error with a message naming the problem
definition load(std::string const &Path);

char const *to_string(command_kind Kind);

} // namespace scenario
