# Task 06 - World Objects (Shape-Based First)

## Objective
Migrate shape-driven world object classes to gather path.

## Scope
Add/complete `pushRenderItems` for shape-based classes first, for example:
- `SignalObj`
- `SpeedpostObj`
- `PlatformObj` / `Siding`
- `CarSpawnerObj`
- `LevelCrObj`
- `PickupObj`
- `HazardObj`
- `SoundSourceObj`

## Suggested Touch Points
- `src/tsre/world/objects/*.cpp` for listed classes
- shared helper paths via `SFile::pushRenderItem` and `OglObj::pushRenderItem`

## Requirements
- Preserve class-specific selection IDs and helper-part behavior.
- Keep LOD and visibility checks aligned with legacy logic.

## Acceptance Criteria
- Listed classes appear in gather mode across nearby tiles.
- Selection remains functional for migrated classes.
- No regressions in legacy mode.

## Out Of Scope
- Procedural geometry-heavy classes (next task).
