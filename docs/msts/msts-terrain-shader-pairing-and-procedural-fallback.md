# MSTS shader pairs and a procedural-terrain fallback design

Date: 2026-09-05
Status: static review and design suggestions; no procedural implementation or
Windows tests performed

Mirror synchronized: 2026-09-08. Native MSTS findings are retained below; the
procedural design sections are historical proposals, not the current TSRE
implementation status. See [procedural materials](../tasks/terrain/terrain-procedural-materials.md)
and [baked fallback](../tasks/terrain/terrain-procedural-baked-fallback.md) for the
implemented TSRE workflow. Evidence paths refer to the separate MSTS workspace.

## Compatibility requirement clarified by the author

TSRE is the writer of the feature-rich format. Existing MSTS/MSRE and ORTS
are read-only consumers of the legacy fallback; preserving procedural data
through an MSRE save is not a requirement. Procedural rendering in ORTS would
necessarily require ORTS changes. The preferred design reuses the existing
material array, keeping legacy-recognized shader names and adding only the
procedural properties and map reference that are needed. A separate stable-ID
palette is optional, not required to defend against an out-of-scope MSRE save.

The author's subsequent target is more specific: one baked ACE atlas per tile
(for example 1024-by-1024), with ordinary patch shader references and different
patch UVs selecting regions of it. Legacy consumers and distant TSRE patches
use this atlas; near TSRE patches use an additional per-patch
`proceduralShaderId` reference. This is a proposed property name, not an
existing MSTS token or an assigned numeric ID. The author wants a flat list of
single-texture materials, not the stock detail/alpha material-pair semantics.
The review below distinguishes that desired representation from what an
unmodified MSTS ordinary-tile loader actually accepts.

## What the two shader halves actually do

The existing stock census and MSRE material-creation analysis establish this
ordinary detailed-tile layout in `terrain_shaders` (151):

```text
DetailTerrain[0 .. M-1]   two texture slots, two UV calculations
AlphaTerrain [0 .. M-1]   one texture slot, one UV calculation
```

The runtime also actively switches between the halves. Draw routine
`0x006cda00` compares the patch center's squared 3D camera distance with the
float at `0x007a9900`, provided its terrain-manager flag `0x02` is set. The
executable's initial threshold is `2560000.0`, corresponding to 1600 distance
units. This is the image's initial value, not a capture of an effective runtime
setting. The switch does not use `terrain_patchset_distance` (160).

This does **not** select another patch set. Normal drawing still takes the
last `terrain_patchset` (159); near/far material selection changes the shader
index of a patch in that same set. The patch-set array and shader array are
independent collections.

Submission routine `0x006f11d0` implements the selected path:

| Path | Material-index operation |
|---|---|
| Normal/near, or alternate path globally disabled | If patch flag `0x00000200` is set, clear it, set `0x00000100`, and subtract half the shader count. |
| Farther-distance alternate path enabled | If `0x00000200` is clear, set `0x00000300` and add half the shader count. |

The global enable is at `0x007a98f8` (initial value 1; setter
`0x006c4a80`). Thus the second half supplies an actively used distance-dependent
material alternative. It is not an unused extension area or a general
"unknown shader, try another shader" fallback mechanism. Full graphics-state
semantics of `DetailTerrain` and `AlphaTerrain` remain only partly traced.

Load routine `0x006f0b50` also folds upper-half patch indices and validates the
result against the half-count when the manager's paired-mode flag is enabled.
Save routine `0x007109b0` normalizes temporary auxiliary-half references before
writing them. These are MSTS behaviors, separate from TSRE's own normalization.

Stock `Lo_tiles` instead contain a flat `TexDiff` list, including odd list
lengths, as documented in the [field-usage report](tsre-msts-terrain-tfile-field-usage.md#paired-ordinary-tile-shaders-versus-flat-low-detail-shaders).
Do not generalize half-splitting to every `terrain_shaders` (151) table, or
assume a custom flat table placed in an ordinary detailed-tile context will
automatically disable MSTS's paired-mode behavior.

The complete functions `0x006cda00`, `0x006f11d0`, `0x006f0b50`,
`0x007109b0`, `0x006ed310`, and `0x006b4180` were compared byte for byte
between the local Microsoft 1.4 and normal Bin 1.8.052113 copies and match.
Input hashes are recorded in the [multiple-set report](msts-orts-multiple-patchsets-and-water.md#inputs-and-verification).
Draw/submission evidence is already retained in
`analysis/pe/ghidra-patchset-selection-review.txt`; shader parsing and saving
are in `analysis/pe/ghidra-terrain-second-pass.txt`. Additional bounded
decompilation used `ExportFunctionsByAddress.java` for the addresses above.

## Who chooses paired versus flat indexing: MSTS and MSRE follow-up

The choice is made by the **terrain manager's construction mode**, before any
tile shader list is read. It is not inferred from `terrain_shader` (152)
names, texture-slot count, or whether `terrain_shaders` (151) has an even
length. The relevant flag is manager `Flags & 0x02`, not a patch flag.

| Context | Static setup evidence | Shader indexing |
|---|---|---|
| Ordinary route `Tiles` | Setup calls at `0x0049395a` and `0x00494b19` pass `EDX=1` to constructor `0x006bd870`; the resulting manager is stored at `0x007c2e08` and configured with `tiles` paths. | Paired: constructor sets manager bit `0x02`. |
| Distant `Lo_tiles` | Initializer `0x00521f30`, call at `0x00522060`, passes `EDX=0`; the separate manager at `0x007c2c14` is configured with `lo_tiles` paths. | Flat: constructor leaves manager bit `0x02` clear. |
| Route Editor | Editor entry at `0x00481657` sets `0x007be0f8=1`. Distant initialization tests this at `0x00521fdc` and takes the early-return path before creating the distant manager. | Ordinary manager remains paired; the inspected MSRE material tools also assume pairs. |

Constructor `0x006bd870` sets bit `0x02` whenever its second argument is
nonzero. It separately probes the globally registered `DetailTerrain` shader
to decide manager bit `0x01`; that probe is not inspection of a tile's shader
names and does not make bit `0x02` conditional on its contents.

The editor early-return path also calls `0x006c4a80(0)`, disabling the runtime
alternate-material path globally. That does **not** clear manager bit `0x02`
or disable paired index validation while loading. This separates MSRE's
behavior from the enabled farther-distance material switch in driving mode.

No file-controlled override was found in the inspected constructor, setup,
tile/shader parsing, or index-selection paths. Merely writing `TexDiff`
instead of `DetailTerrain` does not select the distant manager. Copying a
flat-list `.t` into ordinary `Tiles` does not reproduce `Lo_tiles` behavior.

### A one-record list fails even before drawing

In paired mode, `0x006f0b50` implements this index normalization/validation
after reading the patch's ordinary `ShaderIndex`:

```text
half = shaderCount >> 1
if ShaderIndex >= half:
    ShaderIndex -= half
if ShaderIndex >= half:
    fail patch loading
```

For one shader and index zero, `half=0`, subtraction changes nothing, and
`0 >= 0` fails the load. This is a static prediction from the actual loader,
not a runtime test performed here. With longer flat lists, upper-half indices
can silently fold to different materials instead. An odd count is not itself
an unconditional rejection; the count and referenced indices determine the
outcome.

### MSRE's material UI is pair-oriented

The reviewed editor helpers do not branch into a flat-list editing mode:

- Creation `0x00568076` constructs `DetailTerrain`/`AlphaTerrain`, inserts at
  the midpoint/end, and increases the count by two.
- Selection/application `0x0056afaf` searches only `shaderCount >> 1` entries.
- Removal `0x00569bce` iterates the ordinary manager's tiles, removes matching
  records from both halves, and reduces the count by two.
- Mapping helper `0x00573b82` also searches the first half and normalizes
  auxiliary references.

Thus the native renderer supporting flat distant materials does not establish
that MSRE can edit arbitrary flat material lists. Editing remains outside the
author's requirement, but the paired **load** check still applies to read-only
MSRE viewing of ordinary tiles.

### One atlas file does not require two texture files

The proposed atlas fallback itself fits the ordinary patch UV representation.
For unmodified MSTS compatibility, a candidate minimal adapter is two valid
single-texture material records which both reference the same atlas:

```text
index 0: TexDiff -> tile-atlas.ace
index 1: TexDiff -> tile-atlas.ace
each patch: ordinary ShaderIndex = 0; its own atlas UV transform
```

Paired loading now has `half=1`; the normal and alternate paths would select
equivalent atlas materials. This is inferred from the load/index/render paths,
not tested graphics compatibility for this fixture. It adds only a second
material record and filename reference, not a second ACE file. It does not
require the two different stock `DetailTerrain` and `AlphaTerrain` recipes.

If procedural materials share this array and every legacy patch still points
to index zero, the corresponding fallback copy must be at
`floor(totalShaderCount/2)`, not necessarily index one. A six-entry example is:

```text
0: atlas fallback       3: atlas fallback copy
1: procedural source A  4: procedural source C
2: procedural source B  5: procedural source D
```

For this example, legacy patch indices are exclusively zero, so only records
zero and three are selected as their materials. All other records must still
load successfully, but the traced read-only paths do not require each unused
procedural record to have a meaningful counterpart. This narrower adapter
relaxes the earlier blanket recommendation to duplicate every material. It
still needs a runtime test and is not an MSRE-editable flat-list guarantee.
TSRE must maintain the midpoint fallback placement and procedural references
when changing the array. A genuinely flat native ordinary-tile mode would
require executable changes; no such patch was made or fully designed here.

Recommended future fixtures are: a valid ordinary tile with one `TexDiff`
atlas record (expected rejection), the two-record same-atlas version, and the
six-record version with visually distinct procedural sources. Check MSRE
loading and driving-mode near/far rendering separately. Windows access and
execution require explicit approval; none occurred in this review.

### Verification and artifacts

The complete constructor, distant initializer, four editor helpers, and patch
parser above match byte for byte between the local Microsoft 1.4 and normal
Bin 1.8.052113 copies. Ordinary setup function `0x004937bf` also matches in
full. Function `0x00494850` has unrelated differences between builds, so only
its relevant setup region `0x00494b0f..0x00494b91` is claimed identical.
The editor-entry region `0x00481657..0x0048167f` also matches.

New read-only Ghidra exports:

- `analysis/pe/ghidra-terrain-manager-creation-review.txt`, generated with
  `ExportFunctionsReferencingAddress.java` for `0x006bd870`.
- `analysis/pe/ghidra-shader-layout-editor-review.txt`, generated with
  `ExportFunctionsByAddress.java` for the constructor and editor helpers.

The decompiler omits some fastcall arguments at call sites. The `EDX` values,
editor early-return condition, and loader comparisons above were additionally
checked directly with Linux `objdump`; they are not inferred from the missing
argument lists in the exports.

## Constraints on extensions

- MSTS loads every declared `terrain_shader` (152), not just those currently
  referenced by patches. `0x006ed310` resolves its name through `0x006b4180`;
  a lookup result of -1 fails shader loading, and the caller fails tile loading.
  An unreferenced TSRE-only shader with an unknown name is therefore unsafe.
- `terrain_patchset_patch` (164) has 60 bytes of positional field data
  (61 bytes including the normal empty-label byte). Do not insert a new integer between existing fields. Tolerance of
  a trailing extension needs a separate binary/Unicode compatibility test.
- MSTS's shader child loop has an unhandled-child skip path. ORTS's
  `terrain_shader` parser also ignores unhandled children through block
  disposal. This makes separate child blocks a candidate, not a completed
  compatibility guarantee: token recognition, namespace decoding, binary and
  Unicode framing still need checking. Old-editor save round trips are outside
  the clarified compatibility requirement.
- In contrast, ORTS's `terrain_samples` (139) parser explicitly throws on
  unknown tokens. A new map reference beside `terrain_sample_ybuffer` (146)
  is not transparently backward compatible in that container.
- Ignoring an extension while reading does not imply preserving it on save.
  MSRE's recovered serializers write known fields; TSRE-only metadata should
  not depend on an MSRE edit/save preserving unrecognized blocks. This is a
  usage boundary, not a reason to reject direct material-array references in
  the intended read-only workflow.

ORTS statements above refer to the inspected source snapshot, not every
possible fork. [TerrainFile.cs](https://github.com/openrails/openrails/blob/bbeb7ab6dd00bf7f61503f0b177839095ee7a5b8/Source/Orts.Formats.Msts/TerrainFile.cs),
[SBR.cs](https://github.com/openrails/openrails/blob/bbeb7ab6dd00bf7f61503f0b177839095ee7a5b8/Source/Orts.Parsers.Msts/SBR.cs).

## Suggested design for the painted material-ID map

Keep one ordinary patch set and a valid legacy shader table for fallback.
Reuse `terrain_shaders` (151) as the common material/palette table. Retain
recognized `terrain_shader` (152) names, valid existing texture slots, and
legacy-compatible ordinary fields. Add optional procedural properties in a
separately framed extension, and reference the external compressed ID map from
a parser-compatible location. A companion metadata file remains an alternative,
but a separate material registry is not required. No new token names or numeric
IDs are allocated by this design note.

The general-purpose compatibility layout for ordinary paired tiles preserves
the existing half-table convention:

```text
[ DetailTerrain 0, DetailTerrain 1, ..., AlphaTerrain 0, AlphaTerrain 1, ... ]
```

A straightforward convention for that layout is that painted byte IDs address
first-half entries. The latest one-atlas design permits the narrower adapter
described above: if all legacy patches select the same fallback, only its
midpoint counterpart must reproduce that fallback. TSRE may use the other
entries as procedural materials. Adding records still changes the midpoint;
using recognized shader names alone does not prevent wrong far-path selection.
A native flat low-detail table has its own existing indexing convention.

The procedural data would contain:

- A losslessly compressed byte-valued material-ID raster, with explicit width,
  height, orientation, and mapping into tile/world coordinates. Its resolution
  should be independent of `terrain_nsamples` (140) and patch count.
- Byte IDs referring directly to the agreed entries in the tile's existing
  material table, providing up to 256 directly addressable entries per map.
  Source textures can reuse `terrain_texslot` (154). TSRE, as the writer, must
  update painted IDs if it reorders the referenced entries; read-only legacy
  consumers do not introduce a persistent renumbering problem.
- Additional per-material generation properties, such as world/tile-space UV
  placement and edge behavior (solid boundary, scattered transition, etc.).
  The existing six-value affine UV transform belongs to each
  `terrain_patchset_patch` (164), not to `terrain_shader` (152).
  `terrain_uvcalc` (156) is a different four-value record, not spare storage
  for an arbitrary six-value procedural transform. Existing texture-slot
  integers also have MSTS consumers. Reuse defined meanings; add properties
  for genuinely new meanings instead of overwriting active legacy fields.
- Versioning and deterministic generation parameters. Define what happens when
  a recipe or map is missing; the existing standard patch material is a useful
  fallback.

TSRE can generate and cache patch textures from the map. Evaluate texture
placement in shared tile/world coordinates and provide neighboring samples or
border texels so patch boundaries do not reset texture phase or filtering.
Generated color textures can be mipmapped normally; categorical IDs must not
be averaged or color-corrected. Smooth transitions require an explicit blending
policy, such as neighboring-material blending or additional IDs and weights.
Deterministic procedural scattering at material boundaries can be generated
from the recipes without painting high-frequency random IDs into the map.

A 2048-by-2048 byte raster occupies 4 MiB before compression. Large painted
regions should compress well; noisy per-pixel choices may not. Storage savings
do not eliminate generated-texture memory or generation cost, so cache lifetime
and edit invalidation matter.

Legacy fallback can be either existing approximate materials, or optionally
small baked ACE textures/atlases for closer visual agreement. MSTS and
unmodified ORTS cannot reconstruct the new painted result from the ID map by
themselves. A byte map plus recipes avoids shipping the full generated texture
set to procedural-aware consumers; matching legacy visuals still requires
legacy-readable texture content.

## Implications for a possible future MSTS executable extension

Reusing the material table and valid fallback fields avoids requiring a second
material registry and a different patch-set selector. That is a useful
constraint for a future binary implementation. It does not by itself provide
procedural rendering: an implementation would still need compressed-map
loading, generation/blending rules, and texture creation, caching, and
invalidation. This is substantially different work from increasing the bounds
of an existing terrain-size algorithm; its difficulty has not been audited.
A small, explicitly framed new property can be easier to support correctly
than making a field with existing MSTS consumers carry an unrelated meaning.
