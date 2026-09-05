# Terrain normal calculation

The paged terrain backend uses one optimized CPU normal calculation and packing
implementation in [TerrainNormals.h](../../src/tsre/world/TerrainNormals.h) /
[TerrainNormals.cpp](../../src/tsre/world/TerrainNormals.cpp). It has no Qt or
OpenGL dependency and operates directly on float height rows.

After exact-output tests and successful interactive testing, the old selectable
normal calculation and packer were removed. There is no reference mode,
`TSRE_TERRAIN_NORMALS` setting, or `reference` launcher argument anymore.
The legacy/precomputed terrain backend itself is unchanged; this cleanup
concerns only the paged backend's temporary normal-comparison implementation.
No terrain files, vertex layout, topology, shaders or brush footprints change.

## Normal definition and boundaries

Normals still represent the same area-weighted triangles, not central
differences or a different smoothing rule:

```text
triangle p00,p10,p01: (S*(h00-h10), S*S, S*(h00-h01))
triangle p11,p01,p10: (S*(h01-h11), S*S, S*(h10-h11))
```

`TerrainNormals::calculate()` reads the centre plus W, E, N, NE, S and SW for
an interior sample on an exactly uniform grid. It reuses repeated terms and
preserves the six NW/NE/SW/SE triangle contributions and their accumulation
order, then normalizes with a square root and division. No persistent normal
array or reciprocal-square-root approximation is introduced.

`uniformCoordinates()` is cached once per backend and selects the constant-
spacing interior arithmetic where coordinate differences are exact. A false
result does not reject a tile. At outer edges/corners and for nonuniform float
coordinate differences, the same algebraic triangle equations use actual
neighbour coordinate differences and include only triangles that exist.
This replaces the old reference fallback; it is not the old cell-construction/
cross-product implementation under another selectable name. Synthesized N
samples are covered, as are fractional/non-power-of-two spacings.

Do not reassociate these sums with `-ffast-math`.

## Packing

`packNormal()` clamps components to [-1,1], multiplies by 511 as a float,
adds +0.5/-0.5 in double precision and truncates to integer, matching the
original round-half-away-from-zero convention. Using float for the addition
would incorrectly round some values just below a half tie. Sign masks,
clamping, the 10-bit components and the gap flag are unchanged.

## CPU-only regression tests and benchmark

```powershell
& .\build\TSRE5vc.exe --test --test-suite terrain-normals
& .\build\TSRE5vc.exe --test --test-suite terrain-normal-benchmark `
    --test-cases 'C:/MagiPacks/Microsoft Train Simulator/ROUTES/ularge'
```

The tests independently enumerate the actual mesh triangles and compute
cross products as a **test-only geometric oracle**. This checks the defining
geometry, including borders, without retaining a selectable legacy runtime
method. Component values are compared exactly (signed zero is not
distinguished), and packed values are checked with both gap states.

Coverage includes flat/sloped surfaces, irregular heights, cliffs,
checkerboards, peaks, large base heights with small differences, all tile
edges/corners, sample counts through 2048, and fractional/non-power-of-two
spacing. Packing tests check every half-rounding boundary and adjacent floats,
one million float bit patterns, clamps, signed zero, infinities and NaNs.

Validation after removing the runtime reference path: 132/132 normal tests
passed, comparing 16,899,906 vertex normals against the geometric oracle.
The profiled terrain-brush/renderer suite passed 367/367, terrain-grid 61/61,
and terrain-edges 52/52. Both real `ularge` CPU benchmark workloads also
passed exact packed-output parity checks.

The CPU benchmark times only the current calculation, packing and output
writes. A geometric oracle is evaluated once outside timing to check outputs.
It has three warm-ups and 20 measured runs on each workload:

- Centre 16 x 16 patches: 1,081,600 vertices, including duplicated patch edges.
- Whole P32 tile: 4,326,400 vertices, including outer borders.

The route fixture requires a 2048/1 m P32 start tile. Loading, allocation and
parity checks are outside timing; terrain is neither edited nor saved. Omit
`--test-cases` for synthetic rough terrain. No GL backend/context, upload or
drawing is used by the benchmark. Its result is not full brush latency or FPS.

For visible-window timings, follow the launch commands and
[profiling procedure](terrain-brush-profiling.md).

## Historical comparison before removal of the reference path

Release MinGW GCC 13.1 `-O3`, AMD Custom APU 0932; three independent launches,
20 measured repetitions per method/workload in each launch (60 total):

| Workload | Reference mean | Optimized mean | Speedup |
|---|---:|---:|---:|
| Centre 256 patches / 1,081,600 vertices | 62.800 ms | 23.827 ms | 2.64x |
| Whole tile / 4,326,400 vertices | 245.362 ms | 95.166 ms | 2.58x |

Per-launch means, retaining variability rather than treating background load
as controlled:

| Workload / method | Launch 1 | Launch 2 | Launch 3 |
|---|---:|---:|---:|
| Centre reference | 62.871 ms | 59.890 ms | 65.639 ms |
| Centre optimized | 24.522 ms | 23.180 ms | 23.780 ms |
| Whole reference | 255.878 ms | 239.281 ms | 240.928 ms |
| Whole optimized | 101.144 ms | 92.671 ms | 91.682 ms |

Every packed output matched the reference in every repetition. These include
both the algebraic normal optimization and faster equivalent packing. A first
prototype retaining the old packer had a smaller gain; the rounding calls were
therefore optimized too. Do not infer an equivalent FPS multiplier: undo,
editing, bounds, GPU uploads and rendering remain outside this benchmark.

Related: [terrain height-brush performance](../tasks/terrain/terrain-height-brush-performance.md).
