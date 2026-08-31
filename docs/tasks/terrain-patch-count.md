# Variable terrain patch-count editing support

Status: implemented as part of terrain heightmap resolution support.

Related task: [heightmap resolution support](terrain-heightmap-resolution.md).

## Result

TSRE loads, renders, edits, and saves regular terrain patch grids from 1 x 1
through 16 x 16 when the complete terrain layout is valid:

- `1 <= P <= 16`;
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
and 16 x 16 for each of its 256/8, 512/4, and 1024/2 heightmap profiles.
All other tile-creation workflows retain the normal 256/8, 16 x 16 default.

## Editability capability

`Terrain` retains an explicit `editable` capability. For every layout that is
currently accepted, `1 <= P <= 16`, it is true. Editing APIs and saving still
check it.

This is intentional preparation for future compatibility work. TSRE may later
choose to display a layout such as 32 x 32 or a non-regular custom layout
without immediately supporting all editing operations. Such a layout can then
be loaded read-only by changing layout acceptance separately from editing
acceptance. The flag should not be removed merely because every currently
accepted layout is editable.

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

Runtime patch arrays retain a capacity of 256. This is deliberate and is
sufficient for the accepted maximum of 16 x 16. Supporting more than 16
patches per side remains a separate feature because it requires larger
storage and review of the eight-bit picking identifier.

Patch texture matrices consume raw patch-local sample coordinates from zero
through `R`. A default once-per-patch transform therefore uses a linear scale
of `1/R`. Map texture generation advances by `1/(P*R)`, equivalently `1/N`,
within a sample. This matches Open Rails' renderer and avoids imposing the
unsubstantiated fixed-16 normalization used by SCOmod. Legacy terrain is
unchanged because its usual value is `R=16`.

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
| `P=17` | not accepted | rejected | rejected |
| `P=10` | non-integral for `N=256` | rejected | rejected |
| `P<=0` | invalid | rejected | rejected |

The same rules combine independently with other accepted heightmap
resolutions, including 512/4 and 1024/2.

## Verification

The focused terrain-grid suite covers:

- the 256/8, 512/4, and 1024/2 profiles;
- 256/8 profile construction with 4 x 4 and 8 x 8 patch grids;
- editable 4 x 4 and 16 x 16 layouts;
- resolution-dependent default texture-matrix scale;
- 4 x 4 record bounds and patch-index round trips;
- rejection above 16 patches and of non-divisible grids;
- a synthetic future 32 x 32 layout remaining non-editable.

The terrain-file corpus suite parses descriptors, validates payload lengths,
loads height/F data, and reports editable versus read-only layouts across
stock MSTS routes and the CMK route. Every accepted patch layout should now be
reported editable; malformed or truncated payloads remain rejected. The final
stock MSTS scan accepted all 1108 descriptors, reported all 1108 editable, and
performed reversible patch-record edits on all 86 non-16 grids with no
failures. It loaded 1106 payloads; the two pre-existing truncated USA1 RAW
files remained safely rejected. CMK loaded all 1194 terrain files and reported
all 1194 editable.

## Remaining boundary

This implementation does not authorize more than 16 patches per side. Future
work may widen descriptor acceptance while leaving `supportsEditing()` false,
which will use the retained read-only capability without weakening current
editing guarantees.
