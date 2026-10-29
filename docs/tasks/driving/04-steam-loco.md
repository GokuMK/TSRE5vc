# Task 04 - The steam loco

Status: started 2026-10-29 on `feature/driving` (steps 1 to 3 done).

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
3. **The core** (done 2026-10-29, below):
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

## Step 3 done (2026-10-29)

### The core

**`src/tsre/sim/steam/SteamCore.h/.cpp`:** one step in Open Rails' order
(step spec §0):
- the exhaust beats and sound variables;
- the tender;
- the firebox and firing;
- the boiler:
  - safety valves, blowdown, blower;
  - the AI fireman's heat ratios;
  - heat in, radiation, the water's share;
  - the pressure through the 240 s smoothed heat;
- the auxiliaries' steam;
- superheat;
- each engine's cylinders and force;
- the water glass, the injectors;
- the AI fireman's injectors and ratios, or manual firing's blower and
  damper.

**Its state** is a list of numbers, 81 for the loco and 3 for each engine,
kept in the script's state table (`state.steam`). Save and restore carry
all of it.

**Its parts are public for the checks:**
- `diagram`: an engine's indicator diagram and its steam;
- `crankForce`;
- `superheatFactor`.

### The native module

**`tsre.steam`** (`SteamModule.cpp`) is registered by `LocoScripts`.
- `steam.step(inputs.vehicle, dt, state, controls, inputs, values)`:
  - gives the force (N) and the events: Open Rails' sound numbers and
    message names;
  - writes its values in SI: boiler pressure, the water glass and the
    gradient glass, fire, injectors, steam chest, back pressure, MEP, power,
    tender, the sound variables.
- `steam.info(inputs.vehicle)`: its maximum pressure, force and power.
- The config is read once for each definition.

### `steam.lua`

- **Levers:**
  - the regulator: the cab's throttle lever, from the ENG's `Regulator`;
  - the reverser: from `Cutoff`;
  - the injectors' water, blower, damper, firing rate, fire door;
  - Open Rails' defaults where the ENG has none, and its sounds.
- **Switches:** the injectors (manual firing only), cylinder cocks,
  blowdown, manual firing (the AI fireman off), a shovel, the AI fireman's
  overrides, refill.
- **`direction`:**
  - 1 or -1 puts the reverser on that side, at a quarter cutoff at least;
  - 0 puts it in mid gear;
  - the loco's direction is the reverser's side, so it is never neutral.
- **Brakes** as the diesel's, with a compressor that is always powered.
- **`LocoScripts::scriptFor`:** `Type ( Steam )` (kind 6) runs `steam.lua`.

### Choices on the step spec's decisions (its §15)

1. **C++ and Lua** are split as the spec proposes.
2. **Inputs:**
   - speed and gradient come from the host;
   - the outside temperature is 15 °C for now. TSRE's weather has the
     air's temperature at each keyframe (set, generated with a daily
     cycle, or imported for the date), but it doesn't reach the vehicle
     models yet, and there is no default by season and place for weather
     that sets none (the user's correction, 2026-10-29);
   - a tender is coupled;
   - there is no train heating, sander or scoop yet.
3. **The force comes from the crank angles** for every steam loco, because
   TSRE models only the driven train (Open Rails' player train).
   - The driving axle's angle follows the speed, with no slip, and every
     engine uses it, as in Open Rails.
   - Open Rails also swings the axle's weight with the crank angles, an
     input to its adhesion. That is left out: TSRE gives the adhesion
     limit.
   - The MEP force (Open Rails' AI trains) is in the module, behind
     `crankForce = false`.
4. **Step size:**
   - The cylinders' 0.6/0.4 average for each step is applied as Open Rails
     applies it at 60 frames a second (0.6^(60·dt)).
   - The 36-step force average becomes an exponential average of 0.3 s.
   - The other filters already do not depend on the step.
5. **Open Rails' slips are reproduced**, for comparison, as in the config:
   - tender coal by the burn curve;
   - injector 2's copy slips and injector 1's share counted twice;
   - the latched heat exceptions;
   - the radiation's +32 °F;
   - the fire-off override's 0.90 BTU test.

   Exceptions:
   - events fire once (decision 10);
   - ln R/(R−1) is 1 at R = 1, where Open Rails gives NaN;
   - a mechanical stoker's fuel boost feeds the stoker's rate. Open Rails'
     boost rate for a stoker is 0, which stops the feed; none of MSTS's
     locos has a stoker.
6. **Units:** Open Rails' inside (step 2's change).
7. **The pressure:** Open Rails' chain is kept: smoothed heat, then water
   temperature, then pressure.
   - TSRE computes in double; Open Rails in float, 7 digits.
   - Its smoothed heat, millions of BTU, moves by steps near a float's
     spacing, so comparisons may differ there.
8. **The superheat order:** saturated tables for MSTS's locos (step 2).
9. **Save:** everything is saved, including what Open Rails forgets (the
   firing mode, the cocks, each engine's steam, the AI ratios).
   - A restored loco goes on as it was.
   - A blown fusible plug stays blown; Open Rails repairs it on restore.
10. **Events once:** the AI fireman's safety valves send 42 and 43 on the
    change; Open Rails sends them every step.
11. **The AI cutoff rule** is left out: no AI or autopilot drives a TSRE
    loco model yet.
12. **Crank offsets** are per loco (step 2).

### Checks

- **`steam` (9):**
  - **The §9.8 example**, the Royal Scot at 60 mph, 245 psi, regulator
    full, cutoff 25 %, all within 0.1-0.3 %:
    - the diagram: release, compression, admission; the steam chest
      221.8 psig; a to f; MEP 75.5 psi; raw steam 9.05 lb/s;
    - superheat 203.5 °F, its factor 0.841, 27,400 lb/h;
    - the MEP force, 11,770 lbf;
    - the crank forces at 0, 30, 60 and 90°, and over a turn: mean 12,442
      lbf, 7,406 to 16,713.
  - **The same point stepped** for 7 s: 12,463 lbf on average, 27,380 lb/h,
    244.8 psi.
  - **Standing 10 minutes** with the AI fireman:
    - both injectors start at once, as the glass is at half;
    - 242 to 250 psi;
    - the fire at its ideal mass.
  - **The safety valves:**
    - manual firing: two lift at 253 psi and reseat at 244;
    - the AI fireman caps the pressure at 250.
  - **`steam.lua`:**
    - its values, the regulator and the reverser;
    - 86 kN to start at cutoff 25 %;
    - the cocks' and the fireman's sounds, a shovel, `direction`;
    - saved and restored, the same step after.
- **`driving` (49):** `steam.lua` costs 15.1 µs a step; the diesel 15.1 and
  the electric 12.0 in the same run.
- **`driving-corpus`:** on MSTS, the five steam locos run `steam.lua`
  without errors. The Royal Scot pulls 91 kN at 5 m/s at full regulator
  and cutoff 25 %.

### Open

- **The widget (step 4):** the values are published, but the UI has no
  units or rows for them yet.
- **The sound variables** (`variable1` to `variable3`) are published but
  not fed to the sound system yet, as with the diesel's RPM.
- **The outside temperature** from TSRE's weather at the train, to
  `inputs.outsideTemperature`, with a default by season and place where
  the weather sets none.
- **Vacuum brakes:** MSTS's Pendennis, Royal Scot and Scotsman run on
  TSRE's air brakes and draw compressor steam. The ejectors' steam waits
  for vacuum brakes.
- **Not yet:**
  - oil (it burns as coal now);
  - a cold start (Open Rails' `HotStart` off);
  - motion pumps, the water scoop, steam heating, the counter-pressure
    brake;
  - the generator's steam (MSTS's locos have no `GeneratorFX`);
  - compound, several engines on their own axles, booster, geared and rack
    (step 5).
