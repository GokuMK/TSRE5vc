# Task 08 - Shape Loading on Worker Threads

## Objective

Parse MSTS shapes (`SFileLegacy`, `SFileComplex`) and glTF shapes
(`GltfShape`) on worker threads, as textures are, so the editor's main
thread no longer stalls while shapes load. The OpenGL calls are already
out of loading (task 05, renderer task 16), so only data parsing would move.

## Current model (2026-10-08)

- `ShapeLib::addShape` creates the shape object without loading it.
- A shape loads on its first `pushRenderItem` when
  `Game::objectLoadingTokens` allows it: two tokens per load, refilled by
  two per editor tick up to `core.rendering.objectLoading.targetTokens`
  (`Game::maxObjLag`, default 10; the counter starts at
  `objectLoading.initialTokens`, 1000). `load()` runs on the main thread in one go:
  `loadData()` (file read and parse) then `initGL()` (renderer meshes,
  `TexLib::addTex`).
- Callers already meet shapes that are not loaded yet (lazy loading): most
  queries are guarded by `isLoaded()`.
- Textures load on one `QThread` per texture; the worker publishes with an
  atomic `loaded` flag and the main thread uploads.

## Review: what a worker would break

Places that expect a shape synchronously or touch it unguarded:

1. `reload()` (`SFileLegacy`, `SFileComplex`, `GltfShape`): resets the
   shape to "not loaded"; the next frame loads it again. Callers:
   `PropertiesStatic::reloadEnabled`, `StaticObj::reload`,
   `TrackObj` (line 155), `Eng::reload`, `ConEditorWindow` (569),
   `ShapeViewerWindow` (349, 354, 431). Already asynchronous in effect;
   a reload while a worker still parses the old data must drop that
   result (generation counter).
2. `ShapeViewerWindow` (373, 399) calls `loadData()` directly and uses the
   data at once: keep it synchronous (an explicit synchronous load).
3. Unguarded reads of shape data that would race with a worker:
   `StaticObj::loadSnapablePoints` (`isSnapable`, `addSnapablePoints`),
   `StaticObj::getLinePoints` (`getFloorBorderLinePoints`),
   `Eng`/`Consist::fillContentHierarchyInfo`, the Shape Viewer widget's
   queries. Each must check `isLoaded()` (or the shape method must).
4. `enableSubObjByName` and other state calls made before loading
   completes: signals already queue them (`enableSubObjByNameQueue`);
   check the remaining callers.

Thread safety of what loading touches:

5. `TexLib::addTex` mutates `TexLib::mtex` without a lock. MSTS shapes call
   it in `initGL()` (main thread, fine); `GltfShape::parseAndBuild` calls
   it while parsing (lines 1408, 1437): split glTF loading so textures are
   registered in the main-thread phase.
6. The log handler (`main.cpp`, `myMessageOutput`) writes `log.txt` without
   a lock; texture threads already log through it. Needs a mutex.
7. `ContentPath` helpers are pure; `Game::season` and `Game::root` are only
   read during loading (a season change during loading is the only
   overlap).
8. `loaded` states differ: `SFileLegacy` uses 2 for "loading", `GltfShape`
   for "failed". The threaded states need to be explicit.

## Design

- States per shape: not loaded, queued, parsed (worker done), ready
  (`initGL` done), failed; `isLoaded()` is true only when ready. The worker
  writes the shape's data, then publishes "parsed" with release order; the
  main thread reads only after seeing it.
- A small pool (`QThreadPool`, a few threads) runs `loadData()`; jobs carry
  the shape's generation and are dropped if it changed (reload).
- The main thread, in the editor tick, finishes parsed shapes with
  `initGL()` (texture and mesh registration), within a per-frame budget.
- glTF: `parseAndBuild` keeps parsing on the worker and records the
  textures to register; registration moves to the main-thread phase.
- Synchronous load stays available (`loadNow()`) for the Shape Viewer and
  tests.
- objLag: tokens no longer pay for main-thread parsing. Proposal: the
  setting limits shapes queued at a time (jobs in flight) and the
  main-thread finishing per frame; the token refill remains for the other
  token users (`ForestObj`, `TransferObj`, terrain).
- A setting `core.rendering.threadedShapeLoading` (as for textures), so the
  old path stays for comparison and as a fallback.

## Verification

- Captures (`parity-views`, shadows) with the setting on and off: equal
  images once settled; settle frames and time to the last shape.
- Load time of a dense route view (CMK, EUROPE1) from launch to the last
  shape, main-thread frame times while loading (PresentMon p90).
- Editor operations: reload of a shape, placing objects, snapping,
  selection boxes, Shape Viewer and Consist Editor, signals' sub-objects.
- Repeated runs for races (shape reload while loading, quitting while
  loading).

## Decisions for the user

- Default of the setting (off while testing, then on).
- objLag's new meaning (jobs in flight and per-frame finishing) and its
  default.
