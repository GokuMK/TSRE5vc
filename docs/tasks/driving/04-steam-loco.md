# Task 04 - The steam loco

Status: started 2026-10-29 on `feature/driving` (steps 1 and 2 done).

The user's (2026-10-29), after the review
[reports/or-steam-loco-review.md](reports/or-steam-loco-review.md):
- "Idea of selected steam modules in c++ sounds great";
- "Proceed."

**The design** (the review's):
- **`steam.lua`** is the loco's script, on the shared Lua modules
  (`controls`, `levers`, `brakes`, `supply`), as the diesel is: the cab's
  controls, commands, values, sounds, brakes.
- **Its thermodynamics are C++,** a native module built into TSRE,
  `require "tsre.steam"`:
  - boiler, firebox and firing, cylinders, the force from the crank angles,
    injectors, superheat, auxiliaries' steam, tender;
  - the steam tables;
  - the ENG's keys with Open Rails' defaults and estimates.

  In Lua the same work measured 60 times C++'s: 215 against 3.5 µs a step.
- **The module keeps no state of its own:**
  - the steam state is an array of numbers in the loco's state table, so
    save and restore are as for any script;
  - its settings are kept once per vehicle definition, found through
    `inputs.vehicle`.
- **Open Rails' units inside, SI at its edge** (a change from the review's
  "SI inside", 2026-10-29). Its formulas are full of imperial constants
  (lb, ft³, BTU, psi, °F, ft/min); kept as they are, every formula and
  number compares with Open Rails' line by line. The module takes and gives
  SI.

**The spec:** in two halves, written from Open Rails `8aafb3147`:
- [reports/or-steam-loco-spec-load.md](reports/or-steam-loco-spec-load.md):
  the ENG's keys, the defaults and estimates, the steam tables;
- [reports/or-steam-loco-spec-step.md](reports/or-steam-loco-spec-step.md):
  the physics each step, the controls, sounds and cab values.

**The content:** MSTS's five steam locos (`310`, `380`, `PENDENNIS`,
`ROYALSCOTSCLASS`, `SCOTSMAN`), MSTS-style. Three of them have vacuum
brakes, which TSRE does not have yet.

## Steps

1. **The spec** (done 2026-10-29): two halves, each by an agent, about
   1,000 lines each.
2. **The native module infrastructure, the steam tables and the config**
   (done 2026-10-29, below), checked against Open Rails' numbers for the
   MSTS locos.
3. **The core:**
   - boiler, firebox, firing, injectors, tender, auxiliaries;
   - a simple engine's cylinders and force;
   - `steam.lua`;
   - checks.
4. **The widget and against Open Rails:**
   - boiler pressure, water level, fire, injectors in the widget;
   - against Open Rails on the MSTS steam locos (after vacuum brakes for
     three of them).
5. **Later:** compound, several engines, booster, geared, rack, oil and
   wood.

## Step 2 done (2026-10-29)

### The infrastructure

- **`Script::State::addNativeModule(name, open)`:**
  - `require` gives what the C++ `open` returns, opened once;
  - it looks there before the module folders, so no file takes a native
    module's place;
  - no shared library is loaded: the sandbox stays closed.
- **`inputs.vehicle`:** every scripted loco's inputs carry its definition, a
  light userdata, set once.
  - **`LocoScripts::definitionAt(L, index)`** gives a native module that
    definition. It accepts only the definitions of this run's scripted
    locos, kept alive for the run, and refuses anything else with a Lua
    error.
- **Checks:**
  - `script` (32): a native module opened once, before a file of its name;
  - `driving` (48): a native module reads the loco's mass through
    `inputs.vehicle` and refuses a table.

### The steam tables

- **`scripts/steam-tables-from-openrails.py`** turns Open Rails'
  `SteamTable.cs` into `src/tsre/sim/steam/SteamTablesData.inc`: its 123
  arrays as written, its 87 tables and 8 2D tables.
- **`Sim::Steam::Table` and `Table2D`** read them as Open Rails'
  `Interpolator` and `Interpolator2D` do: linear, and beyond the ends too.
- **Check (`steam`):** the points, between and beyond them; a 2D table row
  by row.

### The config

- **`Sim::Steam::readConfig(definition)`:** what Open Rails sets up at load
  (load spec §1 to §3):
  - the type, the engines (one from the MSTS keys, or each `Steam` block's
    keys over them);
  - fuel, grate, fire, heating surface and the design output from the
    cylinders;
  - boiler geometry and the water levels;
  - boiler efficiency, firing, grate limit;
  - cylinder constants, crank angles, tractive effort, top speed, IHP;
  - injectors sized, superheat, the burn and back pressure tables;
  - the hot start, dampers, blower, fireman, safety valves, the cylinders'
    geometry.
- **Its choices on the load spec's decisions:**
  - Open Rails' first car's results (TSRE has no copies);
  - the superheat order kept: MSTS locos get saturated tables, as in Open
    Rails;
  - crank angles per loco (Open Rails shares one array);
  - Open Rails' dead defaults kept (a 1 ft³ boiler, the drive weight the
    loco's mass);
  - its unit slips kept where they change results (a bare
    `SteamGaugeGlassHeight` in metres, rejected);
  - the 380 simple, as in Open Rails;
  - unused values left out;
  - per-step constants (safety valves, cylinder geometry) set up here;
  - the data typos kept;
  - extrapolation as in Open Rails;
  - each `Steam` block's keys read on their own;
  - a hot start.
- **Check (`steam`):** 32 values each for MSTS's Royal Scot and 380 (the
  load spec's §5, keys in fixtures) all within 0.15 %:
  - grate 54.53 and 107.94 ft²;
  - design output 40,898 and 80,956 lb/h;
  - MaxTE 33,108 and 65,536 lbf;
  - MaxIHP 3,240 and 4,389 hp;
  - injectors 12 + 11 mm and 17 + 17 mm;
  - the hot start, 245 and 223 psi with 7,233 and 7,531 lb of water and
    steam;
  - and the rest.
