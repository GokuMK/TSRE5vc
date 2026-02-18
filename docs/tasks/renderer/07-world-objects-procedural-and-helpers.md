# Task 07 - World Objects (Procedural And Helpers)

## Objective
Migrate procedural and helper-heavy classes to gather path.

## Scope
Target classes such as:
- `DynTrackObj`
- `ForestObj`
- `TransferObj`
- `RulerObj`
- `SoundRegionObj`
- `TrWatermarkObj`
- any remaining helper-only rendering paths using lines/points

## Suggested Touch Points
- `src/tsre/world/objects/*.cpp`
- `src/tsre/ogl/OglObj.cpp`
- renderer packet helpers as needed

## Requirements
- Preserve procedural geometry generation and material behavior.
- Support line/helper rendering through generic queue path.

## Acceptance Criteria
- Procedural object classes are visible and selectable in gather mode.
- Helper lines/gizmos render with expected thickness/color.

## Out Of Scope
- TrackDB/activity/path/marker overlays.
