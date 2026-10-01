# Task 05 - Distant Terrain, Water, And Sky

## Objective
Restore non-highres environment passes in gather pipeline.

## Scope
- Distant terrain pass.
- Water passes (low/high where applicable).
- Skydome rendering in new path.

## Suggested Touch Points
- `src/routeEditor/RouteEditorGLWidget.cpp`
- `src/tsre/world/TerrainLibQt.cpp`
- `src/tsre/world/Terrain.cpp`
- `src/tsre/world/Skydome.cpp`

## Requirements
- Pass ordering should be consistent with legacy visual behavior.
- Respect current editor toggles for terrain/water visibility.

## Acceptance Criteria
- Gather mode displays distant terrain and water similarly to legacy mode.
- Skydome is visible and does not break depth behavior.

## Out Of Scope
- World object class migration.

## Gather implementation

Status: implemented; the gather frame no longer draws these layers directly.

- Sky: `Skydome::pushRenderItems()` submits the sky shape in `LAYER_SKY` with
  the sky transform on the renderer matrix; the frame draws `PASS_SKY` with
  the sky projection, then clears depth.
- Distant terrain and water: `TerrainLib::pushRenderItemsLo()` and
  `pushRenderItemsWaterLo()` mirror `renderLo()` / `renderWaterLo()` and call
  `Terrain::pushRenderItem()` / `pushRenderItemWater()`. They submit in
  `LAYER_DISTANT`; the frame draws `PASS_DISTANT` with the distant projection,
  then clears depth.
- Near water: `TerrainLib::pushRenderItemsWater()` submits in `LAYER_WATER`.
  `PASS_WATER` draws after the scene and overlay passes, matching legacy,
  which draws water after `Route::render()` and its overlays.

Validation (parity capture, separate processes): distant terrain primitives
and samples match legacy exactly on BNSF_SCENIC, the test route with paged
distant terrain. None of the test routes has a `skydome.s`; with a stand-in
skydome linked into a scratch copy of EUROPE1, the sky phase submits the same
1,848 primitives in both pipelines.
