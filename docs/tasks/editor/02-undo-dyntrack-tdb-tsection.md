# Task 02 - Undo For DynTrack TDB And Dynamic TSection Changes

## Status

Implemented; visual and route-save acceptance testing pending.

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

## Implementation

- DynTrack insertion no longer clears the undo stack.
- The world object, target TDB/RDB, and route TSection allocation boundary are
  captured in one transaction.
- `TDB::fillDynTrack(...)` remains append-only. Undo removes definitions added
  by the transaction when its recorded post-allocation boundary still matches.
- A boundary mismatch leaves generated definitions as an unused cache instead
  of risking deletion of definitions used by newer, untracked data.
- Each accepted continuous Flex segment, including companion tracks or road
  lanes, is one undo item.
- Dynamic Track section switches and numeric section properties now create
  world-object undo snapshots.

The general undo behavior and the TSection delta strategy are documented in
`docs/features/undo.md`.

## Acceptance Testing

- Add a new rail DynTrack with `Z`, undo it, and verify its previous undo
  history remains available.
- Repeat with a road DynTrack.
- Place and undo a continuous Flex segment without companions.
- Place and undo a segment with both companion sides enabled.
- Repeat an equivalent shape and verify undo does not remove the shared
  pre-existing TrackShape.
- Save and reload after placement and after undo, checking both TDB topology
  and route `tsection.dat`.
- Redo remains out of scope because the editor has no redo stack.

## Related Work

- `docs/tasks/tracks/02-flexible-track.md`
- `docs/tasks/tracks/03-continuous-flex-track.md`
- `src/tsre/Undo.cpp`
- `src/tsre/world/Route.cpp`
- `src/tsre/tdb/TDB.cpp`
- `src/tsre/tdb/TSectionDAT.cpp`
