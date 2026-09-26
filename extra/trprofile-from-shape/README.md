# Tutorial: Build an ORTS TrProfile from an MSTS shape

## Purpose

This tutorial is for an online coding agent that receives:

- a reference MSTS `.s` shape;
- an example `TrProfile*.stf` whose rail system, materials, or general style
  should be retained.

The result is a route-local ORTS track profile that TSRE can select as a
`ShapeTemplate` and sweep along a procedural track, road, or Ruler path.

This is a guided conversion, not a blind mesh conversion. An MSTS shape may
contain end caps, transformed subobjects, repeated objects, junction geometry,
or a cross-section that changes along its length. A TrProfile represents an
open cross-section swept along a path.

## Expected deliverable

Create one file under:

```text
<route>/TrackProfiles/<profile-family>.stf
```

TSRE reads every `.stf` and `.xml` file in this directory. Open Rails builds
without the TrProfile-family improvements may still require a `TrProfile*`
filename, so retain that prefix when compatibility with an older build is
required.

Also report:

- which source materials and LODs were retained;
- which faces were deliberately omitted;
- whether any part is an approximation;
- parser/generator test results;
- visual checks still required.

Do not edit the source shape or the supplied example profile.

## Repository prerequisites

Use an incremental build. Never clean the full TSRE build merely to obtain the
shape-export utility.

Build the utility if it is missing:

```powershell
cmake --build build --target tsre_shape_parser_bench --parallel
```

The expected executable on Windows is
`build/tsre_shape_parser_bench.exe`.

## Start with the extraction helper

Run the repository helper from the TSRE root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\extra\trprofile-from-shape\extract_shape_trprofile_reference.ps1 `
  -ShapePath "C:\path\to\route\SHAPES\reference.s"
```

The default output directory is:

```text
build/shape-profile-reference/<shape-name>/
```

You may set it explicitly:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\extra\trprofile-from-shape\extract_shape_trprofile_reference.ps1 `
  -ShapePath "C:\path\to\reference.s" `
  -OutputDirectory ".\build\my-profile-analysis"
```

For shapes whose genuine longitudinal subdivisions are shorter than 0.25 m,
lower the filtering threshold:

```powershell
  -MinimumLongitudinalSpan 0.05
```

The helper creates:

| File | Purpose |
|---|---|
| exported `.s` text | Full readable shape hierarchy and mesh data |
| `shape-profile-report.md` | Materials, shaders, textures, LODs, and primitive summary |
| `shape-profile-edges.csv` | Candidate cross-section edges and estimated UV deltas |
| `shape-profile-primitives.csv` | Per-LOD primitive and cap statistics |
| `parser-output.log` | TSRE shape parser diagnostics |

The script is read-only with respect to the route. It does not create a
TrProfile automatically.

## Decide whether the shape is sweepable

A direct conversion is appropriate when the shape is primarily an extrusion:

- local Z is the length of the object;
- the same X/Y cross-section appears at multiple Z positions;
- UVs change consistently with longitudinal distance;
- the useful faces run along Z.

Ignore these when constructing the profile:

- front and rear caps;
- small cap bevels;
- faces lying entirely at one Z coordinate;
- special one-time geometry that should appear only at the start or end.

The default helper threshold ignores cap bevels up to 0.25 m. Confirm the
result in the exported shape text.

A standard TrProfile cannot exactly represent:

- a true longitudinal taper whose X/Y cross-section changes with Z;
- a bridge portal or end wall;
- a junction;
- discrete signs, poles, or other objects that occur only once;
- arbitrary animated or hierarchical geometry.

### Preserve full 3D geometry with Template3D

TSRE also supports route-local OBJ geometry inside a TrProfile `LODItem`.
Use this when the complete cross-section cannot be reduced to polylines, or
for discrete ties, poles, wires, and endpoint objects:

```text
LODItem (
    TexName ( "material.png" )
    PathFrameMode ( NoRoll )
      Template3D (
          GenerationMode ( Repeat )
          GeometryMode ( Baked )
          ShapeSelectionMode ( Cycle )
        Shape ( "meshes/object-a.obj" )
        Shape ( "meshes/object-b.obj" )
        Offset ( 0 0 0 )
        Spacing ( 30 )
        Phase ( 0 )
    )
)
```

`GenerationMode` is `Sweep`, `Stretch`, `Repeat`, or `Place`. `Sweep`
advances longitudinal V with travelled path distance. `Stretch`, `Repeat`,
and `Place` preserve the OBJ's authored UVs.
`GeometryMode` defaults to `Baked`. Use `Shared` for rigid `Repeat` or `Place`
objects whose source geometry should be uploaded once and rendered at multiple
transforms, such as complex poles. Keep frequent simple objects such as ties
`Baked`; `Shared` is not valid for deforming `Sweep` or `Stretch` templates.
`PathFrameMode` is `Full`, `NoRoll`, or `Upright`. Place entries combine the
location and orientation, for example `Placement ( Both Outward )`. Use
`Placement ( Nodes AlongPath )` for one object at every unique authored Ruler
node. Internal node objects use the angular bisector between the incoming and
outgoing spans; the first and last objects use their only adjacent span.
`Start` and `End` remain aligned to their individual span rather than this
averaged node frame. `Nodes` accepts `AlongPath` or `AgainstPath` only.

The minimal OBJ subset is `v`, `vt`, `vn`, and triangular `f v/vt/vn` with
positive indices. OBJ materials are ignored; the parent LOD item supplies the
texture and material. Mesh coordinates are metres, +X right, +Y up, and -Z
forward. Sweep/Stretch sources begin at Z=0 and extend toward negative Z.

For the old line-oriented TSRE draft template file, the migration helper can
create TrProfiles and self-contained OBJ copies:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\extra\trprofile-from-shape\migrate-shape-templates.ps1 `
  -Source "C:\route\PROCEDURAL\shapetemplates.dat" `
  -Destination "C:\route\TrackProfiles" `
  -MeshDestination "C:\route\TrackProfiles\meshes"
```

The helper preserves the old 3 m rail and 4 m ballast sweep spans and supplies
a neutral UV to legacy OBJ faces which used `v//vn`. It does not modify the
source `PROCEDURAL` directory.

Do not mistake a mesh subdivision for a taper. A 100 m shape may be divided at
an arbitrary Z value while retaining identical X/Y coordinates. If the
cross-section is unchanged and the UV delta per metre is consistent, it is
still one sweepable profile.

## Coordinate-space rule

The exported shape and the TrProfile are authored in MSTS/ORTS local
coordinates:

- X is lateral;
- Y is vertical;
- Z is longitudinal;
- `Position ( X Y )` stores the cross-section only.

Do not:

- combine tile coordinates with local shape coordinates;
- convert points to world space;
- negate Z manually for TSRE;
- pre-flip X to compensate for OpenGL.

TSRE performs the complete ORTS/MSTS-to-TSRE basis conversion inside
`OrtsTrackProfileRenderer::transformVertex()`. Pre-flipping the data produces
a left/right mirror error.

The extraction helper reports vertex matrix indices but does not apply shape
matrices. Inspect the exported `matrices` block. Directly copy coordinates
only when the referenced matrices are identity. Otherwise transform the points
and normals into shape-local space first.

### Mirroring a profile side

To create a geometrically mirrored left/right variant:

1. Negate every X position.
2. Negate every normal X component.
3. Reverse the vertex order in every affected `Polyline`.
4. Keep UV coordinates attached to their original vertices.

Mirroring X without reversing the polyline changes face winding.

## Understand the TrProfile structure

A minimal profile is:

```text
SIMISA@@@@@@@@@@JINX0p0t______

TrProfile (
    ObjectType ( TRACK MAIN )
    Name ( "Example" )
    IncludedShapes ( "Example_*" )
    LODMethod ( "CompleteReplacement" )
    ChordSpan ( 1 )
    PitchControl ( "ChordLength" )
    PitchControlScalar ( 10 )

    LOD (
        CutoffRadius ( 2000 )
        LODItem (
            Name ( "surface" )
            TexName ( "texture.ace" )
            ShaderName ( "TexDiff" )
            LightModelName ( "OptSpecular0" )
            AlphaTestMode ( 0 )
            TexAddrModeName ( "Wrap" )
            ESD_Alternative_Texture ( 1 )
            MipMapLevelOfDetailBias ( 0 )

            Polyline (
                Name ( "surface_top" )
                DeltaTexCoord ( 0 0.2 )
                Vertex (
                    Position ( -2 0 )
                    Normal ( 0 1 0 )
                    TexCoord ( 0 0 )
                )
                Vertex (
                    Position ( 2 0 )
                    Normal ( 0 1 0 )
                    TexCoord ( 1 0 )
                )
            )
        )
    )
)
```

Use the supplied example profile as the authority for root settings such as
`LODMethod`, chord control, gauge, and superelevation behavior unless the
requested profile intentionally differs.

## Choose between cloning and rebuilding

Clone the example when the source uses the same rail, sleeper, and ballast
system and adds only a structure such as an embankment, wall, walkway, or
railing.

When cloning:

- change `Name` and `IncludedShapes`;
- remove unrelated layers such as an overhead wire absent from the source;
- remove any generic ballast skirt that the extracted structure replaces;
- never stack two coincident track beds;
- add the extracted components to every applicable LOD.

Rebuild the complete profile when the source has a substantially different rail
cross-section or material layout.

The file stem is the profile-family identity. `Name` is descriptive and does
not replace that identity. Use `ObjectType ( TRACK MAIN )`, `ROAD MAIN`, or
`STATIC MAIN` to restrict the profile to the correct consumer. If omitted, the
compatible default is `TRACK MAIN`.

An STF file may contain several top-level `TrProfile` blocks. Role variants
use `LEFT`, `MIDDLE`, `RIGHT`, or `SINGLE` as the second ObjectType value and
are identified externally as `<family>_left`, `<family>_middle`,
`<family>_right`, and `<family>_single`. Keep all members of such a family in
the same file. See `docs/tasks/tracks/12-trprofile-improvements.md` for the
complete resolution rules. XML remains one profile per file.

Use a distinct identity, for example:

```text
File: TrProfile_DB1_emb1.stf
Name: DB1_emb1
IncludedShapes: DB1_emb1_*
```

Route-local profiles override global profiles with the same identity.

## Convert materials

Create one `LODItem` for a material/rendering group. The extraction report maps
source primitive-state indices to shaders and textures.

Use these starting rules:

| Source use | TrProfile settings |
|---|---|
| Opaque surface | `ShaderName ( TexDiff )`, `AlphaTestMode ( 0 )` |
| Smooth transparency | `ShaderName ( BlendATexDiff )`, `AlphaTestMode ( 0 )` |
| Cutout ties, fencing, railing | `ShaderName ( BlendATexDiff )`, `AlphaTestMode ( 1 )` |

Alpha-test judgment cannot always be recovered from shape tokens alone.
Inspect the texture and compare with the example profile. A cutout texture
incorrectly treated as ordinary alpha blend may write or sort depth badly and
hide the opaque layer below it.

Keep texture filenames exactly as referenced by the source, including case
where the target filesystem requires it. Confirm that every texture exists in
the route or global texture directory.

## Convert cross-section faces

Each candidate CSV edge describes one surface strip:

```text
(X1,Y1) ---- (X2,Y2)
```

Convert it to a two-vertex `Polyline`. Adjacent coplanar or smoothly shaded
edges using the same material may be combined into one longer polyline.

Map fields as follows:

| Extracted field | TrProfile field |
|---|---|
| `X1 Y1`, `X2 Y2` | `Position ( X Y )` |
| `NX NY NZ` | `Normal ( NX NY NZ )` |
| `U V` | `TexCoord ( U V )` |
| `DeltaU DeltaV` | `DeltaTexCoord ( U V )` |

Preserve the directed vertex order reported by the source triangle winding.
If a surface renders from the wrong side, verify winding before changing
materials or enabling two-sided rendering.

### Normals

Source normals near an end cap can contain an unwanted longitudinal Z
component because the exporter smoothed the cap and side together. Prefer:

1. a normal from an interior longitudinal ring;
2. the stable normal repeated across the long face;
3. a geometric cross-section normal calculated from the face.

For a two-dimensional segment:

```text
dx = x2 - x1
dy = y2 - y1
normal = normalize( -dy, dx, 0 )
```

Flip it when required by source winding. Do not automatically smooth a corner
that is visibly sharp in the source.

## Calculate longitudinal UVs

`DeltaTexCoord` is UV change per metre, not total change across the reference
shape.

For matching vertices at two longitudinal coordinates:

```text
DeltaU = (U2 - U1) / (Z2 - Z1)
DeltaV = (V2 - V1) / (Z2 - Z1)
```

TSRE applies:

```text
generatedUV = baseUV + DeltaTexCoord * pathDistance
```

The helper estimates this value whenever a triangle contains matching X/Y
vertices at different Z coordinates.

Do not round the value to a visually pleasing repeat rate unless visual testing
shows that the source value is noise. A source may legitimately use values such
as `-0.200533`.

Keep the cross-section UV coordinate on each vertex. This aligns the ballast
edge, road marking, wall top, or texture border across adjacent polylines.

## Preserve LOD behavior

With `LODMethod ( CompleteReplacement )`, each LOD must contain the complete
set of geometry intended to remain visible at that distance. A component
present only in the first LOD disappears after its cutoff.

Prefer the source strategy:

- near LOD: complete geometry;
- middle LOD: simplified rail or structure;
- far LOD: silhouette-critical surfaces only.

It is acceptable during an initial experiment to repeat the same extracted
structure in all example-profile LODs. Record that it is not yet optimized.

When the source omits a railing, underside, or small rail side in a distant LOD,
the generated profile should normally omit it too.

## Validate the file structurally

Check balanced parentheses:

```powershell
$p = "C:\path\to\TrProfile_New.stf"
$s = Get-Content -LiteralPath $p -Raw
$open = ($s.ToCharArray() | Where-Object { $_ -eq "(" }).Count
$close = ($s.ToCharArray() | Where-Object { $_ -eq ")" }).Count
if ($open -ne $close) { throw "Unbalanced profile: $open / $close" }
```

Also check:

- at least one `LOD`;
- positive, increasing cutoff radii;
- every `LODItem` has a texture and at least one valid polyline;
- every polyline has at least two vertices;
- all referenced textures exist;
- no accidental duplicate track bed or rail layer.

The parser appends diagnostics and invalidates malformed profiles, so do not
ignore warnings.

## Run TSRE's procedural-profile test

The existing benchmark currently exercises profile IDs `TrProfile_DB1` and
`TrProfile_SR_w`. To test an arbitrary candidate without renaming the real
file, make a disposable route under `build` and expose the candidate through
those filenames.

Example for a route containing `textures` and `procedural` directories:

```powershell
$candidate = "C:\path\to\route\TrackProfiles\TrProfile_New.stf"
$route = "C:\path\to\route"
$caseRoot = Join-Path $PWD ("build\profile-validation-" +
    [DateTime]::Now.ToString("yyyyMMdd-HHmmss"))
$testRoute = Join-Path $caseRoot "routes\profile"
$profiles = Join-Path $testRoute "TrackProfiles"

New-Item -ItemType Directory -Path $profiles -Force | Out-Null
New-Item -ItemType HardLink -Path (Join-Path $profiles "TrProfile_DB1.stf") `
    -Target $candidate | Out-Null
New-Item -ItemType HardLink -Path (Join-Path $profiles "TrProfile_SR_w.stf") `
    -Target $candidate | Out-Null
New-Item -ItemType Junction -Path (Join-Path $testRoute "textures") `
    -Target (Join-Path $route "textures") | Out-Null
New-Item -ItemType Junction -Path (Join-Path $testRoute "procedural") `
    -Target (Join-Path $route "procedural") | Out-Null

& .\build\TSRE5vc.exe --test `
    --test-suite=procedural-profile-benchmark `
    --test-cases=$testRoute
```

If the candidate is on another volume, use temporary copies instead of
hardlinks. Do not modify real route profile names for testing.

Passing this test proves that TSRE parsed the profile and generated straight
and curved meshes. It does not prove visual correctness.

## Perform visual acceptance

Test in this order:

1. A straight section on level terrain.
2. The original static shape beside the procedural result.
3. A gentle left curve and right curve.
4. A steeper curve that exposes inside/outside errors.
5. An elevated section.
6. Every LOD transition.
7. Alpha-tested layers over their opaque base.
8. TSRE and Open Rails when cross-engine compatibility is required.

Check specifically:

- left and right sides have not swapped;
- cross-section width and height match;
- UV orientation and repeat rate match;
- adjacent polylines do not leave gaps;
- transparent pixels do not hide opaque geometry below;
- distant LODs retain the intended silhouette;
- curved inner and outer surfaces advance correctly;
- there are no repeated front/end plates.

## Common failure patterns

| Symptom | Likely cause |
|---|---|
| Left feature appears on the right | X was pre-flipped despite renderer conversion |
| Mirrored face is invisible | X was mirrored without reversing polyline order |
| Vertical plate repeats along track | A front/end cap was converted |
| Ballast flickers or looks too dark | Old and extracted beds overlap |
| Texture stretches along track | `DeltaTexCoord` is total change, not change per metre |
| Texture border does not join | Cross-section `TexCoord` values were rounded or reordered |
| Railing or ties hide the base | Cutout texture uses blend instead of `AlphaTestMode ( 1 )` |
| Shape disappears at distance | Complete-replacement LOD is missing the component |
| End normals point partly along Z | A cap-smoothed normal was copied |
| Candidate report has duplicates | Subdivisions, changing normals, or coincident faces require review |
| Result is unlike the source | The source is tapered, transformed, or not sweepable |

## TSRE files to inspect

Read these files in this order when more context is needed:

| File | Knowledge supplied |
|---|---|
| `extra/trprofile-from-shape/extract_shape_trprofile_reference.ps1` | Shape export and cross-section analysis |
| `docs/examples/track-profiles/RdProfile.stf` | Small, readable multi-role STF family |
| `docs/examples/track-profiles/TrProfile_NR_Bridge.stf` | Real multi-material and multi-LOD family extracted from an MSTS shape |
| `src/tsre/procedural/OrtsTrackProfile.h` | Profile, LOD, material, polyline, and vertex model |
| `src/tsre/procedural/OrtsTrackProfile.cpp` | Parsing, validation, discovery, family identity, and role resolution |
| `src/tsre/procedural/OrtsTrackProfileRenderer.cpp` | Sweep math, coordinate conversion, UV distance, LOD and materials |
| `tests/shapes/ShapeParserBenchmark.cpp` | `--export-text` for compressed/binary shapes |
| `src/tsre/shape/SFileDocument.h` and `.cpp` | Shape document parser and text serialization |
| `src/tsre/tests/ProceduralProfileBenchmark.cpp` | Straight/curve benchmark and test assumptions |
| `docs/features/msts-shape-file-format.md` | Shape-format and parser background |
| `docs/tasks/tracks/07-procedural-track-profile-pipeline.md` | TSRE discovery, selection, and persistence |
| `docs/features/openrails-dyntrack-shape-template-road-design.md` | TSRE/Open Rails compatibility |

Do not begin by reading the entire renderer or legacy shape loader. Run the
helper, read the example profile, and inspect only the implementation needed to
resolve an ambiguity.

## Compact task prompt for another agent

```text
Create a route-local ORTS TrProfile from the supplied MSTS shape.

Inputs:
- Route directory: <route>
- Reference shape: <shape.s>
- Example profile: <TrProfile_example.stf>
- Desired profile ID/name: <name>

Follow extra/trprofile-from-shape/README.md.
Run extra/trprofile-from-shape/extract_shape_trprofile_reference.ps1 first.
Preserve the example's compatible rail/material/LOD structure.
Extract only longitudinal faces; omit front/end caps.
Do not pre-flip MSTS/ORTS coordinates for TSRE.
Remove overlapping or unrelated cloned layers.
Validate parsing and straight/curved generation.
Report approximations and visual tests still required.
Do not modify the source shape or example profile.
```

## Completion criteria

A conversion is complete when:

- the new route-local profile has a unique identity;
- every retained material has intentional shader and alpha behavior;
- all relevant LODs contain required components;
- caps and one-time geometry are absent;
- UV deltas are expressed per metre;
- parentheses and textures validate;
- TSRE parses and generates straight and curved geometry;
- the static reference and procedural result have been visually compared;
- unsupported taper, transform, or end geometry is documented.
