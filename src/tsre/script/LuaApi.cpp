/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/script/LuaApi.h>
// Lua's internals, in a file of their own: their macros (cast and others)
// clash with Qt's names.
#include <lstate.h>

namespace Script {

// A coroutine's error with no handler of its own goes on to the main
// thread's (luaD_throw), so the main thread's handler is the one that counts.
bool inProtectedCall(lua_State *L) {
    return mainthread(G(L))->errorJmp != nullptr;
}

}
