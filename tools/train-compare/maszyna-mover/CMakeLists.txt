# This file is part of TSRE5's train comparison tool (task driving 10): it
# stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
# 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
# is: this Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# maszyna-mover: MaSzyna's own train physics (its "mover", McZapkie/Mover.cpp and the brakes) run
# headless for a scripted scenario (../scenarios/*.json), written out as CSV (../README.md), as the
# MaSzyna side of the TSRE/MaSzyna comparison runs.
#
# A tool of its own, not part of TSRE's build:
#   cmake -S tools/train-compare/maszyna-mover -B <build dir> -DCMAKE_BUILD_TYPE=Release
#   cmake --build <build dir>
#   <build dir>/maszyna-mover tools/train-compare/scenarios/<name>.json tools/train-compare/out/maszyna/<name>.csv \
#       [--dynamic ~/ORTS/maszyna/dynamic]
#
# MaSzyna's sources (MPL-2.0) are compiled in place from MASZYNA_SOURCE and never modified. The parts of
# the game the physics includes but doesn't need (vehicle object, simulation, settings, scenery layers)
# are replaced by the headers in shim/, which come first on the include path, and src/engine_stubs.cpp.

cmake_minimum_required(VERSION 3.20)
project(maszyna_mover CXX)

set(CMAKE_CXX_STANDARD 20) # as MaSzyna's own build
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(MASZYNA_SOURCE "/home/arch/NetBeansProjects/maszyna" CACHE PATH "MaSzyna source checkout (compiled in place, read only)")
if(NOT EXISTS "${MASZYNA_SOURCE}/McZapkie/Mover.cpp")
	message(FATAL_ERROR "MASZYNA_SOURCE=${MASZYNA_SOURCE} has no McZapkie/Mover.cpp")
endif()

# glm (header only), which MaSzyna's headers use: an explicit GLM_INCLUDE_DIR, else MaSzyna's own submodule
# if it's checked out, else the commit that submodule pins (ref/glm @ adf31f55, glm 1.0.0) fetched once
set(GLM_INCLUDE_DIR "" CACHE PATH "Directory holding glm/glm.hpp (empty: MaSzyna's ref/glm, or fetched)")
if(GLM_INCLUDE_DIR STREQUAL "")
	if(EXISTS "${MASZYNA_SOURCE}/ref/glm/glm/glm.hpp")
		set(GLM_INCLUDE_DIR "${MASZYNA_SOURCE}/ref/glm")
	else()
		include(FetchContent)
		# headers only: SOURCE_SUBDIR names a folder without a CMakeLists.txt, so glm's own project isn't added
		FetchContent_Declare(glm_headers
			URL https://github.com/g-truc/glm/archive/adf31f555e73e2bd6fda373bb5d8740f9c6c17c0.tar.gz
			URL_HASH SHA256=41937430c6a67dd884887adfb914ecb501f4b01423e6fcbc4055dde4ffa4d0ad
			DOWNLOAD_EXTRACT_TIMESTAMP TRUE
			SOURCE_SUBDIR headers-only)
		FetchContent_MakeAvailable(glm_headers)
		set(GLM_INCLUDE_DIR "${glm_headers_SOURCE_DIR}")
	endif()
endif()
message(STATUS "maszyna-mover: MaSzyna source ${MASZYNA_SOURCE}, glm ${GLM_INCLUDE_DIR}")

# the physics and the helpers it needs: MaSzyna's eu07_core library minus vehicle/handover.cpp (AI driver
# logic, not used here)
set(MASZYNA_CORE_SOURCES
	"${MASZYNA_SOURCE}/McZapkie/friction.cpp"
	"${MASZYNA_SOURCE}/McZapkie/hamulce.cpp"
	"${MASZYNA_SOURCE}/McZapkie/Mover.cpp"
	"${MASZYNA_SOURCE}/McZapkie/Oerlikon_ESt.cpp"
	"${MASZYNA_SOURCE}/utilities/parser.cpp"
	"${MASZYNA_SOURCE}/utilities/utilities.cpp"
	"${MASZYNA_SOURCE}/scripting/ladderlogic.cpp")

add_library(maszyna_core STATIC ${MASZYNA_CORE_SOURCES} src/engine_stubs.cpp)
target_include_directories(maszyna_core PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/shim" # first: stands in for the game's headers of the same names
	"${MASZYNA_SOURCE}"
	"${MASZYNA_SOURCE}/McZapkie"
	"${MASZYNA_SOURCE}/global_include")
target_include_directories(maszyna_core SYSTEM PUBLIC "${GLM_INCLUDE_DIR}")
# MaSzyna force-includes its stdafx.h in every unit (precompiled header); the stand-in the same way
target_precompile_headers(maszyna_core PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/shim/stdafx.h>")
# MaSzyna's own sources: their warnings are theirs
set_source_files_properties(${MASZYNA_CORE_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

add_executable(maszyna-mover src/main.cpp src/train.cpp src/scenario.cpp)
target_link_libraries(maszyna-mover PRIVATE maszyna_core)
target_include_directories(maszyna-mover PRIVATE src)
target_compile_options(maszyna-mover PRIVATE -Wall -Wextra -Wno-unused-parameter)
