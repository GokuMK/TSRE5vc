# ACE library and TSRE integration

Implementation date: 2026-09-08. This replaces the production ACE reader/writer
on top of source revision `4faa54cd3ee63aa7de541149e1fdb3196846e02c`.
The old implementation remains in `AceLibLegacy.h/.cpp`, renamed but otherwise
kept as the comparison implementation. There is **no automatic legacy fallback**
for a file rejected by the new bounded parser.

## Layers

| Layer | Responsibility | Dependencies |
| --- | --- | --- |
| `AceDocument` | SIMISA envelope, header, descriptors, palettes, offsets, mip payloads, CPU decode and atomic file writing | Qt Core; bundled miniz for bounded inflation |
| `DxtCodec` | DXT1–5 block decoding/encoding, including DXT2/4 premultiplication | Qt Core; no GPU |
| `AceLib` | Existing QThread loader API plus checked synchronous adapters for `Texture` and `QImage` | Document layer; TSRE `Texture` |
| `Texture` | Transient upload staging, OpenGL upload/readback, editing and mip invalidation | Qt OpenGL and TSRE editor |
| `TexLib` | Existing cache, aliases, file-type choice and loader dispatch | Unchanged public cache API |

ACE is structured, but not a nested `.t`/`.w` token stream. The document exposes
the fixed 152-byte header, 16-byte channel descriptors, 12-byte palette
descriptors, independent image levels and trailing resources. Header labels in
the [format and legacy TSRE audit](../msts/tsre-msts-ace-file-field-usage.md)
are descriptive names, not MSTS token names. That report's TSRE support gaps
describe `AceLibLegacy`, not this replacement library.

## Rendering and CPU-only loading

`AceDocument::decodeThumbnail(level, width, height, bgra, error)` provides a
separate bounded thumbnail path. It returns top-down premultiplied BGRA using
area filtering, decodes at most four source rows at once, and leaves output
unchanged on failure. Output dimensions must be 1..1024 and no larger than the
chosen source level. The caller chooses the mip and aspect ratio. Encoded
payloads remain in the document; this API does not stream the file envelope.
TSRE's existing `decode`/`decodeInto`, texture upload and rendering paths are
unchanged. The Explorer provider uses this API for textures up to 8192 x 8192.

Existing `TexLib::addTex(...)` calls automatically use the new `AceLib` for ACE.
No caller has to rename the loader. Its worker does not call OpenGL. It stages
authored lower mip levels because the later rendering consumer may request them.

For a synchronous renderer-owned texture:

```cpp
Texture texture(path);
AceLoadOptions options;
QString error;
if (!AceLib::load(path, texture, options, error)) {
    // Report error; target content is unchanged on parse/decode failure.
    return;
}
// On the owning GL context thread:
if (!texture.GLTextures(true)) { // false means base level only
    // texture.errorMessage describes a diagnosed upload failure.
    return;
}
```

For a procedural generator, preview conversion or another CPU-only consumer:

```cpp
AceLoadOptions options;
options.cpuPixels = true;
options.stageMipmaps = false;
options.quality = 1; // Full source resolution, independent of render quality.
if (!AceLib::load(path, texture, options, error))
    return;
// texture.imageData is tightly packed RGB/RGBA8; texture.editable is true.
// No upload or GL readback has occurred.
```

`quality` is a positive rendering downsample divisor (values below 1 normalize to
1). A matching authored mip is used when available; otherwise the adapter uses
nearest-neighbour reduction. Dimensions never collapse to zero. This is a
rendering policy, not a modification of an explicitly retained source document.

The latest procedural terrain source loader and background baked-texture loader
now use the explicit CPU-only path. Their sources remain full resolution even
when ordinary rendering uses a lower `Game::textureQuality`.

## Memory and editing contract

`editable` keeps its existing meaning: **CPU pixels are present and ready to
edit now**. It is not an editing-capability request or a promise to keep copies.

| State | CPU storage | `editable` |
| --- | --- | --- |
| Planar/packed ACE decoded before upload | Base RGB/RGBA plus requested transient mip staging | true |
| Compatible DXT ACE staged before upload | Base compressed blocks plus transient mip staging | false |
| Successful rendering upload | Small metadata only by default; no base pixels, blocks or mip buffers | false |
| `decodeToCpu()` before upload, or successful `setEditable()` | Editable base pixels | true |
| Explicit document-preservation mode | Also retains original channel/mip payloads and resources | Independent of that document |

`setEditable()` first uses retained CPU data, then reads back an existing GPU
texture if necessary. It does not force an upload just to obtain CPU pixels.
Without a valid source or current GL context it leaves the texture unready.

After editing raw `imageData`, call `update()` on the GL thread. It invalidates
staged encoded data, authored mips and `aceDocument`, uploads the edited base,
and regenerates mips only if this GPU texture uses mipmaps. For a CPU-only edit
call `pixelsChanged()`; upload can happen later. `paint`, crop/rotation and
`fillData` invalidate stale source state through these same paths.

Default metadata retention includes the complete header and descriptor values;
palette payloads and trailing resources are discarded. Unmodified RGB/alpha
pixels can be recovered from the GPU, but an independent ACE mask, authored
lower mips, Photoshop resources and the exact original compressed envelope
cannot be recovered by readback.

The document and new staging containers use RAII. This change deliberately does
not redesign the entire legacy `TexLib` cache lifetime: existing callers still
own its raw `imageData` and `tex` arrays. In particular, a standalone stack
`Texture` consumer must follow the existing raw-buffer cleanup contract. Use
`AceDocument::decode()` when an automatically owned `QByteArray` is preferable.
`decodeInto()` instead decodes directly into caller-owned, correctly sized
RGB/RGBA storage; that storage must not alias the document and may contain partial
data on failure. The Texture adapter uses this to avoid an extra full-image copy.

## Content-editor / preservation API

A dedicated ACE editor need not construct `Texture` or an OpenGL context:

```cpp
AceDocument document;
AceReadOptions read;
read.retainOriginal = false; // Default: no duplicate original file envelope.
if (!AceDocument::read(path, document, error, read))
    return;

QByteArray rgba, independentMask;
int components;
if (!document.decode(0, rgba, components, error, &independentMask))
    return;

// Unknown header fields, channel/mip payloads and resources survive this rewrite.
if (!document.write(newPath, document.compressedEnvelope, error))
    return;
```

The decoded mask is unpacked, one 0/1 byte per pixel where independently
available. Ordinary RGBA rendering uses the alpha channel when both alpha and
mask exist; the separate mask is not silently multiplied into alpha.
For indexed images the independent mask gates the palette entry's alpha, matching
the native indexed converter; this is distinct from a planar 8-bit alpha channel.

`serialize`/`write` rebuild valid offsets and normalize payload ordering. They
are record/payload preserving, **not byte-exact rewrites** of gaps, original
offset order or zlib bytes. For an exact unmodified-file copy explicitly set
`read.retainOriginal = true` and use `originalBytes()`. That accessor is a saved
original snapshot and does not reflect later document edits.

For a `Texture`-based content editor, set `AceLoadOptions::preserveDocument`.
This retains `texture.aceDocument` and full metadata across GPU upload. Set
`options.reader.retainOriginal` separately only if an additional exact-file copy
is genuinely needed. Editing texture pixels detaches the retained source
document rather than silently saving stale pixels/mips.

Unsupported channel/packing profiles can be preserved structurally where their
record layout is valid, but `decode` returns an explicit error. Unresolved
header values and resource payloads are preserved, not assigned guessed visual
semantics. Photoshop resources are currently opaque bytes, not a resource-editor
object model.

## Writing new or edited images

```cpp
AceWriteOptions write;
write.encoding = AceEncoding::Dxt5;
write.mipmaps = true;
write.zlib = false;
AceDocument document;
if (!AceDocument::fromPixels(pixels, byteCount, width, height, 4,
                             write, document, error))
    return;
if (!document.write(path, write.zlib, error))
    return;
```

Input pixels are tightly packed, straight-alpha RGB/RGBA8. `fromPixels` does not
retain the caller's pointer. `write.mask` optionally supplies a separate base
mask; `headerTemplate` preserves unresolved small header values while controlled
layout fields are rebuilt. Pixel re-encoding clears `ResourceBytes` because it
does not carry an old Photoshop footer into a newly encoded image.

| Encoding | Storage / important qualification |
| --- | --- |
| `Rgb` | Lossless planar RGB8 |
| `Mask` | Planar RGB8 plus independent 1-bit mask |
| `Rgba` | Planar RGB8, mask and full 8-bit alpha |
| `Rgb565`, `Argb1555`, `Argb4444` | Raw packed 16-bit pixels; quantized colors/alpha |
| `Dxt1`, `Dxt1Mask` | DXT1 opaque or binary-alpha blocks |
| `Dxt2`, `Dxt3` | Explicit 4-bit alpha; DXT2 colors are premultiplied on disk |
| `Dxt4`, `Dxt5` | Interpolated alpha; DXT4 colors are premultiplied on disk |
| `IndexedRgb` | 8-bit indices, up to 256 exact RGB palette colors |
| `IndexedRgba` | RGBA palette plus independent 1-bit mask; tested native MSRE path reduces partial alpha to cutouts |

The indexed writer requires at most 256 distinct colors across the whole image
and requested mip chain. It fails rather than silently quantizing to a palette.
The DXT encoder is a deterministic, fast color-endpoint fitter, not an offline
high-quality compressor. Compression is lossy; DXT2/4 also lose hidden RGB where
alpha is zero. Writer-generated mips use a simple 2×2 box filter, not a
gamma-correct, alpha-coverage-preserving or atlas-aware filter. A specialized
authoring tool can supply its own authored levels/blocks via `AceDocument`.

Live MSRE/Wine transfer tests render DXT2/4 partial-alpha colors too dark, despite
standard premultiplied encoding. DXT3/5 and planar RGBA give the intended partial
alpha in that test. Likewise, `IndexedRgba` is **not a native full-alpha substitute**
for those formats; the TSRE decoder preserves its palette alpha, but native MSRE
surface selection does not preserve it in the tested transfer path. Opaque
`IndexedRgb` is verified there. These are observed native-path qualifications,
not reasons to write mislabeled data.

`AceLib::save(path, texture)` keeps the existing convenient API and writes RGB
or full RGBA according to the texture, without mips by default. The overload
accepting `AceWriteOptions` selects a different encoding. A general
`save(path, QImage, options, error)` overload handles QImage conversion/row padding.
Procedural baked fallbacks now call that API with explicit `AceEncoding::Rgb`;
the temporary `saveRgbChecked()` API exists only in `AceLibLegacy`. All new disk writes use `QSaveFile`;
validation/encoding failure does not truncate an existing destination.

## Mipmap and OpenGL details

- `GLTextures(false)` does not enable mip filtering or generate mips.
- `GLTextures(true)` uses staged authored levels when available; otherwise it
  generates a chain. A later first request for mips after a base-only upload
  generates them: previously discarded authored levels are not recovered.
- Compatible DXT1/3/5 data remains compressed on the GPU. Unsupported GPU
  compression falls back to CPU decoding. DXT2/4 normalize to straight-alpha
  pixels because TSRE's existing blending expects that representation.
- ACE DXT 2×2 and 1×1 tails are planar. For a homogeneous compressed GPU chain
  only those tiny levels are block-encoded at upload; this can introduce small
  additional tail quantization. An opt-in document keeps their original bytes.
- Tight rows isolate and restore pixel pack/unpack alignment, row length,
  skipped rows/pixels and pixel-buffer bindings. This also fixes odd-width RGB
  uploads/readbacks.
- Existing alpha-border removal still applies only to CPU-uploaded RGBA. If it
  changes a base image, authored mips are discarded and regenerated if requested.
- CPU source buffers are released only after a successful initial upload.
  Upload is limited by the active context's `GL_MAX_TEXTURE_SIZE`.

## Integration completed / remaining

Completed:

- Production ACE dispatch now uses the structural reader/writer.
- Legacy ACE implementation remains available for comparison, not malformed-file
  recovery. The new parser recognizes only the exact old TSRE RGB 4× offset
  mistake, reports a warning, and allows strict callers to disable that recovery.
- `Texture::takeContentFrom` replaces `TexLib`'s manual content-field copying.
  It moves pixels, blocks, metadata, mips and GPU state; cache identity, aliases,
  ID and references remain cache-owned. A CPU-only reload retains the existing
  GPU texture name for its next upload. Moving another GPU-resident texture over
  resident GPU content requires the owning GL context, as does other existing
  cache GPU management.
- Procedural source and background baked-texture readers request CPU pixels and
  no mip staging. Baked-file writing uses the new general QImage API with explicit
  RGB options, not an adapter preserving the temporary checked-RGB API.
- Cloning, editing, upload and memory estimates account for the new source state.
  `loaded` publishes completed worker content atomically; this is not a redesign
  of concurrent reload/cache ownership.
- Rectangular crop/rotation indexing and no-context editing failure are guarded.
- `Route.cpp` includes `Texture.h` explicitly instead of relying on the old
  loader's transitive include. Bundled miniz unaligned integer reads use
  constant-size `memcpy`, fixing a sanitizer finding without changing the format.
- Terrain test fixtures disable TSRE's default path lowercasing within the test
  scope; otherwise mixed-case `QTemporaryDir` names fail on Linux. Production
  filesystem settings are unchanged.

Remaining, intentionally separate from this implementation:

- Further native investigation of RGBA-indexed full-alpha behavior and unusual
  channel/packed formats that have no verified samples.
- A dedicated ACE editor/preview UI, encoding-choice controls, Photoshop resource
  editing, palette quantization and production-quality compression/filtering.
- Full cache lifetime/deferred GPU deletion and concurrent-reload redesign.
- DDS authored-mip ingestion: this change supports ACE mip staging; it does not
  rewrite the DDS file reader into an equivalent document model.

## Reproducible verification

```sh
cmake -S . -B build-ace -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ace --target tsre_ace_tool
ctest --test-dir build-ace --output-on-failure
build-ace/tests/ace/tsre_ace_tool --scan /path/to/stock/ace/root
build-ace/tests/ace/tsre_ace_tool --bench /new/benchmark.json
build-ace/tests/ace/tsre_ace_tool --bench-write /new/writer-benchmark.json
build-ace/tests/ace/tsre_ace_tool --generate /new/fixture/directory
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 build-ace/tests/ace/tsre_ace_tool --gl
```

Full-app terrain integration checks need the appdata shaders. An isolated Linux
run avoids overwriting the ordinary app log/settings (the current launcher derives
its working directory from the executable location):

```sh
cmake --build build-ace --target TSRE5vc
ace_run_dir=$(mktemp -d /tmp/tsre-ace-check.XXXXXX)
cp build-ace/TSRE5vc "$ace_run_dir/TSRE5vc"
ln -s "$PWD/appdata" "$ace_run_dir/appdata"
mkdir "$ace_run_dir/build"
QT_QPA_PLATFORM=offscreen "$ace_run_dir/TSRE5vc" --test --test-suite=terrain-material
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 "$ace_run_dir/TSRE5vc" --test --test-suite=terrain-material-gl
```

Run each build command to completion; do not start two Ninja processes against
the same build directory. Keep the temporary directory if its logs/images are useful.

The test target links the actual document, codecs, old/new loaders and Texture
upload code. Minimal Game/Undo/Brush definitions isolate it from the app; codec,
file I/O and OpenGL calls are not mocked. The benchmark compares old base-only
loading with new base-only and new authored-mip staging separately, with warmups,
alternating order and medians. Generated inputs exclude formats the unchecked
legacy reader cannot safely process.
The separate writer benchmark compares the previous checked RGB bake writer
with the new general QImage writer, including conversion and atomic file output.

Default parser limits: 512 MiB input/inflated/aggregate payload, 64 million pixels,
16,384 per dimension, 64 descriptors of each kind. These are safety limits, not
MSTS compatibility promises. Non-square and odd base images are supported;
authored ACE mip chains currently require square power-of-two dimensions.
The fixture matrix covers powers of two from 64 through 4096, with CPU tests also
covering tiny and rectangular images.

Measured performance and isolated MSRE findings are recorded in the mirrored
[implementation and test report](../msts/tsre-ace-v2-implementation-and-msre-tests.md).
Its raw screenshots/traces remain in the separate MSTS research workspace.
Windows-host execution was not used for that original branch report; subsequent
merged-main verification is recorded separately below.

Final verification: 7,539 standalone CPU/API checks pass in Release and under
ASan/UBSan; 1,062 OpenGL checks pass through 4096; all 508 extracted stock ACEs
decode. The full Release TSRE app builds, its terrain-material suite passes
296 checks, and its terrain-material-gl suite reports zero failures.

Measured on this Linux Release build: at 1024, planar RGB/mask/RGBA base loading
is roughly 39–46% faster; at 2048, RGB/RGBA have an approximately 18% / 5–6 ms
overhead. At 4096 the base cases range from slightly faster to about 8% slower;
DXT1 is 3.94 ms versus 3.61 ms. Checked RGB baking at 4096 is 323 ms versus
330 ms for the previous quick-fix writer. Authored-mip staging is measured
separately because the old loader ignored those levels. These are warm-cache
medians, not cross-platform guarantees or a claim of zero overhead in every case.

## Windows merged-main integration (2026-09-08)

The complete `feature/ace-v2-texture-integration` branch is merged with the newer
procedural save optimizations and independent 2048 m detailed-texture distance.
Both source-image loading and the shared `loadBakeImage()` helper use explicit
`AceLoadOptions` with CPU pixels, quality 1 and no mip staging. The latter helper
also serves incremental saves after CPU-bake eviction; it must not use the
rendering adapter's `Game::textureQuality` policy. Baked writing uses the new
QImage API with RGB encoding, preserving checked/atomic output and one `.bk`.

A regression fixture runs at `textureQuality=2`, loads an authored-mip ACE source
with one-pixel stripes, and verifies the generated texture against full-resolution
source pixels. It also saves/reloads the fallback and checks its native dimensions
and absence of staged mipmaps.

Windows verification commands (Qt and MinGW runtime directories on `PATH`):

```powershell
cmake --build build -j 1
ctest --test-dir build --output-on-failure
$env:QT_QPA_PLATFORM = 'windows'
& .\build\tsre_ace_tool.exe --gl
& .\build\TSRE5vc.exe --test --test-suite terrain-material-gl
$env:QT_QPA_PLATFORM = 'offscreen'
& .\build\TSRE5vc.exe --test --test-suite terrain-material
$env:TSRE_TERRAIN_MATERIAL_RGB = '1'
& .\build\TSRE5vc.exe --test --test-suite terrain-material
Remove-Item Env:TSRE_TERRAIN_MATERIAL_RGB
& .\build\TSRE5vc.exe --test --test-suite terrain-grid
```

The full app writes `log.txt` in its resolved working directory. Preserve that
file before testing and copy each suite's log aside before the next invocation;
restore the original afterward. The standalone ACE tool writes to stderr.

Merged-main results on Windows, Qt 6.10.1 / MinGW 13.1 Release:

- Full application and standalone ACE tool build successfully.
- All **7,539 CPU/API checks** pass through CTest.
- All **405 procedural CPU checks** pass in both BC1 and RGB modes, including the
  reduced-render-quality regression above and the existing incremental-save tests.
- The full-app procedural OpenGL suite reports **zero failures**; all **66
  terrain-grid checks** pass. The original application log was restored and its
  SHA-256 verified against the pre-test copy.
- The standalone native OpenGL suite on **AMD Custom GPU 0932** reports **10
  failures out of 1,062 checks**, all DXT3 GPU readback pixel comparisons at
  8/64/512/2048/4096 with and without mipmaps. The maximum component difference is
  consistently 15: channel 3 (alpha), expected 255, actual 240 from GPU readback,
  beyond the existing tolerance of 2. This does not yet establish whether actual
  shader sampling is affected or identify the cause. Upload/error-state,
  lifetime, other encoding and remaining checks pass. This is a reproducible
  Windows-host discrepancy, not a successful repeat of the Linux GL result.
  No production workaround or relaxed tolerance is introduced by the merge;
  the test now reports the maximum mismatch and component values for diagnosis.

Logs: `build/ace-v2-merge-{ctest,gl,gl-diagnostic}.log` and
`build/terrain-material-ace-merge-{cpu,rgb,gl,grid}.log`.
The MSRE experiments and stock-file scan in the original report were not rerun
as part of this Windows merge verification.
