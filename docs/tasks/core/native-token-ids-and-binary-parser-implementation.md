# TSRE native token IDs and binary parser changes: implementation and follow-up

Initial implementation: 2026-09-08; scope/status clarification: 2026-09-09.
Branch: `feature/native-token-ids`; implementation commit: `8c85bb1`.
Base: `main` at `9f389f08450411614256d9f3e018225ddc62584a`.

**This is a combined token-ID and parser-change report, not a token-only diff.**
The native-ID mechanism is implemented and the checks below passed at the
recorded revision. The additional parser integration is **not complete or
accepted as a recovery-first TSRE replacement**: current whole-file aborts and
world-object rollback discard usable content. Test success does not approve
that policy. The user controls publication; no merge is implied by this report.

The canonical [consumer inventory](../../features/file-buffer.md#11-current-consumers)
and [remaining-work checklist](../../features/file-buffer.md#111-status-and-follow-up-todos)
are in the FileBuffer guide. The recovery example there is documentation, not
an implemented recovery fix. This TSRE implementation history belongs under
`docs/tasks/core`; the separate MSTS workspace's earlier report is historical.

The user approved implementation of the
[reviewed local plan](native-token-id-migration.md).
[Native SIMIS token IDs](../../features/native-token-ids.md) documents the final
allocation, API, compatibility break and remaining binary-codec limits.

## 1. Native token-ID and allocation changes

- Complete unsigned uint32 IDs throughout the enum, registry, terrain/world/
  shape readers, writer and network-token boundary. Removed the world-file
  offset state/subtraction and numeric SIMIS token use sites, including Core
  terrain/shape dispatch and AS/US ordering. Ordinary payload numbers remain.
  A later audit found two remaining literal IDs in QuadTree's writer; the
  narrowly scoped follow-up is recorded below.
- Native sparse forms and all 34 brake-order corrections; canonical names
  replace the invented `...2` aliases. Added recovered `terrain_sample_usbuffer`
  (282), `Soundsource` (`0x00040043`) and `Soundregion` (`0x00040044`).
- Three procedural terrain file blocks use `TSRETerrainMaterialBuffer`,
  `TSRETerrainBakedMaterial`, `TSRETerrainMaterialMap` at `0x00061000–02`.
  No prototype aliases or test-tile conversion. Eight network names keep their
  underscores and use `0x00060001–08` on both ends.

## 2. Additional parser and compatibility changes

These changes go beyond enum renumbering. They must be reviewed as parser
behavior changes rather than assumed to be a mechanical part of token migration.

- Checked block/label/parent boundaries, endian-safe scalar/token reads and
  non-mutating unknown-name lookup. Legacy positional reads are bounded when
  parsing a scoped binary block; this is not a wholesale text-parser rewrite.
- World parsing has a CPU-only entry point used by production loading and
  generated tests. It preserves control-record ordering and rolls back newly
  parsed objects after framing/payload failure. Binary WS uses its distinct
  root and the recovered sound-form IDs.
- Shape sections, animation keys and LOD geometry use their declared block
  framing instead of fixed header skips. Existing renderer conventions, one
  LOD-control policy and supported primitive/controller types remain.
- Counted terrain slots/UV calculations exceeding TSRE's two-entry storage are
  rejected, instead of overflowing those existing arrays. Existing shader-list
  splitting and last-patchset behavior are intentionally not redesigned.
- `findToken()` now throws for missing required tokens as well as malformed
  framing. `Simis::Block` introduced additional required-child searches;
  optional versus required assumptions need explicit review.
- `TFile::load()` now returns failure, and terrain network callers stop their
  local load processing on that result. This is a load-policy/API change.
- Binary detection now checks the subheader; BOM and short-input probes changed
  in shared FileBuffer/ReadFile code. Shape labels and shader-name strings now
  use their declared framing.
- Binary sound-source/region factories became reachable through the corrected
  WS root and native IDs; specialized SoundRegion fields remain incomplete.
- Signal-unit count/index checks, shape geometry-index checks and an explicit
  missing-GL-context failure were added. These check policies are distinct from
  token identity and do not implement local recovery.
- Shared network header encoding/decoding adds empty/truncated/unsupported-token
  diagnostics. The wire envelope and data payloads otherwise remain unchanged.

## 3. Allocation/cleanup and test infrastructure changes

- Shape temporary vertex storage changed from an initial 120,000-entry array
  to a sized vector. Terrain-writer temporary length arrays now use QVector.
  Some file/buffer cleanup was added; this is not a comprehensive ownership fix.
- The CPU-only world parse entry point and the `tokens`, `token-world` and
  `token-shape-gl` test suites were added, including a standalone CTest target.
- API/allocation documentation, actual world-extension inventory and current
  procedural terrain docs were updated. No parser performance benchmark was
  performed.

## 4. Minimum QuadTree follow-up, 2026-09-09

`QuadTree::saveTD()` now writes `TS::terrain_desc` (132, `0x84`) and
`TS::terrain_desc_tiles` (135, `0x87`) as explicit quint32 IDs instead of two
numeric literals, with a direct TS.h include. Both TD and TDL use this writer.
This follow-up changes **no reader code**, fixed header skips, packed quadtree
bytes, lengths, labels or recovery behavior. It is not a conversion of QuadTree
to the new parser. The native values and resulting serialized bytes are unchanged.

Verification of this follow-up: the actual QuadTree translation unit compiled
successfully. A temporary QDataStream check compared the old and named-ID block
serialization for payload sizes 0, 1, 257 and 65,536; all four were byte-identical.
This checks the changed writer expressions, not a new full-route/TD parser test.

The [FileBuffer TODOs](../../features/file-buffer.md#111-status-and-follow-up-todos)
separate this completed token cleanup from any optional future QuadTree parser
work. They do not authorize additional implementation in this follow-up.

No ACE implementation, procedural material catalogue/bitmap format, selection
ID scheme, ORTS source, MSTS executable or existing route/test tile was changed.
No Windows-host filesystem/registry/process access or Wine run was performed.
Static executable table verification read the already-provided Linux-local
copies as data; it did not execute them.

## Stock-file schema corrections, 2026-09-09

Stock-file testing exposed two compatibility bugs missed by the original
generated fixtures. These corrections do not change native token IDs, generic
FileBuffer bounds checks, or the unresolved whole-file failure/recovery policy.

- **Shapes:** `sub_object_header` (40) permits a final optional `uint32 SubObjID`
  after `geometry_info` (41) and optional `subobject_shaders` (104) /
  `subobject_light_cfgs` (105). This is a positional scalar, not a child block
  and not padding inside `geometry_info`. The corrected header loop consumes
  the final four bytes without interpreting them as a token; TSRE continues
  to ignore the value. Microsoft's supplied `UTILS/FFEDIT/newshape.bnf`, rule
  `sub_object_header`, documents `[:uint,SubObjID]` at the end of the rule.
  The stock scan found 797 such fields, including values 0, 1 and 2.
- **Terrain:** `terrain_water_height_offset` (251) accepts either one float32
  or four float32 values after the label. One height applies to all four
  corners; the four-value form retains SW, SE, NE, NW order. The MSTS 1.4
  terrain loader `FUN_006ee290`, instructions `0x006ee486–0x006ee498`, confirms
  single-value replication. The reader checks payload size after skipping the
  label, so nonempty labels do not change the decision. Empty, truncated,
  two-/three-float and overlong payloads are still rejected; no following
  sibling is consumed as a missing water value.

The old shape fixtures omitted SubObjID and therefore missed the stock layout.
The expanded `token-shape-gl` fixtures exercise absent and 0/1/2 SubObjID values
through plain/compressed full shape loading and mesh VBO readback, including
the optional shader/light-configuration children. The `tokens` suite now tests
both water layouts with empty/nonempty labels, a following sibling, and invalid
payload lengths. Proprietary assets are not added to the repository.

Verification after applying the corrections on `feature/native-token-ids`:

| Check | Result |
| --- | --- |
| Full Release application build | Passed |
| `tokens` suite | **1,533 passed, 0 failed** |
| `token-shape-gl`, including world checks and mesh VBO readback | **35 passed, 0 failed** |
| CTest: `simis_tokens`, `ace_codec`, `ace_converter` | **3/3 passed** |
| Supplied MSTS `TRAINS`, actual `SFile::load()` with Xvfb/Mesa | **129/129 loaded**, previously 8/129 |
| Supplied `fail1`, actual `TFile::load()` | **7/7 loaded**, previously 2/7; water values checked |
| Local MSTS installation's `ROUTES`, actual `TFile::load()` | **38/38 loaded** |

The 121 originally failing stock shapes are binary; the eight survivors are
text and bypassed the affected binary reader. Stock shape success here means
successful loading, not visual/pixel comparison. The larger external terrain
corpus was not rerun. The stock diagnostics linked the rebuilt application
objects (shapes) or compiled the branch readers (terrain); their drivers and
logs were kept outside the repository under `/tmp/tsre-stock-parser-fixes.Qfs0o5`
and `/tmp/tsre-stock-parser-diagnosis.70Fi8U`. These are local, temporary test
artifacts, not distributable fixtures. No Windows access or stock-asset edits
were used. This result does not resolve the separate recovery-policy concerns.

## Verification results for the original implementation

Linux/WSL, GCC 16.2.1, Qt 6.11.2. Full Release application build succeeded.
The original mixed LF/CRLF endings of unchanged lines were restored afterward
with a whitespace-only formatter; it asserted unchanged non-whitespace line
content. The whitespace check passes with Git's `cr-at-eol` setting.

| Check | Result |
| --- | --- |
| Direct static enum audit against clean MSTS 1.4 | All **1,437 shared native names/IDs match** |
| Direct static enum audit against clean Bin 1.8 | All **1,437 shared native names/IDs match** |
| Portable `tokens` suite, Debug and Release | **1,519 passed, 0 failed** |
| Same token suite with AddressSanitizer + UndefinedBehaviorSanitizer | **1,519 passed**, no sanitizer diagnostics; leak detection disabled, see below |
| `token-world` | **27 passed, 0 failed** |
| `token-shape-gl` | **29 passed, 0 failed**, including full plain/compressed shape LOD VBO readback |
| Existing `terrain-material`, BC1 output | **484 passed, 0 failed** |
| Existing `terrain-material`, RGB output | **484 passed, 0 failed** |
| Existing `terrain-material-gl` | **0 failures**, Mesa llvmpipe / LLVM 22.1.8 |
| Existing `selection-id` | **16 passed, 0 failed** |
| Existing `terrain-grid` | **66 passed, 0 failed** |
| Existing `terrain-edges` | **52 passed, 0 failed** |
| Existing `terrain-brush` | **360 passed, 0 failed** |
| Existing `terrain-normals` | **132 passed, 0 failed**, 16,899,906 compared vertices |
| CTest: `simis_tokens`, `ace_codec`, `ace_converter` | **3/3 passed** |

The portable golden table contains 1,435 reference enum entries, including
`error = 0`. The executable comparison excludes that sentinel and adds the
recovered US-buffer and two sound forms, giving 1,437 native matches. The
registry has 1,466 unique canonical IDs and case-insensitive names including
extensions/internal messages. No proprietary files are needed by the tests.

The new fixtures exercise wrong-namespace low-word collisions, unaligned reads,
high-bit namespaces through `0xFFFFFFFF`, labels at each level, unknown siblings,
invalid/overflowing lengths, nested parent bounds and positional truncation.
Terrain fixtures include all three extensions, high unsigned material UiDs,
invalid-presence behavior, prototype-ID nonrecognition, AS/US order/labels and
ordinary/new terrain byte-for-byte round-trips. Network tests use the actual
shared encoder/decoder, check all eight golden IDs and parse unchanged nested
terrain blobs at a nonzero message offset.

World tests compare all 15 supported native W object factories with the Unicode
path and check view spheres, dynamic-track sections, signal units, WS roots,
unknown extensions and rollback. Shape tests cover named shaders (including
long names), labeled arrays, linear/TCB/slerp keys and actual mesh upload bytes.
The GL test is a vertex-data comparison, not a visual MSTS capture.

Two test-environment issues were resolved during verification:

- The first full build exposed missing sound-form enum declarations; both IDs
  were verified against the executable tables before being registered.
- An initial terrain-GL launch had seven failures because it could not locate
  `appdata/0.7/shaders*` from the build-directory launch path. Repeating it from
  the isolated layout below, with the repository appdata linked, passed with
  zero failures. No rendering-code fix was needed.

LeakSanitizer was disabled for the standalone terrain fixtures: legacy `TFile`
ownership/destruction is not self-contained and this migration does not redesign
it. The sanitizer result covers invalid memory access/undefined behavior, not a
claim that the full legacy application or every fixture is leak-free.

## Repeating the tests in another checkout

All paths below are relative to the TSRE repository. Use local Linux tools;
Windows-host access still requires explicit user approval.

```sh
cmake -S . -B build-tokens -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-tokens -j 4
ctest --test-dir build-tokens --output-on-failure
```

For core checks without building the full application:

```sh
cmake -S . -B build-token-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTSRE_BUILD_ACE_TESTS=OFF
cmake --build build-token-tests --target tsre_token_tests -j 4
QT_FORCE_STDERR_LOGGING=1 build-token-tests/tests/tokens/tsre_token_tests
```

The full application derives its working directory from the executable path.
Use an isolated copy with the repository shaders, rather than the normal user
installation/settings. Run build commands to completion first.

```sh
token_run_dir=$(mktemp -d /tmp/tsre-token-check.XXXXXX)
cp build-tokens/TSRE5vc "$token_run_dir/TSRE5vc"
ln -s "$PWD/appdata" "$token_run_dir/appdata"
export XDG_CONFIG_HOME="$token_run_dir/config"
export XDG_DATA_HOME="$token_run_dir/data"
export QT_FORCE_STDERR_LOGGING=1
QT_QPA_PLATFORM=offscreen "$token_run_dir/TSRE5vc" --test --test-suite token-world --appdata-profile
xvfb-run -a env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 "$token_run_dir/TSRE5vc" --test --test-suite token-shape-gl --appdata-profile
QT_QPA_PLATFORM=offscreen "$token_run_dir/TSRE5vc" --test --test-suite terrain-material --appdata-profile
QT_QPA_PLATFORM=offscreen TSRE_TERRAIN_MATERIAL_RGB=1 "$token_run_dir/TSRE5vc" --test --test-suite terrain-material --appdata-profile
xvfb-run -a env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 "$token_run_dir/TSRE5vc" --test --test-suite terrain-material-gl --appdata-profile
```

Run the other CPU suites with the same invocation, substituting `selection-id`,
`terrain-grid`, `terrain-edges`, `terrain-brush` or `terrain-normals`.
Xvfb is needed only for the software-OpenGL checks, not for the token/core suite.

Sanitizer build used:

```sh
cmake -S . -B build-token-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTSRE_BUILD_ACE_TESTS=OFF -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-token-asan --target tsre_token_tests -j 4
ASAN_OPTIONS=detect_leaks=0 QT_FORCE_STDERR_LOGGING=1 build-token-asan/tests/tokens/tsre_token_tests
```

## Testing-only handoff after the user publishes the branch

The user has approved the local commit and will publish the branch themselves.
After publication, fetch `feature/native-token-ids`; record the tested revision
with `git rev-parse HEAD`. Do not merge the older
`docs/tsre-token-migration-plan` branch as if it contained this implementation.

Ask the next agent to **review/test the current change, not blindly reimplement
the historical plan or assume the parser integration is finished**. Recovery
corrections require a separately scoped implementation request; the TODO list
is not authorization to perform them:

1. Fetch the approved `feature/native-token-ids` commit and repeat the commands
   above in a clean checkout, preserving any unrelated local work.
2. Build with the user's normal toolchain. If that requires Windows host access,
   prepare the command/task and obtain approval before running anything there.
3. Open copies of representative ordinary binary/Unicode W and S files and
   ordinary terrain tiles. Check selection, property values, texture/material
   behavior and animated shapes. This local run used generated fixtures, not
   broad real-route certification.
4. Recreate the experimental procedural tiles under the new implementation;
   verify paint/bake/save/reopen and new wire IDs. Do not add migration support
   or rewrite the old three tiles unless separately requested.
5. Test a matched client/server pair with real terrain/QT exchange. Header and
   nested-file tests pass, but a live network route session was not exercised.
   Check clear rejection when a new peer receives legacy IDs.

Remain aware of explicit non-goals: no binary W converter, no Ruler/ShapeTemplate
or ORTS extension payload codecs, incomplete binary SoundRegion fields, no ORTS
strict-container policy change and no procedural renderer for MSTS/ORTS.
Unknown blocks are skipped, not round-trip-preserved.
