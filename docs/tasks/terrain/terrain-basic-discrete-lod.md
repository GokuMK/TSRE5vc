# Basic discrete terrain patch LOD

Status: tile-local milestone and cross-tile continuation completed;
user testing confirms the cross-tile implementation works

## Cross-tile continuation (current implementation scope)

The native outer-ring restriction below describes the completed first
milestone. It was temporary because the adjacent-edge cache did not yet exist.
The continuation uses that cache and removes the restriction for
participating detailed terrain. This section supersedes the historical
first-pass exclusions of cross-tile coordination and outer-ring removal.

Agreed requirements:

- Full stitching targets physically aligned power-of-two grids. Coarse sample
  positions coincide with a subset of the fine grid. Additional intermediate
  fine points are handled by transition topology; arbitrary unrelated sample
  lattices are not required. Other loadable layouts retain best-effort rendering.
- Compare final effective sample spacing in metres, whether determined by a
  tile's native grid or by distance-selected LOD. Native 8 m terrain beside a
  patch rendered at 4 m requires the same transition as native 4 m terrain
  reduced to 8 m beside that patch.
- Select the finer patch's existing 2:1 edge template when the neighbouring
  spacing is at least twice its own. Equal or finer neighbours require no
  transition on this side. Exact agreement is guaranteed for aligned 2:1
  transitions; larger ratios use the same template as best-effort improvement.
  Do not add 4:1/8:1 templates. Keep useful existing refinement for avoidable
  LOD differences, but do not try to refine below a tile's native resolution.
- Prepare participating tiles' states before drawing. Compare neighbours after
  native limits, gap constraints, and any refinement have been applied. Do not
  let traversal/draw order or a previous frame determine neighbour spacing.
- Discover loaded neighbours through the ordered cached edge sections. Derive
  source patch indices arithmetically and inspect overlapping patches by a
  short linear walk. LOD discovery must not load more terrain or regenerate
  native point vectors because the camera moved.
- A patch edge spanning different neighbour effective spacings remains outside
  guaranteed stitching. Use a simple deterministic conservative fallback and
  bounded diagnostics; do not introduce partial-edge topology or a new mapping
  structure for this uncommon configuration.
- Emit valid transition masks even at a tile's coarsest available LOD if its
  current patch grid still has an even number of cells along the edge. Another
  tile can be coarser even when this tile has no next selectable level. Preserve
  the regular template at the same source step if a requested mask is invalid.
- Use the same prepared topology for terrain, grid, map, selection, and
  shadow/depth rendering. Keep distant terrain and the precomputed backend on
  their established rendering paths.

The index-based design stays selected. Stored heights and vertex heights are
not averaged or overwritten to implement stitching; unused intermediate edge
vertices remain available for editing and later topology changes. A separate
adjusted float edge vector is a retained alternative for a future mesh path
which keeps those vertices referenced, not a requirement of this implementation.

Native edge caching and rendered topology have separate lifetimes. Source
height/layout/availability changes can rebuild a cache; moving the camera
changes LOD source steps and stitch masks without changing the cached points.
The deliberately incomplete diagonal-corner fallback remains as documented in
the edge-cache task.

Required continuation tests include:

- native 4 m / 8 m neighbours with the fine-only midpoint lowered: the stitched
  boundary does not reference the lowered midpoint and follows the coarse line;
- both height-ownership orientations, all four sides, edit and save/reload;
- equal effective spacing produced by different native profiles;
- neighbour spacing selected by distance, and consistency across draw order;
- 4:1 or larger native mismatch selecting the existing 2:1 improvement;
- different tile sizes and patch counts, multiple sections along a large edge;
- a patch edge spanning mixed neighbour levels, missing neighbours, gaps, and
  the documented corner fallback;
- transition availability at the coarsest selectable tile level;
- unchanged native height data and no LOD-only vertex uploads;
- existing terrain-grid/edge suites and both renderer submission paths.

The sections below retain the original topology design and first-milestone
history. Their first-pass-only boundaries must not be interpreted as cancelling
this continuation.

### Cross-tile implementation notes

- `TerrainLibQt` gathers the camera's detailed-terrain render neighbourhood
  before patch selection. Colour, selection and terrain shadow entry points
  use the same neighbourhood and camera coordinates. The normal render set is
  loaded up front; edge discovery itself uses loaded neighbours only and does
  not recursively expand this set.
- `TerrainLibLod.cpp` prepares transient states, walks cached native sections,
  and splits their spans arithmetically at the two sides' patch boundaries.
  It does not retain a point-to-patch map or camera-dependent edge vectors.
- `TerrainLod::connectTileStates()` applies cross-border gap-neighbour pinning
  and monotonic refinement before assigning masks. The finer patch selects
  its 2:1 template for any neighbour at least twice as coarse. For a patch edge
  meeting different neighbour levels, the coarse transition wins for the whole
  edge; this remains best effort. Unsupported larger native ratios and mixed
  spans produce a warning once per terrain, not once per frame.
- Final states live only during submission. `Terrain::render()` and
  `pushRenderItem()` consume them for their existing terrain, map, grid,
  selection and depth paths. There are no LOD-only height changes, vertex
  uploads or new shaders. Direct rendering without the coordinated Qt library
  retains the earlier conservative tile-local fallback.
- Transition generation includes the coarsest available level whenever its
  edge has an even number of cells (at least two). Invalid-mask lookup first
  falls back to the regular template at the requested step. Index-memory
  accounting includes these additional masks.

CPU tests cover 4 m/8 m boundaries in all directions, preserving a lowered
fine-only RAW sample while omitting it from the transition, shared endpoint
heights in both ownership directions, save/reload, 4:1 native fallback,
equal effective spacing from unequal native grids, camera/traversal order,
4 km/2 km sections and different patch sizes, missing neighbours, gap pinning,
mixed spans, and coarsest-level transitions. Full topology tests cover all
16 masks. These are CPU/serialization tests. The user subsequently confirmed
that the implementation works in interactive testing. This confirmation does
not establish exhaustive coverage of both direct and gather renderers or all
documented best-effort boundary cases.

Verification of the continuation: Release build succeeded; `terrain-edges`
passed 52/52 and `terrain-grid` passed 61/61. Warm preparation of the synthetic
five-tile mixed-resolution neighbourhood averaged approximately 71-84
microseconds over 100 successive camera positions, including neighbour
lookup and refinement, without terrain loads. This is a CPU fixture timing,
not an editor/GPU frame-rate measurement. The broader `all` run also passed
the procedural-policy, dyntrack-road, ORTS-profile, selection, settings and
TDB-load suites. It retained the `flex-point` failure `complete TDB subsection
frames` (reproduced with the previous executable); route-load tests lacked
their required MSTS root/test-route configuration, and flex baseline capture
was unavailable. These unrelated checks were not changed by this task.

Implementation summary:

- route-owned `TsreTerrainLod` parsing, validation, saving, and Route settings
  editing are implemented;
- the paged backend builds every valid regular and 16-mask 2:1 transition
  template up to 32 m and stores them in its existing per-terrain EBO;
- patch-center selection, cross-tile outer rings, gap/neighbour pinning, 2:1
  constraint relaxation, and stitch-mask selection are implemented;
- direct and queued terrain, grid, map, selection, and depth/shadow submissions
  use the selected topology;
- the legacy backend and distant `Lo_tiles` remain native and unchanged;
- automated topology/profile/constraint tests are included in the
  `terrain-grid` suite.

Related tasks:

- [Paged terrain mesh and shared map rendering](terrain-paged-mesh-and-shared-map.md)
- [Terrain adjacent-edge cache](terrain-adjacent-edge-cache.md)
- [Terrain heightmap resolution support](terrain-heightmap-resolution.md)
- [Variable terrain patch count](terrain-patch-count.md)
- [MSTS adaptive terrain LOD executable analysis](../../msts/msts-terrain-adaptive-lod-executable-analysis.md)

This task adds the first deliberately simple terrain level-of-detail system to
the paged terrain renderer. Its immediate purpose is to make 1024-sample
terrain practical at normal viewing distances without attempting to reproduce
MSTS adaptive triangulation.

The design must support at least three active resolution bands from the start.
It must not encode assumptions which restrict the renderer to exactly two
levels, even if the first topology test compares only two adjacent levels.

This task is independent of the MSTS executable study. Do not require knowledge
of AS, E, ErrorBias, or MSTS's dynamic triangle hierarchy to implement it.

## First implementation boundaries

Keep the first implementation deliberately tile-local and deterministic:

- apply it only to normal detailed terrain, not `Lo_tiles` distant terrain;
- calculate LOD independently inside each loaded `Terrain` object;
- keep the complete outer patch ring at native resolution;
- keep gap-constrained patches at native resolution;
- refine coarser requested patches locally until every drawn internal edge is
  either 1:1 or 2:1;
- use planar distance to patch center without hysteresis;
- keep one concatenated LOD element buffer in each `TerrainMeshPaged` backend;
- generate every valid `(R, K)` template up to 32 m for that tile.

These limitations avoid cross-tile preparation, shared OpenGL-resource
lifetime, previous-frame LOD state, and distant-terrain interactions in the
first pass. They do not constrain the topology API: source step `K`, effective
sample spacing, and edge mask remain generic so later work can remove the
limits without replacing the mesh representation.

## Physical-resolution model

Define terrain LOD by effective sample spacing in metres, not by a tile-local
`LOD 0`, `LOD 1`, or `LOD 2` number. Different terrain profiles can then meet
at the same rendered resolution.

The complete supported resolution sequence is:

```text
1 m, 2 m, 4 m, 8 m, 16 m, 32 m
```

Do not generate terrain LOD coarser than 32 m. Eight-metre terrain already
renders very well on inexpensive hardware; beyond 32 m the extra loss of
terrain shape and normal quality is not justified by the small additional
performance gain. Routes needing extremely long-distance landforms should use
the distant-terrain feature.

Let:

```text
S = native terrain sample spacing in metres
R = terrain_nsamples / terrain_patchset_npatches
K = source-vertex step used by the selected topology
effective sample spacing = S * K
rendered cells per patch = R / K
```

`K` is a power of two. A level is available only when `R` is divisible by `K`
and `S * K` is one of the supported physical resolutions no greater than
32 m. Its topology references every `K`th source vertex in each direction.

For example, an `R = 64` patch from 2 m terrain can provide:

```text
2 m:  K = 1, 64 x 64 cells
4 m:  K = 2, 32 x 32 cells
8 m:  K = 4, 16 x 16 cells
16 m: K = 8,  8 x 8 cells
32 m: K = 16, 4 x 4 cells
```

Each step doubles sample spacing and quarters triangle count. For a level with
`C = R / K` cells per side:

```text
triangles = 2 * C * C
indices   = 6 * C * C
```

All levels reuse the patch's existing complete `(R + 1) x (R + 1)` source
vertex block. LOD selection changes only the index range used by the draw. It
does not clone, compact, upload, average, or modify vertex data.

This first implementation reduces submitted triangles and vertex-shader work,
but does not reduce resident vertex-buffer size. Vertex-memory LOD, eviction,
and regeneration may be considered after topology-only LOD has established
correct switching and useful frame-time gains.

Terrain whose native spacing or `R` cannot provide a configured level remains
loadable and renders using the closest safe level it actually has. LOD
eligibility must not become another terrain-file loading restriction.

## Route-owned distance profile and native-tile fallback

Configure distance bands in physical metres and assign each band a requested
sample spacing. A representative three-band profile is:

```text
distance 0..1000 m:     request 4 m samples
distance 1000..2000 m:  request 8 m samples
distance 2000..4000 m:  prefer 16 m samples
distance beyond 4000 m: continue 16 m until normal visibility culling
```

This is an example to test and tune, not a mandatory final default.

The complete profile is route-design data and must be persisted in the route's
`.trk` file through `Trk`. It must travel with the route because safe bands
depend on the terrain profiles, patch sizes, and layout chosen by that route's
designer. Edit it through the Route settings window, not the application-wide
settings window.

Use the following TSRE-prefixed block inside `Tr_RouteFile`, which represents
an ordered list rather than three hard-coded member names. Store a preferred
end distance for every level, including the last one:

```text
TsreTerrainLod (
    Level ( 4 1000 )
    Level ( 8 2000 )
    Level ( 16 4000 )
)
```

Each entry is `(requested metres per sample, preferred end distance in metres)`.
Both values are positive integers. The first level applies from zero to its
preferred end; each later level starts at the preceding preferred end. Sample
spacings must strictly become one step coarser at a time in the supported
sequence, and preferred end distances must strictly increase. Thus
`(4,1000), (8,2000), (16,4000)` is valid; skipped or repeated resolutions,
unordered ends, or finer terrain at a greater distance are not.

For ordinary Stage 1 rendering, the last configured level continues beyond its
preferred end until normal terrain visibility culling removes the patch. Its
numeric preferred end is not a terrain draw cutoff. Retain it because later
AS/ErrorBias-aware refinement may distinguish a level's preferred range from
the fallback used beyond that range. For example, a future rule could keep an
important patch one resolution step finer beyond the normal preferred end.
The exact AS/ErrorBias rule and that refinement are not part of this task.
Loading the block must not mark the route modified; editing it must.

A legacy route without this block uses TSRE's runtime default profile:

```text
Level ( 1 500 )
Level ( 2 1000 )
Level ( 4 2000 )
Level ( 8 8000 )
```

Normal MSTS 8 m terrain still renders at its full native resolution because it
cannot provide the requested 1 m, 2 m, or 4 m levels. Higher-resolution terrain
gets useful LOD immediately, allowing the feature to be tested without
modifying every existing route. The fallback is not written to the `.trk` file
unless a route author explicitly selects a route-owned profile.

An empty or malformed block is cleared and therefore uses the same runtime
default. A malformed block reports a clear route warning. Do not silently
normalize malformed values and later overwrite the route with different data.
The Route settings dialog may help the user construct a replacement explicitly.

Keep the Route settings integration small: show a terrain-LOD summary and a
button in `TrkWindow`, and open a dedicated profile dialog rather than growing
the existing settings grid with up to six rows. The dialog can use a two-column
table of resolution and preferred end distance, with resolution selected from
`1/2/4/8/16/32 m` and simple add/remove controls. Cancel must leave `Trk`
unchanged; accepting a changed valid profile marks it modified. This UI is
route-owned and must not reuse the application Settings registry.

Build the tile's sorted set `A` of available effective spacings. For requested
spacing `Q`, select the largest `A` value no greater than `Q`. If no available
value is that fine, select the smallest value in `A`, which is the tile's best
available detail. Examples:

```text
A = { 4, 8, 16 }, Q = 8   -> 8 m
A = { 4, 8, 16 }, Q = 32  -> 16 m
A = { 8, 16, 32 }, Q = 4  -> 8 m
```

If the tile has no level in the supported physical sequence, keep its native
full topology and disable LOD transitions for that tile. This is a renderer
fallback, not a terrain-load failure.

Thus a native 4 m tile decimated to 8 m and a native 8 m tile at full detail
both have the same effective 8 m resolution. Their numeric level within their
source tile differs, so renderer and neighbour logic must compare physical
sample spacing rather than local level indices.

Terrain visibility distance, tile loading radius, and mesh-detail LOD remain
separate concepts:

- `objectLod` and the frustum decide whether a patch is visible;
- `tileLod` decides which terrain tiles are resident;
- the terrain LOD profile decides effective sample spacing.

Do not silently reuse `objectLod` as the mesh-detail threshold. Store the
terrain bands in `Trk` and expose them through Route settings when the initial
implementation is ready for interactive tuning. A future application setting
may multiply all route-defined distances for a user's hardware or preference,
but that multiplier is deliberately outside this task and must not replace the
route's authoritative profile.

## Required adjacency invariant

Adjacent rendered patches may differ by at most one step in the supported
resolution sequence:

```text
allowed:     4 m beside 4 m or 8 m
not allowed: 4 m beside 16 m, 32 m, or a non-power-of-two spacing
```

The regular stitch handles only a 2:1 edge. It cannot guarantee a correct
transition across a 4:1 or greater difference.

Distance bands must therefore be configured widely enough, relative to
physical patch size, that one resolution ring cannot be skipped between
adjacent patch centers. Requiring every intermediate band to be at least the
largest applicable patch width is a simple sufficient check, although runtime
validation remains authoritative.

Do not generate an unverified multi-ratio stitch when configuration still
produces a 4:1 edge. Refine the coarser patch by one or more available levels
until the edge is at most 2:1, and propagate this constraint through its other
neighbours until the tile is stable. The six-level limit bounds this relaxation
to a few passes. Log the original violation once with its tile, patches,
requested resolutions, and route bands so the route designer can correct it.

A future TSRE tool may validate a route's terrain profiles and proposed LOD
distances, visualize invalid adjacencies, and help the route designer choose
safe band widths. That authoring aid is not required for the first renderer
implementation.

## Transition ownership

When adjacent patches use consecutive resolutions, stitch the finer patch to
the coarser patch. The coarser patch keeps its ordinary topology. The finer
patch uses a transition index template on the shared edge.

Do not average or replace the Y value of every other fine edge vertex. Height
averaging can close the crack, but it mutates render vertices according to
neighbour state, requires transition-specific vertex refreshes, affects
normals, and creates several vertex variants for the same logical patch.

Instead, the transition topology must:

- reference only every second fine vertex on the shared boundary, so its edge
  exactly matches the next-coarser patch's segments;
- retain vertices at the current fine step in the first interior row;
- retriangulate the boundary strip between those two rows;
- preserve the existing winding and checkerboard convention away from the
  transition;
- use no new vertices and keep one draw call for the complete patch.

The skipped intermediate boundary vertices remain in the shared VBO but are
not referenced by that transition's indices.

For each pair of cells at the current fine level along a stitched edge, the
regular boundary strip contains four triangles. The transition replaces those
four triangles with three; it does not merely remove a triangle. Ignoring
winding, let `b0`, `b1`, and `b2` be consecutive current-level vertices on the
shared boundary and let `i0`, `i1`, and `i2` be the corresponding vertices on
the first current-level interior row:

```text
shared boundary:  b0 ----- b1 ----- b2
interior row:     i0 ----- i1 ----- i2

b1 is not referenced.
transition triangles: (b0, b2, i1), (b0, i1, i0), (b2, i2, i1)
```

The middle transition triangle spans the area which would otherwise become a
hole. The two other triangles fill the sides. Actual index order must follow
TSRE's winding convention. At source step `K`, consecutive current-level
vertices in this example are `K` source samples apart; `b0` and `b2` are
therefore `2 * K` samples apart and match the coarser level.

## Edge masks and regular template count

Four independently stitchable edges require an explicit four-bit mask, with
names tied to patch-local source coordinates rather than screen or world
directions. Terrain rows and world Z use opposite signs in parts of the current
renderer, so `minimum Z` would still be ambiguous:

```text
bit 0: localSampleX == 0
bit 1: localSampleX == R
bit 2: localSampleZ == 0
bit 3: localSampleZ == R
```

For every available level which can touch the next coarser level, generate 16
templates:

```text
mask 0: regular topology at source step K
masks 1..15: every transition-edge combination from K to 2 * K
```

The coarsest available level needs only its regular template. A three-level
patch therefore has `16 + 16 + 1 = 33` possible regular/transition templates,
not a design hard-coded around one fine and one coarse grid.

One- and two-edge masks should be the common result along a sufficiently large
and stable circular boundary, but that is not a safe invariant. A fine patch
just inside a center-distance threshold can have its outward and both lateral
neighbours outside the threshold, producing three stitched edges. A small
detail radius can leave one fine patch surrounded on all four sides.
Hysteresis, forced-full gap patches, conservative tile-boundary rules, and
future per-patch constraints can also make the field non-circular. Supporting
every four-bit value keeps neighbour resolution simple and costs little for
regular shared topology.

Generate each mask as one complete patch index list. The topology generator
must handle the two incident bits at a corner together: blindly concatenating
independent edge strips can overlap or leave a hole in their common corner
cells. Verify that every covered region is triangulated exactly once.

For regular cells away from transitions, calculate checkerboard diagonal
parity from level-local cell coordinates `(cellX + cellZ) & 1`. Do not use the
source sample coordinates after multiplying by `K`: for every even `K`, that
would make the parity permanently even and change all diagonals to the same
orientation.

"Generate each mask" means constructing its complete index array once when a
template for `(R, K)` is created, then reusing it for every matching patch and
frame. It does not mean regenerating an index array when the camera crosses an
LOD threshold. Runtime switching only changes the selected offset and count in
the already-created element buffer.

## Retained split-mesh alternative for custom topology

For regular grid LOD, one complete template per mask is the selected first
implementation: it preserves one draw per patch and shared templates make its
memory cost small.

Do not discard the alternative of dividing a patch into independently drawn
core/edge or four quadrant meshes. It becomes relevant if AS data, E-driven
refinement, gaps, or another future feature gives each patch custom topology.
Generating as many as 16 complete variants of every unique custom patch mesh
could then be much more expensive than selecting several reusable boundary
pieces at draw time.

The split design trades index memory and generation work for more draw calls
and harder corner ownership. Keep it documented as a future custom-topology
option and compare it when such topology is implemented; do not introduce its
extra draws into the first regular-grid LOD merely in anticipation of AS/E
support.

## Index-buffer organization

Use 16-bit patch-local indices, as in the current paged backend. Store regular
and transition templates in a shared buffer for each supported `(R, K)`.

A practical concatenated element buffer for a three-level example is:

```text
[K=1 masks 0..15]
[K=2 masks 0..15]
[K=4 regular mask 0]
```

Each template record supplies an index byte offset and index count. The patch
continues to supply its current VBO-page `baseVertex`. `RenderItem` already
supports indexed drawing through `indexOffset`, `vertCount`, and `baseVertex`,
so selecting topology should not require another VAO or vertex buffer.

For the first implementation, concatenate all templates required by one tile
into the existing `TerrainMeshPaged` element buffer. This matches the current
ownership model: every terrain backend owns an EBO and its page VAOs capture
that EBO. It needs no context-global OpenGL resource manager and no additional
EBO bind while drawing the tile.

Generate all levels the tile can represent up to 32 m, whether or not the
current route profile selects every level. The set contains at most six
physical resolutions, avoids route-profile-dependent buffer rebuilds, and lets
Route settings changes take effect without reloading terrain GPU resources. A
later renderer/context-level cache keyed by `(R, K, edgeMask)` can remove
duplicate GPU index data if measurements justify its resource-lifetime
complexity. Physical sample spacing would not belong in that cache key because
it affects selection, while index topology depends only on source-grid
dimensions and step. Never clone vertex pages to implement a transition.

For `R = 64`, the regular `K = 1` template occupies 48 KiB and the regular
`K = 2` template occupies 12 KiB with 16-bit indices. A native 2 m tile with
native boundary fallback and route bands requesting 4, 8, and 16 m needs at
most approximately 1 MiB of per-tile index templates. This is acceptable for
the first implementation beside its vertex pages; measure it before adding a
global GPU cache.

## Fit with the current paged renderer

This LOD does not require a new vertex format, shader coordinate calculation,
patch UBO layout, or VBO page layout.

Replace or extend `TerrainMeshPaged::buildRegularIndices(R)` with a pure
topology generator conceptually shaped as:

```text
buildIndices(R, K, edgeMask) -> uint16 source-local indices
```

The generated indices must address the original full `(R + 1)^2` patch block,
not a densely renumbered coarse grid. The current vertex shader then continues
to recover the correct source-local X/Z from `gl_VertexID`, including for
`K > 1`.

Concatenate the generated arrays into `TerrainMeshPaged::indexBuffer` and keep
metadata containing each template's byte offset and index count. All page VAOs
continue to capture that one EBO. `configureRenderItem()` needs the selected
`K`/mask or resolved template key and changes only:

```text
item.indexOffset
item.vertCount
```

`item.baseVertex`, `terrainVerticesPerPatch`, `terrainPatchSide`, sample
spacing, patch parameter UBOs, textures, and materials remain unchanged. Both
the current direct draw and queued `OpenGL3Renderer` already consume
`indexOffset`, `vertCount`, and `baseVertex`. LOD switching therefore creates
no vertex upload, parameter upload, VAO rebuild, or shader uniform update.

The current `TerrainGridLayout::pagedIndexBytes` and paged-mesh startup log
describe one full-resolution template. Update memory accounting to use the
actual concatenated EBO byte count so diagnostics and tests do not under-report
LOD index storage.

## LOD state and selection

Build temporary effective-spacing, source-step, and edge-mask arrays for every
patch in the tile before submitting render items. They are frame/render state,
not persistent terrain data, because every result initially depends only on
the current camera, route profile, and tile constraints. Complete-tile arrays
are at most 1024 entries each and avoid special handling when a visible patch
needs the state of a neighbour just outside the frustum.

Put classification, constraint relaxation, and edge-mask construction in one
CPU-side `Terrain` helper used by both direct and queued rendering. Call it once
per terrain render entry, before the terrain/grid/map patch loops; do not
recalculate LOD independently inside each pass. Keep the calculation independent
of OpenGL so its complete output can be unit tested.

Use horizontal camera distance to the existing runtime patch center. Do not
leave center-versus-bounds selection open in the first implementation. The
center must use actual terrain placement and patch size rather than fixed
128 m patch or 2048 m terrain assumptions.

Compare squared planar distance with squared band boundaries; no square root is
needed for classification. Reuse the camera-local coordinates already
calculated for patch visibility.

Do not add hysteresis initially. Stateless distance classification is easier to
verify, gives every render pass the same result, and cannot preserve an invalid
old neighbour level. LOD popping at a boundary is acceptable for the first
interactive test. Add hysteresis later only if visible oscillation justifies
the previous-frame state and neighbour-coordination complexity.

Use planar X/Z distance for the first implementation. Camera height should not
make terrain directly below a high-altitude camera remain unnecessarily fine
unless a later visual test justifies three-dimensional distance.

Suggested frame flow:

1. Build or reuse patch visibility.
2. Select the requested physical resolution from the patch's distance band.
3. Map that request to an available `(effective spacing, K)` for its tile.
4. Pin the outer ring and gap-constrained patches to native resolution.
5. Repeatedly refine the coarser side of every internal edge wider than 2:1
   until the tile is stable.
6. On the finer patch, set an edge bit wherever its neighbour is one
   step coarser.
7. Submit one render item using the selected template offset/count and existing
   page-local base vertex.

Changing resolution or edge mask is transient render state. It must not mark
terrain modified for saving and must not enter terrain Undo.

## Tile boundaries and mixed layouts

Same-tile neighbours are straightforward. Do not resolve cross-tile LOD
neighbours in the first implementation. `TerrainLibQt` currently discovers and
submits terrain objects independently; preparing a globally consistent field
would expand this task into terrain-library scheduling and lifetime work.

Instead, pin the complete outer patch ring of every normal terrain tile to its
native/full topology, then apply the same 2:1 relaxation inward. It costs at
least `4 * P - 4` native patches per tile (60 of 256 for P16, or 124 of 1024
for P32), plus any finer intermediate rings needed to reach the requested
interior level without skipping resolution. This reduces the maximum saving
but guarantees that LOD itself does not alter cross-tile edge topology.

Small patch grids receive less benefit from this conservative rule: a P4 tile
has 12 boundary patches and only four interior patches. Keep P4/P8 rendering
correct, but do not make strong performance gains on them an acceptance gate.
The first performance target is high-resolution P16/P32 terrain; removing the
boundary cost belongs to later cross-tile coordination.

Later compatible cross-tile selection must compare physical edge intervals
and effective spacing in metres because adjacent terrain tiles may have
different native spacing, patch counts, physical sizes, or patch divisions.
That follow-up can remove the pinned rings without changing the topology
generator or render-item format.

Aligned power-of-two grids share their coarse sample positions; the finer grid
also contains intermediate points. Heightmap edge filling supplies boundary
heights at those sample positions. Transition topology makes the two rendered
sides use matching boundary segments by omitting intermediate fine vertices.
Edge filling alone does not constrain a lowered fine-only midpoint on a stored
RAW boundary to the neighbour's coarser line.

## Gaps, hidden patches, maps, and render passes

The current paged backend carries MSTS F-buffer gap state in each packed vertex
and removes affected terrain fragments. A coarse topology skips source
vertices, so a gap flag stored only on a skipped sample could otherwise be
lost.

For the first milestone, pin any patch containing a gap and its direct drawn
neighbours to native/full topology, then run the ordinary 2:1 relaxation away
from those patches. Cache a per-patch `hasGap` value and update it through the
existing gap invalidation path; do not scan the complete F buffer every frame.
Gaps are rare. Hole-aware coarse or split custom topology is future work and
must preserve TSRE's rule that every triangle containing a flagged vertex is
omitted.

`Lo_tiles` are already the route's low-resolution distant-terrain mechanism.
Render them with their native topology in this task; do not apply the detailed
terrain LOD bands or introduce stitching between normal and distant terrain.

Normal terrain, selection, and shadow/depth passes continue to skip hidden
patches. The paged map overlay deliberately renders the complete patch surface
and ignores hidden/gap state, as it does now. Calculate LOD and transition masks
for the complete logical patch grid so both uses can share one topology field;
a harmless transition beside a patch omitted from the normal pass is preferable
to a separate map-only neighbour solution. Water rendering is independent of
the terrain patch index templates and must not be changed by this task.

All passes which render the terrain surface should use the same chosen
topology, including ordinary terrain, terrain-grid overlay, shadows/depth,
selection, and map overlay passes. Otherwise a coarse distant patch can still
pay the full triangle cost in another pass, and selection or shadows may not
match the visible surface. Showing the selected topology in terrain-grid mode
also provides a useful transition diagnostic. The map pass continues to ignore
gap fragments but can reuse the same selected indices and existing map
parameter block.

Continue using the full-resolution packed normals at vertices selected by a
coarser topology. They describe the source heightfield rather than a newly
averaged coarse surface, but require no extra normal buffers and are adequate
for the first visual test. Coarse-level normals are a later measured refinement
if lighting shimmer or inconsistent distant shading is visible.

The legacy/precomputed backend remains unchanged and available as visual and
performance reference. LOD initially applies only to the
`On GPU / Experimental` paged backend.

## Additional limitations considered

The following restrictions would shorten isolated pieces of code but are not
worth imposing on route support:

- **Exactly three stored bands:** not selected. The renderer must demonstrate
  three simultaneous bands, but an ordered container with at most the six
  supported physical resolutions is little more complex and avoids another
  `.trk` format change.
- **Only one- and two-edge transition masks:** not selected. Three- and
  four-edge cases arise from discrete circles and pinned constraints; all 16
  shared regular masks are inexpensive.
- **Power-of-two `R` only:** not selected. The generator needs only that the
  requested `K` and any `2 * K` transition divide `R`. Rejecting an otherwise
  supported terrain layout would not simplify the core indexing materially.
- **One native terrain profile for the complete route:** not selected. Pinned
  native tile boundaries isolate the first LOD implementation from mixed
  profiles without restricting loading or editing.
- **Disable LOD for a complete tile when one gap exists:** not selected. A
  cached `hasGap` bit per patch plus ordinary neighbour relaxation is small and
  preserves most of the benefit on the rare affected tile.
- **Apply LOD only in the colour pass:** not selected. Full-detail map,
  selection, or shadow/depth passes would retain avoidable triangle cost and
  could disagree with the visible surface.
- **Terrain skirts instead of stitched topology:** not selected. Skirts can
  hide a crack from common viewpoints but add geometry and do not make the two
  surfaces meet; they are unsuitable as the basis for later terrain LOD.

The selected first-pass restrictions--normal terrain only, native outer rings,
no cross-tile coordination, no hysteresis, per-backend EBO ownership, and 32 m
maximum spacing--remove substantial subsystem work while preserving all normal
terrain profiles and the intended topology design.

## Implementation stages

### Stage 1: generic topology generator

- Generate regular topology for every valid power-of-two source step `K` up to
  an effective 32 m resolution, independent of the currently selected route
  bands.
- Generate all 15 non-zero edge-mask transitions for every `K` which has an
  available `2 * K` neighbour level.
- Make the API and template key accept arbitrary valid `K`; do not provide only
  `fullIndices` and `halfIndices` fields.
- Concatenate the required templates into the existing per-backend EBO; leave a
  context-global GPU cache for later measurement.
- Validate index range, winding, triangle count, duplicates, missing regions,
  and every corner-mask combination without an OpenGL context.
- Verify that a transition boundary references only every second vertex of the
  current fine topology.

### Stage 2: physical three-band renderer selection

- Add an ordered route-owned LOD-band container to `Trk`, plus `.trk` parsing,
  writing, defaults, validation, and Route settings editing.
- Support at least three configured physical-resolution distance bands without
  hard-coding the storage or renderer to exactly three entries.
- Map each patch's requested metres-per-sample to its available source step.
- Build temporary complete-tile effective-resolution, source-step, and
  edge-mask arrays for each render calculation.
- Calculate the complete relevant LOD field before render submission.
- Pin the outer ring and gap constraints, then refine coarse neighbours until
  every internal drawn edge differs by no more than one resolution step.
- Log profile-caused 4:1 requests once; do not attempt a multi-ratio stitch.
- Use deterministic planar patch-center distance without hysteresis.
- Preserve ordered page traversal so the previous P32 VAO/UBO bind reduction
  is not regressed.

### Stage 3: boundaries and special cases

- Keep the native/full-resolution tile boundary policy throughout this task;
  cross-tile LOD coordination is a later task.
- Keep gap patches and required neighbours at native/full resolution.
- Apply selected topology consistently to terrain, shadow/depth, selection,
  and map passes.
- Leave distant `Lo_tiles` on their existing native rendering path.

### Stage 4: profiling and tuning

- Compare LOD disabled, forced physical resolutions, and distance-selected LOD
  on identical cameras.
- Measure terrain-pass CPU time, GPU time, triangles, draw calls, VAO/EBO/UBO
  binds, and template memory.
- Tune a representative three-band profile using the 3 x 3 1024-sample
  `large` route and representative 256/512 routes.
- Verify that stationary camera frames perform no LOD-related uploads or mesh
  regeneration.

## Required verification

Automated topology tests must cover at least `R = 4`, `8`, `16`, `32`, `64`,
and `128`, for every power-of-two `K` which divides `R`:

- regular grids at all effective resolutions up to 32 m;
- every transition mask from 0 through 15 at every adjacent level pair;
- all four single-edge directions;
- adjacent-edge corners, opposite-edge pairs, three-edge masks, and the
  all-edge mask;
- maximum index no greater than `(R + 1)^2 - 1`;
- consistent triangle winding and no zero-area or duplicate triangles;
- exact 2:1 boundary agreement between fine transition and next-coarser
  regular topology;
- constraint relaxation from a pinned native edge or gap through three or more
  requested levels;
- safe full/native fallback when no standard level can be generated.

Interactive and renderer tests must cover:

- 256, 512, and 1024-sample terrain;
- P16 and P32 layouts, especially 1024/P16 (`R = 64`) and 1024/P32
  (`R = 32`);
- at least three simultaneous physical resolution rings;
- a native-coarser tile inside a finer requested band;
- no direct 4:1 adjacency, plus diagnostic and conservative fallback when a
  deliberately bad profile creates one;
- visible stepped circular boundaries while moving slowly through each
  threshold; visible switching is recorded but is not a first-pass failure;
- no cracks on flat, rough, steep, edited, and differently textured patches;
- transitions on every edge and corner combination;
- native/full outer rings on differently sized tiles, unloaded neighbours, and
  mixed terrain profiles;
- gaps and hidden patches;
- unchanged distant `Lo_tiles` rendering;
- main terrain, map overlay, selection, and shadow/depth output;
- legacy backend visual comparison with LOD forced to native/full resolution;
- unchanged editing, save/reload, and terrain descriptor behavior.

Route-data tests must cover:

- loading a legacy `.trk` without terrain LOD data and applying the documented
  native/full fallback without marking it modified;
- reading and writing at least three ordered bands without numeric or ordering
  changes;
- preserving unrelated standard and TSRE `.trk` fields during save;
- rejecting non-positive or unordered preferred end distances,
  duplicate/unsupported sample spacing, and adjacent bands which skip a
  resolution;
- Route settings edits setting `Trk::modified` and surviving save/reload.

Performance acceptance should demonstrate a substantial reduction in
processed terrain triangles outside the finest-detail radius without
increasing draw calls above one per visible regular patch or reintroducing
per-patch VAO/UBO page thrashing. Record measurements rather than treating FPS
capped by VSync as a useful comparison.

## Explicit non-goals

Do not include any of the following in this first LOD task:

- AS-buffer interpretation or always-high-detail points;
- E-buffer or ErrorBias-driven refinement;
- MSTS executable research or reproduction of its adaptive hierarchy;
- per-frame dynamic triangle subdivision;
- effective terrain resolutions coarser than 32 m;
- vertex-page eviction, compressed heights, or reduced resident VBOs;
- new terrain file fields or rewriting AS/E/N/F payloads;
- arbitrary non-2:1 transition ratios;
- an application-wide terrain LOD profile or user distance multiplier;
- cross-tile LOD coordination or removal of the pinned native outer ring;
- LOD or stitching changes to distant `Lo_tiles`;
- hysteresis or retained previous-frame patch LOD state;
- a renderer/context-global OpenGL index-template cache;
- changing the legacy/precomputed renderer.

Future work may add E/AS-aware custom or split index ranges, pooled topology,
reduced resident vertex pages, or MSTS-like adaptive triangulation. Those
extensions should build on measured physical-resolution selection rather than
being prerequisites for it.
