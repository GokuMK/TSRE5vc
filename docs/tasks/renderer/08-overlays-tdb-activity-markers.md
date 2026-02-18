# Task 08 - Overlays: TrackDB, Activity, Path, Markers

## Objective
Restore non-world editor overlays in gather mode.

## Scope
- TrackDB and RoadDB overlays (`renderAll`, `renderLines`, `renderItems`) via gather-compatible submission.
- Activity/event/service/path visualization.
- Route marker rendering.

## Suggested Touch Points
- `src/tsre/world/Route.cpp`
- `src/tsre/tdb/TDB.cpp`
- `src/tsre/trains/Activity.cpp`
- `src/tsre/trains/Path.cpp`

## Requirements
- Preserve overlay toggles and selection behavior.
- Prefer gather submission over direct immediate draw in gather mode.

## Acceptance Criteria
- Overlay visibility and interaction match legacy mode on representative routes.
- No missing major overlay category in gather mode.

## Out Of Scope
- Shadows and HUD/compass/pointer restoration.
