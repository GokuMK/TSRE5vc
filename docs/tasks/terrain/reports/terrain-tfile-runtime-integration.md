# T-file runtime integration — 2026-09-21

Parent: [complete T-file model/parser](../terrain-tfile-structure-and-parser.md).

## Implemented

The application now uses `TFile : TerrainFile::Data`, with one authoritative
native model. The old pointer arrays/maps are gone from production. The frozen
legacy implementation lives only in the standalone benchmark target.

Migrated terrain loading, legacy/paged UV generation, materials, bounds, water,
sample resources, procedural source copies, all-set material snapshots, undo,
rollback, batch baking, network parsing and creation/layout probing. Cropping now
uses typed UVs and actual samples-per-patch. The content-case scanner discovers
C/D, patch-F, terrain-shape and nested transfer-shader resource references too.

Labels, ordering, unknown framed blocks/tails and all patch sets survive native
round trips. Editable TSRE extensions merge into the preserved sample metadata.
Shader tables remain flat in storage; the primary/auxiliary view is classified
from contents, not directory. Auxiliary indices are repaired once, never authored
by editing. Unknown potentially index-bearing records prevent renumbering.

Patch-F loading is explicit, outside the codec. It supplies effective runtime
flags; untouched external bytes and original inline words survive saves. Changed
bytes use MSTS's `0xcb` mask. Checked staging commits sidecars before the descriptor
and restores them if descriptor commit fails. Save checks external modifications.
Missing/invalid sidecars keep a tile read-only; network loads do not consult local
disk. This is **not** a complete multi-file transaction covering height RAWs/ACEs.

Ordinary patches remain 60-byte records (P16: 15 KiB; P32: 60 KiB), with no per-patch
QString/vector/virtual object or draw-time metadata traversal. Dirty-height
bookkeeping adds a four-integer rectangle; inactive bounds are handled on save.
The existing heightmap, vertex formats and LOD policy are unchanged.

## Verification

Builds use MinGW 13.1 / Qt 6.10.1, Release, **two compiler jobs maximum**.

- `tokens`: 1,630 passed, 0 failed.
- `terrain-tfile`: 78 passed, 0 failed, including production adoption/remapping,
  sidecar precedence/preservation/conflict refusal, transactional reload, layout
  probing and editable extension preservation.
- `terrain-grid`: 66/66, including creation/overwrite, E/N names and field round trips.
- `terrain-material`: 575/575, including R8/R16/R32/R64 typed cropping, inactive-set
  bounds after height editing, UV-only preservation and procedural undo/save.
- `terrain-edges`: 52/52; `terrain-brush`: 360/360; `terrain-normals`: 132/132;
  `transfer-mesh`: 54/54.
- `terrain-material-gl`: 0 failures on AMD Custom GPU 0932, covering GPU UV remap,
  procedural paint/undo/save, async generation and shared texture lifetime.
- `transfer-depth-gl`: 44 checks, 0 failures, including immediate/queued paths.
- `content-case`: passed, including the added rare terrain-reference fixture.

User smoke test: the previously crashing route now loads without crashing.
This does not replace the remaining interactive acceptance checks below.

The read-only descriptor scanner checks original payload bytes against re-encoding,
then production runtime load/preflight and layout-probe agreement:

| Corpus | Files | Failed | Paired / flat | Exact descriptor payloads |
|---|---:|---:|---:|---:|
| MSTS installation ROUTES, including archived test descriptors | 1,169 | 0 | 1,083 / 86 | 1,169 |
| `C:/trainsim/routes/CMK` | 1,194 | 0 | 1,194 / 0 | 1,194 |

Application-level terrain loading also checks height resources and variable-P UV
editing. CMK loads all 1,194 tiles, editable. In the MSTS installation, 1,141 active
route descriptors are accepted/editable, 1,139 height payloads load, and 105
variable-patch edit checks succeed. Two previously documented USA1 `_y.raw` files
remain truncated: `-07415a30` (131,051 bytes) and `-074176e4` (16,385 bytes), versus
131,072 required. No descriptor regression is attributed to those resources.
The application scanner now excludes arbitrarily nested archived route snapshots:
its loader requires `root/ROUTES/route`; the descriptor-only scanner still covers them.

Tests use temporary save fixtures. Original user/research route files are not
rewritten. Proprietary fixtures are not committed.

## Performance

Three alternating legacy/runtime process pairs; each uses 3 warmups + 31 samples
for N256, P4/P8/P16/P32, one ordinary shader pair. Memory parse timing excludes
input-buffer construction and object destruction. Output is checked against the
legacy fixture each time. No compilation was active during these measurements;
an otherwise fully idle host was not established.

Medians of the three process medians, milliseconds:

| P | Legacy parse | Runtime parse | Legacy write | Runtime checked write |
|---|---:|---:|---:|---:|
| 4 | 0.0053 | 0.0082 | 0.0199 | 0.0286 |
| 8 | 0.0064 | 0.0096 | 0.0350 | 0.0486 |
| 16 | 0.0208 | 0.0156 | 0.1240 | 0.1461 |
| 32 | 0.0760 | 0.0676 | 0.4620 | 0.5289 |

There is no large per-patch parser regression in this fixture, but this is **not**
universal speed parity. P4/P8 pay ~3 microseconds of extra parse work; P32 checked
writing adds ~0.067 ms here. Relative changes exceed the design's 10% review trigger
in several cells. They are recorded rather than hidden by averaging all layouts.
Runtime parse includes auxiliary repair and extension extraction. Checked write
includes preservation merging/copying, unlike the unchecked legacy writer.

The new `--io` mode additionally includes warm filesystem open/read, SIMIS
inflation and parsing. Qt debug logging is disabled in both modes (the legacy
reader otherwise logs every filename). Three alternating pairs produced:

| P | Legacy median | Runtime median |
|---|---:|---:|
| 4 | 0.1383 | 0.1864 |
| 8 | 0.1256 | 0.1456 |
| 16 | 0.1634 | 0.2090 |
| 32 | 0.4435 | 0.4637 |

These warm-I/O results are noisy: runtime P16 process medians ranged
0.1892–0.4527 ms, and runtime P32 p95 ranged 0.8175–1.5782 ms. They are not cold-SSD
latency measurements or a full-route FPS/memory benchmark. No N1024/N2048 mesh
performance conclusion follows from a descriptor-only test.

## Reproduction and remaining acceptance

With the configured Qt/MinGW runtime on PATH:

```powershell
ninja -C build -j 2                          # Include all enabled standalone targets
ctest --test-dir build --output-on-failure -j 1
ninja -C build -j 2 TSRE5vc tsre_token_tests tsre_terrain_file_tests tsre_terrain_file_benchmark tsre_content_case_tests
build/tsre_token_tests.exe
build/tsre_terrain_file_tests.exe
build/tsre_terrain_file_tests.exe --scan 'C:/MagiPacks/Microsoft Train Simulator/ROUTES'
build/tsre_terrain_file_tests.exe --scan 'C:/trainsim/routes/CMK'
build/tsre_terrain_file_benchmark.exe             # Frozen legacy reader/writer
build/tsre_terrain_file_benchmark.exe --runtime   # Production TFile
build/tsre_terrain_file_benchmark.exe --typed     # Preservation codec only
build/tsre_terrain_file_benchmark.exe --runtime --io
build/tsre_content_case_tests.exe
build/TSRE5vc.exe --test --test-suite terrain-material
build/TSRE5vc.exe --test --test-suite terrain-files --test-cases 'C:/trainsim/routes/CMK'
```

CPU suites can use `QT_QPA_PLATFORM=offscreen`; GL suites on this machine use
`QT_QPA_PLATFORM=windows`. `QT_FORCE_STDERR_LOGGING=1` exposes standalone Qt logs.

Standalone-build follow-up: the ACE crop fixture still used the removed
`float[7]` UV API. It now uses `TerrainFile::PatchUv` with explicit R=16,
preserving its 6-by-4 output and pixel assertions. The complete default build
passes with two build workers. All eight registered CTest suites pass:
tokens, terrain TFile, ACE codec, ACE converter, shape document, content case,
content paths and elevation (12.42 seconds total on this run).

Still not claimed: exhaustive live/peak allocator profiling, whole-route FPS and
all interactive shadow/Gather/selection combinations, or fresh MSRE/ORTS visual
acceptance. Interactive tests should include detailed and distant flat/paired
tiles, texture rotation/mirroring/crop, water, height editing and procedural saves.
Text-format parsing remains the explicitly separate, fixture-led milestone.
