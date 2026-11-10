# TSRE's loco scripts: what they read from a vehicle

Inventory for task maszyna 03 (2026-11-10, at dae242f8): every read of
vehicle data in `appdata/0.7/scripts/loco/` (written `loco/` below), for
turning the modules' ENG reads into config tables. Units as the key
catalogue (`src/tsre/trains/VehicleKeysTable.inc`) hands them to scripts;
"raw" is a path not in the catalogue (a bare number as written).

**Every Lua read happens in `init`** or helpers it calls; no module reads
`vehicle` in `update` or `command` (they are not given it; `inputs.vehicle`
is a lightuserdata for native modules). The one read outside init is C++:
`tsre.steam` reads its config from the ENG document on the first
`steam.step`, inside `update`.

## The host's API (`src/tsre/sim/LocoScript.h/.cpp`)

- `init(state, vehicle)` when the model is made (.cpp:348-373); `update(dt,
  inputs, state, out)` each step (461-526); `command(name, value, state[,
  from])`, `from` "line" for another unit's (592-607); a 3D cab's `cab.<n>`
  goes through `tsre.cabview.commands` first (381-428); `save(state)`
  optional (none of the three defines it, so `s.config` is saved with the
  run's state); restore swaps the table in after `init` (544-556).
- `inputs` (rewritten each step): speed, gradient, adhesionLimit,
  lineVoltage, airTemperature, brakePipe, brakePipeVolume, brakeCylinder,
  mainPipe, heatingPipe, heatingSteam, cab, cabFacing, vehicle. `out`:
  motiveForce, brakeForce, brakePipeAir, mainPipeAir, independentBrake,
  bailOff, heatingSupply (zeroed before each update), `out.values` (copied
  whole).
- `send(name[, value])` to UI, cab and sounds; `line(name[, value])` to the
  train's other units; a script error gives one `scriptError` and no force.
- `vehicle`: `number(path[, i[, default]])` in SI, `text`, `values`, `all`,
  `blocks`, `has`, `each` (scoped readers), the proxy `vehicle.engine.x`,
  `exhaustKinds` (from the C++ definition). Without a document it reads as
  empty (dae242f8).
- Lua states: one for the driven train, one (`shadowScripts`) shared by every
  heard train, stepping at 0.1 s. Each file loaded once per state. Limits
  100 ms per call, 64 MB per state. **No file access in the sandbox**: a
  script cannot open a `.fiz`.
- `scriptFor`: no folder or no document: none; else kind 4 electric.lua, 5
  diesel.lua, 6 steam.lua.

## Reads by module

- **traction** (`init(s, vehicle, runUpDefault)`, 27-89, `s.config`):
  maxForce, maxPower, maxContinuousForce (`engine(maxforce` / `maxpower` /
  `maxcontinuousforce`, 0); maxVelocity (`engine(maxvelocity`, positive,
  1000); maxCurrent; continuousTime (1800 s); unloadingSpeed; curves
  (`engine(ortsmaxtractiveforcecurves`, else
  `engine(ortstractioncharacteristics`, raw); force and power ramps (three
  each, chained defaults; derived from `engine(runuptimetomaxforce` when
  none given); brakeCutsPower (proxy `doesbrakecutpower`) and
  brakeCutPressure (4 psi); dynamic brake: max force, min usable / fading /
  maximum effective / fade-out speeds (mph→m/s), fade-out share, delay,
  `engine(ortsdynamicbrakeforcecurves`, ramps.
- **curves**: `read` (blocks) and `readTable` (values); output already plain
  (`{negative, {t, v={}, f={}}…}`).
- **levers** (`read(vehicle, path)`, 58-72): min, max, step, default and
  each notch's value, smooth and type (mapped to Open Rails' notch kinds,
  17-41). The lever (spec mixed with notch, value, moving…) lives in the
  **state**, not `s.config`.
- **controls** (`init`, 52-77): throttle, trainBrake (lever or plain, a
  step ≤ 0 becomes 0.1), engineBrake and dynamicBrake (nil if step ≤ 0),
  combined (`engine(enginecontrollers(combined_control`); headlight reads
  `s.config.keyHeadlights`.
- **brakes** (`init`, 38-70; `controller()`, 23-36; all `positive()`):
  mainVolume (0.3 ft³), mainMax (130 psi), restart (110 psi),
  compressorRate (from `ortsmainreschargingrate` or the compressor rating ÷
  volume), charging and quick charging (21 psi/s), service and emergency
  time factors, engine brake apply and release (12.5 psi/s), autoRelease
  (`wagon(maxreleaserate`, 1.86 psi/s), ep and twinPipe (text
  `wagon(brakesystemtype`), mainPipeMax. Train and engine brake
  controllers: ten paths each (max system pressure 90 psi, overcharge 95,
  release 5, quick release 10, overcharge elimination 0.036, apply 2, slow
  apply 1, emergency 10 psi/s, full service drop 26, min reduction 6 psi).
  State seeded from config (main reservoir, equalising).
- **breaker**: kind (text `engine(ortscircuitbreaker`: manual, pushbuttons,
  twostage, else automatic), closing delay.
- **supply**: battery mode, delay, default on; master key mode, delay off,
  headlight control.
- **relay**: kind (exact case: Automatic, Manual, PushButtons), delay.
- **pantographs**: count (`#all("wagon(ortspantographs(pantograph")`, 2 to
  4), delays (state only).
- **exhaust**: `vehicle.exhaustKinds`.
- **steamheat** (`init(s, vehicle, boiler)`, 65-92, `s.heat`): max pressure
  (not fitted if ≤ 0), lever, water and oil tank capacities, chimney.
- **dieselengine** (`init`, 333-367): per engine block through
  `vehicle.each("engine(ortsdieselengines(diesel")` (rpm values, power,
  tables, oil, temperatures, consumption, exhaust colours); loco-level
  fallbacks (`locoKeys`, 148-164). `establish` and `railPower` are Open
  Rails' derivations needing maxPower, curves and efficiency first.
- **electric.lua** `init` (51-74): traction, power-on and aux power-on
  delays, breaker, supply, brakes, controls, pantographs, steamheat(true).
- **diesel.lua** `init` (114-143) with `derive` (45-112): traction(10),
  exhaust, then derive interleaving seven reads with Open Rails'
  derivations (efficiency, maxPower rewritten, engines, fallbacks,
  maxForce from drive wheel weight, continuous-force speed, power limited,
  ramps), then delays, mechanical compressor, tank, relay, supply, brakes,
  controls, steamheat.
- **steam.lua** `init` (60-90): supply, brakes, controls, steamheat, the
  regulator and seven more levers; the thermodynamics in C++
  (`SteamConfig`, read on the first step, cached per definition).
- **cabview**: no vehicle keys (the `.cvf` controls and `out.values`).

## Composition

| | electric | diesel | steam |
|---|---|---|---|
| init | traction, breaker, supply, brakes, controls, pantographs, steamheat | traction, derive (dieselengine), relay, supply, brakes, controls, steamheat | supply, brakes, controls, steamheat, its levers |
| update | supply → pantographs → breaker → controls → traction → compress → steamheat → pantographs.step → main pipe → drive → values | engines → supply/relay → controls → traction → compress → steamheat → fuel → main pipe → drive → values | supply → controls → levers → compress → steam.step (C++) → main pipe → drive → steamheat → values |

Shared: levers, curves, controls, supply, brakes, steamheat, exhaust, util,
traction (electric, diesel). Own: electric's power supply and voltage
filter, pantographs, breaker; diesel's derive, engines, relay, fuel;
steam's C++ core and levers.

## Constraints and traps

- Save format: numbers, strings, booleans and nested tables only; renamed
  config fields break older saves (only tests save today).
- A part's documented shape (`driving/02-driving-harness.md:1225-1240`):
  `init(s, vehicle)` reads keys into `s.config`, state in `s`, `values(s,
  o)`, a `commands` table; user scripts may require these modules.
- Regression net of the earlier module split: the driving checks printing
  the same numbers, USA1's drive-run CSVs byte for byte, the OR_PSK corpus.
- Traps for a config refactor: file data also in the state (lever specs,
  combined, pantograph delays, `s.heat`); default rules differ per field
  (`number()` keeps 0, `positive()` replaces ≤ 0; chained defaults; bare
  units mph, psi, ft³ are ENG rules and stay in the ENG reader); diesel's
  order (derive overwrites maxPower and maxForce); a config shared per
  definition would be mutated (derive, `controls.init`, `levers.set`): a
  fresh table per loco; value domains are Open Rails' (relay kinds exact
  case, notch kinds); exhaust kinds from C++; steam's config is C++ and
  never sees a Lua table; module-level scratch tables are shared by every
  loco in a state.
- Size: about 400-450 Lua lines touched, two thirds moved unchanged into one
  ENG reader of about 250-300 lines (about 113 paths); about 100 lines of
  real change.
- Latent: `brakes.controller` builds
  `engine(ortsenginebrakescontrollermaxoverchargepressure` and
  `…overchargeeliminationrate`, not in the catalogue: a bare number there is
  read raw (Pa), not psi.
