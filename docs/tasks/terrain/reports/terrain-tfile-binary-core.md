# T-file binary-core checkpoint — 2026-09-21

Parent: [complete T-file structure and caller migration](../terrain-tfile-structure-and-parser.md).

## Scope and status

Historical checkpoint. The subsequent [runtime integration report](terrain-tfile-runtime-integration.md)
supersedes the integration status below; the measurements here remain unchanged.

This is an **independent codec checkpoint**, not completed engine integration.
Normal routes still load/save through the existing `TFile`. The new value model
and codec are in `src/tsre/world/TerrainFileData.{h,cpp}` and exposed by the
`terrain-tfile` test suite. No route files were changed by the corpus scans.

Implemented:

- All researched native binary record layouts, including C/D and patch-F
  references, AS/US payloads, all patch sets, native transfers and shapes.
- Contiguous typed 60-byte patches, exact unsigned shader indices, floating
  patch-set distances and UV scales; dynamic texture/UV lists.
- Flat/paired classification from shader names/order, independent of directory.
  Auxiliary-half repair is explicit, idempotent and separate from parsing;
  invalid indices are not folded modulo the table size.
- Cold labels, field order, fixed-record tails and unknown complete blocks.
  Empty/absent containers and one/four-float water forms are retained.
- Transactional parsing, bounded framing/counts and bounded SIMIS inflation;
  64 MiB descriptor/input/output cap, not a heightmap/GPU-memory limit.
- Composable binary writer and checked `QSaveFile` descriptor commit. This does
  **not** implement a multi-file terrain transaction.

The codec preserves TSRE procedural extension containers whole and opaque. It
does not yet replace the existing extension editor. External sidecars are only
referenced, never opened by the codec. Missing renderer-required fields may be
represented without making the descriptor renderable. Ambiguous duplicate
known fields disable rewriting. Text descriptors are explicitly unsupported.

## Shape-parser lessons applied

Reviewed `SimisReader.h`, `FileBuffer`, `SFileDocument` and the shape
[performance investigation](../../shapes/reports/sfile-parser-performance.md)
and [optimization results](../../shapes/reports/sfile-optimization-results.md).

Terrain uses the shared `FileBuffer::readBlock`, `ScopedLimit` and token enums,
but does not instantiate the shape document or a general-purpose scalar DOM.
Ordinary patch rows have no per-row preservation object/order entry. Exceptional
patch labels/tails live in a sparse sidecar. A fixed row receives one 60-byte
bounds check and explicit little-endian field decoding, not a raw struct read.
Inflation uses a validated destination bound; it cannot grow until decompression
succeeds. Cold metadata remains outside renderer/sample loops.

An initial generic patch-collection implementation roughly doubled P32 parse
time. The dedicated contiguous-row path removed that per-patch metadata cost.
Benchmarks retain the unchanged production reader as the comparison baseline.

## Verification

Synthetic tests cover ordinary P4/P8/P16/P32 descriptors, flat odd/even shader
tables, paired repair, multiple sets, noninteger distances, more than two shader
slots/UVs, unknown IDs/versions, labels/tails, bit-exact special floats, malformed
counts/strings, every truncation of the rare-record fixture, failed reloads,
compressed-size violations and atomic descriptor save/reload.

The dedicated suite passes **57 checks**, both standalone and through the built
application. Existing standalone token tests pass **1630 checks**. The application
and test targets build successfully with `-j 2`.
The existing offscreen `terrain-grid` suite passes **66/66**, and
`terrain-material` passes **564/564**; these still exercise the unchanged runtime
`TFile` path, not the future typed caller migration.

Read-only corpus results:

| Directory | Passed | Failed | Paired / flat | Byte-exact descriptor payloads |
|---|---:|---:|---:|---:|
| `C:/MagiPacks/Microsoft Train Simulator/ROUTES` | 1169 | 0 | 1083 / 86 | 1169 |
| `C:/trainsim/routes/CMK` | 1194 | 0 | 1194 / 0 | 1194 |

The MSTS installation includes stock routes and local test routes; it is not a
stock-only corpus. Comparisons include every byte after the canonical 32-byte
SIMIS header and normalize compression envelopes. Re-encoded files are also
parsed and encoded a second time. The scanner never calls `save()` on originals.
Real proprietary fixture data is not tracked in Git.

## CPU microbenchmark

Windows, MinGW 13.1, Qt 6.10.1, CMake Release (`-O3 -DNDEBUG`), 2026-09-21.
Three alternating legacy/new process pairs; each process takes three warmups
then 31 measurements per layout. Input is synthetic N256/8 with one ordinary
shader pair and P4/P8/P16/P32. Input-buffer allocation/copy and destruction of
the parsed document are outside timing. Output is checked against the original
fixture every iteration. Compilation had finished before this run; a fully
idle host was not established.

Below are medians of the three process medians, in milliseconds:

| P | Descriptor bytes | Legacy parse | Typed parse | Legacy write | Typed write |
|---|---:|---:|---:|---:|---:|
| 4 | 1796 | 0.0049 | 0.0100 | 0.0148 | 0.0302 |
| 8 | 5108 | 0.0071 | 0.0084 | 0.0358 | 0.0435 |
| 16 | 18356 | 0.0233 | 0.0172 | 0.1241 | 0.1263 |
| 32 | 71348 | 0.0804 | 0.0568 | 0.4896 | 0.4334 |

P32 typed parse medians range 0.0563–0.0665 ms; typed write medians range
0.4233–0.5365 ms. P32 parse p95 ranges 0.0748–0.1027 ms, and write p95
0.4783–0.6832 ms. The executable prints median/p95 for every layout.

The packed-row path avoids the large per-patch slowdown; it does **not** establish
universal performance parity. P4/P8 still pay relatively higher preservation/
framing overhead (about 5/1 microseconds extra parse time here). Their repeated
relative regressions exceed the parent's 10% review trigger and remain an
integration/performance review item, despite the small absolute cost. These
measurements exclude decompression, filesystem access, AS/US-heavy cases, runtime
extraction, graphics and editing. N1024/N2048 heightmap performance cannot be
inferred from this descriptor-only N256 benchmark.

## Reproduction

Use the configured Qt/MinGW runtime PATH. Build with **two jobs maximum**:

```powershell
ninja -C build -j 2 tsre_terrain_file_tests tsre_terrain_file_benchmark tsre_token_tests TSRE5vc
.\build\tsre_terrain_file_tests.exe
.\build\tsre_token_tests.exe
.\build\tsre_terrain_file_tests.exe --scan 'C:/MagiPacks/Microsoft Train Simulator/ROUTES'
.\build\tsre_terrain_file_tests.exe --scan 'C:/trainsim/routes/CMK'
.\build\tsre_terrain_file_benchmark.exe
.\build\tsre_terrain_file_benchmark.exe --typed
.\build\TSRE5vc.exe --test --test-suite terrain-tfile
```

`terrain-tfile` is also included in the application's `all` suite. The standalone
benchmark is intentionally excluded from the default build.

## Remaining integration gate

The complete parent task is **not done**. Migrate direct runtime/editor callers,
ownership and material snapshots, exact patch indices/UV access, all-set shader
remapping, procedural undo/rollback and extension editing together. Implement
effective patch-F loading/edit/save policy and terrain save preflight before
switching normal terrain to this codec. Preserve the separate text milestone.

No GL performance, paged distant-crash fix, MSRE visual compatibility, full-file
I/O/inflate timings or allocation/peak-memory result is claimed by this checkpoint.
