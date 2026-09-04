# TSRE5 loading and use of MSTS terrain `.t` files

Status: **static source audit, executable data-flow review, and stock-file census**
Date: 2026-09-03

## Executive result

TSRE5 does not implement MSTS's adaptive terrain selection. It renders and edits
a regular mesh from `terrain_sample_ybuffer`, uses `terrain_sample_fbuffer` for
terrain gaps, and uses the patch shader index and six-value UV transform. It
parses and writes several adaptive-LOD values—most importantly
`terrain_sample_ebuffer`, `terrain_sample_nbuffer`,
`terrain_sample_asbuffer`, `terrain_errthreshold_scale`,
`terrain_alwaysselect_maxdist`, `terrain_patchset_distance`, and patch
`ErrorBias`—but does not use them to render terrain.

The parser is also incomplete. Unknown blocks are skipped in memory and omitted
when TSRE saves. This makes a TSRE `.t` save potentially lossy for
`terrain_sample_cbuffer`, `terrain_sample_dbuffer`,
`terrain_patchset_fbuffer`, `terrain_transfers`, and `terrain_shapes`. Current
TSRE preserves `terrain_sample_asbuffer` and `terrain_sample_usbuffer` as opaque
blocks, but does not interpret them. Multiple `terrain_patchset` records are
read into one set of member variables, so the last set replaces the earlier
ones and save emits only one.

Shader handling is layout-dependent. Ordinary stock `Tiles` use two ordered
halves—`DetailTerrain` records followed by their corresponding `AlphaTerrain`
records—and TSRE's normal/auxiliary material model matches that MSRE editing
convention. Stock `Lo_tiles`, however, use one flat `TexDiff` table, which can
have an odd number of entries. TSRE currently applies the paired-halves model
to both layouts, so loading and saving a low-detail tile can fold valid shader
indices and omit the last shader.

All token references below include both the symbolic name and numeric ID. The
primary name/order source is the Microsoft/Kuju table shipped with MSTS at
`Game/Utils/FFedit/CoreIDs.tok` (SHA-256
`a596c80f464d7e15ba9a1e188f521b4fd765a7acc83c3728ac8c7189cd6bf3fb`).
The sequence starts at token 1 (`comment`), so its `SIDDEF` order directly gives
the binary IDs: for example, `terrain` is 136,
`terrain_sample_asbuffer` is 281, and `terrain_sample_usbuffer` is 282. These
were cross-checked against TSRE5's `TS.h`/`TS.cpp`, Open Rails `TokenID.cs`, and
the MSTS executable dispatches. Open Rails is correct that its initial enum was
derived from this `CoreIDs.tok`; it anchors `terrain_sample_asbuffer` explicitly
at 281 and handles `terrain_sample_usbuffer` (282) separately in its terrain
parser.

The adjacent FFEdit `worldfile.bnf` is only a grammar for world objects; it is
not the master token-name table and is not expected to define the terrain `.t`
grammar. The supplied `UTILS/FFedit` directory is complete with respect to
these files: it contains `CoreIDs.tok`, `Appids.tok`, `FFEdit.cfg`,
`worldfile.bnf`, and `NewShape.bnf`.

Open Rails' much larger `TokenID` enum has an additional shipped source:
`Appids.tok` includes `forms.hdr` and `loadstr.hdr`. The 1,165 enum names from
`Tr_Worldfile` through `DEMPath` match `loadstr.hdr` exactly and in order.
Open Rails offsets these application IDs by 300 to avoid collisions with Core
IDs, then appends some inferred, arbitrary, ORTS-specific, and TSRE-specific
entries. `FFEditC_Unicode.exe` loads these external token tables according to
`FFEdit.cfg`; it is not necessary to recover a hidden 2,000-name table from the
executable. The source breakdown is recorded in `analysis/token-maps/README.md`.

## Scope and method

The original TSRE5 audit used commit
`9b3b1c971ff9fc04acb9fb35844ea68d8fdafc8e`; the terrain-grid and opaque-buffer
sections were re-audited after the synced change at commit `6d77046`. The
principal code is `src/tsre/world/TFile.cpp`, `TFile.h`, `Terrain.cpp`, and
`TerrainGridLayout.h`. Open Rails was used as an independent parser/consumer
comparison. Previously recovered MSTS functions in the patch-v1.4 `train.exe`
were used only where this report labels behavior “MSTS-confirmed.” No Windows
files, registry, process, or mounted Windows path was accessed.

Terminology in this report is explicit: “TSRE” describes the open-source editor
implementation; “MSTS” or “MSTS executable” describes Microsoft Train
Simulator. An unqualified limitation in a TSRE table is not an MSTS limitation.

The initial two stock corpora were inspected with
`scripts/inspect_msts_tfiles.py`:

| Corpus | `.t` files | Patch records | Parse errors |
|---|---:|---:|---:|
| Tutorial route from CD2 | 36 | 9,216 | 0 |
| Europe1 from `EUROPE1.CAB` | 121 | 30,976 | 0 |

The derived inventories are under `analysis/tfiles/`. The source extraction
remains under the ignored `proprietary/` tree.

A later read-only census expanded this to all six route CABs from the local
MSTS media plus the tutorial route: 1,085 terrain files in total. The CABs
contained 963 ordinary `Tiles` files and 86 `Lo_tiles` files; the tutorial
added 36 ordinary tiles. Every inspected file contained exactly one
`terrain_patchset` (159), and none contained `terrain_transfer` (166) or
`terrain_shape` (168). The temporary CAB extraction was removed after the
census; the original local media was unchanged.

## What TSRE actually opens

`Terrain::load` opens the `.t` file through `TFile::readT`, then opens the file
named by `terrain_sample_ybuffer`. Failure to load Y aborts terrain loading. If
`terrain_sample_fbuffer` exists, TSRE opens it too. It does **not** open the
external files named by `terrain_sample_ebuffer` or
`terrain_sample_nbuffer`; it only retains their names. AS is embedded in the
`.t` file. Current TSRE copies both AS and US label/payload bytes into opaque
storage, but no code outside `TFile` consumes them.

The Y file is little-endian unsigned 16-bit data. TSRE computes height as:

```text
height = terrain_sample_floor + terrain_sample_scale * raw_uint16
```

It expands the N-by-N samples to an `(N+1)`-square working grid by duplicating
the last row and column. F is an N-by-N byte grid; bit `0x04` is TSRE's terrain
gap/hole flag. TSRE edits that bit and writes F back.

## Token-by-token audit

“Preserved” means parsed and normally emitted again by TSRE, not necessarily
byte-identical. “Used” means it affects editor behavior or output rendering.

MSTS evidence labels are deliberately separate:

- **MSTS used (confirmed)** — a downstream executable consumer was traced;
- **MSTS loaded (consumer open)** — parsing/layout is confirmed, but the code
  which gives the value meaning has not yet been recovered;
- **probable** — supported by names, distributions, or another implementation,
  but not yet by the relevant MSTS consumer;
- **unknown** — neither static evidence nor current stock examples establish a
  meaning. This does not automatically mean Windows capture is required;
  further static xref/decompilation can often resolve it.

### Terrain root and samples

| Token | TSRE load/save | Runtime use in TSRE | MSTS load/use status and meaning |
|---|---|---|---|
| `terrain` (136) | Container | Structural | Terrain tile root. |
| `terrain_errthreshold_scale` (137) | Preserved float | None | **MSTS-confirmed:** tile-level float which multiplies patch `ErrorBias` in the adaptive-LOD threshold. It is separate from route `TerrainErrorScale` (1230), which scales the named `trterrain_errthreshold` setting earlier in the same input path. |
| `terrain_alwaysselect_maxdist` (138) | Preserved float | None | **MSTS loaded/saved, no consumer found:** float32 stored at terrain `+0x34`; MSTS caches its square at `+0x38` and serializes the original value. No read of either field was found outside parse/save in the recovered terrain paths. The selector's distance gates instead come from the separately named executable settings `asnear` and `asmax`. |
| `terrain_samples` (139) | Container | Structural | Sample metadata and auxiliary buffers. |
| `terrain_nsamples` (140) | Preserved integer | Heavily used | **MSTS used (confirmed):** number of samples per tile side; controls buffer sizes and adaptive hierarchy. The individual buffer loaders are dynamic, but later stock-MSTS terrain-registration code explicitly rejects values greater than 256. The user's `N=128`, 16 m, `P=16` fixture loads in MSRE, confirming that 256 is a ceiling rather than a required value. Current TSRE supports larger layouts for its own renderer, so TSRE acceptance must not be presented as unmodified-MSTS compatibility. |
| `terrain_sample_rotation` (141) | Preserved float | None found | **MSTS loaded (consumer open):** float32 rotation; recognized values map to quarter turns, but downstream use has not been traced. |
| `terrain_sample_floor` (142) | Preserved float | Used | **MSTS loaded; ordinary role confirmed:** float32 base height for decoded Y samples. |
| `terrain_sample_scale` (143) | Preserved float | Used | **MSTS loaded; ordinary role confirmed:** float32 height increment per Y integer. |
| `terrain_sample_size` (144) | Preserved float | Used | **MSTS used (confirmed):** float32 horizontal sample spacing used in coordinate calculations; MSTS validates its rounded value as a power of two. |
| `terrain_sample_fbuffer` (145) | Preserves filename; opens file | Used | **MSTS used (partial):** UTF-16 filename of an external `N*N` byte grid, expanded to `(N+1)^2`. MSTS bit `0x04` suppresses the underlying terrain triangles and sets patch summary flag `0x2`. Bits `0x01`/`0x02` set patch summary flag `0x4` and participate in adaptive-error hierarchy propagation; their individual meanings remain open. |
| `terrain_sample_ybuffer` (146) | Preserves filename; opens file | Essential | **MSTS used (confirmed):** UTF-16 filename of an external `N*N` little-endian uint16 elevation grid, expanded to `(N+1)^2` float values. |
| `terrain_sample_ebuffer` (147) | Preserves filename only | None | **MSTS-confirmed:** float32 geometric/adaptive error hierarchy used by recursive LOD selection. |
| `terrain_sample_nbuffer` (148) | Preserves filename only | None | **MSTS used (confirmed):** UTF-16 filename of an external `N*N` byte grid, expanded to `(N+1)^2`. Each byte indexes a 256-entry table of three-float normal vectors used while building lit terrain vertices. |
| `terrain_sample_cbuffer` (149) | Not parsed; omitted on save | None | **MSTS used (partial):** UTF-16 filename of an N-by-N ACE/image resource. MSTS accepts decoded type `0x0e` or `0x11`, packs three or five source planes into a 32-bit `(N+1)^2` grid, and includes channel residuals when generating E. Its broader rendering role is open. |
| `terrain_sample_dbuffer` (150) | Not parsed; omitted on save | None | **MSTS loaded, use partial:** UTF-16 filename of an external `N*N` byte grid, expanded to `(N+1)^2`. MSTS copies matching D bytes between adjacent tile edges during seam synchronization, together with F/Y/E/N/C. No interpretation beyond that propagation was found, and D does not occur in the two inspected corpora. |
| `terrain_sample_asbuffer` (281) | Preserves label and payload opaquely; does not use them | None | **MSTS-confirmed:** packed, LSB-first “always select” mask over the adaptive hierarchy; can force refinement after an E-based stop. Current TSRE no longer assumes 8,257 bytes while preserving the block, but it also does not validate the opaque payload against a changed `terrain_nsamples`. |
| `terrain_sample_usbuffer` (282) | Preserves label and payload opaquely; does not use them | None | **MSTS loaded/saved, no selector consumer found:** separate embedded `ceil((N+1)^2/8)` byte bitset at terrain `+0x48`. The recovered adaptive selector reads AS at `+0x44`, not US. “US” must therefore not be documented as a synonym for AS; the expansion of the name and its purpose remain unknown. |
| `terrain_water_height_offset` (251) | Preserves four floats | Used | SW, SE, NE, and NW water-surface corner offsets; TSRE bilinearly interpolates and edits the surface. |

Current TSRE preserves `terrain_sample_asbuffer` and
`terrain_sample_usbuffer` as variable-length opaque blocks. The recovered MSTS
loader expects `ceil((terrain_nsamples+1)^2/8)` payload bytes. TSRE must either
validate that relationship or discard/regenerate the blocks when changing
`terrain_nsamples`; bit-perfect preservation of a stale payload is not enough.

Current TSRE's terrain-grid work makes its Y/F loading and active renderer
dynamic for the layouts it accepts. That is a **TSRE capability**, not evidence
of equivalent MSTS support. MSTS later rejects `terrain_nsamples > 256` and
rejects any patch set for which
`terrain_nsamples / terrain_patchset_npatches > 16`. The custom 256/8-patch
fixture and the attempted 512/16-patch fixture fail these explicit MSTS guards;
see [`msts-custom-terrain-grid-compatibility.md`](msts-custom-terrain-grid-compatibility.md).
The user's `N=128`, 16 m spacing, `P=16` (`R=8`) fixture subsequently loaded
in MSRE. Static follow-up shows that merely bypassing the `R` guard is unsafe:
it protects a 17-by-17 shared vertex cache, a fallback mesh sized for
16-by-16, and four 1,536-index terrain batch thresholds. The bounded `R=32`
and `N=512` patch assessment was recorded in the earlier local
`msts-r32-n512-patch-feasibility.md` report, which is not retained here.

### Shaders

| Token | TSRE load/save | Runtime use in TSRE | MSTS load/use status and interpretation |
|---|---|---|---|
| `terrain_shaders` (151) | Preserved only under TSRE's assumed paired layout | Used indirectly | **MSTS loaded and used:** counted material table addressed by each patch's shader index. Ordinary detailed tiles use paired halves, while low-detail tiles use a flat table; details below. |
| `terrain_shader` (152) | Preserved | Name itself is not used to select a TSRE shader | **MSTS loaded and used:** UTF-16 shader name followed by `terrain_texslots` and `terrain_uvcalcs`; detailed renderer selection remains untraced. |
| `terrain_texslots` (153) | Preserved | First texture used; second depends on renderer path | **MSTS loaded and used:** uint32 count followed by `terrain_texslot` records. |
| `terrain_texslot` (154) | Filename and two integers preserved | Filename active; two integers have no clear TSRE consumer | **MSTS used, names open:** UTF-16 texture filename plus two int32 values. For each accepted slot, MSTS builds three distinct material-state nodes: state kind `0` from the second integer, kind `1` from the resolved texture resource, and kind `5` from the first integer. Both integers therefore affect material setup, although the public enum/semantic names of state kinds `0` and `5` remain unidentified. |
| `terrain_uvcalcs` (155) | Preserved | Mostly inactive in current renderer | **MSTS loaded:** uint32 count followed by `terrain_uvcalc` records; renderer semantics only partly traced. |
| `terrain_uvcalc` (156) | Four 32-bit values preserved | Fourth value can supply second-texture UV scale | **MSTS used, partial:** three int32 values followed by one float32. When a shader has at least two records, MSTS reads the second record's fourth float and multiplies the generated terrain UVs by it. TSRE incorrectly types this fourth field as int bits; default bits `1107296256` represent float `32.0`. No consumer of the first three integers was found in the recovered terrain renderer. |

`Mat` has fixed arrays for only two texture slots and two UV calculations. A
file declaring more can overrun those arrays. This is another parser limit.

#### Paired ordinary-tile shaders versus flat low-detail shaders

TSRE's split was not an arbitrary guess. MSRE function `0x00568076` creates a
logical terrain material as a `DetailTerrain` shader with two texture slots and
two UV calculations plus an `AlphaTerrain` shader with one of each. It inserts
the new detail record at the midpoint of `terrain_shaders` (151) and the alpha
record at the end, preserving this serialized order:

```text
DetailTerrain[0 .. M-1]
AlphaTerrain [0 .. M-1]
```

The editor also recognizes patch flag `0x00000200` as a patch temporarily
referring to the auxiliary half. Its normalization path changes that flag to
`0x00000100` and subtracts half the shader count from `ShaderIndex`; material
selection and removal paths contain matching pair-aware logic. This establishes
that the two halves are alternate records for one editable ordinary-terrain
material. The precise visual meaning of every `0x00000100`/`0x00000200` state
is not yet fully named.

The expanded six-route census confirms the convention for ordinary `Tiles`:
all 963 shader lists have even length; all 5,211 first-half entries are
`DetailTerrain` with two texture slots, and all 5,211 second-half entries are
`AlphaTerrain` with one texture slot. Of the 246,528 patch references, 246,521
address the first half. The remaining seven occur in USA2 tile `-01a18944.t`,
use flags `0x00000300`, and address an `AlphaTerrain` record in the second half.
Most corresponding pairs share their primary texture, but 14 do not, so a
loader must not use texture-name equality as a pairing requirement.

The 86 stock `Lo_tiles` files have a different representation. Their shader
names are `TexDiff`, list lengths range from one through nine, 42 lists have an
odd length, and patch indices address the entire serialized list. Dividing
these lists in half and folding every upper-half index is therefore data loss,
not MSRE-compatible normalization.

The robust TSRE representation is one authoritative flat vector in serialized
order plus a detected layout such as `PairedDetailAlpha` or `Flat`. A derived
pair view can preserve the existing material editor for ordinary tiles. Paired
mode should require an even list whose first half is `DetailTerrain` and second
half is `AlphaTerrain` (case-insensitively); `Lo_tiles` and unrecognized custom
layouts should remain flat. Save should retain the original mode unless the
user explicitly converts it.

### Patch grids and adjacent tile-owned records

| Token | TSRE load/save | Runtime use in TSRE | MSTS load/use status and interpretation |
|---|---|---|---|
| `terrain_patches` (157) | Container | Structural | Patch section. |
| `terrain_patchsets` (158) | Reads declared list | Limited | Collection of patch grids with an apparently intended distance field. No inspected stock tile contains more than one. |
| `terrain_patchset` (159) | Reads each, retains only one | Limited | One terrain patch grid. Multiple sets are lossy in TSRE; describing them as active MSTS LOD levels would be stronger than the executable evidence permits. |
| `terrain_patchset_distance` (160) | Preserves the four bytes but declares them as an integer | None | **MSTS loaded/saved as float32, no consumer found:** serializer independently confirms the type. The recovered draw path selects the last patch set by array position, not this distance, and no read of patch-set offset `+0x04` was found outside parse/save. TSRE and Open Rails currently type it incorrectly as int32. The token name suggests intended selection semantics, but that is not executable evidence for this build. |
| `terrain_patchset_npatches` (161) | Preserved integer | Heavily used | Number of patches per side; record count is its square. Stock MSTS parses and allocates `P*P` records dynamically, then computes `R=terrain_nsamples/P` and rejects the terrain during registration when the largest `R` exceeds 16. This is a samples-per-patch limit, not a requirement that `P` itself always equal 16. The user's `N=128`, `P=16` (`R=8`) fixture loads in MSRE. The executable's later mesh resources explain why changing only the guard to permit `R=32` would be unsafe. |
| `terrain_patchset_fbuffer` (162) | Not parsed; omitted on save | None | **MSTS loaded and used:** UTF-16 filename of an external `P*P` byte array, where `P=terrain_patchset_npatches`. Each byte initializes the corresponding patch flags word and overrides the flags value in the patch record. MSTS serialization writes `Flags & 0xcb` to this file. Absent from the expanded stock census. |
| `terrain_patchset_patches` (163) | Preserved list | Used | Container holding the fixed-layout patch records. |
| `terrain_patchset_patch` (164) | Preserved record | Partly used | Flags, bounds/statistics, material, texture mapping, and adaptive-LOD bias; detailed below. |
| `terrain_transfers` (165), `terrain_transfer` (166) | Not parsed; omitted on save | None | **MSTS loaded and used:** top-level counted list following `terrain_patches` (157). Each transfer contains one nested `terrain_shader` followed by four float32 values in `x0, z0, x1, z1` file order. MSTS reorders these into runtime X/Z bounds, maps the rectangle to affected patches, and builds rendered overlay geometry from the nested shader. The four values are rectangle endpoints; do not assume ascending min/max order without normalization. No occurrence in inspected corpora. |
| `terrain_shapes` (167), `terrain_shape` (168) | Not parsed; omitted on save | None | **MSTS loaded and used:** top-level counted list adjacent to, not nested in, `terrain_patches` (157). Each shape contains a UTF-16 shape filename, four int32 sample-grid bounds, and three float32 rotations. MSTS positions the shape at the bounds' midpoint, applies the rotations directly as radians, loads/transforms its geometry, marks/traverses affected patches, and includes the shape in terrain height, normal, and ray-intersection queries. This is a terrain query/collision surface; the recovered path does not establish that it is independently drawn. No occurrence in inspected corpora. |

The Microsoft `CoreIDs` source labels an adjacent older group of terrain IDs
as obsolete as early as August 1999. That history explains why the token table
contains more terrain vocabulary than common shipped tiles, but token names by
themselves do not establish layout or behavior. Executable analysis has now
established the layouts and concrete use of shapes, patch-set F, and terrain
transfers. D participates in cross-tile edge copying, although the meaning of
its byte values remains unknown.

Multiple patch sets and `Lo_tiles` should not be treated as equivalent. Every
patch set belongs to one `.t` object and shares that tile's sample buffers,
sample spacing, and physical extent. It may have been intended to provide
near/mid-distance patch grouping or material metadata for the same terrain,
whereas `Lo_tiles` provide separate heightmaps covering much larger geographic
areas. One plausible history is that distance-selected patch sets were an
earlier or abandoned design and E/AS refinement plus separate `Lo_tiles`
became the shipped solution. That is a format-history hypothesis, not a
confirmed MSTS behavior: `terrain_patchset_distance` (160) has no recovered
consumer and the final draw path selects the last patch set by array position.

TSRE could deliberately define useful distance-selection semantics for
multiple sets, but the records do not contain independent heightmaps or
prebuilt meshes. The renderer would still need to define how a selected set
reduces geometry, use transition hysteresis, and retain a sensible last set as
the fallback for MSTS, which ignores the distances in the analyzed build.

## Implementation schema for TSRE gaps

Sizes below describe each token's payload and exclude the common binary SIMISA
block wrapper: uint32 token ID, uint32 block length, and one label byte. A
UTF-16 string is `uint16 character_count` followed by exactly `count*2`
little-endian bytes; `L` below is that character count.

| Token / current problem | Confirmed payload and external data | Recommended implementation |
|---|---|---|
| `terrain_sample_ebuffer` (147), filename preserved but data not loaded | T payload: UTF-16 filename, `2+2L` bytes. External raw file: exactly `N*N` little-endian float32 values (`4*N*N` bytes); MSTS edge-expands it to `(N+1)^2` and consumes it as the adaptive-error hierarchy. | Retain the filename for minimum compatibility. For adaptive LOD, load into a dynamic float vector sized `(N+1)^2`, reject short/long files, and reproduce MSTS edge expansion. Do not rewrite the external file unless TSRE can regenerate valid errors. |
| `terrain_sample_nbuffer` (148), filename preserved but data not loaded | T payload: UTF-16 filename, `2+2L`. External raw file: exactly `N*N` bytes; MSTS edge-expands it to `(N+1)^2`. Each byte is an index into MSTS's 256-entry normal-vector table. | Retain and emit the filename. Full support should use a dynamic byte vector sized `(N+1)^2` and preserve the encoded indices. If TSRE chooses to consume them, decode through a compatible 256-entry normal table for vertex lighting; do not silently replace the external data merely because TSRE can recompute normals. |
| `terrain_sample_cbuffer` (149), dropped | T payload: UTF-16 filename, `2+2L` bytes. Referenced ACE/image must decode to exactly `N*N`; MSTS accepts decoded types `0x0e`/`0x11` and builds `(N+1)^2` packed uint32 samples. | Minimum lossless support: retain and emit the filename token unchanged. Full support: use the ACE decoder, validate N-by-N/power-of-two input, preserve packed channels, and do not regenerate C until its writer semantics are known. |
| `terrain_sample_dbuffer` (150), dropped | T payload: UTF-16 filename, `2+2L`. External raw file: exactly `N*N` bytes; MSTS edge-expands it to `(N+1)^2` and copies values across matching adjacent-tile edges. | Store filename as an optional string and preserve the external file. A future consumer should use a byte vector sized `(N+1)^2` and include D in edge synchronization, but keep individual byte meanings opaque. |
| `terrain_sample_asbuffer` (281), fixed TSRE size | Embedded byte array with no internal count: `ceil((N+1)^2/8)` bytes. | Replace the fixed allocation with `std::vector<uint8_t>` sized from `terrain_nsamples`; validate the enclosing block length; preserve unused tail bits. |
| `terrain_sample_usbuffer` (282), dropped | Embedded byte array with no internal count: `ceil((N+1)^2/8)` bytes. | Add a separate dynamic byte vector, using the same size validation as AS. Preserve even while no TSRE consumer exists. |
| `terrain_patchsets` (158), collapsed | uint32 count followed by that many `terrain_patchset` child blocks. All sets share the containing tile's sample buffers and physical extent. | Replace singleton patch-set fields with `std::vector<PatchSet>` and keep order. Rendering may select one set, but saving must emit all sets. Any distance-based TSRE renderer behavior is a new policy, not recovered behavior of this MSTS build. |
| `terrain_patchset_distance` (160), wrong TSRE type | One float32, 4 bytes. | Change the model field to float. During migration, bit-copy the old stored uint32 rather than numerically converting it. |
| `terrain_patchset_fbuffer` (162), dropped | T payload: UTF-16 filename, `2+2L`. External raw file: exactly `P*P` bytes. Each byte supplies a patch flag value; MSTS save masks it with `0xcb`. | Store optional filename plus `P*P` bytes. Load before patch records and record that it overrides record flags. Preserve original bytes unless flags are edited; if regenerating, reproduce MSTS's `Flags & 0xcb` rule. |
| `terrain_patchset_patches` (163) / `terrain_patchset_patch` (164), weakly typed in TSRE | The container holds exactly `P*P` child records. Each record payload is 60 bytes: one uint32 `Flags`, six float32 bounds/statistics values, one uint32 `ShaderIndex`, and seven float32 mapping/error values. Including the normal 8-byte block header and 1-byte label gives 69 serialized bytes per child. MSTS expands this to a 76-byte runtime record. | Replace the anonymous float array with a typed `Patch` structure. Keep `Flags` and `ShaderIndex` as uint32 and every other field as float32; validate both the declared block length and `P*P` record count. Preserve unknown flag bits on edits. |
| `terrain_shaders` (151), unconditional TSRE half-split | uint32 count followed by that many `terrain_shader` (152) blocks in index order. Ordinary `Tiles` use equal `DetailTerrain` and `AlphaTerrain` halves; `Lo_tiles` use a flat, sometimes odd-sized `TexDiff` list. | Keep an authoritative `std::vector<Mat>` in serialized order plus an explicit detected layout. Expose a derived pair view only for a validated `DetailTerrain`/`AlphaTerrain` layout. Preserve flat and unknown layouts and their exact patch indices; do not infer pairs from texture-name equality. |
| `terrain_texslots` (153), fixed `[2]` | uint32 count plus child blocks. Each `terrain_texslot` (154) payload is a UTF-16 filename plus two int32 values: `2+2L+8` bytes. MSTS feeds the first and second integers into distinct material-state kinds `5` and `0`. | Use a vector with the declared count. Preserve both integers as signed 32-bit values. Until the state enums are named, expose them only as explicitly unknown MSTS material-state values rather than inventing sampler labels. |
| `terrain_uvcalcs` (155), fixed `[2]` and wrong fourth type | uint32 count plus child blocks. Each `terrain_uvcalc` (156) is three int32 plus one float32: 16 bytes. MSTS uses the second record's float as a UV multiplier when present. | Use a vector of `{int32 a,b,c; float d;}`. Serialize `d` as float32, not an integer conversion. Preserve all records and integers even though only the second `d` consumer is currently known. |
| `terrain_transfers` (165), dropped | uint32 count plus `terrain_transfer` (166) blocks. Each transfer contains a complete nested `terrain_shader` (152), then four float32 values (16 bytes after the shader block) in `x0, z0, x1, z1` order. | Implement `vector<TerrainTransfer>` with a shader object and four named rectangle endpoints. Normalize only in derived calculations; preserve original order and float bits for save. Rendering support should find intersected patches and build overlay geometry with the nested shader. |
| `terrain_shapes` (167), dropped | uint32 count plus `terrain_shape` (168) blocks. Each shape is a UTF-16 filename, four int32 sample-grid bounds, then three float32 rotations: `2+2L+28` bytes. The rotations are consumed as radians. | Implement `vector<TerrainShape>` containing filename, four bounds, and three rotations. Loading the referenced shape is optional for lossless editing; retaining and re-emitting the record is mandatory. A full consumer should position it at the bounds' midpoint and include its geometry in terrain height/normal/intersection queries; do not assume a separately visible world object. |
| Any unknown future child | Length is supplied by the enclosing SIMISA block. | Keep an `OpaqueBlock {token, label, payload, original_order}` alongside typed children. Re-emit it byte-for-byte. This prevents data loss while semantic work is incomplete. |

As a recommended future design, the same lossless-opaque mechanism should
retain known-but-unedited blocks too. TSRE does not currently do this
universally. Typed parsing can then be added incrementally without making save
fidelity depend on complete reverse engineering.

The sizes in this table are serialized sizes, not C++ `sizeof` values. Do not
map these payloads directly onto native structs: compiler padding, alignment,
endianness, and the variable-length UTF-16 strings make that unsafe.

## `terrain_patchset_patch` record

TSRE stores the 13 values after flags in an anonymous `float tdata[]`; the
shader integer is converted to float. Open Rails supplies the field names used
below. MSTS's parser independently confirms the order and loads every value
into a 76-byte runtime record.

| File order | Field name | TSRE behavior | MSTS status, meaning, and confidence |
|---:|---|---|---|
| 0 | `Flags` | Interprets bit `0x00000001` as do-not-draw and bits `0x000000c0` as water; preserves other bits | **MSTS used, partial:** bit `0x1` disables drawing; bit `0x2` summarizes F samples containing hole bit `0x04` and selects the hole-aware mesh builder; bit `0x4` summarizes F low bits; bit `0x8` gates terrain-shape substitution/collision. The external patch F writer preserves only `Flags & 0xcb`. Bits `0x10`/`0x20` and `0x100`/`0x200` are runtime cache/rebuild state and are excluded from that external mask. Water bits `0x40`/`0x80` and serialized high bits `0x01000000`/`0x02000000` are still not individually decoded in MSTS. |
| 1 | `CenterX` | Loaded/saved, otherwise ignored | **MSTS used (confirmed):** patch center X in placement, view culling, and bounds. |
| 2 | `AverageY` | Loaded/saved, otherwise ignored | **MSTS used (confirmed):** vertical center/representative height. MSTS uses `AverageY - RangeY` and `AverageY + RangeY` as the patch's vertical bounds and also derives tile-level bounds from it. |
| 3 | `CenterZ` | Loaded/saved, otherwise ignored | **MSTS used (confirmed):** patch center Z in placement, view culling, and bounds. Standard grids progress in the negative-Z direction. |
| 4 | `FactorY` | Loaded/saved, otherwise ignored | **MSTS used (confirmed):** bounding-sphere radius passed with the three-dimensional patch center to the frustum plane/sphere test. Flat stock patches use about `99.48125458`; relief increases the conservative radius. |
| 5 | `RangeY` | Loaded/saved, otherwise ignored | **MSTS used (confirmed):** vertical half-extent used in `AverageY ± RangeY` patch bounds. |
| 6 | `RadiusM` | Loaded/saved, otherwise ignored | **MSTS used (confirmed):** horizontal X/Z half-extent used in `CenterX/CenterZ ± RadiusM` patch bounds. It is 64 in all 40,192 inspected records. |
| 7 | `ShaderIndex` | Actively selects material | **MSTS used (confirmed):** exact index into `terrain_shaders` (151). In ordinary paired tiles, MSRE has explicit `0x00000200`-flagged auxiliary-half handling and can normalize that reference to the corresponding first-half index. TSRE performs the fold unconditionally, which is valid only for that paired layout and corrupts flat `Lo_tiles` indices. |
| 8 | `X` | Actively used/edited | **MSTS used (confirmed layout; renderer path partial):** U translation in the patch UV affine transform. |
| 9 | `Y` | Actively used/edited | **MSTS used (confirmed layout; renderer path partial):** V translation in the patch UV affine transform. |
| 10 | `W` | Actively used/edited | **MSTS used (confirmed layout; renderer path partial):** U coefficient for first local patch coordinate. |
| 11 | `B` | Actively used/edited | **MSTS used (confirmed layout; renderer path partial):** U coefficient for second local patch coordinate. |
| 12 | `C` | Actively used/edited | **MSTS used (confirmed layout; renderer path partial):** V coefficient for first local patch coordinate. |
| 13 | `H` | Actively used/edited | **MSTS used (confirmed layout; renderer path partial):** V coefficient for second local patch coordinate. |
| 14 | `ErrorBias` | Exposed by editor, preserved, and reset to zero by terrain-height edits; not used by TSRE rendering | **MSTS-confirmed:** continuous multiplier in the adaptive-LOD threshold. Zero is not a special bypass in the recovered executable path. |

The texture transform is:

```text
U = X + local_u * W + local_v * B
V = Y + local_u * C + local_v * H
```

TSRE uses these six fields for vertex UVs and implements rotation, mirroring,
scaling, and cropping by modifying them. This part of the record is not unused.

The recovered MSTS consumers remove the earlier uncertainty around the three
height-related fields. Patch bounds are:

```text
min = (CenterX - RadiusM, AverageY - RangeY, CenterZ - RadiusM)
max = (CenterX + RadiusM, AverageY + RangeY, CenterZ + RadiusM)
```

`FactorY` is separately supplied as the radius to a plane/sphere frustum test.
These meanings are MSTS-executable results; TSRE still loads and saves the
values without using them in its renderer.

### Second-pass MSTS executable landmarks

These are function entry virtual addresses in the analyzed patch-v1.4
`train.exe`, not source-level names:

| Recovered behavior | Function VA(s) |
|---|---:|
| `terrain_alwaysselect_maxdist` (138) parse/cache and terrain serialization | `0x006ee290`, `0x006f3320` |
| `asnear`/`asmax` option parsing and squared manager initialization | `0x00499bdd`, `0x004937bf`, `0x00494850` |
| route `TerrainErrorScale` (1230; MSTS application token `0x403a2`) parse/save | `0x004ae6db`, `0x004afd48` |
| route/user terrain-threshold composition and render handoff | `0x005310e1`, `0x004902e2`, `0x0053314d`, `0x006bf450` |
| recursive E/AS selection | `0x006cd4e0` |
| N-index terrain lighting | `0x006f19f0`, `0x006f1ee0` |
| F-to-patch summaries and F-hole mesh construction | `0x006f1130`, `0x006f1810` |
| F low-bit participation in E propagation | `0x006d1820` |
| F/Y/E/N/C/D adjacent-edge copying | `0x006ef2d0`, `0x006ef5a0` |
| patch AABB and sphere/frustum culling | `0x0070bc60`, `0x006cda00`, `0x00697150` |
| transfer parse, relocation, and overlay construction | `0x006db6a0`, `0x006c2bc0`, `0x006db840` |
| terrain-shape list/record parsing and terrain-query integration | `0x006eed90`, `0x0070eec0`, `0x0070af70`, `0x0070b4f0` |
| MSRE paired shader creation, selection/removal, and normalization | `0x00568076`, `0x0056afaf`, `0x00569bce`, `0x00573b82`, `0x007109b0` |
| texture-slot material-state chain | `0x006ed8c0`, `0x006ffe10` |
| second UV-calc float consumption | `0x006f1560`, `0x006f19f0`, `0x006f1ee0` |
| patch-set distance parse/save and normal draw selection | `0x00710630`, `0x007109b0`, `0x006cda00` |

The decompiler evidence is retained in
`analysis/pe/ghidra-terrain-second-pass.txt` and
`analysis/pe/ghidra-terrain-control-offset-scan.txt`; the corresponding
reproducible scripts are under `scripts/ghidra/`.

## Stock-file observations

Presence by file, not occurrence count:

| Token | Tutorial (36 files) | Europe1 (121 files) |
|---|---:|---:|
| `terrain_sample_ebuffer` (147) | 36 | 120 |
| `terrain_sample_nbuffer` (148) | 36 | 121 |
| `terrain_sample_fbuffer` (145) | 1 | 16 |
| `terrain_sample_asbuffer` (281) | 19 | 70 |
| `terrain_sample_usbuffer` (282) | 8 | 61 |
| `terrain_sample_cbuffer` (149) | 0 | 0 |
| `terrain_sample_dbuffer` (150) | 0 | 0 |
| `terrain_patchset_fbuffer` (162) | 0 | 0 |

Patch values in the larger Europe1 corpus:

- `RadiusM`: always 64.
- `AverageY`: about 19 through 719.
- `RangeY`: 0 through about 49.645.
- `FactorY`: about 99.481 through 113.437.
- `ErrorBias`: 30,966 records at 1 and 10 records at 0.
- `Flags`: 27,961 at zero; other observed values were `0x00000100`,
  `0x010000c0`, `0x020000c0`, `0x020000c2`, and `0x00000002`.

These frequencies establish that AS/US and non-zero flag bits are real shipping
data, not merely dead vocabulary. The second static pass explains `0x2` as the
patch summary for F holes and identifies `0x100` as runtime mesh/rebuild state;
it does not yet separate water bits `0x40`/`0x80` or explain the two high bits.

## Save and compatibility hazards

The important consequences for editing workflows are:

1. TSRE only serializes the token subset it understands. Unsupported blocks do
   not survive a save.
2. Opaque `terrain_sample_asbuffer` and `terrain_sample_usbuffer` payloads are
   preserved without checking their size against a changed
   `terrain_nsamples`; resizing a tile can therefore preserve a stale bitset.
3. Multiple patch sets collapse into one, and the saved file always contains
   one set.
4. Shader storage assumes at most two `terrain_texslot` and two
   `terrain_uvcalc` records. It also assumes every shader table consists of
   paired normal/auxiliary halves, which is false for stock `Lo_tiles`.
5. Although allocation often uses `terrain_nsamples` and
   `terrain_patchset_npatches`, TSRE's accepted-layout range is wider than
   legacy MSTS's explicit end-to-end limits (`N <= 256` and `N/P <= 16`).
6. Height editing sets affected patch `ErrorBias` to zero. In MSTS this changes
   adaptive selection behavior; it is not just an editor “dirty” marker.

For forensic work, preserve the original `.t` and compare the complete binary
block tree after every TSRE save. The supplied inspector is read-only and is a
safer first pass than opening a rare-format tile in the editor.

## Conclusions and open questions

TSRE's apparent “unused patch values” split into four groups:

- values TSRE genuinely needs: flags, shader index, X/Y/W/B/C/H UV transform,
  water offsets, Y heights, and F holes;
- MSTS runtime data TSRE preserves but does not consume: E, N, AS,
  `terrain_errthreshold_scale` (137), patch bounds/radii, and `ErrorBias`;
- values MSTS parses and serializes but for which this executable review found
  no downstream consumer: `terrain_alwaysselect_maxdist` (138),
  `terrain_patchset_distance` (160), sample rotation, US, and most UV-calc
  values. This is stronger than “not yet written down,” but is still scoped to
  the recovered paths rather than a proof about every possible MSTS tool;
- MSTS-supported data TSRE currently drops: C, D, US, patch-set F, transfers,
  and shapes. These are not all dead or merely historical: MSTS use of C,
  D edge propagation, patch-set F, transfers, and shapes is statically
  confirmed.

Some missing MSTS entries in earlier versions of this report were simply not
written down; others are genuinely unresolved. The tables now distinguish
confirmed use, loaded-with-consumer-open, probable meaning, and unknown meaning.
The second pass resolved several entries which had merely been missing from the
earlier write-up: N is a normal-index grid; `AverageY`, `RangeY`, `RadiusM`, and
`FactorY` are active patch bounds/culling inputs; F bit `0x04` drives terrain
holes; D is synchronized across tile edges; both texture-slot integers enter
MSTS material state; the second UV-calc float scales UVs; and all four transfer
floats define the rendered overlay rectangle. Route `TerrainErrorScale` (1230)
is also active: MSTS loads/saves it as float32 and computes
`clamp(trterrain_errthreshold * TerrainErrorScale, 7, 50)` before the value is
passed into the camera-scaled adaptive-LOD threshold. The named
`trterrain_errthreshold` setting defaults to 7 and has command-line alias
`errthresh`. The pass also found negative evidence: the recovered draw path
ignores patch-set distance and the selector reads AS, not US.

The genuinely open static targets are the separate meanings of F bits
`0x01`/`0x02`; D and US semantics; sample rotation; C's role beyond E
generation; water flags `0x40`/`0x80` and serialized high patch bits; public
names for the two texture-slot state values and three UV-calc integers; the
public meaning of the camera field which divides the terrain threshold; and
Route Editor writer formulas.

## Narrow runtime fixtures for rare tile-owned objects

The static consumers justify two controlled runtime tests, but not a general
capture session. Both fixtures should start from separate copies of one flat,
ordinary `terrain_nsamples` (140) = 256,
`terrain_patchset_npatches` (161) = 16 tile. This avoids mixing the rare
records with custom-dimension executable patches. Inspect and screenshot the
first load before allowing MSRE to save; afterwards, binary-diff the complete
`.t` block tree and referenced files. Neither fixture should contain the
similarly named W-file object in its observation area.

### `terrain_transfer` (166) overlay fixture

Add one top-level `terrain_transfers` (165) list after `terrain_patches` (157)
inside `terrain` (136). It is a sibling of `terrain_patches` (157), not a child
of its `terrain_patchsets` (158) list. Its sole `terrain_transfer` (166)
contains a complete, nested `terrain_shader` (152), followed directly by four
float32 values in `x0, z0, x1, z1` order. It does not contain a `ShaderIndex`
into the tile's global `terrain_shaders` (151) table.

For an ordinary 2,048 m tile, use a central 256-by-256 m rectangle:

```text
x0 = 896.0
z0 = -896.0
x1 = 1152.0
z1 = -1152.0
```

Clone a known-good `AlphaTerrain` `terrain_shader` (152) from the same tile,
including its `terrain_texslots` (153) and `terrain_uvcalcs` (155), and change
only its primary texture to a conspicuous checkerboard ACE with opaque and
transparent regions. An opaque known-good texture is the first control if
alpha behavior introduces another variable. Expected evidence is a rectangular
overlay following the existing terrain surface, rather than a separately
placed W-file `Transfer`. A second copy moved 256 m in X should move only the
overlay and will verify the endpoint convention. MSRE save behavior should be
checked separately from rendering behavior.

### `terrain_shape` (168) query-surface fixture

Add one top-level `terrain_shapes` (167) list after `terrain_transfers` (165),
or directly after `terrain_patches` (157) when the transfer list is absent. It
is also a child of `terrain` (136), not of a patch set. Its sole
`terrain_shape` (168) should reference a simple route shape and use these four
central sample-grid bounds and zero rotations:

```text
"terrain_probe.s"
120 120 136 136
0.0 0.0 0.0
```

At 8 m per sample, those bounds cover a central 128 m region and place the
shape at sample `(128,128)`. `terrain_probe.s` should be a conventional closed
box or shallow ramp centred at its local origin, with a footprint smaller than
the bounds and a top surface approximately 25–40 m above the flat RAW terrain.
Do not also add it as a W-file shape.

Visibility is not the primary pass condition. Put a separate marker object or
short track section above the centre and use MSRE's terrain-apply (`Y`) command.
The baseline fixture should snap it to the RAW terrain; the shape fixture should
snap it to the probe shape's top if MSRE calls the recovered terrain query
path. A follow-up with one rotation changed to `0.174533` radians should create
an approximately 10-degree slope and can establish the otherwise unnamed axis
order. This test can therefore distinguish a tile-owned terrain collision or
height-query surface from an independently rendered scenery object.

The binary fixtures can be generated and structurally verified in WSL without
running MSTS. Any Windows filesystem access, execution, instrumentation, or
automated capture remains a separate step requiring explicit approval; none
was performed while preparing this design.

## Second static review and Windows-capture decision

The current result does **not** justify a broad Windows capture session. Static
analysis has resolved enough of the high-value fields to restrict any runtime
work to the two narrow fixtures and observation points defined above.

| Remaining question | What static analysis now says | Would Windows help now? |
|---|---|---|
| `terrain_alwaysselect_maxdist` (138) | Parsed as float, square cached, original serialized; no consumer of tile `+0x34/+0x38` found. Selector manager `+0x4c/+0x50` receives `asnear²`/`asmax²`, default `350²`/`700²`, directly during application setup. | No capture recommended for semantic discovery: the static data flow shows that this token is not the selector setting in this build. A watchpoint would only validate the negative result. |
| Route `TerrainErrorScale` (1230) | Float load/save and full runtime handoff are confirmed. It multiplies `trterrain_errthreshold`, defaults effectively to 7 when the route scale is 1, is clamped to 7–50, and then enters the camera-scaled LOD factor. | No capture is needed to establish that it is active. Runtime comparison would only quantify visual impact or help name the remaining camera divisor. |
| `terrain_patchset_distance` (160) | Float type is confirmed; no consumer of patch-set `+0x04`; normal draw selects the last set by position. | Only with a valid multi-patch-set tile. None is currently identified, so capture is premature. |
| F low bits, water/high patch flags | F low bits affect hierarchy propagation and patch summary `0x4`; remaining flag branches are not separated. | Useful only after finding or generating a tile where one bit can be changed independently. |
| D and US | D edge copying is confirmed; US is separate from AS and has no recovered selector consumer. | Low value without a real D/US fixture and data watchpoints. Visual testing alone is unlikely to reveal meaning. |
| `terrain_transfers` (165) / `terrain_transfer` (166) | Parser, rectangle-to-patch conversion, and overlay-mesh construction are confirmed; no stock record was found. | The central checkerboard fixture above can confirm that the retained feature renders and establish texture/alpha behavior. API capture is unnecessary for the first test. |
| `terrain_shapes` (167) / `terrain_shape` (168) | Shape loading, bounds-centre placement, rotations, patch marking, and terrain-query participation are confirmed; independent visual rendering is not. | The elevated box/ramp plus `Y`-snap fixture above can distinguish a query/collision surface from visible scenery and determine rotation axes. |
| Texture-slot integers / UV-calc integers | Both texture integers enter distinct material states; only the second UV record's float has a confirmed terrain-mesh consumer. | Potentially useful after the basic transfer fixture succeeds, by changing one value at a time. GPU/API state capture is a later option, not a prerequisite. |
| Route Editor writer formulas | Runtime readers cannot establish how the editor derives E, N, patch bounds, or rare flags during authoring. | This is the strongest eventual Windows case: make one controlled edit, save to a copied route, and diff all outputs. It still needs a prepared disposable test copy and an exact manual script. |

Accordingly, the next action can remain Linux-side fixture construction and
binary validation. If Windows is later approved, run one fixture with its exact
manual observation script rather than a general gameplay or
`terrain_alwaysselect_maxdist` (138) capture. No Windows filesystem, registry,
process, or executable was accessed for this review.
