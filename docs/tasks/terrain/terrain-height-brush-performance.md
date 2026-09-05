# Terrain height-brush CPU performance

Status: direct tile processing is now the editor brush; reusable float-area API
and buffer-brush example implemented. Original brush retained for comparison
and unusual-grid fallback. Interactive brush/mesh profiling and normal
optimization are complete; the user confirmed the improved editing speed is
sufficient. Exact triangle-weighted normals are retained.

Related tasks:

- [Terrain heightmap resolution support](terrain-heightmap-resolution.md)
- [Paged terrain mesh and shared map rendering](terrain-paged-mesh-and-shared-map.md)
- [Terrain adjacent-edge cache](terrain-adjacent-edge-cache.md)
- [Variable terrain patch count](terrain-patch-count.md)
- [Basic discrete terrain patch LOD](terrain-basic-discrete-lod.md)

## Current integration

Normal generation and packing are now optimized in the paged backend. After
exact-output and interactive validation, the temporary old normal/packing path
and its runtime switch were removed. Boundary/custom-spacing handling uses
direct triangle formulas, verified against a test-only geometric oracle. See the
completed
[normal calculation feature](../../features/terrain-normals.md) for the
CPU-only benchmark and exact float/packed output tests. Historical comparison
measurements remain documented; the current benchmark times the single path.
Dirty-patch selection, whole-patch uploads and brush behavior are unchanged.

Opt-in interactive stage instrumentation is now available through
`TSRE_TERRAIN_BRUSH_PROFILE=1`. See the launch commands in
[the profiling procedure](../../features/terrain-brush-profiling.md).
It separates CPU vertex/normal construction from buffer bind/write time,
records undo/bounds/route-object work and dirty-patch/upload counts, and captures
deferred render-time mesh refreshes as separate records. A real visible-window
2048 capture of five size-49 clicks identified CPU vertex/normal construction
as the main cost: average build 51.654 ms, upload calls 2.288 ms, undo 16.679 ms,
total event 78.871 ms. Each separate click captured a fresh undo snapshot.
The follow-up three-click size-58 capture averaged 74.140 ms in normals/packing
and 4.590 ms in vertex allocation/height writes. Background tasks and a different
brush size mean the two user runs are not directly comparable, but all three
clicks consistently showed normals dominating build time. This motivated the
normal optimization above. The profiler splits those times using diagnostic-only
whole-patch passes; the ordinary builder remains interleaved. See the profiling
feature document for the measurement caveat and byte-parity verification.

`TerrainLibQt::paintHeightMap()` now delegates to `DirectSlices`. Its original
body is retained as `paintHeightMapLegacy()`; both the benchmark's old baseline
and the unusual-spacing fallback call that explicit method, avoiding recursion.
Existing Route callers, returned modified-tile sets and client update hooks are
unchanged. Mixed-spacing neighbours are still skipped by the ordinary brush.

The float path now uses the reusable `TerrainHeightArea::getArea()` / `setArea()`
API. General callers request an inclusive global-metre rectangle, edit its
row-major float heights, then commit. The brush uses the prepared-slice overload
to avoid duplicate discovery. Direct and area-based tools share undo/dirty/patch
commit bookkeeping; the buffer example retains the original brush footprint
semantics through an optional touched mask.

See [Terrain height-area editing API](../../features/terrain-height-area.md)
for the completed API contract, lifetime constraints, mixed-grid behavior and
simple rectangular/radial editing examples. KEY_F remains on its action raster;
this API does not force all terrain tools onto one algorithm.

After extraction, `terrain-brush` passes 360 cases, now including the actual
editor dispatch alongside old/direct/buffer comparisons, plus public-API gather,
scatter, undo, no-op, non-finite, rounded-boundary and read-only tests.
`terrain-grid` and `terrain-edges` pass 61/61 and 52/52 respectively.

The previous measurements below are retained as historical comparison data.
The extracted buffer also uses an optional 801 x 801 byte footprint at size 50
(0.61 MiB in addition to the 2.45 MiB float rectangle). Its `scatterMs` now
includes the `setArea()` commit/refresh; `commitMs` measures subsequent disposal.
Total function timings remain comparable, but old/new per-phase columns must
not be interpreted as identical instrumentation boundaries.

Post-extraction confirmation on `ularge` (same settings, one final launch,
30 same-stroke events per cell; GPU excluded):

| Method | Centre mean / median | Corner mean / median |
|---|---:|---:|
| Legacy reference | 188.999 / 187.053 ms | 188.579 / 186.184 ms |
| Direct, shared inline slice bookkeeping | 6.739 / 6.647 ms | 8.240 / 8.022 ms |
| Reusable buffer example | 11.017 / 10.763 ms | 17.233 / 15.798 ms |

All full-height, metadata and dirty/update comparisons passed again. The buffer
corner run had a 48.792 ms maximum (P95 25.226 ms); its later no-undo batch
averaged 12.366 ms. Retain that variability rather than present this single run
as a new stable buffer baseline. The direct hot-loop bookkeeping remains inline
after extraction to avoid introducing a function call per sample.

## Initial experimental comparison stage (before editor integration)

Both proposals are retained in `src/tsre/world/TerrainHeightBrush.h/.cpp`:

- **DirectSlices** discovers each terrain once, prepares a native sample
  rectangle, and edits its `terrainData` directly in row-major order.
- **HeightBuffer** gathers a world-aligned float rectangle, applies the brush
  to that contiguous array, and scatters the native sample values back. Equal
  grids use row copies during gathering. An explicit mixed-resolution mode
  uses the finest discovered spacing, interpolates the coarser parts of the
  rectangle, and scatters by selecting each native sample's buffer position.
  It does not average away unchanged native samples.

Shared preparation/commit handles undo, touched-patch masks, ErrorBias reset,
dirty rectangles, modified state, edge notifications and `refreshModified()`.
No per-sample World lookup, Qt hash/set insertion, or coordinate-based
`setErrorBias()` is used in the new mutation loops. Buffer allocation,
initialization, gathering, scattering and destruction are included in the
complete benchmark duration; it does not time just the array brush kernel.
The temporary float rectangle is allocated afresh per call, not retained
across events. At size 50 on a 1 m grid it contains 801 x 801 floats:
2,566,404 bytes, approximately 2.45 MiB (separate from undo snapshots).

The mode-1 special centre operation and X-major/Z-minor float accumulation
for flatten are deliberately preserved for compatibility. Flatten calculates
its reference from native samples in both experiments, not from extra
interpolated buffer points. Other editing loops use row-major traversal.

At this comparison stage neither alternative replaced editor dispatch. The
original function was the unchanged reference in the three-way benchmark (it
is now named `paintHeightMapLegacy()`). The only added terrain
mutation API is `setPatchErrorBias(patchId, value)`, permitting one reset per
touched patch without repeating coordinate conversion.

By default both experiments retain the old mixed-spacing skip policy. The
explicit `allowMixedResolution` test option enables native mixed-grid editing
for comparison between the two experiments, not against the old skip result.
This option has not been exposed in the editor. Unusual sample spacings
outside aligned power-of-two grids through 1024 m use the old-path fallback;
they are not rejected at tile loading. General overlap/irregular-grid policy
and interactive mixed-grid brush semantics remain future integration work.

The opt-in `terrain-brush-benchmark` now runs old/direct/buffer automatically,
restoring the same starting height arrays before each variant. Comparison
checks include every float (including N+1 borders), complete serialized `.t`
metadata, dirty rectangles and height-update notifications after identical
sequences of real edits. Test-only callback recording is once per affected
tile and is applied to all three variants.

`terrain-brush` provides deterministic tests on temporary terrain fixtures,
including all four modes, both signs, zero/nonzero alpha, size 0/1/50/100,
centre/edge/corner and fractional positions, native and synthesized boundary
ownership, P4/P8/P16/P32, 4 km/2 km terrain, missing/read-only neighbours,
mixed-spacing skip, explicit mixed-grid direct/buffer agreement, unusual-spacing
fallback after World-coordinate normalization, and actual undo restoration.
The tests compare complete native height arrays and serialized metadata
without broad float tolerances. The suite is included in `all`.

The original direct-slice design below is retained as implementation rationale.
Editor promotion has subsequently been completed, as described above.
GPU-inclusive interactive timings, populated-route tests and redesign of undo
remain separate work. Retain both methods for future tests and
batch/neighbourhood tools rather than deleting the slower variant.

### Three-way results on `ularge`

Three independent Release launches, 30 measured same-stroke events per method
and position after five warm-ups (90 events per table entry). All paths use
size 50 / radius 400 m, no GPU mesh backend, and the same initial heights.

| Method | Centre mean | Corner mean | Centre speedup | Corner speedup |
|---|---:|---:|---:|---:|
| old | 187.531 ms | 193.548 ms | 1.0x | 1.0x |
| direct | 6.982 ms | 9.454 ms | 26.9x | 20.5x |
| buffer | 10.519 ms | 12.139 ms | 17.8x | 15.9x |

Fresh-stroke means (10 events per launch, 30 total) include new full-tile undo
snapshots. No-undo means contain 30 events per launch, 90 total:

| Method | Centre fresh undo | Corner fresh undo | Centre no undo | Corner no undo |
|---|---:|---:|---:|---:|
| old | 199.237 ms | 236.643 ms | 191.916 ms | 206.112 ms |
| direct | 17.226 ms | 50.268 ms | 6.959 ms | 9.086 ms |
| buffer | 20.663 ms | 51.389 ms | 10.427 ms | 11.592 ms |

Per-launch sustained medians and P95, to retain the observed variability:

| Position | Method | Launch 1 median / P95 | Launch 2 median / P95 | Launch 3 median / P95 |
|---|---|---:|---:|---:|
| Centre | old | 185.171 / 215.558 ms | 182.809 / 202.292 ms | 184.804 / 198.056 ms |
| Centre | direct | 6.725 / 7.200 ms | 6.695 / 7.225 ms | 7.074 / 10.031 ms |
| Centre | buffer | 9.838 / 10.201 ms | 11.578 / 14.899 ms | 9.994 / 11.701 ms |
| Corner | old | 185.967 / 214.006 ms | 191.566 / 232.227 ms | 190.749 / 248.661 ms |
| Corner | direct | 8.131 / 9.801 ms | 8.037 / 9.727 ms | 11.572 / 17.590 ms |
| Corner | buffer | 11.355 / 12.400 ms | 11.268 / 12.419 ms | 12.991 / 16.555 ms |

New-path mean phase times across the three launches (milliseconds):

| Position | Method | Prepare | Gather | Edit | Scatter | Commit |
|---|---|---:|---:|---:|---:|---:|
| Centre | direct | 0.010 | 0.000 | 5.374 | 0.000 | 1.592 |
| Centre | buffer | 0.011 | 1.090 | 2.125 | 5.396 | 1.892 |
| Corner | direct | 0.015 | 0.000 | 6.264 | 0.000 | 3.171 |
| Corner | buffer | 0.012 | 1.295 | 2.160 | 5.534 | 3.132 |

Preparation includes existing-snapshot checks; mode-3 average calculation is
also assigned to preparation, but the performance test uses mode 0. Direct
editing and buffer scattering include native dirty/touched-sample tracking.
Commit includes ErrorBias reset, CPU patch-bound refresh, edge notification,
per-tile callbacks and, for the buffer path, buffer disposal. Minor total/phase
differences include timer overhead and result-container cleanup.

Both alternatives matched the old complete resulting heights, serialized
terrain metadata, dirty rectangles and update sets exactly in every benchmark
launch. The `terrain-brush` correctness suite passed 343/343 cases; the existing
`terrain-grid` and `terrain-edges` suites passed 61/61 and 52/52. The real
route was never saved; before/after SHA-256 manifests verify no file changes.

**Conclusion:** direct slices are the faster choice for the simple additive
brush, approximately 20-27x faster than the old path here. The buffer is still
approximately 16-18x faster than old and costs only about 2.7-3.5 ms more per
sustained event than direct slices. It remains a useful retained option for
batch or neighbourhood operations; no smoothing/batch performance claim is
made from this additive benchmark. Fresh four-tile undo snapshots now dominate
first-stroke cost in both alternatives and remain a separate task. Measurements
have noticeable tails and do not include GPU mesh/normal refresh or render time.

## Measured baseline: `ularge`, brush size 50

The original baseline called the production `TerrainLibQt::paintHeightMap()` unchanged,
using the real loaded QuadTree, not a copied algorithm or a synthetic lookup
registry. At that stage only a headless benchmark entry point was added in
`src/tsre/tests/TerrainBrushBenchmark.cpp`. The experimental comparison above
extends that baseline without replacing its original painting function.

Fixture and measurement scope:

- Route: `C:/MagiPacks/Microsoft Train Simulator/ROUTES/ularge`.
- TRK start tile: `-5354, 14849`; TSRE internal World coordinates:
  `-5354, -14849`. Centre terrain descriptor: `-11dbfba0.t`.
- All nine loaded terrain tiles are `2048/1 m`, with `32 x 32` patches and
  a physical footprint of `2048 x 2048 m` each.
- `Brush::size = 50` means **400 m radius**, not 50 m, in the existing height
  brush. `hType = 0`, `alpha = 1`, `direction = +1`.
- Test points are World-tile-local `(0, 0)` and `(1024, 1024)`, relative to
  the start tile. The latter is a four-tile corner, not the midpoint of an edge.
  Returned modified-terrain counts are checked: one and four respectively.
- Release build, MinGW GCC 13.1, `-O3`; reported CPU: AMD Custom APU 0932.
  Tiles, native edge caches and CPU patch bounds are prewarmed outside timing.
- No OpenGL context or terrain mesh backend is created. CPU patch-bound
  recalculation in `refreshModified()` and dirty/edge notification remain
  included; vertex/normal generation and GPU uploads cannot run. Route-level
  forest/transfer updates and multiplayer serialization are not invoked.
- Each position starts with a timed first event including its undo snapshot,
  then five warm-up calls and 30 measured calls in the same undo state. Ten
  fresh-stroke measurements capture undo again; a further 30 calls run with
  no undo state. Snapshot disposal occurs outside timing. Calls stay at the
  specified position and make real, cumulative in-memory height changes.
- Debug logging is disabled during measurement. Route writing is disabled,
  and the benchmark makes no filesystem saves (parity serialization stays in
  memory). This is a function-duration test,
  not a window FPS or mouse-event-frequency measurement.

Run from the repository root:

```powershell
& .\build\TSRE5vc.exe --test --test-suite terrain-brush-benchmark `
    --test-cases 'C:/MagiPacks/Microsoft Train Simulator/ROUTES/ularge'
```

The suite is opt-in (not part of `all`) because it requires this real-route
fixture. It verifies that the start neighbourhood contains nine editable
2048/1 m terrain tiles before measuring.

### Results before optimization

Measured against painting code at commit `30a5a25`, in three independent
process launches. Values below are milliseconds per complete function call.
Each sustained row contains 30 measured events after the five warm-ups:

| Position | Launch | Mean | Median | P95 | Min | Max |
|---|---:|---:|---:|---:|---:|---:|
| Centre `(0,0)` | 1 | 204.146 | 194.663 | 250.413 | 183.205 | 256.872 |
| Centre `(0,0)` | 2 | 189.252 | 183.988 | 217.163 | 178.837 | 218.695 |
| Centre `(0,0)` | 3 | 199.319 | 189.590 | 282.461 | 177.949 | 418.362 |
| Corner `(1024,1024)` | 1 | 215.951 | 202.459 | 277.738 | 186.065 | 307.829 |
| Corner `(1024,1024)` | 2 | 209.607 | 196.522 | 325.070 | 182.198 | 365.462 |
| Corner `(1024,1024)` | 3 | 181.827 | 181.253 | 189.339 | 178.292 | 189.554 |

Aggregated means across equally sized launch batches:

| Undo condition | Events per position | Centre, one tile | Corner, four tiles |
|---|---:|---:|---:|
| Existing same-stroke snapshot | 90 | 197.572 ms | 202.462 ms |
| Fresh undo snapshot per event | 30 | 199.060 ms | 238.706 ms |
| No open undo state | 90 | 194.970 ms | 201.958 ms |

The first brush events after preload, including snapshot capture, took
210.207 / 206.488 / 192.542 ms at the centre and
242.499 / 225.080 / 223.433 ms at the corner in launches 1/2/3.

There is substantial run-to-run/tail variability; do not infer tiny differences
between phases as isolated component costs. The robust finding is that even
without GPU work or undo capture, a sustained event still costs approximately
180-200 ms at its typical/median rate. Crossing four tiles does not multiply
the sustained cost by four. Fresh undo snapshots have a more visible extra
cost at the corner (four roughly 16 MiB height snapshots), but do not explain
the dominant sustained delay.

All three runs completed with the expected affected-tile counts. SHA-256
manifests of **every file below the route directory** matched before and after
the three runs. No route file was changed. The painting function and renderer
were not modified for the baseline. The later experimental comparison adds
separate entry points rather than replacing that reference path.

### Review implications

The selected tile-oriented design below remains appropriate. At this radius
and spacing, each event examines an `801 x 801` square in both main passes;
502,625 lattice positions pass the circular inclusion test. The existing
pre-pass and mutation pass together make approximately **1.14 million terrain
lookups per event**, before small extra seam/notification lookups, although
the edit touches only one or four terrains. This count follows from the loop
structure; it is not a sampled call-stack profile.

The related edge-cache and LOD work does not remove that repeated lookup work.
Preserve its boundary invalidation hooks when replacing traversal. Also keep
CPU patch-bound refresh distinct from mesh/normal/GPU work in later timing:
skipping uploads is not equivalent to skipping all of `refreshModified()`.
The phase-by-phase measurements and old/new parity tests below are still
required during optimization; this baseline measures the complete CPU call.

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

## Original avoidable work (legacy baseline)

The original `TerrainLibQt::paintHeightMap()` (now `paintHeightMapLegacy()`)
performed the following work for each mouse-move event.

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

## Original direct-slice proposal (implemented; retained design rationale)

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
- enabling mixed-resolution seam painting by default in the editor;
- replacing full-tile undo snapshots with delta undo;
- adding mouse-event throttling, stroke interpolation, or background editing;
- merging this brush with KEY_F/action-raster terrain adjustment;
- changing GUI brush meanings or visual falloff.

Those may become valuable follow-up tasks after the tile-oriented traversal is
measured. In particular, dirty-rectangle network transfer and delta undo could
remove large N-dependent costs, but neither should be mixed into the first CPU
brush rewrite.
