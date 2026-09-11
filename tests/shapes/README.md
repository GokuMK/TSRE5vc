# New MSTS shape checks

The synthetic fixture is independently authored and contains no stock asset data.

## Parser stage measurements

`cmake --build build --target tsre_shape_parser_bench` builds a manual CPU benchmark using the real new reader/document code and legacy ParserX numeric reader. Pass shape paths to measure document loading; text inputs additionally measure decoding and tokenization independently. It uses one warmup and seven samples per stage, with prior document destruction outside timing. Document loading includes file access, native numeric conversion and source-record construction, but excludes runtime extraction and GL. Lexer timing excludes decoding and does not convert numeric values. Add `--compact` to apply the Compact policy during parsing; output includes materialized block/scalar counts and skipped blocks. Export requires Complete mode.

For equivalent UTF-16 versions of the three compressed inputs:

```sh
build/tests/shapes/tsre_shape_parser_bench --export-text /tmp/sfile-parser-inputs/utf16 \
  /root/msts/proprietary/msts_app/TRAINS/CD_193_290/CD_193290.s \
  /root/msts/proprietary/msts_app/TRAINS/CD_193_290/CD_193290_MS.s \
  /root/msts/proprietary/msts_app/TRAINS/CD_193_290/CD_193290_FG.s
build/tests/shapes/tsre_shape_parser_bench /tmp/sfile-parser-inputs/utf16/*.s
```

The numeric microbenchmark reads 100,000 simple literals through each API and verifies an identical checksum. It measures token reading plus numeric conversion, not full shape loading. The optimized document converts known numeric fields once while parsing and reads native values during runtime extraction. The text reader borrows ordinary token spans, uses checked numeric conversion and owns decoded quoted strings; retained exceptional lexemes are copied explicitly.

UTF-16 profiling additionally reports `lexer_with_lookahead` (copying `peek()` then `next()`), `lexer_with_kind_lookahead` (`peekKind()` then `next()`), and `numeric_conversion_only`. The last stage preselects tokens accepted by the floating-point converter outside timing, then converts that same subset and verifies its checksum; it is not a measurement of every shape field's typed conversion. These diagnostic stages overlap and must not be added together. `simis_direct_numeric_100k` measures the consuming `readNumber()` API on the same literals as the existing token/ParserX comparison.

The reusable reader keeps up to three lookahead tokens in fixed storage. `peekKind()` can classify ordinary atoms without materializing their text; quoted strings still go through normal validation. `readNumber()`, `readInteger()` and `readUnsignedInteger()` consume one token with the same syntax/range checks as the Token APIs. Regular text integer arrays write directly into native document storage in Complete and Compact modes; irregular content rolls back the speculative cursor/values and uses the general parser. No changes to legacy ParserX are required.

The [Open Rails harness](openrails/README.md) builds the actual local C# parser separately. Results and interpretation are in the [parser investigation](../../docs/tasks/shapes/reports/sfile-parser-performance.md). Run benchmarks sequentially without concurrent builds. Generated asset contents remain private under `/tmp`.

The [optimization report](../../docs/tasks/shapes/reports/sfile-optimization-results.md) records the current implementation's results and distinguishes pre-load Compact from later explicit compaction.

The subsequent [UTF-16 report](../../docs/tasks/shapes/reports/sfile-utf16-optimization-results.md) records reader/array optimizations, saved pre-change executables, paired Complete/Compact timings and cross-version export checks. Its text-loading measurements supersede the earlier report.

## Correctness checks

Configure normally with CMake. `TSRE_BUILD_SHAPE_TESTS=ON` (default) builds the CPU-only `tsre_shape_document_tests` target. Run it through `ctest --test-dir build --output-on-failure`. The target requires only Qt Core and the bundled codec; it does not create GL resources. Passing a local corpus directory additionally repeats semantic round trips three times for every `.s` file and compares re-encoded binary payloads byte-for-byte with the original input (inflated independently through Qt/zlib where needed).

Application suites:

```sh
QT_QPA_PLATFORM=offscreen build/TSRE5vc --test --test-suite shape-complex
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a build/TSRE5vc --test --test-suite shape-complex-gl
```

Compare the private original corpus, one process per file to isolate legacy lifetime behavior:

```sh
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a python3 tests/shapes/compare.py \
  --exe build/TSRE5vc \
  --corpus /root/msts/proprietary/msts_app/TRAINS/TRAINSET \
  --output /tmp/sfile-stock-comparison --gl
```

The output contains relative paths, source hashes, JSON metrics and logs. `TSRE_SHAPE_COMPARE_IMAGES=/tmp/shape-images` additionally saves static comparison images. Keep stock assets, generated images and raw logs outside the repository. Private temporary symlinks reproduce case-insensitive Windows texture lookup on Linux; originals are never modified.

The GL harness fixes the camera to the legacy CPU bounds for both backends, warms textures synchronously, checks packed vertex data, matrices, direct/gather images and integer selection, and renders normalized saved output. Known legacy differences are reported, not silently declared identical. For diagnosis only, it also measures the effect of unique legacy matrix cache hashes and corrected legacy animation endpoint lookup entries; these changes are confined to test instances.

`requested_compact_*` metrics use a separate instance whose mode is selected before CPU loading. They cover CPU load, materialized source records, GL initialization and agreement with Complete rendering/picking. This differs from the older `compact_*` frame metrics, which measure an instance compacted after Complete loading. `loadDocumentBytes`/`requested_compact_load_document_bytes` capture source-arena storage before extraction cleanup; current arena estimates include vector capacities, but not allocator overhead or process RSS. They are not measurements of total peak loading memory.

Compact packs regular points, normals, UV coordinates and vertex references into numeric tables without per-row source records. Unusual layouts fall back to the general parser. `requested_compact_blocks` counts materialized records, so packed rows are absent from that count; `requested_compact_scalars` includes their native words. The document byte counter includes packed vector capacities. `requested_compact_cpu_geometry_bytes` records the separate geometry arrays retained before upload. Complete retains the full editable source model.

`requested_compact_stages_ms` splits CPU loading into `read` (file I/O, decompression and document parsing), `extract` (runtime arrays, validation and bounds), `metadata` (adjacent `.sd` reading/extraction) and `cleanup` (storage accounting and temporary-document release). These timers exclude GL and textures. Run comparisons without concurrent builds or corpus stress work when collecting timing evidence.

`*_frame_ms` includes GL completion and framebuffer readback (12 samples). `*_submission` reports median/p95 for 24 draw submissions, with prior work completed outside each timed interval and readback omitted. Driver calls can still block; this is not an isolated GPU timing measurement. Document/runtime/geometry byte counters are estimates excluding allocator overhead, shared textures and process RSS. Load timing uses an already readable/warm filesystem cache and excludes texture loading.

`legacy_retained_cpu_estimate_bytes` estimates accessible legacy storage immediately after `load()`, before render caches or textures are populated. It counts the SFile object, retained public arrays (including known spare slots), QVector capacities, string contents and animation key/frame-lookup arrays. It excludes released points/normals/UVs/indices, private state allocations, unrecorded spare primitive slots, spare string capacity and Qt/allocator overhead. Shared string contents may be counted more than once. The legacy loaders themselves are unchanged. `legacy_animation_estimate_bytes` is the animation portion of that total.

`legacy_gpu_bytes` queries each loaded VBO's GL buffer size, with the previous binding restored afterward; `legacy_gpu_size_valid` checks the sum against the legacy vertex counts and nine-float stride. These are buffer payload bytes, excluding driver overhead and textures, even when using software GL. `complete_retained_cpu_estimate_bytes` and `requested_compact_retained_cpu_estimate_bytes` sum the new implementation's document, source-geometry and runtime estimates after upload and before its first draw. `requested_compact_gpu_bytes` records its uploaded buffer payload. Retained storage is distinct from the source-document-only counters and from peak loading memory; none of these counters measures process RSS or legacy allocations leaked outside the shape's reachable fields.

Sanitizer example (build just the independent target):

```sh
cmake -S . -B /tmp/sfile-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O1 -g' \
  -DTSRE_BUILD_ACE_TESTS=OFF -DTSRE_BUILD_TOKEN_TESTS=OFF \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build /tmp/sfile-asan --target tsre_shape_document_tests -j3
ctest --test-dir /tmp/sfile-asan --output-on-failure
```

### Joined legacy comparison

The GL corpus run also checks `SFileLegacy` against the unchanged `SFile`/`SFileX`/
`SFileC`. JSON fields beginning with `joined_` record CPU-only loading (with the
context released), GL initialization, bounds, every LOD's buffer contents/hierarchy,
direct images, picking and sampled legacy-animation equivalence. Timings exclude
context release/reacquisition. `joined_load_gl_ms` is CPU plus initialization time.

`joined_gather_different_pixels` records the production pointer-keyed gathered path;
its draw order can differ across allocations. The test-only ordered renderer checks
`joined_ordered_gather_different_pixels` separately without changing production
batching. See [the task and results](../../docs/tasks/shapes/05-sfile-legacy-load-gl.md).

Set `TSRE_LEGACY_UPLOAD_BENCH=1` for a focused GL corpus run to collect
`joined_cpu_samples_ms` and `joined_upload_samples_ms`: two warmups followed by
15 fresh loads/uploads. The upload timer includes `glFinish()` and excludes CPU
loading; each iteration destroys its shape before the next. Use the same original
large CD shape and alternate baseline/candidate executables between processes.

### Read-only large-collection compatibility run

Use `legacy_compat_inventory.py --source <Windows trainset> --work <Linux directory>`
to build a case-normalized Linux symlink view and manifest. Then run
`legacy_compat_compare.py --work <directory> --workers 4` under Xvfb. The driver
wraps each old/new process with bubblewrap (`/mnt` read-only) and resource limits;
it resumes completed paths from JSONL results. Large files are serialized within
the worker pool. It does not run SFileComplex parsing or round-trip tests.

See [scope, protections and results](../../docs/tasks/shapes/reports/sfile-legacy-third-party-comparison.md).

For the three-mode comparison, use `three_compat_compare.py` with the same manifest
and `--output three-camera-results`. It runs SFileLegacy, requested Compact and
Complete in independent processes; old SFile/X/C is not run. The driver supplies
Legacy's camera and animation duration to both new modes. See
[three-mode methodology and results](../../docs/tasks/shapes/reports/sfile-three-mode-comparison.md).

### Application backend selection

Set `TSRE_MSTS_SHAPE_BACKEND=legacy` for SFileLegacy, `complex` for Complete, or
`complex-compact` for Compact before launching the application. Unset uses SFileLegacy; `old` explicitly selects original
SFile/C/X. `TSRE_MSTS_FIRST_LOD_ONLY=1` affects only the Complex backends. Selection
is fixed per ShapeLib and does not affect glTF loading.

### Resuming compatibility runs

Both large-collection drivers record SHA-256 hashes of the executable, driver,
shared resume helper and source shape. Before resuming, they verify provenance
and rehash selected inputs, including their mirror paths. Changed or missing
provenance, changed shape contents, or a mismatched mirror aborts with a request
to use a new `--output` directory; existing results are not overwritten or mixed
with a different build. Older results without provenance remain readable reports
but cannot be resumed by these drivers.

Keep the executable and corpus fixed during a run. These checks do not fingerprint
textures, adjacent `.sd` files, driver/environment settings or GPU configuration;
use a new output directory when those change too. Resumption preserves recorded
failures as well as successful cases; use a new directory to retry them.

Run the synthetic resume-guard checks with:

```sh
python3 -m unittest discover -s tests/shapes -p test_compat_resume.py -v
```
