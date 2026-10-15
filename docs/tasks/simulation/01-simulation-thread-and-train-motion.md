# Task 01 - The Simulation Thread and Trains in Motion

Status: design (2026-10-15), decisions taken the same day (at the end).

Sources:
- `msts-analysis/reports/signalling/` (2026-10-14, -15): `overview.md`,
  `threading-and-update-loop.md`, `preview-design.md`,
  `basic-timing-preview.md`, `ai-driving.md`, `msae-simulation.md`.
- Open Rails (`openrails-linux-spike`), a functional reference only:
  `AITrain.cs` (rates), `Train.CalculateStationStop` (stop position),
  `AI.PrerunAI`.
- TSRE on main at `17a5bf8` (activity editing merged).

## Objective

The first step of activity simulation (roadmap): a simulation thread that
runs the activity's trains and shows them in the editor.

- **A simulation core on its own thread,** owning all runtime state, fed by
  an immutable copy of the track and activity data, driven by commands
  (play, pause, speed, reset), publishing what the editor draws.
- **Trains run their paths and timetables:** AI trains appear at their start
  times, the player's train with the activity and runs its timetable as
  they do (an MSAE-style preview); they accelerate, run, brake to their
  stops, wait for their departure times, stop at waiting points, reverse at
  reversal points and leave at their path ends.
- **No signals and no interaction:** trains that meet pass through each
  other. Occupation and authority are the next task.
- **A new train traveller** for the simulation, moving along a train's
  route (its path as track intervals in order) instead of `Ruch`'s
  per-junction choices and 0.5 m steps over the live track database.

Not in this task: signals, occupation and reservations, collisions, speed
posts and curve limits, activity events, coupling and uncoupling, the
player driving (controls), sounds of simulated trains, physics, saving
simulation state (`.asv`), stepping backwards other than by replaying.

## Current state (main, 2026-10-15)

- **Today's "simulation"** runs on the main thread from the GL widget's
  timer: `RouteEditorGLWidget::timerEvent` → `Route::updateSim` (loaded
  tiles' animations, then the current activity) → `Activity::updateSim` →
  the player's service → `Consist::updateSim` → `Eng::updateSim` (a
  stand-in driver, `PlayDriver`) and `Eng::move`, plus the train's sounds.
  Only the player's train moves; AI trains are drawn standing at their
  starts (task editor 07, phase 7).
- **`Ruch`** (`tsre/tdb/Ruch.cpp`), the track traveller: a vector node, a
  distance and a direction; it moves in 0.5 m steps, reads the global
  `Game::trackDB`, takes junction branches from one choice per junction
  (`Path::getJunctionDirections`, the last pass of the path winning) and
  turns back at a track end. It knows nothing of the path's reversals or
  waits. Each `Eng` holds two (its bogies) and is drawn from them.
- **Paths** (task editor 06): resolved nodes (start, end, junction,
  reversal, wait) and the legs between them (vector node, from, to);
  platform passes with their distances down the path (task editor 07).
- **Activity data** (task editor 07): `ActivityTrains` lists the player's
  and the AI trains with their timetables; services give paths and
  consists; the activity gives efficiencies per stop.
- **Track to world:** `TDB::getDrawPositionOnTrNode` scans a node's placed
  sections and applies the section's geometry (`TSectionDAT`).
- **Placement:** a service's train stands behind its path start, the first
  vehicle across it, moved ahead when the track behind is shorter
  (`Consist::initOnTrack`, task editor 07).

## Reference

From the analysis (MSTS, MSAE) and Open Rails' code:

- **MSTS's traffic driver is kinematic:** current and target speed,
  acceleration, and a braking test `distance <= v × 1 s + (v² − target²) /
  (2 × deceleration)` against a list of constraints (stops, waits,
  reversals, path end, speed limits, signals).
- **MSAE's preview:** Play and pause, 1× to 128×, a one-second scheduler
  step; going backwards resets and replays.
- **Open Rails' AI:** acceleration 1.0 m/s² and braking 1.0 m/s² for
  passenger trains (braking 1.5 above 40 m/s, 2.5 above 55 m/s), 0.5 and
  0.5 for freight; speed cap the lower of the route's limit and the
  consist's maximum speed × efficiency. A train stops centred on its
  platform (a longer one overhangs both ends equally, within its route).
  It departs at the booked time or, when late, after its booked dwell. AI
  trains started before the activity are run forward to its start
  (pre-run, five-second steps).
- **Waiting points** (task editor 06, `WaitingPoint`): seconds; until a time;
  uncouple keeping the first or last N cars, waiting S seconds; horn and
  crossing horn, which do not stop the train.

## Design

### 1. Threads and ownership

| Thread | Owns | Talks by |
|---|---|---|
| Main | Editor data, UI, rendering, the presentation of simulated trains | Builds the input at start; sends commands; reads frames; delivers events to editor objects |
| Simulation | The core and all runtime state | Reads its input; publishes frames and events |

- The core (`SimulationCore`) is plain C++: no QObject, no globals
  (`Game::trackDB`, `Route`, `Service`, `Consist` and `Eng` are never
  touched), a `step(seconds)` that tests and fast runs call directly.
- The host (`SimulationHost`) is a QObject moved to a `QThread`: commands
  arrive as queued calls, a timer in the thread paces the core, and it
  steps in bounded batches so commands are taken between them.
- Nothing editable is shared, so there are no locks around editor data. The
  shared things are the command queue (Qt's), the frame mailbox (a mutex
  around a pointer swap) and a bounded event queue.

### 2. The immutable input

`SimulationInput`, built on the main thread at start and at Reset, held as
`std::shared_ptr<const SimulationInput>`; copies, never pointers into editor
objects:

- **Track geometry** (`TrackGeometry`): for each vector node, its placed
  sections (section length, straight or curve radius and angle, tile,
  position and rotations) with cumulative lengths. `pose(node, distance)`
  finds the section by binary search and gives the world position and
  direction as `getDrawPositionOnTrNode` does (a test checks they agree).
- **Trains** (`TrainPlan`): an id; the editor's identity of the train (the
  player's, or the AI service name and start time); its vehicles (length,
  shape files, flip), length, maximum speed, rates, efficiencies (default
  and per stop); its start time; its route (§3); its stops (where the train
  stops, booked arrival and departure, platform name); its waiting points
  (where, value); its reversal points and path end.
- **Activity:** name, start time, the route's speed limit (`.trk`), a run id.

A train whose service, path or consist is missing is not made; a warning
event says why (as Check Activity does). The geometry of all vector nodes is
copied (a placed section is about 40 bytes); its build time is measured on
the largest local routes.

### 3. Routes and the train traveller

`TrainRoute` replaces, for the simulation, `Ruch`'s junction choices:

- **A route is the path's track in order:** intervals (vector node, from,
  to; the direction is the sign) with cumulative distances, split into
  subpaths at the reversal points. It is built from the path's legs when
  the input is made, so each pass of a junction keeps its own branch (the
  per-junction choice issue of task editor 07 does not arise) and the core
  needs no track topology in this task.
- **The lead-in:** the train first stands behind its path start, the first
  vehicle across it, as the editor places it. The track behind the start,
  for the train's length, is found when the input is made, walking back on
  the track database (at a junction met from its points, its main route);
  where the track ends first, the train starts advanced so its rear is 1 m
  from the end (the placement rule of task editor 07).
- **A train is its front's distance along its route and its length.** Each
  vehicle's two ends are at fixed offsets behind the front (as `Consist`
  places them now); `RouteCursor` turns a route distance into a node,
  distance and direction by binary search, and the geometry into a world
  pose.
- **Reversal:** at a subpath's end the train stops. The next subpath starts
  at the reversal point the other way; the train's old rear becomes its
  front, at its length along the next subpath (its body lies on that
  subpath's first metres). A reversal point nearer a junction than the
  train is long (so the body is not on the next subpath) is reported as a
  warning and the train goes on.
- `Ruch` stays for the editor's placement and the Play mode until those move
  onto these classes (a later task).

### 4. The driver

One kinematic driver per train, in the spirit of MSTS's:

- **States:** waiting for its start time; running (accelerating, cruising,
  braking); stopped at a station, at a waiting point, reversing; finished
  (AI trains leave the route at their path end; the player's train stays).
  The player's train is driven like the AI trains in this task.
- **Speed cap:** the lower of the route's speed limit and the consist's
  maximum speed × the efficiency of the current section (the activity's per
  stop, else the service's). Later tasks add speed posts and signals.
- **Stop targets:** the nearest of the next station stop, the next waiting
  point that stops the train, the next reversal point and the path end. The
  train brakes at constant deceleration to stand exactly at the target; a
  step never carries it past a target.
- **Station stops:** the train stands centred on the platform (Open Rails'
  rule). It stands at least its boarding time, Open Rails' rule: 10 s per
  passenger waiting at the platform (the activity's
  `PlatformNumPassengersWaiting`, else the platform item's) shared among
  its passenger cars at the platform, never under 20 s; a train without
  passenger cars 20 s. It leaves at the booked departure when that is
  later. A stop without booked times (arrival and departure 0, a fifth of
  the local stops) leaves after its boarding time.
- **Waiting points:** seconds, wait so long; until a time, wait until then;
  uncouple kinds, wait their seconds (no uncoupling yet); horn kinds do not
  stop the train (an event, for sounds later).
- **Rates:** Open Rails' AI rates (Reference), freight when the consist has
  no passenger car; constants in one place.
- **Start and pre-run:** AI trains appear at their start times. Those that
  started before the activity are run forward from their start times to the
  activity's start before the first frame; with no interaction, each alone.

### 5. Time

- **Simulation time** is seconds after midnight, starting at the activity's
  start time. It does not depend on frame rate.
- **Steps are at most 0.1 s:** each is the smaller of 0.1 s and the time the
  pace asks for since the host's last tick. In real time (Play) that is
  about 10 to 20 ms, so trains move smoothly and driving (later) answers at
  once; fast runs and high multipliers step 0.1 s. A real-time run is
  therefore not step for step the same as a fast run (they differ by
  fractions of a second); tests use the fixed 0.1 s step.
- **Pace:** real time (Play), or real time × a multiplier (2× … 128×, as
  MSAE), or as fast as possible for runs to a time and timetable
  calculations. When the core falls behind, it shows its actual time; it
  does not enlarge steps.
- **Pause** stops advancing and is acknowledged. **Reset** builds a new
  input from the current editor data (so edits apply) and starts at the
  activity's start. **Run to a time** resets and steps to it as fast as
  possible (MSAE's way back). **Stop** ends the simulation. A run goes on
  until Stop; when every train has finished, the panel says so.
- **The world's time of day is set to the activity's start time when a
  simulation starts** (the session's `core.rendering.timeOfDay.time`, as the
  environment panel sets it); after that the two clocks run on their own.
  No further link between the simulation and the world's time or weather
  in this task: that feature is being reworked on another branch, so this
  one keeps clear of it. Later, fast runs may set the weather's time and its
  timelapse speed, and runs as fast as possible may leave the world's
  simulation out.

### 6. Messages

- **Commands** (main → simulation), each with a sequence number: start
  (with an input), play, pause, speed, run to, reset, stop. The host answers
  each with the step it took effect at.
- **Frames** (simulation → main), the latest replacing earlier ones, at most
  one per host tick (about 60 a second, so real-time motion stays smooth):
  `SimulationFrame` (run id, step,
  simulation time; per train its state, speed, route distance, next target
  and, per vehicle, the world pose of its two ends). Immutable, handed over
  through the mailbox; the renderer draws the latest without waiting.
- **Events** (simulation → main), ordered and bounded: a train started,
  arrived, departed, waited, reversed, finished; warnings; acknowledgements.
  Each carries the train's id and its editor identity. The main thread
  delivers it to the editor objects it finds by that identity (the
  Timetable window's row, later world objects); one that has gone gets
  nothing.

Poses are computed in the simulation from its own geometry, so drawing never
needs the live track database: an edit, even a deleted track, cannot break a
running simulation's picture.

### 7. In the editor

- **Controls in the activity panel (F4):** the Play section moves below
  Activity Info and the Check and Save buttons, and holds the controls:
  Play (starts or resumes the simulation in real time; later it will also
  set the player's train to manual driving and switch the camera), Pause,
  Stop, Reset, the speed, the simulation time, Run to a time, and a status
  line (trains waiting, running and finished; warnings; that the run shows
  the activity as it was when started).
- **Drawing:** `SimulationPresenter` (main thread) keeps, per simulated
  train, its vehicles' shapes (copies of the consist's `Eng`s, as the editor
  makes them now) and places them from each frame (a pose-based draw next to
  `pushRenderItemOnTrack`). While a simulation runs, the trains standing at
  their starts are not drawn; the map's activity layer draws the frame's
  vehicles.
- **Timetable window:** a state per train from the frame (waiting until
  08:20, running 72 km/h, at Settle until 08:31, finished); **Follow**, the
  camera following the chosen train; the chosen train's events (arrived,
  departed, waited…) in its information column.
- **Activity editing is off while a simulation exists** (from start until
  Stop; Pause keeps it): the activity panel's settings, the Events,
  Services, Traffic and Timetable windows and the path tool show but do not
  edit; the activity chosen in the panel cannot change; undo refuses
  activity steps (they stay on the undo stack; world steps undo as usual).
  Saving is allowed. The simulation works on its copy, so this is not
  needed for its
  safety; it keeps what the windows show and what the simulation runs the
  same, and leaves no way to miss a link between the activity's objects and
  what the simulation drives (the drawn trains, later switches and
  signals). Route and world editing stay on: the simulation does not read
  them, and messages to a world object that has gone are not delivered.
  Reset or Stop and start again picks up edits made before.

### 8. Later tasks on this base

- The editor's placement and the Play mode move onto `TrainRoute` and the
  core; the Play mode becomes the simulation with the player's train under
  the user's control.
- Task 02 adds the track topology to the input, occupation and
  reservations, and a basic block policy between the driver and the route.
- Signals, activity events, speed posts and sounds follow (roadmap).

## Phases

1. **Input:** `TrackGeometry` (with the pose test), `TrainRoute` from paths
   (legs, lead-in, subpaths), `TrainPlan` from activity data; the core with
   trains moving at constant speed, tested synchronously.
2. **Driver:** speeds, braking to targets, stops, waiting points, reversals,
   path end, start times, pre-run.
3. **Thread and editor:** the host, commands, frames and events; the
   panel's controls; drawing in 3D and on the map; the Timetable window's
   states, Follow and events; the world's time set at start; activity
   editing off.
4. **Measured:** input build time and memory, step cost per train, on the
   largest local routes; the doc's progress.

## Verification

- **Core** (synchronous, deterministic):
  - poses agree with `getDrawPositionOnTrNode` (path-edit's test track and
    10,000 random points on each local route, within a centimetre);
  - routes: legs in order, each pass of a junction its own branch (the USA2
    path with a junction passed twice), the lead-in, the start advanced at a
    track end, the body's mapping at a reversal;
  - driver fixtures: a free run's time equals the formula; on-time and late
    station stops; each waiting point kind; a reversal; the path end; two
    trains meeting and passing; pre-run equals running from the start; a
    large step does not skip a stop; the same input gives the same trace.
- **Host:** start, pause, play, speed, run to, reset and stop acknowledged
  in order; frames latest-wins and events in order; stopping with work
  queued ends the thread; editing the live track and activity during a run
  changes nothing in it; events for a deleted service are dropped quietly.
- **Editor:** the panel's controls and the Timetable window's states in an
  `activity-ui`-style test, activity edits, activity choice and activity
  undo refused while a simulation exists and allowed after Stop; a capture
  of simulated trains at a set simulation time in 3D and on the map.
- **Content:** every local activity run to its end as fast as possible: no
  crash, trains started and finished counted.
- **Measured:** input build time and memory on the largest local routes;
  simulation seconds per wall second with 100 trains.

## Decisions (user, 2026-10-15)

1. **The player's train** runs its timetable like an AI train.
2. **The controls** are in the activity panel (F4), in the Play section,
   which moves below Activity Info and the Check and Save buttons. Play
   runs the simulation in real time; later it will also set the player's
   train to manual driving and switch the camera. The Timetable window has
   the trains' states, Follow and the chosen train's events.
3. **Steps:** at most 0.1 s, smaller in real time so motion is smooth and
   driving answers at once; 0.1 s in fast runs and timetable calculations.
4. **Dwell at stops:** Open Rails' boarding time (§4), not a fixed time
   (the question was how long a train stands at a stop with no booked
   times, a fifth of the local stops).
5. **Activity editing is off while a simulation exists,** the simulation
   working on its copy all the same (§7): the activity choice is locked and
   undo refuses activity steps; route and world editing stay on.
6. **A run goes on until Stop.**
7. **The world's time is set to the activity's start time at start,**
   then both clocks go their own way; no weather or time sync in this task
   (reworked on another branch). Later: fast runs may set the weather's
   time and timelapse speed; runs as fast as possible may leave the world's
   simulation out.
8. **Follow:** the camera can follow the train chosen in the Timetable
   window.
9. Work on `feature/simulation` in `../TSRE5vc-simulation`.
