# Task Sound 02 - Train sounds from the vehicle models

Status: steps 1-4 done 2026-10-29 on `feature/driving` (it needs the
driving harness and the loco scripts there), not merged. AI trains: shadow
scripts within earshot decided and built 2026-10-29 (below), not merged.

The user's (2026-10-29): "I would focus now on the sound system", then,
after the gaps below: "Current AI train sound isn't great either. But lets
proceed with 1-4 and maybe ideas and fixes for ai trains will come on the
way."

## What was missing

Sound task 01 left a driven train's engine and brake sounds to "a train
simulation". The driving harness (task driving 02) then sent its models'
sound triggers to the cars' SMS files, but only that:

1. **Variable1-3 were stand-ins:** `Eng::updateSounds` made them for every
   engine from the cab's throttle by fixed formulas for each engine type.
   The models' own values never reached the sounds; `electric.lua` and
   `diesel.lua` did not publish Open Rails' sound variables.
2. **The brake cylinder:** every car got the cab's brake cylinder share,
   though the host has each vehicle's own.
3. **Controls nobody fed:** engine RPM, power and torque, tractive effort
   and power, steam back pressure.
4. **The cab `.sms`** went to the consist's first car, not the driven cab,
   and the listener rode the first car whichever car the camera was in.
5. **The environment** (task 01's step 6): not started, still open.

## Steps

1. **The scripts publish Open Rails' sound variables** for each loco type
   (the specs: electric §11, diesel §14, steam §11).
2. **The host gives each driven car its model's values;** trains nobody
   drives keep the stand-ins.
3. **The cab `.sms` plays on the driven cab,** and the listener rides the
   camera's car.
4. **Checks:** the suites, then a driven train heard end to end.
5. **Later:** the environment (task 01's step 6), and AI trains (below).

## Done (2026-10-29)

### The scripts (step 1)

- **Electric** (`electric.lua`, Open Rails' ELOCO:203-220):
  - Variable1: the throttle in %;
  - Variable2: the pull in % of MaxForce. It follows the pull by at most 2
    a frame in Open Rails, 120 a second at its 60 frames; it is 0 with the
    throttle shut.
- **Diesel** (`diesel.lua`, `tsre/dieselengine.lua`, DLOCO:796-837):
  - Variable1: the throttle, 0 to 1;
  - Variable2: the first engine's RPM share between idle and its maximum,
    followed at the legacy keys' rate (DieselEngineMaxRPMChangeRate over
    the RPM range), else 0.2 a second;
  - each engine's `engineRpm`, `enginePower`, `engineTorque`
    (`engineRpm2`... for the others). The torque is 0 at 0 rpm, where Open
    Rails divides by zero.
- **Both:** Variable3, the dynamic brake's force as a share of its most
  (its lever without one), in `tsre/traction.lua`.
- **Steam:** `tsre.steam` already gives Variable1 to 3 (driving task 04)
  and its back pressure.

### The host and the cars (steps 2, 3)

- **The host** (`SimulationDriving.cpp`) adds each engine's
  `tractiveEffort` and `tractivePower`, as Open Rails' axles give them: the
  force pulling the way it runs (negative braking dynamically), and that
  force times the speed.
- **`Sim::carSoundInput(values)`** (`VehicleModel.h`) turns a vehicle's
  published values into a `CarSoundInput` (`src/tsre/sound/CarSoundInput.h`):
  - pressures in psi;
  - Variable1 for each engine (`variable1`, `variable1_2`...);
  - the engines' RPM, power and torque;
  - what a model does not publish is 0.
- **`SoundValues::setFrom(input)`** sets the controls an SMS file reads.
- **`SimulationController`** gives each car of the driven train its input
  every frame (`Eng::setSoundInput`). A train nobody drives keeps the
  stand-in's values; a car that stops being driven starts from fresh
  stand-in values.
- **The cab:**
  - `Consist::setSoundCab` names the driven cab, and `Eng::updateSounds`
    moves the cab `.sms` and the wheels' inside sound to it;
  - the listener rides the camera's car (`Consist::cameraVehicle`), not
    the first car.
- **`TSRE_SOUND_DEVICE=none`:** the sounds run without a device, for
  checks on a machine others listen to. `TSRE_SOUND_LOG`'s line for the
  ridden car adds Variable3, the engine RPM and the tractive effort.

### Checks (step 4)

- **`driving` (52):**
  - electric: Variable1 100 and Variable2 the pull at full throttle; the
    host's tractive effort and power;
  - a wagon's sounds hear its own brake cylinder, 50 psi in full service;
  - diesel: Variable1; Variable2 0.31 after 5 s at full throttle, its RPM
    share, under the 0.0684 a second the legacy keys allow; 1 after 30 s;
    the torque its power over its speed.
- **`sound` (95):** the values by name to the controls, by engine, in psi;
  a wagon with only its brake.
- **End to end** (the drive captures with sound on, no device,
  `TSRE_SOUND_LOG`), riding the driven loco:
  - USA2's Dash 9 (`driving-diesel.json`) at half throttle: Variable1
    0.50, Variable2 0.50, 608 rpm, 92.9 kN of tractive effort; with the
    engine stopped, 0 rpm;
  - EUROPE2's 380 (the new `driving-steam.json`) at 50 % cutoff and 60 %
    regulator: Variable1 0.79, Variable2 55 (cylinder pressure %),
    Variable3 4.3 (firing %), 165 kN.
- **Not run:** a driven cab other than the first car. Every capture drives
  from the first car, so moving the cab's sounds is checked by reading the
  code only.

## AI trains: ideas so far

The user finds the AI trains' sound "isn't great". What they get now (task
01's stand-in, `SimulationController::SoundControls`):
- the throttle and brake guessed from the speed's rises and falls;
- Variable1-3 from fixed formulas for each engine type;
- no engine RPM, power or tractive effort;
- the brake as a share of 50 psi on every car.

Ideas, not decided:
1. **Run the AI trains' loco scripts** at their 0.1 s step, fed the
   throttle and brakes their plans imply. Each would then sound as a
   driven one does (RPM, power, cocks, injectors, compressor). It costs
   about 15 µs a loco a step: 150 µs a second for each AI loco.
2. **Or only better stand-ins for each type,** from the ENG:
   - a diesel's Variable2 as its RPM share, following at Open Rails'
     rate;
   - EngineRPM from idle and maximum;
   - an electric's Variable2 from the force share;
   - a steam engine's Variable1 0 with the regulator shut, and its cocks
     opening at a start.
3. **Open Rails' events for AI trains:** its AI fires the horn and some
   other triggers, which a spec would list first.

### Decided (the user's, 2026-10-29): shadow scripts within earshot

The user, after the questions on how idea 1 works and its cost: "this
shadow script idea is great. Even for all trains 22 ms / s is nothing
because we run this outside render thread. But earshot improves it
further, no point to generate sounds messages impossible to hear."

**The AI does not drive levers.** Each step it plans its speed (Open Rails'
AI states, bands and braking curves), its acceleration limited by the ENG's
force and power, resistance and grade. It is one mass: no brake pipe,
couplers or adhesion. Open Rails' AI moves real levers, but where its
physics gives less than half its target it raises the speed anyway, and its
pre-run has no physics. Driving the AI by forces is not part of this.

**The design (not built):**
- **The AI keeps moving the train as now.** Its scripts' forces are thrown
  away, so the AI's results, the timetables and run-to are unchanged.
- **Levers from the AI's plan:**
  - the force needed: mass × the planned acceleration + resistance + grade;
  - pulling: the throttle is that over the full force at the speed (the
    figure the AI uses), smoothed;
  - braking: the dynamic brake first where fitted, the train brake for the
    rest; standing, braked;
  - steam: the regulator from the same share, the reverser in full gear at
    a start and notched up with speed, the cylinder cocks open for the
    first metres; the script's AI fireman does the rest;
  - the direction the way it runs.
- **Their values go to the sounds** through `Sim::carSoundInput`, as a
  driven train's do (steps 2, 3), with the pantographs and lights.
- **Only within earshot:**
  - the scripts start when a train comes within the sounds' 1,500 m
    (`HeardWithin`), warm (steam's hot start, a diesel at its notch, an
    electric powered), and go beyond 2,000 m;
  - none while running to a time or in the pre-run;
  - stepped at 0.1 s, even while a driven train steps the run at 0.01 s.
- **The host needs the listener's place;** the frame carries values for
  every train with models, not only the driven one.
- **The AI's horn:** the core records its horn waiting points
  (`TrainEvent::Horn`) but nothing plays them; they would sound the horn.

**Cost** (Release, Linux; Windows about 5 µs more): a loco step 11.7 µs
(electric) to 15.1 µs (diesel, steam). About 150 locos for 100 trains,
all scripted at 0.1 s: 22 ms a simulated second, about 2 % of a core in
the simulation's thread. At 0.01 s ten times that. During a run to a time,
an hour would take about 81 s instead of about 0.1 s, so no shadows then.
Within earshot: usually 2 to 6 locos, under 1 ms a second; a busy station's
25 locos about 4 ms. Not measured: the wagons, the plumbing. Open: whether
steam's exhaust beats come out bunched at 0.1 s.

The message flow this builds on: [simulation messages](../../features/simulation-messages.md).

### Agreed before building (the user's, 2026-10-29: "Fine, go ahead")

1. **A shadow is the driven train's per-vehicle setup:** loco scripts, the
   C++ wagons, the air brakes and the pipes. The AI's brake demand works
   the brake lever, so each car's cylinder comes from the air brake. Only
   motion stays the AI's: no couplers, no physics.
2. **Sounds only:** AI trains' pantographs and lights stay as now.
3. **A Lua state of its own** for the shadows. Which trains get one depends
   on the camera, so a driven run must not share their random numbers.
4. **The main thread names the trains heard** (`heard` in `updateSounds`,
   1,500 m on, 2,000 m off) and sends the set when it changes. With sound
   off, none.
5. **When:** while the run runs, at any speed; not while running to a
   time or in the pre-run; stepped at 0.1 s. Steam's exhaust beats
   bunched at 0.1 s are to be reported, not fixed by a finer step unasked.
6. **Messages:** a shadow's sound triggers go to its cars; nothing to the
   panel; a script's error logged once.
7. **The horn at the AI's horn waiting points:** 60010 + N for N seconds
   (Open Rails' duration); 60021, the crossing horn, 3 s (the middle of
   Open Rails' single blast, 2 to 5 s at random).
8. **Frames:** each heard train's values, all of them, with a version; the
   main thread converts a train's values to sound inputs only when its
   version changed, if that stays clean.
9. **Checks:** a suite without route content (levers from a planned run,
   start and stop by earshot, a driven run unchanged by shadows), then a
   capture near AI trains with the sound log, silent.

**Plan:**
- **The core** (`SimulationDriving.cpp`):
  - the driven train's vehicles' step factored out (models, control line,
    air brakes, pipes, the host's values), the same for a shadow;
  - `hear(trains)`: shadows made for AI trains on the track, dropped for
    the others; made again when a train's vehicles change; none for the
    driven train;
  - each shadow every 0.1 s: its cars placed along the AI train at its
    speed; its levers from the AI's acceleration over the step; its
    vehicles stepped; its sound triggers kept; its version raised;
  - reversing: its cars, pipes and direction turned, as a driven train's;
  - its own `LocoScripts` (a Lua state seeded apart).
- **Levers** (sent only when they change, as a driver moves them):
  - force needed F = m × a + resistance + grade (the AI's Performance);
  - F > 0: throttle (or regulator) F over the full force at the speed, in
    steps of 0.05, moved at most once a second;
  - F < 0: the share b of the AI's deceleration; with a dynamic brake it
    takes the first half (dynamicBrake 2b), the train brake the rest;
    without one, trainBrake b; standing, the train brake held;
  - steam: the reverser 75 % from a stand, notched up with speed to 25 %;
    the cylinder cocks open for the first 30 m from a stand.
- **The host:** `hear(trains, seq)`, kept and given to the core while
  running (not while running to a time); the frame's `heard` list; every
  message queued with its train.
- **The controller:** the heard set sent when it changes (and emptied with
  sound off); a heard train's cars get their sound inputs from its values
  (converted when its version changes), its cab's throttle and brake
  cylinder as the driven train's; its sound triggers played.

### Heard trains: done (2026-10-29)

As planned, with these differences:
- **The train brake is a brake demand, not the lever.** MSTS's train brake
  notches differ too much for a lever value to be a share of braking: the
  Dash 9, GP38 and Genesis have Release at 0, Continuous Service at 0.9 and
  Emergency at 0.95, so anything under 0.9 releases; the 310 and 380 lap
  from 0.4; the 30000 and KIHA apply graduated EP from 0.35. So
  `tsre/controls` takes a new command, `brakeDemand` (0 to 1 of full
  service, or off), and `tsre/brakes` holds the equalising reservoir there
  as a self-lapping valve would, whatever the lever's notches. The pipe,
  the triple valves and the cylinders stay the air brake's. A driven loco
  never gets it; `brakeDemand` is published only while set.
- **The dynamic brake** is used where the cab's ENG has a `Brake_Dynamic`
  controller with a step (as the scripts read one), and only above 5 m/s,
  so a standing diesel does not go into dynamic braking.
- **The capture harness** got `runFor` (and `runSpeed`): a view plays the
  run at a pace for so many simulated seconds, as heard trains' shadows
  step only then. `tests/renderer/sound-heard-trains.json` uses it.

**Code:**
- `SimulationDriving.cpp`:
  - the driven train's setup and its vehicles' step factored out
    (`setUpVehicles`, `stepVehicles`), shared by both;
  - `Shadow`: a `Driven` plus the driver's state;
  - `hear`, `stepShadows`, `stepShadow` (the driver), `shadowHorn`;
  - `outputsOf`, `cabOf` and `takeMessages` cover heard trains too.
- `SimulationCore`: `fullForce` (the AI's full force at a speed, now also
  used by `fullAcceleration`); the horn hook; `TrainMessage`.
- `SimulationHost`: `hear(trains, seq)`, applied on every mode change (none
  while running to a time); the frame's `heard` (`HeardFrame`: train, cab,
  version, values); every message queued with its train.
- `SimulationController`: `sendHeard` from `updateSounds` (none with sound
  off, cleared on a new run); a heard train's cars get sound inputs when
  its version changes; every train's sound triggers played.
- `appdata/0.7/scripts/loco/tsre/controls.lua`, `brakes.lua`:
  `brakeDemand`.

**Checks:**
- **`driving` (59, 6 new):**
  - an AI electric with 10 wagons run twice, heard the second time: the
    same motion exactly; its shadow stepped each 0.1 s; the throttle up
    starting; the brake demand up to full service stopping at its path's
    end, a wagon's cylinder 21.9 psi;
  - heard, not heard, heard again: made, gone, made anew;
  - a horn waiting point 60013: the horn on, off 3.0 s later;
  - a heard diesel with a dynamic brake: 900 rpm starting away (idle 315);
    its dynamic brake at 0.5 braking;
  - a driven train beside a heard one: its run the same as without; the
    frames give the heard train's values, 100 new versions in 10 s of
    0.01 s steps;
  - the cost (Release): an electric and 40 wagons, 36 to 40 µs a shadow
    step, about 0.4 ms a simulated second while heard.
- **End to end** (`sound-heard-trains.json`, EUROPE1, riding AI train 3, a
  steam loco; sound on, no device, `TSRE_SOUND_LOG`):
  - braking into Long Marton from 15 m/s: the brake cylinder 9 to 15 psi;
  - standing: held at 10 to 12 psi;
  - starting away: the brakes off; Variable2 (cylinder pressure) 44 to 76;
    tractive effort 129 to 146 kN; Variable1 rising with the speed.

  Before this, run to a time, the same train gave the stand-in's values
  (Variable1 19.4 from its formula, no tractive effort).
- `ctest` 11/11; `steam` 9, `script` 32, `rmlui` 37, `sound` 95,
  `vehicle-file` 38, and the simulation suites (core 89, host 19, ui 80,
  coupling 24, events 44, work 25) pass.

**Found, not changed (as agreed):** a steam shadow's exhaust pulses. The
steam core sends at most one a step, as Open Rails does a frame; at 0.1 s
that is at most 10 a second, and the core wants more above about 12 m/s
(a 3-cylinder loco there: 11.5). Only Open Rails' pulse triggers (121 on)
hear them, and none of the 1,752 local `.sms` files (MSTS, OR_PSK) uses
them; MSTS steam sounds follow Variable1 and 2.

**Left:**
- AI trains' pantographs and lights from their shadows (decided against
  for now: sounds only).
- The environment sounds (task 01's step 6).
