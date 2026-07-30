# Task 05 - Road Flex Research And Roadmap

## Objective

Extend the successful continuous **FLEX TRACK** workflow to roads while
preserving MSTS and Open Rails compatibility.

This is a research and design task. It records the current behavior, separates
the work into implementation tasks, and identifies compatibility questions
which require controlled route tests before code is changed.

The intended end state is:

- the same `DynTrackObj` geometry and placement workflow can target rail or
  road databases;
- Flex snapping searches only the selected database unless a future explicit
  cross-network tool says otherwise;
- rail and road DynTracks use appropriate procedural profiles;
- an explicitly selected TSRE template or Open Rails track profile overrides
  the database default;
- unresolved template names fall back safely instead of producing an invisible
  object;
- the internal database API can represent more than the two MSTS databases,
  while MSTS serialization continues to use its standard rail and road files.

## Status

Research complete enough to split implementation into Tasks 06-08. No runtime
code has been changed by this task.

One format detail remains deliberately unconfirmed: the project assumes that
MSTS uses a DynTrack `StaticFlags` bit to distinguish rail from road, but the
exact bit-level road/rail contract has not been verified against native MSTS.
TSRE currently initializes DynTrack with `0x00100000`; all DynTracks sampled
from the locally available routes use the same value. Open Rails parses this
value but does not assign a meaning to bit `0x00100000`.

Do not encode a road flag rule until the controlled MSTS fixture in Task 06 has
been completed.

## Current TSRE Findings

### Static `TrackObj`

Static track and road objects already share `TrackObj`. Database selection does
not use `StaticFlags`; it uses:

```text
TSectionDAT::isRoadShape(track->sectionIdx)
```

Relevant paths:

- `Route::addToTDB(...)`
- `Route::newPositionTDB(...)`
- `Route::setTerrainToTrackObj(...)`
- `TrackShape::roadshape`

This works because a static `TrackObj` has a `SectionIdx` whose `TrackShape`
can carry `RoadShape`.

### `DynTrackObj`

DynTrack is rail-only in several independent places:

- `Route::addToTDB(...)` always calls `trackDB->fillDynTrack(...)`;
- it then always calls `trackDB->placeTrack(...)`;
- live Flex snapping searches only `Game::trackDB`;
- `DynTrackObj` procedural rendering reads `Game::trackDB->tsection`;
- DynTrack superelevation angles come only from `Game::trackDB`;
- `Flex::AutoFlex(...)` uses rail endpoints;
- continuous companions inherit no database identity because none is stored.

`DynTrackObj` parses and saves `StaticFlags`, but no current code uses those
flags to select its database.

### Dynamic TSection Shapes

`TDB::fillDynTrack(...)` creates or reuses route-specific TSection entries and
a dynamic `TrackShape`. Its current lookup compares only geometry.

For road support this has two consequences:

- a rail and road DynTrack with identical geometry can accidentally share one
  dynamic `TrackShape`;
- newly created dynamic shapes set `dyntrack = true` but do not set
  `roadshape = true`.

The dynamic-shape cache identity must therefore include database/network kind,
and road dynamic shapes must be marked as road shapes where that remains part
of the compatibility model.

### Existing Ruler Road Paths

`RulerObj::createRoadPaths()` is an important existing proof of concept. For
each straight line between adjacent Ruler points it:

- creates a temporary `DynTrackObj` as a section-data container;
- calculates a straight length and orientation;
- calls `Game::roadDB->fillDynTrack(...)`;
- calls `Game::roadDB->placeTrack(...)`;
- records the Ruler tile and `UiD` as the owner of every generated vector.

`RulerObj::removeRoadPaths()` removes those vectors from RDB using the same
Ruler identity.

This proves that the shared dynamic TSection and `TDB::placeTrack` backend
already works with RDB. Task 06 does not need a second road-specific placement
algorithm.

It does not, however, solve road DynTrack:

- every Ruler span is one straight section rather than Flex geometry;
- one temporary `DynTrackObj` is reused and leaked;
- no `Dyntrack` world object is saved for any RDB span;
- the TSRE-only Ruler remains the database owner;
- there is no undo transaction or duplicate-creation guard;
- heading uses manual sign/`atan` math instead of the tested Flex coordinate
  boundary;
- all spans use the Ruler's origin tile/`UiD`;
- optional visible geometry is generated separately by `ProceduralShape` and
  is not the RDB object.

Open Rails may consume the resulting RDB for road traffic, but it cannot render
these spans as DynTrack because the corresponding DynTrack world objects do
not exist.

### Current Procedural Rendering

DynTrack rendering has one global switch:

```text
Game::proceduralTracks == true  -> ProceduralShape using templateName
Game::proceduralTracks == false -> ProceduralMstsDyntrack hardcoded mesh
```

Problems:

- an empty or `DEFAULT` template maps only to `DefaultTrack`;
- an unknown template silently returns an empty shape;
- `DISABLED` has no useful DynTrack behavior and also produces no shape;
- the selected template is not written by `DynTrackObj::save(...)`;
- the DynTrack properties panel has no template selector;
- the hardcoded fallback is rail-specific;
- the advanced template file declares `TRACK`/`ROAD` types, but the parser
  currently ignores the type value and assigns `DEFAULT`;
- `shapetemplates.dat` is loaded only from application data, while referenced
  OBJ and texture assets can be overridden by a route.

## Current Open Rails Findings

The local Open Rails source is at:

```text
C:\Users\pgade\Documents\NetbeansProjects\openrails
```

The tracked source baseline reviewed was commit
`91414172dea8f16c08e587f2792264110aaabab1`. The working tree contains unrelated
local changes, so a future agent should record its own baseline before
repeating or extending the review.

Open Rails:

- parses `DyntrackObj.StaticFlags`;
- does not use those flags to choose TDB versus RDB;
- creates a visual DynamicTrack for every DynTrack world object independently
  of database membership;
- always assigns dynamic track profile index `0` to DynTrack because it has no
  static shape name for profile matching;
- builds its superelevation map from the rail TDB only;
- safely renders a DynTrack without a matching superelevation section using
  the default profile.

The likely result for a road DynTrack written into RDB is therefore:

- the route and object should load;
- the road path remains usable by RDB consumers;
- the object is rendered with the default rail profile;
- it receives no road-aware profile selection;
- no direct crash path was found, but this must be verified with an actual
  mixed rail/road route.

Relevant Open Rails files:

- `Source/Orts.Formats.Msts/WorldFile.cs`
- `Source/RunActivity/Viewer3D/Scenery.cs`
- `Source/RunActivity/Viewer3D/DynamicTrack.cs`
- `Source/RunActivity/Viewer3D/SuperElevation.cs`
- `Source/Orts.Simulation/Simulation/SuperElevation.cs`

## Open Rails Track Profile Documentation

Documentation is already available locally:

```text
openrails/Source/Documentation/Online/
How to Provide Track Profiles for Open Rails Dynamic Track.docm
```

The current official PDF is also available online:

- [How to Provide Track Profiles for Open Rails Dynamic Track](https://static.openrails.org/files/OpenRails-Testing-How%20to%20Provide%20Track%20Profiles%20for%20Open%20Rails%20Dynamic%20Track.pdf)
- [Open Rails manuals and documents](https://www.openrails.org/learn/documents/)

No additional download is required for design work. The official document
should be retained as the behavioral reference because the local Open Rails
source may evolve.

## Proposed User Workflow

Keep **FLEX TRACK** as the geometry workflow and add an explicit network mode:

```text
Network: Rail | Road
Profile: Default | <TSRE templates> | <Open Rails profiles>
```

Recommended behavior:

- Rail remains the default to preserve existing projects.
- Road mode creates a DynTrack carrying road database identity from birth.
- Companions inherit network and profile identity.
- Endpoint snapping searches the chosen network only.
- `Z`/continuous acceptance inserts every object into its chosen database.
- The properties panel exposes the same network/profile identity for an
  uncommitted DynTrack.
- Changing network is forbidden after database insertion until safe
  remove/reinsert and undo support exists.

A second top-level **FLEX ROAD** button would be simpler initially, but it
duplicates tool state and becomes awkward when additional database types are
introduced. A network selector attached to one Flex tool is the preferred
long-term design.

## Profile Resolution Contract

Use one centralized, case-insensitive resolver:

1. If an explicit template/profile name is present:
   1. match a TSRE procedural template;
   2. otherwise match an Open Rails profile identifier;
   3. otherwise emit one warning and use the network default.
2. If no explicit name or `DEFAULT` is present:
   1. use the default rail profile for rail;
   2. use the default road profile for road.
3. During migration only, a named `LEGACY` mode may invoke the existing
   hardcoded rail generator.

For Open Rails profiles, the canonical identifier should be the file stem
(`TrProfile`, `TrProfileRoad`, and so on). The profile's internal `Name` may be
accepted as an alias only when unique.

`DISABLED` should not silently mean “invisible DynTrack.” Either remove it from
the DynTrack UI or reserve it as an explicit diagnostic no-mesh mode. It must
not be selected accidentally as a fallback.

## Additional Challenges

- Road curves may need a smaller configurable minimum radius than rail.
- Road profile geometry is normally wider and may require different terrain
  painting and clearance defaults.
- Ruler road paths need eventual migration to the same database-targeted path
  builder, or at least guards preventing duplicate insertion and unsafe
  geometry, but that cleanup is not a prerequisite for road Flex.
- Rail-only wire and superelevation processing must not run for roads.
- Texture lookup differs: TSRE procedural assets currently use
  `route/procedural`, while Open Rails profiles normally reference route
  textures and alternative texture rules.
- Profile caches must include profile identity, network kind, geometry,
  superelevation state, and any LOD-affecting settings.
- Generated geometry must retain the last complete mesh during rebuilds, as
  established by the live Flex work.
- Invalid profiles need warnings and deterministic fallback, not crashes or
  empty cached meshes.
- A mixed route can contain identical world-object `UiD` values in separate
  databases; membership checks must include database identity.
- Undo still does not transactionally capture dynamic TSection creation.
- MSTS and Open Rails may ignore TSRE-only `ShapeTemplate` metadata. If native
  MSTS rejects an extension token, TSRE profile choice needs a route sidecar
  rather than an extra DynTrack world-file token.
- More-than-two databases are a TSRE extension unless a separate serialization
  format is defined. Standard MSTS export remains limited to TDB and RDB.

## Task Breakdown

1. **Task 06 - Track Database Targeting And Registry**
   - establish the MSTS StaticFlags contract;
   - give DynTrack explicit database identity;
   - route all Flex/TDB operations through that identity;
   - introduce a registry API which does not hardcode only IDs 0 and 1.
2. **Task 07 - Procedural Track Profile Pipeline**
   - formalize default/forced/fallback behavior;
   - add Open Rails STF/XML profile parsing and generic cross-section sweep;
   - provide bundled default rail and road profiles;
   - retire the hardcoded DynTrack mesh after parity is demonstrated.
3. **Task 08 - Open Rails Road DynTrack Compatibility**
   - build a controlled mixed route;
   - verify current behavior in MSTS and Open Rails;
   - define or implement the smallest Open Rails change needed for road profile
     selection.

## Recommended Order

1. Complete the MSTS/ORTS compatibility fixture from Task 08 far enough to
   confirm the StaticFlags rule.
2. Implement Task 06 without changing rendering.
3. Verify rail Flex remains unchanged and road paths enter RDB.
4. Implement Task 07 behind an experimental profile backend.
5. Verify mixed rail/road routes in TSRE, MSTS, and Open Rails.
6. Only then remove the hardcoded default rail generator.

## Research Exit Criteria

- Every rail-only hardcoding affecting DynTrack is listed.
- Database identity has one proposed owner and one resolver boundary.
- Standard MSTS export and TSRE extension limits are explicit.
- Open Rails behavior is described from source, not assumed.
- Track profile documentation and source entry points are identified.
- Implementation is split so database correctness can be tested before the
  rendering backend changes.
