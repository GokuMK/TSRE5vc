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
