// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

// maszyna-mover: MaSzyna's train physics (McZapkie/Mover.cpp, hamulce.cpp, Oerlikon_ESt.cpp, MaSzyna
// 66a940d4) run headless for a comparison scenario (../../README.md), written out as CSV.
//
//   maszyna-mover <scenario.json> <out.csv> [--dynamic DIR] [--frame-dt S] [--friction MU] [--seed N]
//                 [--no-keep-awake] [--no-trainman] [--verbose] [--trace FROM:TO[:EVERY]]
//
// The vehicles are loaded and checked by MaSzyna's own loader, coupled as a scenario couples them,
// prepared as MaSzyna's AI driver prepares a loco, and stepped the way the game steps them each frame
// (train.h). The run's events (relays, slipping, alerter, the trainman) go to stderr.
//
// What isn't MaSzyna (stated here so the comparison knows):
// - the track: straight, one grade, friction 0.15 (TTrack's default; --friction or track.friction to
//   change), quality 20, no damage, no events, no speed limits; the wire everywhere at lineVoltage with no
//   supply resistance (the game's wire sags with current through its power station's resistance);
// - the pantograph arms (part of the 3D model in the game) touch the wire at once while the mover's
//   pantograph is up and its tank pressure and low voltage would raise them, and leave it at once;
// - the frame: 0.01 s (--frame-dt), i.e. the game at 100 fps, one physics step per frame (the game splits
//   a longer frame into steps of up to 0.01 s, with the couplers' gaps extrapolated between them);
// - the hot start: every vehicle created "ready to depart" (what the game does for a train placed in
//   motion), then the loco prepared with the AI's actions (battery, cab, pantographs, reverser forward,
//   main switch with the relay resets, converter, compressor, blowers, brake to running, alerter and cab
//   signal reset) and the train left standing until the pipes and the main hose have equalised and the
//   compressor has filled the main reservoir to its MaxCP; then t=0, and every vehicle given startSpeed;
// - the driver is the player: the scenario's controls go through what the cab's commands call
//   (TTrain::OnCommand_*), the vigilance acknowledged every `acknowledge` s through the alerter button
//   (press and release, ignored with the reverser at 0 as in the cab); of the driver logic the game runs
//   for a player's train only the virtual trainman (Global.AITrainman, default on) is replicated: it
//   releases overcharged brakes at a stand;
// - vehicles without a cab are kept computed at a stand (the game switches their physics off; it switches
//   it back on when air flows in the brake pipe or the main hose); --no-keep-awake gives the game's way;
// - random numbers (brake timing spread, pipe pressure spread of definitions without HiPP) from --seed.

#include "scenario.h"
#include "train.h"

#include "utilities/Globals.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>

namespace {

void usage() {
	std::fprintf(stderr,
	             "usage: maszyna-mover <scenario.json> <out.csv> [--dynamic DIR] [--frame-dt S] [--friction MU] [--seed N]\n"
	             "                     [--no-keep-awake] [--no-trainman] [--verbose] [--trace FROM:TO[:EVERY]]\n"
	             "  --dynamic DIR     MaSzyna's dynamic/ folder (default ~/ORTS/maszyna/dynamic)\n"
	             "  --frame-dt S      game frame time (default 0.01 = 100 fps, one physics step a frame)\n"
	             "  --friction MU     track friction (default track.friction or 0.15, TTrack's default)\n"
	             "  --seed N          simulation random seed (default 1)\n"
	             "  --no-keep-awake   let MaSzyna switch off the physics of standing vehicles without a cab\n"
	             "  --no-trainman     no virtual trainman (Global.AITrainman off)\n"
	             "  --verbose         more events, MaSzyna's log with MASZYNA_MOVER_LOG=1\n"
	             "  --trace A:B[:S]   per-vehicle forces and states every S s (default 0.1) from t=A to t=B, to stderr\n");
}

std::string expand_home(std::string Path) {
	if (!Path.empty() && Path[0] == '~') {
		if (char const *home{std::getenv("HOME")}) {
			Path = std::string(home) + Path.substr(1);
		}
	}
	return Path;
}

std::string number(double Value, int Digits) {
	if (!std::isfinite(Value)) {
		return "";
	}
	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), "%.*f", Digits, Value);
	return buffer;
}

void print_state(train &Train, char const *Title) {
	std::fprintf(stderr, "%s\n", Title);
	for (auto const &v : Train.vehicles()) {
		auto const &mv{*v->mover};
		std::fprintf(stderr,
		             "  %-16s pipe %.3f  cyl %.3f  aux %.3f  ctrl %.3f  main hose %.3f  mr %s  couplers %d/%d",
		             v->name.c_str(), mv.PipePress, mv.BrakePress, mv.Hamulec->GetBRP(), mv.Hamulec->GetCRP(), mv.ScndPipePress,
		             mv.VeselVolume > 0.0 ? number(mv.Compressor, 3).c_str() : "-", mv.Couplers[0].CouplingFlag, mv.Couplers[1].CouplingFlag);
		if (mv.Power > 0.01) {
			std::fprintf(stderr, "  mains %d conv %d comp %s(%d) pant %.2f/%.2f bar V %.0f/%.0f  battery %d 24V %d 110V %d cab %d",
			             mv.Mains, mv.ConverterFlag, mv.CompressorAllow ? "allowed" : "off", mv.CompressorFlag, mv.PantPress, mv.ScndPipePress,
			             mv.PantFrontVolt, mv.PantRearVolt, mv.Battery, mv.Power24vIsAvailable, mv.Power110vIsAvailable, mv.CabActive);
		}
		std::fprintf(stderr, "\n");
	}
	auto const er{Train.driver_valve_chamber()};
	if (std::isfinite(er)) {
		std::fprintf(stderr, "  driver's valve control reservoir %.3f\n", er);
	}
}

} // namespace

int main(int argc, char **argv) {
	std::string scenario_path;
	std::string csv_path;
	harness_options options;
	options.dynamic_dir = expand_home("~/ORTS/maszyna/dynamic");
	std::optional<double> friction;
	double trace_from{-1.0}, trace_to{-1.0}, trace_every{0.1};
	for (int i = 1; i < argc; ++i) {
		std::string const arg{argv[i]};
		auto const value{[&]() -> std::string {
			if (i + 1 >= argc) {
				usage();
				std::exit(2);
			}
			return argv[++i];
		}};
		if (arg == "--dynamic") { options.dynamic_dir = expand_home(value()); }
		else if (arg == "--frame-dt") { options.frame_dt = std::stod(value()); }
		else if (arg == "--friction") { friction = std::stod(value()); }
		else if (arg == "--seed") { options.seed = static_cast<unsigned>(std::stoul(value())); }
		else if (arg == "--no-keep-awake") { options.keep_awake = false; }
		else if (arg == "--no-trainman") { options.trainman = false; }
		else if (arg == "--verbose") { options.verbose = true; }
		else if (arg == "--trace") {
			// --trace FROM:TO[:EVERY]  per-vehicle forces and states to stderr
			auto const text{value()};
			std::sscanf(text.c_str(), "%lf:%lf:%lf", &trace_from, &trace_to, &trace_every);
		}
		else if (arg == "-h" || arg == "--help") { usage(); return 0; }
		else if (!arg.empty() && arg[0] == '-') { usage(); return 2; }
		else if (scenario_path.empty()) { scenario_path = arg; }
		else if (csv_path.empty()) { csv_path = arg; }
		else { usage(); return 2; }
	}
	if (scenario_path.empty() || csv_path.empty() || options.frame_dt <= 0.0) {
		usage();
		return 2;
	}
	while (!options.dynamic_dir.empty() && options.dynamic_dir.back() == '/') {
		options.dynamic_dir.pop_back();
	}

	scenario::definition definition;
	try {
		definition = scenario::load(scenario_path);
	}
	catch (std::exception const &e) {
		std::fprintf(stderr, "scenario %s: %s\n", scenario_path.c_str(), e.what());
		return 1;
	}

	track_conditions track;
	track.friction = friction ? *friction : definition.track_friction ? *definition.track_friction : 0.15;
	track.grade = definition.track_grade / 1000.0;
	track.line_voltage = definition.line_voltage;

	auto const events{[](std::string const &Text) { std::fprintf(stderr, "%s\n", Text.c_str()); }};
	train consist(options, track, events);
	try {
		for (auto const &entry : definition.consist) {
			for (int i = 0; i < entry.count; ++i) {
				consist.add_vehicle(entry.folder, entry.fiz, entry.flipped);
			}
		}
	}
	catch (std::exception const &e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
	{
		auto const &loco{*consist.vehicles().front()->mover};
		if (loco.EngineType == TEngineType::ElectricInductionMotor ||
		    ((loco.EngineType == TEngineType::DieselEngine || loco.EngineType == TEngineType::DieselElectric) && loco.EIMCtrlType > 0)) {
			std::fprintf(stderr, "the driven vehicle needs the induction motor / digital controller blocks of TDynamicObject::Update, not replicated here\n");
			return 1;
		}
	}
	consist.couple();
	print_state(consist, "created (MaSzyna's \"ready to depart\" state):");

	auto const preparation{consist.hot_start()};
	std::fprintf(stderr, "hot start: prepared and settled in %.1f s at a stand\n", preparation);
	print_state(consist, "at t=0:");

	consist.set_speed(definition.start_speed);
	consist.set_origin();

	// the run
	auto const dt{options.frame_dt};
	auto const steps{std::lround(definition.duration / dt)};
	auto const sample_steps{std::max(1L, std::lround(definition.sample / dt))};
	struct scheduled {
		scenario::command command;
		long next_step;
		bool done{false};
	};
	std::vector<scheduled> commands;
	for (auto const &command : definition.commands) {
		commands.push_back({command, std::lround(command.t / dt)});
	}
	long const acknowledge_steps{definition.acknowledge > 0.0 ? std::max(1L, std::lround(definition.acknowledge / dt)) : 0L};

	std::FILE *csv{std::fopen(csv_path.c_str(), "w")};
	if (csv == nullptr) {
		std::fprintf(stderr, "can't write %s\n", csv_path.c_str());
		return 1;
	}
	std::fprintf(csv, "t,v,s,main,actual,second,im,force,brakeForce,pipe,pipeLast,cyl,cylFirst,cylLast,mr,er\n");

	auto &loco{consist.loco()};
	auto const &vehicles{consist.vehicles()};
	bool warned_track_end{false};
	// braking applications and the stands that end them
	struct braking_run {
		double t, s, v;
		std::optional<double> stop_t, stop_s;
	};
	std::vector<braking_run> brakings;
	double max_speed{0.0}, max_im{0.0}, max_force{0.0};

	for (long k = 0; k <= steps; ++k) {
		auto const t{k * dt};
		// the driver's actions due now, in the scenario's order
		for (std::size_t c = 0; c < commands.size(); ++c) {
			auto &item{commands[c]};
			if (item.done || k < item.next_step) {
				continue;
			}
			auto const &command{item.command};
			auto const kind{command.kind};
			if (k == std::lround(command.t / dt)) {
				// a new command takes over a controller from an unfinished ramp
				for (std::size_t o = 0; o < c; ++o) {
					if (!commands[o].done && commands[o].command.kind == kind) {
						commands[o].done = true;
					}
				}
			}
			auto &mv{*loco.mover};
			switch (kind) {
			case scenario::command_kind::direction: {
				auto const direction{static_cast<int>(std::lround(command.value))};
				if (!consist.set_direction(direction)) {
					events("t=" + number(t, 2) + " direction " + std::to_string(direction) + " refused (DirActive " + std::to_string(mv.DirActive) + ")");
				}
				item.done = true;
				break;
			}
			case scenario::command_kind::main_controller:
			case scenario::command_kind::second_controller: {
				auto const main{kind == scenario::command_kind::main_controller};
				auto const target{static_cast<int>(std::lround(command.value))};
				auto const current{[&]() { return main ? mv.MainCtrlPos : mv.ScndCtrlPos; }};
				auto const step{[&](int Towards) { return main ? consist.main_controller_step(Towards) : consist.second_controller_step(Towards); }};
				if (command.every) {
					if (current() != target) {
						auto const from{current()};
						if (!step(target > from ? 1 : -1)) {
							events("t=" + number(t, 2) + " " + scenario::to_string(kind) + " step from " + std::to_string(from) + " refused");
						}
					}
					item.next_step += std::max(1L, std::lround(*command.every / dt));
					item.done = current() == target;
				}
				else {
					for (int guard = 0; guard < 200 && current() != target; ++guard) {
						auto const from{current()};
						if (!step(target > from ? 1 : -1) || current() == from) {
							events("t=" + number(t, 2) + " " + scenario::to_string(kind) + " stuck at " + std::to_string(current()) + " (target " + std::to_string(target) + ")");
							break;
						}
					}
					item.done = true;
				}
				break;
			}
			case scenario::command_kind::train_brake_position: {
				consist.set_train_brake(command.value);
				if (command.value >= 1.0 && mv.Vel > 0.1) {
					brakings.push_back({t, consist.distance(), mv.V * 3.6 * loco.DirectionGet(), std::nullopt, std::nullopt});
				}
				item.done = true;
				break;
			}
			case scenario::command_kind::local_brake_position: {
				consist.set_local_brake(command.value);
				item.done = true;
				break;
			}
			}
		}
		if (acknowledge_steps > 0 && k > 0 && k % acknowledge_steps == 0) {
			consist.acknowledge_vigilance();
		}
		// a row: the state at t, with the controls as just set
		if (k % sample_steps == 0) {
			auto const &mv{*loco.mover};
			auto const &last{*vehicles.back()->mover};
			double brakeforce{0.0};
			for (auto const &v : vehicles) {
				brakeforce += v->mover->Fb;
			}
			auto const nan{std::numeric_limits<double>::quiet_NaN()};
			std::fprintf(csv, "%s,%s,%s,%d,%d,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", number(t, 2).c_str(), number(mv.V * 3.6 * loco.DirectionGet(), 3).c_str(),
			             number(consist.distance(), 3).c_str(), mv.MainCtrlPos, mv.MainCtrlActualPos, mv.ScndCtrlPos, number(mv.Im, 1).c_str(),
			             number(loco.traction_force * loco.DirectionGet() / 1000.0, 3).c_str(), number(brakeforce / 1000.0, 3).c_str(), number(mv.PipePress, 4).c_str(),
			             number(last.PipePress, 4).c_str(), number(mv.BrakePress, 4).c_str(), number(vehicles.size() > 1 ? vehicles[1]->mover->BrakePress : nan, 4).c_str(),
			             number(vehicles.size() > 1 ? last.BrakePress : nan, 4).c_str(), number(mv.VeselVolume > 0.0 ? mv.Compressor : nan, 4).c_str(),
			             number(consist.driver_valve_chamber(), 4).c_str());
		}
		if (k == steps) {
			break;
		}
		consist.frame();

		if (consist.time() >= trace_from - 1e-9 && consist.time() <= trace_to + 1e-9 &&
		    std::lround(consist.time() / dt) % std::max(1L, std::lround(trace_every / dt)) == 0) {
			for (auto const &v : vehicles) {
				auto const &m{*v->mover};
				std::fprintf(stderr,
				             "trace t=%.2f %-14s V %+.6f nrot %+.5f slip %d phys %d FTrain %+10.1f FStand %10.1f Fb %10.1f FTotal %+10.1f CF %+9.1f/%+9.1f "
				             "dist %+.5f/%+.5f AccS %+.5f bcp %.3f pipe %.3f\n",
				             consist.time(), v->name.c_str(), m.V, m.nrot, m.SlippingWheels, m.PhysicActivation, m.FTrain, m.FStand, m.Fb, m.FTotal,
				             m.Couplers[0].CForce, m.Couplers[1].CForce, m.Neighbours[0].distance, m.Neighbours[1].distance, m.AccS, m.BrakePress,
				             m.PipePress);
			}
		}
		auto const &mv{*loco.mover};
		max_speed = std::max(max_speed, mv.Vel);
		max_im = std::max(max_im, std::abs(mv.Im));
		max_force = std::max(max_force, loco.traction_force / 1000.0);
		bool standing{true};
		for (auto const &v : vehicles) {
			standing = standing && std::abs(v->mover->V) * 3.6 < 0.1; // km/h: below this the train counts as standing (see the creep in the report)
		}
		for (auto &run : brakings) {
			if (!run.stop_t && standing) {
				run.stop_t = consist.time() - run.t;
				run.stop_s = consist.distance() - run.s;
				events("t=" + number(consist.time(), 2) + " the train stands: " + number(*run.stop_t, 2) + " s and " + number(*run.stop_s, 1) +
				       " m after the brake application at t=" + number(run.t, 2) + " (from " + number(run.v, 1) + " km/h)");
			}
		}
		if (!warned_track_end && consist.distance() > definition.track_length) {
			warned_track_end = true;
			events("t=" + number(consist.time(), 2) + " warning: the head passed the end of the " + number(definition.track_length, 0) + " m track (the run goes on)");
		}
	}
	std::fclose(csv);

	std::fprintf(stderr, "summary: max %.1f km/h, max motor current %.0f A, max traction %.1f kN, distance %.1f m, trainman releases %d\n", max_speed, max_im, max_force,
	             consist.distance(), consist.trainman_releases());
	print_state(consist, "at the end:");
	return 0;
}
