# Task 02 - The driving harness: a first whole stack

Status: design, for the user's review (2026-10-28). No code yet.

The user's request (2026-10-28), after the ENG refactor (task 01) was
merged:
- the harness: train motion, the buses, the vehicle model interface;
- a Lua electric loco, based on Open Rails' electric engine (which is
  small);
- a simple RmlUi widget with buttons controlling the loco, instead of cab
  and keyboard integration (the next task).

The aim is the whole stack quickly, to see what works and what doesn't. The
UI controls and the scripts must both be able to control the loco: the
pantograph up and down, the lights on and off, and so on.

It builds on the roadmap's decisions ([00-task-roadmap.md](00-task-roadmap.md)),
the [harness review](reports/driving-harness-review.md) and the vehicle
definitions ([01-vehicle-definitions.md](01-vehicle-definitions.md)):
- **The host owns motion and couplers.** Vehicle models give forces and
  never move themselves. Couplers are springs and dampers with slack per
  pair, all solved implicitly together each step.
- **The host solves the buses** (brake pipe, main reservoir pipe, electric
  lines, commands) from what each vehicle puts in and takes out.
- **One fixed step a run, no separate physics tick:** 0.01 s when a train
  is driven by the player, a script or an external app; 0.1 s for AI-only
  runs.
- **The simulation never waits** for anything outside it.
- **Lua loco scripts replace only the loco's physics.** Each loco keeps a
  state table (`init`, `update(dt, train, state, out)`, `save`), and saving
  is restricted to numbers, strings, booleans and tables.
- **Our own command system,** later mapping Open Rails' and MaSzyna's cabs.
  The AI's simple driving stays the base of AI trains.

## TSRE today

- **The simulation core** (`src/tsre/sim/SimulationCore`) steps every
  train at a fixed `MaxStep` of 0.1 s:
  - placing trains that are due;
  - `advance`, the AI driver: route limits, signals, stops, the braking
    curve, `Simple` or `Physics` driving;
  - aspects;
  - events.

  A train is one point mass, its `TrainState` holding its front, speed,
  acceleration and how far it has travelled. Its vehicles' places are
  worked out from their lengths (`vehicleEnds`).
- **`TrainPlan::player`** marks the player's train, which today runs its
  path as the AI does.
- **Frames:** the host publishes an immutable `SimulationFrame` (each
  train's state, its vehicles' two ends, its vehicles) and takes commands
  (play, pause, speed, run to) with sequence numbers.
- **The editor** (`SimulationController`) places each simulated consist's
  cars from the frame:
  - it raises pantograph 1 on every electric engine, as Open Rails does
    for AI trains;
  - it opens doors at platforms;
  - it guesses throttle and brake for the sounds from the speed changes
    (`SoundControls`).

  Headlights come from `EngLights::Train` (`headlight` off, dim or bright),
  set by the consist.
- **Vehicle definitions** (task 01) give each vehicle's file data:
  - typed fields (mass, force, power, top speed, Davis coefficients,
    couplings);
  - the whole block tree;
  - the key catalogue, giving every key's units.

  The simulation thread reads them directly through `Sim::Vehicle`.
- **The track copy** (`TrackGeometry`) gives each place's position, heading
  (yaw), pitch and roll: gradient, curvature and cant are there to be read.
- **Lua** (`Script::State`, task scripting 01):
  - a simulation-thread kind, with no wall clock and random numbers
    seeded per run;
  - per-script environments, deadlines and memory limits;
  - values kept between calls (`keep`/`push`);
  - `save`/`restore` of plain values.

  TSRE exposes no functions of its own to Lua yet.
- **RmlUi** (`Ui::Screen`, `Ui::Hud`): the HUD document (`hud.rml`, data
  model `train`, event `trainupdate`, `hud.lua`) is shown for a ridden
  train and can be dragged. The UI's Lua shares globals with RmlUi's
  documents.

## The stack

```
widget (RmlUi + UI Lua) --command--> SimulationController --queued--> host thread
   ^                                                                   |
   |                                                          core, each 0.01 s step:
   |                                                          1. commands to the driven
   |                                                             train's vehicles
   |                                                          2. vehicle models: the loco's
   |                                                             Lua script, C++ wagons ->
   |                                                             forces, bus flows, states
   |                                                          3. buses solved
   |                                                          4. couplers and motion solved
   |                                                          5. signals, events (as now)
   |                                                                   |
   +--- data model <-- editor: cars placed, pantographs, lights, <--frame
                       sounds from the real controls
```

## Design (proposal)

### The driven train

- **One train of a run is driven,** the player's (`TrainPlan::player`). For
  an Open Rails timetable it's the train chosen in the activity panel,
  which Follow already uses.
- **The driven train moves by its vehicle models' forces.** It gets a third
  driving kind beside `Simple` and `Physics`. Every other train stays AI.
- **The panel's Simulation section gets a *Drive* choice beside Play.** It
  starts the run with the player's train driven, the camera riding it (as
  Follow does) and the control widget shown.
- **The core still reserves the track ahead** of the driven train, as for
  the player's AI train today, so signals clear for it and other trains
  react to it. It no longer stops the train at stations or signals: that is
  the driver's job.
  - Passing a stop signal at danger, or the end of its authority, is
    reported as an event (Open Rails' SPAD). The core doesn't stop the
    train for it in this task.
  - Station stops are the driver's: the task 06 events (arrival, departure)
    work as they do for the player.

### The step

- `MaxStep` becomes the run's step, set in the input: 0.01 s with a driven
  train, 0.1 s otherwise, as decided.
- AI trains step at 0.01 s too in such a run. Today's measures put a
  0.1 s step at 0.9 µs on EUROPE1 and 5 µs with PRUDNIK's 266 trains. Ten
  times as many steps is well within real time, but it is measured in the
  checks.

### Motion and couplers (the driven train)

- **Per vehicle:** a mass (the definition's, kg), a speed and a place along
  the train.
- **Forces per vehicle, each step:**
  - the model's motive force and brake force;
  - resistance (the definition's Davis coefficients, the leading one's air
    drag for the first vehicle);
  - gravity from the gradient at the vehicle (the track pitch at its
    middle).

  Curve resistance comes later, when per-vehicle curvature is read.
- **Couplers between neighbours:** a spring (stiffness), a damper and slack
  (free play), from the definition's `Coupling ( Spring ( Stiffness, Damping,
  r0 ) )` where given, else defaults. They are solved implicitly together
  with the speeds: one tridiagonal system along the train each step, as
  decided 2026-10-24. That keeps stiff couplers stable at 0.01 s, and Open
  Rails' rigid couplers are the stiff limit.
- **Brake force** acts against motion and never reverses it: a standing
  vehicle's brake holds it up to its brake force (static friction).
- **What the train's state keeps:**
  - its front is the first vehicle's place, so the reservations and the
    route work as now;
  - its speed is the leading vehicle's;
  - `vehicleEnds` uses each vehicle's own place, so the editor draws the
    slack.
- **Adhesion,** first version: the loco's motive force is limited to its
  weight × the rail's adhesion. The coefficient is a host input: dry 0.33
  for now, weather later. The script gets that limit and can ask for less.
  Wheel slip models come later.

### Buses (first version)

- **The brake pipe,** a simplified version of Open Rails' air single
  pipe.
  - Each vehicle has a pipe volume and a pressure.
  - The loco's brake valve is part of its model:
    - the train brake controller sets the equalising reservoir (in the
      simplest case, without notches, `max − fullService × position`);
    - the pipe at the loco follows the equalising reservoir: charging at
      the charging rate from the main reservoir, falling toward it when
      reduced, vented to 0 in an emergency.
  - The host spreads the pressure along the train each step: an implicit
    diffusion over the pipe sections, so no sub-steps are needed.
  - Each wagon's triple valve works on the pipe, its auxiliary reservoir and
    its brake cylinder:
    - it applies when the pipe drops below the auxiliary reservoir, at most
      `MaxApplicationRate`;
    - it releases above it, at `MaxReleaseRate`;
    - it recharges from the pipe.
  - **Brake force** = `MaxBrakeForce` × (cylinder − spring) / (reference −
    spring), the reference being `BrakeCylinderPressureForMaxBrakeBrakeForce`.
  - These keys are in all 107 MSTS vehicles and about 2,200 OR_PSK files.
    Open Rails' defaults fill in the rest.

  Open Rails' other valves and the engine brake come later.
- **The control line** (multiple-unit commands): the lead loco's commands
  (throttle, brake, direction, pantographs) reach the other units of the
  train as messages. In this task there is only one loco, but the line is
  there.
- **The line voltage:** the route's `MaxLineVoltage` (TRK), as Open Rails
  reads it. Locally it's 0 on EUROPE1, 1800 on JAPAN1, 16000 on PRUDNIK and
  missing on USA2.
  - In Open Rails it is only shown: it never limits force. Every track
    counts as electrified, which is what this task does too.
  - Current, voltage drop and unelectrified track come later.
- **The main reservoir pipe:** later.

### The vehicle model interface

A C++ interface, the same for every kind of model:

- **In each step:**
  - dt;
  - the vehicle's speed in its own frame (+ toward its front);
  - the gradient and cant at it;
  - its adhesion limit;
  - the line voltage;
  - its bus values (the brake pipe pressure at it, the control line's
    messages);
  - for the vehicle with the active cab, the controls;
  - the commands and events for it.
- **Out each step:**
  - motive force (N, own frame);
  - brake force (N, ≥ 0);
  - dynamic brake force;
  - power and current drawn;
  - bus flows (air into or out of the pipe);
  - its visual state (pantographs, headlight, doors), in the frame;
  - its cab values (named numbers for displays and the widget);
  - events (sound triggers, messages).
- **Kinds:**
  - **A wagon in C++** (brake cylinder from the pipe, resistance) for every
    car without a script.
  - **A Lua loco:** the roadmap's `init`/`update`/`save`, in the simulation
    thread's Lua state, one state table per loco.
  - Later MaSzyna's mover.
- **Sign convention, once:** each vehicle sees its own frame (+ toward its
  front as built). The host turns it for flipped vehicles and for a train
  running reversed; scripts never deal with it.

### Lua loco scripts (the host side)

- **One `Script::State` per run** (simulation kind), seeded by the run.
- **Each loco script is loaded once per file**, its functions shared and
  its state table per loco. `init(state, vehicle)` gives the definition's
  values through the key catalogue, in SI, e.g.
  `vehicle.engine.maxforce`.
- **`update(dt, inputs, state, out)` every step.** `out` names the
  outputs above, e.g. `out.motiveForce`, `out.pantographs[1]`,
  `out.cab.lineVoltage`.
- **`command(name, value, state)`** takes a control or command, e.g.
  `pantograph`, `circuitBreaker`, `throttle`, `headlight`.
- **The deadline** is generous per step but bounded. A script error stops
  that loco's model: it gives no force and is reported.
- **Cost:** one Lua call per loco per 0.01 s is about 100 calls a second
  per loco, which is cheap. Measured in the checks.

### The electric loco script

`appdata/0.7/scripts/loco/electric.lua`, after Open Rails' electric loco
(read 2026-10-28, upstream `8aafb3147`). Its formulas are written afresh,
with no code taken. Open Rails' own part is small (`MSTSElectricLocomotive`,
486 lines, mostly cab values). The behaviour sits in the power supply, the
circuit breaker, the controllers and the traction force of
`MSTSLocomotive`.

**Read from the definition,** in SI through the key catalogue, with Open
Rails' defaults:
- **Traction:** `MaxPower`, `MaxForce`, `MaxContinuousForce`,
  `MaxVelocity`, `RunUpTimeToMaxForce` (none: force changes at once, as
  Open Rails does for electrics), `ORTSMaxTractiveForceCurves` /
  `ORTSTractionCharacteristics` where given (29 OR_PSK files).
- **Power:**
  - `ORTSPantographs ( Pantograph ( Delay ) )` (default 0 s);
  - `ORTSCircuitBreaker` (Automatic by default, or Manual);
  - `ORTSCircuitBreakerClosingDelay`, `ORTSPowerOnDelay`,
    `ORTSAuxPowerOnDelay` (default 0 s).
- **Controls:** `EngineControllers ( Throttle … )` (steps, notches; without
  it, continuous in steps of 0.1) and `Brake_Train`.
- **Display:** `MaxCurrent`.
- **Air:** `AirBrakesMainResVolume`, `AirBrakesMainMaxAirPressure`,
  `AirBrakesCompressorRestartPressure`, `AirBrakesAirCompressorPowerRating`,
  and the train brake controller's rates and pressures.

**Each step, in this order** (Open Rails' order, with its pantograph lag
removed):
1. **Commands:**
   - pantograph n up or down;
   - breaker close or open (Manual only; Automatic ignores the driver);
   - throttle;
   - direction, which changes only at a throttle under 1 %;
   - train brake;
   - headlight 0 off, 1 dim, 2 bright.
2. **Pantographs:** Down → Raising → Up → Lowering, the delay counted up
   and down. The loco has power when any pantograph is Up.
3. **The breaker** (Automatic):
   - authorised while a pantograph is Up;
   - Open → Closing → Closed after the closing delay;
   - back to Open when authorisation goes.

   Manual also needs the driver's closing order.
4. **Power:** main power on after `ORTSPowerOnDelay` with the breaker
   closed and a pantograph up; auxiliary power after `ORTSAuxPowerOnDelay`;
   both off at once otherwise.
5. **Traction force:**
   - zero at once without main power or in neutral;
   - else, with the throttle t:
     - **without curves:** F = `MaxForce`·t, falling to 0 over the last
       0.05 m/s below `MaxVelocity`; power P = `MaxPower`·t²; F ≤ P / v;
     - **with curves:** F interpolated in t and v from the curves, then
       the same P / v cap;
   - the continuous-force heat limit where `MaxContinuousForce` is given;
   - ramped by `RunUpTimeToMaxForce` where given;
   - signed by the direction;
   - limited to the host's adhesion limit.
6. **The compressor and main reservoir:** on below the restart pressure,
   off at the maximum, while auxiliary power is on.
7. **The brake valve:** the equalising reservoir from the controller, the
   pipe at the loco toward it (the host spreads it), the loco's own
   cylinder as a wagon's.
8. **Outputs:**
   - motive force;
   - brake force;
   - **visual state:** each pantograph's commanded state (Open Rails
     animates the command, not the delay), and the headlight;
   - **cab values:**
     - line voltage;
     - the "ammeter": force / `MaxForce` × `MaxCurrent`, as Open Rails
       shows it, since it models no current;
     - force, throttle and direction;
     - breaker and pantograph states;
     - pipe, main reservoir and equalising reservoir pressures;
   - **events, as Open Rails' sound triggers:**

     | Trigger | Event |
     | --- | --- |
     | 45 / 46 | Pantograph up / down |
     | 150 / 151 / 152 | Breaker open / closing / closed |
     | 23 / 24 | Power on / off |
     | 16 | Throttle change |
     | 15 | Reverser change |
     | 17 | Train brake change |
     | 37 | Light switch |
     | 12 / 13 | Compressor on / off |

     The editor passes them to the car's SMS sounds (`SoundSource::event`,
     which takes discrete triggers already).

**Size:** a few hundred lines of Lua, about the size of Open Rails' own
pieces without their cab plumbing.

**Left out of the first script,** each a later addition:
- the dynamic brake (Open Rails needs a `Brake_Dynamic` controller for it);
- the engine brake;
- the TCS and its authorisations;
- the battery and master key;
- the Two-stage and PushButtons breakers;
- voltage selectors and dual mode.

### Commands and values (names)

- **One catalogue of TSRE's own names,** first in this task:

  | Name | Values |
  | --- | --- |
  | `throttle` | 0 to 1 |
  | `trainBrake` | 0 release … 1 full; emergency separate |
  | `direction` | -1, 0, 1 |
  | `pantograph` | n, up/down |
  | `circuitBreaker` | close/open |
  | `headlight` | off/dim/bright |
  | `horn` | on/off |

  Each is a name, a value and the vehicle (the active cab by default). They
  serialise to text, so websockets and saved runs can use them later.
- **Cab values** are named numbers the loco gives each step (speed, line
  voltage, current, force, pantograph and breaker states, brake pipe
  pressure). The widget shows them, and cab displays and sounds use them
  later.

### The control widget

- **`appdata/0.7/ui/loco.rml`** and `loco.lua`, a draggable panel like the
  HUD:
  - Pantograph ↑ ↓;
  - Breaker close/open;
  - Direction F/N/R;
  - Throttle − / + (and a bar);
  - Brake release/apply (and a bar);
  - Headlights off/dim/bright;
  - Horn;
  - readouts: speed, line voltage, current, force, brake pipe, and the
    pantograph and breaker states.
- **The UI's Lua gets a small TSRE API,** its first:
  - `train.command(name, value)`, queued to the simulation;
  - `train` values in the data model, filled from each frame's cab values.
- **It shows while a driven train exists.** The keyboard and the cab
  controls come in the next task, through the same commands.

### Frames and the editor

- **The driven train's frame gives more:**
  - each vehicle's place, with slack;
  - each vehicle's visual state (pantographs, headlight);
  - the loco's cab values;
  - the controls (throttle, brake) for the sounds.
- **The editor applies them:**
  - pantographs from the state, not "always up";
  - `EngLights::Train::headlight` from the loco's headlight;
  - sounds from the real throttle and brake instead of `SoundControls`.

## Steps

1. **The driven train, the run's step and motion.**
   - Couplers and per-vehicle forces, with a C++ stand-in loco (force from a
     throttle) and C++ wagons.
   - A content-free suite, `driving`: acceleration as F/m on the level and
     on a grade, couplers stable at 0.01 s from 2 to 50 cars, slack taken
     up on starting, determinism.
2. **The buses:** the brake pipe, wagon brakes, the control line, the line
   voltage. Checks: braking distances against the formula; a pipe charging
   and applying along 50 cars.
3. **The vehicle model interface and the Lua host,** then the electric loco
   script. Checks:
   - the script's force against the formulas;
   - pantograph and breaker timings;
   - a script error contained;
   - save and restore of the state table.
4. **Commands, the widget, frames and the editor:**
   - `train.command`;
   - `loco.rml`;
   - pantographs, lights and sounds from the state;
   - *Drive* in the panel.

   Checks: the UI suite pressing the buttons; a capture of a driven train
   with its pantograph down and up and its lights.
5. **Against Open Rails:** the same electric loco and train accelerating and
   braking on the same track in both. Speed over time compared, from Open
   Rails' web API, driven with its own controls.

Each step is built (`all`) and checked with ctest and the simulation suites
before its commit, on `feature/driving` in its own worktree. AI-only runs
must stay identical to main: the seven routes and PRUDNIK, as in task 01.

## Not in this task

- **Next task:** the keyboard and the 2D/3D cab controls and displays.
- **Later:**
  - sounds' own events beyond throttle and brake;
  - more units in a train;
  - Open Rails' air brake valves;
  - the main reservoir pipe;
  - currents on the line;
  - wheel slip;
  - diesel and steam scripts;
  - MaSzyna;
  - the external full-train link;
  - saving runs;
  - multiplayer.

## Questions for the user

1. **Starting it:** a *Drive* choice in the activity panel's Simulation
   section, with the camera riding the train and the widget shown? Or
   something else?
2. **What the core still does for a driven train:**
   - it reserves the track ahead, as for the player's AI train today;
   - it reports passing a stop signal at danger, or the end of the
     authority, without stopping the train.

   Enough for this task, with the stops left to the driver?
3. **The whole run at 0.01 s while a train is driven,** AI trains included,
   as decided? Or AI trains at 0.1 s and the driven train in ten sub-steps?
   I'd keep the decision and measure.
4. **Which script a loco gets:**
   - every electric engine (Engine `Type ( Electric )`) gets the shipped
     `electric.lua`;
   - a TSRE key in the ENG, such as `ORTSLocoScript`/`TSRELocoScript`
     `( name.lua )`, chooses another, found by the roots (`[[Vehicle]]`,
     `[[Trainset]]`).

   Is that the way? Diesel and steam engines would get a C++ stand-in force
   until their scripts exist.
5. **The dynamic brake:** in the first script, or after it?
6. **The widget's controls:** pantograph, breaker, direction, throttle,
   train brake, headlight, horn, plus readouts. Anything to add or drop for
   the first stack?
7. **Where scripts live:** shipped ones in `appdata/0.7/scripts/` and a
   vehicle's own in its folder?
