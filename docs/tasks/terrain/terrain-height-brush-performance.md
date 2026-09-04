# Terrain height-brush CPU performance

Status: proposed, ready for implementation and profiling

Related tasks:

- [Terrain heightmap resolution support](terrain-heightmap-resolution.md)
- [Paged terrain mesh and shared map rendering](terrain-paged-mesh-and-shared-map.md)
- [Terrain adjacent-edge cache](terrain-adjacent-edge-cache.md)
- [Variable terrain patch count](terrain-patch-count.md)

## Objective

Make interactive `Heightmap +/-` painting practical on 1024/2 m and 2048/1 m
terrain by replacing per-sample World/QuadTree terrain discovery with one
tile-oriented preparation step followed by direct local-heightmap iteration.

Preserve the current brush result, undo behavior, ErrorBias clearing, modified
state, mixed-resolution policy, edge ownership, and paged-renderer dirty-patch
refresh. This task optimizes the CPU editing path; it is not another terrain
mesh or GPU-buffer redesign.

## Evidence and present diagnosis

Testing was deliberately performed on an empty route so World-object count,
forest regeneration, transfers, and unrelated route content do not obscure the
terrain cost.

In the ordinary additive brush path, commenting out:

```cpp
if (terr->terrainData[tpz][tpx] != oldHeight)
    includeDirtySample(dirtySampleBounds, terr, tpx, tpz);
```

leaves no dirty bounds for the edited tile. Consequently the final
`invalidateSamples()` and `refreshModified()` calls are skipped. The visible
mesh remains unchanged, proving that no edited patch data reached the GPU, yet
interactive performance remains approximately 8 FPS on the high-resolution
test. The dominant cost in that case is therefore before mesh regeneration.

This conclusion applies directly to the normal `hType == 0` additive test.
`hType == 1` also records its center sample through a separate dirty-sample
call and must have both paths disabled if the experiment is repeated for that
mode.

`TerrainLib::updateTerrainHeightmap()` is not a local mesh operation. Its base
implementation is empty. `TerrainLibQtClient` overrides it to serialize and
send a complete float heightmap to the multiplayer server. Network editing has
a separate full-heightmap transfer problem, but that does not explain local
empty-route performance and is outside the first implementation.

## Current avoidable work

`TerrainLibQt::paintHeightMap()` currently performs the following work for
each mouse-move event.

### Complete brush pre-pass

The first nested loop exists only to discover terrain tiles for undo. For each
sample position inside the brush it:

- calculates a square root for the circle test;
- normalizes World-tile coordinates with `Game::check_coords()`;
- calls `getTerrainByXY()`, which performs a QuadTree lookup;
- sometimes calls `getTerrainByXY()` a second time;
- repeats the scan even after all affected tiles already have snapshots in the
  current undo state.

### Complete mutation pass

The main nested loop again visits the brush square. For each candidate it:

- normalizes World coordinates;
- performs another QuadTree terrain lookup;
- calculates distance;
- inserts the same `Terrain*` into a `QSet` repeatedly;
- calls `Terrain::setErrorBias()`, including patch-coordinate conversion;
- calls `Terrain::getLocalCoords()` separately;
- converts the result back into integer sample coordinates;
- reads and potentially writes one height;
- updates a `QHash<Terrain*, QRect>` for changed samples.

The main pass currently resolves the terrain before rejecting square-corner
samples outside the circular brush.

### Resolution scaling

For a fixed physical brush radius, halving sample spacing approximately
quadruples the visited sample count:

```text
8 m terrain:  1x
4 m terrain:  4x
2 m terrain: 16x
1 m terrain: 64x
```

The float height write is inexpensive. Repeating coordinate normalization,
QuadTree lookup, patch lookup, Qt-container operations, and duplicate brush
passes for every sample is the primary target.

### Costs which are not the sustained empty-route cause

- `Undo::PushTerrainHeightMap()` copies `(N+1)^2` floats the first time a tile
  enters the current undo state. This can cause a first-contact hitch--about
  4 MiB for N=1024 and 16 MiB for N=2048--but later drag events find the
  existing snapshot and do not copy it again.
- `Route::paintHeightMap()` asks affected World tiles to invalidate forest and
  transfer VBOs. This should be reviewed separately for populated routes, but
  the empty-route reproduction excludes it as the cause of the observed 8 FPS.
- Paged mesh normal generation and upload still have a real cost, but disabling
  dirty-sample collection removed those operations without improving this
  reproduction.

## Selected design: prepare tiles, then edit local samples

Split one brush event into two levels:

```text
world-space brush
    -> discover each intersected Terrain once
    -> prepare one TerrainBrushSlice per accepted tile
    -> iterate that slice directly in local integer sample coordinates
    -> commit dirty bounds and modified patches once per tile
```

Do not call `getTerrainByXY()`, `Game::check_coords()`,
`Terrain::getLocalCoords()`, `Terrain::setErrorBias()`, or `QSet::insert()` from
the inner sample loop.

### Prepared per-terrain slice

The exact type and ownership may be adjusted during implementation, but one
event-local record should contain equivalent information:

```cpp
struct TerrainBrushSlice {
    Terrain *terrain = nullptr;

    // Brush center expressed once in this terrain's local metre coordinates.
    double centerLocalX = 0.0;
    double centerLocalZ = 0.0;
    double radiusMetres = 0.0;
    double radiusSquared = 0.0;

    int sampleSpacing = 0;
    int sampleMinX = 0;
    int sampleMaxX = -1;
    int sampleMinZ = 0;
    int sampleMaxZ = -1;

    bool undoCaptured = false;
    bool changed = false;
    int dirtyMinX = 0;
    int dirtyMaxX = -1;
    int dirtyMinZ = 0;
    int dirtyMaxZ = -1;

    // A small P x P event-local mask or list, not one operation per sample.
    QVector<int> touchedPatches;
};
```

This is not persistent terrain state. Build it on the stack or in an
event-local vector and discard it after the brush event.

### Terrain discovery

1. Convert the brush center and radius to a World-space axis-aligned bounding
   box.
2. Enumerate the fixed 2048 m World-coordinate cells intersected by that box.
3. Query `getTerrainByXY()` once per intersected World cell.
4. Deduplicate returned `Terrain*` values. A terrain tile can cover several
   World cells, so pointer deduplication belongs here, not in the sample loop.
5. Reject null, unloaded, non-editable, or policy-incompatible terrain once.
6. Intersect the brush bounds with each terrain's actual physical extent. Drop
   slices with no intersection.
7. Convert the brush center into each retained terrain's local coordinate
   system once and calculate clamped integer sample bounds.

The supported layouts use the 2048 m World lattice for addressing even when a
terrain tile covers multiple World files. Do not infer terrain dimensions from
the World-cell count. Use each terrain's validated `TerrainGridLayout` for its
physical extent, sample spacing, sample count, and patch layout.

If terrain smaller than one World cell is admitted in the future, discovery
must enumerate QuadTree leaves intersecting the brush instead. Do not silently
claim such layouts are covered by a World-cell-only implementation.

### Mixed-resolution seams

The present height brush accepts the starting terrain's spacing and skips
neighbouring terrain with a different spacing, issuing the existing warning.
Preserve that policy in this performance task unless a separate behavior change
is explicitly approved.

Tile-oriented preparation naturally permits one slice per native spacing, but
enabling that would change which vertices a single brush event edits and how
the result aligns across the seam. Treat it as a later functional enhancement,
not as an incidental optimization.

### Local sample bounds

For each accepted terrain, derive the smallest inclusive integer sample
rectangle which can intersect the brush. Conceptually:

```text
minX = ceil((centerLocalX - radius) / S)
maxX = floor((centerLocalX + radius) / S)
minZ = ceil((centerLocalZ - radius) / S)
maxZ = floor((centerLocalZ + radius) / S)
```

where `S` is that terrain's sample spacing. Clamp through shared layout helpers
which encode the RAW grid's half-open ownership and the synthesized N+1 edge.
Do not independently invent whether sample N is editable. Match current edge
and save/reload behavior, including edits exactly on a terrain boundary.

Use sufficiently precise intermediate coordinates that negative World tiles
and exact boundaries do not gain off-by-one errors. Supported sample spacing is
integral today, but the algorithm should not rely on truncating a negative
floating-point value toward zero.

### Direct inner loop

For every local `(sampleX, sampleZ)` in the prepared rectangle:

1. Obtain local metre coordinates arithmetically from the sample index and
   spacing.
2. Calculate `dx`, `dz`, and `distanceSquared` relative to the prepared local
   brush center.
3. Reject the point when `distanceSquared > radiusSquared`.
4. Calculate `sqrt(distanceSquared)` only when the selected brush formula
   actually needs linear distance/falloff.
5. Read and update `terrainData[sampleZ][sampleX]` directly.
6. If the value changed, expand four integer dirty extrema stored in the slice.
7. Mark the containing patch in a compact event-local mask/list without calling
   the coordinate-based `setErrorBias()` overload.

Patch index follows directly from sample indices and `R`, subject to the shared
boundary-ownership helper. Clear ErrorBias once for every touched patch after
the sample traversal. Do not clear it once per vertex.

The definition of **touched patch** must preserve the editor contract: terrain
height generation/editing leaves affected patches at `ErrorBias = 0`. If the
old path clears a candidate patch even when a cut/limit rule prevents an
individual sample change, retain that result unless tests and a deliberate
decision establish that only actually changed patches should be reset.

### Brush modes

Preserve all four current modes:

- `hType == 0`, additive: one prepared mutation pass with radial falloff;
- `hType == 1`, conditional/radius add: capture the center reference height
  from the owning slice first, then use the same direct iteration for the
  remaining samples;
- `hType == 2`, fixed height: one prepared mutation pass toward `hFixed`;
- `hType == 3`, flatten/average: first sum accepted local samples across all
  prepared slices, calculate the common average, then perform the mutation
  pass.

Flatten genuinely needs a read pass before mutation. It must still reuse the
same prepared slices and local bounds; it must not repeat terrain discovery or
coordinate conversion.

Keep direction, alpha, center-sample handling, radius inclusion, falloff, clamp
behavior, and floating-point comparison compatible with the present output.
Before optimizing formulas further, compare complete resulting height arrays
between old and new paths.

### Undo and no-op edits

The safest initial implementation may capture undo once per prepared terrain
before its first possible mutation. After parity is established, it may be
made lazy:

1. calculate the candidate new height without writing it;
2. if it differs and the tile has no snapshot in this operation, call
   `Undo::PushTerrainHeightMap()`;
3. then perform the first write.

This preserves the complete pre-edit tile while avoiding a 4-16 MiB snapshot
for an operation which changes nothing. Never take the snapshot after the first
write.

Return and mark only terrains which the preserved brush semantics consider
affected. A later cleanup may distinguish `visited`, `ErrorBias touched`, and
`height changed`, but should not silently change save state, object refresh, or
undo behavior during the traversal rewrite.

### Commit once per tile

After processing one slice:

- clear ErrorBias for its unique touched patches;
- call `setModified(true)` according to preserved semantics;
- call `invalidateSamples(minX, minZ, maxX, maxZ,
  TerrainDirtyHeight | TerrainDirtyNormals)` once when heights changed;
- call `refreshModified()` once;
- add the terrain to the returned set once;
- call `updateTerrainHeightmap()` once, retaining existing client behavior for
  now.

The paged backend can then regenerate only the modified patches and neighbouring
normal dependencies exactly as it does today. Do not replace
`refreshModified()` with a complete `refresh()`.

## Suggested implementation structure

Keep World discovery, layout arithmetic, and brush formulas testable rather
than building another monolithic function. A reasonable separation is:

```text
collectTerrainBrushSlices(...)
    World/QuadTree discovery, deduplication and policy checks

prepareTerrainBrushSlice(...)
    local center, clamped sample rectangle and layout values

forEachBrushSample(slice, callback)
    direct sample iteration and circular inclusion

applyHeightBrush(...)
    hType-specific height calculation

commitTerrainBrushSlice(...)
    ErrorBias, dirty bounds, modified state and GPU refresh
```

These names are illustrative. Avoid a callback abstraction if profiling shows
it prevents inlining or complicates the hot loop; the important boundary is
one discovery/preparation per terrain and no terrain lookup per sample.

Do not generalize the first patch into a large universal terrain-raster
framework. The track-bed action raster solves a different problem involving
continuous distance to object lines and overlapping influences. Shared
coordinate helpers are welcome, but forcing both algorithms through one
abstraction is not required.

## Measurement plan

Measure event duration directly; editor FPS alone mixes render time, mouse
event delivery, and synchronous editing stalls.

Record at least:

- terrain discovery/preparation time;
- undo snapshot time, reported separately for first and subsequent events;
- average/read pass time for `hType == 3`;
- mutation-loop time;
- ErrorBias/dirty commit time;
- `refreshModified()` time;
- route terrain-object update time;
- complete `Route::paintHeightMap()` time.

Use a release-like optimized build. Warm terrain and GPU resources before
collecting samples. Report median and a high percentile across repeated runs;
do not rely on one first run.

Benchmark the same physical brush radii and positions on:

```text
256 samples @ 8 m
512 samples @ 4 m
1024 samples @ 2 m
2048 samples @ 1 m
```

Include an interior position, a terrain edge, and a terrain corner. Run both a
single event and a continuous drag. Keep the primary performance route empty;
use a separate populated-route check only to ensure forest/transfer behavior
has not changed.

The report must compare:

1. current complete path;
2. current path with dirty collection/mesh refresh disabled, reproducing the
   diagnosis;
3. tile-oriented path with refresh disabled, isolating CPU improvement;
4. complete tile-oriented path with normal regeneration and upload enabled.

## Correctness tests

For deterministic inputs, run old and new brush implementations from identical
height arrays and compare:

- every resulting height sample;
- dirty rectangles;
- patches whose ErrorBias becomes zero;
- modified terrain set;
- undo then redo results;
- save/reload heightmaps;
- paged mesh refresh coverage, including neighbouring normals;
- ordinary and synthesized tile edges.

Cover:

- all four `hType` values;
- positive and negative direction;
- zero and nonzero alpha where allowed;
- minimum, typical, and maximum GUI brush radii;
- brush centers exactly on a sample, between samples, on a tile edge, and on a
  tile corner;
- P16 and P32 terrain;
- different terrain physical sizes already supported by the QuadTree;
- same-resolution neighbouring tiles;
- mixed-resolution neighbours, confirming the current explicit skip policy;
- no-op fixed/flatten operations;
- editing with no open undo state and with an existing open state.

Exact float equality is preferred where operation order remains identical. If
flatten accumulation order changes across terrain slices, document the reason,
use a narrowly justified tolerance, and visually/numerically check seam
behavior. Do not accept broad tolerances which can conceal a different brush.

## Acceptance criteria

- The hot local sample loop contains no `getTerrainByXY()`,
  `Game::check_coords()`, `getLocalCoords()`, coordinate-based
  `setErrorBias()`, per-sample `QSet` insertion, or per-sample `QHash` dirty
  update.
- Each intersected terrain is discovered, prepared, committed, and refreshed
  at most once per brush event.
- ErrorBias is cleared at most once per touched patch per event.
- Additive, conditional, fixed, and flatten results match the preserved path.
- Undo/redo, modified state, edges, save/reload, and mixed-resolution rejection
  remain correct.
- Profiling on the empty 1024/2 m reproduction shows that CPU traversal is no
  longer the dominant event cost. Publish measured old/new timings rather than
  asserting improvement from code structure alone.
- 2048/1 m results are reported even if the largest legal brush cannot meet an
  interactive frame budget; work must scale with actual visited samples, not
  with repeated World/QuadTree operations per sample.
- Both precomputed/legacy and on-GPU/paged terrain renderers receive correct
  invalidation. The traversal must not depend on renderer selection.

## Explicit non-goals

- changing terrain LOD, AS/E interpretation, normals, vertex formats, VBO
  paging, or shaders;
- optimizing forest/transfer reconstruction on populated routes;
- redesigning the network protocol or sending dirty height rectangles;
- enabling mixed-resolution seam painting;
- replacing full-tile undo snapshots with delta undo;
- adding mouse-event throttling, stroke interpolation, or background editing;
- merging this brush with KEY_F/action-raster terrain adjustment;
- changing GUI brush meanings or visual falloff.

Those may become valuable follow-up tasks after the tile-oriented traversal is
measured. In particular, dirty-rectangle network transfer and delta undo could
remove large N-dependent costs, but neither should be mixed into the first CPU
brush rewrite.
