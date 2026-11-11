# MaSzyna's mover: the whole core, or modules for the engines?

Review for the user, 2026-11-11. The question (the user): the plan of
2026-10-24 was MaSzyna's mover in TSRE as a C++ vehicle model, wrapped as
MaSzyna-Reloaded's `libmaszyna` wraps it. TSRE's host owns the train's
motion, couplers, adhesion and buses, so only the engines would be needed.
Would Lua loco scripts with C++ native modules, as for Open Rails' steam
locos (driving task 04), be enough? And how much does the present Lua
implementation miss?

Read: MaSzyna at 66a940d4 (`../maszyna`, `McZapkie/`), TSRE
`feature/maszyna` at ab561de0, the pack in `~/ORTS/maszyna/dynamic` (1,466
`.fiz`, 621 of them powered, counted with includes expanded as MaSzyna
does), `libmaszyna` on GitHub (read 2026-11-11). Line numbers are
`Mover.cpp`'s unless a file is named. Code read, nothing run; estimates
say so.

## Short answer

- **Modules are enough, and fit TSRE better than the whole mover.** The
  engines are 16–18 % of the mover (3.7–4.0k of 22.6k lines with every
  option, 2.7–3.0k without). They talk to the rest of it through a few
  values that become plain inputs and outputs. The whole mover is built
  around owning what TSRE's host owns: its movement step runs its devices
  and brakes, it writes air flows straight into its neighbours, it wants
  steps of 10 ms or less and freezes idle vehicles. `libmaszyna` made it
  work by letting the mover own the train.
- **The Lua path misses less than its size suggests.** About 550 of the 621
  powered vehicles get MaSzyna's force (within a few percent) at a given
  line voltage. The force is wrong in three places: mechanical and
  hydraulic diesels (32), controllers working by increments or with brake
  positions (57), and series motors at speed from the 3000 V choice (282).
  Most other gaps are procedures and devices (vigilance, breaker, the
  brake's pressure switch, speed control), Lua-sized logic.
- **The largest MaSzyna code worth reusing is not an engine**: the brake
  equipment (`hamulce`, `Oerlikon_ESt`, `friction`: 8.2k lines, 36 % of the
  mover) is self-contained and belongs to the host. So does adhesion,
  which today is a fixed 0.33 for every vehicle.

## The two shapes

**The whole mover** (`libmaszyna`'s way). One `TMoverParameters` per
vehicle behind TSRE's vehicle model interface; TSRE's cabs, sounds, HUD
and displays bound to its fields.

**Lua with native modules** (the steam way). The loco script stays the
frame: controls, commands, devices, cab, sounds (as now). Numeric engine
cores where Lua is too slow or the port would be too long become C++
modules opened with `require` (`Script::State::addNativeModule`; the
steam's `tsre.steam` takes the vehicle, dt, its state as a list of
numbers, the controls and inputs, and gives the force and values).

## What the mover is made of

| Part | Lines | Share | In TSRE's design |
|---|---|---|---|
| Brake equipment: valves, distributors, handles, cylinders, EP (`hamulce` 6,062, `Oerlikon_ESt` 869, `friction` 265, Mover 1,040) | 8,236 | 36 % | the host's (per-car valves on host-solved pipes) |
| Engines and controllers (Mover 3,830, declarations ≈400) | ≈4,250 | 19 % | the loco's |
| Loading and defaults (the `.fiz` parser 3,110) | 3,308 | 15 % | TSRE has its own `.fiz` reader |
| Devices: pantographs, breaker, converter, compressor, battery, pumps, heating, lights, doors, sanding, vigilance, radio | 2,354 | 10 % | the loco's |
| Host parts: movement, couplers, adhesion and slip, resistance, pipe flows to neighbours, power along couplers, commands between units | 2,238 | 10 % | the host's |
| The rest of `MOVER.h` (types, other fields) | ≈2,200 | 10 % | |

The engines' code, per type (a native module's core / with its options;
estimates):

| Engine type | Powered `.fiz` | Core | With options |
|---|---|---|---|
| Series motors (`Current` 285, `Momentum` 6666, `AutoRelayCheck` 6794, controllers) | 282 | ≈1,100 | ≈1,350 (resistor and EP09 dynamic brakes, fans) |
| Induction motors (`TractionForce` 6175-6483, `CheckEIMIC` 7240) | 141 | ≈800 | ≈1,300 (speed control, MED blending) |
| Diesel-electric (`TractionForce` 5817-6173, `dizel_Update`) | 159 | ≈950 | ≈1,450 (heat model, digital control) |
| Mechanical and hydraulic diesels (`dizel_*` 7582-8165) | 32 | ≈900 | ≈1,400 (heat model, digital control) |
| Dumb, WheelsDriven | 5 | <150 | |

Shared pieces (controller stepping, relays, `CheckEIMIC`, the heat model)
bring all of them together to 3.7–4.0k.

### How the engines talk to the rest

Easy to make plain inputs and outputs (all types):
- wheel speed (`nrot`, `enrot = Ratio·nrot`) and the direction signs;
- line voltage in, current drawn out (`Itot`, read by the pantograph and
  the power couplers);
- 17 `SendCtrlToNext` calls in engine code, which become the host's bus;
- requests to devices (`MainSwitch(false)`, `FuseOff`, automatic sanding),
  which become outputs;
- brake values for blending (`GetEDBCP`, `SetED`, cylinder and pipe
  pressure), which TSRE's host already gives scripts;
- no `TDynamicObject` in engine code except display helpers; random
  numbers only in the series motors' flashover.

Tangled (to restructure, not copy):
- the series motors' positions, spread over the controller functions,
  `AutoRelayCheck`, `Current` and `TractionForce` with 39 `TrainType`
  branches (already ported to Lua as `maszyna.dcmotors`);
- induction units' ED and air blending across the train (MED,
  `DynObj.cpp` about 3214-3557), a train-level part: a unit or train script;
- `CheckEIMIC` moving the train brake handle itself (type 3 controllers);
- fields meaning different things by type (`Mains` = engine running on a
  diesel; `EngineVoltage` the generator's voltage on a diesel-electric);
- engine code in three phases of MaSzyna's step (lever input, force, state
  after movement): a module puts them in one order.

## Why not the whole mover

- **Two physics.** The mover moves itself (1385-1449) and runs its devices
  and brakes only from inside its movement step (`compute_movement_`
  1536). A host that skips the mover's movement skips those too, or
  patches them out.
- **Neighbours written directly.** Brake pipe and main pipe flows go into
  the neighbours' reservoirs (`GetDVc` 4813, `UpdateScndPipePressure`
  4715), so results depend on the vehicles' order; the host's pipes would
  have to replace them inside MaSzyna's brake code.
- **Step size.** Built for steps of 10 ms or less; the induction motors'
  demand lag (6352) is unstable above roughly 8–16 ms (estimate). TSRE
  steps AI-only runs at 0.1 s.
- **Idle vehicles freeze** (4958-4975): compressors, batteries and valves
  stop until a neighbour wakes them.
- **Globals and quirks**: `Global.AirTemperature`, crash damage, pause,
  weights; a parser that is not re-entrant; unqualified `abs()` that
  truncates to integers without MaSzyna's precompiled header (about 46
  lines of `Mover.cpp`, engine maths included, and one in `hamulce.cpp`);
  the inverter factor scaling coupler forces (5020-5085).
- **Our tasks 03–05 bind TSRE's Lua.** The cab (`maszyna.cab`), sounds
  (`maszyna.sounds`) and devices (`maszyna.auxiliaries`) read script
  state; a mover would need all of that bound again to its fields, and
  users could no longer change a loco in Lua.
- **`libmaszyna`** vendors the mover (9,598 of its lines; its own `.fiz`
  parsers) and keeps MaSzyna's couplers and pipes, sub-stepping at 10 ms
  and refreshing neighbours before each sub-step; its quirks document runs
  to 600 lines. It shows the whole mover works when the mover owns the
  train, which is the opposite of TSRE's harness decisions (2026-10-24).

What the whole mover would give that modules do not: MaSzyna's devices
and procedures exactly, quirks included. They are 10 % of the mover and
Lua-sized (`maszyna.auxiliaries` took 321 lines for pumps, blowers and the
diesel start).

## What the Lua path misses today

Force (wrong performance):
- **Mechanical and hydraulic diesels** (32; torque converter 19, retarder
  16): run as a plain diesel on `PWR`. Needs `dizel_*`: torque curve,
  fill, clutch, gears, converter, retarder. *Big.*
- **Induction controller types** (57: E186 type 1, Elf type 2, 22 diesels
  with `UCList` brake positions): every `MCPN` position is a fixed share
  of full force (`tsre/fiz.lua:43-49`), so brake and hold positions pull.
  *Big.*
- **Series motors at speed**: 3000 V against MaSzyna's 0.95·`MaxVoltage`
  (3420 V) without live wires: ET22 −15 % at 80 km/h, −27 % at 100. The
  user's decision (2026-11-10); noted only.
- Smaller: induction motors without `FMAXMAX` and the current limit (the
  Elf's force differs by about 14 % at 120 km/h and 36 % at 160, estimate;
  the E186 by 2 % at most); diesel-electrics without
  the per-notch current limit, field relays, heating power (72) and
  shunting mode (12).

Procedures and devices (easier to drive than MaSzyna):
- **Vigilance and cab signal** (`Security:`; 502 powered and 202 control
  cars): missing (task 03 step 7).
- **Line breaker of MaSzyna electrics** (423): TSRE's automatic breaker
  (no ORTS key, `tsre/eng.lua:155`), so it closes by itself with a
  pantograph up and the **cab's main switch buttons do nothing**
  (`tsre/breaker.lua:29` acts only for manual, push-button and two-stage
  breakers); no voltage trips (`MinV`). A fault, not in the task docs.
- **Brakes cutting traction** (348): MaSzyna's pressure switch (7085: cylinder
  over 2 bar or pipe under 3.6 bar) and passive dynamic brakes: missing.
- **Speed control** (150), **open doors blocking traction** on induction
  units (131), **ED brake from the train and local brake handles** on
  induction motors (141; TSRE only on its own lever), **series dynamic
  brakes** (EP09, EW58, ET42: 27): missing.
- Small: converter, compressor and battery switches automatic; no train
  heating (503); no spring brake (168); no engine temperatures; lamps.

Host (not the loco's, but larger than most of the above):
- **Adhesion**: a fixed 0.33 × mass × g (`SimulationDriving.cpp:25`),
  no slip, no speed, no sand. MaSzyna's `Adhesive` (5220) is about 0.30 at
  rest and 0.20 at 100 km/h, with wheel slip and anti-slip. All 621.
- **Brakes**: one distributor (Open Rails'), friction by Open Rails' curve
  (`AirBrake.cpp:222-231`), not MaSzyna's valve classes (LSt 309, EStED
  179, EP2 85…), delay switch and shoe materials (0.41 at rest, 0.09 at
  100 km/h).
- **Couplers**: 7.5e6 N/m and 0.01 m for every car, unbreakable
  (`SimulationDriving.cpp:37-38`), not `kB`/`kC`/`Dmax`.
- **Mass**: no rotating mass (`Mred`), about 10–15 % less inertia
  (estimate).

## Performance

Not measured for the MaSzyna scripts. For scale (driving task 04 review):
a steam-shaped step cost 210–228 µs in TSRE's Lua against 3.3–3.9 µs in
C++; the electric script's own step 5.9 µs. The MaSzyna scripts add
`maszyna.sounds` and `maszyna.auxiliaries` each step; tens of µs per loco
is the expectation (estimate), 0.1–0.5 % of a core a loco at 0.01 s. Parts
that need sub-steps (the induction demand lag, a torque converter) are
cheaper as C++ sub-stepping inside a module than as Lua.

## Suggested direction (for the user to decide)

1. **Retire "MaSzyna's C++ mover as a vehicle model"** in favour of:
   - Lua loco scripts as the frame (as now);
   - **native modules from MaSzyna's engine code** where the Lua path is
     wrong or would need sub-steps: first the mechanical/hydraulic diesel
     (`dizel_*` with gears, clutch, converter, retarder: 32 vehicles), then
     induction motors with `CheckEIMIC`'s controller types, `FMAXMAX` and
     the current limit (141, the 57 controllers first). The series motors
     stay in Lua (`maszyna.dcmotors` matches).
   - **MaSzyna's brake equipment as host brake models** (a simulation
     task): `hamulce`'s valves and handles beside TSRE's distributor, on
     the host's pipes, and shoe friction by material. Self-contained code
     (no globals, no mover fields).
   - **Adhesion** in the host: MaSzyna's formula, slip, sand (and weather).
2. **Lua work, each small**: the breaker fault above (MaSzyna's manual
   main switch with its checks and voltage trips), vigilance and cab
   signal, the pressure switch, speed control, the door interlock, ED from
   the brake handles, series dynamic brakes.
3. MaSzyna's own code in a module keeps its MPL 2.0 header per file, which
   TSRE's GPL-3.0-or-later allows (MPL §3.3).

## Decisions (the user, 2026-11-11)

1. **The whole-mover task is retired.** Engines get modules; each module
   is C++ or Lua by its measured cost and its complexity (so question 4,
   measuring, is part of it).
2. **Order: mine.** And the force fixed where it is clearly wrong: the
   mechanical and hydraulic diesels, the induction controllers, and the
   series motors' line voltage if MaSzyna's locos expect MaSzyna's.
   Task [06](../06-engines.md).
3. **Brakes: MaSzyna's as inspiration, not a copy.** TSRE's trains mix
   ENG and `.mmd` vehicles, so the host's brake model stays one for all;
   MaSzyna's valve classes inform it (a later host task).
