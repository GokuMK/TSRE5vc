# ACE file structure, MSTS consumers, and legacy TSRE support gaps

> **Legacy comparison, not the new ACE library:** the TSRE load/save behavior,
> limitations and proposed upgrades below describe the pre-upgrade `AceLib`
> and `Texture` source snapshot. The old ACE implementation is now retained as
> `AceLibLegacy`; these limitations are **not** a support matrix for the new
> production `AceLib` / `AceDocument` and upgraded `Texture`. References to
> `AceLib.cpp` or “current TSRE” below retain their historical meaning.

For implemented behavior, memory/preservation policy and newer runtime results,
see the [ACE API/integration guide](../features/ace-library.md)
and [TSRE ACE implementation and MSRE size/format tests](tsre-ace-v2-implementation-and-msre-tests.md).
This report remains the detailed format and MSTS executable-evidence reference.
Its upgrade proposals and open test questions are historical, not an outstanding
implementation checklist; the newer reports supersede them where addressed.

Mirror synchronized: 2026-09-08, from
`reports/tsre-msts-ace-file-field-usage.md` in the separate MSTS research workspace.
Evidence paths under `analysis/`, `scripts/`, `proprietary/` and non-linked
`reports/` paths below refer to that workspace, not this repository. Those raw
evidence files and non-mirrored reports are not bundled here. The legacy scope
notice above also appears in the source report.

Date: 2026-09-07. Scope: Linux-local static executable analysis, supplied-file
inspection, synchronized TSRE/ORTS sources, and subsequently authorized
MSRE/Wine format probes (`reports/msts-ace-wine-format-probes.md`). No Windows-host access
was performed. Runtime claims below are limited to the documented Wine tests.

## Main findings

ACE has substantially more structure than legacy TSRE retained: a header,
channel descriptors, optional palette records, image offset tables, mip levels,
and sometimes Photoshop resource blocks. In particular:

- MSTS's normal texture reader **uses the stored image offsets**. Legacy TSRE's writer
  emits a four-times-too-large RGB row stride in that table. The supplied
  `ROUTES/mini/graphic.ace` has exactly that pattern.
- The normal planar variants contain separate RGB, mask and alpha channels.
  Legacy TSRE assumes a particular channel layout and its writer saves RGB only.
- MSTS has actual palette consumers and DXT1–DXT5 surface mappings. Follow-up
  Wine tests confirm **DXT3/5 rendering, including partial alpha**, and packed
  raw 16-bit rendering. A specific RGB-indexed profile works; the candidate
  RGBA-indexed profile does not render correctly. This remains narrower than
  compatibility certification for every mapped variant or rendering path.
- `options & 0x04` is a **static surface-access hint**, not compression or an
  alpha mode. `0x02` and `0x08` select other DirectDraw access hints.
- The 2×2 and 1×1 tail of the observed DXT1 mip chains is **planar channel data**,
  without the larger levels' DWORD length prefix—not two more DXT blocks.
- Odd-sized ACE bitmaps are ordinary supplied assets, not merely a hypothetical
  extension: 132 of the 508 extracted stock ACEs have an odd dimension. Legacy
  TSRE rejects them.
- Much of the apparent trailing data is well-formed Photoshop `8BIM` resources.
  It should not be mistaken for corrupt pixels or stripped during a lossless
  round trip.

### Structure versus the terrain token grammar

This distinction is based on **MSTS's executable, not on TSRE's shortcuts**.
ACE and terrain files share the outer SIMISA file/compression machinery, but
their payload readers are different:

- An ACE body starts with a fixed version DWORD. `0x006ec990` reads **152 bytes
  as one header**, then checks that its first DWORD is `1`.
- `0x006ec9d0` reads **16 bytes per channel descriptor**. The following palette,
  offset and image readers consume the records documented below.
- These ACE paths do not pass those records through the terrain/world
  `token ID + block length + label + children` reader. The Core token names
  `image`, `images`, `texture` and `textures` are not the tags of these ACE
  records. In particular, the initial `1` is a version, not Core token 1.
- The optional Photoshop footer really does have tagged, length-delimited
  resource blocks; those are Adobe resource IDs, not MSTS Core/TRAIN tokens.

Thus a structural reader is absolutely needed, but reusing the `.t` token parser
for the ACE body would misinterpret the bytes. In this report, PascalCase ACE
field names are **descriptive labels**, not recovered original C structure names
or invented MSTS tokens. Existing ORTS enum names and actual DirectDraw constants
are identified explicitly.

## Evidence and version scope

The primary executable is the clean Bin 1.8 specimen:

`proprietary/msts_app/train-1.8.exe`

SHA-256: `69218fce876298c684a2140c7d3925a452c47bb10037ffd8c491f65c5c0c6e7a`.

The comparison specimen is Microsoft 1.4:

`proprietary/extracted/official-update/train.exe`

SHA-256: `730b5054adc73c2cbfb0b3eb6eb2d9d95ae339fcc3922fe74cb318d747319584`.

All 20 initially audited ACE reader/writer/consumer function ranges and the five
DXT format records are byte-identical between these specimens. See
`analysis/ace/executable-audit.json` and the reproducible
`scripts/audit_msts_ace_executable.py`. This does not assert
that every graphics path elsewhere in Bin is identical to 1.4.

The follow-up live tests used private `train-wine-r64-v5.exe`, with the existing
terrain/direct-launch/editor-window changes. Its same 20 ACE ranges and five
DXT records were compared with clean Bin and are unchanged. The Wine report (`reports/msts-ace-wine-format-probes.md`)
records its exact hash, fixtures, screenshots and scope; no ACE-related EXE
patch was made.

Source comparison is explicitly **TSRE-related** or **ORTS-related**, not a claim
about MSTS:

| Source snapshot | Reviewed file | SHA-256 |
| --- | --- | --- |
| TSRE local HEAD `6d77046472b97082ddebb9cc4c3d5fb7ca8315d8` | `/root/TSRE5vc/src/tsre/texture/AceLib.cpp` | `f80f2fc1f78613261914a406d95dde2e4aaff49a9e9783d36180bbdc1830aeb2` |
| Same TSRE snapshot | `/root/TSRE5vc/src/tsre/texture/Texture.cpp` | `4cb4135224eb2339c28b2c6c2716218d847286495482a9afea05cc5d51a4539a` |
| Synchronized local ORTS copy; not represented as a freshly fetched branch | `/root/openrails/Source/Orts.Formats.Msts/AceFile.cs` | `e3d90c7e6c0b4d2d3103d43509f3e9ff1d0479ffca5b60d15e0c10523e51828e` |

The Microsoft-authored `proprietary/extracted/official-update/Utils/MakeACE Release
Notes.txt` supplies authoring-policy evidence. DirectDraw/DXT and Photoshop
documentation below supplies names for those external formats, not proof that
MSTS executes a particular path successfully.

## 1. Overall layout and offset origin

ACE integers are little-endian unless specifically marked otherwise. The
following tree describes serialization order, not a hierarchy of MSTS tokens.

```text
16-byte SIMISA envelope
└── ACE body, optionally zlib-compressed as a whole
    ├── fixed header                         152 bytes
    ├── ChannelCount channel descriptors     16 bytes each
    ├── PaletteCount palette descriptors     12 bytes each
    ├── palette payloads                     count × entry size for each palette
    ├── offset table(s)
    │   ├── planar: one uint32 per row, for every mip
    │   └── raw:    one uint32 per mip
    ├── image levels                         largest first
    └── optional trailing data/resources
```

Every body offset below starts at the version DWORD. For a plain physical file,
**add 16**. For a compressed file, first inflate the body; compressed physical
offsets cannot be used as image offsets. TSRE's current shared `ReadFile` helper
retains an extra 16-byte envelope before the inflated data, which explains its
physical-looking constants such as 216 and 248.

### Outer envelope

| Physical offset | Name / type / size | Meaning and implementation |
| --- | --- | --- |
| `0x00` | Signature, `byte[8]` | `SIMISA@@` for plain, `SIMISA@F` for zlib. Validate the whole signature, not just byte 7. |
| `0x08`, plain | Padding, `byte[8]` | Eight `@` bytes. The complete plain prefix is `SIMISA@@@@@@@@@@`. |
| `0x08`, zlib | InflatedBodyBytes, `uint32`, 4 bytes | Expected size of the uncompressed body, excluding the 16-byte envelope. Bound allocations and compare with actual inflation output. |
| `0x0C`, zlib | Padding, `byte[4]` | Four `@` bytes. |
| `0x10` | Body or zlib stream, variable | Standard zlib-wrapped DEFLATE in the compressed case. This compression is independent of the pixel surface format and `RawData`. |

Do not conflate three things: SIMISA/zlib disk compression, raw versus planar ACE
storage, and DXT block compression. A robust implementation represents them
separately. Outer-zlib plus DXT is structurally expressible; it was not present
in the inspected corpora and is not runtime-tested here.

## 2. Fixed header: 152 bytes

`0x006ec990` reads this record; `0x006ecea0` writes it. The pointers MSTS adds at
in-memory offsets `0x98`, `0x9C`, and `0xA0` refer to channel, palette and offset
arrays. **Those pointers are not additional serialized header fields.**

| Body offset | Descriptive field | Type / bytes | MSTS evidence / meaning | TSRE load / save; upgrade requirement |
| --- | --- | --- | --- | --- |
| `0x00` | Version | `uint32` / 4 | Must equal `1` in the native header reader. | Loader does not validate; writer emits 1. Validate before interpreting subsequent data. |
| `0x04` | Options | bitmask `uint32` / 4 | Mips, surface-access hints and raw layout; see next table. | Reads low byte and matches 0/1/4/5 in the planar path; writer emits 0. Read full DWORD and test bits independently. |
| `0x08` | Width | `uint32` / 4 | Used for rows, mip count, image allocation and cropping. | Reads only low 16 bits; writes 32 bits. Use a checked full-width value. |
| `0x0C` | Height | `uint32` / 4 | Used for row-table lengths and image allocation/cropping. | Same low-16-bit problem; do not infer square images without checking mip policy. |
| `0x10` | SurfaceFormat | enum-sized `uint32` / 4 | Chooses native destination/conversion or raw surface; see surface table. | Reads low byte into misleadingly named `texture->compressed`; only 18 gets the raw/DXT path. Writer always 14. |
| `0x14` | ChannelCount | `uint32` / 4 | Controls the 16-byte descriptor array and plane-read loops. | Low byte used to infer RGB/RGB+mask/RGB+mask+alpha from counts 3/4/5. Read descriptors instead. |
| `0x18` | PaletteCount | `uint32` / 4 | Controls optional 12-byte palette descriptors and their payloads. Native texture path consumes a palette when nonzero. | Skipped and zeroed. Must at least parse/skip the associated arrays correctly. It is not padding. |
| `0x1C` | FormatText | `byte[16]` / 16 | Commonly `Unknown`; a native texture rewrite copies exactly 16 bytes. No image-decoding dispatch by this string found. Exact historical field name unknown. | Skipped/zeroed. Preserve bytes; expose a bounded, optional text view. |
| `0x2C` | CreatorText | `byte[64]` / 64 | Examples identify the Photoshop plug-in. Native rewrite copies 64 bytes. Not a Unicode string or a length-prefixed token label. | Skipped/zeroed. Preserve without assuming NUL termination. |
| `0x6C` | UnresolvedBytes6C | `byte[4]` / 4 | Native rewrite explicitly copies the first three bytes. They are zero in the stock census. A color-key interpretation is plausible but **not established** here. | Skipped/zeroed. Preserve; do not implement a guessed chroma-key policy. |
| `0x70` | UnresolvedWord70 | `uint32` / 4 | Zero in stock census; no semantic consumer established. | Preserve as opaque. |
| `0x74` | ResourceBytes | `uint32` / 4 | Corpus-backed length of a Photoshop resource footer, where present. All 416 stock / 870 app footers parse from `body_end - ResourceBytes`. Some files retain a nonzero value without that footer, so it is not an unconditional seek instruction. | Skipped/zeroed. Preserve together with resources; see footer section. |
| `0x78` | RendererScale | `float32` / 4 | `0x006ab280` copies this float to the texture descriptor, substitutes 20.0 for values below a small positive threshold, and multiplies it by `2^firstLoadedMip`. Its final visual/units meaning remains unresolved. | Skipped/zeroed. Preserve, and keep separate from a claimed gamma or alpha threshold. |
| `0x7C` | FallbackSurfaceFormat | enum-sized `uint32` / 4 | Used by the DXT fallback path and the small planar mip tail. Stock DXT1 examples contain 14 for RGB or 16 for RGB+mask. | Skipped/zeroed. Needed for faithful raw-to-planar/fallback interpretation. |
| `0x80` | SecondarySurfaceFormat | `uint32` / 4 | All 79 stock DXT1 files contain 18; the other stock files contain 0. Correlation established, independent runtime meaning not established. | Preserve; do not substitute it for the main format field by assumption. |
| `0x84` | UnresolvedHeaderTail | `byte[20]` / 20 | Stock census all zero; no semantic consumer established in this review. | Preserve as opaque rather than claim it is safe extension space. |

The ORTS reader skips the complete 128 bytes from `0x18` through `0x97`.
Consequently its comment about miscellaneous data is **not** an inventory of
fields MSTS does not use.

### Options bitmask

| ACE bit / value | Descriptive or existing name | Native use | TSRE / ORTS guidance |
| --- | --- | --- | --- |
| bit 0 / `0x01` (1) | `MipMaps` — ORTS enum name | Counts the full chain from width and reads the corresponding offset tables. | Parse and retain all levels. Do not compare the entire flags word to 1. |
| bit 1 / `0x02` (2) | Dynamic-access hint | `0x006ab280` maps to engine flag `0x80`; `0x006b69a0` maps that to `DDSCAPS2_HINTDYNAMIC` (`0x04`). | Preserve. It is not a different pixel layout. |
| bit 2 / `0x04` (4) | Static-access hint | Maps through engine flag `0x100` to `DDSCAPS2_HINTSTATIC` (`0x08`). Explains common flags 4 and 5. | TSRE allows these values but discards the hint; ORTS ignores it. No alpha/compression interpretation is appropriate. |
| bit 3 / `0x08` (8) | Opaque-to-CPU/access hint | Maps through engine flag `0x4000` to `DDSCAPS2_OPAQUE` (`0x80`) in the texture-surface path. | **Not “all pixels have alpha 255.”** The hint concerns future surface access, not visual transparency. |
| bit 4 / `0x10` (16) | `RawData` — ORTS enum name | Changes row-offset tables to mip-offset tables and selects length-prefixed raw levels, with the DXT small-mip exception. | Use this bit, not `SurfaceFormat == 18`, to select layout. |
| remaining bits | Unresolved | No use established here. | Preserve on round trip, reject unsupported structural interpretations explicitly. |

The native header-to-descriptor path takes the first of the 2/4/8 hints in that
order, unless caller settings override the header hints. They are not three
independent pixel-processing effects. The mapped DirectDraw meanings are
documented in Microsoft's [DDSCAPS2 reference](https://learn.microsoft.com/en-us/windows/win32/api/ddraw/ns-ddraw-ddscaps2);
numeric constants also match [Wine's DirectDraw header](https://github.com/wine-mirror/wine/blob/master/include/ddraw.h).
These are surface placement/access optimizations; TSRE can preserve them without
reproducing the old DirectDraw allocation policy.

## 3. Channel descriptors and planar pixels

Each descriptor is exactly 16 bytes:

| Descriptor offset | Field | Type / bytes | Meaning |
| --- | --- | --- | --- |
| `+0x00` | BitsPerPixel | `uint64` / 8 in ORTS's representation | Number of bits this channel contributes per pixel. Native row-size arithmetic uses the low DWORD. The observed high DWORD is zero. |
| `+0x08` | ChannelId | `uint64` / 8 in ORTS's representation | Identifies the logical channel. Observed high DWORD zero. This is a channel enum, not a namespaced SIMISA token. |

Retain the full serialized widths, even if implementation validation accepts
only a small set of values. Do not allow truncation of a corrupt 64-bit field to
silently turn it into a valid 8-bit value.

| Channel ID and name | Ordinary size | Meaning / decode | Preservation and native-use qualification |
| --- | --- | --- | --- |
| `Mask` (2) | 1 bit/pixel | MSB-first packed bits: bit 7 is the leftmost pixel, 1 means present/opaque, 0 cut out. Row size `ceil(W/8)` bytes. | Keep separately from 8-bit alpha. It permits a binary-transparency destination/path. |
| `Red` (3) | 8 bits/pixel | `W` unsigned bytes per row. | Preserve precision; planar format 14 does not mean the file stores only 5 red bits. |
| `Green` (4) | 8 bits/pixel | Same, green component. | Ditto. |
| `Blue` (5) | 8 bits/pixel | Same, blue component. | Ditto. |
| `Alpha` (6) | 8 bits/pixel | `W` unsigned bytes: 0 transparent, 255 opaque, intermediate coverage. | Do not discard the accompanying mask or derive it anew unless exporting by explicit policy. |
| Other IDs/sizes | Not established comprehensively | MSTS's low-level row reader can size other bit depths, and its palette/conversion subsystem is broader than RGB. | Parse bounds and retain opaque descriptors, but fail explicitly for unsupported decoding. A generic sizing loop is not proof that arbitrary channels render. |

The five names above are those in ORTS's `SimisAceChannelId`, cross-checked with
the supplied RGB/mask/alpha layouts and native consumers. They are not FFEdit
token-table names.

All inspected stock and app channel lists have the conventional order:

```text
Solid: R8, G8, B8
Trans: R8, G8, B8, Mask1
Alpha: R8, G8, B8, Mask1, Alpha8
```

For each image, data is **row-interleaved by channel**, not fully planar over the
whole image and not pixel-interleaved RGB:

```text
row 0: R[W] G[W] B[W] [Mask[ceil(W/8)]] [Alpha[W]]
row 1: R[W] G[W] B[W] [Mask[ceil(W/8)]] [Alpha[W]]
...
```

Native `0x006ecc60` reads `max(1, ceil(mipWidth * channelBits / 8))` bytes for
one channel row. For valid positive dimensions and supported nonzero bit depths,
this is the usual byte ceiling. Do not reproduce its permissive behavior on
malformed zero-width levels.

**Channel-order compatibility:** a new TSRE reader should dispatch by channel
ID, as ORTS does, rather than infer channels from their count. However the MSTS
texture uploader reads planes into positional buffers and selects conversion
callbacks from the surface format. This review does not prove MSTS supports
arbitrary channel reorderings. Write conventional order for MSTS compatibility.

**Mask versus alpha:** the reviewed ORTS renderer chooses Alpha if present,
otherwise Mask, otherwise 255. That is a useful RGBA view, but not a lossless ACE
data model. MSTS chooses native conversion/destination paths according to format
and caller flags; do not describe every MSTS use as simply “ignore Mask whenever
Alpha exists.” The terrain C-buffer path specifically consumes the 8-bit alpha
plane in its five-channel case.

## 4. Optional palette records

These are executable-backed, not hypothetical extension tokens:

- `0x006eca30`: reads `PaletteCount` descriptors, **12 bytes each**.
- `0x006eca90`: reads each payload using `EntryCount * EntryBytes`.
- `0x006ecea0`: writes the same descriptors and payloads before image offsets.
- `0x006ac080`: interprets the first palette and prepares color entries for the
  destination surface.

| Descriptor offset | Name | Type / bytes | Meaning / native behavior |
| --- | --- | --- | --- |
| `+0x00` | EntryCount | `uint32` / 4 | Consumer recognizes 2, 4, 16 or 256 entries. |
| `+0x04` | EntryBytes | `uint32` / 4 | Multiplied by EntryCount when reading/writing the payload. RGB needs 3 and RGBA needs 4 bytes for the observed consumer logic. |
| `+0x08` | EntryType | `uint32` / 4 | Type 7 is packed RGB bytes; type 8 is packed RGBA bytes. Other types rejected by the reviewed palette consumer. |
| after **all** descriptors | Entries | `byte[EntryCount * EntryBytes]` per palette | Type 7 reads R,G,B and normally supplies opaque alpha; a caller flag can make the first entry transparent. Type 8 reads R,G,B,A. |

MSTS's in-memory palette record is 16 bytes because it adds a payload pointer at
`+0x0C`; that pointer is **not** in the file. Do not serialize an in-memory struct
with its native pointer/ABI layout.

All inspected **supplied** ACEs have `PaletteCount == 0`; synthetic probes below
are separate. No supplied paletted ACE was found.
The low-level loader loops over the count, but the reviewed color consumer uses
the first palette and fixed global scratch storage. Therefore this is **not** a
claim that multiple arbitrary palettes are safe in MSTS. The exact indexed-image
surface/channel profiles need targeted fixtures before promising general
paletted ACE interoperability.

The Wine follow-up (`reports/msts-ace-wine-format-probes.md`) successfully rendered a
64×64, no-mip indexed image with surface 4, one 8-bit channel of ID 1, and one
256-entry RGB palette (stride 3, type 7). ID 1's original symbolic name remains
unrecovered. A candidate surface-12 profile with the same index plane and an
RGBA palette (stride 4, type 8, all entries opaque) produced mostly black areas
and colored boundaries. That combination is **not a verified RGBA-indexed
encoding**; the correct source/channel/caller combination remains unresolved.

TSRE and the inspected ORTS reader do not handle these records. With a nonzero
palette count, their assumptions about where the offset table starts are wrong;
this is more serious than simply ignoring an unused palette field.

## 5. Surface formats and raw image levels

`SurfaceFormat` has two related uses: selecting the desired/conversion format
for structured channels, and describing actual bytes in raw image levels.
**When RawData is clear, the descriptor sizes describe the file's pixels.**
For example, structured format 17 stores 8-bit R/G/B/Alpha planes plus a 1-bit
mask, not a stream of packed 16-bit ARGB4444 pixels.

| Surface value and name | Raw representation | MSTS evidence | TSRE ACE / inspected ORTS support |
| --- | --- | --- | --- |
| 14 / `0x0E`, RGB565 | 16-bit packed RGB, R5:G6:B5 | Stock solid profile/native selection; raw opaque colors rendered in the Wine probe. | TSRE handles the ordinary planar profile but not raw RGB565. ORTS maps raw to `Bgr565`. |
| 16 / `0x10`, ARGB1555 | 16-bit A1:R5:G5:B5 | Stock masked profile/native selection; raw opaque colors rendered in Wine. Raw cutouts not tested. | TSRE handles conventional planar mask files, not packed raw. ORTS maps to `Bgra5551`. |
| 17 / `0x11`, ARGB4444 | 16-bit A4:R4:G4:B4 | Stock alpha profile/native selection; raw color and partial-alpha overlay rendered in Wine. | TSRE handles conventional planar files subject to its mask-skip bug, not packed raw. ORTS maps to `Bgra4444`. |
| 18 / `0x12`, DXT1 / BC1 | 8 bytes per 4×4 block | Native FourCC `DXT1`; 79 stock examples. | TSRE ACE's only raw/DXT variant; ORTS maps to `Dxt1`. |
| 19 / `0x13`, DXT2 | 16 bytes per 4×4 block, explicit alpha, premultiplied RGB | Native FourCC `DXT2` and selection case. No corpus example or live test. | Neither reviewed ACE reader supports it. |
| 20 / `0x14`, DXT3 / BC2 | 16 bytes per 4×4 block, explicit alpha | Native FourCC `DXT3`; no supplied example. Synthetic no-mip color and partial-alpha overlay rendered correctly in Wine. | TSRE ACE reader does not select it; shared TSRE texture decoder already supports DXT3. ORTS maps to `Dxt3`. |
| 21 / `0x15`, DXT4 | 16 bytes per 4×4 block, interpolated alpha, premultiplied RGB | Native FourCC `DXT4` and selection case. No corpus example or live test. | Neither reviewed ACE reader supports it. |
| 22 / `0x16`, DXT5 / BC3 | 16 bytes per 4×4 block, interpolated alpha | Native FourCC `DXT5`; no supplied example. Synthetic no-mip color and partial-alpha overlay rendered correctly in Wine. | TSRE ACE reader does not select it; shared decoder already supports DXT5. ORTS maps to `Dxt5`. |
| Other values | Not completely mapped here | Native selection includes additional cases below 14, and 15 shares a selection branch with 14. This is insufficient to assign their complete disk semantics. | Preserve/reject explicitly; do not send an unknown raw format to the planar RGB reader. |

The DXT1–DXT5 mapping is anchored by `0x006ab3e0` and the five 32-byte DirectDraw
format records at `0x007aa788`, `0x007aa7a8`, `0x007aa7c8`, `0x007aa7e8`,
`0x007aa808`. The DXT layouts and premultiplication distinction are described by
Microsoft's [compressed texture formats](https://learn.microsoft.com/en-us/windows/win32/direct3d9/compressed-texture-formats)
and [DXT usage reference](https://learn.microsoft.com/en-us/windows/win32/direct3d9/using-compressed-textures).
ACE is the container here; there is no DDS header to prepend or parse.

The live results (`reports/msts-ace-wine-format-probes.md`) use 64×64, single-level
fixtures. DXT3/5 and raw ARGB4444 overlay images match the planar-alpha control
pixel-for-pixel in the compared scene region. The terrain material itself
ignored alpha even for the planar control, so opaque terrain screenshots alone
were not counted as transparency evidence. Wine's software graphics renderer
does not prove that MSTS used its own DXT fallback; that branch was not traced.

### Offset tables and mips

For a square, power-of-two mipmapped texture:

```text
MipCount = log2(Width) + 1
Wm = Width  >> m
Hm = Height >> m
```

If MipMaps is clear there is exactly one image. Native counting is driven by
width, not by the larger of width and height. Arbitrary rectangular mip chains
are not certified by this review. Microsoft's texture authoring contract and
the ORTS reader require square power-of-two mipmapped images; bitmap images
without mips are a different case.

| Record | Type / size | MSTS use | Required implementation |
| --- | --- | --- | --- |
| Structured offset tables | `uint32[Hm]` per mip; total `4 * sum(Hm)` bytes | `0x006ecb00` reads them. `0x006ecbc0` seeks to the selected mip/row offset and reads its first channel; subsequent channels/rows can be sequential. | Offsets are body-relative and point to the row's first channel. Validate actual extents. Do not just skip tables on the assumption rows are always contiguous. |
| Raw offset table | `uint32[MipCount]` | `0x006ecd30` seeks to the selected mip's offset, then reads its DWORD length and payload. | Offset points at the length prefix for an ordinary raw level. DXT small-tail offsets instead point directly at planar data. |
| Ordinary raw level | `uint32 PayloadBytes`, followed by `byte[PayloadBytes]` | Length is actively used by native input, not decorative metadata. | Validate against remaining file size and format/dimensions before allocating/uploading. |
| DXT small mip tail | Channel rows for 2×2, then 1×1; no length prefix | Native software/fallback path reuses channel reads, `FallbackSurfaceFormat`, and a special load mode. | Decode/store these separately from block-compressed mips. Do not pretend they already contain padded 4×4 DXT blocks. |

For a normal DXT level, the expected byte count is:

```text
ceil(Wm/4) * ceil(Hm/4) * (8 for DXT1; 16 for DXT2–DXT5)
```

The ceil formula describes block storage; it does not by itself establish which
top-level non-multiple-of-four sizes MSTS/the graphics driver accepts.

Example: stock `Game/Template/TerrTex/microtex.ace`, 256×256 DXT1, RGB descriptors:

| Level | Size | Body offset | Stored record |
| --- | --- | --- | --- |
| 0 | 256×256 | 236 | Length 32768, then 32768 DXT1 bytes |
| 6 | 4×4 | 43940 | Length 8, then one 8-byte DXT1 block |
| 7 | 2×2 | 43952 | 12 planar RGB bytes; no length DWORD |
| 8 | 1×1 | 43964 | 3 planar RGB bytes; no length DWORD |

For RGB+mask tails, each short row still needs a complete mask byte. Thus a
2×2 tail is 14 bytes and a 1×1 tail 4 bytes, not 16 and 4 bytes of generic ARGB.

The native compressed-capable path excludes these final tiny levels from its
direct DXT surface chain; the fallback path can load them as planar data. ORTS
instead reuses the last 4×4 compressed payload for the two tiny GPU mip levels.
That is an **ORTS implementation approximation**, not the actual ACE tail
encoding. It should not become the model for a lossless ACE reader/writer.

### Important native-path difference

The terrain C-buffer ACE reader `0x006d0d60` uses `0x006ecdc0` to **skip** offset
tables and then reads sequentially. The normal texture path reads and follows
them. A malformed file might therefore look fine in a sequential reader and
still fail in another MSTS consumer. Never infer that the offsets are unused
just because TSRE/ORTS or a special MSTS path does not consult their values.

## 6. Trailing Photoshop resource blocks

416 stock files and 870 readable app files have complete, successfully parsed
resource streams beginning at `body_end - ResourceBytes`, using the DWORD at
header `0x74`. These resources commonly contain resolution, alpha-channel names,
display information and a thumbnail. All image-offset spans remain valid
without consuming the footer.

Some files also have opaque bytes between the last image and this resource
stream. Some raw DXT files retain a nonzero `ResourceBytes` value but have no
footer. Preserve this distinction; do not blindly interpret all trailing bytes
as resources or reject every missing footer as corrupt image data.

Adobe resource records have **big-endian** integers, unlike the ACE header:

| Field | Type / size |
| --- | --- |
| Signature | Four bytes `8BIM` |
| ResourceId | Big-endian `uint16`, 2 bytes |
| Name | Pascal byte string: byte length then name; pad the complete field to an even size |
| DataBytes | Big-endian `uint32`, 4 bytes |
| Data | DataBytes bytes, followed by one pad byte if odd-sized |

Relevant symbolic descriptions include ResolutionInfo (1005), alpha-channel
names (1006), thumbnail (1036), and Unicode alpha-channel names (1045). These
are **Adobe resource identifiers**, not an ACE/MSTS token namespace. Structure
and identifier descriptions are in Adobe's
[Photoshop file-format specification](https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/).

No render-time resource consumer was found in the reviewed MSTS ACE paths.
TSRE may treat resources as opaque for initial decoding, but lossless rewriting
should retain them. If pixels/dimensions change, a saved thumbnail may become
stale: distinguish “preserve original bytes” from “regenerate authoring metadata.”

## 7. Legacy TSRE-specific load/save audit

These are findings in **TSRE**, not limitations of MSTS or ACE itself.

| Area | Legacy TSRE behavior | Consequence | Suggested implementation (historical) |
| --- | --- | --- | --- |
| Envelope/header safety | `ReadFile` and `AceLib::run` inspect fixed byte offsets without first proving the required lengths. ACE version is not checked. | Malformed/truncated files can cause invalid access instead of a clean unsupported/corrupt result. | A bounded cursor, checked zlib output and checked integer arithmetic; return a useful error with path/field/offset. |
| Field widths | Width/height low 16 bits; flags/format/count low byte. | Truncation and incorrect interpretation of invalid or extended values. | Parse the real 32/64-bit fields before range validation. |
| Size policy | Rejects dimensions ≤1 and every odd dimension. The 8192 upper-limit test uses `&&`, so only exceeding both dimensions is rejected. | Rejects ordinary stock UI/cab-style sizes while not robustly bounding allocation. | Separate bitmap/mip rules, CPU memory limits, GPU size limits and consumer policy. Use checked products and reject either oversized dimension where appropriate. |
| Descriptor arrays | Skips their contents and infers channels from count. | Wrong for reordered/noncanonical channels, palettes, or other profiles. | Keep a descriptor vector and decode by ID; export canonical order for MSTS. |
| Metadata | Drops all 128 bytes after the six initial DWORDs. | Loses palettes, resource metadata, fallback format and renderer hints. | A retained ACE document model, not just `Texture` RGBA pixels. |
| Row-offset tables | Computes where pixels should start, ignores stored offsets. | Conceals malformed tables and cannot handle nonsequential storage. | Parse actual offsets; validate spans independently from a contiguous-layout optimization. |
| RGB+mask+alpha rows | Skips `height / 8` bytes for the mask on **every row** (`AceLib.cpp`, around line 169). | Should be `ceil(width/8)`; rectangular images and short mip rows become misaligned. | Use descriptor-based byte counts; preserve both planes. |
| Layout selection | Only format 18 takes the raw path; everything else is treated as planar. | Raw RGB565/ARGB1555/ARGB4444 or DXT3/5 is misread rather than identified as unsupported. | Select raw/planar from bit `0x10`, then dispatch by surface format. |
| DXT1 start calculation | Hard-coded descriptor offsets; counts mips from height even without MipMaps. | Non-mip DXT1 offsets are wrong. An extracted stock example exists: `Template/Textures/Snow/JP2bluebrgtex.ace`. | Parse channel/palette tables and the correct number of mip offsets/lengths. |
| DXT software path | `AceLib::run` has an older inline DXT1 decoder for reduced texture quality, writing full 4×4 blocks. | Its edge writes are not clipped for dimensions not divisible by four. | Reuse the existing shared, clipped block decoder; eliminate divergent decode implementations. |
| File mipmaps | Reads only level 0; reduced quality resamples that image. | Discards authored mip images and their transparency decisions. | Retain all levels; select existing mips where appropriate, regenerate only by explicit edit/export policy. |
| GPU upload | `Texture::GLTextures` uploads level 0, optionally calls `glGenerateMipmap`, and clears its CPU/compressed source buffers. | Current `Texture` is not a lossless ACE source representation. | Keep persistent ACE source data separately from disposable GPU resources. |
| Optional border processing | With `AASamples > 0` and `AARemoveBorder`, the uncompressed RGBA upload path sets border alpha to zero. | GPU/readback pixels can differ from the original file even without a texture-paint edit. | Keep display-specific processing separate from the original ACE channels used for preservation/export. |
| ACE save profile | Always plain, no mips, format 14, three 8-bit RGB channels. | Drops mask/alpha, authored mips, hints and metadata. | Explicit RGB / RGB+mask / RGB+mask+alpha profiles, mip policy and preservation options. |
| ACE save offsets | `first = 200 + 4*H`; writes `first + row * W * 3 * 4`, but writes only `W*3` pixel bytes per row. | Offset stride is **4× too large**. First row is correct, subsequent rows point too far ahead/outside the file. | Correct row stride `W*3` for this profile; general writer should record actual output positions and backpatch the table. |

The source already contains reusable DXT work: `Texture.cpp` has
`decodeDXT1Block`, `decodeDXT3Block`, `decodeDXT5Block`, and the clipped
`decodeCompressedToImageData` helper. GPU compressed upload also knows DXT3/5.
The author confirms that this shared support was added for the newer DDS
implementation and has not yet been fully connected to ACE. **All three DXT
variants—1, 3 and 5—should use `Texture.cpp`'s shared decode/upload path**;
`AceLib` should parse the container and supply the correctly identified payloads,
not maintain its own older DXT1 decoder. Making the existing static helper
available through an appropriate `Texture` interface may be needed for CPU-side
editing or quality reduction. Source mip storage and safe metadata/layout
parsing remain necessary; those are not supplied by a block decoder alone.

### What “premultiplied-alpha policy” means for DXT2/4

This is only an additional decision if TSRE chooses to support **DXT2 and DXT4**.
It is not a prerequisite for wiring ordinary DXT1/3/5 ACEs into the existing
shared implementation.

DXT2 has DXT3's block layout, and DXT4 has DXT5's, but their stored RGB is already
multiplied by alpha. For example, half-transparent red is approximately
`(R=128, G=0, B=0, A=128)` in premultiplied form, versus
`(R=255, G=0, B=0, A=128)` in straight-alpha form. The distinction is documented
in Microsoft's [DXT usage reference](https://learn.microsoft.com/en-us/windows/win32/direct3d9/using-compressed-textures).

The inspected TSRE route-editor and shape-viewer initialization uses
`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`
(`RouteEditorGLWidget.cpp` and `ShapeViewerGLWidget.cpp`). Supplying premultiplied
RGB to that straight-alpha path multiplies its contribution by alpha again,
making translucent pixels too dark. Sharing the DXT3/5 **block decompressor** is
fine; silently treating the resulting colors as ordinary DXT3/5 is not.

Two implementation choices are possible:

1. **Normalize the decoded view to straight alpha.** For `A > 0`, reconstruct
   each byte component as `clamp(round(255 * storedComponent / A), 0, 255)`.
   For `A == 0`, use a defined RGB value such as zero; the original RGB cannot
   be recovered. This fits the existing straight-alpha views with less renderer
   work, but introduces rounding/low-alpha losses. Retain the original compressed
   payload separately for unchanged-file round trips. Do not upload the original
   premultiplied blocks and then forget that normalization was needed.
2. **Keep premultiplied pixels and record that alpha mode on the texture.**
   Use a matching rendering path, typically source factor `GL_ONE` and destination
   factor `GL_ONE_MINUS_SRC_ALPHA`, and make shaders, compositing, painting and
   export respect that representation. This permits direct compressed upload but
   is a wider change than the ACE reader alone.

For the initial ACE upgrade, connect DXT1/3/5 first. If DXT2/4 is added later,
normalizing the editable/rendered view while preserving the source bytes is the
smaller change for the currently inspected TSRE pipeline.

### The supplied TSRE-style offset failure

`proprietary/msts_app/ROUTES/mini/graphic.ace` is a plain RGB 200×150 image:

```text
body bytes:                  90800
first image row:                800 = 152 + 3*16 + 150*4
actual RGB row bytes:           600 = 200*3
stored first row offsets:       800, 3200, 5600, 8000, 10400, ...
correct contiguous offsets:    800, 1400, 2000, 2600, 3200, ...
```

The incorrect sequence exactly matches the current writer's formula. This is
static file/source agreement, not a newly observed MSTS screenshot failure.
The author's proposed second loading attempt is reasonable: keep the old
**sequential-layout behavior as a compatibility fallback**, while the normal
reader follows and validates offsets. The existing function should not be kept
unchanged as an unrestricted retry because it contains the same unchecked reads,
mask-size bug and DXT assumptions identified above.

A safe two-stage design is:

1. Parse and decode normally into temporary state. Distinguish invalid offsets
   from a bad signature, truncated payload, unsupported format or resource-limit
   failure; not every failure should trigger another decoder.
2. For recognized recoverable layouts—initially TSRE's plain RGB profile with
   the exact fourfold row-offset pattern—validate all required contiguous bytes,
   then run a bounds-checked sequential fallback. Clear the failed attempt's
   temporary state before retrying, and publish the texture only on success.
3. Report that recovery was used. A later explicit save writes correct offsets;
   merely opening the file must not silently replace it. Add other compatibility
   patterns only when a real fixture establishes how they can be decoded safely.

The old routine can serve as the starting point for this fallback after its
bounds, dimensions and mask handling are corrected. Do not retry it for an
unknown raw format or unrelated bytes, or treat every plausible-looking output
as a successful decode. This retains compatibility without making incorrect
offset tables the normal file contract.

The second exceptional app file, `ROUTES/mini/textures/jp1signals.ace`, does not
start with a supported SIMISA envelope (`00 00 05 82 02 78 9c ...`). Following the
author's note that it came from the route template, the Linux-local copies were
compared directly:

| Copy | Result / SHA-256 |
| --- | --- |
| `proprietary/msts_app/TEMPLATE/TEXTURES/jp1signals.ace` | Valid 256×256 mipmapped DXT1 with RGB+mask; `acfeef0a52d1c11592ecbe247c9cac4b477b5cc47d67c3a8c786095910be80ae` |
| `proprietary/extracted/msts-1.0-gamecab/Game/Template/Textures/JP1Signals.ace` | Byte-identical to the installed template copy; same hash |
| `proprietary/msts_app/ROUTES/mini/textures/jp1signals.ace` | Not a valid ACE; `f917e1b6beb484e5f195a09b517c43b562b3598824e27f8fe9bddb4f885d5e1d` |

All three files are 44,002 bytes, but the `mini` copy differs from the good
template at 28,264 byte positions, including its last byte. This is not just a
missing SIMISA prefix. A zlib stream at byte 5 ends at byte 1414 and inflates to
42,645 bytes of unrelated structured data beginning with `Level` / `Entities`,
not an ACE header or the template's image payload.

**The evidence supports a corrupted/replaced route copy, not a missing ACE
variant or a defective supplied template.** It does not establish when, by
which program, or during which copy/save operation the damage occurred. The good
template is available if a replacement is wanted; no asset was replaced during
this review. This case must not enter the legacy ACE recovery path above.

## 8. MSTS authoring limits versus format limits

Microsoft's supplied MakeACE release notes describe:

- solid RGB, RGB plus a 1-bit transparency mask (`-trans`), and partial-alpha
  images from 32-bit TGA;
- texture ACEs with square power-of-two mip chains, documented source/output
  texture sizes 32 through 512;
- bitmap ACEs without mips, with cropped output dimensions from 1×1 through
  1024×1024, including examples of 640×480 and 1024×768 cab images;
- plain, lossless zlib, and lossy DXT output; its DXT workflow is limited to
  solid/masked textures, not partial-alpha or bitmap output.

These are **that utility's documented creation rules**, not a universal 512-pixel
limit in ACE or a verified current MSTS/Bin texture cap. Conversely, a 32-bit
Width field does not guarantee arbitrary huge textures work in MSTS. Graphics
device limits, bitmap/texture entry points and allocations are separate.

No additional Windows capture is needed to establish the row-offset or mask-skip
bugs. Captures would be useful for the still-untested exotic formats, pixel
interpretation and rendering-policy effects, not for proving basic field sizes.

### Where additional Wine runs would help

**Useful, but not required to begin the TSRE ACE upgrade.** The existing isolated
Wine/LLVMpipe lab (`reports/msts-wine-wsl2-live-test.md`) has already run MSRE and displayed
route textures; this was also used for the mini terrain comparison (`reports/msts-wine-mini-terrain-comparison.md`).
The subsequent ACE probes (`reports/msts-ace-wine-format-probes.md`) now provide bounded
format coverage, including partial-alpha DXT3/5 overlays.

| Question | Best next check | Is running an EXE necessary? |
| --- | --- | --- |
| Header/descriptor sizes, offsets, mask stride, odd-size parsing, corrupt `jp1signals.ace` | Static bytes, supplied files, parser tests and round trips | No. These findings already have direct evidence. |
| Microsoft's exact writer conventions for common ACE profiles | Optional MakeACE reference outputs, only if matching that writer is of interest | Not required for MSTS compatibility. No MakeACE run was needed for the live probes; its documented switches exclude partial-alpha DXT. |
| Broader DXT3/5, packed 16-bit and palette compatibility | Extend the documented controlled fixtures to mips, other materials/devices, fallback paths and the unresolved RGBA-indexed layout | Basic no-mip color and partial-alpha DXT3/5 rendering are now observed; these broader combinations are not. |
| Which authored mips and alpha/mask data MSTS uses in a particular rendering path | Distinct-color mip levels and alpha/mask fixtures; screenshots at fixed camera positions, with tracing if needed | Useful. A visually plausible top-level texture alone does not verify tiny mips or all transparency paths. |
| Exact names/units of unresolved header fields | Continue static data-flow analysis, then change one field per controlled fixture | A general app run alone is unlikely to answer this; targeted tracing may help. |

For a future MSRE test, use disposable route/texture copies, record file and EXE
hashes, use fresh names or restart to avoid texture-cache confusion, and compare
actual rendered output rather than just “route loaded.” Record whether Wine used
the raw compressed-surface path or MSTS's fallback where that distinction matters.
Wine software rendering does not automatically force MSTS's own fallback, and a
Wine result does not certify the user's Windows driver/wrapper configuration.

The authorized follow-up used only the isolated Linux-local lab. Host filesystem,
registry or process access remains a separate action requiring explicit approval.

## 9. Upgrade sequence and regression fixtures

Recommended implementation order:

1. **Safe document parser + correct writer:** retain header bytes, channels,
   palettes, offsets, mip payloads and footer separately from the GPU `Texture`.
   Fix the saved RGB offset stride, dimensions and mask-row size first.
2. **Complete common profiles:** arbitrary bitmap dimensions; RGB, mask and
   alpha; plain/zlib; every supplied planar mip; DXT1 with/without mips and its
   real planar tail. Keep alpha and mask independently.
3. **Use the shared DXT1/3/5 decoder/upload infrastructure in `Texture.cpp`:**
   connect ACE layout dispatch, remove the duplicate ACE-side DXT1 decoder, and
   add raw packed 16-bit decoding; validate with fixtures. Keep the
   MSTS-executable evidence distinct from tested MSTS rendering.
4. **Preservation and optional legacy breadth:** Photoshop resources, unknown
   header bytes and access hints; then indexed palettes and DXT2/4 if wanted.
   A safe “unsupported profile” result is preferable to a guessed decode.

A practical data model is `AceDocument { envelope, header, channels, palettes,
mips, trailingData }`, with decoding to RGB/RGBA as a derived view. Each mip
records dimensions, storage kind, original payload/rows and offsets; GPU copies
are not the only retained representation. This also supports byte-preserving
copying when nothing was edited and deliberate re-encoding after pixel edits.

The author explains that TSRE's historical even-size restriction was an early
filter for its then-only ACE use: 3D textures. It was not intended as a definition
of valid ACE bitmap dimensions. The upgraded **parser should accept valid odd
rectangles**; a particular 3D consumer may still reject a size or request an
explicit conversion if its renderer/use case requires it. A cheap header probe
can let that consumer decline an unsuitable image before full pixel decoding,
without imposing its restrictions on every ACE user. Do not silently resize the
source document to satisfy one consumer.

| Fixture / check | What it distinguishes | Available now / future work |
| --- | --- | --- |
| Stock odd rectangles, e.g. `Gui/DriverAids/OpsSldBk.ace` 312×15 | Valid bitmap versus TSRE even-dimension restriction | Present in stock census. |
| Synthetic 9×3 RGB+mask+alpha, and 1×1 RGB | Width-based ceil mask bytes; independent alpha; short rows | Layout tests present. Pixel decoder tests still to add when TSRE changes. |
| Same planar image, plain and zlib | Envelope independent of pixel layout | Layout tests present. |
| Stock DXT1 mip chain and no-mip `JP2bluebrgtex.ace` | Mip count, offsets, payload sizes, tiny planar tail | Both present. |
| Authored distinct color at every mip | Retained source mips versus automatic GPU-generated chain | Proposed synthetic pixel/visual test. |
| Alpha ramp plus deliberately different binary mask | Preservation versus collapsed RGBA-only round trip | Proposed pixel/round-trip test. |
| Correct versus 4× stride row tables | TSRE writer regression; a sequential reader is not enough | Synthetic rejection test plus supplied `mini/graphic.ace`. |
| Nonsequential but bounded row storage | Reader follows offsets rather than fixed skips | Proposed TSRE test; compatibility policy needs care. |
| DXT3/5; raw RGB565/ARGB1555/ARGB4444 | Added format dispatch and alpha interpretation | No-mip colors rendered in Wine; DXT3/5 and raw ARGB4444 partial-alpha overlays match the planar control. Mips/other devices/fallback remain untested. |
| Paletted RGB/RGBA with 2/4/16/256 entries | Actual indexed profile and native scratch/caller restrictions | 256-entry RGB/surface-4 probe rendered correctly. Candidate RGBA/surface-12 probe rendered incorrectly; other layouts remain untested. |
| Truncated header/payload/zlib; bad count/offset/size | Safe error paths and allocation bounds | Research inspector tests present; TSRE needs equivalent tests. |
| Existing Photoshop footer, then edited pixels | Opaque preservation versus stale thumbnail metadata | 416 stock examples; inspector parses the resource records. |

For further live MSTS testing, use the isolated Wine workflow where
appropriate. **Windows-host access still requires explicit approval**; preparing
fixtures or documenting a test is not approval to launch anything on the host.

## 10. Verification performed and remaining unknowns

The read-only `scripts/inspect_msts_ace.py` parses the
layout, validates referenced image spans and raw sizes, and recognizes Photoshop
resources. It does not decode pixel colors or execute MSTS. Its resource limits
are inspector safeguards, not inferred MSTS limits.

| Corpus | Files | Readable image layouts | Exceptions | Parsed Photoshop footers |
| --- | --- | --- | --- | --- |
| Extracted MSTS 1.0 game cabinet | 508 | 508 | None | 416 |
| Supplied installed app, including repack/user route assets | 1252 | 1250 | `mini/graphic.ace` bad offsets; `mini/textures/jp1signals.ace` corrupted/replaced content, unlike the valid template | 870 |

The app set is **not** a second independent stock sample: it includes duplicates
and later/custom files. No palette-bearing file, DXT2–DXT5 file, or packed raw
16-bit file was found. Stock observed surfaces are 14, 16, 17 and 18. Stock
options are 0, 1, 16 and 17; the app set additionally has 4, 5 and 21. A readable
layout here is not a certificate of successful rendering.

Nineteen synthetic inspector tests pass, covering plain/zlib, descriptor order,
odd/one-pixel dimensions, mip tables, DXT1 tails, a DXT3-sized raw payload,
palette records, resource blocks and several malformed-input cases. They test
the research parser, **not the current TSRE executable**.

Four additional fixture-validation tests pass, including independent DXT
payload decoding through ImageMagick's DDS reader. The Wine probe report (`reports/msts-ace-wine-format-probes.md`)
separately records actual rendered results and the unsuccessful RGBA-palette
candidate. Generated probes are not added to the supplied-file census totals.

Remaining uncertainties are explicit rather than labelled “unused”:

- exact original names and intended semantics of header `0x6C`, `0x70`, `0x80`
  and `0x84–0x97`, and the final visual/units meaning of float `0x78`;
- full indexed/special surface enumeration and valid channel combinations;
- DXT2/4 runtime behavior, and DXT3/5/packed-16 compatibility beyond the tested
  no-mip Wine cases: mip chains, other material/device configurations and
  MSTS's own software/fallback paths;
- arbitrary noncanonical channel order, multiple palettes, rectangular mip
  chains, and device-specific maximum dimensions;
- meaning of non-resource bytes between some image payloads and Adobe footers.

### Reproduce / resume

```bash
python scripts/test_inspect_msts_ace.py -v
python scripts/inspect_msts_ace.py proprietary/extracted/msts-1.0-gamecab
python scripts/inspect_msts_ace.py proprietary/msts_app/ROUTES/mini/graphic.ace
python scripts/audit_msts_ace_executable.py
```

Use `--output NEW_PATH.json` for a new metadata file; the tools deliberately
refuse to overwrite an existing output. The stored censuses are
`analysis/ace/stock-census.json` and
`analysis/ace/app-census.json`.

Native evidence, with exact specimen hashes embedded in each export:

| Evidence file | Relevant functions / purpose |
| --- | --- |
| `analysis/pe/ghidra-ace-reader-review.txt` | Header/channels, row sizing, offset skipping, terrain C-buffer use |
| `analysis/pe/ghidra-ace-header-callers.txt` | Texture entry `0x006aae00`, bitmap entry `0x006acd00`, native rewrite |
| `analysis/pe/ghidra-ace-layout-and-writer.txt` | Palette/offset records, raw reads, header/row writer and offset backpatching |
| `analysis/pe/ghidra-ace-extra-record-callers.txt` | `0x006abf50`: load channels, palettes, then offset tables |
| `analysis/pe/ghidra-ace-palette-consumer.txt` | `0x006ac080`: RGB/RGBA palette interpretation |
| `analysis/pe/ghidra-ace-row-callers.txt` | `0x006ac320`: ordinary raw, structured and tiny-mip read branches |
| `analysis/pe/ghidra-ace-texture-consumers.txt` | DXT fallback/tiny levels, structured upload and surface selection |
| `analysis/pe/ghidra-ace-texture-construction.txt` | `0x006b69a0`: map engine access hints to DirectDraw surface capabilities |
| `analysis/pe/ghidra-ace-writer-callers.txt` | Native rewrite and terrain C-buffer writer examples |

Ghidra's recovered calling conventions are imperfect: file read/seek calls have
register arguments missing from some pseudocode. Sizes, destination offsets,
absolute-versus-relative seeks and float copies were checked against native
disassembly before assigning the layouts above.
