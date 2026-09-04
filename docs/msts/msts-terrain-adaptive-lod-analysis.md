# MSTS adaptive terrain LOD: executable analysis

Status: **static-analysis milestone; one user-supplied MSRE observation**
Date: 2026-09-01

## Executive result

The MSTS patch-v1.4 executable contains a recursive, error-driven terrain
selection path. Patch `ErrorBias` is read as a float, defaults to `1.0`, and is
multiplied into the effective selection threshold. The product is squared
before the recursive sample test. No `ErrorBias == 0` special-case or bypass was
found on this path. Consequently, zero is a zero tolerance, not an explicit
request to skip E or force a prebuilt full-resolution mesh. Exactly zero-error
samples may still stop refinement.

The executable also consumes E and AS dynamically from `terrain_nsamples`.
E is an `N x N` float32 file expanded to `(N+1) x (N+1)` in memory by copying
the final column and row. AS remains bit-packed and is indexed LSB-first over
the same `(N+1)` row stride. AS can force recursion when the ordinary E test
would stop, subject to distance/depth gates.

Dynamic buffer loading does not imply unrestricted terrain dimensions. A later
MSTS registration stage explicitly rejects `terrain_nsamples > 256` and
rejects a patch set when
`terrain_nsamples / terrain_patchset_npatches > 16`. These guards exactly
explain the user-observed failures of 256-sample/8-patch,
256-sample/4-patch, and 512-sample/16-patch tiles.

The user's later `N=128`, 16 m spacing, `P=16` (`R=8`) fixture loads in
MSRE. That positive observation agrees with the executable data flow and
confirms that MSTS is not fixed to `N=256` or 8 m samples. It was performed
by the user on Windows; this WSL analysis did not access or run Windows.

The executable conclusions come from Linux-only static analysis. This review
used no Windows access or execution; the positive MSRE result above is a
user-supplied observation. Runtime render output and complete E-value units
remain open. Cross-tile copying of F/Y/E/N/C/D edge samples is now confirmed,
while the selector's crack-prevention/dependency rules are only partly
decoded.

A second data-flow pass also corrected an earlier inference about
`terrain_alwaysselect_maxdist` (138). MSTS parses it as float32, caches its
square, and serializes the original value, but no read of either tile field was
found outside parse/save. The selector's two distance fields are initialized
instead from named executable settings `asnear` and `asmax`, whose defaults are
350 and 700. Their squares are copied directly into the terrain manager. This
report therefore concludes that `terrain_alwaysselect_maxdist` (138) does not
control the recovered selector in this build.

The route-level `TerrainErrorScale` (1230 in TSRE/Open Rails numbering; MSTS
application-token value `0x403a2`) is active and is now traced end to end. MSTS
loads and saves it as float32. During rendering it multiplies the named
`trterrain_errthreshold` setting, whose default is 7; MSTS clamps that product
to the range 7 through 50 and passes it into the terrain threshold setup.

## Scope and identity

Analyzed module:

- private path: `proprietary/extracted/official-update/train.exe`
- SHA-256: `730b5054adc73c2cbfb0b3eb6eb2d9d95ae339fcc3922fe74cb318d747319584`
- size: 4,091,953 bytes
- PE32/i386, image base `0x00400000`, entry RVA `0x0031edf8`
- embedded description: `Microsoft Train Simulator Patch v1.4`

The source-media and extracted-corpus provenance is in
`analysis/manifests/base-corpus.md`; complete PE metadata is in
`analysis/pe/msts-official-update-train.json`. GNU Binutils supplied the initial
disassembly. After explicit approval for a full Arch update, Ghidra 12.1.2 with
OpenJDK 26 completed whole-program headless analysis in 246 seconds. Its
recovered function bounds, direct references, and decompiler output independently
cross-checked the principal data flows below. `bsdtar`, `file`, `sha256sum`,
Python, and `rg` supported static extraction and inventory. See `TOOLS.md` for
initial and final versions and the first, atomic installation failure.

No synthetic route was executed by this analysis and no proprietary fixture
was modified in this phase. The one runtime result in this revision is the
user-reported successful MSRE load of the `N=128`, `P=16` fixture. There are
no captured screenshots, draw counts, file-access traces, or pre/post runtime
hashes yet.

## Recovered data flow

### File loading

At VA `0x006d0700` (RVA `0x002d0700`), the E loader obtains `N` from the terrain
sample structure at `+0x3c`, allocates `(N+1)^2 * 4` bytes, and reads `N` rows of
`N * 4` bytes. For every row it copies the last float to the extra column, then
copies the last completed row to the extra row. The resulting float grid is
stored at terrain offset `+0x6c`.

At VA `0x006cec30`, the AS reader computes
`ceil((N+1)^2 / 8)` and reads that many bytes into terrain offset `+0x44`. The
parallel US reader at `0x006cec60` uses offset `+0x48`.

Recovered E expansion pseudocode:

```text
side = terrain_nsamples + 1
E = allocate(side * side * sizeof(float))
for z in 0 .. terrain_nsamples-1:
    read terrain_nsamples little-endian float32 values into E[z * side]
    E[z * side + terrain_nsamples] = E[z * side + terrain_nsamples - 1]
copy E[(terrain_nsamples - 1) * side : terrain_nsamples * side]
  to E[terrain_nsamples * side : (terrain_nsamples + 1) * side]
```

This disproves a fixed 256/257 assumption in these **MSTS executable** loaders.

### Non-standard `terrain_nsamples`

The same MSTS executable also loads `terrain_sample_ybuffer` dynamically. At
VA `0x006d0400`, it reads exactly `terrain_nsamples * terrain_nsamples * 2`
bytes as little-endian unsigned 16-bit values, converts them to float, and
duplicates the final column and row to form an `(N+1)`-square runtime grid.
The read must return the complete requested byte count or tile loading fails.

The audited MSTS buffer paths have these dimension rules:

| Full token name | File/embedded payload selected by MSTS |
|---|---:|
| `terrain_sample_fbuffer` | `N*N` bytes, expanded to `(N+1)^2` |
| `terrain_sample_ybuffer` | `N*N*2` bytes, expanded to `(N+1)^2` float values |
| `terrain_sample_ebuffer` | `N*N*4` bytes, expanded to `(N+1)^2` float values |
| `terrain_sample_nbuffer` | `N*N` bytes, expanded to `(N+1)^2` |
| `terrain_sample_cbuffer` | image dimensions must be exactly `N*N`; decoded channels are packed into an `(N+1)^2` grid |
| `terrain_sample_dbuffer` | `N*N` bytes, expanded to `(N+1)^2` |
| `terrain_sample_asbuffer` | `ceil((N+1)^2/8)` embedded bytes |
| `terrain_sample_usbuffer` | `ceil((N+1)^2/8)` embedded bytes |

Here `N` is `terrain_nsamples`. No literal 256, 257, or 16 controls these
individual MSTS buffer loaders. Patch-record allocation is likewise derived
from `terrain_patchset_npatches` in the audited parser. This does **not** mean
that MSTS accepts arbitrary dimensions end to end.

A later terrain-registration stage imposes explicit limits. The patch-set
parser at `0x00710630` computes
`R = terrain_nsamples / terrain_patchset_npatches`. `FUN_006ee1c0` rejects a
terrain manager when the largest `R` is greater than 16 (`0x006ee23b`) or the
largest `terrain_nsamples` is greater than 256 (`0x006ee253`). The initial
terrain-loading path `FUN_006bdd50` repeats the two comparisons at
`0x006bde74` and `0x006bde83`. It cleans up the partial object and returns zero
on failure.

Therefore the recovered end-to-end necessary conditions include:

```text
terrain_nsamples <= 256
terrain_nsamples / terrain_patchset_npatches <= 16
```

This explains the rejected Route Editor results without a Windows capture:
`256/8` gives 32 samples per patch, `256/4` gives 64, and `512/16` gives 32
while also exceeding the total-sample limit. All are explicitly rejected.
`256/16` gives 16 and passes these two guards. A smaller grid such as `64/4`
also gives 16, so the executable does not require a literal 16x16 patch grid in
every file.

The user's successful `128/16` fixture gives `R=8` and supplies positive
runtime confirmation of that last point. A separate follow-up found that the
`R <= 16` guard protects a 17-by-17 vertex cache, a fallback mesh sized for
16-by-16, and 1,536-index renderer thresholds. Consequently, bypassing only
the guard is unsafe, but an `R=32` experiment appears to require a bounded set
of localized changes. The detailed follow-up was recorded in the earlier local
`msts-r32-n512-patch-feasibility.md` report, which is not retained here.

These conditions are necessary, not sufficient for arbitrary inputs. MSTS
derives hierarchy levels from N and the patch dimensions, and its error
generation and selection walk power-of-two steps. A non-power-of-two layout is
therefore structurally unsafe even if it reaches the parser. The parser also
accepts `terrain_sample_size` only when its rounded value is an exact power of
two, and the C-buffer image loader explicitly requires power-of-two, N-by-N
input. All present auxiliary buffers must be resized coherently.

The custom-file audit and exact guard details are in
[`msts-custom-terrain-grid-compatibility.md`](msts-custom-terrain-grid-compatibility.md).

### Tile and patch controls

The terrain root parser begins at VA `0x006ee347`. Its dispatch reads
`terrain_errthreshold_scale` (137) into tile offset `+0x30`. Object
construction at `0x006edc60` initializes this field to float `1.0`.
`terrain_alwaysselect_maxdist` (138) is read into `+0x34`, and its square is
cached at `+0x38`. The matching serializer at
`0x006f349a`/`0x006f355e` writes the same named fields. Thus these mappings are
confirmed:

| Token | Field | Runtime offset | Static result |
|---:|---|---:|---|
| 137 | `terrain_errthreshold_scale` | `+0x30` | float, default `1.0`; consumed by patch LOD |
| 138 | `terrain_alwaysselect_maxdist` | `+0x34` | float; square cached at `+0x38`; no downstream consumer found in recovered terrain paths |
| 164 | patch `ErrorBias` | patch record `+0x44` | float, default/fallback `1.0` |

Patch records are 76 bytes (`0x4c`) and are allocated dynamically from the
declared patch grid. The patch parser at VA `0x006f0b50` reads `ErrorBias` into
`+0x44`; initialization at `0x006f0ad0` supplies `1.0`.

### Threshold and recursive selection

The patch-level LOD setup at VA `0x006f11d0` executes, in order:

```text
t = patch.ErrorBias              # [patch + 0x44]
t *= tile.errthreshold_scale     # [tile + 0x30]
t *= camera_or_display_factor    # global at 0x0082d76c
lod_threshold_squared = t * t    # global at 0x0082d6a0
```

The global factor is updated near VA `0x006bf4a2` from camera/display state.
The complete recovered input path is:

```text
route TerrainErrorScale = route[+0xb4]                 # float32
user threshold = trterrain_errthreshold                # default 7
effective threshold = clamp(user threshold * route TerrainErrorScale, 7, 50)
camera factor = effective threshold / camera[+0x84]
lod threshold = ErrorBias * terrain_errthreshold_scale * camera factor
lod threshold squared = lod threshold * lod threshold
```

The route parser branch at `0x004ae6db` recognizes MSTS application token
`0x403a2`, corresponding to `TerrainErrorScale` (1230 in TSRE/Open Rails
numbering), and reads a float into route offset `+0xb4`. The serializer at
`0x004afd48` writes the same field as float32. `FUN_005310e1` multiplies it by
the `trterrain_errthreshold` setting and clamps the result to 7 through 50. The
render loop at `0x004902e2` passes that result through `FUN_0053314d` to
`FUN_006bf450`, which divides it by camera field `+0x84` and stores the factor
consumed by terrain LOD. The command-line option parser also accepts
`errthresh` for this setting. The precise public meaning/units of camera field
`+0x84` remain open, but the route-scale connection itself is confirmed.

The recursive selector begins at VA `0x006cd4e0`. Its linear sample index is
equivalent to `z * (N+1) + x`; it retrieves E through terrain offset `+0x6c`.
For a camera-to-sample vector, the ordinary stop condition recovered by Ghidra
is equivalent to:

```text
E[index] * horizontal_distance_squared
    < full_3d_distance_squared^2 * lod_threshold_squared
```

Otherwise the function refines into four child positions. The local E generator
at VA `0x006d1510` computes a midpoint elevation interpolation residual and
squares it. The propagation function replaces parent values with neighborhood
maxima. Therefore the purely geometric component of E is squared elevation
error in world-coordinate units. An optional four-channel sample buffer at
terrain offset `+0x74` adds a channel-difference term, so the general E metric
is not safely describable as only squared metres without identifying that
buffer and the coordinate scale.

AS is not converted into E. The selector tests bit
`1 << (index & 7)` in byte `AS[index >> 3]`. In multiple branches, an asserted
AS bit can lead to child recursion after the ordinary E result would stop.
Distance and recursion-depth gates surround that override. The distance fields
read by the selector are at manager offsets `+0x4c` and `+0x50`, reached through
the terrain tile's owner pointer. At application setup, MSTS assigns:

```text
manager[+0x4c] = asnear * asnear   # default 350²
manager[+0x50] = asmax  * asmax    # default 700²
```

The startup option parser identifies both settings by the literal names
`asnear` and `asmax`. The selector compares them against the squared
camera-to-sample distance in MSTS terrain coordinate units. On the relevant
branch, `asmax=0` permits an AS-forced result only at exactly zero distance;
for ordinary positions it effectively disables the AS distance override.
`asnear` controls whether the AS bit is consulted at the deepest hierarchy
level, while shallower levels also pass a depth condition.

`terrain_alwaysselect_maxdist` (138) is stored separately at tile `+0x34` with
its square at tile `+0x38`. A whole-path xref review found no data flow between
these locations. Its apparent lack of a consumer may represent vestigial
format state or use in a path/tool not yet recovered.

### Small call graph

```text
terrain/patch update (0x006f11d0)
  -> build squared threshold from ErrorBias,
     terrain_errthreshold_scale (137), and display factor
  -> recursive selector (0x006cd4e0)
       -> E test at 0x006cd636
       -> AS bit test at 0x006cd5cf and child tests later in the function
       -> pre-recursion bookkeeping (0x006cd100)
       -> four recursive calls

terrain load
  -> E path wrapper (0x006d08f0) -> E loader (0x006d0700)
  -> AS parser (0x006cec30)

terrain error generation (0x006d1400)
  -> propagation stage (0x006d1820)
```

## Answers to the requested questions

| # | Assessment | Answer |
|---:|---|---|
| 1 | **confirmed** | `ErrorBias == 0` does not bypass E lookup/evaluation on the recovered path. It participates in ordinary threshold multiplication and squaring. |
| 2 | **probable** | Zero means zero error tolerance. It is not a literal full-resolution flag; exactly planar/zero-error cases can still terminate under the ordinary comparison. Runtime confirmation is still warranted. |
| 3 | **confirmed** | Positive bias is a continuous multiplier of the linear threshold; after squaring, its contribution to the compared squared term is proportional to `bias^2`. Larger positive bias therefore makes the ordinary test more permissive (coarser), subject to other controls. Negative values lose their sign through squaring, though they are likely invalid authoring inputs. |
| 4 | **confirmed / partial** | Stored E is read as float32, edge-expanded, and used directly by the selector. Generation at `0x006d1400` computes local errors and propagation at `0x006d1820` takes hierarchy/neighborhood maxima; whether normal route loading ever regenerates a valid existing E file is not yet proven. |
| 5 | **confirmed / partial** | Index mapping is row-major `z*(N+1)+x`, with replicated right/bottom boundaries. The base geometric value is a squared midpoint elevation interpolation residual. An optional four-channel residual can be added, so the complete metric/units need that buffer identified. |
| 6 | **confirmed** | AS is tested independently as a packed bit mask and can override an E-based stop; it is not first converted into E. A transient runtime selection structure also exists. |
| 7 | **not found** | Static bit access does not reveal whether the stored AS file already contains dependency closure. |
| 8 | **confirmed unused on recovered path** | `terrain_alwaysselect_maxdist` (138) is parsed as float32, squared into an adjacent cache field, and serialized, but no consumer was found. The actual selector gates are the separate `asnear`/`asmax` executable settings, default 350/700 in terrain coordinate units. `asmax=0` effectively disables the AS distance override except at exactly zero distance; this behavior must not be attributed to `terrain_alwaysselect_maxdist` (138). |
| 9 | **confirmed** | `terrain_errthreshold_scale` (137) is tile offset `+0x30`, default `1.0`, multiplied directly with patch `ErrorBias` at VA `0x006f13e8`. |
| 10 | **confirmed** | Route `TerrainErrorScale` (1230; MSTS application token `0x403a2`) is a float32 at route `+0xb4`. MSTS computes `clamp(trterrain_errthreshold * TerrainErrorScale, 7, 50)` and passes it into the terrain threshold factor. |
| 11 | **confirmed / partial** | The named `trterrain_errthreshold` setting defaults to 7 and has command-line alias `errthresh`. Its route-scaled/clamped value is divided by camera field `+0x84`; that field's exact public meaning and units remain unidentified. |
| 12 | **confirmed / partial** | MSTS synchronizes F/Y/E/N/C/D values at matching adjacent-tile edge samples. A bookkeeping/dependency routine is also called before child recursion, but its exact crack-prevention closure is not fully decoded. |
| 13 | **confirmed** | N is a byte-grid of indices into a 256-entry table of three-float normal vectors. Terrain mesh builders at `0x006f19f0` and `0x006f1ee0` use it for vertex lighting; it is not part of the recovered E predicate. |
| 14 | **confirmed / partial** | `terrain_nsamples` and patch-grid allocation are dynamic in the recovered loaders/parsers. No compiled 256/257/16 assumption controls these routines. Sample spacing is read from structure fields in coordinate calculations, but every downstream constraint was not audited. |
| 15 | **confirmed** | Incidental mechanics include dynamic E/N edge replication, F/Y/E/N/C/D cross-tile edge copying, independent AS/US packed buffers, patch-record defaulting, N-based terrain lighting, midpoint-residual E generation, and hierarchy/neighborhood maximum propagation. Addresses are listed above. |
| 16 | **probable / untested** | The generator walks power-of-two hierarchy levels and selects interpolation endpoints from coordinate parity/orientation, which predicts structured diagonal/triangle patterns. No licensed E fixture was decoded or overlaid in this phase, so the remembered image itself remains unvalidated. |

## Evidence inventory

| Mechanic | Trigger/input | VA | Evidence | Confidence / follow-up |
|---|---|---:|---|---|
| E file load and edge extension | `*_e.raw` | `0x006d0700` | `N` row reads of `N*4`, last-value and last-row copies | confirmed; validate with file-access trace |
| N file load and edge extension | `*_n.raw` | `0x006d0980` | byte-sized parallel loader | confirmed |
| N lighting consumer | terrain mesh build | `0x006f19f0`, `0x006f1ee0` | N byte indexes a 256-entry table of three-float normals used in lighting calculations | confirmed |
| AS packed read | `terrain_sample_asbuffer` (281) | `0x006cec30` | `ceil((N+1)^2/8)` read to `+0x44` | confirmed |
| US packed read | `terrain_sample_usbuffer` (282) | `0x006cec60` | parallel read to `+0x48` | confirmed; consumer untraced |
| Adjacent-tile sample synchronization | matching tile edges | `0x006ef2d0`, `0x006ef5a0` | copies F/Y/E/N/C/D samples across corresponding edges | confirmed copying; D semantics and LOD closure open |
| E generation | missing/invalid cache path unknown | `0x006d1400`, `0x006d1510` | squared midpoint elevation residual, with optional channel residual | confirmed algorithm; trigger unresolved |
| E propagation | generator stage | `0x006d1820` | power-of-two hierarchy walk and neighborhood maxima | confirmed core behavior; dependency details remain |
| Patch bias parsing | `terrain_patchset_patch` (164) | `0x006f0b50` | final float to patch `+0x44`, fallback `1.0` | confirmed |
| Recursive E selection | camera/patch update | `0x006cd4e0` | E lookup, distance comparison, four recursive calls | confirmed |
| AS override | AS bit and distance/depth gates | `0x006cd5cf` | LSB-first bit test feeding recursion | confirmed |
| `asnear`/`asmax` option parsing | executable startup settings | `0x00499bdd` | literal option-name matches populate float globals, default 350/700 | confirmed |
| AS distance-gate initialization | terrain-manager setup | `0x004937bf`, `0x00494850` | writes `asnear²`/`asmax²` to manager `+0x4c`/`+0x50` | confirmed |
| Route `TerrainErrorScale` (1230) load/save | MSTS application token `0x403a2` | `0x004ae6db`, `0x004afd48` | float32 route field at `+0xb4` | confirmed |
| Route/user threshold composition | `TerrainErrorScale`, `trterrain_errthreshold` | `0x005310e1`, `0x004902e2`, `0x0053314d`, `0x006bf450` | multiply, clamp to 7–50, pass to camera-scaled LOD factor | confirmed; camera `+0x84` meaning open |

Raw Binutils excerpts are retained under `analysis/pe/`, including the E loader,
root parser/serializer, AS parser, patch parser, threshold setup, selector, and
generator. The bounded Ghidra export is
`analysis/pe/ghidra-terrain-functions.txt`; its reproducible exporter is
`scripts/ghidra/ExportTerrainFunctions.java`. The broader second-pass evidence
is `analysis/pe/ghidra-terrain-second-pass.txt`; its exporter is
`scripts/ghidra/ExportTerrainSecondPass.java`. The negative
`terrain_alwaysselect_maxdist` (138) review is supported by
`analysis/pe/ghidra-terrain-control-offset-scan.txt` and
`scripts/ghidra/ScanTerrainControlOffsets.java`. These are evidence artifacts,
not source reconstructions.

## Discarded or narrowed hypotheses

- `ErrorBias == 0` is an explicit bypass: contradicted on the recovered path.
- E is always loaded as a fixed `256 x 256` field: contradicted by dynamic `N`.
- E's runtime grid remains `N x N`: contradicted by explicit boundary expansion.
- AS uses MSB-first bits or an `N`-wide stride: contradicted by the selector.
- AS is folded into E before selection: contradicted by direct independent bit
  tests.
- `terrain_sample_usbuffer` (282) is another name for the AS mask: contradicted
  by separate storage and direct AS-only reads in the recovered selector.
- The selector's visible distance comparisons prove the semantics of
  `terrain_alwaysselect_maxdist` (138): narrowed/withdrawn. They read separate
  manager fields initialized from `asnear` and `asmax`, and no
  `terrain_alwaysselect_maxdist` (138) data flow to those fields was found.
- Route `TerrainErrorScale` (1230) is merely stored metadata or the same field
  as tile `terrain_errthreshold_scale` (137): contradicted. It is a separate
  float input which multiplies the named user threshold before the camera and
  tile/patch factors are applied.

## Next work and capture decision

Windows capture is not the next required step. The remaining productive static
targets are:

1. Identify the public meaning and units of the camera field `+0x84` used as
   the final divisor in the threshold factor.
2. Finish the low-bit F dependency analysis and the trigger which regenerates E.
3. Resolve US and D consumers, C-buffer use beyond E generation, and the
   remaining terrain material-state values.
4. Search for a coherent non-standard-N, rare-buffer, or multi-patch-set
   fixture before designing any behavioral test.

If later evidence justifies Windows, use a separate, narrow task. The strongest
candidate is a controlled Route Editor edit/save followed by complete
before/after hashes and binary diffs. A human can perform the UI actions on the
host installation; automated hashing and logging would improve evidence quality,
but neither is useful without a prepared copied fixture. A
`terrain_alwaysselect_maxdist` (138) watchpoint would now only validate the
static negative result and is not recommended for semantic discovery. A VM is
desirable for isolation, not technically required. No such task has been run,
and Windows access still requires explicit approval.
