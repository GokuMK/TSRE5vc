# Task 10 - Flex Vertical Alignment

## Objective

Make elevated Flex Track and Flex Road geometry meet the requested mouse or
snapped endpoint in three dimensions. Implement this in two milestones:

1. correct the main DynTrack endpoint height and horizontal position;
2. correct companion-track elevation after the main-track behavior is
   accepted.

Do not mix the companion diagnosis into the first implementation.

## Status

Milestone 1 live-preview geometry was visually accepted for ordinary cases.
The subsequently found multi-subsection TDB composition defect now has a
system-wide full-frame correction and headless placement/reversal regression
coverage. A preliminary author check looked correct, but this is not visual
acceptance: independent route testing is still required, especially for
joined/reversed vectors and existing MSTS routes with nonzero third-angle
values. Milestone 2 exact endpoint alignment remains enabled for separated rail
companions. Contiguous road lanes share the main road grade instead, keeping
their wide profiles on one rigid plane. Visual testing accepted the resulting
continuous lateral lane surface. Successive curved, elevated road objects do
not join perfectly, but this matches the known limitation of rigid MSTS static
road shapes and is not treated as a regression or milestone blocker.

Implementation notes:

- `Flex::ElevatedPlanarTarget(...)` owns the double-precision world/Flex
  conversion and returns the adjusted planar target plus absolute promille;
- the initial direction straight, free-point solver, snapped pose solver, and
  legacy `AutoFlex(...)` use the same conversion;
- `DynTrackObj::setElevation(...)` now applies the exact difference between
  current and requested pitch instead of a small-angle sine approximation;
- TDB subsection composition now transports the complete stored frame
  (pitch, yaw, and the third Euler angle) through every curve;
- `newTrack(...)`, `appendTrack(...)`, TrackShape path rotations, vector
  reversal, yellow/collision lines, vector point extraction, and
  `getDrawPositionOnTrNode(...)` share that representation;
- the hot sampling path keeps its legacy work for ordinary zero-third-angle
  sections and performs one additional conditional local rotation when the
  stored third angle is nonzero;
- headless coverage checks an elevated curved endpoint in final XYZ and
  repeated absolute elevation assignment;
- the captured `UiD 101` curve-straight-curve sequence checks all internal
  starts and sampled boundaries, requires transported third-angle data, and
  verifies that vector reversal and a second reversal preserve the path;
- route `bbb` endpoints 308-309 provide a second captured reversal case. Its
  three identical one-degree curves were rotated independently before being
  joined, so neighboring stored yaws are intentionally discontinuous;
- the same case compares a generated yellow/collision-line point against the
  hot TDB sampler on a rolled curve, guarding the renderer's opposite-Z sign.

## Multi-Subsection TDB Finding

Route `bbb`, world tile `w-005306+014961.w`, DynTrack `UiD 101` is a captured
regression case:

```text
curve   -0.634851 rad, radius 100 m
straight 87.7 m
curve   +0.0432589 rad, radius 100 m
QDirection pitch approximately 0.126 rad
```

Its TDB vector starts rise approximately:

```text
0.979 m -> 8.445 m -> 19.485 m -> endpoint 20.029 m
```

The second rise is approximately:

```text
87.7 * sin(0.126) = 11.04 m
```

This proves that `TDB::appendTrack(...)` pitches the second straight around
the tangent produced by the preceding curve. The complete DynTrack world
object, `Flex::DyntrackEndpoint(...)`, and Open Rails dynamic decomposition
instead transform cumulative planar subsection positions through the original
world-object orientation. The two models agree for the first subsection and
diverge after a curve changes heading.

This is why another DynTrack with a later straight may look correct: when the
accumulated heading change before that straight is zero or small, the erroneous
per-subsection pitch axis is equal or close to the original object pitch axis.

Do not compensate for this by changing the solved DynTrack grade. That would
move the visible main shape away from its requested endpoint and would only
hide one particular TDB path error.

The system-wide correction uses the native TDB subsection frame:

1. reconstruct a subsection's orientation matrix from stored pitch, yaw, and
   third angle;
2. transform the subsection displacement through that complete frame;
3. advance the frame by the subsection's local curve angle;
4. decompose the result back into all three stored TDB angles for the next
   subsection;
5. when a vector is joined in reverse, derive each reversed frame from the
   original section's end tangent rather than copying or negating angles.

Vector reversal deliberately does not recalculate subsection positions.
`param[8..12]` comes from the following stored subsection or the old end node,
matching the legacy TDB behavior. Stored boundaries are authoritative because
valid routes may contain independently rotated pieces and old or third-party
placement conventions which current TSection math cannot reproduce exactly.
Only the reversed frame is derived geometrically from the individual section.
The endpoint-node frames remain unchanged: both endpoint yaws point outward
from the vector and are used directly when appending more track.

The endpoints 308-309 capture also contains an existing approximately 3.75 cm
disagreement between a calculated curve end and its stored boundary. The test
requires exact preservation of the stored boundary and compares interior
reversed geometry within that known source-data discrepancy. This prevents a
reversal from silently "repairing" and thereby moving existing route data.

## Rigid-Elevation Turn Limit

A DynTrack stores elevation as one rigid rotation of its complete local X/Z
plane. It can represent a monotonic grade only while every path tangent stays
within +/-90 degrees of the starting forward direction. Beyond that boundary,
local forward depth decreases along the path, so increasing the single pitch
to meet an elevated endpoint produces an increasingly steep shape and makes
the far part of the curve descend. Near a 180-degree turn, no sensible finite
rigid pitch can represent a nonzero endpoint height.

The Flex Track/Road preview therefore rejects elevated solutions whose
cumulative curve heading crosses +/-90 degrees. Horizontal solutions remain
valid through 180 degrees. The last complete preview stays visible when this
limit is crossed. Supporting monotonic elevated U-turns requires multiple
DynTrack objects or a future generator which applies grade along path length.

This is the more general fix: it applies to DynTrack and static TrackObj,
single- and multi-path TrackShapes, forward placement, and vectors reversed by
joining. It also makes existing MSTS TDB data with a nonzero third angle render
through the same frame used for endpoint composition.

The earlier placement-only exact-boundary workaround remains preserved under
`docs/experiments/trackdb-exact-boundary-fallback.*`. It is deliberately not
compiled and should only be restored on a temporary branch if route testing
finds a fundamental problem in the full-frame model.

Inspection of the original/partly original `CMK` route supports this meaning
of the third value: thousands of vector sections contain small nonzero values,
including single-section objects, and curved examples match the roll component
created by transporting a pitched plane through their curve angle. It is not
merely a TSRE superelevation scratch field.

The global `tsection.dat` confirms that this is not DynTrack-specific. Shape
86, `A1t45dYardCrvLft.s`, is a simple installed one-path regression candidate:
a 60 m radius, -45 degree curve followed by a 10.71068 m straight. At 100
promille, legacy tangent-relative composition overshoots the rigid shape by
approximately 0.31 m vertically.

Yellow-line rendering, collision-line generation, vector point extraction,
and `getDrawPositionOnTrNode(...)` now consume the third angle. The real-time
sampling change is intentionally narrow: zero remains the fast legacy path;
nonzero sections rotate the local point once before the existing pitch/yaw
matrix transform.

## Confirmed Main-Track Defect

Before this milestone, `RouteEditorGLWidget::updateLiveFlex(...)` calculated:

```text
Elevation = 1000 * deltaY / horizontalXZChord
```

The complete XZ chord includes both the DynTrack-local lateral displacement
and its local forward/Z displacement. A curved track has a non-zero lateral
component, so the chord is longer than the local forward depth. The resulting
pitch is too small and the generated endpoint remains below the mouse target.

This is not an arc-length problem. MSTS DynTrack `Elevation` is represented by
pitching the complete planar object around its local X axis. Consequently:

- local lateral X is not affected by elevation;
- local forward/Z is split between horizontal forward distance and height;
- travelled centerline/arc length is not the elevation denominator.

Using curve arc length would model grade along the rail, but it would not match
the rigid-pitch representation actually saved and rendered by DynTrack.

## Required Geometry

Resolve the requested world displacement into the start track's horizontal
frame:

```text
lateral = dot(horizontalDelta, startRight)
forwardHorizontal = dot(horizontalDelta, startForward)
deltaY = targetY - startY
```

For a forward target, construct the equivalent unpitched planar endpoint:

```text
forwardPlanar = hypot(forwardHorizontal, deltaY)
pitch = atan2(deltaY, forwardHorizontal)
gradePromille = 1000 * sin(pitch)
              = 1000 * deltaY / forwardPlanar
```

The planar Flex solver must solve to `(lateral, forwardPlanar)`, not to
`(lateral, forwardHorizontal)`. After the resulting DynTrack is pitched:

```text
forwardPlanar * cos(pitch) = forwardHorizontal
forwardPlanar * sin(pitch) = deltaY
```

This preserves both requested horizontal position and height. Merely changing
the current denominator to local forward depth would correct height but would
leave a small horizontal shortfall caused by `cos(pitch)`.

## Coordinate-Space Boundary

The elevation-aware target conversion belongs in `Flex`, next to the existing
WorldObj/Flex/TDB conversion helpers. The editor must not reproduce yaw or Z
sign transformations.

Use double precision while combining tile coordinates and calculating the
continuous displacement. Convert back to tile/local floats only after the
adjusted planar target has been normalized. This follows the established rule
used by companion offsets and avoids the earlier large-world precision bug.

A suitable pure helper or wrapper should return:

- the adjusted planar target tile/local position consumed by the existing
  free-point or pose-to-pose solver;
- the required absolute DynTrack grade/pitch;
- failure for non-finite data, a non-forward target, or an impossible pitch.

The initial direction-defining straight, later free mouse placement, and
endpoint-snapped placement must use the same conversion. Snapped endpoint yaw
remains the yaw supplied to `Flex::NewFlex(...)`;
the target endpoint's own pitch is not a second degree of freedom. One rigidly
pitched DynTrack can match endpoint height and horizontal yaw, but it cannot
also represent an arbitrary change of vertical grade.

## Applying Elevation

`DynTrackObj::setElevation(...)` is intended to set an absolute promille grade,
but its current incremental quaternion formula should be verified rather than
assumed exact. The implementation must test the final pose through
`Flex::DyntrackEndpoint(...)`.

If the existing setter is not exact for a DynTrack which already has pitch,
add a narrowly scoped absolute-grade operation or correct the setter with
tests. Preserve world yaw and do not introduce roll. Continuous placement must
inherit the same pitch at the next segment start without yaw drift.

## Milestone 1 - Main Track

1. Add and unit-test the elevation-aware planar-target conversion in `Flex`.
2. Use it for the initial straight and before both `NewFlexToPoint(...)` and
   snapped `NewFlex(...)` calls.
3. Apply the returned grade only after a successful geometry solve.
4. Validate the resulting endpoint in XYZ using `DyntrackEndpoint(...)`.
5. Keep the last complete preview when conversion or solving fails.
6. Do not change companion elevation math in this milestone.

## Milestone 2 - Companion Tracks

Companion geometry has a different local forward depth on curves because its
radius changes. The previous preview divided the requested height by each
companion's XZ chord. This repeated the original main-track denominator bug
and left inner and outer endpoints at different heights.

`Flex::RigidElevationForEndpointHeight(...)` now evaluates the companion's
own unpitched section endpoint and uses its local forward coordinate:

```text
gradePromille = 1000 * deltaY / companionForwardDepth
```

This is applied after `ParallelDyntrackSections(...)` has produced the
companion radii. It therefore uses neither the main chord nor the main
centerline length and does not reproduce world/tile coordinate math in the
editor. Headless coverage checks both sides of a rotated 60-degree curve at
100 promille, including a tile crossing, exact endpoint height, and the
expected inner/outer grade ordering.

That exact-height rule is appropriate for separated railway tracks, but it is
visually harmful for contiguous road lanes: the different radii produce
slightly different pitches, putting adjacent lane meshes on different rigid
planes. Road companions therefore copy the main road's grade. Their centerline
end heights may differ slightly on a curved grade, which is an intentional
milestone compromise to preserve the joined road surface without introducing a
new swept-road elevation generator.

Visual acceptance confirmed that left, middle, and right road lanes form one
consistent surface with this policy. A small mismatch can remain where one
curved, elevated road object ends and the next begins because each object is a
separately transformed rigid shape. Existing MSTS static road shapes exhibit
the same limitation. As a small visual mitigation, elevated curved road
DynTracks rendered from ORTS profiles add a 25 cm end-only mesh apron, lowered
2 mm at its extra row. This does not alter the RDB endpoint, snapping, or saved
track geometry. It merely underlaps the next road surface while avoiding a
coplanar overlap. Removing the underlying limitation requires the deferred
roll-free swept-road model or finer shape subdivision and is outside this
milestone.

## Tests

Add headless cases covering:

- straight uphill and downhill tracks;
- left and right single curves with significant lateral displacement;
- rotated starting yaw;
- positive and negative tile crossings;
- a continuous second segment starting with inherited pitch;
- snapped endpoints at a different height;
- non-finite and near-vertical rejection;
- final `DyntrackEndpoint(...)` XYZ error, not only the stored promille value.

Milestone 2 adds left, right, and both-side companion cases for both curve
directions.

## Acceptance Criteria

### Milestone 1

- The main elevated DynTrack endpoint matches the requested world XYZ within
  a small documented tolerance for straights and curves.
- Free-point and snapped-endpoint placement share one elevation conversion.
- Curved tracks are not under-elevated because of their lateral displacement.
- Continuous curved segments meet vertically and preserve heading.
- Flat-track behavior remains unchanged.

### Milestone 2

- Every rail companion meets its independently offset endpoint in XYZ.
- Road companions share the main road grade so adjacent lane profiles remain
  coplanar; small curved-grade centerline endpoint-height differences are
  accepted for this milestone.
- Inner and outer curves use the correct height sign and do not exhibit the
  reported apparent Z-axis flip.
- Main and companion section boundaries remain aligned.

## Relationship To Existing Tasks

Tasks 02-04 describe implemented and accepted horizontal Flex workflows.
Changing those historical task definitions would obscure the later vertical
defect. This task is therefore a separate corrective task. It supersedes Task
02's instruction to reuse the legacy AutoFlex chord-based elevation behavior,
while retaining the existing horizontal solver, continuous-placement, and
companion-coordinate boundaries.
