# Binary world parser recovery

Status: proposed post-merge follow-up to `feature/native-token-ids`. Do not
start implementation until the user approves this task after merge validation.

## Problem

`Tile::loadBinaryData()` records the initial object count, then rolls back every
object added by the current W/WS file if any later block throws `ParseError`.
That discards usable objects completed before the damaged region.

This behavior is not accepted as TSRE's recovery policy. The native-token
branch may merge with the regression explicitly tracked, but recovery must be
implemented as a separate change.

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
