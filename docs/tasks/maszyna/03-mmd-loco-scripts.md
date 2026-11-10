# 03 - MaSzyna locos on Lua loco scripts

## Objective

MaSzyna's engines run TSRE's Lua loco scripts, fed from their `.fiz` and
`.mmd`: driven, and as heard AI trains' shadows. The physics modules are
shared with the ENG scripts, so a fix to brakes or traction reaches both,
and MaSzyna's scripts can grow what an ENG cannot say (resistor steps,
relays, vigilance, its many cab switches) without touching the ENG ones.

Status 2026-11-10: steps 1 to 6 built on `feature/maszyna`, committed 10efd763
([results](#results-2026-11-10)); step 7 and the cab task later.

Reviews behind it:
- [What the scripts read today](reports/loco-script-reads.md)
- [`.fiz`: format, pack, force models](reports/fiz-physics-format.md)
- [Cab controls in `.mmd`](reports/mmd-cab-controls.md) (for the cab task)

Not in this task: importing MaSzyna's C++ mover (a simulation task of its
own); interactive MaSzyna cabs (the cab task, outlined below); sounds;
steam (no MaSzyna steam content or physics).

## Decisions (the user, 2026-11-10)

- **Blocking is not a solution.** `SimulationController::drivable` refusing
  MaSzyna trains was a stop-gap (task 02); MaSzyna engines get scripts.
- **Scripts read `.fiz`, not ENG values filled from it.** Filling ENG keys
  in C++ would drive something soonest, but the mapping is lossy (a
  resistor loco becomes an Open Rails force curve), every MaSzyna feature
  would need an invented key plus script support, and it is the generated
  ENG of task 02 held in memory. Templates such as `electric-mmd.lua` can
  take more of the cab's controls than ENG and `.cvf` have.
- **The existing Lua physics is good enough to start.**
- **MaSzyna's C++ simulation is a separate task.** This changes the plan of
  2026-10-24 (`driving/00-task-roadmap.md`: "MaSzyna's mover in C++ as one
  kind of vehicle model"), which becomes that later task.
- **Answers to the design's questions** (the user, 2026-11-10):
  - Script names `-mmd.lua` in `loco/`, MaSzyna-only modules in
    `loco/maszyna/`; the electric script split by motor, resistor and
    induction, and MaSzyna's series motor model in Lua straight away, not
    precomputed curves (section 6). Lua is there for custom scripts.
  - `vehicle.fiz` in SI, converted in C++: every physical value stays in SI
    inside TSRE (an engine rule).
  - TSRE's air brake is enough for now, with `.fiz`'s pressures.
  - The line voltage as ENG scripts take it (the host's, 3000 V on PKP
    routes), not MaSzyna's 0.95 × `MaxVoltage`.
  - DieselEngine without a gearbox: Open Rails' ENGs lack one too, so both
    wait for TSRE's gearbox.
- Found on the way and fixed (dae242f8): a heard MaSzyna AI loco got
  `electric.lua`, whose reader dereferenced the missing ENG document: TSRE
  crashed when such a train came within earshot. An engine without an ENG
  now has no ENG script (the C++ model, as one missing a script) and the
  reader treats a missing document as an empty file.

## What there is

- **Scripts**: every Lua read of vehicle data is in `init`; about 113 ENG
  paths, filling `s.config` and some state (levers, the combined handle,
  pantograph delays, steam heating). The steam core reads its own config in
  C++. Scripts cannot open files (sandbox).
- **Pack**: 621 powered `.fiz` (train category), 658 drivable rows:

| EngineType | Files | Distinct | Force model in MaSzyna | Open Rails' shape |
|---|---|---|---|---|
| ElectricSeriesMotor | 282 | 89 | Resistor positions (RList), field shunting (MotorParamTable), current and relays (Circuit), line voltage | Lossy: a position is a resistance, not a force share |
| ElectricInductionMotor | 141 | 81 | min(Pmax/v, F0 − a1·v, field limit) | Close (F0, Pmax, a slope) |
| DieselElectric, DumbDE | 159 | 50 | Ftmax to a hyperbola, WWList notch power | Nearly one to one |
| DieselEngine | 32 | 27 | Torque curve, clutch, converter, gears | Lossy: TSRE has no gearbox yet |
| Dumb, WheelsDriven | 5 | 5 | min(PWR/v, Ftmax) × position share | Plain |

- **Brakes**: valves LSt 309, EStED 179, EP2 85; handles FV4a 316,
  MHZ_K5P 79, MHZ_K8P 49; engine brake FD1 501. **Vigilance and cab
  signal** on 487 of 621.

## Design

### 1. Config tables (the ENG scripts, no behaviour change)

Each module's `init` takes a plain table of SI values instead of reading
the vehicle:

```lua
-- before: traction.init(s, vehicle, 10)
-- after:
local eng = require "tsre.eng"          -- the ENG reader, all the paths
local c = eng.electric(vehicle)          -- a fresh table per loco
traction.init(s, c.traction)
brakes.init(s, c.brakes)
controls.init(s, c.controls)             -- lever specs, not levers
```

- `tsre.eng` holds every ENG path, default and bare-unit rule now spread
  over the modules (psi, mph, ft³, `number` keeping 0 and `positive`
  replacing ≤ 0, chained defaults). About 250-300 lines, mostly moved.
- Lever specs (min, max, step, notches with Open Rails' kinds) go in the
  config; `levers.make(spec)` builds the state's lever. The pantograph
  delays and steam heating's values move into the config too.
- A fresh config per loco: `derive` (diesel), `controls.init` and
  `levers.set` change it.
- The config's field names stay as `s.config` has them, so saves keep
  working.
- Steam stays as it is (its config is C++, and MaSzyna has no steam).
- `vehicle` is still passed for what a module reads beyond the config
  (`exhaustKinds`), and user scripts keep the reader.
- Checked as the earlier module split was: the driving checks printing the
  same numbers, USA1's drive-run CSVs identical byte for byte, the OR_PSK
  corpus.

### 2. `.fiz` read in C++, given to scripts in SI

Scripts cannot open files, so C++ reads `.fiz` once per type (with
`MaszynaVehicle`, already cached) and gives it to `init` as
`vehicle.fiz`, read-only:

```lua
vehicle.fiz.Param.M            -- 120000, as written (kg)
vehicle.fiz.Engine.EngineType  -- "ElectricSeriesMotor"
vehicle.fiz.RList.rows         -- { {1, 25.88, 1, 6, 0}, ... }
vehicle.fiz.Circuit.ImaxLo     -- 600 (A)
```

- **The reader follows MaSzyna's parser** (`LoadFIZ_Parse`): a line is the
  unit; sections are case-sensitive prefixes; `key=value` after a space,
  case-sensitive; tables until their END marker (RList until END-RL,
  MotorParamTable until END-MPT, Cntrl. rows until a section or blank line);
  `//`, `/* */` and `#` lines dropped; includes with `(pN)`; a repeated
  section parsed again. Its quirks are part of the format (`LocMaxBP` is
  not a key; `Cx=-0.1` in 63 files is read as written).
- **In SI** (the user), as `vehicle.number` gives an ENG's values: a `.fiz`
  key catalogue in C++ (as `VehicleKeysTable.inc` is for ENG) gives each
  known key's unit as MaSzyna's parser takes it (km/h, kN, kN/m, bar, kW,
  rpm, rev/s, litres, kg·m², Ω…) and each table's columns (RList: position,
  Ω, branches, motors in series, auto switch). Keys it does not know come
  as written, numbers as numbers, the rest as text, so a script can still
  read any key without C++. Text values (EngineType, BrakeHandle) stay
  text.
- `.mmd` values the physics needs (Load, the cab's control list later) the
  same way, as `vehicle.mmd`.
- Built once per definition and kept in the Lua state's registry, so a
  shadow made again does not rebuild it.

### 3. `tsre/fiz.lua`: `.fiz` to the same config

The MaSzyna counterpart of `tsre/eng.lua`: fills the modules' config from
`vehicle.fiz`.

| Config | From `.fiz` |
|---|---|
| Mass, max velocity, wheel radius | `Param.M` (+ `Mred` as rotating mass), `Vmax` km/h, `Wheels.D`/2 |
| Driven axles | `Wheels.Axle` (capital letters) |
| Induction motors: maxForce, maxPower, curve | `F0`, `Pmax`, `a1` (F = min(Pmax/v, F0 − a1·v)) |
| Diesel-electric: maxForce, maxPower, engine | `Ftmax`, `PWR` or WWList, `Vhyp`/`Vadd`/`Cr`; idle and max rpm from `nmin`/`nmax` or WWList |
| Series motors | RList, MotorParamTable(0), Circuit, WindingRes, Trans, `Wheels.D` for the motors' own model (question 3); ET22 at 3000 V, steady state: 352 kN at rest, 200 at 80, 79 at 120 km/h |
| Dynamic brake | Induction `Fh`, `Ph`, `Vh0`, `Vh1`; `DynamicBrake` kind |
| Main reservoir, compressor | `MaxCP`, `MinCP` bar, `Vv` m³, `CompressorSpeed`, electric or mechanical from `CompressorPower` |
| Brake pipe | `HiPP` bar; twin pipe if `AllowedFlag` has 0x20 |
| Train brake handle | `BrakeHandle`, `BCPN` and the Cntrl. rows (pipe pressure per position) as notches |
| Engine brake | `LocBrakeHandle` (FD1: smooth), `MaxLBP` (else `MaxBP`) |
| Throttle | `MCPN` positions as notches; `SCPN` a second lever (shunt field) |
| Pantographs, line voltage limits | `Power.CollectorsNo`, `MinV` (breaker opens below) |
| Delays | `MainInitTime`, `ConverterStartDelay` |

What `.fiz` lacks (pantograph timing, adhesion, brake rates) keeps the
modules' defaults; derived values (Davis resistance, shoe force × μ) follow
MaSzyna's formulas (report, section 4-5).

### 4. Scripts and their selection

- `loco/electric-induction-mmd.lua`
  (ElectricInductionMotor, 81 types: EU44, Elf, Impuls…; TSRE's traction
  module on curves from F0, a1, Pmax; also Dumb and WheelsDriven
  electrics), `loco/electric-resistor-mmd.lua` (ElectricSeriesMotor, 89
  types: ET22, EU07, EN57…; `maszyna.dcmotors`), `loco/diesel-mmd.lua`
  (diesel-electric; DieselEngine without a gearbox). MaSzyna-only modules
  in `loco/maszyna/`. The power supply part of `electric.lua` (pantograph
  voltage and its filter, main and auxiliary power) becomes a shared module
  for all three electric scripts; the breaker gets a trip for the resistor
  script's overload relay.
- `LocoScripts::scriptFor`: a definition with `maszyna` set gets the script
  of its `EngineType`; without a document and without `maszyna`, none (as
  now).
- Command names stay TSRE's where they mean the same (throttle, trainBrake,
  direction, pantograph1, circuitBreaker, battery, cabLight, horn…); new
  ones for MaSzyna's own devices (`shuntField`, `vigilanceReset`,
  `converter`, `compressor`, `overloadReset`…), so the cab task binds the
  `.mmd`'s labels to them.
- `drivable` drops its MaSzyna refusal once a script runs; heard shadows use
  the same scripts.

### 5. The simulation's keys

The simulation reads a few ENG keys of every car (its air brake, the train
brake's pressures, a shadow's dynamic brake lever) through
`VehicleDefinition::number` and `text`. A MaSzyna definition, which has no
ENG document, answers them from its `.fiz` (`MaszynaVehicles::engValue`,
`engText`; the formulas in the results). Twin pipe and EP brakes wait for
the cars' `brakeSystemType`.

### 6. Series motors (the user: the model straight away)

MaSzyna's model for its 89 resistor-controlled types, written afresh in
Lua as `maszyna.dcmotors` (as Open Rails' electric was), not as a force
curve:

- **Current and force each step**: the motors' current from MaSzyna's
  circuit equation (`Current`, Mover.cpp:285-461: line voltage, motor speed
  from the wheels and gear ratio, the position's resistance and grouping,
  the field step's parameters), torque per motor (`Momentum`, 6666), force
  at the wheels; the line current for the supply and the ammeter.
- **The lever and the actual position**: the lever is a position number
  (MCPN, up to 48); the actual position follows it through the relay
  (`AutoRelayCheck`, 6794-7083): steps with their delays (IniCDelay,
  SCDelay), automatic rows only once the current falls below Imin (Lo or
  Hi), grouping changes (series, series-parallel, parallel), field
  shunting only at no resistance, by the second lever (SCPN) or
  automatically.
- **Protection**: the overload relay (current above ImaxLo or ImaxHi, by
  the driver's switch) trips the breaker after a delay; its reset.
- **Kinds**: units stepping through their rows on their own (EN57 and the
  other units with MCPN 3: 90 files), ET22's group transition cut, and the
  other type branches (`TrainType`: ET40, ET42, EP05…; about 38 such
  checks in `Mover.cpp`) as content needs them.

MaSzyna's code for it is about 1,000 lines of C++ (current 173, relay 289,
traction branch about 85, controller steps about 350 for all kinds,
relays and fuses about 150). Afresh in Lua: about 400-600 lines and a test
set. Force curves precomputed per position would already need the current
equation and torque, about half of it; they cannot show the relay
stepping, which the units (MCPN 3) rely on.

### 7. MaSzyna's own devices (later steps)

On top of the shared modules, in `loco/maszyna/`:
- **Vigilance and cab signal** (`Security:`): timers, warnings, emergency
  brake, the reset buttons.
- **Converter, compressor, battery and cab activation** switches as
  MaSzyna's cabs have them.

### Cab task (later, outline)

The `.mmd`'s cab definitions read in C++ (gauges, buttons, lamps, types,
sounds), bound to the scripts' commands and values: `rot`/`mov` through
`E3dShape::setPartTransform`, lamps through `enableSubObjByName`, picking
on `.e3d` triangles, MaSzyna's mouse model, per-control sounds, Python
screens as RmlUi displays (229 of 291 cab models). 658 drivable rows, 291
cab models, about 63 controls each. See the
[cab report](reports/mmd-cab-controls.md).

## Steps

Each step ends with `build all`, `ctest` and the app suites.

1. **Config tables**: `tsre/eng.lua`, modules taking config; ENG scripts'
   behaviour unchanged (driving checks, USA1 CSVs, OR_PSK corpus).
2. **`.fiz` reader**: `MaszynaFiz` in C++ + suite `fiz-file` (sections, keys,
   tables and their ends, includes with `(pN)`, comments, repeated
   sections) + corpus suite over the pack (no failures; the statistics of
   the report reproduced).
3. **Scripts get it**: `vehicle.fiz`, `tsre/fiz.lua`, `scriptFor`; the
   host's typed fields from `.fiz`.
4. **`electric-induction-mmd.lua`**: the shared power supply module,
   pantographs, breaker, supply, brakes from `.fiz`; induction motors.
   **`electric-resistor-mmd.lua`** with `maszyna.dcmotors` (section 6): the
   current and force on manual positions first, then the relay, the
   protection, field shunting and the units' stepping.
5. **`diesel-mmd.lua`**: diesel-electric; DieselEngine without a gearbox.
6. **Driven and heard**: `drivable` unblocked; runs on a route with ET22
   (series), EU44 (induction), SM42 (diesel-electric) and an EN57 unit
   (multiple unit, MCPN 3), driven and as AI.
7. **MaSzyna devices**: vigilance, switches (may become a task of its
   own).

## Verification

- ENG scripts: the same numbers and CSVs as before step 1.
- `fiz-file`, the corpus suite; every powered MaSzyna loco's script starts
  without an error (621 files).
- Force against MaSzyna: ET22 on each of its positions against MaSzyna's
  steady state at 3000 V (the report's envelope); the relay's steps and
  delays and the overload trip against MaSzyna's code; EU44's F0 and Pmax;
  SM42's curve.
- A heard MaSzyna AI train next to a driven one: its run unchanged (as for
  ENG locos).

## Results (2026-11-10)

- **Step 1, config tables:** `tsre/eng.lua` holds every ENG read (about
  113 paths, moved unchanged); each module has `setup(s)` working from
  `s.config`, its `init(s, vehicle)` kept for user scripts (the ENG part,
  then `setup`). Levers come from specs (`levers.spec`, `levers.make`).
  `electric.lua` and `diesel.lua` are now wrappers over `tsre.electricloco`
  and `tsre.dieselloco` (their bodies, taking a config and, for the
  electric, a motor module: `c.motor`, `tsre.traction` by default); the
  electric's power supply is `tsre.electricpower`. Unchanged, checked after
  each move: USA1's (HHP-8), USA2's (Dash 9s) and EUROPE2's (380) drive runs
  byte for byte, the script suites' printed numbers (only the saved states'
  sizes grew, with the specs), OR_PSK's corpus (817 electric, 435 diesel, no
  script error).
- **Step 2, `.fiz`:** `MaszynaFiz` (`src/tsre/maszyna/`) as MaSzyna's parser
  reads it, in SI by a key catalogue. Found while checking: Cntrl.'s
  rows are in MPa (MaSzyna's own pressures: 0.5 is running), not bar as
  the review said. MotorParamTable rows stay as written (their columns
  differ by engine type; series motors' are SI already). `MaszynaVehicle::
  physics()` reads it on first use: reading it with every type cost about a
  quarter more on the pack's scan. First use is the simulation's: a run
  reads every MaSzyna type in it when it starts (`SimulationCore`), so a
  heard train's first shadow reads nothing in a step; the editors, listing
  and placing never read it. Kept for the type until its `.fiz` or `.mmd`
  changes. Suites `fiz-file` (18) and `fiz-corpus`:
  the review's counts exactly (1,466 files, the engine kinds, 621 powered).
- **Step 3, scripts get it:** `vehicle.fiz` (sections, keys in SI, `rows`);
  `tsre/fiz.lua` builds the configs from it over tsre.eng's defaults;
  `scriptFor` picks a MaSzyna engine's script by `EngineType`, the C++ model
  where the script is not there. The train brake handle: Cntrl.'s rows as
  notches (filling as Overcharge, running as Release, each service position
  holding its pressure, the empty pipe Emergency); the engine brake a
  lever to `MaxLBP` (else `MaxBP`).
- **The simulation's keys:** it reads a few ENG keys of every car through
  `VehicleDefinition::number` and `text`, not typed fields. A MaSzyna
  definition answers them from its `.fiz` (`MaszynaVehicles::engValue`,
  `engText`): the brake force, MaSzyna's ((MaxBP·πBCR² - BCS)·BCM - BSA)·BRE
  on each of BCN cylinders times its block friction (friction.cpp by `BM`)
  at 50 km/h, since TSRE's brake force has no speed in it (ET22: 98 kN); the
  cylinders' most (`MaxBP`); the train brake's pressures (`HiPP`, `LoPP`);
  the main reservoir's (`MaxCP`); the brake valve, a distributor (a triple
  valve for Westinghouse's `W`). Without the valve no MaSzyna car braked.
  The brake system (twin pipe, EP) is `VehicleDefinition::brakeSystem()`:
  `brakeSystemType`, or a MaSzyna car's from its `.fiz` on first use (EP
  from `BrakeSystem=ElectroPneumatic`, the main reservoir pipe from
  AllowedFlag 0x20), which the air brake and tsre.fiz both take; a twin pipe
  loco with cars the simulation took as single pipe had emptied its
  reservoir into a pipe nobody had.
- **Step 4, electrics:** `electric-induction-mmd.lua` (curves of min(Pmax/v,
  F0 - a1·v), the ED brake from Fh and Ph); `electric-resistor-mmd.lua` on
  `maszyna.dcmotors`, MaSzyna's model (section 6) with the shunt field as
  a second lever and the commands `shuntField`, `overloadReset`,
  `highCurrent`, `autoRelay`.
- **Step 5, diesels:** `diesel-mmd.lua` on tsre.dieselloco: a curve per
  notch from MaSzyna's DieselElectric formula with the notch's generator
  power (WWList), the engine's RPM and power from WWList; DieselEngine and
  Dumb as Open Rails' plain diesel on PWR.
- **Control cars, for now** (the user: engines with no force are enough):
  a MaSzyna car with a controller (`MCPN`) and a cab model but no engine is
  an engine of kind Control, on its unit's script (resistor for `ezt`,
  else induction) without force; the cab is the first engine in running
  order, so EN57's ra drives and its levers reach the motor car on the
  control line. 345 such cars in the pack.
- **The line contactors** close on position 1 as MaSzyna's (its driver
  steps through it); a controller set up from 0 at once (a key, the panel,
  a takeover) counts as having passed it, one already up when power comes
  back goes to 0 first.
- **The driver's panel** (`ui/loco.lua`, `loco.rml`): the throttle as its
  position (12 / 48), rows for the shunt field lever and the overload relay
  (Reset, High current), readouts for the controller position and the
  motors' current, where the loco publishes them. Seen on USA2 with the
  test consist (ET22, two 441W) at 3000 V: notch 12, 70 A, 17.9 kN at 37
  km/h.
- **Step 6, driven and heard:** `drivable` no longer refuses MaSzyna
  trains. Suite `maszyna-scripts` (11, `--game-root` the pack):
  - ET22's force at 3000 V, each notch in turn, then the field steps, the
    most with 600 A or less: 352, 383, 268, 200, 119, 79 kN at 0 to 120
    km/h, the review's MaSzyna steady state to the kilonewton;
  - ET22 from standstill (the contactors after IniCDelay); EU44 304 kN at a
    start (F0) and Pmax/v at 200 km/h; EN57's motor car on notch 2: its
    relay through the 9 series rows as it gathers speed, no overload;
    SM42 228 kN at a start (Ftmax), 42.7 kN at 40 km/h (540 kW over v +
    Vadd);
  - a train (ET22, two 441W) driven on a test track: the ET22 its cab, notch
    12, the last service position stops it, its reservoir at 8.5 bar; heard
    as an AI train, its shadow on the resistor script;
  - an EN57 unit (ra, s, rb): ra the cab without force, notch 2 drives the
    motor car, its brake stops the unit; ET22's controller set to 6 at once
    steps the relay up;
  - all 1,071 engines in the pack (control cars among them): 476
    resistor, 381 induction, 214 diesel, none on the C++ model, no script
    error.
- Build all, ctest 11/11, the driving, steam, cab, heating, script,
  simulation, sound, display, vehicle and MaSzyna suites pass.

## Open: work left

- **Control cars proper:** now engines without force on their unit's
  script (chosen by `MCPN` and a cab model: check the 345 cars in the
  Consist Editor, where they now list as engines; a push-pull's driving
  trailer is among them). A control car kind of its own (its script holding
  only the levers, the brake valve and the switches) when the cab task
  needs it. OR_PSK's EN57 works the MSTS way (ra and rb ENG files of 200 kW
  each, s a WAG).
- **Step 7:** vigilance and cab signal (`Security:`), converter and
  compressor switches, battery and cab activation as MaSzyna's cabs have
  them.
- **Series motors not ported yet:** the dynamic brakes of ET42 (switched)
  and EP09 (automatic, TUHEX); ET22's interlock (no main controller with
  shunting on); EZT's own controller steps beyond the relay; the motors'
  overspeed and damage; the starting resistors' fans (RVent).
- **Couplers' stiffness** from `kB`, `kC` (TSRE's default now).
- **Gearboxes** (DieselEngine: 27 types), with Open Rails' ENGs.
- **The cab task:** [04](04-cabs.md).

## Open: to review

- **Brake force** taken at 50 km/h of MaSzyna's friction (TSRE's brake has
  no speed in it): ET22 98 kN; MaSzyna's is 0.41 of the blocks' force at
  rest, 0.09 at 100 km/h.
- **Air brake rates:** TSRE's distributor (Open Rails' defaults), not
  MaSzyna's valve classes: ET22's cylinders reach 3.5 bar in about 60 s;
  MaSzyna's LSt and EStED have their own fill times (BDelay is the AI's
  only).
- **EP and twin pipe** from `BrakeSystem` and AllowedFlag 0x20 as Open
  Rails' `ep` / `air_twin_pipe`: whether MaSzyna's EP (EN57) behaves as
  TSRE's EP wire expects; units with EP and no main reservoir pipe become
  `ep_single_pipe`.
- **Induction motors:** the demand ramp (0.96 of F0 a second) and the ED
  brake (1.2 of Fh a second, faded in from Vh0 to Vh1) from the review's
  reading, not measured against MaSzyna; its field weakening limit
  (FMAXMAX) left out.
- **Diesel-electrics:** the engine's RPM change (its range over AIM a
  second) is a guess at MaSzyna's dt/AIM; the current limit per notch
  (WWList Imax) and heating's power (HeatingP) left out.
- **The line contactors:** a controller set up from 0 at once counts as
  having passed position 1 (TSRE's keys and panel set a lever at once;
  MaSzyna's driver steps).
- **The high current switch** sets both thresholds, Imax and Imin, Hi or
  Lo; MaSzyna switches Imin on its own for EZT (MinCurrentSwitch).
- **The panel:** booleans shown as true/false (Overload relay), the motors'
  current without its unit; the Polish texts (Bocznikowanie, Przekaźnik
  nadmiarowy, Odblokuj, Wysoki rozruch, Pozycja nastawnika, Prąd silników).
- **No catenary, no traction:** a MaSzyna electric on a route without
  `MaxLineVoltage` gets 0 V, as an ENG electric does (the HUD capture used
  a scratch copy of USA2 at 3000 V).
- **MotorParamTable** rows as written (their columns differ by engine
  type); Cntrl.'s rows are MPa, against the review.

## Questions

None open.
