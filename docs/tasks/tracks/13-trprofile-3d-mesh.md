# Task 13 - TrProfile 3D Template Geometry

Status: implemented in TSRE; automated validation complete; route visual
acceptance pending.

## Objective

Move TSRE's experimental OBJ-based procedural templates into the shared
TrProfile pipeline. A TrProfile LOD item may contain ordinary swept polylines,
3D template meshes, or both. This is implemented and visually accepted in
TSRE before a corresponding Open Rails patch is prepared.

## Format

`PathFrameMode` belongs to `LODItem` and defaults to `Full`:

- `Full` follows yaw, pitch, and roll.
- `NoRoll` follows yaw and pitch without track banking.
- `Upright` keeps world up while following the path direction.

`Template3D` supports:

- `GenerationMode ( Sweep | Stretch | Repeat | Place )`;
- `GeometryMode ( Baked | Shared )`, defaulting to `Baked`;
- one or more `Shape ( "mesh.obj" )` entries;
- `ShapeSelectionMode ( First | ByObject | Cycle | DeterministicRandom )`;
- `Offset ( x y z )`;
- `Spacing` and optional `Phase` for `Repeat`;
- repeated `Placement ( Start|End|Both AlongPath|AgainstPath|Outward|Inward )`
  values for `Place`, plus `Placement ( Nodes AlongPath|AgainstPath )`.

The fixed source convention is metres, +X right, +Y up, and -Z forward.
Sweep and Stretch source meshes begin at Z=0 and extend toward negative Z.
The parent `LODItem` supplies texture and material settings; OBJ material
libraries are not loaded.

## OBJ subset

The procedural reader accepts `v`, `vt`, `vn`, and triangular
`f v/vt/vn` records. It accepts ordinary spaces, tabs, comments, and unknown
directives. Positive one-based indices, UVs, normals, finite values, and at
least one complete triangle are required. A malformed recognized record
rejects the complete mesh. Parsed meshes are cached by normalized source path.

## Generation rules

- `Sweep` tiles the source at its authored Z depth inside each profile path
  span and deforms every copy through the shared path frames. The final copy
  is shortened to the span boundary. U is preserved while longitudinal V
  advances continuously with path distance, matching legacy Rail/Ballast
  expansion and retaining texture density.
- `Stretch` maps one normalized source copy over a complete track path. For a
  point-defined Ruler it maps one copy per explicitly authored Ruler span,
  interpolating between the averaged frames of its authored nodes. Wires and
  other offset geometry therefore meet both adjacent spans and a `Nodes`
  support at the same orientation. `Stretch` is an
  atomic placement feature: the renderer does not insert synthetic path
  points or subdivide it into additional source copies for LOD. Authored UVs
  and source topology are preserved.
- `Repeat` places rigid copies at `Phase + n * Spacing`.
- `Place` places rigid copies at requested endpoints and facing modes.
  `Start` and `End` align with their individual span. `Nodes` places one copy
  at every unique authored Ruler node; internal nodes use the angular bisector
  of the incoming and outgoing spans, while the first and last nodes follow
  their sole adjacent span.
- `Baked` copies generated occurrences into their procedural material mesh.
  It is appropriate for dense, simple repeated objects such as sleepers.
- `Shared` uploads source geometry once and retains a transform per occurrence.
  It is accepted only for rigid `Repeat` and `Place` templates. TSRE currently
  reuses the GPU mesh but submits ordinary transformed draws; hardware
  instancing is not required by this feature.
- Polyline and Template3D geometry in one LOD item is additive and shares one
  generated material mesh.

## Ruler spatial units and LOD

- Every original point-to-point Ruler span is one semantic procedural section.
  It is equivalent to a runtime straight TSection, but is not inserted into
  `TSectionDAT`, TDB, or RDB.
- The complete Ruler point list supplies both endpoint frames. In particular,
  the end frame of a span can match the following span rather than copying the
  start orientation.
- Baked output is retained as one render part per original Ruler span and LOD
  item. No artificial Ruler points are created.
- Each baked part and each shared occurrence evaluates its profile cutoff from
  its own transformed bounding sphere instead of the parent Ruler origin.
- A single very long `Stretch` span intentionally remains one LOD unit. Authors
  wanting independently culled pieces use `Sweep`, `Repeat`, or explicitly
  authored Ruler spans.

## Migration and compatibility

- Migrate the legacy `bbb/PROCEDURAL/shapetemplates.dat` choices to route
  TrProfiles and copy their OBJ assets into the TrackProfiles data set.
- Supply simple replacement textures if the historical PNG files cannot be
  recovered.
- Disable loading legacy `shapetemplates.dat` in TSRE after migration, but keep
  its parser/generator code and the route `PROCEDURAL` directory for now.
- Existing polyline-only STF and XML profiles retain their current behavior.

## Acceptance

- Parser tests cover valid and invalid Template3D definitions and the OBJ
  subset.
- Geometry tests cover straight and curved Sweep/Stretch, Repeat spacing,
  endpoint Place orientation, frame modes, mixed polyline/mesh items, and
  finite output.
- Existing `orts-profile` tests remain green.
- Incremental build succeeds.
- Migrated `bbb` rail and ruler profiles are selectable and visually inspected.
- Generation timing is recorded for representative straight and curved paths.

## Deferred

- Open Rails implementation and patch.
- True GPU-instanced draw submission; `Shared` currently optimizes geometry
  storage and generation while retaining one transformed draw per occurrence.
- General-purpose OBJ loading/rendering.
- XML representation for Template3D.
- Mesh-defined materials, negative indices, polygons, smoothing groups, and
  arbitrary source-axis conversion.
- Removal of the legacy TSRE procedural implementation and route directory.

## Implementation record

- Legacy loading is disabled by `ProceduralShape::Load()` while its classes
  remain compiled for comparison and possible rollback.
- `bbb` contains 22 migrated Track/Static profile families, copied OBJ assets
  below `TrackProfiles/meshes`, and route-local `ballastv1.png`, `rails1.png`,
  and `siec.png` textures. Missing UVs in old Blender exports are migrated to
  one neutral coordinate; the runtime reader continues to require UV indices.
- `extra/trprofile-from-shape/migrate-shape-templates.ps1` reproduces this
  migration without changing the old `PROCEDURAL` directory.
- Rulers submit their complete point list through the point-backed
  `ComplexLine`, rather than generating each point pair independently. The
  line stores cumulative node distances and stable 3D node frames. `Stretch`
  interpolates the averaged node frames shared by adjacent spans. `Place` can
  use section-aligned `Start`/`End` frames or one averaged frame per unique
  `Nodes` point; `ByObject` advances by the corresponding span or node.
- `GeometryMode ( Shared )` is enabled for migrated point objects by the
  migration helper; the `bbb` `Electric50mh` profile uses it for its complex
  tower while retaining baked stretched wire geometry.
- The `orts-profile` suite passes 49/49 cases, including Full/NoRoll/Upright
  frame behavior, every Template3D generation mode, multiline Ruler frames,
  joined Stretch endpoints, per-span rendering parts, invalid shared/deformed
  combinations, shared placement geometry equivalence, unique ownership of
  `Repeat` occurrences on Ruler-span boundaries, and unique averaged `Nodes`
  placement in baked and shared modes.
- The Release benchmark loads and generates all 22 migrated profiles. On the
  100 m straight plus 1000 m radius/10 degree curve case, median complete
  generation times were approximately 5.9 ms for both legacy and migrated
  Template3D `DefaultTrack`; `DefaultTrack3` measured approximately 6.8 ms
  legacy and 8.6 ms migrated. The migrated profiles produce two material
  objects rather than four/six legacy objects.
- A 1000 m, twenty-span `Electric50mh` Ruler benchmark places towers at all
  21 authored nodes. It reduced generated vertices from 720,360 fully baked
  vertices to 188,640 baked wire vertices plus one 25,320-vertex shared tower
  mesh referenced by 21 transforms. Median CPU generation time in the recorded
  Release run fell from approximately 29.9 ms to 13.2 ms. Shared mode
  deliberately retains one transformed draw per tower; it is a
  geometry-storage optimization, not GPU instancing.
