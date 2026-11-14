// This file is part of TSRE5's train comparison tool (task driving 10): it
// stands in for or follows MaSzyna's code (https://github.com/eu07/maszyna at
// 66a940d4), and so is under the Mozilla Public License 2.0, as MaSzyna's code
// is: this Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

// Stand-in for MaSzyna's precompiled header (stdafx.h in the MaSzyna source root), force-included in
// every translation unit of this tool the way MaSzyna's CMake force-includes its own (PCH).
//
// It brings exactly the standard library and glm the original brings, in the same order, and leaves out
// only the windowing/GL/UI parts the vehicle physics doesn't use (glad, GLFW, imgui, the OpenGL matrix
// stack, the crash reporter).
//
// The trap this header must not fall into: MaSzyna's physics calls unqualified abs() on doubles (e.g.
// hamulce.cpp TLSt local brake flow: abs(S) * ...). In the game's build abs(double) resolves to the
// floating point overload (it has to: lines like std::min(abs(LimPP - CP), 0.05) don't compile with
// int abs()). With libstdc++ the global-namespace floating point overloads come from the C++ wrappers
// <stdlib.h> / <math.h> (which do `using std::abs;`); <cstdlib> / <cmath> alone leave only the C
// int abs(int) in the global namespace, and a pressure difference below 1 bar silently becomes 0.
// So they are included explicitly below, and the static_assert at the end checks it in every unit.

#ifndef STDAFX_H
#define STDAFX_H

#define _USE_MATH_DEFINES
#define NOMINMAX
#include <cmath>
#ifdef __unix__
#include <sys/stat.h>
#endif
// stl
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <array>
#include <vector>
#include <deque>
#include <list>
#include <forward_list>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <tuple>
#include <cctype>
#include <locale>
#include <iterator>
#include <random>
#include <algorithm>
#include <functional>
#include <numeric>
#include <regex>
#include <limits>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <typeinfo>
#include <bitset>
#include <chrono>
#include <optional>
#include <filesystem>
#include <variant>
// global-namespace abs(double)/abs(float), see above
#include <stdlib.h>
#include <math.h>

// glm, configured as in MaSzyna's stdafx.h (SIMD forcing left out: it only changes how glm computes,
// and the physics uses glm in two places, a coupler collision speed check and a vector it stores)
#define GLM_FORCE_SWIZZLE
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_CTOR_INIT
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/string_cast.hpp>

#ifndef PACKED_VEC3_DEFINED
#define PACKED_VEC3_DEFINED
using packed_vec3 = glm::vec<3, float, glm::packed_highp>;
#endif

#define STRINGIZE_DETAIL(x) #x
#define STRINGIZE(x) STRINGIZE_DETAIL(x)

static_assert(std::is_same_v<decltype(abs(0.5)), double>, "abs(double) must not resolve to int abs(int), see stdafx.h");
static_assert(std::is_same_v<decltype(abs(0.5f)), float>, "abs(float) must not resolve to int abs(int), see stdafx.h");

#endif
