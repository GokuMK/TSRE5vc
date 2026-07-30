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
