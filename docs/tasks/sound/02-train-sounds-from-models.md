# Task Sound 02 - Train sounds from the vehicle models

Status: steps 1-4 done 2026-10-29 on `feature/driving` (it needs the
driving harness and the loco scripts there), not merged.

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
