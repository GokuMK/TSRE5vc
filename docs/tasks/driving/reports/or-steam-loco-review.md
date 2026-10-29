# Open Rails' steam locomotive: why so big, Lua or C++

Written 2026-10-29, after task 03 (the diesel). The user asked:
- why Open Rails' steam loco is so much code;
- what its modules are;
- whether it could run in Lua as the electric and the diesel do;
- if not, a design for a C++ "module".

Read from Open Rails `Source/` at `8aafb3147`; no code taken.

## Why so big

`MSTSSteamLocomotive.cs` has 10,967 lines, about 760 fields and one class.
`SteamEngine.cs` adds 1,016 and `SteamTable.cs` 1,452. For comparison, the
electric is 486 lines and the diesel 1,483 plus its engine's 2,245, on top
of `MSTSLocomotive`'s 6,484 that all three share.

**Lines by what they are for:**

| Part | Lines | Runs |
| --- | --- | --- |
| HUD text (`GetDebugStatus`) | 1,109 | per frame, display only |
| Smoke and steam effects (`UpdateFX`) | 801 | per frame, visual only |
| Defaults and estimates (`Initialize`) | 1,431 | once, at load |
| Reading the ENG (`Parse`), save, restore, copy | about 480 | once |
| Cab values, commands, controllers | about 600 | on demand |
| The physics each step | about 4,000 | every step |
| Steam property tables (`SteamTable.cs`) | 1,452 | data |

**The physics each step:**

| Part | Lines |
| --- | --- |
| cylinders (`UpdateCylinders`, per engine) | 1,197 |
| boiler | 601 |
| tractive force from the crank angles | 562 |
| `Update` itself (booster, rack, the engines' loop) | 416 |
| injectors | 353 |
| firebox | 311 |
| firing (the fireman) | 246 |
| auxiliaries' steam | 194 |
| tender | 191 |
| controllers, force, axles, superheat, water gauge | about 530 |

**So the size comes from:**
- **About a third is not physics at run time:** the HUD, the effects, and
  the load-time defaults. MSTS steam files give little, so Open Rails
  estimates boilers, grates, burn rates and injectors by rules of thumb.
- **The physics is real thermodynamics:**
  - steam properties by pressure (31 tables, 83 lookups a step);
  - an indicator diagram per engine: admission, cutoff, expansion, release,
    compression, back pressure, condensation;
  - crank-angle forces per cylinder;
  - the boiler's heat and water, the fire's mass and burn rate, injectors
    by temperature and pressure;
  - about 110 calls to `pow`, `log`, `sqrt` and trigonometry a step.
- **Every variant lives in the one class:**
  - simple and compound;
  - several engines (Mallet, Garratt), booster, rack, geared (Shay);
  - coal, wood and oil;
  - live and exhaust steam injectors;
  - manual and automatic firing;
  - saturated and superheated.
- **Its style adds more:**
  - every quantity is a field, no state struct;
  - imperial units inside, with conversions inline;
  - about 1,150 comment lines;
  - some duplication (its own comments say so).

## Could it run in Lua?

**Measured** with the standalone Lua 5.5 built from TSRE's `thirdparty/lua`
at `-O3`. The benchmark is a step of Open Rails' shape:
- 2,500 statements on state and config fields;
- 83 lookups in 20-point tables;
- 110 maths calls in the per-step mix;
- 15 functions.

The same step was translated line for line to C++, without NaNs or
denormals, so both do the same work.

| | One steam-shaped step | The electric's step, for scale |
| --- | --- | --- |
| Lua (TSRE's) | 210 to 228 µs | 5.9 µs of script |
| C++ | 3.3 to 3.9 µs | |

**About 60 times slower in Lua.** At 0.01 s steps one driven steam loco
would take 2 % of a core; a dozen would take a quarter. Written by hand in
Lua, with locals in place of fields, it might halve, still 30 times C++.

**Lua could run it, but it is the wrong place:**
- the cost is 36 times the electric's;
- about 4,000 lines of numeric code would have to be kept exact, in a
  language without types;
- the steam tables, interpolation and thermodynamics are what native code
  is for.

**What Lua is right for, as in the electric and the diesel:**
- the cab's controls, commands and sounds;
- the brakes;
- what a loco publishes;
- what a user changes per loco.

## The proposal: a native module the Lua script uses

**`steam.lua` stays the loco's script.** It uses the shared modules
(`controls`, `levers`, `brakes`, `supply`) as the diesel does. Its
thermodynamics come from a C++ module, `require "tsre.steam"`, loaded like
the Lua ones. The steam loco then keeps everything the other locos have
(commands, the control line, values, sounds, save and restore, user
scripts), and only the heavy core is native.

**Rejected: a whole C++ `VehicleModel` for steam.** It would repeat in C++
the controls, levers, brakes, commands and values the Lua modules already
have, and a user could not change it.

### The C++ side (`src/tsre/sim/steam/`)

| Part | What |
| --- | --- |
| `SteamTables` | saturated steam and water by pressure, SI, built once |
| `SteamConfig` | the ENG's keys through the key catalogue, Open Rails' defaults and estimates (its `Initialize`, about 1,400 lines in C#), one per vehicle definition, shared, immutable |
| `Boiler` | heat in, steam out, pressure, water mass and level (the gauge, the gradient), safety valves |
| `Firebox` | fuel, fire mass, burn rate, draught (blower, exhaust), damper, fire door, the automatic fireman |
| `Cylinders` | an engine's indicator diagram, steam use, back pressure, condensation, cocks, superheat |
| `Traction` | the force from the crank angles, per engine, a booster |
| `Injectors` | live and exhaust steam, water delivered, steam used |
| `Auxiliaries` | steam for the blower, ejectors, compressor, heating, generator, whistle |
| `Tender` | coal and water |

**Shape:**
- **Plain functions** over three structs:
  - `Config`: constant;
  - `State`: plain numbers;
  - `Controls` (regulator, reverser, firing, blower, damper, injectors,
    cocks, ejectors) and `Inputs` (dt, speed, gradient, adhesion) in.
- **Out:** the force, steam and water flows, pressures and levels, and
  events for sounds.
- **SI inside,** as the rest of TSRE: Pa, kg, kg/s, J, K. It is checked
  against Open Rails' numbers, as the diesel was.

### The Lua side

- **One function a step:**
  `steam.step(inputs.vehicle, state.steam, controls, inputs, out)`.
- **The state stays in the loco's state table** (`state.steam`, an array of
  numbers). Save and restore then work as for every script. The native
  module reads and writes about 150 numbers a step, about 1 µs.
- **The config is never in the state:** the module keeps one per vehicle
  definition. `inputs.vehicle` is a new input the host gives every step,
  that definition as a light userdata. Nothing unsavable sits in the state,
  and a restored loco needs no `init` again.
- **`steam.lua`** maps the levers to the controls (regulator, a continuous
  reverser, firing and the rest), publishes the values, turns events into
  sounds, and drives the brakes.

### What TSRE needs for it

1. **Native modules:** `Script::State` lets the host register C++ modules
   for `require` beside the module folders. They are not files, so a user's
   folder cannot shadow them. `LocoScripts` registers `tsre.steam`.
2. **`inputs.vehicle`** for every scripted loco.
3. **Vacuum brakes:** three of the five MSTS steam locos here have them, and
   TSRE has only air brakes. That is a task of its own (Open Rails'
   `VacuumSinglePipe`, 1,513 lines; ejectors take steam).
4. **Possibly train-level inputs:** Open Rails' steam `Update` walks the
   train's cars for their resistance each step. The spec should find
   whether anything but the HUD uses it.

The same mechanism can carry other heavy parts later: an axle model with
wheel slip, the diesel's gearbox, MaSzyna's physics.

## The content

- **MSTS:** five steam locos (`310`, `380`, `PENDENNIS`,
  `ROYALSCOTSCLASS`, `SCOTSMAN`), all MSTS-style with `BoilerVolume` and
  no ORTS steam keys, so Open Rails' defaults and estimates decide most of
  their behaviour. Three have vacuum brakes.
- **OR_PSK and TS_STARTER_ROUTE:** no steam locos.

## Steps, if it goes ahead

1. **The spec,** as the diesel's: the physics, the defaults and estimates,
   the controls, sounds and cab values. Not the HUD and not the effects.
2. **The native module infrastructure** and `inputs.vehicle`; the steam
   tables and the config with its defaults, checked against Open Rails'
   numbers for the MSTS locos.
3. **The core:** boiler, firebox, firing, injectors, tender, auxiliaries, a
   simple engine's cylinders and force; `steam.lua`; checks.
4. **The widget** (boiler pressure, water level, fire, injectors) and
   against Open Rails, on the MSTS steam activities (after vacuum brakes
   for three of them).
5. **Later:** compound, several engines, booster, geared, rack, oil and
   wood.
