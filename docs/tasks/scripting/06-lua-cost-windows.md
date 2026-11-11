# Task 06 - Lua's cost on Windows

Status (2026-11-11): on `feature/lua-watchdog-windows`, merged into main;
measured on the Steam Deck (Windows 10, MinGW Release). The next steps
below are not done.

## The question (the user, 2026-11-11)

A loco's step cost 2 to 3 times as much on Windows as on Linux (task
maszyna 06's numbers, a Xeon E5-1650 v3 of about the same speed a core).
If TSRE's loco scripts are to be Lua, Lua has to be fast there too.

## Measured

`driving` and `maszyna-scripts`' step probes (20,000 steps at half
throttle, 36 km/h, the cab on), µs a step, the median of three
interleaved runs:

| | Windows before | the watchdog | and `require`'s cache | Linux |
| --- | --- | --- | --- | --- |
| ET22 (MaSzyna) | 125.5 | 99.5 | 49.0 | 41–42 |
| `electric.lua` | 75.2 | 66.4 | 20.8 | 22–24 |
| `diesel.lua` | 44.6 | 31.0 | 31.3 | 22–24 |
| `steam.lua` | 33.7 | 26.1 | 25.5 | 22–24 |

- **The watchdog** (driving task 02, open question 6): Lua without the
  clock's hook, as on Linux; 1.13 to 1.44 times faster.
- **`require`** looked for the module's file (`QFileInfo::isFile`, one
  `GetFileAttributesExW` a folder) before its cache, at every call.
  `tsre.electricloco` requires its motor's module every step
  (`require(s.config.motor or "tsre.traction")`), so the ET22 and
  `electric.lua` asked the file system each step: 44 % of the ET22's step
  (Very Sleepy), tens of µs on Windows against about one on Linux. Now a
  module required before is found by its name first; new module folders
  look again. The diesel and steam scripts never required in a step:
  unchanged.

## What a step is made of now (the ET22, Very Sleepy)

- Lua interpreting, about 60 %: `luaV_execute` 30 % alone, field look-ups
  by name (`luaH_getshortstr`) 13.5 %, calls.
- TSRE copying the step's values out of Lua, about 17 %: the values' hash
  (`QHash::emplace`), `nameOf`, `messageValue`, `lua_next`.
- Tables and strings made every step, 8 to 10 %: `luaH_newkey`,
  `luaH_resize`, `luaV_concat` (a string joined each step), most of the
  allocations.
- The allocator (`msvcrt`'s `malloc` and `realloc`, the Windows heap), 6
  to 9 %: not the cause first suspected.

## Next, by gain (not done)

1. The values: copied only when a frame is made, or only the changed ones
   (driving task 02 noted the interface change; now about 8 µs of the
   ET22's 49).
2. The scripts' per-step tables and joined strings: made once, reused
   (the `sound.*` keys and the like).
3. A small-block allocator for Lua's states: a few percent.

Lua 5.5 has no JIT; the interpreter's share stays.

## Checks

`script` 37 (new: a module required again comes from memory, its file
deleted; new module folders look again), `driving` 69, `rmlui` 43,
`mmd-cab-script` 20, `maszyna-scripts` 10 of 11 (the 11th needs the whole
pack), six times each with and without the watchdog; build `all`, ctest
11.

How the profile was made: `-g1` for the `tsre_lua` target and for
`LocoScript.cpp`, `ScriptState.cpp` and `VehicleModel.cpp` only (a
temporary CMake line, 42 s to rebuild, the code unchanged), the ET22's
probe made 800,000 steps long, Very Sleepy attached for 25 s
(`sleepy-report.py --root LuaLoco`).
