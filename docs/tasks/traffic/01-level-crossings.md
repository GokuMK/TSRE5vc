# Task 01 - Level crossings, and cars that wait

## Goal
While a simulation runs:
- the simulation closes a level crossing when a train comes and opens it
  when the train has passed,
- the crossing object plays its shape's closing and opening animation and
  its sounds,
- car spawner cars keep in a line, stop before a closed crossing, and go on
  when it opens; a spawner whose road is full spawns no car.

The car spawner gets only what waiting needs. Road networks are task 02.

Status 2026-11-12: designed, the user's answers at the end ("Decisions");
built on `feature/traffic` (worktree `../TSRE5vc-traffic`), not merged;
what differs from the design is in Progress.

## Today
Details in the roadmap ("What TSRE has today"). In short: the simulation
does not read track `LevelCrItem`s. `LevelCrObj` is a plain shape with no
state, animation or sound. Spawner cars run at one speed with fixed timing
and drive through each other. All of it runs only on tiles near the camera,
with the view's frame time.

## Open Rails' rules
### When a crossing closes (`Orts.Simulation/Simulation/LevelCrossing.cs`)
Open Rails checks each train against each track crossing item of a loaded
crossing (`UpdateCrossings`, :92-301). For one train and one item:

- `warning` = the object's `LevelCrParameters` first value (warning time,
  s); `minimum` = its second (minimum distance, m).
- Reach: `warning × |v| + minimum + 1`; or, as a second chance, plus
  `warning × (vAllowed − |v|) / 2` if the train may still speed up (:117-121).
- `frontDist`: track distance from the train's front to the item ahead,
  negative once passed; `rearDist = −frontDist − length` (:172-184).
- The item counts as reserved for the train only within its reserved track
  length (`ReservedTrackLengthM`). A train held at a red signal does not
  close a crossing beyond the signal.

The train holds the crossing closed when (:251-298):

| Train | Holds it closed while |
|---|---|
| Standing (\|v\| ≤ 0.1 m/s), crossing ahead within reach and the reserved length, rear not more than `minimum` past | `frontDist ≤ minimum`; standing further back opens it (Open Rails' "timeout"). A train standing on the crossing at the start keeps it closed. |
| Forward, 0 < v ≤ 5 m/s | `frontDist ≤ minimum + 65` |
| Forward, v > 5 m/s | `frontDist ≤ reach`, until `rearDist > minimum` |
| Backward, \|v\| ≤ 5 m/s | rear approaching: `rearDist ≤ minimum + 65`; opens once the front is 9.5 m clear |
| Backward, \|v\| > 5 m/s | `rearDist ≤ reach`, until `frontDist > minimum` |

- Standing (static) consists: closed while a standing car is within
  `minimum − 13.5` m of the item (`minimum` ≥ 20), else `minimum − 6.5`
  (:203-249). This is for sidings beside crossings.
- A crossing (the object's group of items) is closed while any of its items
  has a train (`HasTrain`, :452).
- Crossings exist only while their shapes are loaded (`Shapes.cs:855`).
- AI trains sound the crossing horn pattern here, if the activity asks
  (`ORTSAIHornAtCrossings`, :186).

### The animation and sound (`RunActivity/Viewer3D/Shapes.cs:811-917`)
- `LevelCrTiming`'s third value (`animTiming`, default 4):
  - **> 0, one-way:** the key runs from 0 to `rate/30` frames at
    `rate/30/animTiming` frames a second, forward to close and backward to
    open. MSTS crossing shapes have keys at frames 0 and 1 at 30 fps: one
    move takes `animTiming` seconds.
  - **< 0, looped** (flashing lights, bells): frame 0 while open; while
    closed it loops through all frames at `rate/30/|animTiming|`.
- Every animated part goes to the same key.
- Sound file: `ORTSSoundFileName`, then the shape's `.sd`
  `ESD_SoundFileName`, then the TRK's `DefaultCrossingSMS`. The crossing is
  silent when invisible (`LevelCrData` bit 1) or when `(LevelCrData & 6) ==
  6` (`WorldFile.cs:890-891`). Closing fires MSTS trigger 3, opening 4
  (`Events.cs:612`). PRUDNIK's `crossing.sms` starts a bell loop on 3 and
  releases it on 4.
- An invisible crossing still closes, and cars still stop at it.

### The cars (`RunActivity/Viewer3D/RoadCars.cs`)
- Each car's speed is `CarAvSpeed × (0.75 … 1.25)` (:239), its length
  `carspawn.dat`'s value (`distanceFrom`).
- The next car is due `CarFrequency × (0.75 … 1.25)` s after the last
  (:106). It appears only once the last car has moved its own length (:99).
- A car stops 10 m before the first item of a closed crossing (:34, :261),
  unless it is within one second of it at its speed (:251). It keeps behind
  the car in front. The speed is set from a sine of the free gap (:283), not
  by acceleration.

## Design
### 1. Level crossings in the simulation
**Data.** `TrackNetwork` reads the track TDB's `LevelCrItem`s on vector
nodes, as it reads heads and speed posts:
- a `LevelCrossing { item, node, distance }` list,
- `levelCrossingsOnNode` sorted by distance,
- `levelCrossingOfItem(item)`.

(The name `crossings` is taken: those are rail diamonds.) `Projection`
gets `levelCrossings` (`ProjectedItem`: index and route distance), filled as
`speedPosts` is.

**Parameters.** Each crossing has a warning time and a minimum distance:
30 s and 20 m unless told otherwise. These are TSRE's and MSAE's defaults,
and 271 of PRUDNIK's 277 objects use them. The world files are not read by
the simulation, and only tiles near the camera are loaded. A loaded
`LevelCrObj` whose values differ sends them by a host command,
`setLevelCrossingParameters(trackItems, warning, minimum)`. It sends again
when its values are edited. The core keeps the values in a small mutable table, not
in the immutable input. So every crossing on the route operates in the
simulation, with its own values wherever its object has been loaded
(question 1).

**The rule.** Each step, for each train on the track, the core looks at the
train's projected crossings around it. This is a cursor over a sorted list,
from the rear minus the largest `minimum` to the front plus the reach. The
core decides which of them the train holds closed, by Open Rails' table
above (question 2):
- "Reserved length" is the train's authority end, as a route distance.
- "May still speed up" uses the train's allowed speed now.
- A driven train uses the same rule.
- Standing trains are checked when they change (task 07's standing
  trains), not every step.

Each train keeps the short list of items it holds. A step compares the new
list with the old and changes per-item counts, so a step costs a few
compares per train. A crossing is closed while its count is above zero.

**Out.** The frame gets `levelCrossings`, a
`shared_ptr<const std::vector<unsigned char>>` by crossing index (1 =
closed), shared with the frame before while unchanged, and
`levelCrossingSerial`, as `aspects`/`drawSerial`.

`SimulationController` implements a `LevelCrossingStates` source, as it
implements `SignalLights::StateSource`:
- `closed(trackItem)` and `serial()`,
- read from the pinned view frame, so barriers and drawn trains agree,
- set when a simulation is made and cleared when it goes. Without a
  simulation every crossing is open.

**Why a state and not events.** Frames replace each other, and the event
queue may drop events (`SimulationHost::takeEvents`, `dropped`). An object
loaded after a train closed its crossing must still see it closed. Closing
and opening are the object's own transitions, seen on the main thread.

**Log.** `TSRE_CROSSING_LOG=1`: one line per change: crossing item,
closed/opened, the train, its distance and speed, the time. The route suite
prints them as it prints `TSRE_SIM_EVENTS`.

### 2. The crossing object
- `LevelCrObj` asks the state source: the crossing is closed while any of
  its track items (`TrItemId ( 0 … )`) is closed. It checks only when the
  serial changes.
- It gets its own shape state (`newState`, as `StaticObj`), released when
  it unloads. Today it draws with the shared state 0, so poses would be
  shared between objects.
- `LevelCrObj::updateSim(dt)` (it has none today) moves the animation key by
  Open Rails' rule above. It sets every animated part with
  `ComplexShape::setPartFrame(state, part, key)`. Frames and rate come from
  `animationFrames()`/`animationRate()`. (Built: MSTS shapes only; glTF
  shapes have no part posing yet, see Progress.)
- A transition reverses the key from where it is (a second train while
  opening).
- **First sight** (the object loads, or a simulation starts or stops): the
  object jumps to that state's pose without animating. If it is closed, the
  bell starts (trigger 3). (Built: when it loads only, Progress 5.)
- Invisible objects stay silent and still count for cars (section 4).
  Open Rails does not draw them; TSRE's editor draws them as before.
- A shape without animation stays as it is; cars still stop.

### 3. The crossing's sound
- The `.sms` is found by Open Rails' order: `ORTSSoundFileName`, the shape's
  `.sd` `ESD_SoundFileName` (a new field in `ShapeMetadata`), then the TRK's
  `DefaultCrossingSMS`. `SoundSystem::findSms` looks in the route's `SOUND`
  and then the content root's.
- Silent by Open Rails' flags (invisible, or `LevelCrData & 6 == 6`).
- One `SoundSource` (`Kind::World`) at the object's place, made and
  dropped as `SoundSourceObj::syncSound` does.
- Closing: `event(3)`; opening: `event(4)`.

### 4. Cars that wait
Only the car spawner's motion changes. Its lane (the begin item's road
vector node between its two items), its car list and its drawing stay.

**The lane's logic as its own class.** A small class without TDB or `Game`
holds one lane's cars and stop points and steps them. It is tested without
content and used again in task 02. A suggested name is
`src/tsre/traffic/LaneTraffic`.

**Stop points.**
- The road `LevelCrItem`s on the lane between the spawner's items, sorted
  in the driving direction.
- Each one's crossing object comes from a main-thread index: road item to
  `LevelCrObj`. The object registers itself on load and leaves on unload or
  delete.
- A stop point is closed while its object is closed. If its object is not
  loaded, it is open.
- After the first item of an object, the object's other items are skipped,
  so cars stop before the first rail (Open Rails).
- Cars stop 10 m before the item (Open Rails).

**A car.**
- Its place along the lane (its middle) and its speed.
- Its desired speed: `CarAvSpeed × (0.75 … 1.25)`.
- Its length from `carspawn.dat` (`CarSpawnerList::val`, read today and
  unused).
- Its shape, as today.

**Following.** The Intelligent Driver Model (IDM) (question 5):
- The acceleration comes from the desired speed, the gap to what is ahead
  (the rear of the car in front, or a closed stop point) and the closing
  speed.
- Parameters: maximum acceleration 1.5 m/s², comfortable braking 2.5 m/s²,
  minimum gap 2 m, headway 1.2 s.
- It is the usual car-following model (traffic simulators such as SUMO
  offer it). It costs about a dozen operations per car. Queues form and
  clear smoothly, and cars accelerate away when a crossing opens. Open
  Rails' sine rule sets the speed directly and jumps.
- A car never moves past what is ahead of it (the step clamps it). Long
  frames step in pieces of at most 0.1 s.

**A closed crossing seen too late.** A car that would need to brake harder
than 4 m/s² to stop goes through, and so does a car already past the stop
point (Open Rails: within one second).

**Order.** Cars stay in spawn order and are stepped front to back, so each
car follows the car in front after that car has moved.

**Spawning.**
- A car is due `CarFrequency × (0.75 … 1.25)` s after the last.
- It appears only when the last car's rear is its length plus the minimum
  gap past the begin item. Until then it waits; at most one car waits
  (question 4). A full road spawns nothing.
- Cars end at the end item, as today.

**Time** (question 3). While a simulation exists, cars and crossing
animations step by the simulated seconds since the last frame: stopped when
paused, four times as fast at 4×, and held during runs as fast as possible
(the world clock is held then). Without a simulation, as in the editor
today, they step by the frame's time.

**Not in this task:**
- overtaking,
- cars of two spawners on one lane seeing each other,
- roads crossing roads,
- cars rising over the rails (Open Rails' ramps, `RoadCars.cs:125`).

### 5. Checks
- **Suite `level-crossings`** (no content; a made-up network, as
  `simulation-core`): each row of Open Rails' table; a train held at a red
  signal before a crossing; two trains on a double line (closed until both
  have cleared); a standing consist beside a crossing; parameters changed
  by command; serial and sharing.
- **Suite `road-cars`** (no content, the lane class):
  - no two cars ever overlap,
  - a queue forms before a closed stop point and clears after it opens,
  - a car too close to stop passes,
  - a due car waits while the begin is occupied,
  - desired speeds stay within 0.75 to 1.25 of the average.
- **Route check** on USA2 (180 road crossing items, 175 spawners), JAPAN1
  and PRUDNIK:
  - Play an activity with `TSRE_CROSSING_LOG=1`.
  - Take a capture at a crossing as a train approaches: barriers down, cars
    queued (`renderer-capture` with `simulateTo` and a view there).
  - `TSRE_SOUND_LOG=1` shows the crossing's triggers 3 and 4.
- **Cost:** `simulation-route` (Release) on PRUDNIK's day and the seven MSTS
  routes: step time before and after, events unchanged.

## Steps
1. Core: level crossings in `TrackNetwork` and `Projection`; the rule,
   counts and command; the frame field and state source; the log; suite
   `level-crossings`.
2. Object: `LevelCrObj` state, own shape state, animation, sound
   (`ESD_SoundFileName` read into `ShapeMetadata`); the road item index;
   parameters sent on load and edit.
3. Cars: the lane class with IDM, stop points, spawning; `CarSpawnerObj`
   using it; the time base; suite `road-cars`.
4. Route checks, captures, cost; this doc's progress.

## Acceptance
- A train at line speed closes its crossing about the warning time before
  it arrives. The crossing opens once the train's rear is the minimum
  distance past.
- The barriers move by the shape's animation both ways, in the object's
  `animTiming`. Looped shapes loop while closed.
- On routes with a crossing `.sms`, the bell rings while the crossing is
  closed.
- Cars stop before a closed crossing in a queue without overlapping, and
  drive on when it opens.
- A spawner whose lane is full spawns nothing until there is room.
- The step cost on PRUDNIK's day grows by no more than a few percent.

## Questions for the user
1. **Which crossings operate.** Recommended: all of them, with the default
   30 s / 20 m, plus each loaded object's own values. The alternative is
   only loaded crossings, as in Open Rails. Then a crossing loading after a
   train has arrived starts open, and Open Rails' rule closes it on the
   next step.
2. **The closing rule.** Recommended: Open Rails' table as it is, including
   the slow-train 65 m and the backward cases. Its comments say it mimics
   MSTS. The simpler alternative: close at the warning time before arrival,
   open when the rear is the minimum distance clear.
3. **Time.** Recommended: cars and animations run in simulated time while a
   simulation exists. The alternative is the view's real time, as today.
4. **A due car while the begin is occupied.** Recommended: it waits for room
   (Open Rails). The alternative is to drop it and draw the next interval.
5. **Following.** Recommended: IDM. The alternative is Open Rails' sine rule.
6. **The map.** Closed crossings drawn red while a simulation runs: now,
   later, or not at all?
7. **AI horns at crossings.** Open Rails' `ORTSAIHornAtCrossings`, which
   TSRE's activity already reads: here (small once the core knows the
   crossings) or later?

## Decisions (user, 2026-11-12)
1. All crossings operate, with the 30 s / 20 m defaults and each loaded
   object's own values.
2. Open Rails' table, if it all fits TSRE's core. Where it does not fit,
   this doc says how it differs (Progress).
3. Cars and animations in simulated time while a simulation exists.
4. "Choose the more practical": a due car waits for room. That is the
   simplest: the timer keeps running, and the car appears on the first
   frame with room; dropping it would need its own reset.
5. to 7. Not answered. Taken: IDM (as recommended); closed crossings on the
   map and AI horns at crossings later, not in this task.

## Progress
Built 2026-11-12 on `feature/traffic` (worktree `../TSRE5vc-traffic`),
steps 1 to 4. Not merged.

**What is in the code:**
- **The core** (`src/tsre/sim/SimulationCrossings.cpp`): `TrackNetwork`
  reads track `LevelCrItem`s; `Projection::levelCrossings` lists them
  along each route part, and up to 100 m past its ends
  (`TrackNetwork::CrossingBeyond`). Each train holds crossings by Open
  Rails' table; the frame carries `levelCrossings` and
  `levelCrossingSerial`. `SimulationHost::setCrossings` takes the loaded
  objects' values and keeps them for a run started again.
- **The main thread** (`src/tsre/traffic/LevelCrossings`): the state
  source (`SimulationController`), the loaded crossing objects (road item
  to object), and the seconds cars and crossings move in a frame
  (`SimulationController::trafficSeconds`: the frame's time at the run's
  speed, none while paused, at most 1 s while running as fast as
  possible).
- **The crossing object** (`LevelCrObj`): its own shape state, Open Rails'
  animation rule on every part, sound triggers 3 and 4, the `.sd`'s
  `ESD_SoundFileName` (read once per shape through `ShapeMetadata`).
- **Cars** (`src/tsre/traffic/LaneTraffic`, `CarSpawnerObj`): IDM, stop
  points at the lane's crossings (the first item of each object), a due
  car waiting for room, car lengths from `carspawn.dat`, desired speeds
  0.75 to 1.25 of the spawner's, spawn intervals 0.75 to 1.25 of its
  frequency. The lane is made again when an item moves (edits, undo).
- **Captures**: `"traffic"` now also runs the world's own simulation during
  a simulation's `runFor` (`RendererParityTestSuite`).

**Where it differs from the design or from Open Rails (decision 2: "if
all fits"):**
1. **A crossing a train holds stays closed while the train runs on
   towards it.** Open Rails lets it go when the train's allowed speed
   drops (its reach shrinks with it) and closes it again further on. On
   USA2 that bounced crossings 12 and 14: closed 855 m out at 22.6 m/s,
   opened, closed again at 682 m. It still opens when the train stands
   short of it (the timeout) or its reserved track ends before it.
2. **The crossings are looked at every half second of simulated time**
   (`SimulationCore::CrossingEvery`), not every step; Open Rails does each
   frame. Half a second is at most 20 m at 40 m/s on a reach of hundreds
   of metres. Every step cost PRUDNIK's day 8.0 → 9.4 µs a 0.1 s step
   (+17 %, mostly walking 296 trains' states). With the half second and a
   per-train skip (a train with no crossing within its longest reach is
   looked at again once it has travelled the distance), Release,
   `simulation-route`, four runs each: main 7.5 to 8.8 µs, the branch 8.1
   to 8.7 µs (about +4 %). USA2's busiest activity (9 trains): 0.8 to
   0.9 µs on main, 0.9 to 1.0 µs on the branch.
3. **Crossings just past a route's ends count** (100 m along the same
   node). A route part ends exactly at the path's end, so a train standing
   at its end 10 m short of a crossing did not see it; Open Rails looks
   along the track.
4. **"Reserved track"** is the train's authority end on its route part.
5. **First sight** happens when the object first updates (it loads).
   When a simulation starts or stops, the barriers move (and ring) as for
   any change, rather than jumping.
6. **glTF crossing shapes do not animate**: `GltfShape` has no part
   posing (MSTS shapes, both backends, do).
7. **Cars brake evenly for a stop line** once standing there takes 90 % of
   their comfortable braking (2.5 m/s²). IDM alone braked up to
   5.4 m/s² at the end of the stop.
8. **AI trains finish at their path's end** (TSRE's rule), so an AI
   train's path ending short of a crossing never holds it; the player's
   train standing at its end does.

**Checks:**
- Suites: `level-crossings` 18, `road-cars` 13 (new); `simulation-core`,
  `-host`, `-ui`, `-events`, `-coupling`, `-work`, `timetable-read`,
  `-trains`, `-commands`, `-commands-more`, `driving`, `steam`, `heating`,
  `sound`, `shape-animation`, `car-animation`, `map-view`,
  `signal-selection`, `token-world`, `settings`, `activity-ui`,
  `timetable-ui` pass; `ctest` 11/11.
- `simulation-route` (`TSRE_CROSSING_LOG=1`): USA2's activities 4,272
  closings; PRUDNIK's day 12,658. A USA2 freight at 24.2 m/s closes
  crossings 352/354 879 m out (36 s ahead) and opens them when its rear,
  about 800 m behind, is 20 m past. PRUDNIK's three route failures are the
  same on main (content).
- Capture `tests/renderer/level-crossing-usa2.json` (itr_e1_s1_w1_t1,
  crossing of items 8 and 10, two spawner lanes): open before, closed
  with cars queuing on both lanes, the train on it with six and five cars
  waiting, open with the queues leaving; close views of the arms down and
  up (`JP1CarCrossing.s`: 2 frames at 30 fps, timing 4 s).
- USA2's crossing objects are flagged invisible (`LevelCrData` 1): Open
  Rails draws them not and plays no sound for them. TSRE's editor draws
  invisible crossings (as before), so they animate; they stay silent.
- Sound, PRUDNIK (scratch capture, `TSRE_SOUND_DEVICE=none`): the crossing
  of items 4386/4387 (`rogatki.s`, two spawner lanes) found
  `SOUND/crossing.sms` through the TRK's `DefaultCrossingSMS`; closing at
  09:53:27.6 raised the streams playing from 2 to 6 (the bell and the
  barrier's stream of each of its two objects). The arms down, a truck and
  two buses waiting, in the pictures.
- Spawners near the camera: a spawner's lane node is looked for only when
  its items move (`TDB::findTrItemNodeId` walks every road node).

## Later
- Closed crossings on the map while a simulation runs.
- AI trains sounding the horn at crossings (`ORTSAIHornAtCrossings`).
- Cars rise over the rails at crossings (Open Rails' ramps).
- Bells and lights for some seconds before the barriers lower. MSTS and
  Open Rails have one closing event.
- Crossings tied to signals (signals protecting a crossing stay at stop
  until it is closed). This needs route data.
- Cars as hazards (MSTS `CrashProbability`, which Open Rails does not use).
- Lanes shared between spawners, roads crossing roads: task 02.
