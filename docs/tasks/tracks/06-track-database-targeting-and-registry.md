# Task 06 - Track Database Targeting And Registry

## Objective

Make `DynTrackObj` and Flex tools target an explicit track database so the same
geometry workflow can create rail or road networks.

At the same time, replace new rail/road boolean branching with an internal
database registry capable of representing more than two network databases.
MSTS serialization remains limited to its standard rail TDB and road RDB.

## Status

Design ready. Blocked from implementation only by the native MSTS
`StaticFlags` compatibility fixture described below.

## Compatibility Rule To Establish First

The working project premise is that DynTrack database selection is encoded in
`StaticFlags`. TSRE currently defaults DynTrack to `0x00100000`; Open Rails
parses this bit but gives it no name or database-selection behavior.

Create a minimal route in native MSTS Route Editor containing:

- one rail DynTrack;
- one road DynTrack, if native tooling permits it;
- one static rail `TrackObj`;
- one static road `TrackObj`.

For every object record:

- record `StaticFlags`, `SectionIdx`, tile, `UiD`, and track sections;
- locate the matching vector section in TDB or RDB;
- rebuild both databases in MSTS and repeat the inspection;
- test whether changing only the suspected flag moves or drops DynTrack during
  a native rebuild.

If MSTS cannot create a road DynTrack through its UI, build two copies of the
fixture and change one candidate bit manually, then observe rebuild behavior.
Do not test this first on a valuable route.

The result must be written into this task before implementation:

```text
Rail DynTrack flag rule:
Road DynTrack flag rule:
Unknown/reserved combinations:
Native MSTS result:
```

## Proposed Data Model

Introduce stable database identity rather than passing `bool road`.

Suggested concepts:

```text
TrackDatabaseId
    0 = MSTS rail
    1 = MSTS road
    2+ = TSRE/extension databases

TrackNetworkKind
    Rail
    Road
    Custom

TrackDatabaseDescriptor
    id
    kind
    displayName
    persistence format/path
    TDB pointer
```

`TrackDatabaseId` is identity; `TrackNetworkKind` supplies behavioral defaults.
Do not use `id == 1` as a synonym for every road behavior.

`Route` should own the registry and expose:

```text
database(id)
defaultDatabase(kind)
databaseForWorldObject(obj)
databases()
```

`Game::trackDB` and `Game::roadDB` can remain temporary compatibility aliases
to registry IDs 0 and 1. New Flex and DynTrack code must not depend on them.

## DynTrack Ownership

Every `DynTrackObj` needs a resolved database identity available immediately
after construction, before it is in any database.

Resolution order:

1. explicit TSRE runtime/sidecar database ID, when present;
2. native MSTS `StaticFlags` rail/road rule;
3. selected Flex tool network for a newly created object;
4. conservative rail fallback with a warning for ambiguous legacy objects.

The database ID must be copied by:

- clone/copy;
- normal placement;
- continuous next-segment placement;
- left/right companion creation;
- undo snapshots;
- network-aware REF placement.

The identity must survive save/load. For IDs 0 and 1, native MSTS fields are
preferred. IDs above 1 require TSRE sidecar metadata or a future extended
format and cannot be promised as MSTS-compatible.

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

## Dynamic TSection Requirements

Change dynamic-shape lookup so geometry is not the sole identity.

At minimum include:

- database/network kind;
- ordered section types;
- straight lengths;
- curve angles and radii.

When a dynamic road shape is created:

- mark it as dynamic;
- mark it as road where the route TSection format supports `RoadShape`;
- ensure reuse cannot return a rail dynamic shape with identical geometry;
- ensure both TDB and RDB can reference the shared route TSection definitions.

Do not create duplicate section records merely because a road and rail shape
share the same primitive geometry unless the file format requires it.

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

## More Than Two Databases

The registry should allow IDs above 1 internally, but this task does not invent
an MSTS file format for them.

Initial extension behavior:

- load IDs 0 and 1 from normal `.tdb` and `.rdb`;
- allow test/in-memory custom databases through the registry;
- avoid `if (id == 0) ... else if (id == 1)` in new code;
- make unsupported persistence explicit;
- never silently export a custom database as rail or road.

Existing track-item world formats store a database ID beside item IDs in
several object types. Audit all two-value assumptions before enabling custom
database persistence, especially `SignalObj`, `Route::getTrackItem`, server
messages, undo, and TDB client synchronization.

## Failure Handling

- Missing selected database: refuse placement with a clear message.
- Ambiguous legacy DynTrack: use rail only as a documented compatibility
  fallback and warn once per object.
- Object recorded in the wrong database: report both expected and actual
  database IDs.
- Same object found in multiple databases: block mutation until repaired.
- Unsupported custom database export: fail explicitly.

## Tests

### Pure/Headless

- StaticFlags fixtures resolve to the confirmed rail/road IDs.
- Unknown flags follow the documented fallback.
- DynTrack clone and undo snapshot preserve database ID.
- Registry lookup supports IDs 0, 1, and a test ID above 1.
- Missing and duplicate IDs fail predictably.
- Dynamic shape keys distinguish rail and road with identical geometry.
- Road dynamic shapes retain the road marker.
- Flex endpoint selection never returns an endpoint from another database.

### Route Integration

- Rail continuous Flex behavior is unchanged.
- Road continuous Flex inserts main and companions into RDB only.
- `Z` toggles the selected owner database.
- Removing road DynTrack does not touch an unrelated rail object with the same
  `UiD`.
- Save/reload preserves owner and geometry.
- RDB rebuild retains the road DynTrack.

### Native Compatibility

- MSTS loads and rebuilds the controlled route.
- Open Rails loads it without an exception.
- Rail operation and road traffic paths remain valid.

## Acceptance Criteria

- `DynTrackObj` has one authoritative database identity.
- No Flex/DynTrack placement or rendering path hardcodes `Game::trackDB`.
- Standard IDs 0/1 round-trip through compatible MSTS fields.
- Continuous rail and road Flex can coexist in one route.
- Rail snapping cannot accidentally connect to road endpoints.
- Dynamic TSection shapes cannot cross-reuse rail/road identity incorrectly.
- Registry APIs accept custom internal database IDs without changing MSTS
  export semantics.
- All compatibility decisions are backed by the controlled fixture.

