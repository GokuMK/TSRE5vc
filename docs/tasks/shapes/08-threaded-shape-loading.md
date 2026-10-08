# Task 08 - Shape Loading on Worker Threads

## Objective

Parse MSTS shapes (`SFileLegacy`, `SFileComplex`) and glTF shapes
(`GltfShape`) on worker threads, as textures are, so the editor's main
thread no longer stalls while shapes load. The OpenGL calls are already
out of loading (task 05, renderer task 16), so only data parsing would move.

## Model before this task (2026-10-08)

- `ShapeLib::addShape` creates the shape object without loading it.
- A shape loaded on its first `pushRenderItem` when
  `Game::objectLoadingTokens` allowed it: two tokens per load, refilled by
  two per editor tick up to `core.rendering.objectLoading.targetTokens`
  (`Game::maxObjLag`, default 10; the counter started at
  `objectLoading.initialTokens`, 1000, a burst for the first view).
  `load()` ran on the main thread in one go: `loadData()` (file read and
  parse) then `initGL()` (renderer meshes, `TexLib::addTex`).
- Callers already met shapes that were not loaded yet (lazy loading): most
  queries are guarded by `isLoaded()`.
- Textures load on one `QThread` per texture; the worker publishes with an
  atomic `loaded` flag and the main thread uploads.

## Review: what a worker would break

Places that expect a shape synchronously or touch it unguarded:

1. `reload()` (`SFileLegacy`, `SFileComplex`, `GltfShape`): resets the
   shape to "not loaded"; the next frame loads it again. Callers:
   `PropertiesStatic::reloadEnabled`, `StaticObj::reload`,
   `TrackObj` (line 155), `Eng::reload`, `ConEditorWindow` (569),
   `ShapeViewerWindow` (349, 354, 431). Already asynchronous in effect
   (`SFileComplex::reload` loads at once); a reload while a worker still
   parses the old data must drop that result.
2. `ShapeViewerWindow` (373, 399) calls `loadData()` directly and uses the
   data at once: must stay synchronous.
3. Reads of shape data that would race with a worker parsing into the
   shape: `StaticObj::loadSnapablePoints` (`isSnapable`,
   `addSnapablePoints`), `StaticObj::getLinePoints`
   (`getFloorBorderLinePoints`), `getBound()`/`getSize()`,
   `Eng`/`Consist::fillContentHierarchyInfo`, `updateSim`,
   `enableSubObjByName`, the Shape Viewer widget's queries.
4. State calls made before loading completes (`enableSubObjByName`,
   `setAnimated`, `newState`) must survive the load.

Thread safety of what loading touches:

5. `TexLib::addTex` mutates `TexLib::mtex` without a lock. MSTS shapes call
   it while drawing (main thread); `GltfShape::parseAndBuild` calls it
   while parsing.
6. The log handler (`main.cpp`, `myMessageOutput`) wrote `log.txt` without
   a lock; texture threads already logged through it.
7. `SFileLegacy::loadSd` read `Game::TextureFlags[...]`: `QHash::operator[]`
   inserts, so concurrent loads would write the shared hash.
8. `Meshes::create`/`release` lock the mesh store: callable from workers.
   `ContentPath` helpers are pure; `GLUU::get()->alphaTest`, `Game::season`
   and `Game::root` are only read.

## Implementation

`ShapeLoader` (`src/tsre/shape/ShapeLoader.h/.cpp`):

- A worker never touches the live shape. `ComplexShape::detachedCopy()`
  creates, on the main thread, a new unloaded shape of the same file and
  options; a job runs `loadDetached()` on it in a `QThreadPool` (file,
  parse, `.sd`, renderer meshes). When the live shape is next drawn,
  `ShapeLoader::request()` sees the finished job and the shape `adopt()`s
  the copy's data on the main thread, keeping its own states (enabled
  sub-objects, animation, queued names). The copy, holding the old data, is
  deleted there too. Until then the live shape is plainly unloaded, so the
  reads in 3 and the calls in 4 see what they saw before its turn came;
  nothing races.
  - `SFileLegacy`: the copy loads with `load()`; `adopt` swaps the fields
    `loadData`, `loadSd` and `initGL` fill. The copy starts from the
    texture path before the season directory (`textureRoot`).
  - `SFileComplex`: the copy loads with `load()`; `adopt` swaps the
    private data as the Compact rebuild in `initGL` already did.
  - `GltfShape`: the copy records its textures instead of registering
    them (ids `PendingTextureBase + i` in the materials, embedded images
    decoded on the worker); `adopt` registers them with `TexLib` and fixes
    the ids.
- Synchronous loading wins: `adopt` does nothing if the live shape loaded
  meanwhile (the Shape Viewer's `loadData`). `reload()` and the
  destructors call `ShapeLoader::cancel()`, so a result parsed from the old
  file is dropped (a running job finishes and is deleted on the main
  thread).
- Without threading `request()` answers "load here" when a token is free,
  as before.
- Settings: `core.rendering.threadedShapeLoading` (on by default) and
  `core.rendering.objectLoading.parallelShapes` (worker threads, so jobs in
  flight, default 4); both apply after a restart.
  `core.rendering.objectLoading.targetTokens` now paces main-thread loading
  only: forests, transfers, and shapes when threading is off.
- Whole views: `core.rendering.objectLoading.initialTokens` (the startup
  burst) is retired. Instead the route editor's first frame, and the first
  frame after a camera jump (more than 500 m in one frame: navigation
  window, startup camera, capture views), gathers the world and drops it
  until nothing more is asked (`RouteEditorGLWidget::loadWholeView`):
  requests then have no limits (`ShapeLoader::WholeView`,
  `Game::ignoreLoadLimits` for forests and transfers), and the gathers wait
  for the workers. Such a view is shown with its shapes loaded, threaded or
  not.
- The log handler takes a mutex; `loadSd` reads the season flags with
  `QHash::value`.
- The renderer capture suites settle only when no job runs and none
  finished during the stable frames.

## Verification

- Captures (`parity-views`, shadows) with the setting on and off: equal
  images once settled; settle frames and time to the last shape.
- Load time of a dense route view (CMK, EUROPE1) from launch to the first
  frame and to the last shape, main-thread frame times while flying
  (PresentMon p90).
- Editor operations: reload of a shape, placing objects, snapping,
  selection boxes, Shape Viewer and Consist Editor, signals' sub-objects.
- Repeated runs for races (shape reload while loading, quitting while
  loading).

## Measurements

CMK, Steam Deck, first view (648 shapes), main-thread time measured with
temporary timers (2026-10-08):

| | Threads | Main thread |
|---|---|---|
| Whole first view | about 430 ms | about 820 ms |
| Tile requests (world files, object creation) | 200-270 ms | 200 ms |
| World gather | 190-230 ms | 640 ms |
| Forests generated | 0.15 ms | 0.13 ms |
| Transfers built | 5 ms | 5 ms |
| Adopting copies | 0.7 ms | - |

Forests and transfers stay on the main thread: they cost little, and both
read state the main thread edits. A transfer re-samples the terrain every
frame to follow brush edits (`TransferMesh::update`); a forest reads
terrain heights, the track and road databases (clearing distance) and the
global `std::rand` sequence, whose shared state would make tree placement
depend on thread timing. The next main-thread costs are reading the tiles'
world files and the rest of the world gather.

## Seasonal shape textures

Both MSTS loaders take the `.sd` season directory from
`TerrainSeason::shapeTextureDirectory`. Before, settings stored seasons as
`SpringClear`, `WinterClear` and so on while the loaders compared the raw
name, so the clear seasons never selected seasonal shape textures;
`SFileLegacy` also tested the Snow bit for SnowTrack (`flags & X != 0`),
left out SummerSnow, and treated an `.sd` without
`ESD_Alternative_Texture` as having every flag. Rain seasons now fall back
to their season, as terrain does. EUROPE1 in WinterClear shows the WINTER
and SNOW shape textures (bare trees, snowy roofs). The rule is not yet
checked against Open Rails: see `09-shape-seasonal-textures.md`.
