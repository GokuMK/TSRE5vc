// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

// Stand-in for MaSzyna's utilities/Globals.h: the game's settings, cut down to the members the compiled
// MaSzyna sources (McZapkie/*.cpp, utilities/parser.cpp, utilities/utilities.cpp) and this tool read.
// Types and defaults are copied from the original (MaSzyna 66a940d4); the defaults are the game's
// values before eu07.ini is read, which is what the physics runs with unless eu07.ini changes them.

#pragma once

#include "utilities/Classes.h"
#include "utilities/utilities.h"
#include <map>
#include <random>
#include <string>

struct global_settings {
	// simulation randomness (the game seeds both from the session seed, see main.cpp --seed)
	std::mt19937 random_engine;
	std::mt19937 local_random_engine;
	uint32_t random_seed = 0;

	int iPause{0}; // globalna pauza ruchu: b0=start,b1=klawisz,b2=tło,b3=lagi,b4=wczytywanie
	float AirTemperature{15.f};
	std::string asCurrentSceneryPath{"scenery/"};

	bool file_binary_terrain_state{true};
	int file_binary_terrain_skipped{0};
	bool ParserLogIncludes{true};

	// simulation
	float fFriction{1.f}; // mnożnik tarcia - KURS90
	float FrictionWeatherFactor{1.f};
	bool bLiveTraction{true};
	bool FullPhysics{true}; // full calculations performed for each simulation step
	bool AITrainman{true}; // virtual assistant performing consist coupling/decoupling and other maintenance tasks
	bool crash_damage = true;
};

extern global_settings &GetGlobalSettings();

#define Global (GetGlobalSettings())
