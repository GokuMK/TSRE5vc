# Paged terrain mesh and shared map rendering

Status: proposed.

Related tasks:

- [Terrain heightmap resolution support](terrain-heightmap-resolution.md)
- [Variable terrain patch count](terrain-patch-count.md)
- [MSTS adaptive terrain LOD executable analysis](msts-terrain-adaptive-lod-executable-analysis.md)
- [Renderer task roadmap](renderer/00-task-roadmap.md)

This is a separate renderer/memory task, not unfinished heightmap-resolution
work. Its first goal is a patch-addressable static mesh suitable for 1024/2
editing and later LOD work. The first implementation uses an aligned 12-byte
vertex with float height and explicit patch-local sample coordinates. The
design must not assume that MSTS adaptive triangulation will be implemented
immediately.

## Stage boundary

This task has a strict stop between implementation and later optimization.

**Stage 1 -- implement and validate the 12-byte paged backend:**

- preserve the legacy backend as the comparison reference;
- implement the indexed paged mesh using `TerrainVertex12`;
- share its vertices with map rendering;
- implement patch parameter aggregation and modified-patch refresh;
- verify rendering/editing correctness and compare its performance and memory
  use with the legacy renderer.

Stage 1 is complete only after the 12-byte backend passes the verification and
legacy-comparison gates near the end of this document. Stop there. Do not begin
an 8-byte implementation as part of Stage 1, even if its data structures appear
easy to add while building the paged backend.

**Stage 2 -- optional measured vertex-format optimization:**

Stage 2 is a separate follow-up after Stage 1 results have been reviewed. It
may prototype float-height/derived-coordinate and raw-height/explicit-coordinate
8-byte formats. Its height encoding, editing behavior, and acceptance criteria
must be clarified using measurements from the working 12-byte implementation.

## Preserve the legacy implementation for comparison

Keep the current renderer available for controlled performance, memory, and
visual-parity comparisons. Do not create a second complete terrain data class
such as `TerrainOld : public Terrain`: loading, editing, selection, Undo, and
saving must operate on the same `Terrain` object and data in both modes.

Split GPU-mesh ownership behind a terrain rendering backend instead:

```text
Terrain
|-- descriptor, height/F data, patch state, editing, selection, save/load
`-- TerrainMeshBackend
    |-- TerrainMeshLegacy
    `-- TerrainMeshPaged
```

`TerrainMeshLegacy` should retain the existing tile-wide VBO, baked UVs,
`terrainBlob`, `glDrawArrays()` calls, and whole-tile regeneration as closely
as practical. `TerrainMeshPaged` owns the new indexed pages, packed normals,
patch parameter blocks, gap attributes, and shared map rendering.

Select the backend with a startup/runtime setting such as `legacy` or `paged`.
A route reload is acceptable when changing it and is preferable for clean
measurements. Only the selected backend may own GPU resources; do not keep both
representations resident during a comparison.

The work outside `Terrain` should remain additive:

- retain the legacy baked-`aTextureCoord` shader path;
- extend render packets and the OpenGL renderer with indexed/base-vertex and
  parameter-block support without removing `glDrawArrays()` fields;
- let any separate map pass dispatch to `terrainBlob` in the legacy backend
  and shared patch geometry in the paged backend;
- keep current loading, editing, selection, Undo, networking, and save APIs
  common to both backends.

The current `Terrain::render()`, `pushRenderItem()`, and `refresh()` methods are
not virtual, and `TerrainLibQt`/`TerrainLibSimple` construct concrete `Terrain`
objects directly in several places. This is another reason to prefer backend
composition over subclassing the complete `Terrain` class.

## Paged patch vertex and index layout

Every resident patch has a fixed, complete `(R + 1) x (R + 1)` vertex block,
where `R = N / P`. Patches are stored in shared VBO pages rather than one VBO
per patch. A page must contain patches with one vertex layout and one LOD, so
all its patch slots have the same `verticesPerPatch` value.

Validate the layout centrally before creating renderer resources:

```text
N % P == 0
4 <= R = N / P <= 128
```

Reject layouts outside this range as unsupported. `R = 2` has too few cells
to justify new compatibility paths and provides almost no useful recursive
LOD hierarchy. `R > 128` makes physically oversized patches, weakens future
patch-distance LOD, and is unnecessary for the supported 1024/32 case
(`R = 32`). The upper bound also guarantees that a complete patch has at most
`129^2 = 16,641` vertices and therefore uses 16-bit patch-local indices.

Stage 1 uses this aligned record:

```cpp
struct TerrainVertex12 {
    float height;             // 4 bytes
    uint32_t packedNormal;    // 4 bytes, GL_INT_2_10_10_10_REV
    uint8_t localSampleX;     // 0..R inclusive
    uint8_t localSampleZ;     // 0..R inclusive
    uint8_t gap;              // MSTS F-buffer bit 0x04
    uint8_t reserved;         // retain for a measured future use
};
static_assert(sizeof(TerrainVertex12) == 12);
```

Do not store float X/Z or baked UV coordinates. Reconstruct X/Z from the
patch origin, explicit local sample coordinates, and sample spacing in the
vertex shader. The 12-byte format is deliberately the complete Stage 1 format:
it keeps every 32-bit field and every record four-byte aligned while avoiding
the height-quantization lifecycle in the initial implementation.

Do not remove or compact vertices to represent gaps. The initial regular
topology is:

```text
patch vertex block
|-- float height
|-- packed normal
|-- local sample X/Z
|-- gap flag
`-- regular all-triangles index template
```

- The regular index template is shared by every patch with the same `R`/LOD.
- A hidden patch is skipped by the terrain pass; hiding does not require a
  custom mesh.
- The map pass always uses the regular all-triangles indices, regardless of
  terrain gaps or hidden-patch state.
- Deliberately duplicated patch-edge vertices remain in their owning patch
  blocks. They permit independent regeneration and unambiguous material/UV
  ownership at a boundary.

TSRE currently interprets an F-buffer `0x04` sample as removing every
triangle which contains that vertex. Preserve that behavior initially with an
interpolated keep value and fragment discard. An unflagged vertex outputs
`1.0`; a flagged vertex outputs `0.0`; a terrain fragment is discarded when
the interpolated value is below a threshold very close to `1.0`. Verify that
no visible sliver survives along the opposite triangle edge. Apply the gap
test to the main terrain, selection, shadow, and terrain depth passes. The map
pass deliberately ignores it. Do not overload texture/material alpha with
this value.

Gaps are exceptionally rare, so optimize for ordinary patches and correctness
rather than gap-heavy frame time. A separate gap shader variant is acceptable
if it lets ordinary terrain avoid gap-specific work. If interpolation produces
visible artifacts on supported hardware, fall back to custom indices only for
gap patches; retain the complete vertex blocks either way. Future LOD or truly
custom topology may independently require generated index lists.

Use 16-bit patch-local indices and draw the shared template with
`glDrawElementsBaseVertex()`. If patch slot `k`
starts at `k * verticesPerPatch`, OpenGL's effective `gl_VertexID` includes the
base-vertex offset, so the shader can derive:

```text
patchSlot = gl_VertexID / verticesPerPatch
```

This is safe under the fixed complete-block rule, including a fallback custom
index list which continues to reference the original block. It ceases to be
safe if an implementation compacts custom vertices, mixes different vertex
counts in one page, adjusts the attribute pointer instead of using
absolute/base-vertex indices, or otherwise abandons fixed patch slots.
Different LODs therefore belong in different pages. A page-local parameter
block lets `patchSlot` directly index its patch record without a per-patch
`glUniform1i()` call. Because local X/Z are explicit in the first-stage vertex,
the shader needs this division only for patch-record selection; it does not
also divide/modulo `gl_VertexID` to recover local coordinates.

## Share vertices between terrain and map rendering

Remove the separate paged-backend `terrainBlob`. Terrain and map rendering
must reference the same position/normal vertex blocks:

- the terrain pass binds the terrain patch parameters, applies gaps where
  present, and binds the patch terrain material;
- the map pass binds generated map patch parameters, always chooses regular
  indices, and binds the map texture.

The map may be organized later as `TerrainLib::renderMaps()` if Gather benefits
from an explicit pass. A separate pass must not imply a second vertex mesh or
require another shader. Keeping it inside the existing terrain ordering is an
acceptable lower-risk first implementation if that preserves current alpha
and depth behavior more easily.

## Local-sample-derived position and MSTS-compatible UVs

The paged vertex format should contain no UV attribute. Both terrain and maps
use the same MSTS affine patch equation:

```text
U = localSampleX * W + localSampleZ * B + X
V = localSampleX * C + localSampleZ * H + Y
```

Here `localSampleX` and `localSampleZ` range from zero through `R`. Store these
two integer coordinates directly in the first-stage vertex. They are the
native input to the texture equation and also reconstruct tile-local position:

```text
vertex.x = patchStartX + localSampleX * S
vertex.z = patchStartZ + localSampleZ * S
```

For example, 128 m corresponds to sample coordinate 16 on 256/8 terrain
because `128 / 8 = 16`. On 512/4 terrain the same distance corresponds to 32.

Do not reverse this relationship by storing float world X/Z and dividing it
again in the shader. Two `uint8_t` local coordinates replace two floats,
avoid boundary-owner ambiguity, and remove unnecessary reconstruction of the
texture-domain input.

The patch GPU record contains the original transform rows and the patch-local
origin:

```cpp
struct PatchGpuParams {
    vec4 uvAndOriginX; // W, B, X, patchStartX
    vec4 uvAndOriginZ; // C, H, Y, patchStartZ
};
```

The vertex shader uses:

```glsl
int patchSlot = gl_VertexID / verticesPerPatch;
PatchGpuParams patch = patchParams[patchSlot];
vec2 localSample = vec2(aLocalSample);

vec3 position = vec3(
    patch.uvAndOriginX.w + localSample.x * sampleSpacing,
    aHeight,
    patch.uvAndOriginZ.w + localSample.y * sampleSpacing);

vec2 uv = vec2(
    localSample.x * patch.uvAndOriginX.x
        + localSample.y * patch.uvAndOriginX.y
        + patch.uvAndOriginX.z,
    localSample.x * patch.uvAndOriginZ.x
        + localSample.y * patch.uvAndOriginZ.y
        + patch.uvAndOriginZ.z);
```

`sampleSpacing` is tile/page state, not a per-patch uniform update. The stored
terrain transform supplies `W/B/X/C/H/Y`. Map records use the same equation
and generated transforms. Expressed in patch-local sample coordinates for
patch `(patchX, patchZ)`, a whole-tile map has:

```text
W = 1/N     B = 0       X = patchX * R/N
C = 0       H = 1/N     Y = patchZ * R/N
```

This is equivalent to using tile-wide sample coordinates divided by `N`. It
is still the same shader equation; the map pass merely supplies different
parameter records and continues to use the regular all-triangles indices.

The common shader retains an additive uniform-controlled legacy branch:

- legacy backend: use baked `aTextureCoord` exactly as today;
- paged backend: obtain the affine rows from the patch parameter block and
  calculate position and UV from explicit local sample coordinates.

The condition is constant for a draw/page and has negligible GPU cost. The
performance concern is repeated CPU-side uniform binding, not this coherent
shader branch.

## Patch parameter aggregation

Do not upload six or more independent uniforms for every patch draw. Also do
not place every tile's records in the default vertex-uniform array. OpenGL 3.3
guarantees only 1024 default vertex-uniform components, shared with matrices
and all other vertex uniforms; 1024 patches times six values cannot fit.
Per-row arrays would add fragile rebinding and old-driver indexing behavior.

Use a UBO aligned with each VBO page. Two `vec4`s make each patch record 32
bytes. A page of 256 patch records consumes 8 KiB, below OpenGL 3.3's guaranteed
minimum 16 KiB uniform-block size. A 32 x 32 patch tile can consequently use
four such pages without relying on a driver-specific 32 KiB block.

The Stage 1 float-height format needs no height decode values. Do not reserve
or redesign this block for speculative raw-height requirements. Stage 2 must
first test direct use of MSTS tile-wide raw height and decode values during
editing. Only if measurements demonstrate a need for patch-local GPU encoding
should it expand this record or add another aligned metadata block. Three
`vec4`s per patch would consume 12 KiB for 256 patches and still fit within the
guaranteed 16 KiB block size, but that is not Stage 1 work or a selected Stage
2 design yet.

Bind the parameter block once per page and index it through the derived
`patchSlot`. This adds no UV uniform binding to the existing per-patch texture
and draw work. If a legacy-path prototype cannot use base-vertex-derived slots,
a single integer patch-slot uniform is a correct fallback and is preferable to
position-derived patch IDs.

Do not derive terrain patch ownership solely with
`floor(vertex.xz / patchWorldSize)`. A vertex exactly on a patch boundary has
the same position in two owning patch blocks, but the two copies may require
different textures and transforms. No positional epsilon can select both
owners correctly.

## Packed terrain normals

Replace three 32-bit normal floats with one normalized 32-bit packed value,
initially `GL_INT_2_10_10_10_REV`. OpenGL can expand it to a shader `vec3`, and
the existing fragment-stage normalization remains applicable. Ten signed bits
per component give a step of approximately `1/511` and an angular error around
one tenth of a degree or less, which should be visually indistinguishable for
ordinary diffuse terrain lighting.

Two `SNORM16` components plus reconstructed upward Y offer much greater
mathematical precision, but use the same four bytes and add reconstruction
work. Keep that as a measured alternative if packed-normal diagnostics,
grazing light, or future specular terrain reveals a visible problem. Validate
packed normals on flat, steep, noisy, and LOD-transition terrain before
removing the float-normal path.

Together with float height, explicit byte-sized local X/Z, a gap byte, and one
reserved byte, the first-stage target stride is 12 bytes rather than 32 bytes
for an indexed float-position/normal/UV layout. For a 1024/2 tile with 16 x 16
patches (`R=64`), complete duplicated patch blocks contain
`256 * 65^2 = 1,081,600` vertices: approximately 12.38 MiB at 12 bytes each
instead of 33.01 MiB at eight floats each, and far below the current
approximately 192 MiB non-indexed terrain VBO. The regular 64 x 64 16-bit index
template is only 48 KiB and can be shared. No second map vertex allocation is
present.

## Stage 2 only: measured 8-byte format experiments

**Stop: do not implement this section during Stage 1.**

Begin these experiments only after the 12-byte paged backend has established
rendering, editing, gap, map, and save/load parity and has been measured against
the legacy renderer. The Stage 1 results are the baseline for deciding whether
the extra complexity produces a worthwhile improvement.

### A. Float height with local coordinates derived from `gl_VertexID`

```cpp
struct TerrainVertex8Derived {
    float height;
    uint32_t packedNormal;
};
```

The fixed complete patch-block invariant permits the GLSL 3.30 vertex shader
to recover local coordinates:

```glsl
int patchSide = patchResolution + 1;
int patchSlot = gl_VertexID / verticesPerPatch;
int localId = gl_VertexID - patchSlot * verticesPerPatch;
int localSampleZ = localId / patchSide;
int localSampleX = localId - localSampleZ * patchSide;
```

This preserves float editing and avoids height rebasing, but adds integer
division/modulo-equivalent work to every processed vertex. `patchSide` varies
between terrain layouts, so do not assume that every driver will optimize it
as compile-time constant arithmetic.

### B. Raw height with explicit local coordinates

```cpp
struct TerrainVertex8Raw {
    uint16_t rawHeight;
    uint8_t localSampleX;
    uint8_t localSampleZ;
    uint32_t packedNormal;
};
static_assert(sizeof(TerrainVertex8Raw) == 8);
```

The shader reconstructs height from decode values associated with the current
GPU encoding:

```glsl
height = heightFloor + float(aRawHeight) * heightScale;
```

Do not assume a patch-local encoding before testing the simpler implementation.
MSTS `_y.raw` uses one `terrain_sample_floor` and `terrain_sample_scale` for the
complete tile. Stage 2 must investigate raw height in this order:

1. Upload the original tile-wide MSTS raw values directly and use the existing
   tile floor/scale.
2. Exercise real height editing and measure conversion, dirty-patch upload,
   normal regeneration, and cases in which an edit no longer fits the current
   raw range.
3. Determine whether retaining/reusing the tile encoding, occasionally
   rebasing it, or delaying some conversion is already fast enough. Do not
   assume that every edit requires new floor/scale values.
4. Consider a separate patch-local GPU raw encoding only if direct tile-wide
   raw behavior or measured full-tile rebasing is actually a problem.

The existing normal generator first accumulates face normals and then walks
vertices again to normalize them. During the experiment, the second pass can
also pack the normal, calculate the short height, and write the final GPU
record, avoiding a separate traversal. Measure this rather than assuming raw
conversion dominates the update.

If a later experiment selects patch-local GPU raw values, distinguish them
clearly from file `yraw`. It will need per-patch floor/scale metadata, rules for
save-time versus resident GPU encoding, and verification that independently
quantized copies of boundary vertices cannot produce cracks. Those policies
remain deliberately unresolved until the direct-`yraw` editing test provides
evidence.

Raw height plus explicit local coordinates is expected to trade one cheap
integer-to-float conversion and multiply-add for the extra integer divisions
in the derived-coordinate format, but expectation is not a performance gate.
Measure both on actual supported Intel, AMD, and NVIDIA hardware.

### Other layout candidate

A 10-byte structure-of-arrays representation can keep float height, packed
normal, and explicit byte local coordinates in separately aligned regions.
The VAO retains their attribute-buffer associations, so this does not require
rebinding every stream for every draw. Its costs are more complicated range
allocation, multiple patch-update writes, and possibly separate attribute
fetch transactions. It is lower priority than the two 8-byte candidates but
may be retained as a diagnostic if their shader or height-management costs are
unexpectedly high.

## Dirty-patch invalidation

Do not confuse render-resource dirtiness with persistent file modification.
`Terrain::modified` already means that terrain needs saving, and
`texModified` identifies changed texture files. Maintain a separate per-patch
GPU/render dirty mask, for example:

```text
DirtyHeight
DirtyNormals
DirtyGap
DirtyTopology
DirtyUvParams
DirtyMaterial
DirtyDrawState
```

Provide explicit operations along these lines:

```cpp
invalidatePatch(patchId, reasons)
invalidateSamples(sampleRectangle, reasons)
refreshModified()
refreshAll()
```

Mutations mark affected records; rendering lazily calls `refreshModified()`
and the active backend consumes them. The legacy backend treats any dirty
record as a whole-tile invalidation. The paged backend updates only the
necessary vertex block, gap attribute, optional custom indices, UBO record,
material, or draw state.

Retain current `refresh()` temporarily as an alias for `refreshAll()`. Existing
callers assume full invalidation, and several TerrainLib/edit/network paths
write public `terrainData` directly before calling it. Do not silently change
those calls to modified-only refresh until each mutation site identifies its
affected samples or patches.

Required invalidation behavior:

- height edit: dirty the affected patch vertices and normals, including patch
  neighbours reached by the normal-calculation halo;
- texture transform/reset: update only the patch parameter record;
- texture replacement: update material/draw state without rebuilding vertex
  positions;
- gap edit: update gap bytes in every owning patch block which duplicates the
  affected sample; if the interpolation fallback fails validation, rebuild
  custom indices only for those patches;
- hidden/patch draw flag: update draw state only;
- RAW/F replacement, reload, grid-layout change, or unknown bulk mutation:
  `refreshAll()`.

For height edits, accept a changed sample rectangle, expand it by one sample
for normal calculation, and translate the expanded rectangle into patch IDs.
This handles internal patch borders without scattered special cases. An edit
on the physical terrain-tile edge must also notify any loaded adjacent terrain
tile whose duplicated border heights/normals are affected.

## Verification and performance gates

### Stage 1 completion gate

Compare legacy and paged backends using identical route, camera, draw-distance,
map-overlay, texture, and editor state. At minimum cover 256/8, 512/4, and
1024/2 with 16 x 16 patches, plus the supported 32 x 32 test layouts.

Verify:

- terrain, selection, shadows, patch flags, hidden patches, gaps, and water
  retain visual/behavioral parity;
- the main terrain, selection, shadow, and depth passes all omit triangles
  touching a gap-flagged vertex, while the map pass ignores gaps;
- map overlays match current orientation, alpha, and coverage while allocating
  no second vertex mesh;
- the regular index template has the expected checkerboard diagonals and
  matches the legacy topology where no gaps exist;
- patch boundaries use the owning patch's transform and show no UV seams;
- texture changes update parameter/material state without rebuilding geometry;
- height edits regenerate only affected patches and normal neighbours;
- changing renderer mode releases the previous backend's resources;
- logged/observed CPU build time, frame time, draw count, buffer bindings, and
  GPU/CPU memory match the expected reduction.

Validate the layout limits explicitly:

- the supported matrix includes `R = 4`, `8`, `16`, `32`, `64`, and `128` where
  complete tile metadata is otherwise valid;
- `1024/32` (`R = 32`) loads, renders, edits, and saves;
- `R < 4`, `R > 128`, and non-integral `N/P` are rejected before allocations
  or RAW indexing;
- every supported patch remains addressable with 16-bit local indices.

Record the 12-byte backend's CPU build time, frame time, draw count, buffer
bindings, CPU/GPU memory, and modified-patch refresh time beside the legacy
backend results. These results complete Stage 1 and form the optimization
baseline. Stop after recording and reviewing them; do not continue directly
into an 8-byte rewrite as part of the same implementation stage.

### Stage 2 experimentation gate

This gate applies only when Stage 2 is started separately. Benchmark these
three vertex layouts through the same indexed draw path:

```text
C: float height + explicit local X/Z + gap/reserved       12 bytes (baseline)
A: float height + gl_VertexID-derived local X/Z            8 bytes
B: patch-local uint16 height + explicit local X/Z           8 bytes
```

Build each representation from the same loaded terrain and select it with a
development/runtime option. Do not compare different cameras or mixes of
visible patches. Use delayed/ring-buffered `GL_TIME_ELAPSED` queries so reading
results does not serialize the measured frame, disable VSync, discard warm-up
frames, and compare median and high-percentile terrain-pass time over several
hundred frames. Cover a close vertex-heavy view, the intended 3 x 3 high-detail
range, map rendering, and the real textured/shadowed pass. Also use a cheap
fragment pass to expose vertex-fetch and vertex-shader differences.

Measure CPU work separately: initial construction, one-patch height update,
normal-neighbour updates, one gap toggle, direct tile-wide raw-height updates,
and any forced height-range rebase. Test patch-local raw requantization only if
the earlier direct-raw experiments justify it. Compare image output, queried
terrain heights, boundary equality, and save/reload results in addition to
timing. Adopt an 8-byte format only after it beats or otherwise materially
improves on the 12-byte baseline without compromising these correctness
checks.

Crack-free transitions, distance-driven LOD selection, E/AS-guided adaptive
triangulation, and constant per-frame MSTS-style refinement remain subsequent
work. The page/backend design must leave them possible without making their
implementation a prerequisite for this static paged-mesh stage.
