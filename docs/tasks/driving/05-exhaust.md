# Task 05 - Exhaust: smoke and steam

Status: designed 2026-10-30; the user's answers in (below). Steps 1 (A), 2
(diesel) and 3 (steam) done 2026-10-30 on `feature/driving`; shadows next.

The user's request (2026-10-30): "Ok, write a task for exhausts. And
suggest who should do what part. Here simulation -> world consists. And
rendering with our render team."

**Decided with it (the user's, 2026-10-30):**
- **Full volumetric effects,** so they must be limited in number and
  distance.
- **Only trains with a vehicle model have exhaust:**
  - the driven train;
  - the shadows of AI trains within earshot (1,500 m, sound task 02).

  That is plenty, since volumetric effects must be limited anyway. Later,
  the shadows' radius becomes a setting, probably from the rendering
  distance.

## Content (~/ORTS, 2026-10-30)

Counted with include files read. OR_PSK keeps its effects in include files
(`common.inc/Lokomotywy/*/Effects_*.inc`). A first count without them saw
only 65 engines.

**Diesels: 440 of 457 have exhausts** in `Effects ( DieselSpecialEffects
( … ) )`:
- every one has `Exhaust1`; 95 also have `Exhaust2`, and 32 have
  `Exhaust3`;
- by package: OR_PSK 419, TS_STARTER_ROUTE 13, MSTS 8;
- 54 OR_PSK diesels with a steam heating boiler also have `GeneratorFX` and
  `HeatingHoseFX`. The same include files give their
  `MaxSteamHeatingPressure`. They belong to the SP45, SP42, ST45, SN61 and
  SM42 families.

**Steam: MSTS's five locos** have `Effects ( SteamSpecialEffects ( … ) )`.
Each has `StackFX`, two `CylindersFX`, `SafetyValvesFX`, `WhistleFX` and
`DrainpipeFX`. Two of them (the Scotsman and the 380) are fitted for steam
heating. OR_PSK has include files for Polish steam locos (`Parowozy`), but
no steam engines installed here.

**Electric engines:** none have emitters.

**Wagons:** 110 OR_PSK carriages have `Effects ( SpecialEffects
( HeatingHoseFX … ) )`, 178 emitters in all. That is steam heating, which
TSRE does not simulate yet (see "Steam heating" below).

**An emitter is seven numbers:**
- its place (x y z, m, in the vehicle's frame);
- its direction (x y z);
- its nozzle's diameter (m).

For example: `Exhaust1 ( 0 4.83 -3.683  0 1 0  0.2 )`.

**Open Rails knows about 50 names.** Besides those above, these are unused
here:
- per-cylinder cocks (`Cylinders11FX` …) and `CylinderSteamExhaust1FX` …;
- the compressor, generator, injectors and blowdown;
- the ejectors, water scoop, tender overflow and sanders;
- the steam brake, counter-pressure brake and booster;
- wagons' `WagonSmokeFX`, `WagonGeneratorFX`, `BearingHotBoxFX` and the
  heating steam traps.

**In TSRE now:** the reader's key table lists the blocks
(`src/tsre/trains/VehicleKeysTable.inc`), but nothing reads the emitters.

The render team's content (C:/trainsim: CMK, PKOR) may differ. They count
theirs at the start, as for train lights.

## Open Rails, as a functional reference

**The split:**
- Every step, the simulation sets each emitter's output: an initial speed
  (m/s), a volume (m³/s), the particles' life (s) and a colour.
- The viewer turns them into camera-facing particles: at most 50 a second
  per emitter, each living at most 50 s.

**Diesel** ([the spec's](reports/or-diesel-loco-spec.md) "Exhaust
(visual)"):
- Particles and their size lie between the ENG's idle and maximum values
  (`DieselSmokeEffect…`), following the RPM.
- While the RPM rises fast: ×2, with the transient colour. While it falls:
  ×0.75, with the decel colour.
- None while the engine is stopped.
- Only engine 0's exhaust is shown.
- The colours come from `ORTSDieselEngines` (`ExhaustColor`,
  `ExhaustTransientColor`, `ExhaustDecelColor`).

**Steam** (`UpdateFX`, SLOCO:3446-4245; not yet in the step spec):
- **The stack's volume** is the steam leaving it: from the cylinders,
  blower, radiation, compressor and generator. It is taken in kg/s at
  100 °C and 1 bar. Standing, only the blower's steam and the fire.
- **The stack's speed** comes from the exhaust's release pressure.
- **The smoke's colour** comes from the fire: the burn against the steam
  made, the damper, and the fire's mass against its ideal, smoothed over
  2 s.
- **The cylinder cocks** give a set volume while open with the regulator
  open.
- **The whistle** gives steam while the horn sounds; the safety valves
  while they lift.
- **The drain pipe** is always off ("not used in MSTS").

Open Rails' diesel values are particle counts; its steam values are
volumes and speeds.

## The user's answers (2026-10-30)

1. **Physical values:** "We want to enjoy some real world simulations."
2. **The order:**
   - A and B here;
   - then `feature/driving` is merged, after one interface UI fix of the
     user's;
   - then back to the driving tasks, while the render team does C.
3. **The OpenGL renderer:** decided on the go.
4. **Steam heating:** "some support should be done here as that is
   important part of steam era." The user asked whether it is big and
   whether it could be done now (see "Steam heating" below).

## Who does what (agreed)

| Part | Who | Where |
| --- | --- | --- |
| A. The interface: emitters and their values | here | `feature/driving`, merged into main |
| B. The values: loco models, shadows, frames, the world's consists | here (driving) | `feature/driving`, merged into main |
| C. Drawing: volumetric smoke and steam | the render team | their branch from main, QRhi |

**Why this split:**
- **The values come from code only the driving work touches:**
  `diesel.lua`, the steam core, the shadows, the frames, and
  `SimulationController::placeTrains`.
- **The drawing belongs with the render team's code:**
  - QRhi;
  - the clouds' volumetric lighting (renderer task 28);
  - the rain's particles (`RhiRain.cpp`);
  - the weather's wind, temperature and dew point (renderer task 33);
  - the hardware budget on the Deck.
- **Train lights (renderer task 34) were split the same way:** the
  renderer started from a test state, and the simulation supplies the real
  one.
- **The render team starts from main** after the merge, with the test
  state and the real values both in place. The two sides meet only at A.

## The interface (proposal, to agree first)

**Emitters (static, read once).** Each vehicle definition lists its
emitters: kind, place, direction and nozzle diameter.
- Kinds are named as the files name them: `exhaust1`, `stack`,
  `cylinders`, `safetyValves`, `whistle`, …
- Several of a kind keep their order, for example a loco's two
  `CylindersFX`.

**Values (per frame, per vehicle, per kind; SI):**
- `flow`: gas or steam leaving the kind's nozzles together (m³/s); 0 means
  off. Its emitters share it equally (as built, 2026-10-30: so a script
  need not count them).
- `speed`: its speed at the nozzle (m/s).
- `temperature` (°C): how fast it rises. For steam, also how long it
  shows, given the weather's temperature and dew point.
- `steam` (0-1): the share that is water vapour; the rest is smoke.
- `soot` (0-1): how dark the smoke is.
- **The stack's beats:** puffs a second (as built, without a phase).
  - The renderer places puffs at its own frame times.
  - So a shadow's 0.1 s step bunches nothing in the picture; the sound
    pulses' limit (sound task 02) stays out of it.
- **A colour from the ENG** where it gives one (Open Rails' diesel
  colours); otherwise the renderer's.

**How they travel:** as ordinary values in the frames, which already carry
every value every frame. Names are set in step 1, for example
`exhaust1Flow`, `stackSoot`, `stackBeats`.

**Cost (estimate):** frames cost about 0.14 µs a value (20-25 µs for 161
values). A diesel adds about 5 values, a steam loco about 30, so a few µs a
frame per loco.

**On the main thread:** `placeTrains` hands each engine its exhaust state,
as it already hands the sound input (`Eng::setSoundInput`) and the
headlight (`setHeadlight`). Vehicles without a model get none. The
renderer takes the state together with the vehicle's place and speed in
the world.

**The renderer's own part:** everything after the nozzle:
- the exhaust leaving at the train's speed and slowing to the wind's;
- rising, spreading and thinning;
- its light;
- the limits.

## Steps

### Here (B)

1. **The interface (A):**
   - the emitters' reader on the vehicle definitions;
   - the value names;
   - each engine's exhaust state on the main thread;
   - a test state for the render team: every emitter on, at set values,
     as in task 34.

   Checks: the 440 diesels, 5 steam locos and 110 carriages are read, with
   places and directions as in the files, include files too.
2. **Diesel:** `diesel.lua` publishes its exhaust from RPM and load, as
   Open Rails does, turned into flow, speed and soot, with the ENG's keys
   and colours.

   Checks: idle, notching up (transient), notching down, stopped,
   starting.
3. **Steam:**
   - add `UpdateFX`'s effects to the step spec (§11);
   - the core gives the stack (steam, smoke, beats), the cocks, the
     safety valves, the whistle (with the horn) and the blower.

   Checks on the Scotsman: standing with the blower, starting with the
   cocks open, running, safety valves lifting.
4. **Shadows:** a heard train's exhaust comes with its values. The
   shadows' horn already blows the whistle.

   Checks: a heard steam train starting, a heard diesel notching up.
5. **End to end:**
   - log the exhaust state on the engines in a capture run (`runFor`);
   - the driving suite's frame cost with the new values.

### The render team (C)

1. **Drawing from the test state:** every emitter of a static consist on.
2. **Volumetric smoke and steam:**
   - carried by the train and the wind;
   - rising with its temperature, spreading and thinning;
   - lit by the sun and sky, and shadowed by the scene;
   - steam fading by the dew point spread;
   - puffs from the beats.
3. **Limits:** how many and how far, as a setting; the Deck's budget
   measured.
4. **With the simulation's values:**
   - a driven diesel (MSTS's Dash 9 or GP38);
   - EUROPE1 `lclsrvce`'s heard steam train 3
     (`tests/renderer/sound-heard-trains.json`, with `runFor`).

## Step 1 (A) done (2026-10-30)

**The code:**
- **Emitters** (`VehicleDefinition::emitters`, `exhaustKinds`): read as
  Open Rails reads them.
  - `Wagon ( Effects ( SpecialEffects ) )` on any vehicle.
  - `Engine ( Effects ( DieselSpecialEffects ) )` on a diesel only, and
    `SteamSpecialEffects` on a steam engine only.
  - Every block's emitters are added in file order. Each has its name as
    written, its kind, its place and its unit direction (up where none is
    given), MSTS's axes as written, and its diameter.
- **The hand-over** (`src/tsre/trains/CarExhaust.h`):
  - `ExhaustOutput` holds flow (m³/s), speed (m/s), temperature (°C),
    steam and soot (0 to 1), beats (a second) and color (ARGB, 0 none);
  - `CarExhaust` has one output per kind, in `exhaustKinds`' order;
  - `exhaustKind(name)`: the name in lower case without "fx"; Exhaust1,
    Exhaust2... become `exhaust`;
  - `testExhaust(kinds)`: the test state.
- **The values:** `fx.<kind>.<field>` (`exhaustValueName`).
  `Sim::carExhaust(values, kinds)`, beside `carSoundInput`, reads them.
  Missing values are 0 (off); shares are clamped.
- **Scripts:** `tsre/exhaust.lua`, `publish(o, kind, out)`.
- **The main thread:**
  - `placeTrains` gives each car with emitters its exhaust
    (`Eng::setExhaust`): a driven train's every frame, a heard train's when
    its version changes, none otherwise.
  - The renderer reads `Eng::exhaust()`: the model's, else the test state
    with `TSRE_EXHAUST_TEST=1` (static consists too), else null.
- **Docs:** [simulation-messages.md](../../features/simulation-messages.md)
  lists the names.
- **Also:** `updateSounds`' `near` is now `inEarshot` (Windows' headers
  define `near`).

**Checks:**
- `vehicle-file` 40 (2 new):
  - a diesel's hose and two exhausts from an include, with its steam block
    left out; an electric's diesel block left out; directions made unit
    vectors;
  - the kinds and the values' names.
- `driving` 60 (1 new): a car's exhaust from its values, and the test
  state.
- **`vehicle-file-corpus` census:**
  - MSTS: diesel 8 of 8, steam 5 of 5;
  - OR_PSK: diesel 419 of 435, carriage 110 of 447; kinds exhaust 419,
    generator 54, heatinghose 164;
  - TS_STARTER_ROUTE: diesel 13 of 14.

  That is the content count above.
- `ctest` 11/11; `sound` 95, `steam` 9, `script` 32, `rmlui` 37; the
  simulation suites (core 89, host 19, ui 80, coupling 24, events 44,
  work 25).

**Found, for step 4:** shadows exist only while sound is on
(`updateSounds` sends no heard trains without it). For exhaust, the set of
trains with a shadow must not depend on sound: trains within 1,500 m of
the camera, sound or not.

## Step 2 (diesel) done (2026-10-30)

**The model** (`tsre/dieselengine`, `M.exhaust`; `diesel.lua` publishes it
as the `exhaust` kind), each engine burning fuel:
- **Fuel:** its power and friction (4 % of its power, with the RPM) at 40 %
  efficiency, 42.8 MJ/kg.
- **Air:** 25 kg a kg of the full-power fuel at its maximum RPM and full
  load, in proportion to the RPM, boosted from 1 at no load to 2.5 at full
  load. This is sized from the engine's power, not from the vehicle
  file's consumption: MSTS's Dash 9 gives 60 gal/h at 3,263 kW, a third of
  a real one's.
- **At the stack:** 150 °C at no load to 500 °C at full; the volume from
  the mass and the temperature; the speed 40 m/s at the engine's full flow,
  in proportion below it.
- **Steam share:** the water burning makes, 1.26 kg a kg of fuel, of the
  exhaust's mass.
- **Soot:** 0.1 to 0.25 with the load. With the RPM rising faster than a
  quarter of its change rate (Open Rails' transient), 0.55 to 0.9. Falling
  as fast, 0.05.
- **Colour:** Open Rails' `ExhaustColor`, `ExhaustTransientColor` and
  `ExhaustDecelColor` of the engine giving the most, where the vehicle
  file has them.
- **Several engines:** their flows add up; temperature, steam and soot are
  their mean. Open Rails shows the first engine only.
- **None** while an engine burns no fuel (stopped, or below the starter's
  RPM), as Open Rails.
- Open Rails' `DieselSmokeEffect…` keys are particle counts and sizes, so
  they are not used.

**MSTS's Dash 9 at 20 m/s** (the driving suite):

| | flow | speed | temperature | steam | soot |
| --- | --- | --- | --- | --- | --- |
| idle | 0.83 m³/s | 3.0 m/s | 150 °C | 0.005 | 0.10 |
| rising | 5.40 m³/s | 19.1 m/s | 500 °C | 0.092 | 0.90 |
| full | 11.29 m³/s | 40.0 m/s | 500 °C | 0.048 | 0.25 |
| falling | 2.12 m³/s | 7.5 m/s | 150 °C | 0.005 | 0.05 |
| stopped | 0 | | | | |

**Cost:** `diesel.lua` takes 19.4-19.8 µs a step, against 16.7 µs without
the exhaust (Release). About 2 µs of that is the host copying 7 more values
out of Lua every 0.01 s step. Copying the values only for the frames would
save that for every value of every scripted loco: a later improvement.

## Step 3 (steam) done (2026-10-30)

**The model** is in the steam core (`Step::exhaust`, C++). It is physical,
not Open Rails' (whose `UpdateFX` sets particles). The module writes the
values for the kinds of emitters the loco has.
- **The stack:**
  - It carries the cylinders' and the blower's steam, leaving the blast
    pipe at 110 °C, with the fire's gases: the coal and 14 kg of air a kg,
    from the smokebox at 250 °C, 400 °C at the grate's full burn. They are
    mixed by their heat.
  - Its speed: 80 m/s at the boiler's full output and the grate's full
    burn, in proportion below.
  - Its soot: 0.15, darker with the fire heavier than its ideal (up to
    +0.45) and with fresh coal on it, fed faster than it burns (up to +0.3).
  - Its puffs: each cylinder exhausts twice a turn of the driving wheels,
    while they take steam (2 × MSTS's `NumCylinders` a turn).
- **The cylinder cocks** (open, as the cylinders give them steam) and **the
  safety valves** (lifted) blow steam at 100 °C, at the speed of sound in
  steam (a choked jet, 450 m/s).
- **The whistle** (`steam.lua`): 0.1 kg/s while the horn sounds, a jet at
  100 °C.
- **The drain pipe:** none, as Open Rails ("not used in MSTS").
- **Only the kinds a vehicle has:** each value costs a copy every step.
  Scripts see the kinds as `vehicle.exhaustKinds` (`exhaust.has(vehicle,
  kind)`). The diesel publishes only with an exhaust emitter.

**The Royal Scot with the Scotsman's emitters** (the steam suite):

| | flow | speed | temperature | steam | soot | beats |
| --- | --- | --- | --- | --- | --- | --- |
| standing, stack | 0.03 m³/s | 0.1 m/s | 250 °C | 0 | 0.15 | 0 |
| 5 m/s, regulator open, stack | 3.44 m³/s | 6.6 m/s | 114 °C | 0.95 | 0.15 | 4.64 |
| cocks | 0.68 m³/s | 450 m/s | 100 °C | 1 | 0 | |
| whistle | 0.17 m³/s | 450 m/s | 100 °C | 1 | 0 | |
| safety valves lifted (two) | 3.60 m³/s | 450 m/s | 100 °C | 1 | 0 | |

**Cost** (Release): `steam.lua` takes 19.5-20 µs a step with the
Scotsman's emitters (25 more values), against 15.2 without. The diesel
takes 19.6-20 µs with an exhaust emitter. Most of it is copying values (see
step 2).

## Steam heating (the user's question, 2026-10-30)

**Open Rails' steam heating** is about 550 lines, roughly a sixth of the
diesel task's 3,700 (engine and loco):
- **the train** (`Train.UpdateCarSteamHeat`, about 240 lines): from the
  loco back along the train, steam flows through the main pipe and hoses.
  Pipes and hoses lose heat to the outside air. Each heated car takes
  steam until its compartment reaches its set temperature. The steam used
  is summed and goes back to the loco;
- **a car's heat loss** (`TrainCar.UpdateHeatLoss`, about 100 lines): its
  body, windows and ventilation, against the outside temperature and the
  train's speed;
- **the source:**
  - a steam loco takes the steam from its boiler (about 30 lines);
  - a diesel or electric loco has an oil-fired heating boiler with a water
    tank (about 40 lines each);
  - or a heating boiler car;
- **the cab:** a steam heat lever (`ORTSSteamHeat`, 0 to
  `MaxSteamHeatingPressure`), and the vehicle file keys;
- **its effects:** hoses, steam traps and the heating boiler (`GeneratorFX`
  on the 54 diesels).

**In TSRE** it would need:
- the weather's air temperature reaching the vehicles (it exists, but is
  not wired to them yet);
- a steam heating line along the train, beside the brake pipe and the
  main reservoir pipe;
- a heated carriage's compartment temperature in its vehicle model;
- the source in `steam.lua` (boiler steam) and in `diesel.lua` (the
  heating boiler);
- a lever command and values.

**Suggested:** its own task 06, right after A and B and before the merge.
Its emitters (hoses, traps, the heating boiler) are part of A's interface
from the start, so the render team draws them too. Unlike Open Rails,
which heats only the player's train, shadows would heat as well.

## Not in this task

- Electric effects (pantograph sparks).
- Wagon effects other than steam heating (hot boxes).
- Static consists: a static loco is off.
- AI trains beyond their shadows.
- Exhaust sounds: they belong to the sound tasks.
