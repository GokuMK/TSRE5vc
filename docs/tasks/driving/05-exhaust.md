# Task 05 - Exhaust: smoke and steam

Status: designed 2026-10-30, not started.

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

**ENG files: 65 of 1,290 have emitters.**
- **60 diesels** have `Effects ( DieselSpecialEffects ( Exhaust1 … ) )`,
  and two of them also have `Exhaust2`. They include:
  - OR_PSK's CD 749, 751 and 720, and the PKP railcars;
  - TS_STARTER_ROUTE's BNSF and Amtrak;
  - MSTS's Dash 9, GP38, Class 50, KIHA 31 and KIHA 140.

  The other diesels (about 400 of 457) have none. As in Open Rails, they
  show no exhaust.
- **MSTS's five steam locos** have `Effects ( SteamSpecialEffects ( … ) )`.
  Each has `StackFX`, two `CylindersFX`, `SafetyValvesFX`, `WhistleFX` and
  `DrainpipeFX`.
- **No electric engine** has emitters.

**WAG files:** 110 have `Effects ( SpecialEffects ( HeatingHoseFX … ) )`,
178 emitters in all. This is steam heating, which TSRE does not simulate.

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

## Who does what (suggested)

| Part | Who | Where |
| --- | --- | --- |
| A. The interface: emitters and their values | both; written here first | a small commit both build on |
| B. The values: loco models, shadows, frames, the world's consists | here (driving) | `feature/driving` |
| C. Drawing: volumetric smoke and steam | the render team | their branch, QRhi |

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
- **Both sides can start at once** once part A is agreed. They meet only
  there.

## The interface (proposal, to agree first)

**Emitters (static, read once).** Each vehicle definition lists its
emitters: kind, place, direction and nozzle diameter.
- Kinds are named as the files name them: `exhaust1`, `stack`,
  `cylinders`, `safetyValves`, `whistle`, …
- Several of a kind keep their order, for example a loco's two
  `CylindersFX`.

**Values (per frame, per vehicle, per kind; SI):**
- `flow`: gas or steam leaving the nozzle (m³/s); 0 means off.
- `speed`: its speed at the nozzle (m/s).
- `temperature` (°C): how fast it rises. For steam, also how long it
  shows, given the weather's temperature and dew point.
- `steam` (0-1): the share that is water vapour; the rest is smoke.
- `soot` (0-1): how dark the smoke is.
- **The stack's beats:** beats a second, and their phase (the driving
  axle's angle).
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

   Checks: the 65 ENGs and 110 WAGs are read, with places and directions
   as in the files.
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

## Open questions (for the user)

1. **Physical values** (flow, speed, temperature, steam share, soot)
   instead of Open Rails' particle counts? Suggested: physical. Open
   Rails' steam values are already volumes and speeds, and its diesel
   counts map onto flow through the ENG's rates.
2. **The interface on main first?** The render team builds on main, but
   `feature/driving` is not merged. Step 1 could go into main on its own
   when you say.
3. **The OpenGL renderer:** volumetrics only on QRhi. On OpenGL, nothing,
   or simple particles?
4. **Steam heating hoses** (110 WAGs) and the other wagon effects: later,
   with steam heating?

## Not in this task

- Electric effects (pantograph sparks).
- Wagon effects: hot boxes, steam heating.
- Static consists: a static loco is off.
- AI trains beyond their shadows.
- Exhaust sounds: they belong to the sound tasks.
