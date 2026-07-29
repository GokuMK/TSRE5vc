# Task 03 - Continuous Flex Track Tool

## Objective

Add a dedicated **FLEX TRACK** object tool that turns live flexible track into
a continuous placement workflow.

## Status

Implemented; GUI acceptance testing pending.

## Tool Flow

1. Press **FLEX TRACK** below the existing Select and Place New buttons.
2. The tool selects a synthetic Dynamic Track REF internally.
3. The first left click places a new Dynamic Track and immediately starts its
   live Flex preview.
4. Moving the pointer updates the preview using Task 02 behavior.
5. A left click accepts the segment and adds it to TDB, matching `Z`.
6. The next Dynamic Track is created at the committed segment's calculated end
   pose and immediately starts its own live preview.
7. Repeat steps 4-6 for continuous construction.
8. Escape, changing tools, or toggling **FLEX TRACK** off removes the unfinished
   segment and exits continuous placement.

Each accepted segment is immediately represented in TDB. As with the existing
DynTrack `Z` implementation, creating its dynamic TSection data clears undo
history because TSection changes are not captured by the current undo model.
The deferred fix is tracked in
`docs/tasks/editor/02-undo-dyntrack-tdb-tsection.md`.

## Coordinate-Space Boundary

The next start pose is calculated by `Flex::DyntrackEndpoint(...)`. It simulates
the accepted DynTrack sections in Flex's tested kinematic convention, converts
the result through the object's quaternion exactly once, and normalizes the
resulting world position across tile boundaries. The editor does not infer the
next pose from the mouse target.

## Acceptance Criteria

- The button spans the Object Tools width below Select and Place New.
- Activating it makes Dynamic Track the current placement REF.
- The first click immediately enters live Flex.
- Accepting a segment immediately appends and previews the next segment.
- Every accepted segment is added to TDB before the next segment is created.
- Curved, elevated, and tile-crossing chains have no visible gaps or heading
  discontinuities.
- Escape removes only the unfinished segment and leaves accepted segments.
- Right-drag rotates the camera without rebuilding the live shape during the
  drag.
- Non-finite preview data is rejected, elevation trigonometry is clamped, and
  segments shorter than `0.1m` cannot be accepted into TDB.
- Normal Select, Place New, `Y`, `Q`, and `Z` behavior remains unchanged.
