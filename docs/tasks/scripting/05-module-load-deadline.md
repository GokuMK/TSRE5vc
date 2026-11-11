# Task 05 - A loco script's modules and the call's time limit

Status (2026-11-11): cause found, fixed on `fix/lua-load-deadline`,
merged into main; tested on CMK by the harness, to confirm live by the
user.

## The report (the user, 2026-11-11)

After main took the remote MaSzyna sounds and engines (tasks maszyna 05,
06; `3c1655e7`), nothing reacted in CMK's New Activity2 (`file2.act`, the
ET22 MaSzyna): no cab button, no HUD control. The log:

```
[script] the script ran longer than its limit of 100 ms
	tsre/dieselengine.lua:27: in main chunk
	maszyna/auxiliaries.lua:39: in main chunk
	electric-resistor-mmd.lua:12: in main chunk
[script] maszyna/cab.lua:100: attempt to index a nil value (field 'config')
```

## What happens

- A script's load is a call: its chunk runs under the 100 ms limit
  (`Limits::callMs`), and so did every module it requires, the module's
  file read and compiled inside the call. The script's own file is read
  and compiled before its call starts.
- Task maszyna 06 made `electric-resistor-mmd.lua` require
  `maszyna.auxiliaries` and `maszyna.sounds`: 18 modules, 195 KB, where it
  had required 2.
- On Windows with the files out of the cache (the first run after a long
  build) reading them took most of the 100 ms; past it the load failed.
  The line in the traceback is where the clock was looked at, not slow Lua
  (line 27 sets a constant).
- `LocoScripts::load` keeps a failed load for the run, so the ET22 had no
  model: its values missing, commands going nowhere. `cab.lua`'s error
  follows: the loco's `init` never ran.
- Also: a module whose loading failed stayed marked as loading, so any
  later `require` of it said "requires itself".

## Measured (Release, the Deck, `electric-resistor-mmd.lua`)

| Run | Load | 18 modules read | compiled |
| --- | --- | --- | --- |
| CMK, first run after a 7-minute build | 92.4 ms | 76.6 ms | 8.4 ms |
| CMK, warm (5 runs) | 15 to 27 ms | 2 to 10 ms | 7 to 9 ms |
| the user's run after the merge's build | over 100 ms, failed | | |

- What runs as Lua in the load (the modules' chunks): about 5 ms. `init`:
  2 to 3 ms (37 ms once, with the route loading beside it). The vehicle
  table (`.mmd` sounds read on first use): 22 to 30 ms, before the call.
- The same exe, main's, on the `maszyna-scripts` suite: 9 of 11 checks
  failed with the files cold (the ET22's timeout), 10 of 11 passed a
  minute later (the 11th needs the whole pack).

## The fix

- `require` gives the time it spends reading and compiling a module back
  to the deadline (`State::giveBack`), as a script's own file is read and
  compiled before its call. Running Lua still counts: a module's chunk, a
  script's chunk, endless loops after a load.
  - Nested deadlines: the outer one keeps the time given back while an
    inner one ran.
  - With the watchdog (not Windows): a hook it set while a module compiled
    is taken off again and the new end watched.
- After a failed outermost run, modules left marked as loading are
  forgotten (`State::forgetUnfinishedModules`): required again they load
  again (Lua's own `require` does the same).
- `Loco scripts: <file> loaded in N ms; M modules read in R ms, compiled
  in C ms`, once a script a run.

## Checks

- `script` (34): a module compiled in 77 ms under a 20 ms limit loads and
  a loop after it is still stopped (the old behaviour fails it, as the
  ET22's load); a module failing as a script loads fails the same when
  required again (without the fix: "requires itself").
- `driving` 69, `rmlui` 43, `mmd-cab-script` 20, `maszyna-scripts` 10 of
  11 (the pack check needs over 600 engines; this machine has 3, all
  without script errors); build `all`, ctest 11 of 11.
- CMK New Activity2 in the renderer harness, every module read slowed by
  10 ms (a test switch, removed): before the fix the user's log line for
  line and the ET22 without values; with it loaded in 284 to 418 ms, the
  pantographs up on their commands, direction and controller taken.
- The harness's views take `"logValues"`: the driven train's cab vehicle's
  values after the view's run.

## Why the Linux runs passed

- **Reading:** 18 small files from Linux's page cache take well under a
  millisecond; on Windows 2 to 10 ms warm, 77 ms after a long build.
- **Lua itself:** Windows still ran every call under the count hook (the
  Windows watchdog of driving task 02, open question 6, built after this
  task): the VM traps every instruction, a loco's step 1.13 to 1.44 times
  as long (measured on the Deck). In this load that is the modules' chunks, about 5
  ms on Windows, so a couple of ms; reading and compiling do not go
  through the VM.
- **The checks:** task maszyna 06's are the `maszyna-scripts` suite, one
  warm load in a quiet process; the only real route drive of the ET22
  (task maszyna 05, USA2's test root) came before `maszyna.auxiliaries`.

## Found alongside (task maszyna 06; decided, see the end)

A MaSzyna electric's main switch closes only with the pantograph voltage
over half its MaxVoltage (`maszyna.auxiliaries`, `mayClose`), from the
route's `MaxLineVoltage`. Of the routes in `C:/trainsim` only Prudnik
gives one; CMK (`Electrified ( 1 )`) does not, so the ET22's main switch
stays open there. Open Rails reads a missing `MaxLineVoltage` as 0 too,
but its default electric supply does not look at the voltage to close the
breaker; TSRE's ENG scripts follow it. The suite sets 3000 V.

The user's quick fix (2026-11-11): `MaxLineVoltage ( 3000 )` in CMK.trk.
With it, on this branch, New Activity2's ET22: the pantographs up, the line
at 3420 V, the main switch closed, main power; the controller at 6, 61 A,
13.5 kN, 39 to 46 km/h in 10 s.

Decided in task maszyna 07's merge (`8c3d8284`, 2026-11-11): the route's
`MaxLineVoltage` where the loco runs on it (half its MaxVoltage to
MaxVoltage + 200 V), else MaSzyna's 0.95 × MaxVoltage with one log line,
so MaSzyna electrics run on routes without the key (dk28 and the like), as
ENG locos do.
