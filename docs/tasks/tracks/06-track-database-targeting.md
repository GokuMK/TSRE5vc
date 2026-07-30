# Task 06 - Track Database Targeting

## Objective

Make `DynTrackObj` and Flex tools target an explicit track database so the same
geometry workflow can create rail or road networks.

Implement only the MSTS rail TDB and road RDB now. Keep the boundary capable of
accepting a database identity later, but do not rewrite the project around a
general multi-database registry as part of this task.

## Status

Design adjusted after review. Deferred until the first TSRE and Open Rails
profile-selection milestones in Tasks 07 and 08 are complete.

Implementation started with an isolated ordinary-placement milestone:

- route and addon REF files remain authoritative for existing Dyntrack entries;
- existing unmarked Dyntrack REF entries are treated as rail;
- TSRE supplies missing rail and road Dyntrack list entries after REF merging;
- the road entry carries `StaticFlags ( 00100100 )` in memory;
- the Dyntrack properties panel reports its resolved TDB/RDB owner;
- ordinary placement and `Z` insertion resolve road Dyntrack to RDB;
- the selected-object Flex button resolves its endpoint lookup from the
  Dyntrack owner, so road objects use RDB without changing Continuous Flex;
- newly placed road Dyntrack explicitly requests
  `ShapeTemplate ( default_road_single )`;

Continuous Flex now exposes adjacent `FLEX TRACK` and `FLEX ROAD` buttons.
Road mode marks the main and companion Dyntracks as road, snaps preview only
to RDB endpoints, inserts accepted segments into RDB, and continues with the
same owner. Road mode requests the route-local `default_road` profile. The
continuous main profile is a crowned three-metre lane using `road.ace`,
raised above its datum to clear terrain and with no road superelevation.
Profile discovery accepts the established `TrProfile*` convention plus
reserved `default_*` profile IDs, allowing a future `default_track` without
renaming the file.

Native MSTS road DynTrack was not available as a complete editor feature, so
the task is not blocked on reproducing one in the native Route Editor.

## StaticFlags Convention

Review `TrackObj`, not DynTrack, to establish the existing rail/road
convention. The normal values are:

```text
Rail/TDB TrackObj: StaticFlags ( 00200180 )
Road/RDB TrackObj: StaticFlags ( 00200100 )
```

This was checked against the supplied:

```text
C:\trainsim\routes\CMK\WORLD
```

For strictly named current `.w` tiles, the shape/flag counts are:

```text
rail shape + 00200180: 22,309
rail shape + 00200100:    469
road shape + 00200100:  6,513
road shape + 00200180:    791
```

The minority combinations do not establish which editor produced them. TSRE
and third-party tools may not preserve flags consistently. Current static
`TrackObj` routing through `RoadShape` can remain unchanged for this task;
using the flags more accurately is optional future work, not a road Flex
acceptance requirement.

Open Rails' `StaticFlag` enum documents:

```text
RoundShadow, RectangularShadow, TreelineShadow, DynamicShadow
Terrain, Animate, Global
```

It does not name `0x80`, `0x100`, or DynTrack's `0x00100000`. Its scenery
loader treats `TrackObj` as global independently of `StaticFlags`, while wire
and superelevation code use `TrackShape.RoadShape`. Open Rails therefore also
does not currently use these low bits to select a database.

Reference:

- [Open Rails `StaticFlag` enumeration documentation](https://james-ross.co.uk/projects/or/documentation/Orts.Formats.Msts~Orts.Formats.Msts.StaticFlag.html)

All 1,346 sampled CMK DynTrack records use:

```text
StaticFlags ( 00100000 )
```

Road DynTrack does not exist in legacy content, so TSRE can introduce the first
road DynTrack convention without reinterpreting any existing object:

```text
Rail DynTrack: StaticFlags ( 00100000 )
Road DynTrack: StaticFlags ( 00100100 )
Road marker:   0x00000100
```

The road marker is interpreted only for `DynTrackObj`. Do not generalize this
single-bit test to every world-object class.

Initial implementation must:

- preserve `00100000` for every normal and legacy rail DynTrack;
- add `0x00000100` only when creating/saving a road DynTrack;
- resolve a DynTrack with that bit set to RDB and one without it to TDB;
- copy the full flag value through placement, companions, clone, and undo;
- make Open Rails milestone B use the same DynTrack-specific test.

The generated fixture can be tested in TSRE and Open Rails. Native MSTS testing
is optional historical compatibility evidence, not an implementation gate.

## Minimal Data Model

Introduce stable database identity rather than passing `bool road`.

Initial concept:

```text
TrackDatabaseId
    0 = MSTS rail
    1 = MSTS road

TrackNetworkKind
    Rail
    Road
```

`TrackDatabaseId` is identity; `TrackNetworkKind` supplies behavioral defaults.
Use one narrow resolver boundary, for example:

```text
databaseForWorldObject(obj)
databaseForId(id)
```

That resolver may map IDs 0 and 1 to `Game::trackDB` and `Game::roadDB`.
Avoid scattering new boolean branches, but do not introduce descriptors,
custom persistence, or registry lifecycle work yet.

## DynTrack Ownership

Every `DynTrackObj` needs a resolved database identity available immediately
after construction, before it is in any database.

Resolution order:

1. DynTrack road bit in `StaticFlags`, when loading;
2. selected Flex tool network for a newly created object;
3. rail fallback for every legacy object without the road bit.

The database ID must be copied by:

- clone/copy;
- normal placement;
- continuous next-segment placement;
- left/right companion creation;
- undo snapshots;
- network-aware REF placement.

The identity survives TSRE save/load through `StaticFlags`. Do not add a
second world-object token, route sidecar, or IDs above 1 in this task.

## Required Routing Changes

Replace direct database selection in:

- `Route::addToTDB(...)`;
- `Route::addToTDBIfNotExist(...)`;
- `Route::toggleToTDB(...)`;
- membership and removal helpers;
- `TDB::fillDynTrack(...)` call sites;
- live Flex endpoint search;
- `Flex::AutoFlex(...)` or its replacement endpoint API;
- DynTrack render-time TSection access;
- superelevation lookup;
- terrain-to-track helpers where object database matters;
- continuous Flex main and companion acceptance;
- undo/database snapshots;
- editor database overlays and diagnostics where filtering is required.

Deletion may continue to probe all databases as a recovery mechanism, but
normal operations should use the resolved owner first and warn if the object
is found elsewhere.

## Existing Ruler Proof Of Concept

`RulerObj::createRoadPaths()` already invokes:

```text
roadDB->fillDynTrack(...)
roadDB->placeTrack(...)
```

for straight `TSection` geometry. Treat this as evidence that the core TDB
writer is database-agnostic enough for an initial road Flex implementation.
Reuse the backend calls, not the Ruler's surrounding design.

Road Flex must improve on Ruler by:

- saving a real DynTrack world object for every accepted segment;
- giving each object explicit database identity;
- using tested Flex endpoint/quaternion conversion;
- supporting curves and multi-section shapes;
- normalizing tile/local coordinates;
- providing undo/error handling;
- preventing duplicate insertion.

After Task 06 is stable, Ruler may call a common low-level
`placeSections(databaseId, owner, pose, sections)` service. Do not make
DynTrack depend on `RulerObj`, and do not make Ruler the general database
identity model.

## Dynamic TSection Requirements

Keep geometry-based reuse. Local dynamic `TrackShape` and `TSection`
definitions do not own TDB/RDB identity, and identical rail and road geometry
may share them. The existing Ruler implementation proves that RDB can consume
the same `fillDynTrack(...)` output.

Database identity belongs to object placement, endpoint lookup, membership,
and vector ownership. Set `RoadShape` only if a specific serialized
`TrackShape` use requires it; do not add it to cache identity or create
duplicate sections by default.

## Flex Tool Behavior

Add a network selector to the continuous Flex options.

For v1:

- values are `Rail` and `Road`;
- Rail is the default;
- a segment snaps only to endpoints in the selected database;
- starting from an existing endpoint adopts that endpoint's database;
- companions always inherit the main segment's database;
- mixed rail/road companion ways are out of scope;
- switching network while an unfinished segment exists cancels and recreates
  that segment rather than mutating it in place;
- switching a committed object is forbidden.

The geometry solvers remain database-independent. Pass endpoint poses into
them; do not give `Flex::NewFlex(...)` global database access.

## Multilane Road Profile Families

The initial route test family is now implemented as:

```text
default_road          main three-metre Flex Road lane
default_road_single   standalone four-metre road
default_road_left     left role of a three-metre lane group
default_road_middle   internal role of a three-metre lane group
default_road_right    right role of a three-metre lane group
```

The three-metre main profile preserves both outside portions of `road.ace`
instead of scaling or cropping its outside edges. It uses two polylines that
meet at the road centre: one samples U `0..0.375`, the other `0.625..1`.
The unused middle U range is therefore removed without overlapping geometry
or stretching either 1.5 m half. This retains both road sides at the same
visual scale as the left and right lane roles.

The grouped left, middle, and right profiles sample U ranges `0..0.75`,
`0.125..0.875`, and `0.25..1`, respectively. U is written in reverse
profile-X order to account for TSRE's profile-to-shape X orientation;
otherwise both side strips appear at internal lane seams. Longitudinal V
advances by `1/6` per metre, giving a six-metre texture repeat.

Every three-metre role uses the same cross-section: 4 cm above the Dyntrack
datum at both edges and a shallow 6 cm centre crown. This keeps longitudinal
connections compatible as a segment changes between main, left, middle, or
right roles. Adjacent lanes meet at equal-height shared edges and have no
overlapping surface area.

An optional `default_road_marked` family uses `road2lane.ace`. Its
three-metre main profile uses the same split construction as `default_road`:
U `0..0.26` and `0.76..1` preserve both solid outside markings while
discarding the dashed centre marking. These asymmetric retained widths match
the U-per-metre scales of the marked left (`0..0.52` over 3 m) and right
(`0.52..1` over 3 m) roles, keeping both solid edge lines the same apparent
thickness. `default_road_marked_single` preserves the original full-width,
two-lane texture.

The grouped marked left and middle roles end at U `0.52`; the right role
begins there, so the lane immediately left of each internal seam owns the
dashed divider. The middle role starts at U `0.08` to omit the outside solid
edge while retaining approximately one lane's texture scale.

This sampling overlap exists only in UV space; the meshes still meet at one
exact edge and cannot z-fight. The marked family is currently available for
manual `ShapeTemplate` testing; FLEX ROAD continues to resolve the unmarked
family until a family selector is implemented.

Commit-ready example files for both families are stored in
`docs/examples/track-profiles/`. Copy the desired files into the route's
`TrackProfiles` directory; route-local files remain the runtime authority.

FLEX ROAD resolves these roles automatically and locks companion separation
to the family's three-metre lane width. Ordinary standalone road placement
uses `default_road_single`.

Future profile-family UI should select the family, not four unrelated raw profiles.
Each generated Dyntrack must nevertheless persist the resolved role profile
in its own `ShapeTemplate`, so save/reload does not depend on reconstructing
the original placement session.

Role assignment for the current main-plus-companions tool is deterministic:

```text
main only:             main = default/main
main + right:          main = left,   right = right
left + main:           left = left,   main = right
left + main + right:   left = left,   main = middle, right = right
```

Additional lanes can reuse `_middle`. Numeric lane counts per side and
persisted corridor/group ownership are later work.

All profiles in one family must share:

- lane width and centerline separation;
- surface height;
- `ChordSpan`, pitch control, and subdivision settings;
- longitudinal texture scale;
- compatible start/end treatment.

The top surface of one lane should end exactly where the next begins. Do not
add overlapping coplanar asphalt strips to hide seams: they will flicker.
Only `_left` and `_right` should add their outward shoulder, verge, kerb, or
side wall; `_middle` must not add outer-edge geometry.

For the first version, bake lane markings into each role texture. Separate
marking meshes need an explicit decal/depth-bias render path before use;
arbitrary vertical offsets are not a stable general solution.

Seamless multilane placement should lock separation to the profile family's
lane width. Arbitrary separation remains useful for independent parallel
roads, but then gaps or overlaps are intentional and the tool must not call
the result a seamless multilane corridor.

Parallel curves can share a seam when all roles use the same angular
subdivision and companion radii are derived from the same centerline.
Placement must reject an inner offset that makes its radius invalid. Merges,
lane-count transitions, intersections, and junction surface infill require
dedicated transition objects or generators and are outside this profile
suffix convention.

## Future Database Extensibility

More than two databases is explicitly out of scope. The only requirement now
is not to make a future extension needlessly harder:

- pass a small database ID through the new DynTrack/Flex boundary;
- centralize ID-to-`TDB` resolution;
- avoid new assumptions that every nonzero value means road;
- reject unsupported IDs explicitly.

Do not audit or rewrite unrelated signals, server messages, track items, undo,
or persistence until a real custom-database task exists.

## Failure Handling

- Missing selected database: refuse placement with a clear message.
- Object recorded in the wrong database: report both expected and actual
  database IDs.
- Same object found in multiple databases: block mutation until repaired.
- Unsupported database ID: fail explicitly.

## Tests

### Pure/Headless

- Existing `00100000` DynTracks resolve to rail.
- New `00100100` DynTracks resolve to road.
- Toggling only the DynTrack road bit changes the resolved database.
- Road identity resolves to RDB after save/reload without an extra token.
- DynTrack clone and undo snapshot preserve database ID.
- Database lookup supports IDs 0 and 1 and rejects unsupported IDs.
- Rail and road may reuse identical dynamic TSection geometry.
- Flex endpoint selection never returns an endpoint from another database.

### Route Integration

- Rail continuous Flex behavior is unchanged.
- Road continuous Flex inserts main and companions into RDB only.
- A road Flex straight produces equivalent RDB section data to a matching
  Ruler road span, while retaining its own DynTrack world owner.
- `Z` toggles the selected owner database.
- Removing road DynTrack does not touch an unrelated rail object with the same
  `UiD`.
- Save/reload preserves owner and geometry.
- RDB rebuild retains the road DynTrack.

### Compatibility

- Open Rails loads it without an exception.
- Rail operation and road traffic paths remain valid.
- If native MSTS becomes available, record whether it retains the experimental
  road DynTrack; this is not an implementation gate.

## Acceptance Criteria

- `DynTrackObj` has one authoritative database identity.
- Rail DynTrack remains `00100000`; road DynTrack is `00100100`.
- Static `TrackObj` routing is not changed by this task.
- No Flex/DynTrack placement or rendering path hardcodes `Game::trackDB`.
- Standard IDs 0/1 round-trip through TSRE save/load.
- Continuous rail and road Flex can coexist in one route.
- Rail snapping cannot accidentally connect to road endpoints.
- Shared dynamic TSection definitions do not confuse TDB/RDB vector ownership.
- New code has one database resolver boundary and does not attempt a general
  registry rewrite.
- Unconfirmed native MSTS flag semantics are documented rather than guessed.
