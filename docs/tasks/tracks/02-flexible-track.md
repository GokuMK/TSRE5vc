# Task 02 - Live Flexible Track Following The Mouse

## Objective

Create a live flexible-track placement mode in which a newly placed Dynamic
Track follows the mouse pointer.

The result should resemble track placement in modern transport simulators.
This task focuses on reliable backend geometry and editor integration rather
than a polished user interface. It builds on the two-endpoint Flex solver from
Task 01.

Expected behavior:

- With a newly placed Dynamic Track selected, `Y` enables live-flex mode.
- The Dynamic Track placement pose is the fixed start point and start angle.
- Every meaningful mouse movement updates the selected object's
  `dyntrackdata`; it must not create a new world object for every update.
- Away from an existing track endpoint, the pointer supplies only the desired
  end position. The solver produces a straight or the simplest single circular
  curve from the fixed start tangent to that position.
- Pointer positions are quantized to a grid before solving, avoiding repeated
  shapes that differ only by centimeters or floating-point noise.
- A collinear forward target produces a straight track.
- When the pointer enters the configured snapping radius of another track
  endpoint, use the exact endpoint pose and Task 01's two-endpoint AutoFlex
  solver.
- Prefer the short, simple arc. Never choose a large loop, such as a 280-degree
  curve, when a direct arc is available.

## Status

Implemented. Automated pose-to-point and coordinate-boundary tests pass.

The live editor workflow still requires the manual acceptance pass listed
below, particularly endpoint snapping, cancellation/undo, and interaction
with existing `Z` workflows.

Implementation summary:

- `Flex::NewFlexToPoint(...)` provides analytic straight/single-arc geometry.
- `Flex::TdbYawFromTrackQuaternion(...)` contains and tests the explicit
  WorldObj/OpenGL-to-TDB heading conversion boundary.
- `RouteEditorGLWidget` owns the live preview lifecycle, world-space grid
  quantization, endpoint-mode selection, and one-state undo transaction.
- Live Flex uses its own `1m` endpoint snap radius and limits geometry rebuilds
  to `20Hz`; neither setting changes the editor's global snapping/render rate.
- `Undo::StateCancel()` discards a cancelled gesture after the object snapshot
  has been restored.
- The `flex-point` headless suite covers geometry, tile transitions, rejection
  cases, length limiting, and heading-space conversion.

## Current Editor Behavior

- `Y` currently enables the generic `resizeTool`/custom tool in
  `RouteEditorGLWidget`; it is not a DynTrack live-flex mode.
- The existing DynTrack **Flex** property button enables `FlexTool`.
- `FlexTool` waits for one left click and emits `flexData(...)`.
- `PropertiesDyntrack::flexData(...)` runs `Flex::AutoFlex(...)` once, updates
  `dyntrackdata`, and returns to the selection tool.
- `DynTrackObj::set("dyntrackdata", ...)` already updates all five section
  slots, marks the object modified, and invalidates its generated geometry.

Relevant code:

- `src/routeEditor/RouteEditorGLWidget.cpp`
- `src/routeEditor/properties/PropertiesDyntrack.cpp`
- `src/tsre/world/objects/DynTrackObj.cpp`
- `src/tsre/math3d/Flex.cpp`

## Existing Foundation

- Task 01 provides the two-oriented-endpoint solver:
  `Flex::NewFlex(...)`.
- `Flex::AutoFlex(...)` finds TDB endpoint poses and calls the new solver.
- Flex already uses a five-section DynTrack representation:
  `L + C + L + C + L`.
- Flex candidates are checked by simulating DynTrack kinematics, which should
  remain the authoritative validation boundary for coordinate signs and
  heading conventions.
- The JSONL Flex capture and headless replay infrastructure can be extended
  with free-end cases.

## Scope

### Included

- Rail `DynTrackObj` objects that are newly placed and not yet inserted into
  TDB.
- Conditional `Y` activation for a selected DynTrack.
- Continuous pointer-driven preview.
- Stable grid quantization across tile boundaries.
- Straight and single-circular-curve free-end geometry.
- Automatic switching to two-endpoint AutoFlex near a TDB endpoint.
- One undo operation for the complete live-flex gesture.
- Pure geometry tests and editor-level integration tests where practical.

### Not Included In V1

- Editing a DynTrack that is already represented in TDB.
- Roads or mixed rail/road endpoint selection.
- Automatically creating multiple Dynamic Track objects for long paths.
- Compound free-end paths when a single arc cannot sensibly reach the pointer.
- Easements, transition spirals, clothoids, or superelevation.
- A new polished toolbox or detailed curve-radius UI.
- Group-object or synthetic grouped-`TrackShape` work.

## Tool Lifecycle

1. User places or selects a DynTrack that is not in TDB.
2. Pressing `Y` enters live-flex mode for that object.
3. Capture an immutable start snapshot:
   - start tile;
   - start local position;
   - start quaternion/heading;
   - original `dyntrackdata` and elevation for cancellation.
4. Mouse movement updates the preview without requiring a mouse button.
5. A left click accepts the current preview and closes the undo transaction.
6. `Escape`, deleting the object, changing selection, or leaving the tool
   cancels the gesture and restores the captured data.

For selected objects other than DynTrack, `Y` retains its existing generic
resize behavior.

## Pointer Quantization

- Convert the pointer to one continuous world-space XZ coordinate first.
- Quantize that coordinate, not separate tile-local values, so crossing a tile
  boundary does not cause a discontinuity.
- Use `Game::DefaultMoveStep` as the initial grid spacing. Its current default
  is `0.25m`.
- Convert the quantized point back to tile/local coordinates only at the API
  boundary.
- Cache the last quantized point and endpoint identity. Do not rerun the
  solver or rebuild the VBO when neither has changed.
- Endpoint snapping overrides grid quantization: a snapped endpoint must use
  its exact TDB position.

The grid step should be isolated behind a small helper or parameter so a
dedicated flexible-track grid setting can be added later.

## Free-End Geometry

### Required API

Add a pure geometry entry point that does not query TDB. Suggested shape:

```cpp
bool Flex::NewFlexToPoint(
    int startTileX,
    int startTileZ,
    const float* startPosition,
    const float* startQuaternion,
    int targetTileX,
    int targetTileZ,
    const float* targetPosition,
    float* dyntrackSections);
```

The exact name may change, but the API must clearly distinguish:

- **pose-to-point**: start position/heading plus a free end position;
- **pose-to-pose**: Task 01 `Flex::NewFlex(...)`.

The solver must clear all ten output floats before producing a result.

### Normalized Coordinate Frame

Reuse the same world-to-Flex coordinate conversion already used by
`Flex::NewFlex(...)`:

1. Resolve tile offsets into one continuous XZ plane.
2. Apply the established Flex Z-axis convention in one helper.
3. Transform the target into a start-local frame:
   - start at `(0, 0)`;
   - start tangent points along local forward.

Do not duplicate sign-changing transformations in the editor integration.
Coordinate conversion belongs in Flex or a shared tested helper.

### Straight Case

If lateral displacement is within tolerance and forward displacement is
non-negative:

- output one straight section;
- set all curve sections to zero;
- treat tiny distances as a valid zero-length preview.

A nearly collinear point must not produce an enormous-radius curve merely
because of floating-point noise.

### Single-Curve Case

For a target with local lateral displacement `x` and forward displacement
`y`, a circle tangent to the start pose and passing through the point can be
derived analytically. Its signed radius magnitude follows:

```text
R = (x*x + y*y) / (2*abs(x))
```

The implementation must derive the signed turn and arc angle using the same
kinematic convention as DynTrack, then validate the output by simulating the
sections.

Rules:

- choose the principal/short arc;
- require `abs(angle) <= pi`, apart from a small numeric tolerance;
- reject the complementary loop;
- reject an initial wrong-way movement;
- respect the DynTrack minimum practical radius;
- output only one enabled curve section when valid;
- require simulated endpoint error to remain within tolerance.

For a fixed start tangent and point, the tangent circle is determined; an
ending-angle search is therefore a fallback/debug technique, not the primary
solver. If the analytic result fails validation, a bounded angle scan may be
used to diagnose conventions, but it must use the same limits and scoring.

### Unsupported Free-End Targets

Examples include a target directly behind the start or one requiring an arc
greater than 180 degrees.

V1 behavior:

- return failure;
- keep the last valid preview, or show the zero-length original shape if no
  valid preview exists;
- do not manufacture a loop or silently invoke a two-curve solver with an
  arbitrary ending angle.

## Endpoint Snapping And AutoFlex

Before solving the free-end case:

1. Search rail TDB endpoint/junction nodes around the raw pointer.
2. Limit the search using `Game::snapableRadius`.
3. Exclude the selected DynTrack's own TDB identity if this mode is later
   extended to committed objects.
4. If an endpoint is found:
   - use its exact tile, position, and orientation;
   - call Task 01's pose-to-pose `Flex::NewFlex(...)`, preferably through a
     helper that does not repeat endpoint lookup;
   - update elevation using the established AutoFlex behavior.
5. Otherwise quantize the pointer and call `NewFlexToPoint(...)`.

Avoid calling `Flex::AutoFlex(...)` blindly for every pointer position:
AutoFlex performs its own nearest-node queries and assumes both ends are
oriented endpoints. The live tool must explicitly choose between free-point
and snapped-endpoint modes.

Use a small snap hysteresis if necessary to prevent rapid switching at the
radius boundary:

- enter endpoint mode at `snapableRadius`;
- leave it at a slightly larger radius or after the endpoint identity changes
  decisively.

## Object Updating And TDB Safety

- Update the existing selected `DynTrackObj`; do not repeatedly place objects.
- Apply results through `DynTrackObj::set("dyntrackdata", ...)`.
- Update elevation only after a successful solve.
- Preserve the captured start position and orientation throughout the gesture.
- Do not call `Route::dragWorldObject(...)`; live flex changes geometry, not
  the DynTrack origin.
- Refuse live-flex mode when the object already exists in track or road TDB.
  This avoids leaving stale vector nodes while preview geometry changes.
- TDB insertion remains an explicit later action through the existing `Z`
  workflow.

## Undo And Cancellation

- Begin one undo state when live-flex mode starts.
- Capture the original object data once.
- Pointer updates must not push a new undo entry.
- Accepting commits that one state.
- Cancelling restores the original sections/elevation and closes or discards
  the state consistently with existing Undo APIs.

This prevents hundreds of undo records during one mouse gesture.

## Performance Requirements

- Solve only when the quantized target or snapped endpoint changes.
- Keep all searches bounded.
- Do not log every mouse event in normal builds.
- Reuse Task 01's lightweight candidate validation.
- Let `DynTrackObj` invalidate generated geometry only after a successful,
  changed result.
- Live editing should remain responsive on normal routes even when raw mouse
  events arrive much faster than rendering.

## Proposed Implementation Steps

1. Extract or formalize the shared Flex coordinate-conversion and DynTrack
   simulation helpers.
2. Implement and unit-test `Flex::NewFlexToPoint(...)`.
3. Add world-space pointer quantization with tile-boundary tests.
4. Add a live-flex tool state to `RouteEditorGLWidget`.
5. Make `Y` enter live-flex conditionally for an eligible DynTrack.
6. Store the immutable start/original-data snapshot.
7. Update previews from mouse movement without a held button.
8. Add explicit endpoint detection and switch to pose-to-pose Flex.
9. Implement accept, cancel, selection-change, and deletion cleanup.
10. Verify that committed/TDB DynTracks cannot enter live-flex mode.
11. Extend capture/replay tests with pose-to-point cases.
12. Add manual editor tests for tool lifecycle and undo behavior.

## Automated Test Cases

### Pure Pose-To-Point Solver

- Forward collinear target produces a single straight.
- Very small lateral noise still produces a straight.
- Left target produces one left curve and reaches the point.
- Right target mirrors the left case.
- Translation and tile changes do not change the resulting geometry.
- Rotating both start and target together produces equivalent sections.
- Targets on both sides of a tile boundary remain continuous.
- Target directly behind start fails without emitting a loop.
- A case whose complementary arc is approximately 280 degrees selects the
  short arc or fails if the short arc is invalid.
- Radius below the hard minimum is rejected.
- Every success reproduces the target when simulated.
- Every output clears unused DynTrack sections.

### Quantization And Mode Selection

- Pointer positions inside one grid cell produce identical inputs/results.
- Crossing one grid boundary produces exactly one recalculation.
- Exact endpoint snapping overrides the pointer grid.
- Entering the snap radius switches to pose-to-pose AutoFlex.
- Leaving the snap radius returns to pose-to-point solving without stale
  endpoint orientation.
- Repeated mouse events at an unchanged target do not invalidate geometry.

## Manual Acceptance Criteria

- Place a Dynamic Track, select it, and press `Y`.
- Moving the mouse forward previews a straight track.
- Moving left or right previews a simple single curve ending at the snapped
  pointer position.
- Preview geometry does not visibly jitter for sub-grid pointer movement.
- No free-end preview uses a loop greater than 180 degrees.
- Approaching an existing track endpoint visibly switches to a valid
  two-endpoint Flex connection.
- Leaving that endpoint returns to free-end behavior.
- The DynTrack origin and start heading remain fixed during all previews.
- Left click accepts the preview.
- `Escape` restores the pre-tool shape and elevation.
- One undo action reverts the complete accepted edit.
- Existing non-DynTrack `Y` behavior remains unchanged.
- Grouped `Z`, normal DynTrack `Z`, and Task 01 AutoFlex continue to work.

## Risks

### Coordinate-System Drift

World objects, TDB, and Flex use different Z/heading conventions. The new
solver must reuse one tested conversion boundary rather than adding editor-side
sign corrections.

### Tool-State Conflicts

`Y` already selects the generic resize tool. Conditional activation must not
alter other object types or leave stale live-flex state after selection/tool
changes.

### TDB Corruption

Changing geometry after a DynTrack has been inserted into TDB would make world
and database representations disagree. V1 explicitly forbids this.

### Mouse-Event Cost

Unquantized solving and VBO invalidation on every raw event would cause
unnecessary work. Grid-cell caching is a functional requirement, not merely
an optimization.

### Degenerate Geometry

Points near the start tangent or directly behind it can make analytic radius
calculations unstable. Straight tolerances, minimum radius, bounded angles,
and simulation validation are mandatory.

## Decisions Captured

- `Y` starts live-flex only for an eligible selected DynTrack; other objects
  keep normal resize behavior.
- Live preview follows mouse movement without requiring a held button.
- The DynTrack placement pose remains the fixed start pose.
- Free-end v1 uses straight or one circular curve only.
- The analytic tangent-circle solution is preferred over an ending-angle
  brute-force search.
- Arcs greater than 180 degrees are rejected.
- Endpoint snapping uses `Game::snapableRadius` and exact TDB endpoint poses.
- Pointer grid initially uses `Game::DefaultMoveStep`.
- Existing pose-to-pose Flex is reused for endpoint connections.
- Already committed/TDB DynTracks cannot be live-flexed in v1.
- One live-flex gesture creates one undo operation.

## Possible Follow-Ups

- Dedicated flexible-track grid and minimum-radius settings.
- Radius/length HUD near the pointer.
- Compound free-end paths when no single arc is valid.
- Automatic multi-DynTrack creation for long routes.
- Editing committed DynTracks with safe TDB removal/reinsertion.
- Road support.
- Easements and transition curves.
