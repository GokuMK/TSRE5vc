/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef LUAAPI_H
#define LUAAPI_H

// Lua's C API. TSRE compiles Lua as C++ (thirdparty/lua/README.md), so its
// headers are included as C++, never through lua.hpp's extern "C".
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#endif // LUAAPI_H
