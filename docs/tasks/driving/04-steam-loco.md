# Task 04 - The steam loco

Status: started 2026-10-29 on `feature/driving` (step 1 under way, the
native module infrastructure done).

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
- **SI inside,** checked against Open Rails' numbers, as the diesel was.

**The spec:** in two halves, written from Open Rails `8aafb3147`:
- [reports/or-steam-loco-spec-load.md](reports/or-steam-loco-spec-load.md):
  the ENG's keys, the defaults and estimates, the steam tables;
- [reports/or-steam-loco-spec-step.md](reports/or-steam-loco-spec-step.md):
  the physics each step, the controls, sounds and cab values.

**The content:** MSTS's five steam locos (`310`, `380`, `PENDENNIS`,
`ROYALSCOTSCLASS`, `SCOTSMAN`), MSTS-style. Three of them have vacuum
brakes, which TSRE does not have yet.

## Steps

1. **The spec** (under way).
2. **The native module infrastructure** (done 2026-10-29, below); the steam
   tables and the config with Open Rails' defaults and estimates, checked
   against its numbers for the MSTS locos.
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

## Step 2, the infrastructure (2026-10-29)

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
