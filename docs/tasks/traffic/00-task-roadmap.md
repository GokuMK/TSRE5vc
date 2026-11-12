# Road Traffic Task Roadmap

Road vehicles in TSRE: level crossings that close for trains, cars that wait
at them, and later cars that drive through a road network.

Status 2026-11-12: design. Nothing built; each task starts on the user's go.

## The user's goal (2026-11-12)
- Trains are simulated, but level crossings do not close when a train comes,
  and cars do not wait. The simulation should close and open the crossings;
  car spawner cars should wait; crossing shapes play their open and close
  animations; crossings make their sounds. **Task 01.**
- TSRE's car spawner cannot do this today: cars do not keep in a line. Fix
  it so cars queue, and a spawner whose road is full spawns no car. In task
  01 only as much traffic work as makes cars wait.
- A proposal for road traffic beyond spawner lines: another spawner kind
  whose car drives through the road network, chooses turns and ends at
  another spawner. RDB junctions or junctions computed from where roads
  cross? How do games do it? Flexible but cheap; traffic lights later.
  **Task 02, a design with options.**

## What TSRE has today
- **Car spawners** (`src/tsre/world/objects/CarSpawnerObj.cpp`): cars are
  spawned on the main thread by `Tile::updateSim`, on tiles near the camera,
  with the view's frame time (not the simulation's clock). A car runs along
  the begin item's road vector node at the spawner's one speed. A new car
  comes every 0.5 to `CarFrequency` + 0.5 s whatever is in front of it. Cars
  do not see each other or crossings: nothing keeps one behind another.
  The car length in `carspawn.dat` (second value of `CarSpawnerItem`) is read
  into `CarSpawnerList::val` and not used.
- **Level crossings** (`LevelCrObj`): editing only. The object places one
  track item and one road item where its road lane crosses each track
  within 30 m (`initTrItems`). It reads and writes `LevelCrParameters`
  (Open Rails: warning time, minimum distance), `LevelCrTiming` (the third
  value is the animation's length), `LevelCrData` (invisible and silent
  flags), `CrashProbability` and `ORTSSoundFileName`. It is drawn as a plain
  shape: no animation, no sound, no state.
- **The simulation** (`src/tsre/sim`) knows diamond crossings
  (`TrackSection::Crossing`, rail over rail), not level crossings. Track
  `LevelCrItem`s are not read. Each frame carries signal aspects as a shared
  vector with a serial. Signal objects pull their draw states through
  `SignalLights::StateSource`, which `SimulationController` implements.
  Level crossings can follow the same pattern.
- **Shape animation** (task shapes 12): `ComplexShape::animationFrames`,
  `animationRate` and `setPartFrame` pose MSTS and glTF parts at any frame.
- **Sound** (tasks sound 01 and 02): world `SoundSource`s (`Kind::World`,
  as `SoundSourceObj`) and discrete events (`SoundSource::event`). The TRK's
  `DefaultCrossingSMS` is read (`Trk::defaultCrossingSMS`). `.sd`'s
  `ESD_SoundFileName` is not read yet.

## What the content has (measured 2026-11-12)
From each route's `.rdb` (road database). Lane crossings and T-junctions are
measured on section chords, so they are close counts, not exact ones.

| Route | Road lanes (vector nodes) | Lane km | Junction nodes | Lane ends touching another lane end | Lanes crossing at grade | Lane ends beside another lane (T) | Road crossing items | Spawners |
|---|---|---|---|---|---|---|---|---|
| EUROPE1 | 142 | 59 | 0 | 0 | 8 | 5 | 10 | 39 |
| EUROPE2 | 83 | 135 | 0 | 0 | 25 | 17 | 4 | 42 |
| JAPAN1 | 471 | 197 | 0 | 0 | 310 | 178 | 532 | 132 |
| JAPAN2 | 277 | 277 | 0 | 0 | 322 | 194 | 103 | 91 |
| USA1 | 686 | 457 | 0 | 0 | 365 | 266 | 0 | 84 |
| USA2 | 495 | 517 | 0 | 0 | 674 | 294 | 180 | 175 |
| PRUDNIK (OR_PSK_NEW) | 2,814 | 1,473 | 0 | 278 | 1,833 | 1,981 | 521 | 170 |

- **No route has a single road junction node**: MSTS never let anyone place
  them (no road junction shapes). The RDB is a TDB and supports junction
  nodes; they need new road junction shape definitions (the user,
  2026-11-12). Every road lane is a vector node between two end nodes. Every road is two parallel lanes (all but 23 of
  PRUDNIK's 2,814 lanes have a partner 1.5 to 6 m beside them). Roads meet
  only geometrically: they cross (median angle 90°), or one road's lanes end
  a few metres beside another's.
- PRUDNIK: 277 level crossing objects, 271 with the default
  `LevelCrParameters ( 30 20 )`, all `LevelCrTiming ( 60 60 4 )`. They have
  1 to 5 track items each, one road item per track item. They use 14
  shapes. No object names a sound (`ORTSSoundFileName`), and no crossing
  shape's `.sd` has `ESD_SoundFileName`; the same holds on USA2. Both TRKs
  give `DefaultCrossingSMS ( crossing.sms )`. PRUDNIK's starts a bell loop
  on discrete trigger 3 and releases it on 4.

## Open Rails, the reference
- `Orts.Simulation/Simulation/LevelCrossing.cs`: crossings close per train
  by distance and speed (details in task 01). A crossing's group exists only
  while its shape is loaded (`Shapes.cs:855`), so crossings far from the
  camera never close.
- `RunActivity/Viewer3D/Shapes.cs:811-917` (`LevelCrossingShape`): the
  animation (looped or one-way by the sign of the animation time), and sound
  events `CrossingClosing`/`CrossingOpening` (MSTS triggers 3 and 4,
  `Events.cs:612`).
- `RunActivity/Viewer3D/RoadCars.cs`: spawner cars on one lane; each keeps
  behind the car in front and stops 10 m before a closed crossing. A spawner
  spawns only when the last car has moved its own length. Cars rise over the
  rails at crossings. No network, no junctions ("TODO: Move to simulator!").

## Execution Order
- [ ] `01-level-crossings.md`: the simulation closes and opens level
  crossings (Open Rails' rules); crossing objects animate and sound; spawner
  cars keep in a line, wait at closed crossings, and a full spawner spawns
  nothing. Designed 2026-11-12, answers the same day; being built on
  `feature/traffic`.
- [ ] `02-road-network-traffic.md`: a proposal with options: a lane graph
  built from the RDB at load, cars that turn and drive from spawner to
  spawner, intersections and their rules, traffic lights later. Design only.

## Decisions (user)
- 2026-11-12: task 01's answers are in its doc; build task 01 on a feature
  branch in its own worktree (`feature/traffic`, `../TSRE5vc-traffic`).
  Task 02 stays in design.
- 2026-11-12: RDB junctions are possible in principle, given new road
  junction shape definitions; MSTS never allowed placing them (task 02).

## Related
- `docs/tasks/simulation/00-task-roadmap.md` (the simulation thread, frames,
  states pulled by world objects).
- `docs/tasks/shapes/12-shape-and-rolling-stock-animation.md` (part poses).
- `docs/tasks/sound/01-sound-system.md` (world sound sources, triggers).
- `docs/tasks/humans/00-task-roadmap.md` (people at platforms: the same
  question of cheap agents near the camera).
