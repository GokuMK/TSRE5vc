# Lua

The Lua language, embedded by TSRE's script runtime (`src/tsre/script`)
for the game UI (RmlUi's Lua plugin), loco scripts and signal scripts.

- Version: 5.5.1 (released 2026-07-24)
- Source: https://www.lua.org/ftp/lua-5.5.1.tar.gz
  (SHA-256 `1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce`)
- Files: the archive's `src/` unchanged, without `lua.c` and `luac.c` (the
  stand-alone interpreter and compiler), `lua.hpp` (see below) and its
  `Makefile`
- Licence: MIT, see `LICENSE` (the notice at the end of `lua.h`; the
  archive has no licence file of its own)

TSRE compiles it as C++ (the `tsre_lua` library in `CMakeLists.txt`), so a
Lua error unwinds as a C++ exception and destructors in TSRE's code run.
Include its headers as C++: `<lua.h>`, `<lauxlib.h>`, `<lualib.h>` (through
`src/tsre/script/LuaApi.h`), never `lua.hpp`, whose `extern "C"` would not
match. To update, replace these files with a new release's `src/` (the same
files left out) and update this file.
