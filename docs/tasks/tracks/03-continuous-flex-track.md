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
8. Escape removes the unfinished segment and its companions, but keeps the
   current Flex tool armed so the next click starts a separate line.
9. If the user clicks a target for which the Flex solver cannot produce valid
   geometry, the unfinished segment is discarded and that click becomes the
   starting point of a new line.
10. `E`, changing tools, or toggling **FLEX TRACK** off removes the unfinished
    segment and exits continuous placement.

Each accepted segment is immediately represented in TDB and creates one undo
item containing the main object, companion objects, TDB/RDB topology, and any
route-local dynamic TSection definitions allocated for the segment. Details are
documented in `docs/features/undo.md` and
`docs/tasks/editor/02-undo-dyntrack-tdb-tsection.md`.

The expanded Flex options expose a minimum curve radius remembered separately
for track and road modes. Defaults are `15 m` for rail and `6 m` for road, with
a selectable lower bound of `5 m`. Both
free-point and snapped-endpoint solving honor the same hard floor. When
companion tracks or lanes are enabled, the effective floor is raised above the
separation distance so an inner companion radius cannot collapse or reverse;
the user's saved/displayed value is not changed.

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
- Undo removes one accepted segment and all its companions without clearing
  earlier undo history.
- Curved, elevated, and tile-crossing chains have no visible gaps or heading
  discontinuities.
- Escape removes only the unfinished segment and leaves accepted segments.
- Escape keeps the current Flex tool active and waits for a new starting click.
- Clicking an unsolvable target cannot accept an older valid preview; it
  discards the draft and starts a new line at the clicked point.
- Right-drag rotates the camera without rebuilding the live shape during the
  drag.
- Non-finite preview data is rejected, elevation trigonometry is clamped, and
  segments shorter than `0.1m` cannot be accepted into TDB.
- Rail and road minimum radii are remembered independently and are applied to
  both free and snapped Flex solutions.
- Normal Select, Place New, `Y`, `Q`, and `Z` behavior remains unchanged.
