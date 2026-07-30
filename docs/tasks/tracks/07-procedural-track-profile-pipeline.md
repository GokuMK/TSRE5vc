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

Research and design complete. This is the first implementation task. Its
initial TSRE profile-selection and rendering milestone does not depend on road
database identity from Task 06.

Implement it incrementally:

1. rendering modes, DynTrack template UI, and `ShapeTemplate` persistence;
2. ORTS profile discovery, parsing, and rendering in TSRE;
3. bundled road/default refinements and optional hardcoded-generator
   replacement only after the first two milestones work.

Milestone 1 acceptance:

- existing TSRE templates can be selected on DynTrack and survive save/reload;
- `Forced`, `Enabled`, and `Disabled` produce the documented result for empty,
  `DEFAULT`, `DISABLED`, valid, and missing template names;
- static and hardcoded fallback rendering remains unchanged and visible.

Milestone 2 acceptance:

- TSRE discovers the same representative STF/XML profiles as Open Rails;
- the editor can select them through the same `ShapeTemplate` field;
- representative straight and curved DynTracks render materially like Open
  Rails without changing the established TSRE advanced-template generator.

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
  the hardcoded/static renderer;
- road `TrackObj` is forced to its static shape even when a procedural
  template is explicitly selected.

### TrackObj template implementation

`TrackObj` is the primary existing implementation to reuse:

- `PropertiesTrackObj` provides `DEFAULT`, `DISABLED`, and named template
  selection;
- `TrackObj::setTemplate(...)` invalidates the current render result;
- `TrackObj::save(...)` persists non-default `ShapeTemplate`;
- `TrackObj` switches between its static shape and `ProceduralShape`;
- explicit `DISABLED` eventually falls back to the static shape.

It is not yet a clean policy implementation:

- the global setting is still boolean;
- `roadShape` bypasses procedural rendering unconditionally;
- `DISABLED` is detected inside the render path, returns before drawing that
  frame, and only uses the static shape on subsequent frames;
- unknown names can still produce and cache an empty procedural result;
- the UI catalog contains only TSRE templates.

Refactor this behavior into the centralized resolver instead of duplicating
its render-time `templateDisabled` state machine. `DISABLED` must select the
static/hardcoded fallback before rendering begins, so it never creates a blank
frame.

DynTrack then adopts the same UI, persistence, and resolution contract, using
`ProceduralMstsDyntrack` where TrackObj would use a static shape.

Ruler is only a supplementary example: it saves `ShapeTemplate` and generates
procedural previews from straight `TSection` vectors. Its manual transforms,
separate RDB path, and straight-only preview are not the model for this task.
A future shared catalog may serve Ruler, but no Ruler refactor is required.

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

Replace the binary global switch with a three-state editor policy. The current
boolean configuration remains readable:

- legacy `true` maps to `Forced`;
- legacy `false` maps to `Disabled`;
- `Enabled` is a new value.

### Forced

This preserves current `proceduralTracks == true` behavior:

- all applicable track objects use procedural generation, even when a static
  shape exists and `ShapeTemplate` is empty;
- explicit `DISABLED` overrides the forced global mode and uses the object's
  static shape or DynTrack hardcoded mesh;
- empty or `DEFAULT` selects the default procedural template/profile;
- a valid custom name selects that template/profile;
- a missing custom name warns once and uses the default procedural result.

### Enabled

This enables procedural rendering per object:

- empty or `DISABLED` uses the object's static shape;
- for DynTrack, which has no static shape, that path means the existing
  hardcoded `ProceduralMstsDyntrack` mesh;
- `DEFAULT` selects the default procedural template/profile;
- a valid custom name selects that template/profile;
- a missing custom name warns once and returns to the static/hardcoded path.

### Disabled

Ignore procedural template selection and use static shapes or the existing
DynTrack hardcoded mesh. This preserves current
`proceduralTracks == false` behavior.

No rendering mode or lookup failure may leave a normal object invisible.

## Profile Identity

TSRE template names remain their declared `Template` names.

Open Rails profiles use:

1. file stem as canonical ID, for example `TrProfileRoad`;
2. internal `Name` as a case-insensitive alias only if unique.

The UI may show the source to explain collisions:

```text
Default road
TSRE: AsphaltRoad
ORTS: TrProfileRoad
```

If a TSRE template and ORTS profile share the same unqualified name, TSRE wins
to preserve the lookup order. The stored value remains the ordinary name; do
not invent source-qualified or ORTS-specific `ShapeTemplate` syntax.

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
    id, name, source, gauge
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

Add it directly to the DynTrack world object when the value is meaningful.
MSTS ignores undefined tokens, and current Open Rails skips unknown world
tokens. No TSRE sidecar is needed for profile choice.

The value is one ordinary template/profile name. It does not encode source or
database identity. An ORTS profile name stored by TSRE will become active in
Open Rails through Task 08 milestone A.

## Error Handling

- Missing default profile: use bundled network default.
- Invalid explicit profile: warn once; use procedural default in `Forced` and
  static/hardcoded fallback in `Enabled`.
- `DISABLED`: select static/hardcoded fallback immediately in both `Forced`
  and `Enabled` modes.
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
- Forced plus unknown explicit name falls back to procedural default;
- Forced plus `DISABLED` uses static/hardcoded rendering;
- Enabled plus unknown explicit name falls back to static/hardcoded rendering;
- Enabled plus empty/`DISABLED` uses static/hardcoded rendering;
- empty/`DEFAULT` under Forced and `DEFAULT` under Enabled resolve to the
  configured default;
- Disabled always uses static/hardcoded rendering;
- cache key distinguishes source, profile, geometry, and elevation.

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

- `Forced`, `Enabled`, and `Disabled` preserve the rendering rules above.
- The DynTrack properties UI can select and save `ShapeTemplate`.
- TSRE can select and correctly render multiple TSRE and ORTS profiles.
- Explicit TSRE and ORTS names select the appropriate procedural backend.
- Missing names cannot make a DynTrack disappear.
- STF and XML profile discovery follows documented Open Rails precedence.
- The ORTS backend behaves like ORTS for supported profile features; existing
  TSRE templates retain their own rendering conventions.
- The hardcoded rail generator is removed only after parity tests pass.
- Profile parsing, resolution, generation, and object/database selection are
  separate components.
