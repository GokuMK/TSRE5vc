# Terrain adjacent-edge cache

Status: implemented; interactive seam/normal inspection remains

## Implementation notes

The runtime implementation is in `TerrainAdjacentEdge.h/.cpp` and
`TerrainLibEdges.cpp`. `Terrain` owns four caches. `TerrainLibQt::fillRaw()`
uses cached positive edges for both mesh backends, while general
`tryGetHeight()` and the non-QuadTree library retain their existing paths.

- Discovery queries World cells along the requested edge, deduplicates the
  adjacent terrains, then builds native point sections. Direct neighbours are
  loaded before reading shared endpoints, avoiding a first-fill ordering gap.
- Source locators store a World-cell address, terrain name, domain, and side.
  They never retain a `Terrain*`. `sourcePatchAt()` derives the neighbouring
  patch arithmetically; native spacing retrieval is constant-time after section
  validation. No LOD selection or outer-ring policy was changed.
- Sampling walks points monotonically, interpolates valid spans, and uses
  current owner heights for missing or conflicting values. A missing endpoint
  is represented by a non-finite cached height, never written to terrain;
  sampling involving it returns failure and applies current owner fallback.
- Boundary edits invalidate owner zero-side and dependent positive-side
  caches. Load, reload, full network RAW replacement, and QuadTree replacement
  notify affected meshes. Last-stored-row/column edits schedule positive-edge
  filling so fallback follows edits as well.
- Unload broadcasts were not added. A refresh rechecks source residency;
  unavailable sources use fallback. Missing sections are reconsidered on an
  actual fill, without a per-frame retry loop.
- The cheap corner improvement uses a canonical endpoint owner only if it is
  already loaded. It never loads a diagonal tile. Missing or ambiguous corner
  data uses the documented owner-height fallback. Exact matching is therefore
  still not guaranteed in the deliberately incomplete case below.

Automated coverage lives in `TerrainEdgeTestSuite.cpp`, exposed as
`--test --test-suite terrain-edges` and included in `--test-suite all`. It uses
synthetic native grids plus actual temporary `.t`/RAW files and the production
QuadTree load/reload path, without an OpenGL context. Coverage includes domain
isolation, mixed sizes/resolutions, 2048/1 m, first-load section junctions,
conflicts, missing payloads, unnotified unload, replacement, reset/undo,
save/reload, and native border parity with `tryGetHeight()` on valid spans.

Warm 1024-sample fills in the synthetic registry fixture performed two terrain
lookups per fill (one per positive-edge section), with measured averages around
16-21 microseconds in Release runs. This is a CPU fixture measurement,
not a claim about complete editor frame time or GPU refresh performance.

Verification: Release build succeeded; `terrain-edges` passed 40/40 cases and
the existing `terrain-grid` suite passed 56/56. Interactive seam/normal checks
on actual routes are still required. Save/reload tests compare cached heights
exactly with decoded RAW values, accounting for the existing uint16 disk
quantization when comparing those values with pre-save float heights.

Related tasks:

- [Basic discrete terrain patch LOD](terrain-basic-discrete-lod.md)
- [Terrain heightmap resolution support](terrain-heightmap-resolution.md)
- [Variable terrain patch count](terrain-patch-count.md)
- [Paged terrain mesh and shared map rendering](terrain-paged-mesh-and-shared-map.md)

## Objective

Give every loaded `Terrain` four adjacent-edge objects. Each edge is an
ordered collection of short native-resolution height sections belonging to
the terrains touching that side.

Use the same representation for two purposes:

- replace repeated point-by-point neighbour searches while filling the
  synthesized `N+1` terrain border;
- expose the adjacent native grid needed by later cross-tile LOD stitching.

The cached edge is static terrain geometry. Camera movement and LOD changes
must never regenerate, decimate, or otherwise rewrite it.

This is a separate foundation task. It does not implement terrain LOD,
adaptive triangulation, AS/E interpretation, or a global render scheduler.

## Main design decisions

1. `Terrain` always owns four edge objects named by local sample coordinates:
   `LocalX0`, `LocalXMax`, `LocalZ0`, and `LocalZMax`.
2. An edge contains ordered sections rather than assuming one neighbour. One
   large terrain edge can touch several smaller terrain tiles or patches.
3. A section contains native `{along, height}` points. Native spacing is
   visible directly in consecutive point coordinates and is not duplicated as
   authoritative per-point metadata.
4. Find overlapping sections with a linear ordered scan. The expected count
   is tiny--normally one or a few and only around eight in unusually divided
   supported layouts--so a separate edge-to-patch lookup table is unjustified.
   Eight is a performance example, not a section-count or capacity limit.
5. Keep only a minimal stable source-terrain locator on a section. Derive the
   adjacent patch index arithmetically from the section position and that
   terrain's layout; do not store a patch mapping.
6. Do not retain raw `Terrain*` pointers. Terrains can be unloaded or replaced.
7. Invalidate and rebuild a complete affected edge instead of maintaining
   fine-grained point or source revisions. Edge data is small and cheap enough
   that the simpler lifecycle is preferable.

Full edge/stitching functionality targets aligned power-of-two layouts. Exact
full-feature requirements remain to be defined. Other currently loadable
layouts remain loadable and visible with best-effort edge handling, allowing
route designers to inspect and replace unsuitable content. Do not tighten
terrain loading validation as part of this task.

Prefer cheap, deterministic fallback over complicated handling of rare visible
imperfections. Do not add clipped-overlap interval metadata, irregular-grid
topology, or per-frame geometric repair solely for unusual layouts.

This task records the agreed design, not an immutable implementation recipe.
Implementation may improve it when inspection reveals a simpler or very cheap
solution consistent with these priorities. Record any such decision and its
actual coverage, especially if it resolves a limitation left open below.

## Existing behavior to preserve

The terrain RAW payload stores `N x N` heights, while `Terrain::terrainData`
allocates `(N+1) x (N+1)`. On load, the extra positive-X and positive-Z rows
initially repeat the last stored row and column. Before paged mesh construction,
`TerrainLibQt::fillRaw()` calls:

- `Terrain::fillTerrainDataX()`;
- `Terrain::fillTerrainDataY()`;
- `Terrain::fillTerrainDataXY()`.

Those methods currently call `TerrainLibQt::tryGetHeight(...,
loadIfNeeded=true)` separately for destination samples. World-space lookup and
interpolation make mixed resolutions work, but terrain selection and coordinate
normalization are repeated along the edge.

`TerrainLib::terrainSamplesChanged()` already handles reverse invalidation.
Changes to canonical `local X == 0` or `local Z == 0` source boundaries locate
loaded terrains whose synthesized positive edge depends on them and call
`invalidateSynthesizedSamples()`. Preserve this half-open ownership convention
and extend that path instead of creating another notification system.

The cache is runtime-only. Its samples are not saved to `_y.raw`, do not alter
the terrain file format, and do not mark a terrain or route modified.

## Coordinates and edge orientation

Do not use north/east/top/bottom names. TSRE terrain rows and world-Z signs make
them ambiguous:

```cpp
enum class TerrainEdgeSide : quint8 {
    LocalX0,
    LocalXMax,
    LocalZ0,
    LocalZMax
};
```

Every edge uses one owner-local physical coordinate named `along`:

```text
LocalX0 / LocalXMax: along follows local Z from 0 to terrainWorldSize
LocalZ0 / LocalZMax: along follows local X from 0 to terrainWorldSize
```

Store points in strictly increasing `along` order even when the source
terrain's corresponding local axis runs in the opposite direction. Reverse a
source vector once while constructing the section; callers must not know its
orientation.

Supported terrain spacings and sizes are integral metres, so `alongM` can be a
signed integer and equality remains exact. If terrain layout rules later allow
fractional-metre samples, change this coordinate type deliberately rather than
adding floating-point tolerances throughout the first implementation.

QuadTree discovery may probe by a small epsilon outside an edge so an exact
boundary cannot resolve back to the owner. The epsilon is only for selecting
the adjacent terrain. Stored point positions and sampled heights remain on the
exact shared mathematical edge.

## Proposed representation

The exact Qt/STL containers may change, but keep the representation small:

```cpp
struct TerrainEdgePoint {
    int alongM = 0;
    float height = 0.0f;
};

struct TerrainSourceLocator {
    quint32 terrainId = 0;       // stable ID/name key, never Terrain*
    bool distantDomain = false; // detailed and distant IDs are separate
    TerrainEdgeSide sourceSide;
};

struct TerrainEdgeSection {
    TerrainSourceLocator source;
    QVector<TerrainEdgePoint> points; // inclusive endpoints, increasing along

    int firstAlongM() const;
    int lastAlongM() const;
};

struct TerrainAdjacentEdge {
    enum class Status { Unresolved, Missing, Partial, Complete, Conflict };

    TerrainEdgeSide side;
    int ownerLengthM = 0;
    QVector<TerrainEdgeSection> sections;
    Status status = Status::Unresolved;
    bool dirty = true;

    bool sampleHeight(int alongM, float &height) const;
};
```

`Terrain` owns:

```cpp
std::array<TerrainAdjacentEdge, 4> adjacentEdges;
```

One section represents the continuous overlap with one adjacent terrain edge,
not one section per adjacent patch. Native spacing is uniform across a terrain
edge, while patch boundaries can be calculated from the terrain layout.
Sections remain ordered by `lastAlongM()`.

The source locator is only data already known while the section is built. It
allows later code to find the source terrain's shared transient LOD array. The
patch coordinate comes from the physical along-edge position, source side,
terrain bounds, patch count, and patch physical size. Do not build a second
array, hash, interval tree, point-to-patch map, or cached `Terrain*` around it.

Adjacent sections normally share their endpoint. Preserve one copy in each
section so either section is independently valid. Sampling uses half-open
section ownership except for the final edge endpoint. If the two copies have
different heights, mark the edge as conflicting and use the owner-height
fallback for the ambiguous sample. Keep usable portions of the edge. Do not
implement iterative seam repair or reject the terrain because of that conflict.

At eight bytes per point, four 1025-point edges occupy about 32 KiB before
small container overhead. Even a 4096 m edge represented at 1 m resolution is
minor beside terrain CPU and GPU data. Optimize lookup simplicity rather than
trying to save a few edge points.

## Building adjacent sections

Centralize terrain physical bounds in a helper derived from `TerrainInfo`,
`TerrainGridLayout::terrainWorldSize`, and the fixed 2048 m World-tile lattice.
Do not repeat the old `mojex`/`mojez`/`level` arithmetic in four implementations.

For one owner edge:

1. Discover directly adjacent detailed or distant terrains in the same domain
   as `Terrain::lowTile`.
2. Intersect their physical bounds with the owner edge.
3. Generate point positions at the adjacent terrain's native spacing across
   that overlap.
4. Obtain their heights from the canonical zero-side RAW boundary owner,
   interpolating when the canonical grid is coarser. Never depend recursively
   on another terrain's synthesized `N+1` edge to build the cache.
5. Retain the source side needed to convert an along-edge position to a source
   patch coordinate later.
6. Convert point positions into owner-local increasing `alongM` coordinates.
7. Store the minimal stable source-terrain locator on the section.
8. Sort sections by their first and last point.
9. Detect missing intervals, overlaps, duplicate intervals, inconsistent
   endpoints, self-resolution, and unsupported source layouts.

One owner edge may therefore look like:

```text
owner edge 0................................................4096 m
section 0  [ adjacent terrain A native edge points          ]
section 1                           [ terrain B edge points ]
```

Do not assume that terrain size, patch count, patch physical size, or native
sample spacing matches the owner. Aligned power-of-two layouts provide the
physical alignment this representation relies on for full functionality;
the current loading validator accepts a wider set of layouts. If an unusual
overlap cannot be represented by a regular native point section, leave that
part unavailable and use the owner-height fallback. Keep the tile loadable.

The cached point spacing describes the adjacent terrain's native grid even
when another terrain owns the persistent boundary heights. For example, on an
owner's `LocalX0` side, the owner supplies canonical RAW heights while the
negative-side neighbour supplies the point positions relevant to that
neighbour's LOD. This preserves RAW ownership without losing adjacent native
resolution information.

The resolver must not depend on mutable
`TerrainLibQt::currentQuadTree/currentQt` state. Pass the detailed/distant
domain explicitly or restore it with a scoped guard.

Support two discovery modes:

```text
LoadedOnly          do not load terrain to construct the edge
LoadDirectNeighbor  may load terrain sharing a nonzero span of the requested edge
```

`N+1` synthesis may retain the current direct-neighbour loading behavior. LOD
inspection uses `LoadedOnly`, so examining an edge cannot expand residency
beyond `tileLod`. Loading a direct neighbour must not recursively resolve all
of that neighbour's edges.

Use endpoint heights already available from ordinary edge construction and
reuse a shared endpoint result for sections meeting there. An endpoint can
otherwise require a third terrain. Do not add endpoint-only terrain loading
or recursively construct another tile's edges merely to obtain it. Use the
owner-height fallback when the required value is unavailable. The diagonal
XMax/ZMax case is explicitly left incomplete as described below.

A `LoadedOnly` miss is not a permanent negative cache. A subsequent
`LoadDirectNeighbor` request can attempt the permitted load. Missing sections
are reconsidered on the next relevant terrain refresh; do not retry failed
loads every frame or once per sample.

## Linear edge scanning and sampling

No special mapping is needed to find the section for a local patch interval.
Sections and owner patches are both ordered along the edge. For a patch range
`[patchFirst, patchLast]`:

```text
while section.lastPoint < patchFirst:
    advance section

process sections until section.firstPoint > patchLast
```

There are normally zero to a few comparisons. A cursor may continue from the
previous patch while walking the edge, making the complete traversal linear in
the number of patches plus sections, but restarting the tiny scan would also
be acceptable until profiling says otherwise.

Height sampling uses the same ordered walk through points. For a requested
destination coordinate `x`, find consecutive points `(x0,h0)` and `(x1,h1)`:

```text
if x == x0:
    height = h0
else:
    t = (x - x0) / (x1 - x0)
    height = h0 + t * (h1 - h0)
```

An average is correct only when `x` is exactly halfway between the two source
points. General linear interpolation is required for every supported ratio.
Never interpolate across a missing interval or from the last point of one
section to the first point of a non-contiguous section.

Native spacing is obtained from adjacent point coordinates inside a section:

```text
nativeSpacing = point[j + 1].alongM - point[j].alongM
```

At an interior point, `(point[j + 1] - point[j - 1]) / 2` gives the same
result. Endpoint code uses the one-sided difference. A section needs at least
two points for spacing inference. Zero, negative, or non-uniform spacing makes
that section unavailable to this regular-grid algorithm, not the terrain
unloadable. Use the cheap fallback instead of adding irregular-grid machinery.

## Replacing `N+1` filling

Replace the repetitive positive-edge queries behind operations conceptually
similar to:

```text
TerrainLib::resolveAdjacentEdge(terrain, side, LoadDirectNeighbor)
Terrain::applySynthesizedPositiveEdges()
```

Only `LocalXMax`, `LocalZMax`, and their common corner write the synthesized
`N+1` storage. `LocalX0` and `LocalZ0` remain stored RAW ownership boundaries
and must not be overwritten from neighbours.

Applying a resolved edge must:

- walk destination samples and cached source points monotonically;
- linearly interpolate mixed native resolutions;
- write resolved or fallback heights only when the destination value changes;
- return the changed sample range;
- invalidate only overlapping patch vertices, bounds, and normal halos;
- use repeated-own-edge heights for missing or ambiguous samples;
- avoid setting terrain or route modified state.

### Deliberately incomplete diagonal corner

The sample `terrainData[N][N]` is synthesized on both axes. Unlike the other
three tile corners, its canonical height may require the diagonal terrain
which ordinary edge construction does not load. Exact canonical matching for
this case is deliberately not required in the initial implementation.

Use a valid endpoint height already available from ordinary edge construction.
If none is available unambiguously, use `terrainData[N-1][N-1]`. Assign the
result once so the XMax and ZMax applications cannot overwrite the shared
corner with different values. Never leave the mesh vertex uninitialized.

This can leave a visible height mismatch in the cells adjoining that corner.
It is an accepted limitation, not a claim that the diagonal case is solved.
Do not build dedicated diagonal discovery or dependency machinery for it.
Existing inexpensive notifications can remain in use. If implementation
reveals a very cheap way to obtain and maintain the correct value through
existing lookups or edge data, it may implement that improvement and update
this note and tests to describe the resulting behavior.

The fallback reads current stored heights, never stale synthesized values:

```text
XMax at row z: terrainData[min(z, N-1)][N-1]
ZMax at col x: terrainData[N-1][min(x, N-1)]
XMax/ZMax corner: terrainData[N-1][N-1]
```

Current RAW initialization already repeats the last stored row/column, but
current point-query fill leaves old synthesized values in place on lookup
failure. The cache implementation deliberately defines fallback again during
a refresh that discovers an unavailable neighbour. Recompute it when the
owner's last stored row/column changes, even if no adjacent cache was dirtied.
X0/Z0 caches may use the owner's canonical boundary heights as fallback for
height consumers, but must not invent resident adjacent terrain or a native
spacing for LOD. Stored X0/Z0 heights remain untouched.

Keep `tryGetHeight()` as the general world-space point query. This cache only
replaces its repetitive use along known terrain boundaries.

## Simple invalidation lifecycle

Do not maintain revisions for individual points or sections. Mark a complete
edge dirty and rebuild it on next use when any fact capable of changing its
contents changes:

- a source height edit touches the shared boundary;
- an owner or adjacent terrain loads, reloads, or is replaced;
- QuadTree population or terrain layout changes;
- a terrain becomes unsupported or becomes available again;
- the detailed/distant terrain domain changes.

Extend `TerrainLib::terrainSamplesChanged()` with explicit local and dependent
invalidation:

- a canonical X0 edit dirties the edited terrain's own X0 cache and the
  dependent neighbours' XMax caches;
- a canonical Z0 edit dirties the edited terrain's own Z0 cache and the
  dependent neighbours' ZMax caches;
- a changed canonical corner dirties supplied endpoints through ordinary
  owner/edge invalidation; no additional diagonal dependency mechanism is
  required for the deliberately incomplete XMax/ZMax case;
- dirty the affected synthesized mesh samples through
  `invalidateSynthesizedSamples()` so an already-built mesh requests edge fill;
- applying synthesized samples must not emit another canonical-height
  notification or recursively invalidate neighbours.

The existing boundary coverage scan is the starting point. Extend it for
owner caches and ordinary edge dependencies; no persistent reverse-dependency map
is required. The section's neighbouring grid source is not necessarily its
height owner. Ordinary interior edits do not rebuild adjacent vectors, though
an edit to the last stored row/column must update a missing-edge fallback.

Successful load, reload/replacement, and complete RAW replacement should mark
affected loaded owners' caches and mesh borders dirty through a small local
notification. Cover a previously missing neighbour becoming available. Use
geometric adjacency and existing domain-aware lookup, not a route-wide scan.

Unload notifications are optional, recommended only if easy and cheap. There
is no requirement to force neighbouring meshes to refresh when a tile is
unloaded. Copied points may remain in use until the owner's next relevant
refresh. During that refresh, cheaply recheck section sources by stable
locator; if unavailable under the requested discovery mode, use the owner's
current height data. Do not preserve stale neighbour heights once absence has
been detected. Do not poll all caches or rebuild them every render frame.

Copied point data and stable source locators prevent dangling pointers.
`LoadedOnly` LOD code checks current residency independently and cannot treat
a cached snapshot as proof that a neighbouring mesh is being rendered.

Whole-edge rebuild is intentionally selected. With only four small vectors,
per-section generation counters and partial refresh bookkeeping add more state
and failure modes than useful performance.

## Using native edges for later LOD stitching

The cached native edge already contains every height and physical vertex
position that a coarser discrete LOD can select. Never generate a second
camera-dependent edge vector.

The shared frame LOD calculation determines an effective sample spacing for
every participating patch. For each local boundary patch:

1. Find its overlapping adjacent section or sections with the ordered linear
   scan.
2. Derive the overlapping adjacent patch coordinate from the section's source
   terrain layout and the physical along-edge range. Use it to read that
   patch's final effective spacing from the shared LOD calculation. If the
   local patch interval crosses source patch boundaries, inspect those few
   adjacent patch entries sequentially.
3. Derive which cached native points that adjacent patch renders:

   ```text
   adjacent source step = adjacent effective spacing / adjacent native spacing
   ```

4. Compare the local and adjacent effective spacing in physical metres.
5. If they match, use ordinary topology.
6. If this patch is the 2:1 finer side, select its appropriate stitch mask.
7. If the adjacent patch is finer, leave this side ordinary; the adjacent side
   owns the stitch.
8. If the ratio exceeds 2:1, refine the coarser side or use the documented safe
   native fallback.

For example, a native 2 m adjacent edge rendered at 8 m uses every fourth
cached point. Moving the camera can change the source step, but not the cached
points or sections.

The finer rendered border conforms to the coarser border line. For example,
a 2 m border meeting an 8 m border must align to the 8 m line during stitching;
the coarser mesh is not required to reproduce every native 2 m height detail.
That topology/geometry work, including ratios beyond the initial 2:1 templates,
belongs to the LOD task. It must not be added to this static cache task.

If one local patch edge overlaps adjacent sections which currently render at
different effective resolutions, it touches two LOD levels. The initial LOD
design declares that layout/configuration unsupported for guaranteed
stitching. Detect it and refine or pin the affected patch safely; do not invent
partial per-edge topology inside this foundation task.

The basic discrete LOD task continues to pin outer patch rings to native
resolution. Removing that limitation still requires `TerrainLib` to calculate
or reconcile LOD for all participating terrains before drawing any of them.
The edge cache supplies static geometry and a trivial adjacency walk; it does
not itself solve render-order coordination.

## Implementation stages

### Stage 1: edge types and discovery

- Add local-coordinate edge enums and centralized physical-bounds helpers.
- Implement one native point section per continuous adjacent terrain overlap.
- Normalize source orientation into owner-local increasing coordinates.
- Implement the stable source-terrain locator and arithmetic patch-coordinate
  calculation without raw pointers or an auxiliary lookup table.
- Test equal, mixed-size, mixed-resolution, missing, partial, and overlapping
  neighbours without OpenGL.

### Stage 2: sampling and `N+1` integration

- Implement monotonic section and point iteration.
- Implement exact lookup and linear interpolation.
- Replace point-by-point positive-edge filling with cached sampling.
- Apply the explicit current-owner last-row/column fallback when lookup fails.
- Compare generated `(N+1)` values with the current `tryGetHeight()` results
  on uniform and mixed-resolution routes, reporting the deliberate diagonal
  corner fallback separately instead of requiring equality there.

### Stage 3: whole-edge invalidation

- Connect canonical boundary changes to owner and dependent-edge invalidation,
  including endpoints supplied by ordinary edge construction. No new dedicated
  diagonal dependency tracking is required.
- Rebuild dirty edges lazily before height fill, mesh refresh, or LOD use.
- Reuse `terrainSamplesChanged()` and `invalidateSynthesizedSamples()`.
- Connect load, reload, RAW replacement, and QuadTree changes to local cache
  and mesh invalidation. Add unload notifications only if easy and cheap.
- Verify fallback on the next refresh after an unnotified unload.

### Stage 4: expose sections to cross-tile LOD

- Expose ordered read-only section iteration and native point spacing.
- Let a later shared LOD coordinator derive adjacent patch coordinates and
  read their final effective spacing using the section's source terrain.
- Do not add camera-dependent points, decimated copies, or transition masks to
  the cache.

## Required verification

Automated tests must cover:

- all four local edge orientations and source reversals;
- the three ordinary corners and the explicitly limited diagonal corner:
  reuse an available endpoint or initialize the documented fallback;
- 256/8, 512/4, and 1024/2 neighbours in both fine-to-coarse directions;
- P4, P8, P16, and P32 patch boundaries;
- a large terrain edge touching several smaller terrains;
- equal, finer, coarser, missing, partial, overlapping, and conflicting
  sections;
- exact endpoints and half-open ownership between consecutive sections;
- interpolation at midpoint and non-midpoint ratios;
- section native-spacing inference and invalid-spacing rejection;
- linear scanning across zero, one, and several sections;
- populated QuadTree entries without a terrain payload;
- `LoadedOnly` causing no terrain loads;
- `LoadDirectNeighbor` not loading a diagonal terrain solely for an endpoint
  or recursively constructing neighbouring edges;
- a `LoadedOnly` miss followed by a permitted direct load;
- a canonical X0/Z0 edge edit rebuilding both owner and dependent caches;
- canonical corner edits invalidating endpoints supplied by ordinary edges;
- interior edits causing no edge-cache rebuild;
- terrain unload, reload, and replacement without stale pointer access;
- unnotified unload followed by refresh using current owner-height fallback;
- edits to the last stored row/column updating a missing-edge fallback;
- previously missing neighbours becoming available and refreshing existing meshes;
- unusual currently loadable layouts remaining loadable with cheap fallback;
- synthesized samples remaining excluded from `_y.raw` save data;
- no terrain/route modified flag caused by cache resolution or refresh;
- LOD source steps such as native 2 m rendered at 4, 8, and 16 m without
  changing the cached vector.

Interactive tests should compare seam geometry and normals before and after
the refactor on uniform and mixed-resolution routes, including a large tile
border split across multiple smaller terrain tiles.

## Explicit non-goals

- terrain LOD selection or transition-index generation;
- camera-dependent edge state or regenerated rendered-edge vectors;
- an edge-to-patch hash, interval tree, or point lookup table;
- per-point or per-section revision tracking;
- retaining neighbour `Terrain*` pointers;
- removing the first LOD implementation's native outer ring;
- AS, E, ErrorBias, or adaptive triangulation;
- changing RAW ownership or saving synthesized `N+1` samples;
- replacing general `tryGetHeight()` point queries;
- supporting terrain layouts not already accepted by `TerrainGridLayout`;
- tightening loading rules to require powers of two, or guaranteeing perfect
  stitching for every currently loadable unusual layout;
- mandatory unload broadcasts, per-frame cache polling, or complex repair of
  rare geometric imperfections;
- guaranteed canonical XMax/ZMax corner matching or dedicated diagonal
  discovery/dependency machinery; a very cheap implementation improvement is
  allowed as described in the deliberate-limitation note;
- forcing detailed and distant terrain to share edge data or LOD policy.
