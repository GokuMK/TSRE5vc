# Task 02 - Undo For DynTrack TDB And Dynamic TSection Changes

## Status

Deferred. Implement as part of the later undo-system improvement work.

## Problem

Adding a Dynamic Track to TDB through `Z` calls `Route::addToTDB(...)`.
When the DynTrack does not yet have a route TrackShape,
`TDB::fillDynTrack(...)` creates dynamic TSection sections and a TrackShape.

The current undo snapshot deep-copies TDB nodes and items, but shares the
`TSectionDAT` pointer. It therefore cannot restore the TSection sections and
shapes created for a DynTrack. `Route::addToTDB(...)` consequently calls
`Undo::Clear()` for DynTracks.

Continuous Flex Track commits every accepted segment through the same TDB
operation and inherits this limitation.

## Required Future Work

- Define ownership and snapshot semantics for mutable route TSection sections
  and TrackShapes.
- Capture or transactionally record additions made by
  `TDB::fillDynTrack(...)`.
- Restore both TDB topology and TSection identifiers consistently.
- Remove the DynTrack-specific `Undo::Clear()` only after the complete state
  can be restored safely.
- Make ordinary DynTrack `Z` and continuous Flex acceptance undoable without
  leaving orphaned sections, shapes, or invalid section IDs.
- Add tests covering add, undo, redo if supported, repeated equivalent shapes,
  and several continuously placed segments.

## Related Work

- `docs/tasks/tracks/02-flexible-track.md`
- `docs/tasks/tracks/03-continuous-flex-track.md`
- `src/tsre/Undo.cpp`
- `src/tsre/world/Route.cpp`
- `src/tsre/tdb/TDB.cpp`
- `src/tsre/tdb/TSectionDAT.cpp`
