# Variable terrain patch-count editing support

Status: regular grids through 32 x 32 are implemented for loading, viewing,
editing, and saving.

Related task: [heightmap resolution support](terrain-heightmap-resolution.md).

## Result

TSRE loads, renders, edits, and saves regular terrain patch grids from 1 x 1
through 32 x 32 when the complete terrain layout is valid:

- `1 <= P <= 32`;
- `16 <= N <= 2048`;
- `N % P == 0`;
- sample spacing is a positive whole number of metres;
- the terrain footprint covers a whole number of 2048 m World tiles;
- sample rotation is zero.

This includes the 4 x 4 distant-terrain tiles shipped with MSTS and ordinary
terrain using intermediate grids such as 8 x 8. Patch-count support is based
on the descriptor, not on whether the tile is stored under `Tiles` or
`Lo_tiles`.

The experimental B-key tile creator exposes patch grids of 4 x 4, 8 x 8,
16 x 16, and 32 x 32 for each of its 128/16, 256/8, 512/4, and 1024/2
heightmap profiles.
All other tile-creation workflows retain the normal 256/8, 16 x 16 default.

## Editability capability

`Terrain` retains an explicit `editable` capability. For every layout that is
currently accepted, `1 <= P <= 32`, it is true. Editing APIs and saving still
check it.

The flag remains necessary for malformed layouts and for future custom
layouts that TSRE may choose to display without editing.

## 32 x 32 compatibility and editing

TSRE accepts otherwise valid regular grids through `P = 32` for loading,
viewing, editing, and saving. Loadability and editability use separate limits:

```text
MaximumLoadablePatchesPerSide = 32
MaximumEditablePatchesPerSide = 32
```

They remain separate capabilities even though both currently equal 32, so
future custom layouts can have a different load/edit policy.

All existing layout checks still apply. In particular, `N % P` must be zero,
the sample grid and spacing must describe a supported complete terrain
footprint, and rotated or malformed layouts remain unsupported.

### Runtime patch capacity

Patch-indexed runtime state uses a fixed capacity of 1024 records. The small
fixed arrays do not create a meaningful memory problem compared with the
terrain height and render buffers. The capacity covers:

- `hidden`, `uniqueTex`, `texid`, and `texid2`;
- `texModified`, `texLocked`, and `selectedPatchs`;
- per-layer water patch objects;
- initialization and network-client reset loops.

Active loops use `P * P`, not the 1024-record capacity.

### B-key replacement is not terrain editing

The experimental B-key command is a tile creation/replacement operation. It
must be allowed to replace an existing non-editable or invalid terrain tile.
For example, a damaged descriptor may exist on disk but fail validation and
leave its `Terrain` object unloaded and non-editable. The user must still be
able to use B, confirm replacement, and write a new supported test-tile
profile at that location.

Replacement detection must therefore be based on the existing terrain/QuadTree
entry and files, not on `Terrain::loaded` or `Terrain::isEditable()`. The
normal editing functions and normal `Terrain::save()` path must continue to
respect `editable`; B calls the explicit empty-tile replacement path instead.
No separate `saveable` capability is required merely to support this recovery
operation.

B must also respect the global application/route write state. When
`Game::writeEnabled` is false, it must not create or replace terrain, remove
old RAW buffers, mutate the QuadTree, create a World file, or initiate a
write-related reload. Guard the UI before showing the creation dialog and
also guard the mutating terrain-library entry point before it changes the
QuadTree, so a future direct caller cannot bypass the global write-disabled
state.

The B dialog includes `32 x 32` as an experimental patch-count choice. Other
tile-creation commands retain their existing 16 x 16 default. This provides
an isolated way to create tiles for selection, editing, save, and reload tests.

### Camera-relative patch picking

The terrain selection colour has only eight bits for a patch value,
while a 32 x 32 grid has patch IDs from 0 through 1023. Keep the existing
colour layout by storing a camera-relative pseudo ID instead of the direct
patch ID. At most the 16 x 16 patch window nearest the camera is selectable in
one selection pass; all 32 x 32 patches continue to render normally.

For a regular grid, calculate a clamped selection window with origin
`windowRow`, `windowColumn` and size 16 x 16. A patch inside that window maps
to:

```text
selectionRow = patchRow - windowRow
selectionColumn = patchColumn - windowColumn
selectionId = selectionRow * 16 + selectionColumn
```

Patches outside the window have no selection ID and should not write a false
patch colour into the selection buffer. The reverse mapping is:

```text
patchRow = windowRow + selectionId / 16
patchColumn = windowColumn + selectionId % 16
patchId = patchRow * P + patchColumn
```

`TerrainPatchSelectionWindow` computes the mapping once from the camera
position during the selection render. `Terrain` caches that exact window and
uses it for decoding, so crossing a patch boundary after rendering cannot map
the pseudo ID to a different patch.

The window is centered on the camera's patch when possible and clamped at the
terrain edges. Tests cover all four edges, all four corners, a centered
window, forward/reverse round trips, and patches outside the active window.

## Implementation details

`TerrainGridLayout` is the source of the active geometry:

- patches per side: `P`;
- patch records: `P * P`;
- samples per patch side: `R = N / P`;
- physical patch size: `R * S`;
- flattening: `row * P + column`.

It also centralizes patch-index validation and flatten/unflatten helpers.
Selection, texture operations, water/draw flags, gap editing, error bias,
overlays, picking, and save paths operate on the active `P * P` records.
Loops inside a patch use `R`, not `P`.

Empty-tile creation also derives patch descriptor geometry from physical patch
size. Open Rails names descriptor field `+3` `FactorY`; its underlying formula
or purpose is not yet documented. MSRE confirms that the legacy
`99.48125458` value for a 128 m patch scales linearly and rewrites a flat 64 m
patch to approximately `49.7406`, so new descriptors apply
`patchWorldSize / 128`. This must not be confused with field `+5`, named
`RadiusM` by Open Rails, which is the physical patch half-size. Both fields are
separate from the UV matrix.

Runtime patch arrays have a fixed capacity of 1024. The camera-relative
picking scheme above keeps the existing eight-bit colour field while all
1024 real patch IDs remain available to editing code.

Patch texture matrices consume raw patch-local sample coordinates from zero
through `R`. A default once-per-patch transform therefore uses a linear scale
of `1/R`. Map texture generation advances by `1/(P*R)`, equivalently `1/N`,
within a sample. This matches Open Rails' renderer and avoids imposing the
unsubstantiated fixed-16 normalization used by SCOmod. Legacy terrain is
unchanged because its usual value is `R=16`.

This model was confirmed directly in the original MSTS Route Editor on
2026-09-01. Equivalent texture placement on `128/16` terrain (`R=8`) and
standard `256/16` terrain (`R=16`) rendered identically in MSRE and TSRE when
the stored transform used `1/R`. SCOmod's fixed-16 domain and `16/R`
compensation are therefore a known bug, not an alternative compatibility
interpretation. Future agents must not use that fork as authority for patch
texture coordinates.

The same experiment established two independent MSRE limits:

```text
N <= 256
R = N / P <= 16
```

Thus `128/16` and `128/8` are MSRE-compatible, while `128/4`, `256/8`, and
`256/4` are refused because `R > 16`; all profiles above 256 samples are
refused because `N > 256`. These restrictions describe MSRE, not TSRE. TSRE
continues to accept the broader validated matrix documented here.

## World-file independence

Changing terrain patch count does not change World-file ownership or the
2048 m `.w` coordinate lattice. A larger terrain tile may cover several World
files. Patch geometry uses the terrain layout, while World objects remain in
their independent 2048 m files.

The existing object-range command still has its separate limitation for
terrain footprints larger than 2048 m. That is not a reason to reinterpret
2048 m World constants as terrain patch constants.

## Supported matrix

For a 256-sample, 8 m terrain footprint:

| Patch grid | Samples per patch | Load/render | Edit/save |
| --- | ---: | --- | --- |
| `P=1` | `R=256` | supported | supported |
| `P=4` | `R=64` | supported | supported |
| `P=8` | `R=32` | supported | supported |
| `P=16` | `R=16` | supported | supported |
| `P=32` | `R=8` | supported | supported |
| `P=17` | not accepted | rejected | rejected |
| `P=10` | non-integral for `N=256` | rejected | rejected |
| `P<=0` | invalid | rejected | rejected |

The same rules combine independently with other accepted heightmap
resolutions, including 128/16, 512/4, and 1024/2.

## Verification

The focused terrain-grid suite covers 34 cases, including:

- the 128/16, 256/8, 512/4, and 1024/2 profiles;
- 256/8 profile construction with 4 x 4, 8 x 8, and 32 x 32 patch grids;
- editable 4 x 4, 16 x 16, and 32 x 32 layouts;
- resolution-dependent default texture-matrix scale;
- 4 x 4 record bounds and patch-index round trips;
- rejection above 32 patches and of non-divisible grids;
- camera-relative selection windows at every edge and corner, a centered
  window, exclusion outside the window, and complete forward/reverse mapping;
- a real 512/4, 32 x 32 descriptor and RAW payload load, including the
  `1/16` UV scale and the MSRE-confirmed `FactorY` value for a 64 m patch;
- mutation of error bias and flags on patch ID 1023 through normal `Terrain`
  APIs, followed by normal save and descriptor reload;
- preservation of E/N resource names during tile replacement;
- refusal of empty-tile creation when `Game::writeEnabled` is false.

Rendering and interactive editor testing remain appropriate for the OpenGL
paths, especially textured terrain, water, picking as the camera crosses a
window boundary, texture tools, gaps, and undo on patch IDs above 255.

The terrain-file corpus suite parses descriptors, validates payload lengths,
loads height/F data, and reports editable versus read-only layouts across
stock MSTS routes and the CMK route. Every accepted patch layout should now be
reported editable; malformed or truncated payloads remain rejected. The
2026-09-01 full MSTS-route-root scan found 1132 descriptor files, including 23
deliberately broken backup fixtures. It accepted and marked editable all 1109
valid descriptors and performed reversible patch-record edits on all 86
non-16 grids with no failures. It loaded 1107 payloads; the two pre-existing
truncated USA1 RAW files remained safely rejected. CMK loaded all 1194 terrain
files and reported all 1194 editable.

## Remaining boundary

The implemented code rejects more than 32 patches per side. Completely
custom, rectangular, or larger patch layouts remain separate work and may
still use the retained non-editable capability.
