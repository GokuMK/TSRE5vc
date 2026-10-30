# Simulation Messages

How the simulation, the vehicle models, the UI, the sounds and the cabs talk
to each other.

Source review: 2026-10-29, branch `feature/driving` (heard trains included). This
describes what is built. Each part's design and history are in the task
docs:
- [simulation 01](../tasks/simulation/01-simulation-thread-and-train-motion.md):
  the thread, frames, commands;
- [driving 02](../tasks/driving/02-driving-harness.md): the driven train,
  messages, the control panel;
- [scripting 01](../tasks/scripting/01-lua-and-rmlui-foundations.md): Lua and
  RmlUi;
- [sound 02](../tasks/sound/02-train-sounds-from-models.md): train sounds
  from the vehicle models.

## The parts

| Thread | Part | Code |
| --- | --- | --- |
| Main | The controller: starts runs, sends commands, takes frames and events | `src/tsre/sim/SimulationController` |
| Main | The views: draw the trains, play their sounds, show the UI | `src/routeEditor/RouteEditorGLWidget` |
| Main | The UI: RmlUi documents and the UI's Lua (the loco panel, the HUD) | `src/tsre/ui/` |
| Main | The trains' copies and their sounds | `src/tsre/trains/Consist`, `Eng`; `src/tsre/sound/` |
| Simulation | The host: owns the core, paces it, publishes frames | `src/tsre/sim/SimulationHost` |
| Simulation | The core: every train, signals, events | `src/tsre/sim/SimulationCore` |
| Simulation | The driven train: its vehicle models, buses, couplers; heard AI trains' shadows | `src/tsre/sim/SimulationDriving.cpp`, `VehicleModel`, `AirBrake`, `TrainPhysics` |
| Simulation | Loco scripts: Lua, and the native module `tsre.steam` | `src/tsre/sim/LocoScript`, `src/tsre/sim/steam/`, `appdata/0.7/scripts/loco/` |

Three Lua states never share anything:
- **the simulation's**, the driven train's scripts: no wall clock, random
  numbers from the run's seed, so a run repeats;
- **the heard trains' shadows'**, the same kind, seeded apart: which trains
  are heard depends on the camera, so they must not touch the driven run;
- **the UI's**: shared with the RmlUi documents.

They talk only through the messages below.

## The flow

```
MAIN THREAD                                          SIMULATION THREAD
                                                     (SimulationHost owns SimulationCore)

loco.rml + loco.lua
  train.command("throttle", 0.5)
    -> Ui::LocoControls
    -> SimulationController::command(name, value, vehicle = -1)
         numbered, a Qt queued call ------------->   SimulationHost::command
                                                       -> SimulationCore::command
                                                       -> the vehicle's VehicleModel::command
                                                       -> its script's command(name, value, state)

                                                     every step (SimulationCore::step):
                                                       trains due placed
                                                       each AI train: advance (its speed planned)
                                                       the driven train: advanceDriven
                                                         each vehicle: inputs -> model.step -> outputs
                                                         the control line between its units
                                                         air brakes, brake pipe, main reservoir pipe
                                                         couplers and motion (TrainPhysics)
                                                       each heard AI train, every 0.1 s: its shadow
                                                         levers from the AI's motion
                                                         its vehicles as the driven train's (no motion)
                                                       signals' aspects, activity events

  updateSounds: the trains heard
    -> SimulationController::sendHeard ---------->   SimulationHost::hear -> SimulationCore::hear

                                                     publish (each tick, after each command):
  frame() / viewFrame()  <------------------------     SimulationFrame: immutable, the newest
                                                         replacing the last (DrivenFrame: the
                                                         driven train's values by vehicle;
                                                         HeardFrame: each heard train's)
  poll() every 100 ms    <------------------------     TrainEvent queue (at most 100,000)
  takeDrivenMessages()   <------------------------     TrainMessage queue (at most 10,000): the
                                                         driven train's messages, the heard
                                                         trains' sound triggers

each view tick: pinViewFrame()
each paint:
  messages "sound"  -> Consist::soundEvent -> the car's SMS trigger
  placeTrains       -> each car's pose, wheels, pantographs, doors, headlight,
                       sound input (carSoundInput -> Eng::setSoundInput)
  LocoControls::update(the cab's values, speed, messages)
                    -> train.values, train.texts; RmlUi events trainupdate, trainmessage
```

## Three channels

- **Commands go down.**
  - Each is a queued call to the host's thread, numbered by the controller.
  - The next frame's `acknowledged` gives the last number the host took
    (`SimulationController::waitFor` waits on it in the tests).
  - Run commands: start, play, pause, speed, run to, stop, reset. Driving
    commands: drive, leave, and messages to the driven train's vehicles.
    Sound: hear, the trains within earshot.
- **State comes up in frames.**
  - A `SimulationFrame` is immutable once made. The newest replaces the one
    before.
  - What has not changed (aspects, section uses, event states, standing
    trains) is shared with the frame before.
  - The host publishes:
    - at the end of every tick: 16 ms while running at a pace, or after a
      batch of at most 20 ms of stepping;
    - after every command, so a paused run still shows a command's effect.
  - **Every frame carries every value of every driven vehicle,** not only
    the changed ones. The user's (2026-10-29): "All values is the obvious
    and usually better solution." A reader that starts late or skips frames
    always has the whole state, with nothing to merge. The cost, measured
    in Release (an electric loco and 40 wagons, 161 values; the `driving`
    suite's "frames" line):
    - the simulation thread: 23 to 25 µs a frame (the shared copy, then the
      next step's own copy when it writes its values); about 1.4 ms a second
      at 60 frames, against the train's step at 37 µs, 3.7 ms a second at
      0.01 s;
    - the main thread: the 41 cars' sound inputs 25 to 29 µs a frame.
- **Events come up in order, through queues.**
  - **The core's `TrainEvent`s:** arrivals, departures, waits, horns,
    activity events, driving started or ended, collisions. At most 100,000
    are kept; the oldest are dropped and counted.
  - **The models' messages** (`TrainMessage`: train, vehicle, message):
    what the driven train's models `send()`, and the heard trains' sound
    triggers. At most 10,000 are kept; the oldest are dropped without a
    count.

**The simulation never waits for the main thread.** The main thread reads
whatever is newest. A slow paint skips frames, but never messages or
events, up to the queues' bounds.

## A message

`Sim::Message` (`src/tsre/sim/VehicleModel.h`): a name and a value.
- **Name:** free text.
- **Value:** true/false, a number or a text.
- **Text form:** `name = value`, the same in Lua, in files
  (`TSRE_SIM_DRIVE`'s command files), and later over websockets
  (`Message::toText`, `fromText`).
- **Reading a value:**
  - `number()`: true is 1, and text is read as a number;
  - `flag()`: non-zero, "on", "up", "close", "true" and "yes" count as set.

**TSRE keeps no list a message must be on.** A script can publish or send a
name TSRE knows nothing about (`BATTERY_CHARGE = 44`,
`CAB_AMBIENT_LIGHT = disable`):
- the panel lists every value by name;
- every sent message reaches the UI as a `trainmessage` event.

The names the engine reads itself:

| Name | Where | Read by |
| --- | --- | --- |
| `sound` (a number) | sent | the car's SMS sounds: Open Rails' trigger numbers |
| `scriptError` (text) | sent by the host | the UI: a script stopped (it gives no force after) |
| `pantograph1`, `pantograph2` | value | the car's pantograph animation |
| `headlight` (0, 1, 2) | value, the cab's | the train's headlights |
| `throttle`, `brakeCylinder` | value, the cab's | the consist's stand-in sound controls |
| `variable1`, `variable1_2`..., `variable2`, `variable2Booster`, `variable3`, `engineRpm`, `enginePower`, `engineTorque` (`engineRpm2`...), `backPressure`, `brakeCylinder`, `tractiveEffort`, `tractivePower` | value, each car's | the car's SMS sound controls (`Sim::carSoundInput`) |
| `fx.<kind>.flow`, `.speed`, `.temperature`, `.steam`, `.soot`, `.beats`, `.color` | value, each car's | the car's emitters of that kind (`Sim::carExhaust` to `Eng::setExhaust`, drawn by the renderer; driving task 05). Scripts publish them with `tsre/exhaust` |
| `epBrake` | control line | the host: the EP wire's demand |
| `brakeDemand` | a command, from the host | a heard train's train brake as a share of full service, whatever the lever's notches (`tsre/controls`, `tsre/brakes`) |

The standard control names (`throttle`, `trainBrake`, `engineBrake`,
`dynamicBrake`, `emergency`, `direction`, `pantograph1`/`2`,
`circuitBreaker`, `battery`, `masterKey`, `headlight`, `horn`) are listed
in driving 02's "Messages". Each loco script's header lists the commands it
takes:
- `electric.lua` also takes `<lever>Increase`/`Decrease` while held, and
  `power`;
- `diesel.lua` takes `engine` and `helperEngines`;
- `steam.lua` takes `regulator`, `reverser`, the injectors, `cylinderCocks`,
  `blowdown`, `manualFiring`, `shovel`, the fire's and `refill`.

## Commands: from a sender to a script

1. **The sender:**
   - the panel's Lua calls `train.command(name, value)`;
   - C++ calls `SimulationController::command(name, value, vehicle)`, which
     the renderer captures' `"commands"` also use;
   - `vehicle` -1 means the driven cab.
2. **The controller** numbers it and queues
   `SimulationHost::command(vehicle, message, seq)` on the host's thread.
3. **The host** passes it to `SimulationCore::command(driven train,
   vehicle, message)`.
   - It is taken between batches of steps, so it acts from the next step.
   - Before the driven train is on the track, it is kept and given when the
     train is taken over.
4. **The vehicle's model** gets `VehicleModel::command(message)`. A Lua loco
   calls its script's `command(name, value, state)`.
5. **The script's handler** runs.
   - A name the script has no handler for is ignored, with no log. The
     design's `TSRE_DRIVING_LOG` is not built.
   - A handler's return value goes on to the train's other units when the
     command is one of the script's `MULTIPLE_UNIT` names (below).

`TSRE_SIM_DRIVE` (the `simulation-route` suite) skips the host and calls
`SimulationCore::command` from a file of timed commands.

## In the simulation step

**A vehicle model** is the simulation object that steps one vehicle: a
`VehicleModel`, which is a Lua loco script, the C++ stand-in engine or the
C++ wagon, with the host's air brake beside it. It is not the 3D shape.

**AI trains move with no vehicle models.** Their vehicles' definitions
(mass, force, power, Davis) are read, and their consists are drawn. The
core plans each AI train's speed for the step (`advance`):
- Open Rails' AI rules: its states, bands and braking curves;
- its acceleration limited by the ENG's force and power, the resistance and
  the grade.

It is one mass, with no levers, brake pipe or couplers.

**A heard AI train** (within earshot, task sound 02) also runs a shadow:
its vehicles' models as the driven train's, every 0.1 s, its cars placed
along the AI train at its speed and their forces unused, so the AI moves it
as before. Its driver works from the AI's motion over the step:
- the force needed (mass × acceleration, resistance, grade) as a share of
  the full force at the speed: the throttle (a steam loco's regulator);
- braking: the share of the AI's deceleration; the dynamic brake for the
  first half where fitted, above 5 m/s; the train brake as `brakeDemand`;
  standing, held at half of full service;
- the levers in steps of 0.05, moved at most once a second;
- steam: the reverser 75 % from a stand, 25 % from 20 m/s; the cylinder
  cocks open for the first 30 m;
- the horn at its horn waiting points.

Its pantographs and lights stay the stand-ins'. An AI train not heard has
no shadow and publishes nothing.

**The driven train** (`advanceDriven`) runs one model per vehicle:
- a Lua loco for each electric, diesel and steam engine;
- the C++ stand-in for any other engine;
- a C++ wagon (`MakeVehicleModel`) for every other car.

Each step, for each vehicle:

1. **In** (`VehicleInputs`, in the vehicle's own frame: + toward its front
   as built):
   - dt, speed, gradient;
   - the adhesion limit, the line voltage;
   - the brake pipe's pressure and volume there, its brake cylinder, the
     main reservoir pipe;
   - whether it is the driven cab.
2. **The model's step.**
3. **Out** (`VehicleOutputs`):
   - motive force, brake force;
   - air into or out of the brake pipe and the main reservoir pipe;
   - the independent brake and bail-off;
   - `values`: values that hold, by name;
   - `events`: messages sent, in order;
   - `line`: messages for the train's other units.
4. **The host adds its own values:**
   - `tractiveEffort` and `tractivePower` for an engine;
   - after the air brakes: `brakePipe`, `mainPipe`, `brakeCylinder`,
     `auxiliaryReservoir`, `controlReservoir`.

Then, for the whole train:
- **The control line:** each unit's `line` messages go to every other
  unit's `VehicleModel::fromLine`. A unit facing the other way gets
  `direction` turned round. A script gets these as `command(name, value,
  state, "line")` and does not send them on.
- **The air brakes, the pipes, couplers and motion:** the host's own, from
  the outputs' forces and air.

## The loco script's side

A loco script (`appdata/0.7/scripts/loco/*.lua`; the full contract is in
`LocoScript.h`):

| Function | Called |
| --- | --- |
| `init(state, vehicle)` | once; `vehicle` reads its file's keys in SI |
| `update(dt, inputs, state, out)` | every step |
| `command(name, value, state[, "line"])` | every message for it |
| `save(state)` | to save (else the state table itself) |

- **`inputs`:** `speed`, `gradient`, `adhesionLimit`, `lineVoltage`,
  `brakePipe`, `brakePipeVolume`, `brakeCylinder`, `mainPipe`, `cab`, and
  `vehicle`, which is for native modules only.
- **`out`:** `motiveForce`, `brakeForce`, `brakePipeAir`, `mainPipeAir`,
  `independentBrake`, `bailOff`, and the table `out.values`.
- **While a script runs:**
  - `send(name, value)` sends an event: to the UI, and for `sound` to the
    car's sounds;
  - `line(name, value)` sends a message to the train's other units.

  The shared modules in `tsre/` use them too (`util.sound(n)` is
  `send("sound", n)`).
- **`command`'s `MULTIPLE_UNIT` names:** the scripts' own lists of names
  that the driven cab's handlers pass on, with `line()`, to the other units.
- **An error in `update` or `command`** stops that loco's script. The host
  sends `scriptError` once, and the loco gives no force after.

## On the main thread

- **The frames:**
  - `SimulationController::frame()` is the newest in the host's mailbox;
  - each view tick pins one with `pinViewFrame()`, and `viewFrame()` gives
    it, so the camera riding a train and the train drawn come from the same
    frame;
  - `poll()`, every 100 ms, takes the newest frame and the `TrainEvent`s
    into `events()` and emits `frameUpdated` and `eventsAdded`. The
    Timetable window, the event list and the map's message labels listen to
    those.
- **Each paint:**
  - `takeDrivenMessages()` takes the messages. Each `sound`, the driven
    train's or a heard train's, goes to `Consist::soundEvent(vehicle,
    trigger)`, which fires that SMS trigger on the car.
  - While a train is driven, `Ui::LocoControls::update` gets the cab's
    values, the speed and the driven train's messages (next section).
- **Placing the trains** (`placeTrains`, before the trains are drawn and
  their sounds updated), for each train on the track:
  - its cars' poses and wheels from the frame;
  - the doors at platforms;
  - **a driven train:**
    - pantographs and headlight from its values;
    - each car's sound input from its own values (`Sim::carSoundInput` to
      `Eng::setSoundInput`);
    - each car with emitters, its exhaust from its own values
      (`Sim::carExhaust` to `Eng::setExhaust`; the renderer reads
      `Eng::exhaust()`);
    - the cab's sounds on the driven cab (`Consist::setSoundCab`);
  - **a heard AI train:** each car's sound input and exhaust from its
    shadow's values, given only when the frame's version for it changed
    (each 0.1 s); the cab's sounds on its shadow's cab;
  - **an AI train not heard:**
    - pantograph 1 up on electric engines, as Open Rails does (a heard one
      too);
    - the stand-in's throttle and brake, guessed from its speed and
      acceleration (`SoundControls`);
    - each engine's Variable1-3 from fixed formulas for its type.
- **The sounds:**
  - the listener rides the car the camera is in
    (`Consist::cameraVehicle`);
  - a train's sounds are updated within 1,500 m of the listener and
    silenced beyond 2,000 m;
  - `updateSounds` sends the trains within that (`sendHeard`) when they
    change, none with sound off; the host gives them to the core while the
    run runs or is paused, none while running to a time, and the core makes
    none in the pre-run.
- **The HUD** (`Ui::Hud`) has a path of its own: speed, distance, throttle
  and brake from the ridden consist.

## The UI's side (RmlUi)

`Ui::LocoControls` loads `appdata/0.7/ui/loco.rml` while a train is driven.
It puts a global `train` table in the UI's Lua:

| Field | Is |
| --- | --- |
| `train.command(name, value)` | a message to the driven cab, any name |
| `train.leave()` | gives the train back to the AI |
| `train.values` | the cab's values, SI, made anew each frame (a value no longer published is gone) |
| `train.texts` | the same values as shown, in the user's units where TSRE knows the name's quantity |

Each paint, the document gets:
- **`trainupdate`**, once;
- **`trainmessage`** for each message since the last paint, with
  parameters `name`, `value` (as text) and `vehicle`.

`loco.lua` builds the panel from these. It lists every value it doesn't
know by name, so a script's own values show with no UI change.

**The cost a frame** (Release, the `rmlui` suite, 60 values, as many as a
steam loco publishes): about 185 µs.
- **Taking the values to the UI's Lua:** 37 to 40 µs. That covers the
  tables, the texts in the user's units and the events.
- **`loco.lua`'s update:** about 150 µs. Each frame it finds each element by
  id and reads its `inner_rml` back to compare.

In the steam drive capture, the UI's whole "event" time (`TSRE_UI_LOG`)
is 0.3 to 0.5 ms a frame.

Input: `Ui::Screen`'s router decides, event by event, whether the UI or the
view gets the mouse and keys.

## Not built yet

- **Cabs:**
  - the 3D cab is drawn and can be ridden, but its levers and gauges don't
    follow the values;
  - `.cvf` files (2D cabs) are not read.
- **Keyboard:** not mapped to commands.
- **External apps:** websockets, and the full-train link in the simulation
  thread, are designed only. The text form of a message is ready for them.
- **A vehicle's own script** named in its ENG.
- **Saving a run:** a model can save and restore its state
  (`VehicleModel::save`, `restore`), but runs aren't saved.
- **More than one driven train.** Commands, `DrivenFrame` and the panel
  are for the one driven train.
