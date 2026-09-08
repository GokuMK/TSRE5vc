# New TSRE ACE library: implementation and live format tests

Work date: 2026-09-07/08. TSRE base revision:
`4faa54cd3ee63aa7de541149e1fdb3196846e02c`. Implementation is in the TSRE source tree, not an MSTS executable patch.

Mirror synchronized: 2026-09-08. Evidence paths under `analysis/`, `scripts/`,
`proprietary/`, and the Wine `logs/` refer to the separate MSTS research workspace;
the binary specimens, captures, logs and scripts are not bundled in this mirror.

## Implementation

The production `AceLib` now wraps a separate CPU-only `AceDocument` and
`DxtCodec`. The original synchronized loader/writer is retained as
`AceLibLegacy`. Normal TSRE dispatch uses the new library; malformed inputs do
not silently fall back to the unchecked old reader.

The [API and integration guide](../features/ace-library.md)
documents rendering, CPU-only sources, document preservation, writing, mipmaps,
memory ownership and remaining work. Changes also cover `Texture`, TexLib's
content replacement, and the latest procedural terrain source/bake integration.
Procedural baking calls the general QImage writer with explicit RGB options;
the temporary `saveRgbChecked()` API remains only in the legacy class.

The new parser follows actual offset tables, reads full channel/palette records,
keeps independent mask/alpha data, handles DXT tiny planar mip tails, and bounds
input/inflated/payload allocations. A narrowly identified historical TSRE RGB
4× row-offset error can be recovered with a warning; strict readers can disable
that recovery. Writes use atomic `QSaveFile` commits.

Normal GPU uploads release CPU pixels, encoded blocks and authored-mip staging.
Full document preservation is explicit opt-in, and preserving an additional exact
file envelope is a separate opt-in. `editable` still means CPU pixels are ready
now. Editing invalidates old encoded payloads/mips; mip regeneration is opt-in.
This does not claim to redesign the entire old cache/deferred GPU lifetime.

## Verification artifacts and scope

- `tests/ace/tsre_ace_tool` links the real old/new loader, document, codecs and
  Texture upload/readback implementation. Game/Undo/Brush support is isolated;
  the file and OpenGL paths themselves are not mocked.
- Stock scan: all **508 extracted stock ACEs decoded successfully**.
- Initial writer fixture matrix: **182 files**, 14 encodings, powers of two from
  64 through 4096; mips on/off except indexed generation. The independent Python
  inspector recognized all 182 layouts without structural errors. This does not
  itself certify visual interoperability.
- Generated files and manifest:
  `proprietary/wine-lab/ace-v2-fixtures/`. The initial RGBA-palette fixture is
  retained as negative evidence; a separately named corrected-mask fixture and
  its `.ace.json` receipt record the later layout test.
- Layout census: `analysis/ace/tsre-v2-generated-census.json`.
- The initial concurrent-load benchmark is preserved as
  `analysis/ace/tsre-v2-benchmark-initial.json`. Its highly variable large-image
  timings are **not the final performance verdict**. The first attempted quiet
  run (`tsre-v2-benchmark-quiet.json`) also overlapped compiler children: stopping
  their parent process group did not stop Ninja's separately grouped children.
  It is therefore not a valid quiet baseline either.

The standalone tests exercise plain/zlib envelopes, record round trips, all
implemented codecs, odd/tiny/rectangular images, independent mask/alpha,
truncations and bounded mutations, editing readiness, mip invalidation and
move/reload ownership. Sanitizer testing exposed an existing unaligned miniz
integer-read macro; this was replaced with constant-size `memcpy` reads rather
than suppressing the finding.

Latest standalone verification: **7,539 CPU/API checks, zero failures** in both
Release and ASan/UBSan (`detect_leaks=1`, `halt_on_error=1`); **1,062 OpenGL checks,
zero failures**, including upload/readback through 4096 and source-buffer release.
The independent Python writer suite passes all three tests (with multiple format,
mip and envelope subcases). Stock scan remains 508 successes, zero failures.

The full Release application builds and passes **296 terrain-material checks,
zero failures**, plus the `terrain-material-gl` suite with **zero failures**.
Those final runs used the ordinary executable, not debugger overrides. They
exercise bake writing/reloading, missing/obsolete bake recovery, source refresh,
bounded background loading/upload, cross-tile sharing and final-owner release.
Expected failure-injection warnings in the CPU log are not test failures.

Logs: `analysis/ace/tsre-v2-terrain-cpu.log` and
`analysis/ace/tsre-v2-terrain-gl.log`. Binary/source hashes and final counts are
in `analysis/ace/tsre-v2-verification.json`.

The initial terrain-suite run failed because TSRE's default path lowercasing
changed mixed-case temporary-directory names on Linux. A scoped test-only
`caseInsensitiveFS=false` fixes that; production filesystem settings are not
changed. The local full-app build directory also emitted Ninja dependency-cache
recovery warnings on incremental invocations. A successful completed build is
verified here, not a claim that this particular cache performs clean no-op
rebuilds. An unnecessary repeat build was stopped after the successful link;
the recorded final executable then passed both application suites.

### Performance

Final Release/GCC 16.2, Qt 6.11.2, warm-cache medians, same process/allocator,
alternating old/new order, two warmups and 15 measured loads (seven at 2048/4096).
The main build and **all three compiler children were confirmed stopped** for
these measurements; no Wine/GL test was running. Compilation resumed afterward.
Artifacts: `analysis/ace/tsre-v2-benchmark-final.json` and
`analysis/ace/tsre-v2-writer-final.json`.

| Profile | Size | Legacy base, ms | New base, ms | New with authored mips, ms |
| --- | ---: | ---: | ---: | ---: |
| RGB | 1024 | 7.54 | 4.08 | 4.71 |
| RGB | 2048 | 29.23 | 34.50 | 36.99 |
| RGB | 4096 | 227.75 | 244.85 | 228.90 |
| RGB + mask | 1024 | 8.13 | 4.96 | 5.51 |
| RGB + mask | 4096 | 281.05 | 269.25 | 318.78 |
| RGBA | 1024 | 9.10 | 4.99 | 5.67 |
| RGBA | 2048 | 33.60 | 39.56 | 41.64 |
| RGBA | 4096 | 303.17 | 294.45 | 298.33 |
| DXT1 | 1024 | 0.146 | 0.146 | 0.142 |
| DXT1 | 4096 | 3.61 | 3.94 | 4.12 |

The new planar reader is substantially faster through 1024. Large-image
allocations/copies dominate, with modest regressions in some cases: approximately
18% / 5–6 ms for base RGB/RGBA at 2048 and 8% / 17 ms for RGB at 4096.
Small DXT1 files have a few microseconds of extra structural-validation overhead.
Authored-mip loading does extra work absent from the old reader; it is shown
separately, not presented as an identical-work comparison. Allocator/cache
variation can even make a mip median slightly lower than base-only; that is not
a promise that extra mips are free. No multi-fold load regression remains in
this controlled run. These are local measurements, not Windows timing claims.

RGB bake writing, including QImage conversion and atomic disk output:

| Size | Legacy checked RGB writer, ms | New general QImage writer, ms |
| ---: | ---: | ---: |
| 256 | 1.84 | 1.35 |
| 512 | 6.61 | 6.28 |
| 1024 | 23.69 | 23.25 |
| 2048 | 77.41 | 76.27 |
| 4096 | 330.24 | 322.92 |

The new bake API does not show a writer regression in this run. The comparison
uses the recent checked RGB implementation, not the much older per-byte writer
with incorrect row offsets. New encoders without a legacy counterpart are not
assigned misleading old/new speed ratios.

## MSRE execution environment

Only Linux-local isolated Wine was used. **No Windows host executable, registry,
mount or desktop was accessed.** The existing lab launcher uses a private
prefix/Xvfb, Wine 11.17 builtin DDraw/D3D and Mesa LLVMpipe software rendering.
Private route `ace_alpha` places a single 20 m Transfer over grey terrain, with
fixed camera coordinates. This tests transparency that the opaque terrain shader
would hide. The fixture's four quadrants use alpha 255, 170, 85 and 0.

Executable: private `train-wine-r64-v5.exe`, SHA-256
`97b4ceda684b447a69878dcdc13e37af8783e2e35103ab6ed48cba8f4a1cfaf0`.
The previously audited 20 ACE function ranges and five DXT mappings are unchanged
from the clean Bin 1.8 specimen. No ACE-specific executable patch was made.

Runner: `scripts/msts_ace_v2_probe_run.sh` in the MSTS research workspace, invoked
inside `msts_wine_lab.sh`. Every case records input hashes, UI inventory, capture,
application log and confirmed-exit receipt. “CAPTURED” is not a pass verdict:
the images are inspected separately. Only generator-owned private route textures
are switched; original supplied assets are not overwritten.

Evidence naming:

```text
proprietary/wine-lab/captures/ace-v2-alpha-CASE-auto.png
proprietary/wine-lab/logs/ace-v2-alpha-CASE-inputs.txt
proprietary/wine-lab/logs/ace-v2-alpha-CASE-auto.log
proprietary/wine-lab/logs/ace-v2-alpha-CASE-result.txt
```

### 4096×4096, no mip chain: inspected results

| Fixture | MSRE appearance |
| --- | --- |
| `rgb_4096` | Correct opaque quadrants |
| `mask_4096` | Correct binary transparency |
| `rgba_4096` | Correct partial alpha, including visible grey through the blue quadrant |
| `raw565_4096` | Correct opaque quadrants |
| `raw1555_4096` | Correct binary transparency |
| `raw4444_4096` | Correct partial alpha |
| `dxt1_4096` | Correct opaque quadrants |
| `dxt1mask_4096` | Correct binary transparency |
| `dxt2_4096` | Loads/renders, but partially transparent colors are too dark |
| `dxt3_4096` | Correct partial alpha |
| `dxt4_4096` | Same darkening qualification as DXT2 |
| `dxt5_4096` | Correct partial alpha |
| `palette_rgb_4096` | Correct opaque indexed quadrants |
| `palette_rgba_4096` | Negative control: missing mask causes broken/dither-like areas |
| `palette_rgba_mask_4096` | Adding the mask removes corruption; partial palette alpha is reduced to cutouts in this native path |

“Correct” here means the deliberately simple quadrant test behaves as intended,
not exhaustive proof for every texture/shader/driver combination. DXT compression
quality on arbitrary artwork is a separate encoder concern. The new TSRE CPU
codec normalizes standard DXT2/4 premultiplication; the native transfer path's
dark result must not be “fixed” by falsely labeling straight-alpha data DXT2/4.

Also visually inspected successfully: DXT5 at **64, 128, 256, 512, 1024 and 2048**;
4096 DXT5 and planar RGBA with mip chains; zlib-wrapped 4096 RGBA, and
zlib-wrapped 4096 DXT5 with mips. The 182-file matrix was not exhaustively run
through MSRE. Interior-pixel measurements and capture hashes for all **26**
screenshots are in `analysis/ace/tsre-v2-capture-pixels.json`; those four-point
measurements supplement visual inspection, not whole-image certification.

### Evidence that 4096 is actually requested

For `dxt5_4096`, the DDraw trace records:

- `17668.704`: `DDSD_HEIGHT : 4096`, `DDSD_WIDTH : 4096`, FourCC `DXT5`.
- The surface is created, locked and filled successfully.
- `17669.228`: the lock returns 4096×4096, FourCC `DXT5`, linear size
  **16,777,216 bytes**, exactly a 4096-square BC3 base level.

See `logs/ace-v2-alpha-dxt5_4096-auto.log`, around lines 18683–18706.
Thus this was not merely file acceptance followed by a silently capped 2048
surface request. It disproves a universal 2048 ACE-reader limit in this setup;
it does not establish a universal maximum for original Windows drivers/hardware.

## Indexed layout finding from this implementation pass

At Bin VA `0x6ac394`, surface 12 maps through `0x7a7538` to conversion source
class 2. Example 32-bit destination converter `0x6aa910` reads index bytes from
global plane 0 and packed mask bits from global plane 1. It copies the selected
palette entry when the mask bit is set and writes zero when clear. The original
index-only candidate left that second source plane undefined.

The revised fixture supplies channel ID 1 / 8-bit indices and channel ID 2 /
1-bit mask, plus a type-8 RGBA palette. The mask defaults to all ones so palette
alpha remains independent in the document. Native surface selection still
reduces the tested partial alpha to binary transparency. Accordingly, the writer
profile is not advertised as a native full-alpha replacement for planar RGBA or
DXT3/5. Surface 4 plus a type-7 RGB palette is the verified opaque indexed profile.

This narrows the earlier unresolved palette result; it does not erase the old
negative captures or claim that every palette count/bit depth is supported by
MSTS.
