# Task 07 - Procedural Track Profile Pipeline

## Objective

Create one profile-driven DynTrack rendering pipeline which supports:

- TSRE advanced procedural shape templates;
- Open Rails `TrProfile.stf` and `TrProfile.xml`;
- distinct default rail and road profiles;
- deterministic fallback for missing or invalid profiles.

After visual and performance parity is demonstrated, replace the hardcoded
`ProceduralMstsDyntrack` rail mesh with a bundled default rail profile.

## Status

Research and design complete. Implementation should begin only after Task 06
provides reliable network/database identity.

## Existing TSRE Template System

TSRE loads:

```text
appdata/<version>/procedural/shapetemplates.dat
```

Templates contain named elements such as:

- `Ballast`;
- `Rail`;
- `Tie`;
- `Stretch`;
- `Point`.

Elements reference OBJ cross-sections/repeated shapes and textures. Assets may
be overridden under:

```text
<route>/procedural/
```

`ProceduralShape` can generate from:

- a `TrackShape`;
- a vector of `TSection`;
- a `ComplexLine`.

Current weaknesses:

- route-local `shapetemplates.dat` is not loaded;
- template type is declared but not parsed correctly;
- unknown names silently cache an empty shape;
- `DEFAULT` is hardwired to `DefaultTrack`;
- DynTrack does not serialize its template choice;
- DynTrack has no template UI;
- global `Game::proceduralTracks` decides between all procedural templates and
  the hardcoded rail generator.

### Ruler procedural preview

Ruler already demonstrates another useful part of the intended policy:

- setting a template explicitly enables procedural shape rendering;
- `ShapeTemplate` is saved with the TSRE Ruler object;
- each straight Ruler span is passed to `ProceduralShape` independently;
- the generated preview is separate from its optional RDB path.

This is useful reference code for template selection and per-span transforms,
but not a road DynTrack renderer. Ruler currently:

- generates only straight preview spans;
- uses the same template list regardless of rail/road type;
- exposes `DEFAULT` and `DISABLED` even though unresolved names can yield no
  mesh;
- does not bind its visual profile to the RDB vectors it creates;
- uses manual orientation math which should not be copied into the new sweep
  backend.

The profile catalog should eventually serve Ruler as well, but DynTrack
profile work must not be coupled to a Ruler refactor.

## Open Rails Profile Model

Open Rails loads profiles from:

```text
<route>/TrackProfiles/
```

Discovery rules:

- `TrProfile.xml` is preferred as the default;
- otherwise `TrProfile.stf` is the default;
- otherwise Open Rails builds its canned Kuju profile;
- additional files start with `TrProfile`;
- XML wins over STF when both have the same file stem.

A profile contains:

- name and gauge;
- LOD method and cutoff radii;
- curve subdivision controls (`ChordSpan`, chord length, or displacement);
- optional include/exclude filters;
- superelevation method;
- LOD items with material settings;
- polylines forming the swept cross-section;
- vertices with position, normal, UV, and superelevation position control.

Open Rails currently always uses profile index `0` for a DynTrack because a
DynTrack lacks a static shape filename for automatic matching.

Authoritative reference:

- [Open Rails track profile guide](https://static.openrails.org/files/OpenRails-Testing-How%20to%20Provide%20Track%20Profiles%20for%20Open%20Rails%20Dynamic%20Track.pdf)

The source implementation is in:

```text
openrails/Source/RunActivity/Viewer3D/DynamicTrack.cs
```

## Rendering Policy

Replace the binary global switch with a per-object resolution policy.

### Explicit profile

When `ShapeTemplate` contains a real name:

1. search TSRE templates;
2. search Open Rails profiles;
3. warn once if unresolved;
4. use the default profile for the object's network.

An explicit valid name forces procedural generation even when the default
editor policy prefers legacy/static rendering.

### Default profile

When the value is empty or `DEFAULT`:

- rail uses the configured default rail profile;
- road uses the configured default road profile.

During migration, rail default may still delegate to
`ProceduralMstsDyntrack`. Once a bundled profile matches it closely enough,
the profile becomes authoritative.

### Legacy and disabled states

Use explicit semantics:

- `LEGACY`: invoke the old hardcoded generator during migration;
- `DISABLED`: diagnostic no-mesh mode only, if retained at all.

Do not expose `DISABLED` in normal DynTrack placement because DynTrack has no
static shape to fall back to.

## Profile Identity

TSRE template names remain their declared `Template` names.

Open Rails profiles use:

1. file stem as canonical ID, for example `TrProfileRoad`;
2. internal `Name` as a case-insensitive alias only if unique.

The UI should show source to resolve collisions:

```text
Default road
TSRE: AsphaltRoad
ORTS: TrProfileRoad
```

If a TSRE template and ORTS profile share the same unqualified name, TSRE wins
to preserve the agreed lookup order. Source-qualified names should also be
accepted for deterministic selection.

## Proposed Architecture

### Profile catalog

Create one catalog service which:

- loads application and route TSRE templates;
- loads route Open Rails STF/XML profiles;
- validates names and aliases;
- exposes network compatibility;
- resolves the policy above;
- records parse diagnostics;
- supports reload without stale cached meshes.

### Neutral swept-profile model

Do not make TSRE rendering depend directly on Open Rails viewer classes.
Parse ORTS files into a neutral TSRE model:

```text
ProceduralTrackProfile
    id, name, source, network kind, gauge
    LOD policy
    subdivision policy
    materials[]
    LODs[]
        items[]
            polylines[]
                vertices[]
```

TSRE templates may continue using their specialized generator initially.
Both backends should return the same generated-shape abstraction and fallback
status.

### Generic sweep generator

Implement the ORTS-compatible backend as a cross-section sweep over the tested
DynTrack `TSection` path:

- preserve the established TSRE/Flex coordinate boundary;
- generate straight sections with one longitudinal span where safe;
- subdivide curves using ORTS chord rules;
- carry distance for `DeltaTexCoord`;
- transform supplied normals correctly;
- connect adjacent profile vertices with two triangles per cell;
- preserve polyline boundaries;
- enforce vertex/index and 2048 m path budgets;
- support additive and replacement LOD policies;
- retain the previous complete mesh until replacement generation succeeds.

Superelevation `PositionControl` (`None`, `All`, `Inside`, `Outside`) can be
parsed in v1 even if full deformation is staged. Unsupported behavior must
produce a diagnostic rather than silently pretending parity.

## Texture And Material Mapping

Open Rails material fields do not map one-to-one to current TSRE materials.
Define and test a compatibility table for:

- `TexName`;
- shader name;
- light model;
- alpha test;
- wrap/clamp address mode;
- alternative texture flags;
- mip-map LOD bias.

Initial scope may support the common combinations used by the canned and
sample profiles, with warning-based fallback for unknown combinations.

ORTS profile textures should resolve through route/global texture conventions,
not TSRE's `procedural` asset directory. Keep both source-specific resolvers.

## Bundled Defaults

Provide:

- one default rail profile reproducing the current TSRE/MSTS-style rails,
  ties, and ballast;
- one default road profile with an asphalt surface and sensible width.

Defaults must not require route-specific assets. They belong in application
data and need stable IDs.

Before removing the hardcoded rail generator, compare:

- dimensions and origin;
- rail gauge;
- vertical offsets;
- UV scale;
- curve direction;
- elevation;
- LOD visibility;
- selection/picking;
- generation time and memory.

## Metadata And Compatibility

`DynTrackObj::save(...)` currently omits `ShapeTemplate`.

Before adding it to the MSTS world object:

- verify native MSTS tolerates the extra token;
- verify current Open Rails skips it safely;
- if MSTS does not tolerate it, store profile selection in a TSRE route
  sidecar keyed by tile and `UiD`;
- keep native `StaticFlags` responsible only for database compatibility, not
  arbitrary TSRE profile names.

An ORTS profile name stored by TSRE does not make Open Rails select it today.
That requires Task 08 or an Open Rails-side mapping.

## Error Handling

- Missing default profile: use bundled network default.
- Invalid explicit profile: warn once and use network default.
- Profile parses but has no renderable LOD: reject it.
- Missing texture: use the normal missing-texture diagnostic material.
- Excessive geometry: cap subdivision and report truncation.
- Generator failure: keep the last valid mesh.
- Reloaded catalog: invalidate only meshes whose resolved profile changed.
- Never store an empty failed mesh as a successful cache entry.

## Tests

### Parser

- minimal valid STF and XML profiles produce equivalent neutral models;
- XML-over-STF file precedence matches Open Rails;
- malformed headers, missing LODs, duplicate aliases, and invalid vertices
  produce diagnostics and fallback;
- application and route TSRE template precedence is deterministic.

### Resolver

- explicit TSRE name wins;
- otherwise explicit ORTS file stem resolves;
- unique internal ORTS name resolves as an alias;
- unknown explicit name falls back by network;
- empty/`DEFAULT` resolves to rail or road default;
- profile type mismatch warns and falls back;
- cache key distinguishes source, profile, network, geometry, and elevation.

### Geometry

- straight and left/right curve endpoints match the DynTrack path;
- tile position does not affect generated local geometry;
- default road width remains constant through curves;
- normals and UV distance are continuous across section boundaries;
- LODs obey their cutoff policy;
- long/tight paths remain within budgets;
- invalid generation retains the old preview mesh.

### Visual Compatibility

- bundled rail profile matches the old generator closely;
- default road has no rails, ties, wire, or rail superelevation;
- the same ORTS STF profile looks materially equivalent in TSRE and Open
  Rails for representative straight and curved sections.

## Acceptance Criteria

- Rail and road DynTracks always have a visible deterministic default.
- Explicit TSRE and ORTS names force the selected procedural backend.
- Missing names cannot make a DynTrack disappear.
- STF and XML profile discovery follows documented Open Rails precedence.
- Road and rail defaults coexist in one route.
- The hardcoded rail generator is removed only after parity tests pass.
- Profile parsing, resolution, generation, and object/database selection are
  separate components.
