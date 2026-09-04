# Paged terrain mesh and shared map rendering

Status: Stage 1 baseline preserved; Stage 2A 8-byte layout selected and
implemented. Interactive gap/map/selection coverage remains to be completed;
initial performance and memory comparisons are recorded below.

Related tasks:

- [Terrain heightmap resolution support](terrain-heightmap-resolution.md)
- [Variable terrain patch count](terrain-patch-count.md)
- [Basic discrete terrain patch LOD](terrain-basic-discrete-lod.md)
- [Terrain adjacent-edge cache](terrain-adjacent-edge-cache.md)
- [MSTS adaptive terrain LOD executable analysis](msts-terrain-adaptive-lod-executable-analysis.md)
- [Renderer task roadmap](../renderer/00-task-roadmap.md)

This is a separate renderer/memory task, not unfinished heightmap-resolution
work. Its first goal is a patch-addressable static mesh suitable for 1024/2
editing and later LOD work. The Stage 1 baseline used an aligned 12-byte vertex
with float height and explicit patch-local sample coordinates. Stage 2A now
uses an 8-byte float-height vertex with coordinates derived from
`gl_VertexID`. Both layouts remain documented for later comparisons. The
design must not assume that MSTS adaptive triangulation will be implemented
immediately.

## Stage boundary

This task used a strict stop between the initial implementation and later
optimization. The stages below remain separate comparison milestones even
though Stage 2A has now started.

**Stage 1 -- implement and validate the 12-byte paged backend:**

- preserve the legacy backend as the comparison reference;
- implement the indexed paged mesh using `TerrainVertex12`;
- share its vertices with map rendering;
- implement patch parameter aggregation and modified-patch refresh;
- verify rendering/editing correctness and compare its performance and memory
  use with the legacy renderer.

The 12-byte implementation is retained as the Stage 1 reference. Its stored-
coordinate shader and record definition must not be removed merely because
Stage 2A is active; using that shader again requires restoring the matching
12-byte VAO layout.

**Stage 2 -- optional measured vertex-format optimization:**

Stage 2 started as a separate follow-up. Stage 2A selected the
float-height/derived-coordinate 8-byte format. Raw-height alternatives remain
experiments: their height encoding, editing behavior, and acceptance criteria
must be clarified using measurements rather than folded into Stage 2A.

## Current selected implementation

The active paged terrain design is:

```text
vertex:      8 bytes = float height + packed 10:10:10 normal/gap
coordinates: local X/Z derived from gl_VertexID
topology:    shared 16-bit regular index template
storage:     one VBO and VAO per page of at most 256 patches
parameters:  two vec4 values per patch in page-local terrain/map UBOs
draw:        glDrawElementsBaseVertex(), one draw per visible patch
mesh default: paged backend; precomputed legacy backend remains selectable
pipeline:    legacy/direct renderer remains the default; Gather is unfinished
```

The packed normal's two-bit W component carries the F-buffer gap flag. Terrain
passes apply the existing fragment discard; the map pass deliberately ignores
gaps. This preserves gap support without a separate byte or custom index buffer
in the ordinary case.

The settings UI names the backends `Precomputed / Legacy` and
`On GPU / Experimental`. Their persisted values remain `legacy` and `paged`
for configuration compatibility. New/default configurations select `paged`;
an existing explicit `terrainMesh = legacy` choice remains respected.

Keep VBO pages rather than merging a complete terrain tile into one permanent
mega-buffer. One tile VBO would save only a few ideal VAO binds while making
future variable-size LOD allocation and patch replacement harder. Future LOD
pages may group patches by vertex resolution. Regular LODs should share index
templates; E/AS-guided or other custom triangulation should use pooled custom
index ranges without forcing unrelated vertex regeneration.

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
patch parameter blocks, packed gap state, and shared map rendering.

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
LOD hierarchy. `R > 128` makes physically oversized patches and weakens future
patch-distance LOD. The preferred native TSRE 1024/16 layout uses `R = 64`;
the patched-MSTS-compatible 1024/32 layout uses `R = 32`. The upper bound also
guarantees that a complete patch has at most
`129^2 = 16,641` vertices and therefore uses 16-bit patch-local indices.

Stage 1 used this aligned record:

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
  calculate position and UV from local sample coordinates—an explicit vertex
  attribute in Stage 1 and `gl_VertexID`-derived values in Stage 2A.

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

The page UBO remains the selected solution. For P32 it costs 64 KiB per tile
for both terrain and map records, or only 576 KiB across the nine-tile `large`
test. Replacing it with ordinary UV uniforms could turn four ordered page binds
into as many as 1024 transform updates when patch transforms differ. Before
reconsidering this choice, instrument real routes for visible patches,
consecutive transform changes, unique transforms, and texture changes. Map
parameters are deterministic and may eventually be derived in the shader, but
removing the map UBO alone is a minor memory optimization rather than a current
priority.

Do not derive terrain patch ownership solely with
`floor(vertex.xz / patchWorldSize)`. A vertex exactly on a patch boundary has
the same position in two owning patch blocks, but the two copies may require
different textures and transforms. No positional epsilon can select both
owners correctly.

## Packed terrain normals

Replace three 32-bit normal floats with one normalized 32-bit packed value,
initially `GL_INT_2_10_10_10_REV`. OpenGL expands it to a shader `vec4`; XYZ
hold the normal and the otherwise unused W component now holds the gap flag.
The existing fragment-stage normalization remains applicable. Ten signed bits
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

## Stage 2: measured 8-byte format experiments

Stage 2 begins only after the 12-byte paged backend has established a usable
baseline. Keep the 12-byte description and stored-coordinate shader as the
comparison reference; do not confuse that shader with a runtime switch because
its vertex attributes are incompatible with the active 8-byte VAO.

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

This candidate is now the active experimental paged layout. The two local
coordinate bytes were removed from the VAO, and `terrainPatchSide` is supplied
as cached terrain render state. The gap flag was not disabled: it occupies the
positive value in the otherwise unused two-bit W component of
`GL_INT_2_10_10_10_REV`. Normal XYZ retain all ten-bit components. The vertex
shader passes that W value to the existing terrain-only fragment discard; the
map pass sets `terrainApplyGaps` false and therefore continues to draw regular
all-triangle geometry.

The first user-side comparison on nine fully visible 1024/32 tiles reported
30--31 FPS for both derived coordinates and the 12-byte stored-coordinate
baseline. Treat that as evidence that coordinate reconstruction is not a
measurable bottleneck on that machine, not as complete Stage 2 acceptance.
Still verify gaps, maps, selection, editing, and other supported hardware with
the active 8-byte format.

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

### B2. Raw height with derived coordinates and logical patch location

An alternative use of the two bytes freed by replacing float height with a
short is to retain derived local coordinates and store logical patch X/Z:

```cpp
struct TerrainVertex8RawPatchId {
    uint16_t rawHeight;
    uint16_t patchLocation;  // packed logical patch X/Z
    uint32_t packedNormal;
};
static_assert(sizeof(TerrainVertex8RawPatchId) == 8);
```

For the supported maximum P32 layout, five bits per axis are sufficient and
six bits remain available. This metadata is redundant while logical patch ID
equals a fixed page slot: page base plus `gl_VertexID / verticesPerPatch` can
already recover it. It becomes useful when future LOD pages assign arbitrary
logical patches to physical slots. Keep B2 as the future-LOD-friendly raw
candidate; do not use it as justification for removing the current UBO before
transform-change measurements exist.

The existing `terrain-raw-benchmark` measured patch-local min/max scanning,
quantization, and 8-byte record generation from a 1024-sample tile. Across ten
separate Release runs of 100 measured iterations after warm-up, the loaded-flat
mean was approximately 3.03 ms and the edited-relief mean approximately
2.92 ms. These figures exclude normal generation and GPU upload. They show
that conversion itself is plausible, but do not resolve tile-wide versus
patch-local floor/scale behavior during editing.

### 10-byte structure-of-arrays candidate

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

### Historical Stage 1 baseline gate

The following criteria defined the 12-byte baseline before Stage 2A began.
Keep them as regression and comparison coverage; they are no longer an
instruction to discard or restart the active 8-byte implementation.

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

The 12-byte backend's CPU build time, frame time, draw count, buffer bindings,
CPU/GPU memory, and modified-patch refresh time form the optimization baseline
beside the legacy backend. The original task stopped after reviewing that
milestone; Stage 2A was subsequently authorized and implemented separately.

### Stage 2 experimentation gate

Stage 2 started separately from the baseline implementation. Preserve and
benchmark these layouts through the same indexed draw path when further
comparisons are needed:

```text
C: float height + explicit local X/Z + gap/reserved       12 bytes (baseline)
A: float height + gl_VertexID-derived local X/Z            8 bytes (selected)
B: uint16 height + explicit local X/Z                       8 bytes
B2: uint16 height + derived local X/Z + logical patch ID    8 bytes
D: float height + packed normal + explicit X/Z in SoA      10 bytes
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
timing. Stage 2A was selected because it reduces vertex memory by one third
without a measurable frame-rate loss in the initial comparison. It remains
experimental until the outstanding visual and editing checks pass. Do not
remove C, B, B2, or D from this document: they are the controlled fallbacks
and future comparison designs if hardware or raw-height testing changes the
decision.

Crack-free transitions, distance-driven LOD selection, E/AS-guided adaptive
triangulation, and constant per-frame MSTS-style refinement remain subsequent
work. The page/backend design must leave them possible without making their
implementation a prerequisite for this static paged-mesh stage.

### Patch visibility culling

Before the current implementation, high-resolution terrain submission followed
`Game::tileLod` only. `Game::objectLod` supplied the projection far plane, but
the old fixed-size patch-distance and view-cone tests in both
`Terrain::render()` and `Terrain::pushRenderItem()` were commented out.
Patches outside the viewport therefore still incurred draw-call,
index-processing, and vertex-shader cost before clipping.

The old tests were disabled in GokuMK/TSRE5 commit `d078dea` on 2017-11-13 as
part of "quadtree terrain rendering support, all tile sizes". Their calculations
used fixed 128 m patches and fixed 1024 m half-tile offsets, so they could not be
re-enabled unchanged for variable terrain sizes or patch counts.

The implementation uses conservative patch bounding-sphere frustum culling
using `TerrainGridLayout::patchWorldSize`, the terrain tile's actual world
bounds, and the active projection whose far plane remains `Game::objectLod`.
The same visibility decision is applied to the legacy and paged backends and to direct
and gather submission, including the paged map pass. Loading radius
(`tileLod`), visibility distance (`objectLod`), and future mesh-detail LOD
remain three separate controls. Patch centers are transformed with the complete
terrain model matrix, which covers edge patches of terrain tiles larger than one
2048 m world tile. Sphere testing also avoids culling a patch solely because its
origin is outside the frustum.

An additional horizontal radial test uses `objectLod + 256 m`. It retains a
patch until its complete horizontal bounding circle is outside that radius.
Interactive testing confirmed the per-patch circle with differently sized
terrain tiles in `qttest1`. The positive margin keeps its stepped patch boundary
behind the projection far plane while preserving radial patch selection for
future terrain LOD.

`TFile::PatchField` now names the anonymous patch descriptor offsets. In
TSRE's 13-float `tdata` record, offsets 0 through 5 are `CenterX`, `AverageY`,
`CenterZ`, `FactorY`, `RangeY`, and `RadiusM`; offset 6 is `ShaderIndex`, and
offsets 7 through 12 are texture-transform `X`, `Y`, `W`, `B`, `C`, and `H`.
MSTS uses the bounds as:

```text
min = (CenterX - RadiusM, AverageY - RangeY, CenterZ - RadiusM)
max = (CenterX + RadiusM, AverageY + RangeY, CenterZ + RadiusM)
```

It separately supplies `FactorY` as the conservative radius for its
plane/sphere frustum test. TSRE already reads and writes these values. New flat
tiles initialize `CenterX`, `CenterZ`, and `RadiusM` from the actual physical
patch size; they initialize `AverageY = 1`, `RangeY = 0`, and scale the stock
flat `FactorY` value with patch size.

Height invalidation marks every patch whose inclusive sample range overlaps the
edit, including neighbours sharing a boundary sample. Before refresh, render,
or save, dirty patches recompute `AverageY` and `RangeY` from live samples,
retain the stock flat-radius convention, and grow `FactorY` conservatively with
vertical range. Initial load calculates separate runtime bounds without
replacing untouched descriptor values with the inferred formula. Invalid
runtime bounds and invalid frustum matrices fail open so malformed data cannot
make visible terrain disappear. The terrain-grid test performs an explicit
write/read round trip of all 13 named fields and verifies regenerated vertical
bounds after a height edit.

## Historical Stage 1 implementation record

The Stage 1 milestone retained selectable `legacy` and `paged` backends. At
that milestone, the paged backend used the 12-byte record, shared 16-bit
checkerboard indices, 256-patch VBO/UBO pages, base-vertex draws, shared map
geometry, and dirty-patch uploads. Shader files for both runtime shader
profiles are source-controlled so the baseline remains reproducible. The
statement that no eight-byte work was included applies to this historical
milestone, not to the current implementation.

Automated verification completed:

- Release build;
- 39/39 `terrain-grid` tests;
- 86/86 `settings` tests;
- CMK terrain corpus: 1194 descriptors accepted and loaded, no payload failure;
- `terrainsize`, `ebias1`, and `qttest1`: 25 descriptors accepted and loaded,
  including two variable-patch mutation probes and no payload failure;
- paged runtime startup on CMK and on all four visible `terrainsize` test tiles,
  including two 32 x 32 layouts using four pages each, with no shader failure.

One same-tile CMK startup smoke measurement (256/8, 16 x 16 patches) recorded:

```text
backend  GPU mesh bytes  initial build
legacy      26,738,688       80.775 ms
paged          907,264       10.625 ms
```

These are single startup samples, not statistically useful performance results.
At that milestone, the remaining gate was interactive comparison of rendered
terrain, map overlay, gaps, selection, shadows, edits, save/reload behavior,
and proper frame-time/buffer-binding measurements on representative hardware.

Initial interactive visibility and performance checks after restoring patch
culling found no problematic periodic stalls after a system restart. With nine
fully visible 32 x 32-patch tiles, 512-sample terrain reached the configured FPS
limit; 1024-sample terrain measured approximately 30-40 FPS. These observations
are useful smoke results rather than controlled benchmarks.

## Current Stage 2A implementation record

The active paged vertex is `TerrainVertex8Derived`: float height plus one
`GL_INT_2_10_10_10_REV` normal/gap value. Local sample coordinates are derived
from `gl_VertexID`; float height deliberately avoids raw floor/scale and
editing-rebase policy in this stage. The 12-byte `StandardFogStoredCoords`
shader is retained for comparison source, but cannot be selected against the
8-byte VAO without restoring the Stage 1 record and attribute bindings.

On the nine fully visible 1024/P32 tiles in `large`, both the 12-byte stored-
coordinate variant and the initial 8-byte derived-coordinate variant measured
approximately 30--31 FPS. This indicates no measurable vertex-fetch or integer
coordinate-reconstruction difference on the tested GPU. The 8-byte layout was
kept for its one-third vertex-memory reduction: including current index and
terrain/map parameter buffers, the nine-tile case is approximately 77.2 MiB
instead of 115.5 MiB, saving approximately 38.3 MiB.

Paged patch traversal was then changed from column-first submission to
sequential row-major patch IDs in both the legacy/direct and Gather pipelines.
The map pass was already sequential, and water does not use terrain VBO pages.
For a fully visible P32 tile this reduces main-pass VAO and UBO page changes
from 128 to four. The same `large` view improved by approximately 2 FPS, from
about 31 to about 33 FPS. This confirms measurable CPU/driver cost from page
state changes, although triangle processing remains the dominant 1024-terrain
cost. Gather remains unfinished and the legacy/direct pipeline remains the
default and authoritative performance path.

Current choices and deferred alternatives are therefore:

- keep the active 8-byte float-height/derived-coordinate layout;
- keep 256-patch VBO/VAO pages and order draws by page instead of merging a
  whole tile into one permanent VBO;
- keep page UBOs for arbitrary MSTS patch UV transforms; measure real transform
  change frequency before considering ordinary per-patch uniforms;
- retain the 12-byte interleaved baseline, 10-byte structure-of-arrays design,
  8-byte raw/explicit-coordinate design, and 8-byte raw/logical-patch-ID design
  for future controlled comparisons;
- defer raw-height selection until editing, rebasing, normal generation, GPU
  upload, cracks, and save/reload behavior have been measured;
- use separate regular index templates for future LOD levels and pooled custom
  index ranges for E/AS or other adaptive triangulation, without regenerating
  vertex pages merely because triangle definitions change.
