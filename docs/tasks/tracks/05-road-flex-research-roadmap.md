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
- the new database-selection boundary does not make a future third database
  unnecessarily difficult, while this work implements only MSTS TDB and RDB.

## Status

Research complete enough to split implementation into Tasks 06-08. No runtime
code has been changed by this task.

The supplied CMK `TrackObj` records confirm the familiar static values:
normal rail records use `00200180`, while normal road records use
`00200100`. Existing files are not reliable enough to audit ownership from
flags alone because TSRE and third-party tools may not preserve the convention.
Changing static `TrackObj` routing is not required for road Flex.

Road DynTrack has no legacy-content ambiguity because it does not exist in
normal MSTS routes. Define a TSRE extension within `StaticFlags`:

```text
Rail DynTrack: 00100000
Road DynTrack: 00100100
Road marker:   00000100
```

Legacy DynTrack remains rail. Only newly created road DynTrack receives the
road marker.

## Current TSRE Findings

### Static `TrackObj`

Static track and road objects already share `TrackObj`. Current TSRE database
selection ignores their `StaticFlags` convention and uses:

```text
TSectionDAT::isRoadShape(track->sectionIdx)
```

Relevant paths:

- `Route::addToTDB(...)`
- `Route::newPositionTDB(...)`
- `Route::setTerrainToTrackObj(...)`
- `TrackShape::roadshape`

This works for the normal content workflow because a static `TrackObj` has a
`SectionIdx` whose `TrackShape` can carry `RoadShape`.

For strictly named CMK world tiles, correlating `TrackObj.SectionIdx` with the
global `RoadShape` marker found:

```text
rail shape + 00200180: 22,309
rail shape + 00200100:    469
road shape + 00200100:  6,513
road shape + 00200180:    791
```

The minority combinations cannot be attributed to one editor: TSRE and
several third-party editing tools may create or preserve different flag
combinations. They are useful compatibility evidence, but not a reason to
migrate existing static objects.

Open Rails documents only the general `StaticFlag` members for shadows,
terrain, animation, and `Global`. It does not name `0x80` or `0x100`, treats
every `TrackObj` as global scenery regardless of the `Global` bit, and uses
`TrackShape.RoadShape` for wire/superelevation filtering.

Keep current static `TrackObj` behavior for this project. A later compatibility
task may use flags more accurately, but road Flex depends only on the new
DynTrack road marker.

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
a dynamic `TrackShape`. Its lookup compares geometry.

Sharing these definitions between rail and road is valid: local TSection
records describe geometry, not database ownership. The existing Ruler road
path implementation already calls `roadDB->fillDynTrack(...)` successfully
without road-specific TSection copies. TDB versus RDB ownership belongs to the
placed vector/object relationship, not to the shared section definition.

Do not add network kind to the dynamic-shape cache or duplicate identical
TSections merely because one consumer is road. A road marker is needed only
where a specific static `TrackShape` or compatibility format requires one.

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

Replace the boolean with three modes while preserving old configuration:

```text
Forced  (legacy true)
    Always generate procedurally.
    DISABLED -> explicit per-object static/hardcoded override.
    Empty/DEFAULT -> default procedural template/profile.
    Valid custom name -> selected procedural template/profile.
    Missing custom name -> warn and use the default procedural result.

Enabled
    Empty/DISABLED -> static shape, or DynTrack hardcoded mesh.
    DEFAULT -> default procedural template/profile.
    Valid custom name -> selected procedural template/profile.
    Missing custom name -> warn and use the static/hardcoded result.

Disabled (legacy false)
    Always use static shapes, or the DynTrack hardcoded mesh.
```

This resolves the missing-name ambiguity without allowing an object to become
invisible. Existing `true` maps to `Forced`; existing `false` maps to
`Disabled`. `Enabled` is the new opt-in per-object mode.


## Current Open Rails Findings

The local Open Rails source is at:

```text
C:\Users\pgade\Documents\NetbeansProjects\openrails
```

The tracked source baseline reviewed was commit
`91414172dea8f16c08e587f2792264110aaabab1`. Its working tree is intentionally
modified so Open Rails can compile under VS Code instead of the official
Visual Studio setup. These local build adaptations must be preserved and must
not be proposed for upstream commit. A future agent should record both the
tracked baseline and local diff before extending the review.

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

Use one centralized, case-insensitive resolver governed by the three rendering
modes above:

1. `ShapeTemplate DEFAULT` selects the default procedural template/profile.
2. A custom `ShapeTemplate` name first matches a TSRE procedural template,
   then an Open Rails profile.
3. `DISABLED` selects the static/hardcoded path in both `Forced` and `Enabled`
   modes; an empty value does so only in `Enabled` mode.
4. A missing custom name falls back to procedural default in `Forced` mode,
   and to static/hardcoded rendering in `Enabled` mode.
5. `Disabled` mode ignores template selection and uses static/hardcoded
   rendering.

Do not invent source-qualified names or special ORTS-only metadata. TSRE stores
the ordinary selected name in `ShapeTemplate`. The TSRE resolver may use an
ORTS profile file stem and its declared name as lookup aliases, with TSRE
templates winning an ambiguous name. Open Rails should read the same
`ShapeTemplate` value and resolve it directly against its own profile catalog;
when the token is absent it retains its original profile-guessing behavior.

## Additional Challenges

- Road Flex now has a separately remembered configurable minimum radius. Its
  `6 m` default is lower than rail's `15 m`, and companion lanes raise the
  effective floor above their separation when necessary.
- Road profile geometry is normally wider and may require different terrain
  painting and clearance defaults.
- Ruler road paths need eventual migration to the same database-targeted path
  builder, or at least guards preventing duplicate insertion and unsafe
  geometry, but that cleanup is not a prerequisite for road Flex.
- Rail-only wire and superelevation processing must not run for roads.
- Texture lookup differs: TSRE procedural assets currently use
  `route/procedural`, while Open Rails profiles normally reference route
  textures and alternative texture rules.
- The TSRE advanced-template generator may retain its existing material,
  texture, and geometry conventions. The separate ORTS profile backend in
  TSRE must follow ORTS profile behavior closely enough that the same profile
  renders equivalently in both programs.

- Profile caches must include resolved profile identity, geometry,
  superelevation state, and any LOD-affecting settings. Database kind alone
  need not split otherwise identical generated geometry.
- Generated geometry must retain the last complete mesh during rebuilds, as
  established by the live Flex work.
- Invalid profiles need warnings and deterministic fallback, not crashes or
  empty cached meshes.
- A mixed route can contain identical world-object `UiD` values in separate
  databases; membership checks must include database identity.
- Undo still does not transactionally capture dynamic TSection creation.
- Save `ShapeTemplate` directly in the world object. MSTS ignores unknown
  tokens, so a sidecar is not required for this value. Task 08 adds explicit
  Open Rails parsing and selection behavior for the token.
- More-than-two databases are not an implementation goal for this work.
  Standard MSTS export remains limited to TDB and RDB. New APIs should avoid
  unnecessary two-database assumptions where practical, but no registry-wide
  rewrite or custom database persistence is required now.

## Task Breakdown

1. **Task 07 - Procedural Track Profile Pipeline**
   - implement `Forced`, `Enabled`, and `Disabled` rendering rules;
   - add missing DynTrack template selection UI and persistence;
   - add faithful Open Rails STF/XML profile rendering in TSRE;
   - retain the hardcoded DynTrack mesh as fallback.
2. **Task 08 - Open Rails ShapeTemplate And Road Compatibility**
   - first make Open Rails read `ShapeTemplate` and select the named ORTS
     profile, retaining its current guessing behavior when absent;
   - later build the controlled mixed rail/road fixture and add road-aware
     database/profile behavior.
3. **Task 06 - Track Database Targeting**
   - give DynTrack explicit TDB/RDB identity;
   - route Flex placement and snapping through that identity;
   - keep future extensibility in mind without implementing a general database
     registry now.

## Recommended Order

1. Implement Task 07's three rendering modes, missing DynTrack template UI,
   `ShapeTemplate` persistence, and ORTS profile backend in TSRE.
   Acceptance: TSRE can select several TSRE/ORTS profiles and render each
   correctly while static/hardcoded fallback remains available.
2. Implement Task 08 milestone A in Open Rails: read `ShapeTemplate`, select
   the named ORTS profile, and retain the original guessing code when the token
   is absent or unusable.
   Acceptance: Open Rails displays multiple profiles selected by TSRE in one
   route.
3. Implement Task 06's minimal explicit Rail/Road database identity and road
   Flex placement, without a general registry rewrite.
4. Complete Task 08's mixed TDB/RDB runtime fixture and the smallest road-aware
   Open Rails change.
5. Retire the hardcoded rail generator only as a later optional cleanup after
   profile parity and compatibility are demonstrated.

## Research Exit Criteria

- Every rail-only hardcoding affecting DynTrack is listed.
- Database identity has one proposed owner and one resolver boundary.
- Standard MSTS export and TSRE extension limits are explicit.
- Open Rails behavior is described from source, not assumed.
- Track profile documentation and source entry points are identified.
- Implementation is split into small, independently demonstrable rendering,
  Open Rails selection, database-targeting, and road-compatibility milestones.
