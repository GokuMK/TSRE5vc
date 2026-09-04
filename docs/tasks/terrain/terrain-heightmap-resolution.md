# Terrain heightmap resolution support

Status: implemented, including variable patch-count editing up to 32 x 32 and
a shared profile selector for manual and automatic tile creation.

Related tasks:

- [Variable terrain patch count](terrain-patch-count.md)
- [Terrain adjacent-edge cache](terrain-adjacent-edge-cache.md)

## Decision and scope

Heightmap resolution and patch count are separate compatibility axes. Existing
valid grids with `1 <= P <= 32` and `4 <= R=N/P <= 128` are loadable,
renderable, editable, and saveable. New detailed terrain can use the same
validated profile/patch combinations through the B-key replacement dialog or
the default profile used by automatic tile generation.

The primary new profile is `512 samples @ 4 m`, alongside the existing
`256 samples @ 8 m` profile. Experimental `128 samples @ 16 m` and
`1024 samples @ 2 m` profiles are also exposed through the shared terrain
profile selector. The implementation must not assume that a terrain file has a
2048 m footprint: terrain size is independent of World (`.w`) files.

World files remain on the fixed MSTS 2048 m coordinate lattice. For example,
a 4096 m terrain tile covers four 2048 m World files. The literals 2048 and
1024 are therefore correct wherever they convert or centre World-file
coordinates. They must not be removed merely because terrain dimensions become
dynamic.

In scope:

- safe loading, rendering, editing, undo, F/gap data, geodata, networking, and
  saving for metadata-defined sample count and spacing;
- validation and editing of `1 <= P <= 32`, subject to `4 <= R <= 128`;
- preserving all terrain tile sizes already represented by the quadtree;
- `512 @ 4 m` as a required supported case;
- a reusable GUI for selecting 128/16, 256/8, 512/4, or 1024/2 heightmaps and
  4, 8, 16, or 32 patches per side;
- loading checks that reject patch counts above 32 or incompatible with `N`.

Out of scope:

- loading or creating more than 32 patches per side, `R < 4`, or `R > 128`;
- changing the 2048 m World-file lattice;
- converting/resampling an existing route to a different grid;
- rectangular or rotated grids unless separately designed;
- terrain rendering LOD redesign.

## Shared terrain profile UI

`TerrainProfileSelector` is a reusable QWidget embedded in the B-key detailed
terrain dialog and in a standalone default-profile selection dialog opened
from GeoTools. It selects the complete `(height profile, patches per side)`
tuple and shows a meaningful name containing `N`, `S`, `P`, and `R`.

| GUI profile | Samples | Spacing | Footprint | Patches | Status |
| --- | ---: | ---: | ---: | ---: | --- |
| Standard | `256 x 256` | `8 m` | `2048 m` | selectable | normal/default |
| Low resolution | `128 x 128` | `16 m` | `2048 m` | selectable | experimental |
| High resolution | `512 x 512` | `4 m` | `2048 m` | selectable | experimental |
| Ultra resolution | `1024 x 1024` | `2 m` | `2048 m` | selectable | experimental |

Requirements:

- initialize to the current automatic-generation default, initially
  `256/8, P=16`;
- offer only these four named profiles, not unrestricted numeric inputs;
- offer `P=4`, `8`, `16`, and `32`, disabling tuples rejected by the central
  layout validator;
- keep the 2048 m terrain footprint for all four profiles;
- show compatibility for TSRE current, MSTS Bin 1.8, MSTS Bin 1.9, Open Rails
  master, and Open Rails unstable; green means confirmed support, red means
  incompatible, and amber means source/limit-compatible but not runtime-tested;
- mark TSRE's `256/P16`, `512/P16`, and `1024/P16` choices supported and
  recommended. `1024/P16` keeps the standard 128 m physical patch and reduces
  per-tile patch draws; patched MSTS instead requires `1024/P32`;
- cancellation must create neither the World file nor terrain/quadtree data;
- pass the selected profile only to detailed-terrain creation, through both
  initial creation and confirmed terrain overwrite, without asking for it
  again or falling back to 256/8;
- after overwrite is confirmed, recreate `_y.raw` even when it already exists;
  the current `Terrain::SaveEmpty()` only writes Y when absent, which would
  leave a descriptor/payload size mismatch when changing profiles;
- remove or regenerate stale referenced `_e.raw` and `_n.raw` data from the old
  profile so MSTS cannot consume buffers with the previous dimensions; do not
  alter existing files until the overwrite confirmation has been accepted;
- create the `.w` file exactly as today, if it is absent; never pass a terrain
  profile into `Tile::saveEmpty()` or other World-file APIs;
- if a World file already exists, preserve it while creating or replacing the
  independent terrain data;
- base the replacement warning on whether detailed terrain exists, not merely
  on `tile[x*10000 + z]->loaded`: the latter describes the World tile;
- use the selected profile only for the detailed terrain `.t`/RAW data;
- retain the current optional `autoGeoTerrain` step, but make it operate at the
  newly created terrain's actual spacing rather than assuming 8 m.

GeoTools displays **Default Terrain Profile**, its current meaningful name, and
a button opening the same selector. Automatic creation while moving through an
empty tile and bulk marker-file `createNewTiles()` both converge on
`Route::newTile()` and use this selected profile explicitly. The B dialog opens
with that default selected but remains a one-off choice and does not silently
change the automatic default. New-route bootstrap terrain and distant/low
terrain creation remain on their established profiles.

Use a named profile/layout value rather than passing two unrelated GUI integers
through the terrain call chain. The intended separation is:

```text
TerrainTileCreationDialog (B key)
  |- World branch (only if .w is absent)
  |    -> Tile::saveEmpty(x, z)                   // no terrain profile
  |
  `- detailed-terrain branch
       -> TerrainLib::saveEmpty(x, z, selectedProfile, selectedPatches)
       -> Terrain::SaveEmpty(name, N, S, P, low=false)
```

The terrain profile never becomes a property of `Tile` or a World-file API.
`Route::newTile()` reads the current automatic terrain default and passes it
only to detailed-terrain creation; World files remain fixed independent data.

## Required terminology and invariants

Put the following values in one validated layout object instead of repeatedly
reconstructing them from literals:

- `WorldTileSize = 2048 m`: fixed `.w`-file coordinate unit;
- `WorldTileHalfSize = 1024 m`: centre-to-edge distance of a `.w` tile;
- `N = sampleCount = *tfile->nsamples`;
- `S = sampleSpacing = *tfile->sampleSize` in metres;
- `T = terrainWorldSize = N * S`;
- `P = patchesPerSide`, validated from the descriptor or selected profile;
- `R = patchResolution = N / P` sample cells per patch side;
- `patchWorldSize = T / P = R * S`;
- stored RAW/F cells: `N * N`;
- in-memory height/F grid with duplicated or stitched border:
  `(N + 1) * (N + 1)`.

Do **not** add `N * S == 2048` as a general validation rule. Instead, validate
that `T` agrees with the physical terrain tile selected by the terrain
quadtree/name. The existing `Terrain::getLowCornerTileXY()` and
`getCornerCoordsXY()` already derive covered World-tile coordinates from
`T / 2048`; this relationship should be centralized and tested, not replaced
with a one-World-file assumption.

For a 4096 m terrain tile, coordinate tests must prove that its terrain data is
shared by the expected 2 x 2 World-file area while World objects continue to
load from four independent `.w` files.

## What is already metadata-driven

The following code is a useful base:

- `TFile` reads `nsamples`, `sampleSize`, and `sampleRotation` from `.t`
  metadata (`src/tsre/world/TFile.cpp`).
- `Terrain::readRAW()` and `readRAWFloat()` allocate `(N + 1)^2` values and
  create the extra border (`src/tsre/world/Terrain.cpp`).
- RAW and float-RAW save loops write `N^2` values.
- `vertexInit()` and `normalInit()` allocate from `N` and position vertices
  using `S`.
- height interpolation and several coordinate/patch helpers already read
  `N`, `S`, and `P`.
- `TerrainLibQt` passes terrain resolution and extent to `HeightWindow` for
  quadtree geodata import.
- the terrain client/server message body is variable length and includes the
  `.t` descriptor; no protocol field fixed at 256 samples was found.
- `TFile` patch-record allocation and serialization already use `P * P`.

These paths need validation and tests, but not a redesign.

## P0: memory safety and corrupt-data handling

### 1. Terrain-owned grids are destroyed as 257 rows

`Terrain::~Terrain()` deletes exactly 257 rows of `terrainData` and `fData`
(`src/tsre/world/Terrain.cpp`). A smaller allocation can delete invalid row
pointers; a larger allocation leaks all rows after 257.

Required change:

- use contiguous RAII storage, or store the allocated side length with each
  row-pointer view;
- destroy by the allocation's recorded size, not current `.t` metadata;
- keep partial load failures and client-side staged loads destructible.

### 2. Active terrain VBO and staging buffers assume 256/16

`Terrain::oglInit()` allocates the VBO using
`256 * 16 * 16 * 6 * 8` floats and the per-patch staging array using
`16 * 16 * 48` floats, but its loops use metadata-derived `P` and `R`.
For `512 @ 4 m`, `R` is 32 and both assumptions become unsafe.

`initBlob()` separately reserves `65536 * 54` floats and writes from `N * N`.
The grid overlays in `reloadLines()` also have allocations/formulas exact only
for the old layout.

Required change:

- VBO floats: `P * P * R * R * 6 * vertexStride`, equivalently
  `N * N * 6 * vertexStride` while `N == P * R`;
- per-patch scratch floats: `R * R * 6 * vertexStride`;
- blob floats: derive from `N * N` and the actual blob vertex stride;
- line overlays: emit into dynamic containers and use actual emitted counts;
- use checked `size_t` multiplication, then verify conversion to the signed
  byte-count/offset types accepted by `QOpenGLBuffer`;
- report a resource-limit error before allocation rather than partially
  initializing the tile.

At the current 48 bytes per terrain cell, the main VBO for a 512 grid is about
48 MiB. Establish a documented maximum grid/resource budget.

### 3. Undo snapshots have a fixed 257 x 257 inline array

`UndoState::TerrainData::data` is `float data[257*257]`, while
`PushTerrainHeightMap()` copies `(N + 1)^2` values (`src/tsre/Undo.h` and
`src/tsre/Undo.cpp`). A 512 grid corrupts adjacent memory.

Required change:

- store the snapshot in a vector with its recorded `N`/side length;
- check overflow before resizing;
- on restore, confirm the target terrain has compatible dimensions;
- distinguish detailed/distant terrain in the snapshot identity if both can
  use the same `(x,z)` key.

### 4. F/gap allocation and editing assume 257, 256, 16, and 8 m

`newF()` allocates 257 x 257. `toggleGaps()` maps coordinates with `/ 8` and a
256 bound. `removeAllGaps()` clears 16 samples per selected patch. These are
height-resolution assumptions even though `P` remains 16.

Required change:

- allocate `(N + 1)^2` F values;
- map world position through the same terrain-local/sample helper as height
  editing and use `S`;
- clear `R x R` F cells per patch;
- define ownership of the duplicated east/south border.

### 5. RAW, F, and network payload lengths are trusted

The readers consume counts derived from metadata without first proving that
the payload has the required number of bytes.

Before allocation or reading, validate:

- `N > 0`;
- `S` is finite and positive;
- `1 <= P <= 32` and `4 <= R=N/P <= 128`;
- `N >= P` and `N % P == 0`;
- `T = N * S` is finite and consistent with the terrain quadtree entry;
- all count/byte products fit `size_t`, API integer limits, and configured
  resource caps;
- height RAW size is exactly `N * N * 2` bytes;
- float RAW size is exactly `N * N * 4` bytes;
- F RAW size is exactly `N * N` bytes when present.

Reject unsupported metadata before calling `readRAW()`. A failed load must
leave a destructible object and log the terrain name, `N`, `S`, `P`, rotation,
expected bytes, and actual bytes.

### 6. `HeightWindow` reuses an allocation with the wrong deletion size

`HeightWindow::drawTile()` replaces its resolution before deleting the cached
row allocation. Switching grid sizes in one session can leak rows or delete
beyond the previous buffer. It also computes an integer sample step, which
silently truncates fractional spacing (`src/tsre/geo/HeightWindow.cpp`).

Required change:

- keep the allocation's actual row count or use a contiguous container;
- use floating-point sample positions if fractional spacing is supported;
- otherwise validate that `S` is integral and never silently truncate it;
- test two imports with different resolutions in one editor session.

## P1: functional correctness

### 7. Height brushes and track-bed deformation

Height painting now uses the edited terrain's native sample spacing and batches
one changed-sample rectangle per affected terrain tile before calling
`refreshModified()`. This avoids repeated complete patch-grid scans while a
brush is being applied.

`TerrainLibQt::setTerrainToTrackObj()` now builds a world-aligned action raster
at the finest sample spacing among the intersecting editable terrain tiles.
Overlapping object-point stamps retain only the strongest legacy
cut/embankment influence. The action is then sampled once at every target
terrain's native vertices, so mixed-resolution tiles no longer require direct
cross-grid writes or an invented high-resolution copy of low-resolution
terrain. Undo, ErrorBias reset, changed-sample bounds, and modified-patch GPU
refresh are applied once per affected tile.

Track and dynamic-track action rasters calculate continuous distance to the
line segments between their approximately 4 m source points. This removes the
coarse point-stamp stepping on 2 m terrain without blurring the flat bed or its
cut/embankment slopes. Because the old flat point array has no explicit strip
breaks, segments are joined only across source-point gaps no larger than 8 m.
Larger gaps retain isolated circular influence. Generic shape borders continue
to use the legacy point mode until their point API provides explicit line-strip
breaks, avoiding accidental connections between unrelated borders.

The terrain-adjustment `Size` and `Max Radius` controls now specify metres
directly rather than using the old fixed 8 m terrain-sample unit. Their numeric
ranges and defaults preserve the previous maximum physical extent and default
footprint while allowing high-resolution terrain to use its full detail.
`Cutting` and `Embankment` now specify slope angles from 10 through 80 degrees.
The applied vertical allowance is the physical horizontal distance beyond the
flat bed multiplied by the angle's tangent, so the same settings produce the
same profile on every terrain sample spacing. The 32-degree defaults closely
preserve the old 5 m vertical change per 8 m horizontal step.

KEY_F terrain adjustment now derives track centerlines exclusively from the
selected World object's current transform. It no longer combines a potentially
stale TDB vector section with the moved object's matrix. Static track iterates
each TrackShape path separately and applies its path offset and rotation;
dynamic track constructs temporary `TSection` values directly from its five
editable section records. Both paths reuse `TSection::getPoints()`, and dynamic
track therefore works independently of TDB membership and generated
`sectionIdx` state.

A temporary height raster remains a useful future companion for other batch
terrain transformations which genuinely need to operate on old and new height
fields together. It should reuse the action raster's world-aligned bounds and
native-tile casting rules rather than reintroducing per-sample terrain lookup.

Remaining cleanup:

- rename obsolete helpers such as `setHeight256()` once all callers migrate;
- replace the temporary 8 m discontinuity guard with explicit line-strip breaks
  in the track-point API;
- extend the same batching architecture to other area-wide terrain tools where
  measurements justify it.

`TerrainLibQt::fillTerrainData()`, used for route merging, independently walks
`-1024..1024` at 8 m and textures at 128 m. Its World-file traversal can keep
2048/1024, but sample and patch steps must come from the destination terrain.

### 8. `TerrainLibSimple` remains selectable and is fixed-grid

Route and client construction still select `TerrainLibSimple` when quadtree
terrain is disabled. Its direct access, slopes, geodata, track-bed, brushes,
fill, and stitching contain extensive 256/8 assumptions.

Required decision:

- migrate it to the shared grid/coordinate helpers, or
- disable the legacy mode for unsupported grids with a clear message.

Updating only `TerrainLibQt` is not enough while this is a user-selectable
path.

### 9. Texture coordinates use samples per patch, not a fixed 16-unit domain

Let `R = N / P` be the number of heightmap samples per patch side. Patch
texture matrices consume raw patch-local sample coordinates from zero through
`R`. A default transform that maps a texture exactly once across a patch must
therefore use a linear scale of `1 / R`. Whole-terrain map generation advances
by `1 / (P * R)`, equivalently `1 / N`, per sample.

This is no longer an inference. On 2026-09-01, otherwise equivalent texture
placement was tested in both TSRE and the original MSTS Route Editor (MSRE) on
terrain with different `R` values, including `128 samples / 16 patches` (`R=8`)
and the standard `256 / 16` (`R=16`). TSRE and MSRE rendered the texture the
same way when the transform used `1/R`. This directly disproves a permanently
fixed 16-unit patch texture domain and any compensating `16/R` rescaling of
stored patch transforms.

Open Rails independently uses the same model in its higher-resolution terrain
work: `PatchSampleCount = SampleCount / PatchCount`; vertices use their raw
patch-local sample indices as texture-matrix inputs. See
<https://github.com/openrails/openrails/pull/1246> and commit
<https://github.com/openrails/openrails/commit/6dca405>.

Implementation rule:

- derive default/reset patch transforms and per-cell patch UV increments from
  `R`;
- derive whole-terrain texture increments from `N`;
- do not multiply stored transforms by `16/R` during load or rendering;
- preserve the format's inset explicitly rather than baking it into a 16-cell
  coefficient;
- test default, reset, rotate, mirror, scale, unique, painted, and map-derived
  texture paths at both `R=8` and `R=16`, then at larger TSRE-only values.

#### Empirical MSRE creation/loading limits

The same MSRE tests found that MSRE refuses terrain unless both conditions
hold:

```text
terrain_nsamples <= 256
terrain_nsamples / terrain_patchset_npatches <= 16
```

These are MSRE compatibility limits, not TSRE format-validation limits. They
explain why `128/16` was needed as a smaller test profile and divide the test
matrix as follows:

| Samples / patches | `R` | MSRE result |
| --- | ---: | --- |
| `128 / 16` | 8 | accepted |
| `128 / 8` | 16 | accepted |
| `128 / 4` | 32 | refused |
| `256 / 16` | 16 | accepted |
| `256 / 8` or `256 / 4` | 32 or 64 | refused |
| `N > 256` | any | refused by the sample-count limit |

TSRE deliberately supports a broader metadata-valid set. Do not turn the MSRE
limits into TSRE loader restrictions; use them only when producing a tile that
must be opened by MSRE.

### 10. Coordinate code must distinguish World and terrain units

Functions including `setHeight()`, `getHeight()`, `getRotation()`,
`isXYinside()`, `getLocalCoords()`, `getPatchCoords()`, rendering transforms,
and terrain-neighbor lookup combine:

- 2048/1024 for the World-file coordinate lattice;
- `T = N * S` for the physical terrain extent;
- `S` for terrain sample addressing;
- `T / P` for patch addressing.

This combination is intentional, but scattered arithmetic is hard to audit.
Introduce shared, tested conversions rather than globally replacing constants.
The helpers must represent the terrain anchor and all covered World tiles.

Audit rule:

- keep 2048 when changing `.w` tile coordinates, normalizing a World position,
  or positioning one World file relative to another;
- keep 1024 when it is the half-size/centre of a World file;
- use `T` for a terrain's extent;
- use `S` for sample steps;
- use `R` for cells within a patch;
- use `T / P` for physical patch size.

Also define exact-edge behavior. `getRotation()` currently lacks some of the
edge adjustment used by `getHeight()` and can address beyond the border.

### 11. Neighbor stitching rejects different grids

`fillTerrainDataX/Y/XY()` only copies a border when neighboring `N` and `S`
match. That is safe for a uniform resolution but leaves the initially
duplicated edge for mixed neighboring resolutions.

Required policy:

- if mixed adjacent resolutions are outside the first release, detect and
  report them rather than silently keeping a false seam; or
- sample the adjacent edge at this terrain's world-space positions.

Index-for-index copying is invalid between unlike grids. Test both directions
if mixed resolution is claimed.

### 12. `sampleSize` is parsed as float but truncated by the API

`TFile::sampleSize` is a `float*`, while `Terrain::getSampleSize()` and many
locals/creation methods use `int`. `sampleRotation` is parsed and written but
not applied.

Required decision:

- either preserve floating-point spacing through all coordinate APIs; or
- validate finite, positive, integral spacing and expose that supported
  restriction explicitly.

Likewise, either implement grid rotation or require zero rotation with a clear
unsupported-file diagnostic.

### 13. Token 281/AS buffer must be length-driven, not assumed to be 257 x 257

The binary child-block header already defines this buffer's stored size. The
layout read by `TFile::get139()` is:

```text
uint32 token = 281
uint32 blockLength
uint8  labelLength
utf16  label[labelLength]
byte   payload[blockLength - 1 - 2 * labelLength]
```

For the normal MSTS buffer, TSRE writes `blockLength = 8258` and an 8257-byte
payload because its label length is zero. `8257` is exactly
`ceil(257 * 257 / 8)`. The current reader ignores the parsed `blockLength` for
token 281 and always consumes 8257 bytes; the writer and parent
`terrain_samples` length calculation are hard-coded in the same way.
The parser subsequently seeks to the declared child-block end, so a larger
buffer remains aligned but is truncated in memory/on save; a smaller or corrupt
buffer can be read past its declared child boundary before that seek.

`terrain_nsamples` is defined earlier in the conventional `terrain_samples`
grammar, before the optional AS buffer. Nevertheless, an opaque buffer does
not need that ordering to be read safely: its own block header is authoritative
for byte count. `nsamples` is only needed for later semantic validation.

AS is MSTS's fine-grained "always selected" mask for its adaptive terrain mesh.
MSTS normally renders a mesh coarser than the stored 8 m height grid and refines
it dynamically as the viewer approaches. Vertices that must retain detail at a
distance, notably around track and other important edited features, can be
marked in this mask by the MSTS Route Editor. The standard payload therefore
contains one bit for every vertex in the 257 x 257 lattice. Inspected route
files also contain sparse set bits at the expected hierarchical subdivision
coordinates, consistent with that use.

TSRE does not implement this per-vertex adaptive mesh. Its compatibility
strategy is deliberately coarser: when terrain heights are edited, it sets the
containing patch's `ErrorBias` to zero so MSTS keeps the whole patch at full
detail. This is done by `Terrain::setHeight()`, `TerrainLibQt::setHeight256()`,
and the height-painting path through `Terrain::setErrorBias()`. Despite often
being described as a patch "high-resolution flag", this fork stores the
behavior in the `terrain_patchset_patch` error-bias field; `TFile::flags` is
used for other patch properties such as drawing and water.

The exact AS bit order need not be understood for this resolution task. The
independent [`msts-tools` terrain grammar](https://github.com/twpol/msts-tools/blob/d34243e2821e34af47f9e917034040f289400fc0/Resources/terrain.bnf)
only identifies both fields as opaque buffers. Open Rails likewise
[`Skip()`s AS and token 282 with TODOs](https://github.com/openrails/openrails-unstable/blob/41fbd610f3221ece60ef762b50d2f708e92eda9d/Source/Orts.Formats.Msts/TerrainFile.cs#L134-L142),
and two other parsers preserve parser alignment by skipping the declared block
length rather than assuming 8257 bytes: [ZR](https://github.com/djw-zr/ZR/blob/b87f156c3036342ef78d47fb416f4b962d946614/src/input/terrain.c#L247-L265)
and [msts-parser](https://github.com/vitro-mod/msts-parser/blob/0c02e59eb3605de3158829d319cbee39afe6e3e9/src/parsers/TIle/BinaryTileParser.ts#L83-L87).

#### The E buffer is an external adaptive-LOD error field

`terrain_sample_ebuffer` is not an embedded buffer like AS. Token 147 stores a
UTF-16 filename, normally `<tile>_e.raw`, and the binary data lives in that
separate file. TSRE reads and writes the filename but never loads or uses the
E-RAW contents.

An audit of 1,351 installed binary `.t` files and their RAW files found 1,257
E files. Every E file belonging to an `N=256` tile was exactly 262,144 bytes,
which is `N * N * sizeof(float)`. Decoding representative files as
little-endian IEEE-754 float32 produced finite, non-negative values. Flat tiles
were all zero; non-flat tiles were mostly zero with positive values at
hierarchical subdivision coordinates. The same large value can occur at a
sequence of coarser-to-finer coordinates, consistent with a child error being
propagated upward through an adaptive-mesh hierarchy.

The strongest supported interpretation is therefore an `N x N` geometric
error field used to decide whether adaptive terrain triangles may be
coarsened. It is reasonable to describe an entry as an error value associated
with a sample position, but **not** as a proven one-to-one value for every
rendered `(N+1) x (N+1)` vertex. The exact indexing, error formula, propagation
algorithm, units, and interaction with AS have not been formally documented.

This must not be confused with `terrain_patchset_patch::ErrorBias`:

- E-RAW contains an `N x N` float field derived from terrain geometry for the
  fine-grained adaptive mesh;
- `ErrorBias` is one float per patch and acts as a coarser LOD control. TSRE
  sets it to zero on modified patches as its full-patch compatibility fallback.

E and N are regenerable derived buffers in the MSTS toolchain. The
[DEMEX documentation](https://www.digital-rails.com/files/demex_tutorial.pdf)
instructs authors to delete `_e.raw` and `_n.raw` after replacing elevations so
MSTS can rebuild them with updated error-bias and normal-shading information.
Consequently, ordinary load/save must preserve their references and files, but
replacing terrain with another resolution must not leave old-dimension E/N
files referenced as though they were valid. This task may invalidate/remove
those stale derived files; it does not need to reverse-engineer or generate E.
Do not assume the MSTS generator can produce valid E files for 512 or 1024
samples without a compatibility test.

For a modern TSRE renderer, neither file should be authoritative runtime data.
The installed `N=256` N-RAW files are `N * N` bytes and contain old MSTS
normal-shading data; normals can instead be derived from the height texture on
the GPU whenever terrain changes. E is also small enough to rebuild from Y at
tile load or after an edit. A `1024` grid contains only 1,048,576 samples, and
the bottom-up hierarchy is linear work. Keep E/N only as MSTS interchange
artifacts unless profiling demonstrates that a disk cache is useful.

#### Likely relationship between E, AS, and patch ErrorBias

The evidence supports three separate inputs to one adaptive-LOD decision, not
three encodings of the same state:

1. E supplies a view-independent geometric approximation error for candidate
   vertices. A renderer projects that error according to camera distance and
   compares it with an allowed screen-space error.
2. AS is an authored forced-selection mask. It can retain a vertex even when
   its geometric error alone would permit removal. Dependencies required for a
   crack-free hierarchy must also be retained, either because the stored mask
   already contains the closure or because the renderer expands it.
3. Patch `ErrorBias` changes the allowed error/refinement policy for an entire
   patch. The documented MSTS behavior is that the default `1` permits normal
   simplification and `0` produces finer detail. Whether MSTS multiplies a
   threshold, divides an error, or special-cases zero is not known.

This is consistent with restricted-quadtree terrain methods. They compute a
local error such as the vertical difference between a candidate vertex and its
coarser interpolation, then propagate maximum errors through the dependency
graph. That "error saturation" makes a top-down threshold test produce a
matching triangulation without resolving forced splits afterward. See the
[terrain multiresolution survey](https://www.crs4.it/vic/data/papers/tvc2007-semi-regular.pdf)
and the original
[restricted-quadtree publication](https://www.ifi.uzh.ch/en/vmml/publications/vis-98.html).
The hierarchy visible in MSTS E-RAW values is a strong match for this model,
but it is not proof that MSTS implements the published formula unchanged.

The route corpus gives useful separation evidence:

- the 436 AS-bearing tiles contained 409,590 set AS bits, all inside the active
  `256 x 256` E lattice rather than on the extra row/column of the nominal
  `257 x 257` mask;
- 240,605 selected positions had positive E, but 168,985 (41.3%) had `E=0`.
  Therefore AS cannot merely mean `E > threshold`; it preserves points the
  geometric metric would otherwise be free to discard;
- positive E at AS positions averaged about `6.69`, versus `0.87` at other
  positive-E positions. This correlation is expected around track cuts and
  embankments, but E and AS are not interchangeable;
- all 345,856 audited patch records used only `ErrorBias` values `0` or `1`.
  Both values occur with and without AS, and their patch-level overlap is low,
  so patch bias is an independent coarse control rather than a summary of AS;
- wherever present in the audited files, `terrain_errthreshold_scale` was `1`
  and `terrain_alwaysselect_maxdist` was `0`. Their exact zero/default semantics
  still require a controlled MSTS experiment.

The established TSRE workflow also changes patch `ErrorBias` without rewriting
E-RAW, yet relies on MSTS displaying that patch at finer detail. This strongly
indicates that MSTS applies patch bias at selection/render time rather than
baking it into E.

A useful working model for a future renderer is therefore:

```text
refine vertex = forced_by_AS_at_this_distance
             OR projected_screen_error(E, camera) exceeds patch tolerance
```

`terrain_errthreshold_scale`, the simulator's terrain-detail setting, and
patch `ErrorBias` probably contribute to `patch tolerance`.
`terrain_alwaysselect_maxdist` probably limits the distance over which AS is
forced. Those placements are deductions from the field names and observed
behavior, not yet a recovered MSTS equation.

For a modern implementation, regenerate geometric E in memory from Y when a
tile is loaded or edited; do not read it every frame. Preserve AS separately
because it contains author intent that cannot be reconstructed from heights,
and retain patch bias as separate authored metadata. A transient refinement
hierarchy may then combine the three efficiently: inject AS vertices (and all
vertices of a forced-detail patch) as forced/infinite importance, propagate the
dependency closure together with geometric errors, and perform one top-down
selection pass. Do not write that combined runtime metric back as if it were
the original geometric E-RAW. Give the observed patch-bias values `0` and `1`
explicit compatibility behavior. This lets E be recomputed cheaply while AS
and patch metadata remain stable.

#### The optional US buffer is still unknown

Token 282, conventionally named `terrain_sample_usbuffer`, is an embedded,
length-delimited opaque child of `terrain_samples`, near AS. Unlike E, it does
not name an external RAW file. Its expansion, bit layout, and purpose remain
unknown; "user select" or a working selection mask would be plausible guesses,
but there is not enough evidence to put either interpretation into code.

No US block was found in the 1,351 installed `.t` files audited for this task.
By comparison, 436 contained AS, always with the standard 8,257-byte payload.
The public parsers reviewed also do not decode US:
[msts-parser labels both AS and US formats unknown](https://github.com/vitro-mod/msts-parser/blob/0c02e59eb3605de3158829d319cbee39afe6e3e9/src/parsers/TIle/BinaryTileParser.ts#L79-L87),
while Open Rails handles numeric token 282 only by skipping its declared block.
This absence of fixtures means there is no evidence that US has the AS size
formula, even if it eventually proves to be another bit mask.

TSRE currently reaches its default token handler for 282 and does not write the
block back, so a rare tile containing US would lose it on save. Token 282 should
therefore use the same generic length-safe opaque-block preservation mechanism
as AS: retain its label/payload and ordering exactly, do not interpret or resize
it, and omit it from newly created terrain.

Required first-stage behavior:

- replace the raw pointer with a byte container carrying its actual length;
- validate `blockLength >= 1`, then after reading `labelLength` validate
  `blockLength >= 1 + 2 * labelLength`, using checked arithmetic throughout;
- validate that the complete child block stays inside its parent/file boundary;
- consume the label-length byte and label, then read exactly the remaining
  `blockLength - 1 - 2 * labelLength` payload bytes;
- preserve and write those bytes exactly, recomputing this child block and its
  parent block lengths from the stored payload;
- apply the same length-safe opaque preservation to token 282/US, but do not
  apply AS's expected-size formula to it;
- after the complete `terrain_samples` block is parsed, optionally compare the
  payload with `ceil((N + 1)^2 / 8)` (`8257` for `N=256`, `32897` for
  `N=512`), but report a mismatch without over-reading or silently resizing;
- never pad, truncate, or reinterpret an existing AS buffer merely because the
  height grid resolution changed.

Do not add AS editing or MSTS-style adaptive triangulation to this task. Keep
the existing patch-level `ErrorBias = 0` compatibility strategy for modified
terrain. Its `getPatchCoords()` lookup already derives patch size from `N`,
sample spacing, and patch count; retain that behavior and add 512/4 boundary
tests. Preserve existing AS and US buffers so unmodified MSTS information
survives a TSRE load/save.

#### Future path beyond 512/4: adaptive triangulation

`512 @ 4 m` is a reasonable practical maximum for TSRE's current static,
full-resolution terrain mesh. A `1024 @ 2 m` grid would provide valuable 2 m
editing detail, but drawing every stored cell would be disproportionately
expensive, especially across many visible terrain tiles:

| Grid | Vertices per tile | Triangles per full tile | Relative to 256 |
| --- | ---: | ---: | ---: |
| `256` | `257^2 = 66,049` | `2 * 256^2 = 131,072` | `1x` |
| `512` | `513^2 = 263,169` | `2 * 512^2 = 524,288` | `4x` |
| `1024` | `1025^2 = 1,050,625` | `2 * 1024^2 = 2,097,152` | `16x` |

Supporting grids larger than 512 should therefore trigger a separate design
for a distance/error-driven adaptive mesh, preferably compatible with the MSTS
terrain mechanism rather than merely increasing static GPU buffers. That work
should investigate together:

- AS bit ordering and hierarchical vertex selection;
- US semantics and its relationship, if any, to AS;
- the E-RAW indexing, error calculation, propagation, and regeneration
  algorithm;
- `terrain_errthreshold_scale` and `terrain_alwaysselect_maxdist`;
- per-patch `ErrorBias`, including TSRE's full-patch fallback;
- crack-free transitions between different refinement levels and across
  patch/tile boundaries;
- editing invalidation and regeneration of selection/error data.

The current resolution work should not make that future implementation harder:
keep grid dimensions and patch resolution explicit, retain AS/US/E metadata,
and avoid APIs or buffer ownership that assume every stored sample must always
become a rendered vertex. The paged renderer supports `1024 @ 2 m`; TSRE
recommends P16 to retain 128 m physical patches, while P32 is the compatible
choice for patched MSTS. GUI creation above 1024 remains unavailable until a
separate policy and adaptive-mesh design exist.

For newly generated 512/4 or experimental 1024/2 tiles, omit the optional AS
and US blocks until their creation semantics and simulator compatibility are
known. That is safer than fabricating a 513 x 513 or 1025 x 1025 mask, and is
consistent with current `TFile::initNew()` behavior. The patches changed by
terrain generation/editing must continue to receive `ErrorBias = 0`. Semantic
AS/US generation and MSTS-style adaptive terrain rendering are separate future
tasks.

Follow-up renderer work is specified separately in
[`terrain-paged-mesh-and-shared-map.md`](terrain-paged-mesh-and-shared-map.md).

## External implementation review

### Open Rails unstable

Reviewed repository: <https://github.com/openrails/openrails-unstable>, branch
`unstable`, commit `41fbd610f3221ece60ef762b50d2f708e92eda9d`.

Compatibility evidence only:

- [`Tiles.cs`](https://github.com/openrails/openrails-unstable/blob/41fbd610f3221ece60ef762b50d2f708e92eda9d/Source/RunActivity/Viewer3D/Tiles.cs)
  keeps the World lattice at 2048 m and separately stores terrain `Size` in
  World-tile units, `SampleCount`, `SampleSize`, and `PatchCount`.
- World coordinates are first normalized with 2048/1024, then mapped into the
  physical terrain tile and divided by that tile's `SampleSize`. Neighbor
  sample lookup similarly crosses the 2048 m lattice before addressing the
  other terrain grid.
- [`Terrain.cs`](https://github.com/openrails/openrails-unstable/blob/41fbd610f3221ece60ef762b50d2f708e92eda9d/Source/RunActivity/Viewer3D/Terrain.cs)
  calculates `PatchSampleCount = SampleCount / PatchCount`, sizes per-patch
  vertex/index data from that value, and positions vertices using
  `Tile.SampleSize`. The
  [higher-resolution terrain commit](https://github.com/openrails/openrails-unstable/commit/6dca4056a407734483371ef9de05c576a11f9c20)
  is narrowly a renderer change: it generalizes per-patch mesh and index-buffer
  sizing. It does not demonstrate TSRE-compatible terrain QuadTree discovery,
  editing, creation, saving, import, or conversion.
- [`TerrainAltitudeFile.cs`](https://github.com/openrails/openrails-unstable/blob/41fbd610f3221ece60ef762b50d2f708e92eda9d/Source/Orts.Formats.Msts/TerrainAltitudeFile.cs)
  allocates and reads by `sampleCount`.

ORTS has no equivalent of TSRE's full terrain QuadTree/editor workflow, so its
code must not be used as the architecture for this task. It is useful for
runtime formulas and for checking files intended to load in ORTS. Even within
ORTS, support is not complete across all tooling: its
[`TerrainValidator.cs`](https://github.com/openrails/openrails-unstable/blob/41fbd610f3221ece60ef762b50d2f708e92eda9d/Source/Contrib/DataValidator/TerrainValidator.cs)
still warns unless detailed terrain is 256/8 (and low terrain 64/256). This is
a useful warning for TSRE: a metadata-driven runtime renderer is not proof that
editors, validators, importers, or converters support the same profiles.

### TSRE5-SCOmod

Reviewed repository: <https://github.com/scottb613/TSRE5-SCOmod>, branch
`master`, commit `1fef2c9e06f3ff0c4c432b424aad4ae121ddff73`.
Its README describes 4 m terrain as experimental.

Compatibility findings and implementation ideas to evaluate independently:

- [`TerrainGridMath.h`](https://github.com/scottb613/TSRE5-SCOmod/blob/1fef2c9e06f3ff0c4c432b424aad4ae121ddff73/TerrainGridMath.h)
  explicitly fixes the supported patch dimension at 16, checks divisibility,
  calculates render/payload sizes with overflow checks, and checks the signed
  OpenGL buffer-size limit. This matches the scope of this task.
- [`Terrain.cpp`](https://github.com/scottb613/TSRE5-SCOmod/blob/1fef2c9e06f3ff0c4c432b424aad4ae121ddff73/Terrain.cpp)
  rejects invalid descriptors early, makes destructor/VBO/staging/blob/line
  sizes resolution-dependent, validates exact RAW/F sizes, uses `R` for F
  addressing, and applies `16.0 / R` to existing patch UV transforms. The last
  behavior is a confirmed texture-coordinate bug and must not be copied.
- [`Undo.h`](https://github.com/scottb613/TSRE5-SCOmod/blob/1fef2c9e06f3ff0c4c432b424aad4ae121ddff73/Undo.h)
  and [`Undo.cpp`](https://github.com/scottb613/TSRE5-SCOmod/blob/1fef2c9e06f3ff0c4c432b424aad4ae121ddff73/Undo.cpp)
  replace the 257-squared array with a vector plus sample count.
- [`HeightWindow.cpp`](https://github.com/scottb613/TSRE5-SCOmod/blob/1fef2c9e06f3ff0c4c432b424aad4ae121ddff73/HeightWindow.cpp)
  records the cached allocation's actual resolution.
- [`TerrainLibQt.cpp`](https://github.com/scottb613/TSRE5-SCOmod/blob/1fef2c9e06f3ff0c4c432b424aad4ae121ddff73/TerrainLibQt.cpp)
  moves significant height/track editing to per-terrain `sampleSize` and
  dynamic undo snapshots.

SCOmod is not a reference implementation or a patch set to copy wholesale. It
is useful for identifying affected paths and compatibility expectations, but
each behavior still needs to be checked against TSRE's format model and focused
tests. In particular:

- its fixed-16 texture-domain assumption and `16/R` transform compensation are
  contradicted by both Open Rails' raw `PatchSampleCount` coordinates and the
  direct MSRE `R=8`/`R=16` rendering test above;
- the token-281 AS buffer remains fixed at 257 x 257 bits;
- `sampleSize` still passes through integer APIs in important paths;
- some fixed 8/16/256 code remains, including legacy/alternate renderer code;
- some 1024-centred editing helpers target the 2048 m detailed-terrain case
  rather than proving all physical terrain sizes;
- its changes include many unrelated features and safety work.

## File-by-file implementation checklist

### `src/routeEditor/RouteEditorGLWidget.cpp`

- use the shared profile selector in the B-key dialog and GeoTools automatic
  profile dialog;
- initialize B from the automatic default and pass its one-off named profile
  only to the detailed terrain branch, including confirmed terrain overwrite;
- create a missing World file without attaching any resolution metadata and
  preserve an existing World file;
- make automatic and marker-based `createNewTiles()` use the selected detailed
  profile while leaving distant-tile creation unchanged.

### `src/tsre/world/Route.h` and `Route.cpp`

- document/refactor the current `newTile()` coupling between World and terrain
  creation rather than adding terrain resolution to the World-tile concept;
- pass the current automatic detailed-terrain profile explicitly from
  `newTile()`;
- provide or expose a World-only helper if the `B` coordinator needs one;
- keep all World APIs profile-free and route the selected profile directly to
  detailed terrain creation;
- reload before optional geodata filling so editing observes the selected grid.

### `src/tsre/world/Terrain.h` and `Terrain.cpp`

- add the validated grid layout and named World constants;
- represent the four GUI choices as validated named layouts and feed their
  values to the existing parameterized `Terrain::SaveEmpty()` implementation;
- make height/F ownership size-aware;
- fix mesh, scratch, blob, and overlay allocations/counts;
- make gaps and UV calculations use `N`, `S`, and `R`;
- centralize World/terrain/sample/patch conversions and edge handling;
- retain `ErrorBias = 0` on every patch touched by height editing/generation;
- preserve the physical terrain anchor across its covered `.w` files.

### `src/tsre/world/TFile.h` and `TFile.cpp`

- validate metadata before terrain allocation;
- make tokens 281/AS and 282/US size/payload aware through one opaque-child
  representation;
- initialize patch UV transforms from `R`;
- preserve byte-compatible output for standard 256/8 tiles.

### `src/tsre/world/TerrainLibQt.cpp`

- add an explicit-profile detailed `saveEmpty()` path while preserving the
  256/8 default and the separate low-terrain branch;
- convert height brushes, track-bed work, and route merging to per-terrain
  sample/patch metadata;
- preserve 2048/1024 only for World-coordinate traversal;
- implement or explicitly reject mixed-resolution seams.

### `src/tsre/world/TerrainLibSimple.cpp`

- preserve its no-profile 256/8 creation behavior;
- if the `B` profile can reach this runtime mode, delegate to the shared
  profile-aware creation path or reject experimental profiles before writing.

### `src/tsre/Undo.h` and `Undo.cpp`

- use dimensioned dynamic snapshots and validate restore compatibility.

### `src/tsre/geo/HeightWindow.h` and `HeightWindow.cpp`

- make cached output ownership dimension-aware;
- preserve spacing precision according to the chosen policy.

### `src/tsre/world/TerrainClient.cpp` and networking

- validate `.t` before committing received height/F bodies;
- validate exact variable payload sizes;
- initialize fixed-capacity patch state only after the descriptor confirms
  `1 <= P <= 32`, and operate only on the active `P * P` records.

## Implemented sequence

1. Add `TerrainGridLayout`, named World constants, checked size calculations,
   early descriptor/payload validation, and length-aware opaque AS/US-buffer
   preservation. Accept regular grids with `1 <= P <= 32` and validated R.
2. Make height/F ownership and undo dimension-aware.
3. Fix VBO, scratch, blob, and line buffers; prove visual parity on 256/8,
   then load/render 512/4.
4. Convert gaps, height tools, track deformation, route merge, and geodata to
   shared conversions.
5. Fix UV generation and all texture operations for `R != 16`.
6. Update/guard `TerrainLibSimple` and validate client/server paths.
7. Add the shared profile widget, the B-key replacement dialog, the GeoTools
   automatic default, and separated World/terrain creation branches.
8. Run save/reload, edge, 4096 m coverage, malformed-file, GUI-scope, and
   memory tests.

## Regression matrix

| Profile | Purpose |
| --- | --- |
| `128 @ 16 m, P=16` | smaller allocation, 2048 m terrain, `R=8` |
| `256 @ 8 m, P=16` | compatibility baseline, 2048 m terrain, `R=16` |
| `512 @ 4 m, P=16` | required high-resolution case, 2048 m terrain, `R=32` |
| `1024 @ 2 m, P=16` | preferred current-TSRE ultra profile, `R=64`; not patched-MSTS compatible |
| `256 @ 16 m, P=16` | 4096 m terrain covering 2 x 2 World files |
| existing distant-terrain profile | prove its 4 x 4 grid loads, renders, edits, and saves |
| `250 @ 8 m, P=16` | reject because `N % P != 0` |
| valid `N/S`, `1 <= P <= 32`, `4 <= R <= 128` | load/render/edit/save |
| `P > 32`, `R < 4`, or `R > 128` | reject as unsupported |
| existing AS block | preserve its declared payload byte-for-byte on save |
| absent AS block | preserve absence, including newly created 128/16, 512/4, and 1024/2 terrain |
| synthetic token-282/US block | preserve its label and arbitrary payload byte-for-byte without assigning semantics |
| existing E-RAW | recognize the expected `N * N * 4` shape; preserve it for an unchanged tile |
| resolution-changing overwrite with E/N | invalidate/remove stale derived buffers and do not leave incompatible files referenced |
| truncated/oversized RAW, float RAW, or F | reject before reading/commit |
| non-zero rotation or fractional `S` | support deliberately or reject clearly |

For every supported profile, verify load/render, centre/corner/edge height and
slope, seams, all height tools, undo, gaps, textures/UVs, geodata, save/reload,
and client/server round trip. Verify that height editing sets `ErrorBias = 0`
on every affected patch without changing preserved AS or US buffers. Repeat
load/unload under AddressSanitizer or an equivalent heap checker. Include both
P16 and P32 in 1024/2 functional and performance comparisons; adaptive
triangulation remains separate future work.

GUI-scope regression tests must verify:

- both selectors offer exactly 128/16, 256/8, 512/4, and 1024/2, plus valid
  P=4/8/16/32 choices, and initially default to 256/8, P=16;
- changing the automatic default changes subsequent navigation-created and
  marker-generated detailed terrain; B opens on that default but remains a
  one-off selection;
- compatibility labels and green/red/amber states follow
  `msts-orts-terrain-profile-compatibility.md`;
- each choice writes matching `.t` metadata and an exact `N * N * 2` Y-RAW;
- cancel writes nothing; overwrite retains the selected profile, recreates Y,
  and does not leave old-dimension E/N data referenced by the new descriptor;
- creating/replacing terrain beside an existing `.w` file leaves that World
  file byte-identical, while a missing `.w` file is created without resolution
  metadata;
- the overwrite prompt follows detailed-terrain existence and does not describe
  an existing World tile as terrain that must be replaced;
- new-route bootstrap terrain remains 256/8 after changing the automatic
  profile;
- low/distant terrain creation is unchanged;
- 1024/2 P16 and P32 tiles can be created, loaded, edited, saved, and reloaded;
  performance comparisons retain P16 as the native TSRE recommendation and
  P32 as the patched-MSTS-compatible choice.

The 4096 m case additionally verifies:

- all four covered 2048 m World files remain independently loaded and edited;
- terrain queries from each World file map into the correct quadrant;
- objects crossing a World-file boundary do not cause a terrain seam;
- no World-coordinate 2048 constant was incorrectly replaced by terrain size.

## Acceptance criteria

- `256 @ 8 m` and `512 @ 4 m`, with any valid patch count up to 32 x 32, load,
  render, edit, undo, save, and reload without out-of-bounds access or UV
  distortion.
- The shared selector creates all four named profiles with selectable 4 x 4,
  8 x 8, 16 x 16, or 32 x 32 patch grids, clearly labels compatibility, and
  supplies both the B workflow and automatic detailed-terrain creation.
- 1024/2 P16 is presented as TSRE's recommended ultra profile; 1024/2 P32
  remains supported and is identified as the patched-MSTS-compatible choice.
- Existing non-2048 terrain sizes remain supported; the 4096 m fixture covers
  four independent `.w` files correctly.
- Unsupported patch counts and malformed/oversized payloads fail before
  allocation or live-state mutation with complete diagnostics.
- No production height-grid code uses literal 256, 257, 16, 128, or 8 to mean
  `N`, `N+1`, `R`, patch metres, or `S`, except named profile/default values.
- Literals/named constants 2048 and 1024 remain where they express the fixed
  World-file coordinate lattice.
- Both selectable terrain-library paths are either compliant or explicitly
  guard unsupported grids.
