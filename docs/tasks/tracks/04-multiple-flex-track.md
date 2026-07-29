# Task 04 - Multiple Continuous Flex Tracks

## Objective

Extend the continuous **FLEX TRACK** tool so one mouse gesture can preview and
place a parallel double- or triple-track way.

## Status

Implemented; GUI acceptance testing pending.

## User Interface

- The `[...]` button beside **FLEX TRACK** expands the multiple-track options.
- **Track on left** adds one companion track to the left of the main track.
- **Track on right** adds one companion track to the right of the main track.
- Both options may be enabled for a three-track way.
- **Track separation** is shared by both sides, defaults to `4.0m`, and accepts
  values from `1.0m` to `20.0m`.
- The main track remains the selected object. Companion previews are owned by
  the continuous Flex placement operation and do not change selection.
- Option changes apply when the next unfinished segment is created.

The common double-track workflow therefore requires enabling either the left
or right checkbox once, then placing Flex segments normally.

## Geometry And Coordinate-Space Boundary

For each companion, its start and end poses are derived by transforming a
signed local-X separation vector through the main track's start and end object
quaternions. The resulting continuous world positions are normalized back into
TSRE's tile/local-position representation.

`Flex::OffsetWorldPose(...)` owns this world/object coordinate conversion. It
is intentionally separate from Flex's mirrored internal 2D solver space.
`Flex::ParallelDyntrackSections(...)` then preserves every straight length and
curve angle while changing each curve radius by the signed separation. This
produces the exact parallel path instead of asking the general pose-to-pose
solver to approximate it from its discrete candidate radii.

This preserves the separation and tangent throughout the shape. Inner and
outer companion curves have different radii and arc lengths from the main
centerline.

## Placement Flow

1. The main Dynamic Track is placed and remains selected.
2. Unselected companion Dynamic Track objects are created at the offset start
   poses and retained by the live Flex tool.
3. Every accepted mouse update rebuilds the main preview, derives its end pose,
   and rebuilds each companion to its matching offset end pose.
4. A click accepts the complete way. The main track and all companions are
   added to TDB before the next continuous segment begins.
5. Escape or changing tools removes the main unfinished object and all
   unfinished companions.

As in Task 03, DynTrack TDB insertion currently clears undo history because
dynamic TSection changes are not represented by the undo model. The deferred
fix remains tracked by `docs/tasks/editor/02-undo-dyntrack-tdb-tsection.md`.

## Acceptance Criteria

- With neither side enabled, the tool behaves exactly as Task 03.
- Left or right mode previews and commits a parallel two-track way.
- Enabling both sides previews and commits a three-track way.
- The main object remains selected throughout placement.
- Separation is correct at both straight and curved segment endpoints.
- Curved companion tracks meet their next continuous segments without visible
  gaps or heading discontinuities.
- Elevated and tile-crossing ways retain valid poses and geometry.
- Invalid companion geometry cannot be committed.
- Cancelling removes all unfinished companion objects but leaves previously
  accepted tracks intact.
- Normal Select, Place New, `Y`, `Q`, and `Z` behavior remains unchanged.

## Verification

- The `flex-point` headless suite covers identity, rotated, positive
  tile-boundary, negative tile-boundary, and non-finite offset-pose cases.
- GUI acceptance should cover left, right, both sides, curves in both
  directions, elevation, tile crossings, cancellation, and continuous TDB
  joins.
