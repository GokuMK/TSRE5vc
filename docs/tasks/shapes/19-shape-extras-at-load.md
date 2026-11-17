# Task 19 - Shape extras at load

Status: design (2026-11-17, the user's idea), questions answered the same
day; parked for later (the user: "note that is not lost and leave it for
later"). Building waits for the user's go.

## The user's idea (2026-11-17)
- What other systems need of a shape's geometry on the CPU is asked for
  before the shape loads and made right after its load, before it drops
  its CPU geometry: no second read of the file.
- Whether the legacy S reader drawing the shapes makes that a problem.
- Where it came from: a train's inside drawn in the 2D view with its
  occupied seats, as the seats finder's plans draw a car (task humans 11:
  cushions, backrests, seats, doors).

## How it works now (main 4059855e)
The drawn shape:
- `.s` files are drawn by `SFileLegacy`; `SFileComplex` only with
  `TSRE_MSTS_SHAPE_BACKEND=complex` (or `complex-compact`) and in the Shape
  Viewer (`ShapeLib.cpp`, `addShape`). `.e3d` by `E3dShape`, glTF by
  `GltfShape`.
- A shape loads the first time it is drawn, on a loader worker (task 08):
  `SFileLegacy::loadData()` parses, `loadSd()` reads the `.sd` (its own
  ParserX loop), `initGL()` builds each sub-object's vertices (a part's
  points in its own space) and hands them to the renderer
  (`Meshes::update`), then frees the parsed points, normals, UVs and index
  lists. Kept: matrices, hierarchy, parts' ranges, bounds and materials.
  `adopt()` swaps the worker's copy in on the main thread.

Read again, each once a file on a worker, each with its own cache:

| Who | Needs | Reads | When |
|---|---|---|---|
| `Humans::CarDoors` (humans 06, 11) | door parts' triangles at rest | the `.s` parsed by `SFileComplex`; an `.e3d` read fresh (the shared copy gives its vertices to the renderer) | a car people use whose `.sd` has no Door lines |
| `ShapeSeats::readFor` (humans 09) | the `.sd`'s `ESD_TSRESeats` | the `.sd` parsed again (`SFileDetail::Document`); a glTF's JSON | a car people use |
| `Humans::ShapeTriangles` (humans 04) | every triangle at rest, the most detailed level; `ESD_TSREWalk` | `SFileComplex`; `GltfShape::readTriangles`; `.e3d` none | station objects for walk areas |
| `CabPicker` (driving 08) | the controls' parts: triangles and UVs | `SFileComplex`, kept | a 3D cab when driven |

What that costs:
- Each file parsed twice: parse time and memory on a worker (not per
  frame). `SFileComplex::loadData()` parses every level for one.
- Two parsers for one file: `SFileLegacy` draws, `SFileComplex` gives the
  geometry. They must agree on part numbers, names, hierarchy and rest
  matrices. The cab picker takes a control part's number from
  `SFileComplex` and asks the drawn shape for that part's matrix
  (`Eng.cpp`, `posedCab->partMatrix(..., p.part, ...)`); CarDoors names
  its door parts by `SFileComplex`'s part names while `CarAnimation` opens
  the drawn shape's. They agree on the content tested (station-people's
  EN57 doors, the 3D cabs), nothing checks it for every file.
- Four caches, four ways to forget them.

## Proposal

### 1. Extras asked for, made at load
- An asker states what it needs of a shape before it loads: flags
  `Doors`, `Plan`, `Triangles` (full, at rest), `PartTriangles` (per part,
  with UVs: the cab picker).
- The backend makes them on the loading thread from the data it has
  parsed, before freeing it: `SFileLegacy::initGL()` (every part's vertices
  and index list are in hand there), `E3dShape` before its vertices go to
  the renderer, `GltfShape` (its `readTriangles` walk), `SFileComplex` for
  the complex backend. The most detailed level, parts placed at rest (as
  the readers above do).
- They are kept with the shape: `std::shared_ptr<const ShapeExtras>
  ComplexShape::extras() const`, null until loaded or when not asked.
  `adopt()` takes the copy's, `reload()` makes them again.
- `ShapeExtras`:
  - door parts' triangles, split into doors as `CarDoors::splitSides` does
    (a MaSzyna car's by its `.mmd`'s door prefix and count, as now);
  - the plan: the cushions' faces and the backrest band's (task humans
    11's plan: upward faces at seat height, upright ones 0.6-1.2 m over
    the floor), in 2D along and across the car, a few thousand triangles;
  - every triangle at rest (walk areas), or per part with UVs (the cab
    picker);
  - the `.sd`'s seats: `SFileLegacy::loadSd()` keeps `ESD_TSRESeats`
    (`ShapeMetadata` already holds them for `E3dShape` and the complex
    backend), ending `ShapeSeats::readFor`'s second parse.

### 2. How an asker asks
- Load options are part of `ShapeLib`'s cache key (`addShape`: path and
  options): a file asked for with extras and without would load twice.
  Cars could pass one more option (`Eng::shapeOptions`, every car's
  request), but walk areas need the triangles of station objects the route
  loads without them.
- So a request on the shape, not an option: `ComplexShape::wantExtras(flags)`
  before its load, the askers' flags merged. Asked once loaded: the shape
  reads its CPU data again for them alone (as today, once).

### 3. Shapes load when drawn
- People use only cars drawn lately (StationPeople checks `lastDrawn`):
  their shapes are loaded, extras ready.
- Walk areas may be built before a station's objects are first drawn, and
  the map would show trains never seen: their shapes are not loaded.
  Decided (below): nothing until a shape is loaded; the map asks the
  loader for the shapes of the cars it shows.

### 4. The legacy reader
- Not a problem for the hook, a reason for it: the reader that draws the
  shape makes its extras from the same data, its own part numbers, so the
  cab picker's lookup and CarDoors' names match the drawn shape by
  construction, and no second parser is left in the game.
- The work in `SFileLegacy`: a part's vertices are in its own space; its
  matrix comes through its primitive state's vertex state; rest matrices
  through the hierarchy (`partRest`). The most detailed level only.
- The complex backend gets the same hook (Shape Viewer, the opt-in
  backend); glTF and `.e3d` theirs.

### 5. Who moves to it
- `CarDoors`: the extras' doors; its own read gone (nothing until the
  shape is loaded).
- `ShapeSeats`: the shape's metadata.
- Walk areas (`ShapeTriangles`): the extras' triangles.
- The cab picker: the extras' part triangles.
- New: the map's car plans with occupied seats (its own task: a layer in
  `ActivityMapLayer`'s vehicle footprints when zoomed in, the seats
  coloured by StationPeople's riders).
- Task 17 (automatic shadow levels) needs the same moment, geometry at
  load before it is dropped: the same hook.

### Memory
Only where asked: doors and the plan a few tens of KB a car shape, shared
by every car of that shape; full triangles for walk-area objects and cab
shapes as now, without the second parse.

## Order
1. The hook and `ShapeExtras` in `SFileLegacy` and `E3dShape`; `CarDoors`
   and `ShapeSeats` moved. Checks: station-people, the same doors from the
   extras as from the old read (OR_PSK_NEW's EN57, MaSzyna's EN57),
   timings of a car's load with and without them (Release).
2. The cab picker; walk areas (with §3's decision).
3. The map's car plans: its own task.

## Decisions (the user, 2026-11-17)
1. A request on the shape is fine as long as there is a clear way to make
   it before `initGL()` runs. If load options are used instead, only the
   options selected for it make up `ShapeLib`'s cache key (extras not
   among them).
   - The clear point: the asker's `ShapeLib::addShape` call (`Eng` for
     cars, as it passes its options now): a shape loads only when first
     drawn, so flags set there come before its load. The flags go to the
     worker's `detachedCopy()`. A request arriving once the shape is
     loaded, or while its copy is loading, gets its extras from a read of
     its own after `adopt()` (the only second read left).
2. A shape not loaded yet: nothing until it is loaded (no fallback read).
3. The map loads the shapes of the cars it shows plans for (a loader
   request outside drawing), so any train on the map gets its plan once
   loaded; nothing is shown before.

## Not in this design
- The map layer and the occupancy colours (task 5's new item).
- Simplified plans (a car's outline only, for small zoom).
