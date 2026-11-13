# Humans Task Roadmap

Animated people in TSRE. Two related sets of tasks:

- **People TSRE moves** (this folder, first): passengers and other people at
  stations, waiting, boarding and leaving trains.
- **People the player moves** (later, its own set): a walking player, with
  inverse kinematics, controls, camera and collisions. It reuses everything
  the first set builds.

Status 2026-11-12: task 01 done (`../humans-gen`), tasks 02 to 06 built on
`feature/humans` (not merged; 03 waits for the user's look and tuning; 05
and 06 for the user's tests here and on the user's hardware). Then, by the
results, tasks 02-05 revisited and their open items, and on through 07-10
(the user, 2026-11-12).

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
- **Waiting is per train** (on task 05's brief): each train stopping at a
  platform brings its own waiting people; two trains due together, twice
  the people.
- **Boarding** (on task 06's brief): no vanishing in the doorway; people
  walk in until the track's middle. The train's floor is taken at the
  door's height (not always true, enough for this step).
- **"Do 5 and 6"**: a complete working feature to test here and on the
  user's hardware; then revisit 02-05 and what is open, and improve through
  07-10.

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
(As built, task 04: the window reaches 2.0 m and the lowest walkable floor
is taken; seeds find the platform's edge first. See 04-walk-areas.md.)
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
	Human ( Name ( "woman_casual_01.glb" ) Weight ( 2 ) )
	Human ( Name ( "man_coat_01.glb" ) )
)
```

A human's `Weight` is how often it is picked (1 when not given); a block,
as it grows (as built in task 05). Later: colours per
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
- [x] [01 - Making humans](01-making-humans.md) (done 2026-11-12 in `../humans-gen`;
  recolouring left to task 03): the workflow from MPFB in
  Blender to a glTF file, in `humans-gen`. Tools installed on this
  machine, one skeleton for everyone, levels, one material, clips from free
  sources retargeted to it, a shared clips file, and example humans (a man,
  a woman, a young person: Polish average passengers) with idle and walk
  clips.
- [ ] [02 - Skinned glTF in TSRE](02-skinned-gltf.md): skins and
  animations read from glTF (rigid node animations too), ozz in
  `thirdparty/`, skinning in both renderers, the Shape Viewer playing and
  blending clips. Checked on Khronos' samples and the example humans.
  Built 2026-11-12 on `feature/humans` (not merged): suite `gltf-skin`,
  captures in OpenGL and QRhi Vulkan, about 7 µs a character a frame on
  one core (3.4 ms for 500).
- [ ] [03 - Crowds drawn at scale](03-crowd-drawing.md): many humans from a
  few files. Levels of detail for meshes and animation updates, shared
  poses, impostors baked by TSRE, variation by tint and size, the quality
  setting, a 500-human test scene. Built 2026-11-12 on `feature/humans`
  (tints later): 500 people 0.3-0.9 ms of CPU a frame by quality, impostors
  baked on the CPU in about a second a human, 3 MB each.
- [ ] [04 - Platform walk areas](04-walk-areas.md) (brief below): built
  2026-11-12 on `feature/humans`, checked on every station of EUROPE1 and
  USA1, 23 of JAPAN1's and 7 of PRUDNIK's (0.03-1.6 s a station on a
  worker); the rules as real stations taught them are in its doc.
- [ ] [05 - People on platforms](05-people-on-platforms.md) (brief below):
  built 2026-11-12 on `feature/humans`: people per train stop (the
  platform's passengers waiting), coming where unseen, waiting and walking
  about on the walk areas (Detour's crowd), `humans.dat` and the setting
  `core.content.people`; where TSRE's default humans ship is open.
- [ ] [06 - Boarding and getting off](06-boarding.md) (brief below): built
  2026-11-12 with 05: doors from door parts (else carriage ends), people
  off first, then on, walking in to the car's middle at the threshold's
  height; checked with an EN57 at Raszowa and the local service at
  Carlisle.

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

## Next (the user, 2026-11-12)
Order: MaSzyna cars' doors first (done, 06-boarding.md), then 09 (inside
trains: scan the cars, upgrade the best), then 08 (seats and waiting places:
benches are reused across stations, so a mass scan like the lamps'), and 07
last (hidden sources need each station's assets adjusted).

For the final `humans.dat` (the user's notes, later):
- **Seasons**: a human's temperature range, so winter brings no summer
  clothes and summer no coats: `Human ( Name ( "man_coat.glb" ) Weight ( 2 )
  Temperature ( -25 12 ) )`; a person picks among the humans whose range
  holds the air's temperature as it is made (the simulation's outside air:
  the route's weather, else the climate at its latitude); none given, any
  weather. Needs seasonal humans from humans-gen.
- **Regions**: TSRE's users are in North America, Europe and India, with
  different mixes of people and clothes. How much TSRE provides and how much
  the route's designer: to be decided; the design to allow it: TSRE's packs
  per region (assets, downloaded), a route's humans.dat naming one
  (`Pack ( "india" )`) with its own weights or humans added, room for kinds
  (passengers, staff), height ranges and colours per region of the body.

## Measuring on the user's hardware
CPU times are this machine's; GPU times and the look only the user's
hardware's (llvmpipe's are no guide). What the numbers decide:
- **Presets**: drop Low and shift the rest down (the user's note in
  [03](03-crowd-drawing.md)), or keep the four.
- **People a station holds**: 250 now (`MostAtStation`); 1000 wanted, with
  Detour agents for walkers only and the people's step on a worker
  ([06](06-boarding.md), Later), if the frame has room for them.
- **Mesh reach and impostors** (`Crowd::Tuning`): if the middle tier costs
  too much, or impostors show too close.

Release builds from main. Humans: a run takes the route's `humans.dat`,
else `assets/humans` (the full pack: humans-gen's `packs/full` copied
there), else appdata's basic set; the log says which (`People: 3 humans
from ...`). Measure with the full pack and note the one used.

### 1. The test scene: 500 people
The Shape Viewer's crowd ([03](03-crowd-drawing.md)): 500 people of the
three humans in 50 rows, every tier in one view (`crowd-500`,
`crowd-500-far`, `impostors-close`, `meshes-close`):

```
TSRE_HUMANS_SHAPES=<TSRE>/assets/humans/SHAPES TSRE5vc --game-root <a game root> \
    --test --test-suite=shape-viewer-capture --test-cases=tests/renderer/humans-crowd-views.json \
    --test-label=q2-vulkan --set core.rendering.humanQuality=2 \
    --set core.rendering.backend=qrhi --set core.rendering.rhiApi=vulkan
```

At `core.rendering.humanQuality` 0, 1, 2 and 3, with QRhi Vulkan, QRhi
OpenGL (`core.rendering.rhiApi=opengl`) and the OpenGL renderer
(`core.rendering.backend=opengl`). 03's table has this machine's CPU times
for the same scene.

### 2. A station on a route
`tests/renderer/humans-route-views.json`: OR_PSK_NEW's PRUDNIK, timetable
PSK_2026, Raszowa from 12:10:30, the people of 1156_R46421 (an EN57: 20
waiting, boarding through its door parts), 1600x900, the sky at noon,
editor lines hidden:

| View | What it shows |
|---|---|
| `raszowa-waiting` | Still, from the platform 10 m out |
| `raszowa-above` | Still, from 27 m above the platform |
| `raszowa-boarding` | An EN57 door from 6 m, the run stepped 0.5 s a frame: people getting off, then on |
| `raszowa-live` | From the same place, the run played 110 s in real time, a paint a frame: the last people getting on, the doors closing, the train leaving, those off walking away |

```
TSRE_PEOPLE_LOG=1 TSRE_FRAME_LOG=<label>.csv TSRE5vc --game-root <OR_PSK_NEW> --route PRUDNIK \
    --test --test-suite=renderer-capture --test-cases=tests/renderer/humans-route-views.json \
    --test-label=<label> --set core.rendering.humanQuality=2 \
    --set core.rendering.backend=qrhi --set core.rendering.rhiApi=vulkan
```

On Windows, `scripts/hardware/Capture-Views.ps1 -GameRoot <OR_PSK_NEW>
-Route PRUDNIK -Label <label> -Cases tests/renderer/humans-route-views.json
-Set core.rendering.humanQuality=2,...`, with `$env:TSRE_PEOPLE_LOG = '1'`
and `$env:TSRE_FRAME_LOG = '<file>'` set first (the started TSRE inherits
them).

The runs:
- `humanQuality` 0 to 3 with QRhi Vulkan; the default (2) also with QRhi
  OpenGL and the OpenGL renderer.
- The baseline: the same with `core.content.people=2` (Never). The people's
  cost is the difference.
- A crowd: `TSRE_PASSENGERS_SCALE=10` (here 200 people at Raszowa, at High
  40 near, 156 middle, 4 impostors from `raszowa-waiting`), at High and at
  the lowest level kept.

What to note for each run:
- `capture.json`, each view's `stats`: `medianFrameMs`, `minFrameMs`,
  `minCpuMs`. A grab waits for the GPU, so a frame there is the CPU's time
  plus the GPU's.
- The frame log, `raszowa-live`'s lines (`sim_s` running): `interval_ms`
  and `gpu_ms` (QRhi timestamps, -1 without), median and 90th percentile,
  against the baseline's.
- The people log (`People at ...` lines, as the figures change): people by
  state, the people's step (`ms`), the crowd's update and push (`crowd a +
  b ms`), the tiers, impostors drawn, impostors baked and their bake time.
- The GPU and its driver. `TSRE_GPU_PASS_TIMES=1` (Vulkan) adds each pass's
  GPU time.

The results and the decisions go in this section.

### Results (2026-11-13)
The user's Steam Deck OLED (Windows 10, AMD Custom GPU 0932, driver
24.10.02.03), Release build of 4b539bd0, the full pack (`People: 3 humans
from assets/humans/humans.dat`), `QT_ENABLE_HIGHDPI_SCALING=0` (captures at
their own size, not 2.5 times on the 4K screen), the user's profile
otherwise (Ultra clouds, shadows). GPU times are `TSRE_GPU_PASS_TIMES=1`'s
Vulkan timestamps: the frame log's `gpu_ms` is -1 in captures (a grab is an
offscreen frame). A second High run on Vulkan agreed within 1 ms on every
view.

**1. Shape Viewer, 500 people (1280x720).** `shape-viewer-capture` logs no
frame times: GPU from the pass times, the median of the 2 s windows inside
an item (a copy of `humans-crowd-views.json` with `minFrames` 120 and a
warm-up item first). QRhi Vulkan, GPU ms a frame, and the tiers (near /
middle / far):

| | Low | Medium | High | Ultra |
|---|---|---|---|---|
| `crowd-500` | 26.8 | 31.5 | 39.7 | 52.2 |
| `crowd-500-above` | 19.9 | 27.4 | 35.0 | 51.1 |
| `crowd-500-far` | 12.4 | 16.2 | 21.0 | 27.8 |
| `impostors-close` (30) | 1.0 | 1.4 | 1.8 | 1.4 |
| `meshes-close` (30) | 6.6 | 6.4 | 6.6 | 7.0 |
| `crowd-500` tiers | 10 / 346 / 144 | 20 / 469 / 11 | 40 / 460 / 0 | 80 / 420 / 0 |
| `crowd-500-far` tiers | 10 / 128 / 362 | 20 / 172 / 308 | 40 / 238 / 222 | 80 / 338 / 82 |

`crowd-500`'s whole frame (an item's time over its frames less the 20 ms
pacing; a grab and its readback, about 16 ms, included): QRhi Vulkan 45 /
52 / 59 / 71 ms, the OpenGL renderer 63 / 99 / 108 / 133 ms. Update + push
on the CPU 0.16 + 0.75 (Low) to 0.48 + 0.83 ms (Ultra).

QRhi OpenGL not measured: broken and set aside (the user, 2026-11-13);
"OpenGL" in these runs is the OpenGL renderer.

**2. Raszowa (1600x900).** Median frame ms (`capture.json`), the people's
cost against `core.content.people=2` in brackets; ×10 is
`TSRE_PASSENGERS_SCALE=10` (200 waiting at 12:10:30):

| Run | `raszowa-waiting` | `raszowa-above` | `raszowa-boarding` |
|---|---|---|---|
| QRhi Vulkan, people off | 72.8 | 63.9 | 79.3 |
| Low | 76.5 (+3.7) | 63.9 (0) | 83.0 (+3.7) |
| Medium | 76.7 (+3.9) | 65.7 (+1.8) | 83.2 (+3.9) |
| High | 78.0 (+5.2) | 68.8 (+4.9) | 83.6 (+4.3) |
| Ultra | 76.5 (+3.7) | 68.9 (+5.0) | 84.7 (+5.4) |
| ×10 Low | 87.3 (+14.5) | 66.3 (+2.4) | 101.2 (+21.9) |
| ×10 Medium | 90.5 (+17.7) | 72.6 (+8.7) | 104.0 (+24.7) |
| ×10 High | 107.0 (+34.2) | 90.3 (+26.4) | 123.3 (+44.0) |
| OpenGL renderer, people off | 44.5 | 38.0 | 39.1 |
| OpenGL renderer, High | 48.7 (+4.2) | 43.9 (+5.9) | 45.4 (+6.3) |

Where the GPU time goes, `raszowa-waiting` on Vulkan (ms; span: the frame
from the first pass's start to the last's end; shadows: the sun's three):

| | Span | View | Shadows | Environment |
|---|---|---|---|---|
| People off | 44.4 | 17.9 | 5.9 | 3.7 |
| High (20 people) | 49.9 | 19.2 | 9.5 | 4.5 |
| ×10 Low | 58.1 | 25.8 | 8.7 | 7.3 |
| ×10 Medium | 61.6 | 25.4 | 10.3 | 9.5 |
| ×10 High | 78.6 | 24.9 | 28.4 | 9.6 |

- The middle people's shadows (High, Ultra) are most of a crowd's cost:
  200 people add 22.5 ms to the shadow passes at High, 4.4 at Medium.
- People are drawn into the environment map: 3.6 to 5.9 ms for 200
  (medians, which hide the differences between the levels: 4 below).
- The main view: about 7 ms for 200 people at every level.
- The boarding view at ×10 (the run stepped 0.5 s a frame): 8.5 ms more
  CPU (`minCpuMs` 21.9 against 13.4); the people's step 9.6-10.4 ms while
  170 walk, 1-3 ms after.
- The first frame with a station's people: 140-175 ms (365 at ×10 High),
  in the crowd's update; the OpenGL renderer's first push 383 ms. A hitch
  as people appear.
- `raszowa-live` at the default scale: 7 people are left at its start,
  none from 12:12:00, so its frame is the baseline's (interval median 71.5
  against 71.2 ms). At ×10 High: median 80.8, 90th percentile 104.0
  (baseline 71.2 / 76.2), GPU span 52.2 against 44.7.

**3. People's shadows from a coarser level** (the user: try it). The full
pack's levels are 15.5k, 3.1k and 0.8k triangles (man; the woman's alike,
the young 10.3k, 2.1k, 0.5k). `Crowd::push` sets only the view's level
(`setCurrentDistanceLevel`), so a person casts the level it is drawn with.
Task renderer 37's shadow stand-in (`setShadowDistanceLevel`,
`GltfShape::pushRenderItem` submitting the coarser level shadow-only, posed
per state) takes skinned shapes, but `setShadowDistanceLevel` keeps only
levels under the shape's `impostorLevel`, which only an impostor file
beside the shape sets: the crowd's impostors are baked into the crowd, so
for people it keeps none. Measured with a temporary build (not committed):
that limit the mesh levels when a shape has no impostor of its own, and
`Crowd::push` casting the next coarser level (`next`) or the last one
(`last`). `raszowa-waiting` on Vulkan (ms; shadows: the sun's three passes'
GPU):

| | Shadows | GPU span | Frame |
|---|---|---|---|
| People off | 5.9 | 44.4 | 72.8 |
| High | 9.5 | 49.9 | 78.0 |
| High, next | 6.7 | 47.6 | 76.8 |
| ×10 Medium | 10.3 | 61.6 | 90.5 |
| ×10 Medium, next | 7.4 | 60.0 | 90.1 |
| ×10 Medium, last | 6.2 | 58.7 | 88.9 |
| ×10 High (the same build without it) | 28.0 | 77.7 | 106.6 |
| ×10 High, next | 11.2 | 63.8 | 94.4 |
| ×10 High, last | 7.4 | 61.6 | 93.4 |

- 200 people's shadows at High: 22.1 ms, 5.3 with `next`, 1.5 with
  `last`. High with `next` is 4 ms over Medium as it is.
- `raszowa-live` at ×10 High: interval median 81.8, 79.2, 78.7 ms; 90th
  percentile 103.2, 93.5, 91.8; GPU span 51.8, 47.1, 46.7 (people off
  44.7).
- The cost: about 1 ms more CPU at ×10 High with `next` (a second posed
  packet set a person: `minCpuMs` 14.1 against 15.6), and the view and
  environment passes 1 ms more each (not explained).
- The look: the shadows at 10-20 m are the same in the three (the shadow
  map's texels decide there). Self-shadowing up close not checked.

Kept with the last level (the user, 2026-11-13). `setShadowDistanceLevel`
takes any mesh level of a shape without an impostor of its own (`ShapeLod`
still sets none for such shapes: they have no shadow floor), and
`Crowd::push` casts each person's last mesh level, the view's own for those
drawn at it (`ShadowLevelsBeforeLast` 0 in `Crowd.cpp`). The level before
the last (`ShadowLevelsBeforeLast` 1) once the environment map costs less.
Checks: `gltf-lod` +1 (a shape without an impostor casts a level set, none
past its last; never a tree's impostor), `humans-crowd` +1 (the man at 5 m
casts the last level shadow-only, drawn at the last level his own).

**4. The environment map by level** (the user: why do Medium and High cost
the cube the same?). They do not. The pass times above are medians of each
frame's times, and the cube draws two faces a frame in turn (a whole cube
every three frames), so a frame's environment time is one pair of faces'
and the median is the middle pair's. The mean over the frames is the
cube's cost. A temporary build (not committed) logged the means beside the
medians and counted the draws into the faces; `raszowa-waiting` on Vulkan,
one session, Medium and High run twice (ms, a frame):

| | Environment, median | Environment, mean | People's triangles in the faces | People's draws in the faces |
|---|---|---|---|---|
| People off | 3.8 | 3.1 | 0 | 0 |
| ×10 Low | 7.4 | 5.8 | 334k | 48 |
| ×10 Medium | 10.0, 10.3 | 7.4, 7.5 | 537k | 58 |
| ×10 High | 10.5, 10.8 | 8.3, 8.5 | 685k | 71 |
| ×10 Ultra | 10.7 | 9.4 | 841k | 73 |

- The people's share of the cube follows their triangles: 7.5 to 8 ms a
  million at every level (Low +2.7 ms, Medium +4.4, High +5.3, Ultra +6.3
  over people off).
- The faces draw people at the levels the view chose: 9.6k triangles a
  person at High (7k at Low, 11.5k at Ultra), for a person 15 to 30 px tall
  at 10 to 20 m in a 256 px face. At their last level (0.8k) the 73 people
  of Ultra would be about 57k triangles, some 0.4 ms.
- Each person is a draw of its own there (draws and instances nearly
  equal).
- The main view sees the same 28 people at every level, all at level 0
  (392k triangles a frame): why the view cost about 7 ms at every level
  (2 above).

What the numbers suggest (the decisions are the user's):
- **Presets**: Low and Medium are 3-4 ms apart at 200 people; High doubled
  the cost by the middle people's shadows, which the last level now takes
  away (3 above). Dropping Low costs little.
- **People a station holds**: 200 people take 17 ms of GPU here at High and
  14 at Medium with shadows from the last level (34 and 17 before). 1000 is
  beyond this GPU without cheaper drawing (environment faces from a coarser
  level, more impostors).
- **Mesh reach and impostors**: 30 people near the camera cost 6.6 ms as
  meshes, 1-1.8 ms as impostors.
- **Environment map**: environment faces draw the view's packets, no level
  of their own (only `core.rendering.environmentMap.objectDistance` and the
  one-texel limit): 2.7 to 6.3 ms a frame for 200 people (means: Low 2.7,
  Medium 4.4, High 5.3, Ultra 6.3; 4 above), as many triangles as the view
  chose; the shadows make no difference. From the last level it would be
  under half a millisecond. A coarser level there is not done yet (a queue
  flag beside `setShadowOnly`). A stopgap discussed (2026-11-13): the faces draw an
  object's shadow stand-in in place of its view packets (flags on both, the
  stand-ins routed to their passes, both renderers). Its limits: only
  casters have a stand-in (not middle people at Low and Medium, impostors,
  or anyone with shadows off), parts that cast none would be missing, and
  trees would change too. Once the faces cost less, people may cast the
  level before the last (3 above).

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
