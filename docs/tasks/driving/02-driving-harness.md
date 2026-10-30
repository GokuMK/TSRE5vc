# Task 02 - The driving harness: a first whole stack

Status: the user's answers of 2026-10-28 in (decisions at the end); built on `feature/driving` (steps 1 to 5 done).

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

- **One train of a run is driven at a time.** It moves by its vehicle
  models' forces, a third driving kind beside `Simple` and `Physics`. Every
  other train stays AI.
- **Driving starts in two ways, and stops in one** (the user's, 2026-10-28):
  - **Drive beside Play** starts the run with the player's train driven.
    - **An MSTS activity:** its player service.
    - **An Open Rails timetable:** the panel gets back its player train
      choice, offering only the trains that can be driven (with an engine).
  - **Drive beside Follow** (the Timetable window, the trains list) takes
    over a running or waiting AI train.
  - **Leave driving** gives the train back to the AI, which drives on from
    where it is.
- **Moving between the two kinds of train:**
  - **AI to driven:** the one-mass train becomes vehicles, each at the
    train's speed, with its couplers at rest. The loco's script starts from
    `init` with the train's state:
    - pantograph up and breaker closed for a train already powered;
    - direction set to the way it runs;
    - throttle and brake at 0, so a running train coasts;
    - the brake pipe charged.

    A reset at the start of driving is accepted.
  - **Driven to AI:** the train's speed and front are the lead vehicle's,
    and the AI's own bookkeeping (next stop, next waiting point, the speed
    limits behind it) is found again from where the train is.
- **The core still reserves the track ahead** of the driven train, as for
  the player's AI train today, so signals clear for it and other trains
  react to it. It never stops the driven train, at stations or at signals
  (the user's, 2026-10-28):
  - passing a stop signal at danger, or the end of its authority, is
    reported as an event (Open Rails' SPAD);
  - station stops are the driver's: the task 06 events (arrival, departure)
    work as they do for the player;
  - **running into another train** (a contact faster than coupling speed)
    ends the simulation, saying which trains, where and how fast. Coupling a
    driven train is a later task.

### The step

- `MaxStep` becomes the run's step, set in the input: 0.01 s with a driven
  train, 0.1 s otherwise, as decided.
- **For now the whole run steps at 0.01 s while a train is driven,** AI
  trains included (the user's, 2026-10-28). It is measured, and whether AI
  trains go back to 0.1 s is decided after. Today's measures put a 0.1 s
  step at 0.9 µs on EUROPE1 and 5 µs with PRUDNIK's 266 trains.

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
- **`update(dt, inputs, state, out)` every step.**
  - `out` takes the forces and flows (`out.motiveForce`, `out.brakeForce`)
    and the open values (`out.values.lineVoltage`,
    `out.values.BATTERY_CHARGE`).
  - `send(name, value)` sends an event (Messages, below).
- **`command(name, value, state)`** takes every message for the loco, a
  standard control (`pantograph1`, `throttle`) or a name of its own.
- **The deadline** is generous per step but bounded. A script error stops
  that loco's model: it gives no force and is reported.
- **Cost:** one Lua call per loco per 0.01 s is about 100 calls a second
  per loco, which is cheap. Measured in the checks.

### The electric loco script

**For now every electric engine** (Engine `Type ( Electric )`) **gets
TSRE's `electric.lua`.** An ENG key naming a loco's own script comes later
(the user's, 2026-10-28). Diesel and steam engines get a C++ stand-in force,
throttle × `MaxForce` capped by `MaxPower` / v, until their scripts exist.

**The complete Open Rails electric loco** (the user's, 2026-10-28: "you
will see in practice"): everything below, and the parts first left out
(dynamic brake, engine brake, battery and master key, all four breaker
kinds).

`appdata/0.7/scripts/loco/electric.lua`, after Open Rails' electric loco
(read 2026-10-28, upstream `8aafb3147`). Its formulas are written afresh,
with no code taken. The parts other locos share are modules in `tsre/`
beside it (after step 5). Open Rails' own part is small (`MSTSElectricLocomotive`,
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

**Also in it** (the complete loco):
- **The dynamic brake:** a `Brake_Dynamic` controller, the engaging delay,
  the force from `DynamicBrakeForceCurves` or the default curve made from
  `DynamicBrakesMaximumForce` and its speeds (Open Rails' defaults, mph
  where it reads mph). It is available without line power, as in Open
  Rails.
- **The engine brake:** `Brake_Engine`, a line of its own on the loco, its
  cylinder the larger of the train brake's and its own.
- **The battery and master key:** battery power, cab power and the
  low-voltage supply, as Open Rails' default power script has them.
- **All four breaker kinds:**
  - Automatic and Manual;
  - PushButtons, where the order lasts while the button is held;
  - TwoStage, which needs the driver's closing authorisation.

**Left out** (the TCS is a system of its own): the train control system's
authorisations are always granted. Voltage selectors and dual mode don't
apply to a plain electric.

### Messages (open, the user's 2026-10-28)

Messages are elastic and future-proof. A script can send what the engine
knows nothing about: a train with extra buttons sends
`CAB_AMBIENT_LIGHT = disable` or `BATTERY_CHARGE = 44` to its cab, and the
engine passes it on.

- **A message** is a name, a value (number, text or true/false) and where
  it goes:
  - a vehicle (the active cab's by default);
  - the train;
  - the cab and UI.

  Names are free text. TSRE keeps no list a message must be on; the names
  below are only the ones TSRE itself sends or reads. Each message serialises
  as text (`name = value`), the same in Lua, in frames, and later over
  websockets and in saved runs.
- **To a vehicle's model, commands:** from the widget, later from the
  keyboard, the cab and external apps, and from other vehicles through the
  control line. The script gets every name in
  `command(name, value, state)` and handles those it knows. The others are
  ignored, and logged once each with `TSRE_DRIVING_LOG`.
- **From a vehicle's model,** two kinds:
  - **values that hold** (`out.values.BATTERY_CHARGE = 44`), published in
    the frame as an open map of the loco's names, sent only when they
    change. As built, every frame carries every value; the user's
    (2026-10-29): "All values is the obvious and usually better
    solution" (costs in `docs/features/simulation-messages.md`);
  - **events, in order** (`send("CAB_AMBIENT_LIGHT", "disable")`), passed to
    the editor once each.
- **The few names the engine itself reads,** for its own parts:
  - visual state: `pantograph1`, `pantograph2`, `headlight`, doors;
  - sounds: the event `sound` with an SMS trigger number;
  - forces and bus flows, which are outputs, not messages.

  Everything else goes untouched to the UI, the cab and later the
  websockets.
- **TSRE's names for the standard controls,** so the widget, the keyboard
  and the cab use the same ones:

  | Name | Values |
  | --- | --- |
  | `throttle` | 0 to 1, or `+`/`-` steps |
  | `trainBrake` | 0 to 1, or `+`/`-` |
  | `engineBrake` | 0 to 1, or `+`/`-` |
  | `dynamicBrake` | 0 to 1, or `+`/`-` |
  | `emergency` | true/false |
  | `direction` | -1, 0, 1 |
  | `pantograph1`, `pantograph2` | up/down |
  | `circuitBreaker` | close/open, and pressed/released for push buttons |
  | `battery` | on/off |
  | `masterKey` | on/off |
  | `headlight` | 0 off, 1 dim, 2 bright |
  | `horn` | on/off |

### The control widget

- **`appdata/0.7/ui/loco.rml`** and `loco.lua`, a draggable panel like the
  HUD:
  - Battery, Master key;
  - Pantograph 1 and 2 ↑ ↓;
  - Breaker close/open (held for push buttons);
  - Direction F/N/R;
  - Throttle − / + (and a bar);
  - Train brake, Engine brake, Dynamic brake − / + (and bars);
  - Emergency;
  - Headlights off/dim/bright;
  - Horn;
  - readouts: speed, line voltage, the ammeter, force, pressures (brake
    pipe, main reservoir, cylinder), and the pantograph, breaker and power
    states;
  - **every other value the script publishes,** in a list by name. A
    script's own messages show without the widget knowing them.
- **The UI's Lua gets a small TSRE API,** its first:
  - `train.command(name, value)`, any name, queued to the simulation;
  - `train.values`, the data model, filled from each frame's values;
  - `trainmessage`, an event per message the script sends.
- **It shows while a driven train exists.** The keyboard and the cab
  controls come in the next task, through the same commands.

### Frames and the editor

- **The driven train's frame gives more:**
  - each vehicle's place, with slack;
  - each vehicle's visual state (pantographs, headlight);
  - the loco's values, the changed ones, and its messages since the last
    frame;
  - the controls (throttle, brake) for the sounds.
- **The editor applies them:**
  - pantographs from the state, not "always up";
  - `EngLights::Train::headlight` from the loco's headlight;
  - sounds from the real throttle and brake instead of `SoundControls`.

## Steps

1. **The driven train, the run's step and motion** (done 2026-10-28,
   below).
   - Couplers and per-vehicle forces, with a C++ stand-in loco (force from a
     throttle) and C++ wagons.
   - Taking over from the AI and giving back; collisions end the run.
   - A content-free suite, `driving`:
     - acceleration as F/m on the level and on a grade;
     - couplers stable at 0.01 s from 2 to 50 cars;
     - slack taken up on starting;
     - takeover and handing back at speed;
     - a collision;
     - determinism.
   - The cost of 0.01 s for all trains, measured on the routes.
2. **The buses** (done 2026-10-28, below): the brake pipe, wagon brakes,
   the control line, the line voltage. Checks: braking distances against the formula; a pipe charging
   and applying along 50 cars.
3. **The vehicle model interface and the Lua host,** then the electric loco
   script (done 2026-10-28, below). Checks:
   - the script's force against the formulas;
   - pantograph and breaker timings;
   - a script error contained;
   - save and restore of the state table.
4. **Messages, the widget, frames and the editor** (done 2026-10-28,
   below):
   - `train.command`, open messages both ways;
   - `loco.rml`;
   - pantographs, lights and sounds from the state;
   - *Drive* beside Play and Follow, *Leave driving*;
   - the timetable's player train choice.

   Checks: the UI suite pressing the buttons; a capture of a driven train
   with its pantograph down and up and its lights.
5. **Against Open Rails** (done 2026-10-28, below): the same electric loco and train accelerating and
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

## Step 1 done (2026-10-28)

**Code** (`src/tsre/sim/`):
- **`TrainPhysics`:** the cars in running order, couplers with free play,
  a spring and a damper.
  - Each step solves one tridiagonal system along the train (backward
    Euler). It repeats the solve while couplers come in or out of their
    free play, or cars stop or start, at most 12 times.
  - Brakes and resistance are friction: a standing car stays held until
    its other forces beat it, and a moving car stops without turning
    round.
  - Where a file gives no damping, the default is a damping ratio of 1.
- **`VehicleModel`:**
  - messages: a free name; a number, text or flag; written as text
    `name = value`;
  - per step, in the vehicle's own frame, a model takes the inputs (speed,
    gradient, adhesion limit, line voltage) and gives its motive force,
    its brake force, open values and events;
  - `MakeVehicleModel`: a stand-in engine (throttle × `MaxForce`, at most
    `MaxPower` × throttle / v, fading above `MaxVelocity`, signed by the
    direction, within adhesion), and a wagon (no force yet).
- **`SimulationDriving.cpp`,** the core's driving:
  - `drive` takes over a placed train, or sets one up once it's placed;
    `leave` gives it back;
  - `command` sends a message to a vehicle; `outputsOf`, `cabOf` and
    `takeMessages` read back.
  - Each step, for every vehicle: its model's outputs; gravity from the
    heights at its two ends; Davis resistance, with the leading car's air
    drag; adhesion of 0.33 × its weight. Then the motion.
  - The core's bookkeeping goes on as for every train: track reserved
    ahead, signals passed, track released behind (keeping 100 m behind for
    backing up).
  - **Events:**
    - a signal passed showing STOP (`PassedAtDanger`);
    - the reserved track's end passed (`PassedAuthority`);
    - the path's end, where the train stops, since the track beyond comes
      later (`EndOfPath`);
    - arrival and departure at a stop's platform;
    - a reversal: at rest within 100 m of its part's end, the cab's
      direction set the other way;
    - running into another train faster than coupling speed, which ends
      the run (`Collided`, `collision()`), from the trains and standing cars
      in the sections at its front.
- **The run's step:**
  - `SimulationInput::step` (0.1 s; 0.01 s with a train driven) and
    `driven`, the train driven from the start;
  - taking over a running train switches the run to 0.01 s, and leaving
    switches it back;
  - `TSRE_SIM_STEP` holds every run at a step, for measuring.
- **The editor's event texts** cover the new events; their Polish
  translations come with step 4.

**Fixed on the way:** the pre-run counted its steps as 0.1 s each. At
0.01 s it ran only a tenth of the time, and AI trains started late; the
measure below found it. One AI rule used 0.1 s as "within a step" and now
uses the run's step.

**Suite `driving`:** 13 checks:
- one car at F/m;
- two cars sharing the pull, the slack taken up first;
- 50 cars stable at 0.01 s, the slack run-in dying out (the peak coupler
  force 1.18 × the pull);
- friction holding, beaten and stopping;
- determinism;
- messages;
- the core:
  - driving from the start, full throttle 20 s against an independent
    integration of adhesion and power (29.621 m/s, the same to four
    digits);
  - braking to a stand without rolling back;
  - the path's end;
  - taking over from the AI at 15 m/s with the speed kept, and handing
    back;
  - the step switching;
  - a collision after passing at danger, on a signalled line;
  - a driven run repeating exactly.

**Measures** (Release; `simulation-route`, AI trains only):

| | 0.1 s step | 0.01 s step |
| --- | --- | --- |
| EUROPE1, 15 h | 0.37 s | 3.85 s |
| JAPAN1, 9.7 h | 0.44 s | 4.1 s |
| USA2, 54 h | 0.69 s | 6.7 s |
| PRUDNIK, 266 trains, 20 h | 4.6 s | 40 s |

At 0.01 s the AI's results are the same as at 0.1 s: the same waits and
stops, arrivals within 1 s. The cost is ten times, still about 1,800 times
real time on PRUDNIK. Whether AI trains go back to 0.1 s is the user's
choice (decision 3).

**Unchanged:** AI-only runs of the seven routes and PRUDNIK give the same
traces as main. All the simulation, timetable and activity suites pass.

**Limits, for later steps or tasks:**
- the track beyond a path's end;
- coupling a driven train;
- collisions behind a train backing up (only ahead is checked);
- a reversal only near a part's end.

## Step 2 done (2026-10-28)

**Code:**
- **`src/tsre/sim/AirBrake`:** Open Rails' single pipe air brake, simplified
  (its formulas written afresh, its defaults kept):
  - **`AirBrake`,** one per vehicle: its valve from `BrakeEquipmentType`, a
    plain triple valve or a distributor (graduated release, with its control
    reservoir), its auxiliary reservoir and its cylinder. Without a valve
    there's no brake from the pipe, as in Open Rails.
    - The force is `MaxBrakeForce` × (cylinder − spring) / (reference −
      spring), with Open Rails' brake shoe friction for older stock (the full
      force below about 20 km/h, two thirds at 100 km/h).
    - An EP brake (`BrakeSystemType ( EP )`) also fills its cylinder to the
      demand on the train's EP wire, which works only with an EP cab, as in
      Open Rails.
    - Vacuum brakes get the triple valve until they have a model of their
      own.
  - **`BrakePipe`:** a length of pipe per vehicle, the air between
    neighbours solved implicitly each step. It is stable at any step and
    keeps the air exactly. The rate is Open Rails' pipe time factor.
  - **`BrakeValve`:** a loco's train brake controller (no notches yet):
    - the equalising reservoir set from its position;
    - the pipe at the loco charged toward it at the charging rate, or vented
      toward it;
    - vented to nothing in an emergency.
- **The model interface:**
  - in: the pipe's pressure and volume at the vehicle, its cylinder;
  - out: the air its valve lets into the pipe, and the control line's
    messages.

  The host adds `brakePipe` and `brakeCylinder` to every vehicle's values.
- **The air brake is the host's, on every vehicle.** A model gives only the
  air of its brake valve and any brake of its own, so scripts don't write
  triple valves again. The loco's own cylinder is its air brake, like a
  wagon's.
- **Each step, in this order:**
  1. the models, with the pipe at each, their valves' air into the pipe;
  2. the control line;
  3. the air brakes;
  4. the air along the pipe;
  5. the forces.
- **The control line:**
  - each unit's messages reach every other unit's model;
  - the host reads one name itself: `epBrake`, the EP wire's demand.
- **The stand-in engine:**
  - its train brake works its brake valve, and `emergency` vents the pipe;
  - with the cab, it sends its throttle, direction and EP demand on the line;
  - it shows the line voltage.
- **The line voltage:** the route's `MaxLineVoltage` (TRK) in the run's
  input.
- **The key catalogue:** keys Open Rails hands to a parser of their own (the
  brake controllers, `ORTSPowerOnDelay`, `ORTSCircuitBreaker`, the master
  key) now carry their units from that parser.
- **`VehicleDefinition::text`,** a text value by path.

**Suite `driving`,** 5 more checks (18):
- **the pipe alone:** the air kept to 1e-9, a vented end pulling it down in
  order, stable at a 1 s step, even at rest;
- **50 cars standing, at half service:**
  - every cylinder at 32.1 to 32.4 psi (13 psi × 2.5 = 32.5 expected);
  - the first car applying at 0.11 s, the last at 2.89 s;
  - released, the last car empty at 190 s;
  - its pipe back to full at 674 s. Open Rails' loco charges only its own
    length of pipe at 21 psi/s, and the MSTS-like files' 14 ft³ emergency
    reservoirs make 10 ft³ auxiliary ones. To compare in step 5.
- **an emergency stop from 20 m/s:** 247.6 m, against the brake forces
  integrated alone (247.6 m);
- **two engines:** the second at the cab's throttle and direction, pulling;
  the line's voltage reaching the engines;
- **EP:** half the brake fills the last car to 35 psi within 2 s, and it
  releases.

The trains in the core checks are now read from ENG and WAG files written by
the suite, through `VehicleDefinition::read`.

**Unchanged:**
- AI-only runs of the seven routes and PRUDNIK give the same traces as main.
- The simulation, timetable, activity and vehicle file suites pass; so do
  build `all` and ctest.

**Left out of Open Rails' brake, for later:**
- emergency reservoirs and valves, quick service, retainers, two-stage
  braking, relay valves, accelerated application;
- the advanced cylinder;
- brake shoe types and friction curves (the older stock's curve for all);
- handbrakes;
- the twin pipe's main reservoir pipe.

Coming in step 3 with the electric loco:
- the controller's notches;
- the main reservoir limiting the charge;
- the engine brake.

## Step 3 done (2026-10-28)

**The Lua host** (`src/tsre/sim/LocoScript`):
- **`LocoScripts`:** the run's Lua state (TSRE's simulation kind: seeded,
  no clock, time and memory limits), made on the first takeover of a
  scripted loco. It loads each script file once. Every engine of
  `Type ( Electric )` runs `appdata/0.7/scripts/loco/electric.lua`; the
  others keep the C++ stand-in.
- **A script's functions,** all optional but `update`:
  - `init(state, vehicle)`, once;
  - `update(dt, inputs, state, out)`, every step;
  - `command(name, value, state[, from])`, every message for it; `from` is
    `"line"` for one from another unit on the control line;
  - `save(state)`, what to save (else the state table).

  While they run, `send(name, value)` sends an event and `line(name,
  value)` a message to the train's other units.
- **`vehicle`,** the loco's file through the key catalogue, in SI:
  - `vehicle.engine.maxforce`, `vehicle.wagon.mass`: a key's first value;
  - `vehicle.number(path, index, default)`, `text`, `values` (the last such
    block's), `all` (every such block's), `blocks` (the blocks inside one,
    each with its name and values: the curves), `has`.
- **`inputs`:**
  - `speed`, `gradient`, `adhesionLimit`, `lineVoltage`;
  - `brakePipe`, `brakePipeVolume`, `brakeCylinder`;
  - `cab`.
- **`out`:**
  - `motiveForce`, `brakeForce`;
  - `brakePipeAir`;
  - `independentBrake` (the loco's cylinder gets at least this; the host's
    air brake takes the larger), `bailOff` (a rate);
  - `values`.
- **Each loco has its own state table,** saved and restored as text by
  TSRE's Lua state.
- **A script error stops every loco running that script:** they give no
  force, and each tells it once (event `scriptError`). The run goes on. An
  endless loop stops at the call's 100 ms limit.

**Control-line directions are the sender's:** the host turns a `direction`
message for a unit facing the other way.

**`electric.lua`** (about 1,200 lines), Open Rails' electric locomotive
from its behaviour: `reports/or-electric-loco-spec.md`, read from Open
Rails 8aafb3147 with file and line references; no code was taken. In Open Rails' order each step:
- **The battery and master key:** AlwaysOn, Switch and PushButtons (the
  push buttons as a switch for now); Manual, with its delay off and its
  headlight control.
  - As in Open Rails' default electric supply, they give only cab and low
    voltage power. Main power, the breaker and traction don't depend on
    them.
- **The circuit breaker:** Automatic, Manual, PushButtons and TwoStage,
  with their driver orders and closing authorisation, the closing delay,
  and one change of state a step.
- **The power supply:**
  - pantographs (two at least, with their delays, `ORTSPantographs` read
    from the Wagon section as Open Rails does);
  - the line voltage filtered as Open Rails does (0.7 Hz);
  - main and auxiliary power after their delays;
  - all off at once without a pantograph up or with the breaker open;
  - a hot start, or everything on when taken over at speed.
- **The levers:**
  - Open Rails' notch controllers: notched and smooth notches, one notch or
    one step a press, 5 steps a second while held;
  - the train brake's equalising reservoir by every notch type, the
    minimum reduction latch, the EP wire's demand;
  - the engine brake's demand and line, from the main reservoir;
  - bail off, with main power only;
  - the dynamic brake after its delay, the interlocks with the throttle.
- **Traction:**
  - throttle × `MaxForce`, at most `MaxPower` × throttle² / v, the fade at
    `MaxVelocity`;
  - or `ORTSMaxTractiveForceCurves` / `ORTSTractionCharacteristics`,
    interpolated and extrapolated as Open Rails;
  - the `MaxContinuousForce` heat limit, the ramps from
    `RunUpTimeToMaxForce`, `DoesBrakeCutPower`;
  - the direction changed only with the throttle shut;
  - the host's adhesion limit.
- **The dynamic brake:** Open Rails' default curve (its speeds read in mph)
  or `ORTSDynamicBrakeForceCurves`; no line power needed.
- **The compressor and main reservoir.**
- **Driving the train:** the brake pipe at the loco charged from the main
  reservoir, vented, or vented in an emergency.
- **Headlight, horn, emergency button.**
- **Sounds:** Open Rails' trigger numbers, sent as `sound` events.
- **Values:**
  - the levers, direction, forces, ammeter;
  - the line and filter voltages, the pantographs and their states, the
    breaker, main and auxiliary power, battery, master key;
  - the compressor and the pressures;
  - emergency, headlight, horn.
- **The cab's loco sends to the other units:** its throttle, direction,
  dynamic brake, pantographs, breaker, battery, master key and quick power
  commands.

**Commands:**
- TSRE's names (the table above);
- `<lever>Increase` / `<lever>Decrease` true/false, for a lever held (the
  keyboard, next task);
- `power` on/off (Open Rails' quick power);
- `circuitBreaker` close/open/released;
- `circuitBreakerAuthorization`;
- `bailOff`.

**Open Rails' odd behaviour, kept:**
- Running, SelfLap and Neutral leave the equalising reservoir alone;
- the half bar slow-down band;
- the engine brake's sounds swapped (21 on release, 22 on apply);
- bail off only with main power;
- the breaker seeing a raised pantograph a step late.

**Open Rails' odd behaviour, not kept:**
- **A missing `Throttle` block:** Open Rails' lever never moves; TSRE's is
  continuous in steps of 0.1.
- **No `MaxPower`:** Open Rails gives no force once moving; TSRE gives no
  power limit.
- **No `AirBrakesMainResVolume`:** Open Rails divides by zero, so the main
  reservoir fills at once; TSRE takes its 0.3 m³ default.

**Left for later:**
- **For the keyboard and cab task:** `ORTSDelayTimeBeforeUpdating`, the
  overcharge and quick release buttons, the sound variables.
- the engine brake's line shared by a multiple unit;
- the battery push buttons' own behaviour;
- the train control system (always authorised);
- wheel slip (the host's adhesion limit stands in).

**Suite `driving`,** 16 more checks (34):
- **The host:**
  - a test script reading its file in SI and echoing a message of its own;
  - two engines pulling at the cab's throttle over the control line;
  - its state saved and restored into a new loco;
  - an error stopping only its locos' force, told once each;
  - an endless loop stopped at 100 ms.
- **`electric.lua`, force:** against the formulas at four throttle and
  speed points (exact), and 202 kN after an hour at full force (the 200 kN
  continuous force).
- **`electric.lua`, dynamic brake:**
  - none in its first second;
  - −150 kN at 20 m/s, −84.2 kN at 6 m/s (exact);
  - no throttle while it is on.
- **`electric.lua`, saved and restored:** the same steps after.
- **`electric.lua`, the hot start's timings** (an ENG with 3 s pantographs,
  a 2 s manual breaker, 1 s and 2 s power delays):
  - pantograph up at 2.99 s;
  - the breaker closing a step later, closed 2.01 s after that;
  - power 1 s after that, auxiliary power 2 s after;
  - moving with power;
  - the line at 25 kV.
- **`electric.lua`, train brake:** full service 64 psi, the pipe following;
  the graduated notch at half, 74 psi; released, 90.
- **`electric.lua`, the compressor:** the main reservoir drawn below its
  restart pressure, filled to its maximum.
- **`electric.lua`, emergency and engine brake:**
  - emergency vents the pipe below 5 psi in 1 s;
  - the engine brake brings the loco's cylinder to 25 psi in 2 s and 64 psi
    after that, the wagons' staying at 0.
- **`electric.lua`, a pantograph lowered:** the breaker opens and power is
  off in the same step, sounds 46 47 150 24 228.
- **`electric.lua`, a turned second loco:** at direction −1 and the cab's
  throttle, both pulling.
- **The cost:** 17 µs a step per scripted loco (Release).

**Suite `driving-corpus`** (`--game-root`): every electric engine through
`electric.lua`:
- **MSTS:** 9 of 9.
- **OR_PSK:** 817 electric engines in 14 s:
  - no script error;
  - every one powered (some breakers close in 40 s);
  - every one with a `MaxForce` pulling at 5 m/s, or half its top speed.
  - The Vectron's characteristics table gives 292.8 kN at 18 km/h, as the
    table has it.

`TSRE_VEHICLE_FILE=<part of a path>` prints a loco's values.

**Unchanged:**
- AI-only runs of the seven routes and PRUDNIK give the same traces as main.
- The simulation, timetable, activity, vehicle file and RmlUi suites pass;
  so do build `all` and ctest.

**The appdata archive** (koniec.org's 0.7.tar) gets `scripts/loco/` when
it is next rebuilt from a release.

## Step 4 done (2026-10-28)

**Driving in the editor:**
- **Drive beside Play** (the Activity panel; a fifth button in Play's
  row):
  - it runs the activity with the player's train driven from its start: a
    timetable set's chosen train, an MSTS activity's player service;
  - in a run without a driven train, it takes the player's train over;
  - while a train is driven it reads **Leave** and gives the train back to
    the AI.
- **Drive beside Follow** (the Timetable window, a toggle):
  - it takes the chosen train over, starting a run first when there is
    none;
  - a train not yet on the track is taken over once it is there;
  - the camera follows it;
  - off, the AI drives it on.
- **A timetable set's player's train:** the existing choice lists every
  running train. Only those an engine drives can be chosen; the others show
  why in a tooltip (`TimetableTrainDrivable`).

**The controller** (`SimulationController`):
- `playDriving`, `drive`, `leave`, `drivable`, `driven`, `drivenValues`,
  `command`, `takeDrivenMessages`, the signal `drivenChanged`;
- `start(…, driving)`, which a reset keeps.

The host takes drive, leave and command in the commands' queue (their
sequence numbers acknowledged as every command's).

**Frames:**
- the train driven;
- each of its vehicles' values (`DrivenFrame`);
- the collision.

The models' messages go to a mailbox beside the events'; the views take
them each tick.

**The editor applies them:**
- pantographs 1 and 2 from each vehicle's values (an AI electric keeps
  pantograph 1 up);
- the cab's `headlight` for the consist's lights (`Consist::setHeadlight`);
- the sounds' throttle from the cab's lever, and their brake from its
  cylinder;
- each `sound` message played as its SMS discrete trigger on that car
  (`Consist::soundEvent`, `Eng::soundEvent`, `SoundSource::event`).

**The widget,** `appdata/0.7/ui/loco.rml`, `loco.lua`, `loco.rcss` and
`Ui::LocoControls`:
- a panel at the view's top right, dragged by its title, while a train is
  driven;
- **buttons for:**
  - battery, master key, pantographs 1 and 2;
  - the breaker (held, released on leaving the button);
  - direction;
  - the throttle and the train, engine and dynamic brakes;
  - emergency, the horn (held), headlights;
  - Leave;
- each lever's bar and notch;
- **readouts** in the user's units (speed, line voltage, current, force,
  pipe, main reservoir, cylinder, pantographs, breaker, power), and every
  value the loco publishes on demand;
- the last messages its models send (`CAB_AMBIENT_LIGHT = disable`
  included).
- **The UI's Lua gets `train`:**
  - `train.command(name, value)`, any name;
  - `train.leave()`;
  - `train.values` (SI) and `train.texts` (as shown);
  - the events `trainupdate` and `trainmessage`.

**Texts:** the buttons' and readouts' ids in `UiTexts.cpp`; Polish for
every new text, the driving events' included.

**Checks:**
- **`rmlui` (35):**
  - the panel's buttons send their commands (the breaker pressed and
    released), Leave;
  - the pipe's readout in the user's units, the throttle's bar, every value
    and the messages listed;
  - hidden.
- **`simulation-ui` (80):**
  - Drive beside Play drives the player's train from the start at 0.01 s,
    the button then Leave, the Timetable window's Drive on;
  - a command reaches its cab's script and its sound (66) comes back;
  - Leave gives it back, told;
  - the Timetable window's Drive takes the AI train over once on the track
    and follows it; off gives it back.
- **`timetable-ui`:** still 37.
- **The capture `tests/renderer/driving-loco.json`** (USA1, brdgstrk, the
  player's Acela driven, the panel over the view):
  - pantograph up, then lowered (the breaker opening, power off, 0 V);
  - at 19:30, in the dark, the headlights on (their beams on the track),
    then off.
  - The capture runner gets `simulationDriven` and each view's `commands`,
    and places each view by the latest frame.

**Unchanged:**
- AI-only runs of the seven routes and PRUDNIK give the same traces as main.
- The simulation, timetable, activity, vehicle file, RmlUi, sound and
  driving suites pass; so do build `all` and ctest.

**Left for later:**
- the values sent only when they change (now each frame's in full);
- a dialog when a collision ends the run (now its event in the list and the
  run finished).

**Noticed:** an ENG's train brake starts in its controller's default notch.
The Acela's is GSelfLapH, with the brakes applied, as in Open Rails. The
driver releases them first.

## Step 5 done (2026-10-28)

**The tools:**
- **`simulation-route` with `TSRE_SIM_DRIVE=<activity>:<commands>`:**
  - the activity's player's train driven from its start by TSRE's loco
    scripts;
  - its commands sent on time (`seconds name value` a line);
  - the train sampled every half second to `TSRE_SIM_DRIVE_LOG` (CSV:
    speed, distance, the levers, the loco's and the last car's pipe and
    cylinder).
- **`scripts/orts-drive-run.sh`:**
  - the same commands posted to Open Rails' web API (`/API/CABCONTROLS`)
    at its game times;
  - its driving display read every half second.
  - Open Rails takes a posted value only when it changes, starting from 0,
    so the script nudges a repeated value.
- **`scripts/drive-compare.py`:** the two side by side.
- **The runs:** `reports/or-driving-comparison.md`.

**Fixed:** a command sent before the train is taken over (the run not yet
stepped, or the train not on the track) was dropped. The core now keeps it
for the takeover. One more check in `driving` (35).

**Where they agree:**
- **Traction within adhesion:**
  - USA1's HHP-8 and 8 cars at throttle 0.6: 10 m/s at 59.5 / 60.2 s, top
    speed 29.35 / 28.7 m/s after 90 s;
  - distance within 2.5%.
- **Braking within adhesion:** a light application stops it in 569 / 582
  m.
- **The single pipe** (USA2, 3 Dash 9s and 38 freight cars):
  - a 13 psi reduction gives 77 / 78 psi and cylinders of 32 / 31 psi;
  - after release both are back to full at 570 to 600 s.

**Where they differ:** what TSRE leaves out of Open Rails' models.
- **Wheel slip:** at full throttle Open Rails' HHP-8 slips the whole time.
  It has 310 kN on 101 t and no slip control, so its force is a third of
  TSRE's, which has only the 0.33 adhesion limit (49 / 24.6 m/s after 90
  s).
- **Wheel slide:** at full service Open Rails' cars slide (70 kN on 30 t),
  so its stop is half as hard (351 / 528 m from 29 m/s).
- **The main reservoir pipe** (`Air_twin_pipe`):
  - Open Rails recharges the distributors' reservoirs from it, and releases
    in about 10 s;
  - TSRE recharges them from the brake pipe, and takes minutes;
  - under a light application the loco's cylinder stops at 23 psi against
    Open Rails' 27.
- **Emergency reservoirs:** Open Rails' freight cars recharge from them on
  release, so its pipe is at 86 psi 30 s after the release against TSRE's
  81, and its cylinders empty sooner.
- **A service's `StartingSpeed`:** Open Rails starts USA2's freight at
  15 mph with its throttle at 25%; TSRE starts it standing.
- **Small:** the HHP-8's suppression notch, set over the web API, gives 84
  psi in Open Rails and 82 in TSRE (FullServicePressureDrop 28).

**Unchanged:**
- AI-only runs of the seven routes and PRUDNIK give the same traces as main.
- The simulation, timetable, activity, vehicle file, RmlUi and driving
  suites pass; so do build `all` and ctest.

## After step 5 (2026-10-28): the main reservoir pipe, the starting speed

The user's: the main reservoir pipe is the most important of the gaps; the
starting speed is to be done if there is a clear way; wheel slip and slide
are not decided.

**The main reservoir pipe:**
- A second `BrakePipe` along the train, joined only between twin pipe
  vehicles (`Air_twin_pipe`, EP, ECP); `BrakePipe::joined`.
- **Its feed:** each twin pipe loco's model:
  - `electric.lua` equalises its main reservoir with its length of the pipe,
    at most `AirBrakeMaxMainResPipePressure` (by default the reservoir's
    maximum and 5 psi), as Open Rails;
  - the C++ stand-in keeps the pipe at its pressure.
  - The interface: `inputs.mainPipe` in, `out.mainPipeAir` out.
- **Its use:** a twin pipe distributor recharges its auxiliary reservoir
  from it, up to its control reservoir, whenever it is above the brake
  pipe. This is Open Rails' `ORTSMainResPipeAuxResCharging`, on by default.
- **Values:** `mainPipe`, `auxiliaryReservoir`, `controlReservoir` for
  every vehicle.
- **Against Open Rails:** the HHP-8's light application released at 200 s.
  Both are at 107 psi in the pipe and 6.6 to 8.6 psi in the cylinders
  after 5 s, and fully released within 10 s; before, TSRE took minutes.
- **The loco's own cylinder** stays at 23.3 psi under that application. The
  HHP-8's file gives its loco `TripleValveRatio ( 2.03 )`: 11.5 psi × 2.03.
  Open Rails' driving display shows 27; whether that line is the loco's
  own cylinder is to look into.
- **Check (`driving`, 36):** twin pipe distributors release in 3.4 s, the
  same on a single pipe in 105 s; the main reservoir pipe at 129 psi.

**A service's starting speed:**
- **The plan's `startSpeed`:** the service's `StartingSpeed` (`.srv`
  TimeTable, m/s), for the player's train and AI trains alike, as Open
  Rails takes it.
- **The core's `beginRun`:** the train goes on moving at it, unless it
  starts at its first station (Open Rails' InitializeMoving).
- **Driven from the start,** its throttle is set as Open Rails sets it: 25%,
  40% up a grade over 2%, none down one over 1%.
- **Against Open Rails:** USA2's freight now starts at 6.7 m/s with its
  throttle at 25% in both and climbs to a stand (189 / 208 s; the Dash 9s
  are TSRE's C++ stand-in, not a diesel model).
- **AI-only runs change where services start moving:**
  - **Unchanged:** JAPAN1, USA1 and PRUDNIK traces are still identical to
    main's.
  - **Changed:** EUROPE1, EUROPE2, JAPAN2, USA2 and BNSF_SCENIC have 4, 3,
    1, 13 and 7 such services. Their trains reach their stops and signals
    seconds to tens of seconds earlier, with the same events. USA2: still 46
    of 48 finished, 2 waiting for track, 26 stops, no deadlock.

**The runs:** `reports/or-driving-comparison.md`, its last two sections.

## After step 5 (2026-10-28): Lua's cost, the shared modules

The user's, after a look at Lua's cost and readability: stop Lua with a
watchdog instead of a hook, copy the values for less, split `electric.lua`
into modules the loco scripts share. Speed alone is no issue, even for a
dozen scripted trains. User scripts may use the modules too.

**The watchdog** (`Script::State`, every state, the UI's too):
- **Before:** a count hook made Lua's VM trap every instruction and look
  at the clock every thousand, about 5 µs of a loco's 16 µs step.
- **Now:** Lua runs without a hook. One thread waits for the deadlines' ends.
  It wakes at the earliest, and every 50 ms while calls keep starting, so a
  call's start does not wake it.
  - Past a deadline, it sends a signal (`SIGURG`) to the deadline's thread.
  - The handler sets the hook that ends the script there. Lua allows a hook
    to be set from a signal handler, as its own interpreter stops a script,
    but not from another thread.
- **Unchanged:** nested deadlines keep the earliest end, and a script cannot
  catch the timeout or go past the memory limit.
- **Still a clock-checking hook:**
  - coroutines, since the handler cannot tell which of a state's threads
    runs;
  - Windows, which has no signal to stop a thread; there it stays as before.
    **Open issue, not built or tested on Windows:** see open question 6.
- **Checks (`script`, 31):**
  - loops in `coroutine.wrap` and in a sort's comparator stopped at 50 ms;
  - no hook between calls;
  - an inner deadline ending while the outer one goes on to its own end
    (400 ms);
  - two states on two threads stopped at once.

**The values' names:**
- The 33 names a loco publishes are converted to Qt text once, and kept by
  where Lua keeps the name. Each is checked against its bytes, as a
  collected name's place may later hold another.
- This saves 0.6 µs of the 3.5 µs copy, less than expected. The rest is
  Lua's table walk (1.5 µs, five text values among it) and Qt's hash
  (1.0 µs). Copying only when a frame is made would change the vehicle model
  interface, for about 2 µs; not done.

**The modules** (`appdata/0.7/scripts/loco/tsre/`, `require "tsre.brakes"`):

| Module | Holds |
| --- | --- |
| `util` | units, `clamp`, `flag`, `sound`, a file's numbers with defaults |
| `levers` | Open Rails' controllers and notches |
| `curves` | force curves and tables |
| `controls` | the cab's levers, direction, emergency button, headlights, horn, bail off: commands, sounds, interlocks |
| `supply` | battery and master key |
| `pantographs` | the pantographs |
| `breaker` | the circuit breaker |
| `traction` | traction and the dynamic brake |
| `brakes` | the main reservoir and compressor, the equalising reservoir, the pipe at the loco, the engine brake, the main reservoir pipe's feed |

- **A part's shape:** `init(s, vehicle)` reads its keys into `s.config` and
  sets its state in `s`; its steps follow; `values(s, o)` publishes its
  values; `commands` holds its handlers by name. A handler returns what goes
  on to the train's other units.
- **`electric.lua`,** now 213 lines, keeps the electric power supply (its
  delays, the voltage filter, quick power) and puts the parts together, in
  the same order as before.
- **`send` and `line`** now sit beside the libraries in the run's state, so
  modules can make sounds.
- **The same results:**
  - the 36 driving checks print the same numbers (but the timing);
  - USA1's two drive runs give the same CSVs byte for byte;
  - OR_PSK's corpus is the same.

**A user's script** can `require` the same modules. Once an ENG can name its
own script, the state's module roots will be the script's folder first, then
TSRE's.

**The cost a scripted loco step** (Release, the probe's loop):

| | µs |
| --- | --- |
| Before | 16.4 |
| The watchdog | 11.0 |
| Cached names | 10.4 |
| Split into modules | about the same |

The `driving` suite's own check gives 11.0 to 11.4 µs, against 17 at step 3.
OR_PSK's 817 electric engines run in 10.9 s, down from 14 s.

## Open questions (user, 2026-10-28: kept open until tested enough)

1. **The battery and master key** give only cab and low voltage power, as
   in Open Rails' default electric supply. Should they also cut main power,
   the breaker and traction?
2. **Open Rails' odd behaviour not kept** (step 3): a missing `Throttle`
   block, no `MaxPower`, no `AirBrakesMainResVolume`. TSRE's choices stand
   for now.
3. **Open Rails' odd behaviour kept** (step 3):
   - Running, SelfLap and Neutral leave the equalising reservoir alone;
   - the half bar slow-down band;
   - the swapped engine brake sounds;
   - bail off only with main power;
   - the breaker seeing a pantograph a step late.
4. **The 50-car pipe recharging in 674 s** (step 2): answered in step 5.
   On a single pipe train Open Rails recharges at the same rate: both are
   back to full at 570 to 600 s on a 41-vehicle freight after a 13 psi
   reduction.
5. **What step 5 found TSRE leaves out of Open Rails' models**, to put in
   order:
   - wheel slip, traction beyond adhesion: not decided;
   - wheel slide, braking beyond adhesion: not decided;
   - the twin pipe's main reservoir pipe: done after step 5 (the user's
     most important);
   - emergency reservoirs;
   - a service's `StartingSpeed`: done after step 5.
6. **Open issue: Lua's watchdog on Windows** (2026-10-29; the user will test
   it on a Windows machine).
   - **Now:** on Windows, `Watchdog::get()` returns null
     (`src/tsre/script/ScriptState.cpp`). Every Lua state there keeps the
     count hook it had before the watchdog: the VM traps every instruction
     and looks at the clock every thousand. Time limits work as before, but a
     scripted loco step costs about 5 µs more than on Linux (16 against
     11 µs here).
   - **Not done on this Linux box:** the Windows build was neither compiled
     nor run. The new code's Windows branches are under `Q_OS_WIN`:
     - `Watchdog::get()`, `currentThread()` and `interrupt()` as stubs;
     - `State::deadlineThread`, a `std::thread::native_handle_type`;
     - the script suite's "no hook between calls" check, left out there.
   - **To test on Windows first:**
     1. the build (MSVC or MinGW, whichever the Windows build uses);
     2. `--test --test-suite script` (31 checks, endless loops stopped at
        50 ms, also when caught, in coroutines, in a sort's comparator, two
        threads at once), `rmlui` (35) and `driving` (36);
     3. the `driving` suite's line "electric.lua: … µs a step", to compare
        with Linux.
   - **To do, a Windows watchdog**, tested there:
     - Past a deadline, the watchdog suspends the deadline's thread
       (`SuspendThread`), makes the suspension take effect
       (`GetThreadContext`), sets the hook (`lua_sethook`) and resumes it
       (`ResumeThread`). This is the Windows form of the signal handler: Lua's
       hook may be set while its thread is stopped, not while it runs.
     - The thread's handle comes from `OpenThread` once per thread, kept, not
       per call.
     - The suite's "no hook between calls" check then applies on Windows
       too.
     - Risk to look at: a thread suspended while it holds a lock the
       watchdog needs (the heap's). `lua_sethook` takes no lock and
       allocates nothing.

## Decisions (user, 2026-10-28)

1. **Drive beside Play** (the activity's player service; for an Open Rails
   timetable the panel's player train choice comes back, offering the
   trains that can be driven). **Drive beside Follow** takes over an AI
   train. **Leave driving** gives it back to the AI. Moving between the
   AI's one-mass train and the driven vehicles is handled, with a reset of
   the driving state at the start where needed.
2. **No forced stops** at red signals or stations. A collision with another
   train may end the simulation, with what happened.
3. **The whole run at 0.01 s** while a train is driven, measured; decided
   later.
   - **The user's, 2026-10-30:**
     - **If needed, the AI step can be halved or quartered** (0.05 or
       0.025 s). PRUDNIK's whole day would take about 9 or 18 s instead of
       4.6 s (an estimate), which is still little.
     - **A finer step only for real-time runs was planned from the
       start:** every train at 0.01 s while the run plays in real time,
       and 0.1 s when it runs fast (to a time, the pre-run).
     - **It waits** because fast and real-time runs would then differ
       slightly. Tests and comparisons come first. The first comparison is
       step 1's table: the AI at 0.01 s gives the same waits and stops,
       with arrivals within 1 s.
     - **Shadows** (sound task 02) step at their own 0.1 s (`ShadowStep`),
       whatever the run's step.
4. **For now every electric engine uses TSRE's `electric.lua`.** A custom
   script named in the ENG comes later.
5. **The complete Open Rails electric loco,** dynamic brake included.
6. **The widget:** the controls of the complete loco (above), and every
   value a script publishes listed by name. The user asked what "add or
   drop" meant; this list is the answer, open to change.
7. **Scripts:** TSRE's in `appdata/0.7/scripts/`, a vehicle's own in its
   folder.
8. **Messages are elastic and future-proof:** scripts send and take any
   name, and the engine passes on what it doesn't know (`CAB_AMBIENT_LIGHT
   = disable`, `BATTERY_CHARGE = 44`).
