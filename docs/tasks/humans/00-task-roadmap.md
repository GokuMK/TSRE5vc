# Humans Task Roadmap

Animated people in TSRE. Two related sets of tasks:

- **People TSRE moves** (this folder, first): passengers and other people at
  stations, waiting, boarding and leaving trains.
- **People the player moves** (later, its own set): a walking player, with
  inverse kinematics, controls, camera and collisions. It reuses everything
  the first set builds.

Status 2026-11-12: design, the roadmap's questions answered (the walk-area
guards accepted). Nothing built; task 01 starts on the user's go.

## The user's goal (2026-11-11)
The first goal: people appear on a platform; when a train stops they board
it, and some people get off. Each part starts simple and grows later:

| First | Later |
|---|---|
| People appear on the platform | They appear at hidden places and walk to the platform |
| They walk to a door and vanish | Paths and seats inside trains |
| They stand or walk on the platform | Seats on the platform |
| Random numbers, random trains | Preferences, passengers counted on trains; TSRE's "passengers waiting" figure |

Before all of it: making humans, drawing them and playing their animations.

## The user's decisions
2026-11-11, first message:
- Tools: **MPFB + Blender + glTF + ozz-animation + TSRE's renderer**.
- Animation clips made by us or downloaded free.
- Large crowds: levels of detail and impostors are required. **500 humans
  drawn at once** must be possible.
- First step: the workflow for making humans, the tools installed, an
  example human, loaded and animated (the Shape Viewer is enough).
- A task folder of its own (this one).

2026-11-11, answers:
- **ozz from the start.**
- **Recast and Detour** for walk areas, from the start.
- **TSRE's rule for libraries**: no external libraries, only source
  projects, in `thirdparty/` and built by TSRE's CMake. ozz and
  recastnavigation come in as sources.
- **Humans belong to the route** and are shipped by its designer, on the
  designer's own rules for quality and licensing:
  - human shapes and their textures in `SHAPES` and `TEXTURES`, like all
    other route content,
  - the route's human configuration in the route's main folder.

  No `HUMANS` folder, unless the configuration turns out large (it does not,
  see "Where humans live"). TSRE's own humans go into TSRE's assets, with
  the default route assets: a separate task prepares those (task assets 03,
  the template route).
- **One shared clips file** for many humans.
- **The generator** is a separate project beside the others (`humans-gen`
  beside `tree-gen` and `assets-gen`). Assets are made on this machine.
- **Impostors**: baked at run time or made beforehand, by what each costs
  (estimate below).
- **Rigid glTF animations** (moving nodes without skins) in task 02 too.
- **No Steam Deck work** for these tasks. 2 ms for 500 people would cost
  quality; a quality setting is expected.

2026-11-11, second answers:
- **Which humans a route gets** is a player setting: *Always* (the route's
  own humans, else TSRE's default humans), *Route defined* (only routes
  that ship humans), *Never*.
- **`humans.dat`** as proposed; the draft is reviewed when it exists.
- **Impostors baked at load.**
- **No crowd density setting**: more people as impostors rather than fewer
  people. A density factor only as a last resort.

2026-11-12:
- **The walk-area guards** ("Where people may walk") accepted.
- **The people setting's default is Always.**
- **Example humans**: Polish or European average passengers; first a man,
  a woman and a young person, to see clips on different heights.
- **Impostor directions** (8 or 16) follow the quality setting.

## What TSRE already has
- **glTF**: TSRE's own reader (`src/tsre/shape/GltfShape.cpp`, Qt's JSON),
  PBR materials, levels from `MSFT_lod` (task shapes 10), impostors for
  trees (`src/tsre/shape/Impostor.h`, task world vegetation-objects).
  **No skins and no glTF animations**: task shapes 02 left them out, and the
  reader does not look at `skins` or `animations` at all.
- **Rigid animation**: `ShapeAnimation` (MSTS keys, slerp and lerp) and the
  pose API of `ComplexShape` (`setPartFrame`, `setPartTransform`,
  `partMatrix`) per shape state (task shapes 12).
- **Platforms**: a platform is two track items on its track (begin and end,
  `TRitem::platformTrItemData`), with a world `PlatformObj` holding its sides
  (left, right), station and platform names. `RouteDisplays::nearestPlatform`
  (`src/tsre/ui/UiRouteDisplays.cpp:458`) already samples platform lines
  along their track and finds the nearest one for a display.
- **Passengers waiting**: per platform item (`PlatformNumPassengersWaiting`,
  editable in the platform's properties), overridden by an activity's
  `PlatformData`; `passengersWaiting()` (`src/tsre/sim/SimulationInput.cpp:57`)
  already turns it into activity trains' boarding times. Also the platform's
  least waiting time.
- **Station stops and doors**: `SimulationController::stationDoors` opens an
  AI train's doors 4 s after arriving and closes them 10 s before it leaves
  (Open Rails' rule), on the platform's side (`platformSide`), on each car
  (`applyDoors`). A driven train's doors come from its driver.
- **Where doors are on a car**: `CarAnimation` finds door parts by Open
  Rails' names (`DOOR_A`..`DOOR_C` one side, `DOOR_D`..`DOOR_F` the other,
  `src/tsre/trains/CarAnimation.cpp:132`) and MaSzyna's doors from the
  `.mmd`. A door part's position gives where people get in.
- **Train states** for displays (task scripting 02): approaching (within
  500 m), standing, its next stop, departure times.
- **Route lists in the route's main folder**: `carspawn.dat` lists car
  shapes, and Open Rails' named `CarSpawnerList`s let a car spawner pick its
  list by name (`CarSpawnerObj::LoadCarSpawnerList`,
  `PropertiesCarspawner`). The humans' configuration follows that pattern.
- **Geometry on the CPU**: shapes do not keep their triangles after upload;
  the cab picker reads a shape's data again for its triangles
  (`CabPicker::load`, `src/tsre/trains/CabPicker.cpp:64`), and
  `Intersections::rayTriangle` exists.
- **Per-instance data in a texture**: the QRhi renderer reads terrain patch
  records from a data texture; joint matrices can go the same way in both
  renderers.
- **Block compression at run time**: `TerrainMaterialMap::encodeBC1`, used
  for procedural terrain materials. Baked impostor atlases can reuse it.
- **Workers**: QThreadPool, never std::thread (MinGW rule).

## Why ozz from the start
IK is not why one would pick ozz. It is a small extra working on ozz's
output. What a crowd needs from its first day is ozz's main part:

- a skeleton,
- sampling clips at a time,
- blending two or three clips (idle into walk, walk into stop),
- joint matrices from the local poses (`LocalToModelJob`), which become the
  skinning matrices.

Using ozz from the start means that layer is written once. TSRE's own glTF
reader fills ozz's raw skeletons and animations, and ozz's builders make
the run-time data at load. There are no `.ozz` files and no `gltf2ozz`
step, so glTF stays the only content format. ozz is MIT licensed, plain
C++ without dependencies (0.16.0 is the latest release found; it adds root
motion extraction).

## Where people may walk
No link to a platform shape. Instead, guards on the geometry. Unity and
Unreal work the same way: each object says whether and how it counts for
navigation, with defaults, and designers override odd objects.

**1. Every kind of geometry has a role.**

| Role | Means | Default for |
|---|---|---|
| Floor | People may stand on it, if the rules below allow | Static shapes (`Static`, `CollideObject`) |
| Solid | Blocks people, never stood on (Recast's "null area": it takes space and headroom, its top is never walkable) | Signals, speed posts, gantries, level crossings, pickups, hazards |
| Ground | Never walkable, except at a platform without a floor (rule 6) | Terrain, transfers |
| Ignored | Not in the walk area at all | Track sections, dynamic track, forests, wires, trains |

A shape changes its role in its `.sd`, written once and true wherever the
shape is placed: `ESD_TSREWalk ( Floor | Solid | Ignore )`, or scene extras
`TSRE_walk` in glTF (as `TSRE_distanceLevelExtension`). One placed object
changes it with an attribute in the world file (as `TSRERepaint`).

**2. Recast's own rules apply to the floor.** At most 40° of slope, 2 m of
headroom, 0.25 m to step up, and a person's radius of 0.3 m.

- A bench's seat, about 0.45 m high, is too high a step, so its top is
  never reached.
- Under the seat there is no headroom, so the bench becomes a hole in the
  floor.
- The same holds for tables, kiosks, shelter roofs and low walls.
- Kerbs and stairs stay walkable.

**3. Only floor connected to the platform is kept.** Seeds are taken every
2 m along the platform line, 1 m in from its edge, with a ray down. A seed
lands on the highest floor hit in the platform's height window (0.1 m below
the top of the rail to 1.3 m above it), so a shelter roof above is skipped.
The walk area keeps only the floor reached from the seeds (a flood fill over
Detour's polygons). Bench tops, roofs and stray flat tops are dropped even
when their role is Floor.

**4. Track is closed.** Within 1.45 m of any track's centre line from the
track database, from 1 m below the rail to 1.5 m above it, nothing is
walkable. Ballast and the space between the rails never are; a footbridge
higher up is not affected.

**5. An edge zone along the platform edge** (about 0.8 m wide) is walkable
at a high cost. People wait behind it and cross it only to board or get
off.

**6. A platform without a floor gets a ground strip.** Rural halts often
have no platform shape: a gravel transfer, or terrain only. Where a seed's
ray first meets Ground, that stretch of the platform walks on the ground in
a strip:

- from 1.7 m off the track's centre line,
- 2.5 m wide by default,
- as long as the stretch.

A platform partly built and partly bare works stretch by stretch. Ground
stays closed everywhere else, so people never walk off into fields.

**7. A station's area is built once.** It covers a corridor on the platform
side of each of its platform lines, from 1.5 m to about 15 m off the track.
Island platforms join by themselves: both lines' corridors find the same
floor. Later tasks widen the area to entrances and underpasses (task 07).

**8. Settings per platform**, later, as attributes of the world's
`PlatformObj`: no people here, the list of humans to use, the ground
strip's width.

**9. Designers see it.** An editor overlay shows floor, solid, ground,
track, the edge zone and the seeds. The editor's error list says when a
platform found no floor (ground strip used) or no walk area at all.

Floor wins where terrain pokes a few centimetres through a platform: Recast
merges spans that close together and keeps the higher area number, and
Floor gets the highest.

Rejected: telling platform shapes apart by their names (`peron`,
`bahnsteig`), which breaks on any other naming. And classifying shapes by
their geometry alone, which the seeds and the flood fill already do without
a class.

## Where humans live
A route's own humans, from its designer:

- the human shapes (`.glb`) in the route's `SHAPES`, their textures in its
  `TEXTURES` (or inside the `.glb`), by the same rules as other glTF route
  shapes,
- a shared clips file (a `.glb` with a skeleton and animations, no mesh) in
  `SHAPES` too,
- one configuration file in the route's main folder, a text file in the
  style of `carspawn.dat` that TSRE's parser reads. Proposed name
  `humans.dat`, with named lists as Open Rails' `CarSpawnerList`:

```
HumanList (
	Name ( "Passengers" )
	Animations ( "people_clips.glb" )
	Human ( "woman_casual_01.glb" 2 )
	Human ( "man_coat_01.glb" 1 )
)
```

The number after a human is how often it is picked. Later: colours per
recolour region, a height range, kinds (passenger, staff). That is a few
lines per human, so one file is enough and no `HUMANS` folder is needed.
Platforms use the first list; a platform may name another one later, as a
car spawner names its list.

TSRE's own humans go into TSRE's assets with the default route assets (task
assets 03). Made from CC0 inputs only. The player setting chooses (the
user, 2026-11-11):

| Setting | A route with `humans.dat` | A route without |
|---|---|---|
| Always (default) | its own humans | TSRE's default humans |
| Route defined | its own humans | no people |
| Never | no people | no people |

## Impostors: baked at run time or made beforehand
Estimate, measured in task 03:

| | Baked by TSRE at load | Made beforehand |
|---|---|---|
| Time | Per human file, 72 small renders (8 directions × 1 idle and 8 walk frames) of a few thousand triangles: a few milliseconds on a real GPU. Then a BC1 encode with `encodeBC1`, a few milliseconds of CPU. One file per frame, so there are no hitches. | None at load |
| Memory | The same either way. 64×128 px cells, 9×8 cells, about 576×1024 px per map, 2 maps (colour, normals and mask): 4.7 MB raw or about 0.6-1.2 MB in BC1/BC3 per human file. 32×64 cells: a quarter of that. | The same |
| Who makes them | TSRE, for any human, including a designer's own | Our generator. A designer's own humans need our tools, or get none. |
| Files | None | Extra images (inside the `.glb`, else beside it) |
| When the renderer changes (lighting, tints) | Nothing to redo | Every shipped human made again |

**Proposal: baked at run time.** It costs milliseconds per human file and no
more memory, and it works for every route's humans. Made beforehand
(images inside the `.glb`) only if task 03 measures a real problem.

## Quality
A player setting for human quality: tier distances, how many people get
full meshes, impostor directions, shadows. Lower quality draws more people
as impostors; it never draws fewer people (the user, 2026-11-11). A density
factor stays a last resort, not planned. Settings follow the settings rules
(cached, never read each frame). The default aims at a good look, not at a
few milliseconds.

CPU times are measured on this machine (Release builds). GPU times only on
the user's hardware when the user runs them; llvmpipe's are no guide.

## Tasks
- [ ] [01 - Making humans](01-making-humans.md): the workflow from MPFB in
  Blender to a glTF file, in `humans-gen`. Tools installed on this
  machine, one skeleton for everyone, levels, one material, clips from free
  sources retargeted to it, a shared clips file, and example humans (a man,
  a woman, a young person: Polish average passengers) with idle and walk
  clips.
- [ ] [02 - Skinned glTF in TSRE](02-skinned-gltf.md): skins and
  animations read from glTF (rigid node animations too), ozz in
  `thirdparty/`, skinning in both renderers, the Shape Viewer playing and
  blending clips. Checked on Khronos' samples and the example humans.
- [ ] [03 - Crowds drawn at scale](03-crowd-drawing.md): many humans from a
  few files. Levels of detail for meshes and animation updates, shared
  poses, impostors baked by TSRE, variation by tint and size, the quality
  setting, a 500-human test scene.
- [ ] 04 - Platform walk areas (brief below)
- [ ] 05 - People on platforms (brief below)
- [ ] 06 - Boarding and getting off (brief below)

### 04 - Platform walk areas (brief)
- recastnavigation (Recast, Detour, DetourCrowd) in `thirdparty/` as
  sources, zlib licence.
- The rules of "Where people may walk": roles and their `.sd`, glTF and
  world overrides; seeds and flood fill; track and edge zone marks; ground
  strips.
- Per station: the platform lines of one station name (the displays'
  sampling, moved to a shared place), the triangles of the shapes and
  terrain in the corridors (read on a worker as the cab picker reads its
  shape, cached per shape file while needed), and the Recast build. Built
  when the station's tiles are loaded and the simulation runs; freed with
  them.
- The editor overlay and the error list messages.
- Checked on stations of the stock MSTS routes and of PRUDNIK, including a
  halt without a platform shape. Build time per station measured.

### 05 - People on platforms (brief)
- A view-side system, like the displays and doors: it reads simulation
  frames and never writes back. The first version changes nothing in the
  simulation.
- Which humans: the route's `humans.dat` list, else TSRE's default humans,
  as the player setting says (Always, Route defined, Never).
- How many: the platform's passengers waiting (activity, else track
  database).
- Where: random points on the platform's walk area, behind the edge zone,
  spread along the whole platform. They idle, and sometimes walk a little.
- Random numbers seeded from the platform and the run's day, so captures
  repeat.

### 06 - Boarding and getting off (brief)
- When a stopping train approaches (the displays' "approaching" state),
  waiting people come closer to the edge zone.
- Doors on the platform side, from door parts (`CarAnimation`); without
  door parts, a door near each car end.
- From doors opening (`stationDoors`, 4 s after the stop) to closing
  (10 s before leaving): people get off first and walk away along the
  platform, vanishing out of view. Then waiting people walk to the nearest
  door and vanish in the doorway. Who boards which train is random for now.

## Later
- **07 Hidden sources**: people appear where nobody sees them (entrances,
  underpasses, platform ends) and walk to the platform. Entrances declared
  in buildings' `.sd`, the walk area grown beyond the platform corridors.
- **08 Seats and waiting places**: benches' seats in their `.sd`, shelters;
  sitting clips.
- **09 Inside trains**: paths and seats in coach shapes (`.sd`), people
  seen through windows, counts per car.
- **10 Preferences and counts**: which trains people take (category,
  destination from the timetable rows), passengers carried and getting
  off, people arriving between trains, boarding time back into the
  simulation.
- **Player-controlled humans**: their own task set. IK (feet on stairs,
  hands on handles), controls, camera, collisions with the walk area.

## Questions
None open.
