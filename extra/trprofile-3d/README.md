# Tutorial: Author a Template3D TrProfile

## Purpose

This tutorial is for a route author or coding agent preparing a TrProfile that
contains route-local OBJ geometry. It complements the cross-section tutorial
in [`../trprofile-from-shape/README.md`](../trprofile-from-shape/README.md).

Use this workflow when geometry cannot be described adequately by ordinary
TrProfile `Polyline` entries, or when a path needs discrete three-dimensional
objects such as rails, sleepers, wires, poles, barriers, or endpoint models.

The examples under [`examples/`](examples/) are intentionally small and use
all four generation modes:

- `Sweep`: deform and tile a longitudinal source mesh along the path;
- `Stretch`: deform one source mesh across one semantic path span;
- `Repeat`: place rigid copies at a fixed distance interval;
- `Place`: place rigid copies at named path locations or Ruler nodes.

## Required input

Before authoring, obtain:

- the target consumer: `TRACK`, `ROAD`, or `STATIC`;
- one example TrProfile whose root, LOD, material, and chord settings are
  appropriate for the route;
- one or more triangulated OBJ meshes;
- the texture used by every `LODItem`;
- the intended path behavior for each mesh;
- the visual distance at which each component may disappear.

If the only source is an MSTS `.s` shape, first inspect it with
[`../trprofile-from-shape/extract_shape_trprofile_reference.ps1`](../trprofile-from-shape/extract_shape_trprofile_reference.ps1).
That helper exports readable shape data and identifies sweepable
cross-sections, but it does **not** convert a complete MSTS hierarchy into an
OBJ. Full 3D conversion still requires applying the source shape matrices and
exporting or constructing the desired geometry deliberately.

If the source is TSRE's old `shapetemplates.dat` plus existing OBJ assets, use
[`../trprofile-from-shape/migrate-shape-templates.ps1`](../trprofile-from-shape/migrate-shape-templates.ps1).
It generates STF profiles and self-contained OBJ copies without modifying the
legacy source directory.

## Deliverable layout

Place profiles and meshes below the route's `TrackProfiles` directory:

```text
ROUTES/<route>/
|-- TrackProfiles/
|   |-- TrProfile_MyFamily.stf
|   `-- meshes/
|       |-- rail.obj
|       |-- sleeper.obj
|       |-- wire.obj
|       `-- pole.obj
`-- TEXTURES/
    `-- my-material.png
```

`Shape` paths are relative to the directory containing the profile. Texture
names are resolved in route `TEXTURES`, then global `TEXTURES`.

Template3D is currently STF-only. Do not add it to an XML profile until a
multi-profile XML wrapper and Template3D representation are implemented.

## Decide between Polyline and Template3D

Prefer an ordinary `Polyline` when the object is fundamentally a
two-dimensional cross-section. It produces less source data, is easy to edit,
and naturally follows curves and elevation.

Use `Template3D` when at least one of these is true:

- the cross-section has meaningful longitudinal topology;
- the object repeats at intervals;
- the object appears only at endpoints or authored Ruler nodes;
- preserving the source UV layout is important;
- reducing the model to line segments would lose required geometry.

Polyline and Template3D entries may coexist in one `LODItem`. Their geometry
is additive and uses the same texture and material settings.

## OBJ contract

The supported OBJ subset is deliberately small:

```text
v  X Y Z
vt U V
vn X Y Z
f  v/vt/vn v/vt/vn v/vt/vn
```

Rules:

- use metres;
- use `+X` right, `+Y` up, and `-Z` forward;
- triangulate every face;
- use positive, one-based indices;
- give every face vertex a position, UV, and normal index;
- apply object transforms before export;
- export finite coordinates and non-zero normals;
- keep each OBJ assigned to one TrProfile material;
- do not rely on MTL files, material names, smoothing groups, negative
  indices, or polygon triangulation at runtime.

Unknown OBJ directives are ignored. A malformed recognized vertex or face
record rejects the complete source mesh.

Do not negate X or Z to compensate for TSRE's OpenGL rendering space. The
renderer performs the MSTS/ORTS-to-TSRE basis conversion. Pre-flipping the OBJ
causes left/right or face-winding errors.

## Choose the generation mode

| Mode | Source origin and length | Runtime behavior | Typical use |
|---|---|---|---|
| `Sweep` | Starts at `Z=0`, extends toward negative Z | Tiles and deforms copies; trims the final copy | rails, ballast solids, continuous beams |
| `Stretch` | Starts at `Z=0`, extends toward negative Z | Deforms one copy over each semantic span | wires, rigid-length panels, span-specific geometry |
| `Repeat` | Pivot at the desired placement origin | Places rigid copies at `Phase + n * Spacing` | sleepers, bolts, delineators |
| `Place` | Pivot at the desired placement origin | Places rigid copies at requested anchors | poles, portals, end caps, signs |

### Sweep

```text
Template3D (
    GenerationMode ( Sweep )
    ShapeSelectionMode ( First )
    Shape ( "meshes/rail-sweep.obj" )
    Offset ( 0.72 0.20 0 )
)
```

The source's authored Z extent is its tile length. TSRE repeats it along the
path and shortens the final copy. Longitudinal V advances with travelled path
distance, while the source U coordinate is preserved. This is the appropriate
mode when texture density must remain constant on a path of arbitrary length.

Avoid front and rear caps: repeating them creates plates inside the generated
object. Keep caps only when they are an intentional visible part of every tile.

### Stretch

```text
Template3D (
    GenerationMode ( Stretch )
    ShapeSelectionMode ( ByObject )
    Shape ( "meshes/wire-stretch.obj" )
    Offset ( 0 0 0 )
)
```

Stretch maps the complete source Z range to a semantic path span and preserves
the authored UVs. A static TrackObj or DynTrackObj normally supplies its shape
path. A multi-point Ruler supplies one span for every adjacent pair of authored
points.

Stretch is intentionally not split merely to improve LOD. Inserting synthetic
points would change the defined deformation and could break offset wires or
arms. Add real Ruler nodes if independently controlled spans are required.

### Repeat

```text
Template3D (
    GenerationMode ( Repeat )
    GeometryMode ( Baked )
    ShapeSelectionMode ( Cycle )
    Shape ( "meshes/sleeper-a.obj" )
    Shape ( "meshes/sleeper-b.obj" )
    Offset ( 0 0.15 0 )
    Spacing ( 0.65 )
    Phase ( 0 )
)
```

`Spacing` must be positive. `Phase` is the first placement distance measured
from the path start. Repeated objects remain rigid; they are not deformed by
the interval between samples.

Use `Baked` for small, frequent objects. Thousands of individually submitted
shared transforms can cost more than duplicating a small mesh into a few
generated buffers.

### Place

```text
Template3D (
    GenerationMode ( Place )
    GeometryMode ( Shared )
    ShapeSelectionMode ( First )
    Shape ( "meshes/pole-place.obj" )
    Offset ( 0 0 0 )
    Placement ( Nodes AlongPath )
)
```

Supported locations and facing rules are:

| Placement | Result |
|---|---|
| `Start AlongPath` | At each span start, facing along that span |
| `End AlongPath` | At each span end, facing along that span |
| `Both Outward` | Start faces backward and end faces forward |
| `Both Inward` | Start faces forward and end faces backward |
| `Nodes AlongPath` | Once at every unique authored Ruler node |
| `Nodes AgainstPath` | Same node ownership, rotated 180 degrees |

`Nodes` is meaningful only for point-backed Ruler paths. The first and last
node use their only adjacent span. An internal node uses the angular bisector
of its incoming and outgoing directions. This is the same frame used by
multiline Stretch interpolation, allowing stretched wires to meet node poles.

`Placement` may occur more than once in one Template3D block. Do not use
`Outward` or `Inward` with `Nodes`; they are ambiguous at internal nodes.

## GeometryMode: Baked or Shared

`GeometryMode` defaults to `Baked`.

- `Baked` copies every occurrence into generated mesh buffers. Prefer it for
  Sweep, Stretch, and small dense Repeat objects.
- `Shared` retains one source mesh and one transform per occurrence. It is
  accepted only for rigid Repeat and Place geometry. Prefer it for complex,
  relatively sparse objects such as catenary poles.

Shared currently reduces geometry storage and generation time; it is not a
single GPU-instanced draw call. Each occurrence still submits a transformed
draw. Measure both choices when an object is neither clearly small nor clearly
complex.

## ShapeSelectionMode

When a block contains several `Shape` entries:

| Value | Selection rule |
|---|---|
| `First` | Always use the first valid source |
| `ByObject` | Select by input span, node, or object index |
| `Cycle` | Cycle through sources for successive occurrences |
| `DeterministicRandom` | Stable hash-based selection; does not change between rebuilds |

Use `ByObject` for authored sets where each input span or node corresponds to
a particular source. Use `Cycle` or `DeterministicRandom` for repeated visual
variation.

## PathFrameMode

`PathFrameMode` belongs to the parent `LODItem` and affects every Polyline and
Template3D child in that item:

| Value | Behavior | Typical use |
|---|---|---|
| `Full` | Follow yaw, pitch, and roll | rails, ballast, road surfaces |
| `NoRoll` | Follow yaw and pitch, ignore banking | wires and objects that should not bank |
| `Upright` | Follow horizontal direction and retain world up | vertical signs and poles |

If components require different frame modes, put them in separate `LODItem`
blocks even if they share a texture.

## Offset and pivot design

`Offset ( X Y Z )` is applied in the authored OBJ basis before path
transformation.

Good source pivots reduce profile-specific offsets:

- a rail source may be centered at `X=0` and instanced twice with lateral
  offsets;
- a sleeper should normally be centered on the track origin;
- a pole should place its foot at the origin;
- a wire may contain its real lateral and vertical position directly when it
  must match a corresponding pole arm.

For related Stretch and Place meshes, author their offsets in one common local
frame. Otherwise a wire can reach the correct path node but miss the pole arm.

## Materials and LODItems

The parent `LODItem` supplies the texture and render state. OBJ material data
is ignored.

```text
LODItem (
    Name ( "metal" )
    TexName ( "metal.png" )
    ShaderName ( TexDiff )
    LightModelName ( OptSpecular0 )
    AlphaTestMode ( 0 )
    TexAddrModeName ( Wrap )
    PathFrameMode ( Full )
    Template3D ( ... )
)
```

Use separate LODItems when geometry needs a different texture, alpha mode,
frame mode, or LOD behavior. For cutout fences and wires use the route's
working alpha-tested material convention; do not treat cutouts as ordinary
transparent blending when depth sorting is unavailable.

With `LODMethod ( CompleteReplacement )`, every LOD must repeat all components
that should remain visible at that distance. With `ComponentAdditive`, later
LOD blocks add independently selected components. Begin by matching the
behavior of the supplied example profile rather than changing LOD policy.

## Rendering chunks

Chunking is a TSRE renderer policy, not profile syntax. Paths above the current
threshold are divided into approximately 100 m rendering parts so LOD is based
on nearby geometry rather than a distant world-object origin.

- Polyline and Sweep output may be divided along the path.
- A Repeat or Place occurrence belongs to exactly one chunk.
- Stretch remains atomic per semantic span.
- No TSection, TDB/RDB node, or Ruler point is created by chunking.

Authors should not encode the current 100/120 m implementation constants into
profile files.

## Complete examples

[`examples/TrackProfiles/TrProfile_3D_Track.stf`](examples/TrackProfiles/TrProfile_3D_Track.stf)
contains swept rails and baked repeated sleepers.

[`examples/TrackProfiles/TrProfile_3D_Ruler.stf`](examples/TrackProfiles/TrProfile_3D_Ruler.stf)
contains one stretched wire per Ruler span and a shared pole at every authored
Ruler node.

The bundled OBJ files are deliberately small and readable. The example
profiles reference `example-3d.png`; copy or create that texture in the target
route's `TEXTURES` directory before visual testing. The examples are teaching
fixtures, not production track artwork.

## Validation workflow

Run the included source validator against a route before opening the editor:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\extra\trprofile-3d\validate-trprofile-3d.ps1 `
    C:\trainsim\ROUTES\MyRoute\TrackProfiles
```

It checks balanced STF delimiters, resolves each `Shape`, and applies TSRE's
numeric and positive-index triangular OBJ rules. It does not replace the real
parser, mesh builder, or visual inspection.

Then:

1. Load the route and inspect profile diagnostics.
2. Run the engine regression suite:

   ```powershell
   .\build\TSRE5vc.exe --test --test-suite orts-profile
   ```

3. Test a short straight path before curves or multi-point Rulers.
4. Test left and right curves, elevation, and sharp changes in direction.
5. Check every LOD transition.
6. Compare Baked and Shared performance when the choice is not obvious.
7. Save and reload the route before acceptance.

The regression suite verifies the parser and generator implementation. It does
not automatically load an arbitrary route profile or prove that its artwork is
correct.

## Visual acceptance checklist

- Left and right have not been mirrored.
- Face winding is visible from the intended side.
- Normals remain finite and lighting is stable.
- Sweep seams do not contain repeated caps.
- Sweep texture density remains stable with path length.
- Stretch endpoints meet the intended anchors.
- Repeat spacing and phase are correct at both ends.
- Place facing is correct at starts, ends, and Ruler corners.
- A shared node object appears once per authored node, not once per span end.
- Full/NoRoll/Upright behavior matches the component.
- Alpha-tested geometry does not hide the opaque layer below it.
- Distant chunks select appropriate LOD near the camera.

## Common failures

| Symptom | Likely cause |
|---|---|
| Left/right are reversed | OBJ was pre-flipped for OpenGL |
| Entire mesh is rejected | Polygon, missing UV/normal index, negative index, or non-finite value |
| Plates repeat inside a rail or beam | Sweep source contains end caps |
| Texture stretches on Sweep | Source V or longitudinal extent is unsuitable |
| Texture unexpectedly repeats on Stretch | Sweep was selected instead of Stretch |
| Thousands of complex objects are slow | Complex Repeat/Place source should be Shared or less frequent |
| Thousands of simple objects are slow | Small dense source was made Shared instead of Baked |
| Pole appears at both sides of a corner | Start/End placement was used instead of Nodes |
| Pole is absent at the last Ruler point | Placement does not include Nodes or End |
| Wire misses a pole arm at a corner | Sources do not share a pivot/offset or use inconsistent frame modes |
| Vertical pole banks with track | Use `NoRoll` or `Upright` in a separate LODItem |
| Mesh disappears after an LOD cutoff | CompleteReplacement LOD omitted that component |

## Source files to inspect when blocked

Read these implementation files in order:

1. `src/tsre/procedural/OrtsTrackProfile.h` — parsed data model and defaults.
2. `src/tsre/procedural/OrtsTrackProfile.cpp` — STF parsing and validation.
3. `src/tsre/shape/ObjFile.cpp` — supported OBJ grammar and rejection rules.
4. `src/tsre/procedural/OrtsTrackProfileRenderer.cpp` — mode semantics,
   coordinate conversion, UV generation, chunking, and shared transforms.
5. `src/tsre/procedural/ComplexLine.cpp` — track/Ruler frames and node
   interpolation.
6. `src/tsre/tests/TestRunner.cpp` — focused Template3D regression cases.

For a shorter syntax reference and coloured complete examples, see
[`../../docs/features/trprofile-showcase.html`](../../docs/features/trprofile-showcase.html).
For implementation status and recorded benchmark results, see
[`../../docs/tasks/tracks/13-trprofile-3d-mesh.md`](../../docs/tasks/tracks/13-trprofile-3d-mesh.md).

## Agent handoff template

Give another agent:

```text
Read extra/trprofile-3d/README.md completely.

Input:
- target route and ObjectType;
- source OBJ files, or source MSTS shape plus the expected conversion scope;
- an example TrProfile whose root/material/LOD behavior should be retained;
- available texture filenames;
- desired path behavior and placement spacing.

Deliver:
- route-local STF profile and OBJ files;
- a note describing mode, frame, geometry, and selection choices;
- omitted or approximated source geometry;
- parser/generator test output;
- remaining visual checks.

Do not pre-flip coordinates for TSRE. Do not invent unsupported OBJ syntax.
Do not claim visual acceptance without testing in the editor.
```
