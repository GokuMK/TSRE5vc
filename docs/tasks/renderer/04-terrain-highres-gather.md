# Task 04 - Terrain High Resolution Gather

## Objective
Bring high-resolution terrain fully online in gather mode.

## Scope
- Ensure terrain high-res packets from `Terrain::pushRenderItem` render correctly.
- Validate transforms, patch offsets, and selection colors.
- Preserve terrain visibility flags and hidden patch behavior.

## Suggested Touch Points
- `src/tsre/world/Terrain.cpp`
- `src/tsre/world/TerrainLibQt.cpp`
- `src/routeEditor/RouteEditorGLWidget.cpp`

## Requirements
- Correct tile/world transform in gather path.
- Textured and selection-color terrain rendering both work.

## Acceptance Criteria
- Terrain appears in gather mode where legacy mode shows terrain.
- Terrain patch selection works.
- No gross z-order or transform errors.

## Out Of Scope
- Distant terrain and water layers (next task).
