# Task 03 - Selection And Picking Parity

## Objective
Restore editor picking and selection behavior in the new pipeline.

## Scope
- Re-enable selection handling in gather path.
- Ensure picking pass renders selectable IDs compatible with existing decode logic.
- Preserve category packing (`ww` domain) used in `handleSelection`.

## Suggested Touch Points
- `src/routeEditor/RouteEditorGLWidget.cpp`
- `src/tsre/world/Route.cpp`
- `src/tsre/world/Tile.cpp`
- any helper used for selection color packing

## Requirements
- Selection mode still limits object/terrain traversal as intended.
- Decoding paths for world object, terrain, activity, and tr-item selection still work.

## Acceptance Criteria
- Click-select works in gather mode for at least:
  - static/track world objects
  - terrain patch
  - activity item
  - tr item
- Legacy and gather selection results match for sampled picks.

## Out Of Scope
- Full visual parity for all content types.
