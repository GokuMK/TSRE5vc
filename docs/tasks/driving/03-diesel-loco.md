# Task 03 - The diesel loco

Status: designed 2026-10-29; built on `feature/driving` after task 02 (steps 1 to 5 done; open questions at the end).

The user's request (2026-10-29), after task 02 split `electric.lua` into
modules the loco scripts share: "if we have those modules, can we try
implementing diesel engine?"

Every diesel engine (`Type ( Diesel )`) gets TSRE's `diesel.lua`, after Open
Rails' diesel locomotive, as every electric engine gets `electric.lua`
(task 02, decision 4). Until now diesels got a C++ stand-in force.

**The spec:** [reports/or-diesel-loco-spec.md](reports/or-diesel-loco-spec.md),
read from Open Rails `8aafb3147` (2026-10-29). It describes behaviour only;
no code is taken. Its sections are written "spec §n" below.

**The scope:** the diesel-electric. In the content here that is 421 of 457
diesel engines:

| | diesel engines | with `ORTSDieselEngines` | with a gearbox |
| --- | --- | --- | --- |
| OR_PSK | 435 | 411 | about 12 |
| MSTS | 8 | 0 | 2 |
| TS_STARTER_ROUTE | 14 | 9 | 0 |

The gearbox (diesel-mechanical and hydraulic, spec §15) is a large part of
Open Rails of its own: a later step. Until then a loco with a gearbox runs
as a diesel-electric.

## What it builds on

The modules of task 02 (`appdata/0.7/scripts/loco/tsre/`):
- **Reused as they are:** `util`, `levers`, `curves`, `controls` (the
  cab's levers, direction, emergency, lights, horn), `supply` (battery and
  master key; on a diesel they gate only the cab and low voltage, as on the
  electric, spec §10).
- **Extended:**
  - `traction`:
    - Open Rails' diesel force law: `MaxForce × t` and `MaxPower × t`,
      linear, no MaxVelocity band, the unloading speed, the share of
      engines running;
    - the engine-power cap;
    - power ramps;
    - the dynamic brake's own ramps, and its need for main power on a
      diesel.
  - `brakes`: a mechanical compressor, charging with the engine's RPM
    (spec §11).
- **New:**
  - `tsre/dieselengine`: the engines. It reads `ORTSDieselEngines` or
    builds one engine from the legacy MSTS keys, with Open Rails' defaults
    (spec §2, §3). It holds their states, RPM, power, fuel flow,
    temperature and oil pressure (spec §4, §5).
  - `tsre/relay`: the traction cut-off relay, the circuit breaker's sibling
    (spec §8).
  - `diesel.lua`: the diesel's power supply (spec §9), the fuel tank, the
    parts put together in Open Rails' order (spec §0), the commands.
- **The vehicle API:** `vehicle.each(path)` gives a reader for each block at
  a path, scoped to it, with the same functions as `vehicle`. A loco's
  engines are its `Diesel ( … )` blocks, read one by one; reading by path
  alone would mix engines whose keys differ.

## TSRE's choices on the spec's open decisions

Kept open until the result is tested, as with the electric's (task 02):

1. **Engines:** n engines per loco, engine 0 the player's (the cab's
   engine button), the others helpers (the helpers' button, with the other
   locos' engines). Every block in the content has one.
2. **`TractiveForcePowerLimited`:** Open Rails' automatic rule kept. Content
   is tuned for Open Rails: OR_PSK's BR232 gives 826 of its 1830 kW in both.
3. **The start hang** (spec, odd item 1): fixed. An engine starting at or
   above 0.9 × StartingRPM moves on to its confirm RPM, so a restart while
   it stops cannot leave it starting for ever.
4. **Triggers 23 and 24 twice** (the engine, then main power): kept. OR_PSK's
   sound files start and stop the engine on them.
5. **A PushButtons relay** does not close at a hot start: kept (the close
   button closes it). No content here has one.
6. **The Manual relay's sounds** 150 to 152 (the breaker's): kept.
7. **An empty tank:** 24 once and the engines stopped, not every step.
8. **Defaults:** Open Rails' results kept (idle 150 rpm where none is
   given; no engine power without MaxPower). Equal idle and full-power fuel
   use no longer divide by zero: no friction then.
9. **Fuel and temperature:** Open Rails' models, friction and the shaft's
   inertia included. They feed only the values, sounds and the tank.
10. **The outside temperature:** 15 °C for now. Open Rails takes it from the
    season, latitude and height.
11. **Electric train supply** (its power and RPM): later.
12. **Units:** values in SI (Pa, °C, m³, W); the widget shows the user's
    units.
13. **Multiple units:** as Open Rails. Relay orders and the battery go on
    the control line; the master key, the player's engine and quick power
    do not. The helpers' button starts or stops the other locos' engines.
14. **Order:** as Open Rails. The engines step first, on the levers and
    main power of the step before.

## Commands and values

**Commands** (beside `controls`' and `supply`'s):
- `engine`: on or off, the player's engine (spec §9). Off is refused with
  the throttle open (Open Rails: "close throttle then re-try"); the script
  sends `engineLocked`.
- `helperEngines`: on or off. In the cab, the loco's other engines; on the
  line, the other locos' engines.
- `tractionCutOffRelay`: close, open, released (push buttons), as the
  breaker's.
- `power`: Open Rails' quick power on and off.
- `refuel`: the tank filled at once (Open Rails' immediate refill).

**Values** (beside the parts' own): `engineState` (stopped, starting,
running, stopping), `engineRpm`, `enginePower`, `helperEngines`,
`oilPressure`, `engineTemperature`, `fuel`, `tractionCutOffRelay`,
`mainPower`, `auxPower`, `availablePower`.

**The widget:** an engine row (on, off, its state) and a relay row, shown
for a loco publishing `engineState` and `tractionCutOffRelay`; readouts for
RPM, fuel, oil pressure and temperature.

## Steps

1. **The spec** (done 2026-10-29) and this design.
2. **The diesel:**
   - `vehicle.each`;
   - `tsre/dieselengine`, `tsre/relay`;
   - `traction` and `brakes` extended;
   - `diesel.lua`;
   - `LocoScripts` giving diesels `diesel.lua`.

   **Checks (`driving`):**
   - the force against the formulas (`MaxForce × t`, `MaxPower × t / v`);
   - the engine's power capping it at low RPM;
   - the ramps;
   - a legacy diesel's built engine against spec §3's Dash 9;
   - the start and stop timings;
   - the RPM idling without main power;
   - a Manual relay's delay;
   - the sounds;
   - fuel drawn, an empty tank stopping the engine;
   - two engines, one stopped;
   - save and restore.

   The electric's checks and drive runs must give the same results, except
   where its dynamic brake's own ramps differ (an ENG with
   `RunUpTimeToMaxForce`).
3. **The widget** rows and readouts, their texts translated.
4. **The corpus:** `driving-corpus` runs every diesel too (OR_PSK, MSTS,
   TS_STARTER_ROUTE). For each:
   - no script error;
   - powered;
   - pulling at a low speed.
5. **Against Open Rails:**
   - USA2's freight (`nnmxdgds`, three Dash 9s), already run in Open Rails
     in task 02 against TSRE's stand-in;
   - BNSF_SCENIC's ES44AC (`3_CanadianImports`), with `ORTSDieselEngines`.

Each step is built (`all`) and checked with ctest and the suites before its
commit. AI-only runs must stay as they are.

## Step 2 done (2026-10-29)

**Built:**
- **`vehicle.each(path)`:** a reader for each block at a path, its paths
  starting there; `SimisDocument::all(path, block)` beneath it.
- **`tsre/dieselengine`:**
  - reads each `Diesel ( … )` block, or builds one engine from the legacy
    keys, with Open Rails' fallbacks;
  - builds the standard power curve, the rail power by RPM, the auxiliary
    power, the friction, the shaft's inertia and the fuel table where they
    are missing (spec §3);
  - steps the states, the RPM law, power, fuel flow, temperature and oil
    (spec §4, §5);
  - Open Rails' table interpolation, its rounding modes included.
- **`tsre/relay`:** the three kinds, their orders and buttons, sounds 214
  to 218 (the Manual's 150 to 152).
- **`tsre/traction`:**
  - the force laws (`c.forceLaw`);
  - the engine-power cap (`powerCap`);
  - Open Rails' ramp, with power ramps and the limit at full throttle;
  - the dynamic brake's own force and power ramps, and its need for main
    power (`c.dynamicNeedsPower`);
  - `RunUpTimeToMaxForce` defaults to 10 s for a diesel.
- **`tsre/brakes`:** the compressor's rate share.
- **`diesel.lua`** (335 lines, comments included):
  - what Open Rails derives at load (spec §3): efficiency, rail power,
    MaxForce from the driving wheels' weight, the continuous force, the
    power ramps, the automatic power limit;
  - the power supply (spec §9), quick power, start in motion, the tank;
  - the commands and values of the design.
- **`LocoScripts`:** `Type ( Diesel )` engines run `diesel.lua`.
- **Zero RPM ranges** guarded. OR_PSK's dead locos ("nieczynna", idle and
  maximum 0 rpm, no power) gave NaN; they now stand stopped, as in Open
  Rails.

**Checks (`driving`, 47):**

| Check | Expected | Got |
| --- | --- | --- |
| Legacy engine (spec §3's Dash 9), power at idle | 281,988 W | 281,987.7 W |
| Fuel at idle | 37.85 l/h | 37.84 l/h |
| Start to running | 9.2 s | 9.16 s |
| Idle to full RPM | 15.6 s | 15.6 s |
| Full RPM back to idle | 15.6 s | 15.6 s |
| Stop | 8.8 s | 8.85 s |

The spec's timings came from a model of the RPM law at 60 steps a second.

- **Starting:**
  - 23 fires at the start and again at main power;
  - the relay closes (215, 216) and auxiliary power comes on (227);
  - the stop fires 214, 24, 228 and 24;
  - stopping with the throttle open is refused (`engineLocked`).
- **A restart while stopping,** at 314 rpm, is running again in 1.55 s.
- **A Manual relay (the SP42's):**
  - closed at 6.04 s, auxiliary power at 10.02 s, main power at 11.05 s;
  - sounds 217 151 152 227 23;
  - the RPM stays at idle until main power, though the throttle is open.
- **Force:**
  - 163,150 N at t 0.5 and 10 m/s, `MaxPower × t / v`;
  - force × speed never above the engines' power, and held to it 542
    steps of 2,000 while the RPM rose.
- **Two engines:**
  - idle 400 and 600 rpm, each read from its own block;
  - 163,150 N, and 81,575 N with the helper stopped.
- **Dynamic brake:** none without main power; 33.3 kN 10 s after it (its
  own ramp, 3.33 kN/s).
- **Fuel:**
  - 0.2 l empty after 19.04 s (19.02);
  - 24 three times: the tank, main power, the engine;
  - it does not start empty; refuelled, it runs again.
- **Saved and restored:** the same steps after.
- **The cost:** 14.8 µs a step (the electric 11.7 µs; it now also works
  out its force at full throttle, Open Rails' limit). Release.

**The corpus (step 4, done with it):** `driving-corpus` runs every diesel
as well as every electric.

| Content | Diesels | Script errors | Without power or force |
| --- | --- | --- | --- |
| OR_PSK | 435 | 0 | 0 (of those with a MaxForce) |
| MSTS | 8 | 0 | 0 |
| TS_STARTER_ROUTE | 14 | 0 | 0 |

OR_PSK's 81 dead locos have no MaxForce and stay without power, as meant.
Its 817 electrics give the same results as before.

**What Open Rails' rules give on real locos:**
- **ES44AC:** its curves give 2,805.9 kW against its engine's 2,803.8 kW,
  so the automatic power limit is off. As in Open Rails, its RPM does not
  limit its force.
- **Dash 9:** limited. Its ramps are as spec §3 gives them (21.7 kN/s,
  223 kW/s).

**The electric unchanged:**
- the 36 earlier checks print the same numbers (its saved state is larger);
- USA1's two drive runs give the same CSVs byte for byte.

Its dynamic brake now has its own ramps and is held to its full-lever force.
That differs only for an ENG with `RunUpTimeToMaxForce` or ramp keys,
where it is now as Open Rails has it.

## Step 3 done (2026-10-29)

**The widget (`loco.rml`, `loco.lua`, `loco.rcss`):**
- **A part's row shows only for a loco publishing its value:**
  - the electric's pantograph and breaker rows;
  - the diesel's engine row (on and off, its state, the helpers' on and
    off) and its traction relay row (close and open, held for push
    buttons, its state).
- **The same holds for the readouts.** The diesel adds engine RPM, fuel, oil
  pressure and engine temperature.
- **Units:** fuel, oil pressure, temperature and power are shown in the
  user's units.
- **Texts:** seven new ones, in Polish too.

**Checks:**
- **`rmlui` (36):** a diesel's values show its engine and relay rows, not
  the pantographs' or the breaker's, and the RPM readout but not the line
  voltage. Its buttons send `engine = off`, `tractionCutOffRelay = close`
  then `released`, and `helperEngines = on`.
- **The capture `tests/renderer/driving-diesel.json`:** USA2's `nnmxdgds`,
  the player's Dash 9s driven from the start, the panel over the view.
  - Idle: 315 rpm, the relay closed, power on.
  - Notched up: at half throttle, 607.5 rpm and 92.9 kN.
  - Stopped: 0 rpm, the relay open, power off, no force.
  - The freight starts moving (its service's `StartingSpeed`), so its
    engines start warm at the throttle's RPM (95 °C), as in Open Rails.

## Step 5 done (2026-10-29): against Open Rails

**The tools:**
- **`scripts/orts-drive-run.sh`** records HUD pages too
  (`ORTS_HUD_PAGES="2 6"`: the locomotives, the forces).
- **`scripts/drive-compare.py`** adds the lead loco's force and RPM.
- **TSRE's drive log** (`TSRE_SIM_DRIVE_LOG`) adds `engineRpm` and
  `trainForce` (every vehicle's force).
- **The runs:** `reports/or-driving-comparison.md`, its last two sections.

**The diesel agrees with Open Rails:**
- **USA2's Dash 9s at 25 %:** the same RPM (461) and engine power (1720 kW
  available). Their force follows `MaxPower × t / v` and `MaxForce × t`.
- **BNSF_SCENIC's ES44AC** (`ORTSDieselEngines`):
  - the RPM within 5 rpm, rising and falling;
  - 2.80 MW a loco at full throttle in both.

**What else differs, all outside the diesel:**
1. **TSRE reads MSTS's `Friction` wrong** (task 01, `resistanceOf`).
   - It fits `C2 × v^(E2+1)` where Open Rails fits `C2 × v^E2`.
   - On the ES44AC's train that gives 722 kN of resistance at 20 m/s,
     against Open Rails' 90. TSRE's train tops out at 16.8 m/s, Open
     Rails' at 20.8.
   - With Open Rails' form (tried, not committed) TSRE's train follows Open
     Rails' within a few percent: 10 m/s at 108.0 s against 108.6 s, top
     speed 21.4 against 20.8 m/s.
   - The same coefficients feed the AI's performance. With the fix, the AI
     traces of USA2, BNSF_SCENIC and PRUDNIK change: trains a little faster,
     the same events. The other five routes are unchanged.
   - **Fixed after step 5 (below).**
2. **Curve and wind resistance:**
   - Open Rails adds them: 14 to 39 kN of curves and up to 38 kN of wind
     over these trains.
   - TSRE's driven train has only its Davis resistance, so USA2's freight
     stands at 209 s in TSRE and at 188 s in Open Rails.
3. **The brakes at a standing start:** Open Rails' train starts with its
   brakes applied (15 psi) and releases them in 17 s; TSRE's starts
   released.

## After step 5 (2026-10-29): MSTS's `Friction` fixed

The user's: the fix goes in with the rest, to main when merged.

- **`resistanceOf` (`VehicleDefinition.cpp`) fits MSTS's `Friction ( C1 E1
  V2 C2 E2 )` as Open Rails fits it:**
  - `C1 × V2^E1 + C2 × V2 + C2 × v^E2`;
  - by least squares over V2 to 80 mph, in integrals as Open Rails solves
    it, not sampled;
  - for V2 at most 4.4407 m/s, Open Rails' "10 mph".
- **Check (`vehicle-file`, 38):**
  - a straight line gives itself back: 227.99 N + 2 v;
  - TS_STARTER's ES44AC gives Open Rails' coefficients, 1990.05 + 72.741 v
    + 9.6875 v², from a port of Open Rails' closed form, to within a
    millionth.
- **The ES44AC's train against Open Rails:**

  | | TSRE | Open Rails |
  | --- | --- | --- |
  | Reaches 10 m/s | 108.0 s | 108.6 s |
  | Reaches 20 m/s | 272 s | 284 s |
  | Top speed | 21.4 m/s | 20.8 m/s |

  The small lead left is the curve and wind resistance TSRE leaves out.
- **Unchanged:**
  - USA1's drives: the same speeds and distances;
  - the USA2 freight: its cars give ORTSDavis or no `Friction`.
- **The AI traces** change on USA2 (8 lines), BNSF_SCENIC (90) and PRUDNIK
  (122). Trains run up to 6 s sooner, with the same events and the same
  counts finished, stuck and in deadlocks. The other five routes are
  unchanged.

## Open questions

1. **Curve and wind resistance** for the driven train (Open Rails: 14 to 39
   kN of curves, up to 38 kN of wind on these trains).
2. **The brakes at a standing start**, applied as Open Rails starts them.
3. **Lua's watchdog on Windows:** task 02's open question 6.

## Not in this task

- **The gearbox** (spec §15): diesel-mechanical and hydraulic, clutches,
  gears, stalls.
- **Electric train supply:** its load and RPM.
- **Others:**
  - mass following fuel;
  - steam heating;
  - scripted power supplies and relays;
  - dual mode;
  - activity failures;
  - exhaust;
  - the HUD.
