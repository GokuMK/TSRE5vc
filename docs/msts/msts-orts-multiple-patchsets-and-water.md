# Multiple terrain patch sets: MSTS selection and Open Rails water handling

Date: 2026-09-05
Status: static executable/source review; multi-set runtime tests remain pending

Mirror synchronized: 2026-09-08. The native/ORTS findings retain their pinned
inputs below. References to the older R32 patch design are historical: the
[current terrain patch profile](msts-orts-terrain-profile-compatibility.md) permits
R64 after the v5 retests. Evidence paths refer to the separate MSTS workspace.

## Result

No file parameter was found which makes the audited MSTS renderer choose the
first `terrain_patchset` (159). It directly selects the last array element.
`terrain_patchset_distance` (160) is parsed and saved, but does not participate
in that selection. The terrain intersection path independently selects the
last element too. This behavior is present in both Microsoft Patch 1.4 and the
local MSTS Bin 1.8.052113 executable.

Open Rails chooses the first set for both terrain and water geometry. Its
`ContainsWater` predicate scans all sets, but does not select or render their
water collectively. The mismatch already existed in the earliest imported
ORTS source examined, from December 2009. No supporting MSTS observation or
multi-set design explanation was found in that history.

## Inputs and verification

All executable work used private WSL copies, without running Windows code or
accessing the Windows host. No executable patch was produced.

| Input | Identity |
|---|---|
| Microsoft updated executable | SHA-256 `730b5054adc73c2cbfb0b3eb6eb2d9d95ae339fcc3922fe74cb318d747319584` |
| Local normal Bin 1.8.052113 executable | SHA-256 `69218fce876298c684a2140c7d3925a452c47bb10037ffd8c491f65c5c0c6e7a` |
| ORTS `master` | Commit `3a4d79804140d1749e39cc175c8f4a5939bbeb66` |
| ORTS `unstable` | Commit `bbeb7ab6dd00bf7f61503f0b177839095ee7a5b8` |

The complete functions at `0x006cda00`, `0x0070bc60`, `0x006bf670`,
`0x006bf8d0`, `0x006c3940`, `0x006e32e0`, `0x00710630`, and `0x006f3ad0`,
and the short callback at `0x006e3db0`, were compared byte for byte between the
two executables and match. The registration region `0x006ee1c0..0x006ee28f`
also matches. Addresses below are virtual addresses in these inputs.

## Why a terrain parameter does not change MSTS selection

The tile object stores its patch-set count at offset `+0x98` and array pointer
at `+0x9c`; each runtime set occupies `0x28` bytes. These are memory offsets,
not token IDs. The parser allocates the declared collection and initializes
`terrain_patchset` (159) children in file order. The inspected load path does
not sort them by distance.

The draw routine `0x006cda00` computes the equivalent of:

```text
selectedSet = tile.patchSets[tile.patchSetCount - 1]
for each patch in selectedSet:
    test visibility and submit the patch
```

The address calculation is at `0x006cdafa..0x006cdb09`. It reads only the
array count and base pointer. There is no alternative first-set branch,
distance comparison, or search for an enabled set. Tile-level culling can skip
drawing, and patch flags can skip individual patches; neither makes it fall
back to an earlier set. The ray/terrain intersection routine at `0x0070bc60`
independently uses the same final-element calculation.

| Candidate control | Recovered effect |
|---|---|
| `terrain_patchset_distance` (160) | Float32 stored at set `+0x04`; not read by these selectors. Changing its sign, magnitude, or order does not select a different set in the recovered paths. |
| `terrain_patchset_npatches` (161) | Sets grid dimensions and derived samples per patch; does not choose the active array element. |
| `terrain_alwaysselect_maxdist` (138) | Earlier audit found no downstream consumer of the parsed tile field; not consulted by this set selection. |
| `terrain_patchset_patch` (164), `Flags & 1` | Suppresses terrain drawing for that patch; does not select the corresponding patch from an earlier set. |
| Count in `terrain_patchsets` (158) | Defines the allocated/read collection. Declaring one set exposes one set; it does not preserve extra children as usable LOD sets. |

Thus there is no recovered file switch for selecting the first of several
loaded sets. This is a conclusion about the audited paths and builds, not a
claim about every possible MSTS version or undocumented editor operation.

MSTS does more with the other sets than merely load them:

- Registration at `0x006ee1c0` and `0x006bdd50` checks the largest
  `terrain_nsamples` (140) / `terrain_patchset_npatches` (161) ratio across
  all sets. An unsupported earlier set can reject the tile even if the last
  set is valid. Stock limits apply to every set; the distributed terrain patch
  changes the limits, not this all-set traversal.
- Bounds rebuilding at `0x006eee70`, affected-patch rebuilding at
  `0x006efa30`, and serialization at `0x006f3ad0` traverse multiple sets.
- Some other helpers use an explicitly supplied set index or the array base.
  For example, tile-owned transfer mapping at `0x006db840` reads the first
  set. Therefore, "MSTS uses only the last set everywhere" is incorrect.

## Ways to keep MSTS and ORTS output consistent

The most established interchange layout remains one patch set. If several sets
must be stored for a TSRE-specific LOD implementation, a possible file-only
arrangement is:

```text
[ compatibility set A, TSRE-specific LOD sets ..., compatibility set A ]
  ORTS renders this                           MSTS renders this
```

This duplicates the chosen fallback; it does not change MSTS's selector. It is
a static design inference awaiting a runtime test. Keep the two copies equal
in dimensions, patch records, material references, and water flags. If a
`terrain_patchset_fbuffer` (162) exists, account for its flag overrides too.
Both copies share the tile's heightmap. TSRE can ignore the final duplicate
when applying its own LOD policy.

Every intermediate set must still satisfy MSTS's limits and be well formed.
For example, a coarser grid which raises samples per patch beyond 16 in stock
MSTS, or beyond the patched R32 design, remains a problem even if not selected.
MSRE edit/save behavior must also be tested: matching endpoint copies on disk
does not establish that every editor operation will keep them synchronized.

A future executable modification could change the draw and intersection
selectors to index zero. The local substitutions appear small, but an editor
compatible change must also audit explicit set indices and first/last-set
assumptions in editing, rebuilding, and related terrain features. Patching only
the visible draw routine would be insufficient evidence of complete support.

## ORTS water: a broad predicate, first-set geometry

In both inspected branches, `Tile.PatchCount` and `Tile.GetPatch(x,z)` select
`terrain_patchsets[0]`. `ContainsWater` checks for water-height metadata, then
scans every set for a patch with `Flags & 0xc0 != 0`.
`TerrainTile` uses this result to decide whether to construct a water primitive.
However, `WaterPrimitive.LoadGeometry` subsequently loops through `PatchCount`
and calls `GetPatch`, so only first-set flags generate water triangles.
[Master Tiles.cs](https://github.com/openrails/openrails/blob/3a4d79804140d1749e39cc175c8f4a5939bbeb66/Source/RunActivity/Viewer3D/Tiles.cs),
[unstable Tiles.cs](https://github.com/openrails/openrails/blob/bbeb7ab6dd00bf7f61503f0b177839095ee7a5b8/Source/RunActivity/Viewer3D/Tiles.cs),
[unstable Water.cs](https://github.com/openrails/openrails/blob/bbeb7ab6dd00bf7f61503f0b177839095ee7a5b8/Source/RunActivity/Viewer3D/Water.cs).

| Water flags in a valid tile with water-height metadata | ORTS result from source |
|---|---|
| First set contains water | Water primitive is constructed; triangles follow first-set flags. |
| Only a later set contains water | Predicate returns true, but the generated index list is empty. Later-set water is not rendered. |
| No set contains water | No water primitive is constructed. |

The later-only case still attempts index-buffer creation with zero entries.
Its runtime outcome has not been tested here; do not assume it always reduces
to a harmless empty draw.

### Historical explanation and limits of the evidence

Commit `b1c9643315f9ea469e1d2cc878e985e0cdabb8af`, dated 2009-12-16
(imported SVN revision 2), already contains both the all-set `TFile.ContainsWater`
loop and a water renderer that accesses set zero. Its commit message gives no
behavioral rationale. [Original predicate](https://github.com/openrails/openrails/blob/b1c9643315f9ea469e1d2cc878e985e0cdabb8af/Source/Run/MSTS/TFile.cs),
[original water renderer](https://github.com/openrails/openrails/blob/b1c9643315f9ea469e1d2cc878e985e0cdabb8af/Source/Run/3DViewer/Water.cs).

The 2013-09-15 rewrite `15ae87e4993c4a1a2181f4ec2ee21d46a17148e0`
moved this predicate into `Tile`. Its stated purpose was fixing quad-tile and
distant-mountain handling; it preserved the all-set/first-set mismatch without
an explanation specific to multiple patch sets.
[2013 rewrite](https://github.com/openrails/openrails/commit/15ae87e4993c4a1a2181f4ec2ee21d46a17148e0).

This supports describing the code as a longstanding inconsistency between a
file-wide predicate and the selected geometry. A generic "any set has water"
helper is a plausible origin, but that motive is an inference. The history
does not establish that its authors observed matching MSTS behavior. A
consistent ORTS first-set policy would make the predicate examine the selected
set as well; empty geometry should also be handled explicitly.

### What MSTS's water callbacks actually use

The MSTS terrain submission routine at `0x006f11d0` adds the selected patches
to its visible lists. The water paths consume those same lists:

- `0x006bf670` tests each submitted patch's `Flags & 0x40` and invokes the
  configured first-layer callback before terrain mesh drawing. Its inputs are
  the normal or auxiliary visible-patch list.
- `0x006bf8d0` tests `Flags & 0x80` on the normal visible list and invokes the
  subsequent-layer callback.
- `0x006c3940` installs `0x006e32e0` for layer zero and `0x006e3db0` for
  subsequent layers. The latter forwards to `0x006e32e0` with layer index +1.
  The common callback reads tile water-corner heights and patch sample bounds;
  it also requires patch flag `0x01000000` before emitting geometry.

These paths therefore inherit the last-set selection. No all-set water
discovery predicate was found in this rendering chain. This establishes which
sets supply water here; it is not a complete decoding of all water flags or
all editor water operations.

### Separate ORTS water-grid limitation

Both inspected branches still use a 17-by-17 water vertex grid, index stride
17, and coordinate normalization by 16. Their patch loops use `PatchCount`,
but those constants do not follow it. Consequently, custom `P!=16` terrain
support does not establish matching water support: `P<16` uses only part of
the fixed grid, while `P>16` can produce indices beyond its 289 vertices.
This is separate from which patch set is chosen.
[Master Water.cs](https://github.com/openrails/openrails/blob/3a4d79804140d1749e39cc175c8f4a5939bbeb66/Source/RunActivity/Viewer3D/Water.cs),
[unstable Water.cs](https://github.com/openrails/openrails/blob/bbeb7ab6dd00bf7f61503f0b177839095ee7a5b8/Source/RunActivity/Viewer3D/Water.cs).

## Minimal future runtime checks

Start with a copied, known-working `N=256/P=16` tile. Keep those dimensions in
every set for the first tests, so dimensions and water-grid limits do not
confound the result. Use distinct existing terrain textures A and B and valid
copies of all patch records and shader references.

| Fixture | Question / static prediction |
|---|---|
| Sets `[A, B]`, then `[B, A]` | MSTS follows the last texture; ORTS follows the first. |
| `[A, B]`, swap only `terrain_patchset_distance` (160) values | No selection change in the recovered paths. |
| `[A, B]`, set one last-set patch's `Flags & 1` | MSTS skips that terrain patch rather than falling back to A. |
| `[A, B, A]` | Both should show A; test MSRE edit/save/reload separately for endpoint synchronization. |
| Water in first set only, then last set only | ORTS geometry follows the first; MSTS water submission follows the last. Copy complete flags from a working wet patch, including high bits, for this test. |
| Valid final set with an earlier `N=256/P=8` set | Stock MSTS should reject due to the earlier set's R32; isolates all-set validation. |

These are proposed manual tests, not observations. Windows access or execution
requires separate explicit user approval.

## Local analysis artifacts

- `analysis/pe/ghidra-patchset-selection-review.txt`
- `analysis/pe/ghidra-patchset-water-review.txt`

Both were generated using `scripts/ghidra/ExportFunctionsByAddress.java` with
`-readOnly -noanalysis` on the existing `msts-terrain` Ghidra project. Each
records the input executable hash and requested function addresses. The
`0x006e3db0` callback lacks a Ghidra function definition in that project; its
30-byte wrapper was verified directly with Linux `objdump` instead.
