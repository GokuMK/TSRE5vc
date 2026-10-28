# Task 03 - The diesel loco

Status: designed 2026-10-29; built on `feature/driving` after task 02.

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
