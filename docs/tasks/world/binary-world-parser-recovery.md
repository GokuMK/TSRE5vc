# Binary world parser recovery

Status: implemented and verified on 2026-09-09 after the native-token merge.

## Problem

`Tile::loadBinaryData()` records the initial object count, then rolls back every
object added by the current W/WS file if any later block throws `ParseError`.
That discards usable objects completed before the damaged region.

This behavior was not accepted as TSRE's recovery policy. The separate
post-merge correction now commits each valid top-level unit independently.

## Required behavior

- Parse each top-level world object transactionally. Do not publish a partially
  decoded object.
- If an object's outer block header and end are valid but its payload is bad,
  discard only that object, seek to the known block end and continue with later
  siblings.
- If framing is damaged and a trustworthy next-object boundary cannot be
  established, stop reading that file and retain all objects completed earlier.
- A bad root must not mutate existing tile state.
- Preserve useful diagnostics: file kind, object token, byte offset and whether
  parsing continued or stopped.
- Mark recovered/incomplete loads separately from normal success. Viewing
  recovered objects must not silently authorize overwriting the source file and
  thereby deleting content TSRE could not decode.
- Apply the same principles to W and WS input without rolling back objects that
  existed before the current file began.

## Regression cases

1. Valid object, malformed object with a valid outer boundary, valid object:
   retain the first and third objects and report the skipped middle object.
2. Valid object followed by malformed top-level framing with no safe boundary:
   retain the first object and stop.
3. Malformed root: preserve all pre-existing tile state.
4. Failure inside ViewDbSphere/control data: retain independently completed
   objects and report whether the remaining file was skipped or abandoned.
5. Repeat generated plain/compressed parser tests and the complete stock W-file
   corpus after the change.

This task does not authorize a binary W writer, unsupported object codecs, or a
general lossless unknown-block document model.

## Implemented result

- `Tile::loadBinaryData()` stages each object, watermark and ViewDbSphere before
  publishing it.
- A malformed payload with a validated outer boundary is discarded and parsing
  resumes at that boundary. Invalid next-sibling framing stops parsing without
  deleting earlier objects.
- W and WS retain separate `Complete`, `Recovered` and `Failed` states. Root
  failure publishes no file state.
- Recovered/failed binary W or WS data is protected against source overwrite.
  `Tile::save()` reports failure, and `Route::save()` leaves the tile modified
  when saving was refused.
- Diagnostics identify W/WS, token, byte offset and whether parsing continued
  or stopped.

Verification passed: 32/32 CPU world/parser checks, 40/40 with shape/OpenGL
coverage, 1,533/1,533 token checks, all three CTest targets, and 727/727 stock
W/WS files through the temporary corpus runner. The runner and corpus assets
remain uncommitted.
