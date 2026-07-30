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

## StaticFlags Evidence And Remaining DynTrack Question

Review `TrackObj`, not DynTrack, to establish the existing rail/road
convention. The normal values are:

```text
Rail/TDB TrackObj: StaticFlags ( 00200180 )
Road/RDB TrackObj: StaticFlags ( 00200100 )
Candidate database bit: 0x00000080
Shared low bit:         0x00000100
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

The minority combinations show why TSRE must not infer database ownership from
`TrackShape.RoadShape`: shape type and intended database can differ. A direct
comparison with the current CMK databases found the same pattern strongly but
not perfectly correlated (`TDB`: 9,300 with `180`, 242 with `100`; `RDB`: 275
with `100`, 43 with `180`). Those disagreements are expected in a route edited
by code which currently chooses the database from `RoadShape`; they must be
reported by an audit tool rather than silently rewritten.

Open Rails' `StaticFlag` enum documents:

```text
RoundShadow, RectangularShadow, TreelineShadow, DynamicShadow
Terrain, Animate, Global
```

It does not name `0x80`, `0x100`, or DynTrack's `0x00100000`. Its scenery
loader treats `TrackObj` as global independently of `StaticFlags`, while wire
and superelevation code use `TrackShape.RoadShape`. Open Rails therefore also
does not currently apply the candidate database bit.

Reference:

- [Open Rails `StaticFlag` enumeration documentation](https://james-ross.co.uk/projects/or/documentation/Orts.Formats.Msts~Orts.Formats.Msts.StaticFlag.html)

DynTrack remains the format gap. All 1,346 sampled CMK DynTrack records use:

```text
StaticFlags ( 00100000 )
```

There is no native road DynTrack sample. Because legacy rail DynTrack does not
carry the static TrackObj `0x80` bit, absence of `0x80` cannot be reinterpreted
as road without breaking every existing route.

Initial implementation must therefore:

- preserve all existing `StaticFlags` values;
- keep `00100000` as the normal rail default;
- carry an explicit runtime `Rail`/`Road` database identity for new TSRE
  DynTracks;
- serialize that DynTrack identity in an explicit TSRE extension token, for
  example `TrackDatabase ( 0|1 )`, rather than overloading an unverified flag;
- make Open Rails milestone B read the same token;
- retain raw `StaticFlags` for compatibility and decode a native DynTrack rule
  later if reliable evidence appears.

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

## Static TrackObj Ownership

Correct the current `RoadShape`-only routing as part of this task, but protect
existing routes from silent migration:

1. New `TrackObj` placement derives TDB/RDB from the `0x80` StaticFlags
   convention.
2. A loaded object already referenced by exactly one database keeps that
   actual owner for mutation/removal.
3. If actual ownership disagrees with `StaticFlags`, report the world tile,
   `UiD`, flag value, shape type, and database; do not move it automatically.
4. If no database owns the object, use `StaticFlags`; use `RoadShape` only as a
   legacy fallback when the relevant flag information is absent or invalid.
5. Provide an explicit repair/reinsert action later instead of changing
   ownership merely by opening and saving a route.

Rendering may continue to use `RoadShape` where it describes visual or
rail-specific geometry behavior. It must not be treated as the authoritative
database owner.

## DynTrack Ownership

Every `DynTrackObj` needs a resolved database identity available immediately
after construction, before it is in any database.

Resolution order:

1. explicit TSRE database marker, when present;
2. selected Flex tool network for a newly created object;
3. a future confirmed `StaticFlags` rail/road rule;
4. conservative rail fallback for legacy objects.

The database ID must be copied by:

- clone/copy;
- normal placement;
- continuous next-segment placement;
- left/right companion creation;
- undo snapshots;
- network-aware REF placement.

The identity must survive TSRE save/load for IDs 0 and 1. Prefer a compatible
world-object token or confirmed flag encoding; do not introduce a route-wide
sidecar or IDs above 1 in this task.

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
- Ambiguous legacy DynTrack: use rail only as a documented compatibility
  fallback and warn once per object.
- Object recorded in the wrong database: report both expected and actual
  database IDs.
- Same object found in multiple databases: block mutation until repaired.
- Unsupported database ID: fail explicitly.

## Tests

### Pure/Headless

- `TrackObj` `00200180` resolves to TDB when it has no existing owner.
- `TrackObj` `00200100` resolves to RDB when it has no existing owner.
- Existing database membership wins safely over a conflicting flag and emits
  an audit diagnostic.
- `RoadShape` is used only as the documented legacy fallback.
- Existing `00100000` DynTracks resolve to rail.
- Explicit new road identity resolves to RDB after save/reload.
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
- If native MSTS becomes available, confirm it ignores TSRE extension tokens
  and record whether it retains the experimental road DynTrack.

## Acceptance Criteria

- `DynTrackObj` has one authoritative database identity.
- New static `TrackObj` placement honors the `0x80` database convention.
- Existing flag/database disagreements are reported and never silently moved.
- No Flex/DynTrack placement or rendering path hardcodes `Game::trackDB`.
- Standard IDs 0/1 round-trip through TSRE save/load.
- Continuous rail and road Flex can coexist in one route.
- Rail snapping cannot accidentally connect to road endpoints.
- Shared dynamic TSection definitions do not confuse TDB/RDB vector ownership.
- New code has one database resolver boundary and does not attempt a general
  registry rewrite.
- Unconfirmed native MSTS flag semantics are documented rather than guessed.
