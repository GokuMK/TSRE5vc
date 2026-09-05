# Terrain profile compatibility: MSTS Bin 1.8, patched MSTS, and Open Rails

Status: **compatibility matrix; static boundaries plus identified runtime tests**  
Date: 2026-09-05 (MSTS R64 update; Open Rails inspection remains 2026-09-03)

## Scope and notation

This report covers ordinary detailed terrain tiles with a 2,048 m side and
power-of-two sample counts from 128 through 2,048. It compares:

- unmodified MSTS Bin 1.8.052113;
- the terrain build produced by
  [MSTS Bin 1.9 R64 patcher](../../extra/MSTS/bin-1.9/README.md);
- Open Rails `master` commit
  `3a4d79804140d1749e39cc175c8f4a5939bbeb66` and `unstable` commit
  `bbeb7ab6dd00bf7f61503f0b177839095ee7a5b8`, inspected on 2026-09-03.

Open Rails `master` and `unstable` are presented separately. At the inspection
point, `master` still contained the older fixed 16-by-16 patch renderer, while
`unstable` derived the patch mesh size from the terrain descriptors. This
distinction matters for every profile with `R!=16`.

The symbols used below are:

```text
N = terrain_nsamples (140), samples stored per tile side
S = terrain_sample_size (144), horizontal metres per sample
P = terrain_patchset_npatches (161), patches per tile side
R = N / P, samples (mesh squares) per patch side
```

For the 2,048 m profiles in this report, `N*S=2048`.

## Cross-engine profile matrix

This first table shows the common `R=16` family. It is geometrically compatible
with both Open Rails branches because the calculated unstable resolution and
master's fixed resolution are both 16. The next table exposes the difference
for other `N/P` ratios.

| `N` | `S` | `P` for this row | `R` | MSTS Bin 1.8 | Patched MSTS | ORTS `master` | ORTS `unstable` |
|---:|---:|---:|---:|---|---|---|---|
| 128 | 16 m | 8 | 16 | Passes recovered limits; not runtime-tested as this exact tuple | Within patch envelope; not separately tested | Source-compatible; custom tuple not runtime-tested | Source-compatible; custom tuple not runtime-tested |
| 256 | 8 m | 16 | 16 | **Supported standard profile** | **Supported**, inherited | **Supported standard profile** | **Supported standard profile** |
| 512 | 4 m | 32 | 16 | **Rejected:** `N>256` | **User-tested render success** | Source-compatible; custom tuple not runtime-tested | Source-compatible; custom tuple not runtime-tested |
| 1024 | 2 m | 64 | 16 | **Rejected:** `N>256` | **Not supported:** `P>32` | Source-compatible in geometry; 4,096 patches/tile is untested and expensive | Source-compatible in geometry; 4,096 patches/tile is untested and expensive |
| 2048 | 1 m | 128 | 16 | **Rejected:** `N>256` | **Not supported:** `N>1024` and `P>32` | Source-compatible in geometry; 16,384 patches/tile is untested and likely impractical | Source-compatible in geometry; 16,384 patches/tile is untested and likely impractical |

“Source-compatible” for Open Rails is deliberately weaker than “confirmed
supported.” Both branches' arrays and RAW readers use descriptor `N`, and
patch arrays use descriptor `P`. The unstable renderer also uses `R=N/P` for
sample origins, vertex loops, index loops, and shared index-buffer selection;
master does not. No explicit sample-count or patch-count ceiling was found.
Both eagerly construct `P*P` patch primitives for each loaded tile, and no
custom profile above the standard `256/16` tuple has been runtime-tested here.

Open Rails also computes detailed-terrain normals using a fixed horizontal
distance of 8 m (`Size*8`) rather than `terrain_sample_size`. Geometry uses the
parsed sample spacing, but lighting normals can therefore be wrong when
`S!=8`, even for the `R=16` profiles above.

## Supported/common patch combinations

This table lists the common power-of-two patch grids inside each implementation's
established support envelope. Parentheses contain `R=N/P`.

| `N` | MSTS Bin 1.8 | Patched MSTS | ORTS `master` correct geometry | ORTS `unstable` source envelope |
|---:|---|---|---|---|
| 128 | `P=8 (R=16)`, `P=16 (R=8)` | `P=2 (R=64)`, `4 (32)`, `8 (16)`, `16 (8)`, `32 (4)` | `P=8 (R=16)` | Power-of-two `P=1..128` (`R=128..1`) |
| 256 | `P=16 (R=16)` | `P=4 (R=64)`, `8 (32)`, `16 (16)`, `32 (8)` | `P=16 (R=16)` | Power-of-two `P=2..256` (`R=128..1`) |
| 512 | none | `P=8 (R=64)`, `16 (32)`, `32 (16)` | `P=32 (R=16)` | Power-of-two `P=4..512` (`R=128..1`) |
| 1024 | none | `P=16 (R=64)`, `32 (32)` | `P=64 (R=16)` | Power-of-two `P=8..1024` (`R=128..1`) |
| 2048 | none | none | `P=128 (R=16)` | Power-of-two `P=16..2048` (`R=128..1`) |

The MSTS columns describe supported target envelopes, not every value the
descriptor parser can allocate. The stock parser has no general `P<=16`
guard, but larger grids exceed the route-safe design of its 4,096-entry
visible-patch lists and have editor-selection concerns. The patched executable
relocates those lists and is deliberately scoped to `P<=32`.

The Open Rails entries are static geometry envelopes, not practical or tested
recommendations. Very large `P` values create `P*P` patch objects and quickly
become impractical. For `unstable`, correct coverage requires `P` to divide
`N`, and its 16-bit mesh indices require `(R+1)^2<=65,536`, or `R<=255`. For
the power-of-two profiles in this table, that makes `R=128` the largest
source-compatible value.

## Engine boundaries

### Unmodified MSTS Bin 1.8

The recovered necessary terrain limits are:

```text
N <= 256
R = N/P <= 16
```

The relevant terrain code in Bin 1.8 is byte-identical to Microsoft Patch 1.4
at the audited guard and renderer sites. The positive `N=128, P=16, R=8`
MSRE test proves that `N=256` and `R=16` are ceilings, not mandatory exact
values. For route-safe profiles this report retains the stock `P<=16`
capacity envelope.

### Patched MSTS Bin 1.8

The distributed terrain patch is designed for:

```text
N <= 1024
P <= 32
R = N/P <= 64
```

It includes the R64 v5 per-patch renderer changes, N1024 Route Editor bitmap and
seam changes, and two enlarged visible-patch lists sized for sixteen complete
P32 tiles. `N=1024, P=16, R=64` is now supported by the 2026-09-05 patcher
update: v5 dense-hover textured/wireframe rendering was tested successfully
in Wine, with user retests on `mini` and the formerly problematic route.
Earlier failing R64 attempts do not describe the current patcher. R64 remains
experimental: fully dense multi-tile, whole-tile ErrorBias zero, and R64 height
edit/save/reload coverage is incomplete. Other newly admitted R64 tuples are
within the envelope, not automatically runtime-confirmed.

Older Bin 1.9 outputs with the R32 limit must be regenerated with the updated
patcher from clean Bin 1.8 input; the display name alone does not identify
which patcher revision produced an executable.

### Open Rails `master`

The `master` parser and RAW loaders allocate from descriptor `N`, and its patch
arrays allocate from descriptor `P`, but its renderer uses literal 16-by-16
patch geometry:

```text
sample origin = PatchX*16, PatchZ*16
mesh squares  = 16*16
mesh vertices = 17*17
```

Correct full-tile coverage on this branch therefore requires `N/P=16`.
With `R>16`, only the first 16 samples of each nominal patch are rendered. With
`R<16`, later patches address beyond the tile's own sample array and can enter
the cross-tile lookup path.

### Open Rails `unstable`

The `.t` parser and RAW loaders allocate from descriptor `N`, while patch
arrays allocate from descriptor `P`. Unlike `master`, the unstable viewer
derives and uses:

```text
R             = N/P
sample origin = PatchX*R, PatchZ*R
mesh squares  = R*R
mesh vertices = (R+1)*(R+1)
```

It also caches a separate shared index buffer for each encountered `R`.
Consequently, correct full-tile coverage requires:

```text
N % P = 0
1 <= R <= 255
```

The upper bound is inferred from the 16-bit (`short`) indices, not from an
explicit validation guard. The listed custom profiles still need runtime tests,
especially the high-patch-count and dense-patch extremes.

Open Rails `unstable` renders each patch as a full-resolution regular mesh and
does not consume MSTS E/AS adaptive terrain LOD for this purpose.

## Runtime evidence

| Profile | Engine | Result |
|---|---|---|
| `N=128, P=16, R=8` | unmodified MSRE | User-reported load success |
| `N=256, P=16, R=16` | MSTS and both inspected Open Rails branches | Standard baseline |
| `N=512, P=16, R=32` | patched MSRE | User-reported rendering and height-edit/save success after descriptor repair |
| `N=512, P=32, R=16` | patched MSTS | User-reported rendering success |
| `N=1024, P=32, R=32` | patched MSTS | User-reported rendering success with a complete 3-by-3 tile grid |
| `N=1024, P=16, R=64` | MSTS Bin 1.9 R64 v5 update | Successful dense-hover rendering and user retests; broader edit/save/reload matrix remains incomplete |
| Any nonstandard tuple | Open Rails `master` or `unstable` | No runtime result recorded in this workspace |

The enlarged P32 visible-patch lists have been tested successfully with a
complete 3-by-3 `N=1024/P=32` tile grid. Nine complete P32 tiles contain 9,216
patch records, although culling and render passes mean this does not by itself
prove that all 9,216 occupied one list simultaneously. The boundary-centred
sixteen-tile case remains untested.

## Practical choices

- TSRE can load, render, edit, and explicitly create `N=2048, S=1` with P16
  (`R=128`) or P32 (`R=64`). These profiles are experimental testing options,
  not recommendations: their CPU height arrays, editing cost, and GPU vertex
  data are substantially larger than 1024 terrain.
- Maximum profile shared by the current patched MSTS design and Open Rails
  `unstable` geometry: **`N=1024, S=2` with P16/R64 or P32/R32**. P16/R64
  requires the R64 patcher update and has bounded rendering confirmation;
  P32/R32 is confirmed with a 3-by-3 tile grid. Open Rails support is
  source-derived and needs a runtime test, and its normals need the
  sample-spacing fix.
- For Open Rails `master` rather than `unstable`, retain
  **`N=512, S=4, P=32, R=16`** because that branch still assumes `R=16` at the
  inspected commit.
- Maximum stock-compatible detailed profile:
  **`N=256, S=8, P=16, R=16`**.

## Evidence references

- Earlier local analysis: `msts-r32-n512-patch-feasibility.md` (not retained
  in this repository).
- Earlier local analysis: `msts-bin-1.8-p32-n1024-r32-experimental-build.md`
  (not retained in this repository).
- [`extra/MSTS/bin-1.9/README.md`](../../extra/MSTS/bin-1.9/README.md)
- Open Rails unstable `Terrain.cs` at the inspected commit:
  <https://github.com/openrails/openrails/blob/bbeb7ab6dd00bf7f61503f0b177839095ee7a5b8/Source/RunActivity/Viewer3D/Terrain.cs>
- Open Rails unstable `Tiles.cs` at the inspected commit:
  <https://github.com/openrails/openrails/blob/bbeb7ab6dd00bf7f61503f0b177839095ee7a5b8/Source/RunActivity/Viewer3D/Tiles.cs>
- Open Rails unstable `TerrainFile.cs` at the inspected commit:
  <https://github.com/openrails/openrails/blob/bbeb7ab6dd00bf7f61503f0b177839095ee7a5b8/Source/Orts.Formats.Msts/TerrainFile.cs>
- Open Rails `master` `Terrain.cs` at the comparison commit:
  <https://github.com/openrails/openrails/blob/3a4d79804140d1749e39cc175c8f4a5939bbeb66/Source/RunActivity/Viewer3D/Terrain.cs>
- Open Rails `master` `Tiles.cs` at the comparison commit:
  <https://github.com/openrails/openrails/blob/3a4d79804140d1749e39cc175c8f4a5939bbeb66/Source/RunActivity/Viewer3D/Tiles.cs>
- Open Rails `master` `TerrainFile.cs` at the comparison commit:
  <https://github.com/openrails/openrails/blob/3a4d79804140d1749e39cc175c8f4a5939bbeb66/Source/Orts.Formats.Msts/TerrainFile.cs>
