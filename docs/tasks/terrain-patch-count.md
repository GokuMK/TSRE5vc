# Variable terrain patch-count support

Status: separate deferred implementation task.

Prerequisite/related task: [heightmap resolution with fixed 16 x 16 patches](terrain-heightmap-resolution.md).

## Objective

Allow `tfile->patchsetNpatches` (`P`) to differ from 16 after heightmap
resolution support is stable. This task changes the number of patch records,
patch-owned textures/state/water objects, selection IDs, and per-patch loops.
It does not change the fixed 2048 m World-file coordinate lattice.

To isolate this axis during development, hold the sample grid and terrain
extent constant first: use `N=256`, `S=8 m`, and vary only `P`.

Derived values:

- patches per terrain side: `P`;
- total patch records: `P * P`;
- samples per patch side: `R = N / P`;
- physical patch size: `terrainWorldSize / P = R * S`.

`P > 0`, `N >= P`, and `N % P == 0` are required by the current regular-grid
renderer.

## Why this is separate

The heightmap RAW body is sized by `N`, not `P`. Conversely, patch count
changes the number of texture/material records and runtime patch objects even
when the heightmap remains exactly 256/8. Combining the two changes would make
buffer, UV, selection, water, and file-format failures difficult to attribute.

The fixed-16 resolution task may read `P` and must reject `P != 16` safely, but
it should not make runtime patch state dynamic.

## What is already dynamic

- `TFile::initNew()` allocates `tdata` as `P * P * 13`.
- `.t` parsing allocates patch records using the descriptor count.
- `.t` sizing and serialization loop over `P * P`.
- parts of texture loading, saving, rendering, and coordinate lookup already
  use `patchsetNpatches` and `N / P`.
- Open Rails derives `PatchCount` and `PatchSampleCount` independently in its
  runtime renderer, providing a useful design reference:
  <https://github.com/openrails/openrails-unstable/blob/41fbd610f3221ece60ef762b50d2f708e92eda9d/Source/RunActivity/Viewer3D/Terrain.cs>.

## Blocking findings

### 1. Runtime patch state is statically sized to 256

`Terrain.h` declares `hidden`, `uniqueTex`, `texid`, `texid2`, `texModified`,
`texLocked`, and `selectedPatchs` as arrays of 256. `WaterTile` likewise owns
256 `OglObj` entries. Constructors, destructor/cleanup paths, texture methods,
selection, water, and error-bias code contain fixed 256 loops.

Required change:

- allocate patch state as `P * P` after `.t` metadata is validated;
- use a single patch index helper, for example `index = z * P + x`;
- make ownership/cleanup safe on partial load;
- initialize defaults through one routine shared by local and client terrain.

### 2. Terrain client initializes 256 entries before it knows `P`

`TerrainClient::load()` performs fixed-size initialization during its staged
load, while the authoritative patch count arrives in the `.t` payload.

Required change:

- defer patch-container finalization until the descriptor is parsed;
- validate `P`, `P * P`, and received patch-record count before committing;
- reject or reset a staged tile atomically on mismatch.

### 3. Selection/picking encodes the patch index in eight bits

Terrain picking stores the selected patch in the low byte of the selection
colour. This makes 256 total patches a format/protocol limit in the current
picker, not merely an array-size bug. Selection range code also divides by 16
and many selection loops stop at 256.

Required change:

- define a wider, collision-free picking encoding or use an integer picking
  attachment/object table;
- update both encoder and decoder together;
- derive row/column as `index / P` and `index % P`;
- document the maximum supported `P` after accounting for GPU/API limits.

Supporting `P=8` alone can temporarily fit the old byte, but full variable
patch-count support must handle `P=32` (1024 patches) or explicitly declare a
lower maximum.

### 4. Many patch loops and indexes still use 16 or 256

Examples in `src/tsre/world/Terrain.cpp` include:

- texture selection, lock, rotate/mirror/scale, unique-texture, and reset
  operations looping over 256;
- texture painting/indexing using `y * 16 + x`;
- selected-range calculations using `/ 16`;
- `refreshWaterShapes()` iterating 16 x 16 and indexing `uu * 16 + yy`;
- `menuSelectObjects()` iterating 16 x 16 with a hard-coded 128 m patch size;
- renderer/alternate-renderer height and F indexes using `patch * 16 + cell`;
- line overlays iterating 16 x 16 patches.

Required change:

- use `P` for patch rows, columns, and flattening;
- use `R` only for cells inside each patch;
- use `terrainWorldSize / P` for patch metres;
- keep texture-coordinate constants separate from patch-count constants.

### 5. Gap/F operations conflate patch count and patch resolution

`removeAllGaps()` walks a selected patch as 16 x 16 samples. This is only
correct in the standard profile where both `P` and `R` happen to be 16.

Required change:

- patch selection/indexing uses `P`;
- F-cell iteration inside a patch uses `R`;
- bounds and border ownership follow the shared height-grid layout.

### 6. Texture transforms contain both patch-count and cell-count assumptions

Patch record indexing must use `P`, while UV increments across a patch depend
on `R`. Whole-terrain map textures depend on `N`. Existing formulas mix these
values because all were related by 16 in the standard profile.

Required change:

- name APIs by coordinate domain: patch index, cell-in-patch, or whole-grid;
- generate/reset per-patch UVs from `R`;
- position each patch in a whole-terrain texture from `P`;
- test every transform with `P != 16` while holding `N` fixed.

### 7. Water and object-range behavior assumes 16 patches at 128 m

Water shapes must be stored and invalidated for `P * P` patches.
`menuSelectObjects()` must calculate ranges from the selected patch bounds in
terrain/world space; `i * 128 - 1024` is not valid for arbitrary terrain size
or `P`.

World objects are still owned by independent 2048 m `.w` files. If one terrain
patch overlaps more than one World file, range selection must query every
covered World file rather than changing World ownership.

## Implementation outline

1. Reuse the validated layout from the resolution task and remove its
   `P == 16` rejection behind an explicit feature gate.
2. Replace every patch-owned fixed array with a `P * P` container and
   centralize flatten/unflatten helpers.
3. Update local/client initialization, cleanup, save, reload, and texture
   ownership.
4. Redesign terrain picking for more than 256 patch IDs, then update range and
   multi-selection.
5. Convert water, F/gaps, texture operations, overlays, renderer offsets, and
   object-range selection to the correct `P`/`R`/metre domains.
6. Add malformed descriptor/resource caps and the regression matrix below.

## Regression matrix

Keep `N=256`, `S=8 m`, and terrain size 2048 m initially:

| Patch grid | Result | Purpose |
| --- | --- | --- |
| `P=8`, `R=32` | supported | fewer patch records; larger patches |
| `P=16`, `R=16` | supported | compatibility baseline |
| `P=32`, `R=8` | supported or documented cap | more than 256 patch IDs |
| `P=10` | rejected | `N % P != 0` |
| `P=0`, negative, or overflow-sized | rejected before allocation | malformed input |

For supported cases verify load/render/save, texture record round trip, patch
picking, single/range selection, water visibility/levels, gaps, overlays,
unique/locked/painted textures, object-range selection, client/server loading,
and repeated destruction under heap checking.

After isolating `P`, combine it with at least one nonstandard heightmap profile
such as `N=512`, `S=4 m` to prove the code does not again conflate `P` and `R`.

## Acceptance criteria

- no patch-owned runtime array is fixed at 256 entries;
- all patch loops/indexes use `P`, all cell-in-patch loops use `R`, and all
  physical patch extents use `terrainWorldSize / P`;
- picking works for the documented maximum patch count without ID collisions;
- `.t` patch record count is validated against `P * P` before use;
- standard `P=16` terrain remains byte-compatible and visually equivalent;
- variable patch count does not change the fixed 2048 m World-file lattice or
  World object ownership.

