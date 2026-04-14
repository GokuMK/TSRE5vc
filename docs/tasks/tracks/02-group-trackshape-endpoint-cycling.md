# Task 02 - Group Track Layout Endpoint Cycling With Synthetic `TrackShape`

## Objective
Add `X` endpoint cycling for selected `GroupObj` track layouts by building a temporary in-memory `TrackShape` that represents the whole grouped layout.

Primary goal:
- when a grouped track layout is snapped to TDB, pressing `X` should cycle only valid external layout routes
- internal joins inside the group must not be selectable as join endpoints

This task note captures the current behavior, the preferred design, the intended implementation boundaries, and the v1 scope limits.

## Status
### Current State
- Design reviewed only.
- No runtime code changed yet.
- Preferred implementation direction agreed:
  - build a synthetic in-memory `TrackShape`
  - refactor `TDB::findPosition(...)` to accept `TrackShape*`
  - reuse existing TDB snapping logic for group endpoint cycling

### Intentionally Out Of Scope For V1
- `dyntrack` endpoint extraction
- mixed rail/road grouped snapping logic
- preserving support for arbitrary disconnected track islands inside one group
- extracting topology by building a temporary `TDB`
- full persistent editor/runtime storage of synthetic grouped shapes

## Current Behavior
Pressing `X` in the editor goes through:
- `RouteEditorGLWidget` key handling
- `Route::flipObject(...)`
- `Route::newPositionTDB(...)`
- `TDB::findPosition(...)`

Relevant code:
- `src/routeEditor/RouteEditorGLWidget.cpp:1392`
- `src/tsre/world/Route.cpp:2308`
- `src/tsre/world/Route.cpp:2045`
- `src/tsre/tdb/TDB.cpp:1590`

For a normal track object, `X` does not just visually flip the object.
It advances `defaultEnd`, and `TDB::findPosition(...)` uses that to choose:
- which `TrackShape::path[i]` is used
- which end of that path is considered the joining end

So the real behavior is:
- cycle over `numpaths * 2`
- recompute position and rotation from the chosen path end

This is important because the grouped version must preserve the same semantics.

## Why Group Support Is Harder
`GroupObj` may contain many `trackobj` children.

A grouped layout can have:
- internal joins between children
- several valid external endpoints
- branch layouts where one external connection point may belong to multiple possible routes

So we cannot simply:
- look at all child endpoints
- expose every endpoint independently

That would allow invalid internal snapping and would also lose path-level information for junction-like layouts.

## Preferred Design
Preferred design:
- synthesize a temporary `TrackShape` for the selected `GroupObj`
- reuse `TDB::findPosition(...)` to compute snapped placement and rotation
- reuse existing `defaultEnd` cycling semantics for `X`

This is preferred over hand-written group transform math because:
- `findPosition(...)` already encodes the current anchor behavior
- `findPosition(...)` already handles path-end selection consistently with normal track objects
- it avoids inventing a second snapping rule just for groups

## Why Not Build A Temporary `TDB`
A temporary `TDB` was considered, but is not the preferred v1 path.

Reason:
- `TDB::placeTrack(...)` is good at consuming a finished `TrackShape`
- it is not a natural tool for discovering a grouped shape from many child shapes

Using a temporary `TDB` would still require us to:
- place all child pieces with correct relative transforms
- infer which temporary nodes form one grouped route
- recover `TrackShape::path[]` data from that graph

That is likely more complex and harder to debug than building the synthetic `TrackShape` directly.

## TrackShape Facts Confirmed In Code
`TrackShape::path[]` already stores the information we need:
- `n`
- `pos[3]`
- `rotDeg`
- `sect[]`

This is loaded from `tsection.dat` and not computed later.

Relevant code:
- `src/tsre/tdb/TSectionDAT.cpp:136`
- `src/tsre/tdb/TrackShape.h:21`

This means the grouped synthetic shape can be built by transforming existing child path data into a common group-local frame.

## Important Existing Semantics
### `path[0]` As Internal Reference
`TDB::findPosition(...)` does not use a true global shape origin.
It uses `path[0]` as a reference when computing the final translation.

Relevant code:
- `src/tsre/tdb/TDB.cpp:1663`
- `src/tsre/tdb/TDB.cpp:1667`

Implementation consequence:
- the synthetic shape must define a stable `path[0]`
- its `pos` and `rotDeg` must be valid and deterministic

### Paths Matter More Than Unique Endpoints
Current snapping cycles over `numpaths * 2`, not over a deduplicated set of physical outside endpoints.

Implementation consequence:
- the grouped synthetic shape must preserve route alternatives
- output should be route paths, not only a set of visible connector points

## V1 Scope Decision
V1 should support only grouped `trackobj` layouts.

Rules:
- ignore `dyntrack` children during grouped shape building
- ignore non-track children for endpoint discovery
- non-track children may still move with the group as usual
- if no valid grouped `TrackShape` can be built, grouped `X` cycling does nothing
- disconnected track islands are allowed in v1
- multiple independent route families should be represented as multiple synthetic paths, similar to ordinary multi-path shapes such as double track

## Proposed Architecture
### 1. Add `TrackShape*` Overload
Refactor `TDB::findPosition(...)`:
- new core overload takes `TrackShape* shp`
- current `sectionIdx` overload becomes a wrapper:
  - resolve `TrackShape*` from `sectionIdx`
  - call the new overload

This keeps existing behavior intact for normal objects while making synthetic grouped shapes usable.

Possible future symmetry:
- `placeTrack(...)` could later get the same style overload if grouped placement ever needs it

### 2. Build Synthetic Group Shape
Add helper logic that builds a temporary `TrackShape` from the selected `GroupObj`.

Suggested ownership:
- produced on demand
- owned by route/editor helper code, not persisted in `tsection`
- not written to disk

The helper should output:
- `TrackShape` with `numpaths`
- one synthetic `path[]` entry per valid external-to-external route
- stable ordering for deterministic `X` cycling

### 3. Use Synthetic Shape For Group `X`
When selected object is a `GroupObj`:
- build or refresh synthetic grouped shape
- advance `defaultEnd`
- call the new `findPosition(..., TrackShape* shp)` path
- apply the returned transform to the whole group

## Proposed GroupShape Builder
### Input
- selected `GroupObj`
- child `trackobj` objects only
- `TSectionDAT` geometry helpers

### Output
- temporary `TrackShape`

### High-Level Algorithm
1. Collect child tracks that participate.
2. For each child track:
   - resolve `childShp = tsection->shape[child->sectionIdx]`
   - inspect every `childShp->path[i]`
3. For each child path:
   - compute start connector pose from `path[i].pos` and `path[i].rotDeg`
   - compute end connector pose by walking `sect[]` using existing section geometry
4. Transform all connector poses into one common group-local frame.
5. Match overlapping connector poses from different child objects with tolerance.
6. Mark matched connector pairs as internal joins.
7. Treat unmatched connectors as external connectors.
8. Build a graph where:
   - nodes are connector sites
   - edges are child path traversals with ordered `sect[]`
9. Enumerate valid routes between external connectors through that graph.
10. Convert each route into one synthetic `TrackShape::SectionIdx`.

Important:
- the graph may contain multiple disconnected components
- that is acceptable as long as each valid external-to-external route becomes a synthetic path

## Reuse Boundary
### Reuse Existing Code / Concepts
- `TrackShape` data loaded from `tsection.dat`
- `TSection::getDrawPosition(...)`
- `TSection::getAngle()`
- `TDB::findPosition(...)`

### Do Not Reuse By Spawning Temporary TDB
- no temporary node graph
- no temporary `placeTrack(...)`
- no temporary synthetic database merge/extract step

## Details For Building One Synthetic Path
Each synthetic path should contain:
- `pos[0..2]`
  - the start connector position in grouped shape local space
- `rotDeg`
  - the start connector heading in grouped shape local space
- `sect[]`
  - the ordered concatenation of sections encountered along the route
- `n`
  - number of sections in the route

Important:
- orientation matters
- when traversing a child path from its far end back toward its start, the section list cannot always be reused blindly

This point must be checked carefully during implementation.
If existing MSTS `TrackShape` data assumes route directionality, reversed traversal may require:
- choosing only routes that already exist in the correct direction
- or synthesizing reversed section definitions where valid

This is one of the main areas to verify in code while implementing.

## Deterministic Ordering
The synthetic `path[]` list should be stable so `X` cycles predictably.

Recommended ordering:
- sort by start connector position
- then by start heading
- then by total section count or route length

Exact tie-break can be implementation-defined, but must be deterministic.

## Reference Frame Choice
Use a stable group-local frame.

Recommended v1 approach:
- choose a deterministic grouped reference
- ensure synthetic `path[0]` is stable

Possible reference choices:
- first external route in deterministic ordering
- group pivot if it maps cleanly to local connector data

The simplest safe option for v1 is:
- build all grouped connector positions in one consistent local frame
- assign synthetic `path[0]` from the first route after deterministic sort

## Geometry Helpers Likely Needed
Suggested helper responsibilities:
- compute end pose of a `TrackShape::SectionIdx`
- transform a child path pose into group-local coordinates
- compare connector poses within tolerance
- enumerate graph routes from external connector to external connector

These helpers can live near `Route` or under TDB-related utilities, depending on where the least coupling is achieved.

## Route / Editor Touch Points
Likely code areas to change:
- `src/tsre/tdb/TDB.h`
- `src/tsre/tdb/TDB.cpp`
- `src/tsre/world/Route.cpp`
- `src/tsre/world/objects/GroupObj.h`
- `src/tsre/world/objects/GroupObj.cpp`
- possibly `src/routeEditor/RouteEditorGLWidget.cpp` only if editor-side key behavior or refresh needs adjustment

Likely behaviors to review:
- `Route::flipObject(...)`
- `Route::dragWorldObject(...)`
- grouped snapping while dragging with stick-to-target enabled

## Dragging Implication
Today, grouped dragging returns early and behaves like plain group transform.

Relevant code:
- `src/tsre/world/Route.cpp:1475`

Implementation consequence:
- if grouped endpoint snapping is added only to `X`, the drag experience may still feel inconsistent
- grouped drag snapping should eventually use the same synthetic grouped shape and chosen current endpoint

This may be part of the same task or a follow-up, depending on implementation size.

## Risks
### Route Reversal Semantics
Reversing a child route may not be as simple as reversing `sect[]`.
This needs careful verification.

### Cycles / Loops
Layouts with cycles may produce many possible routes.
V1 should keep route enumeration conservative.

### Disconnected Layouts
Disconnected track islands are allowed in v1.
This should behave similarly to ordinary shapes with multiple independent paths.
The main requirement is still deterministic path generation and stable ordering.

### Tolerance Matching
Connector matching needs numeric tolerance to avoid tiny transform drift causing missed joins.

## Suggested Implementation Steps
1. Add `TDB::findPosition(..., TrackShape* shp)` and wrap the old overload around it.
2. Add a small helper that computes start/end connector poses for one existing child `TrackShape::path`.
3. Build grouped connector matching and detect internal vs external connector sites.
4. Build a route graph for grouped `trackobj` children.
5. Enumerate valid external-to-external routes and emit a synthetic grouped `TrackShape`.
6. Add grouped branch in `Route::flipObject(...)` using the synthetic shape.
7. Decide whether grouped drag snapping should be updated in the same task or left for a follow-up.
8. Test with:
   - straight chain
   - turnout / 3-way style layout
   - crossover-like layout
   - group with no valid external route
   - group containing ignored non-track children

## Acceptance Criteria
- Selecting a supported grouped track layout and pressing `X` cycles only valid external grouped routes.
- Internal joins are never used as snap candidates.
- Resulting snapped rotation and position match normal single-track endpoint cycling semantics.
- Behavior is deterministic between repeated selections.
- Unsupported groups fail safely without corrupting selection or placement.

## Notes From Review
- `path[0]` is used as a reference point in current snapping logic rather than a guaranteed endpoint at shape origin.
- Existing `TrackShape` examples with multiple `SectionIdx(...)` entries confirm that path definitions are route-based, not endpoint-based.
- The section list is the most important payload, but valid grouped `pos` and `rotDeg` still need to be synthesized correctly.
