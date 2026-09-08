# TSRE native token-ID implementation and testing handoff

Date: 2026-09-08. Branch: `feature/native-token-ids`.
Base: `main` at `9f389f08450411614256d9f3e018225ddc62584a`.
Status: implemented and locally verified. Local commit approved; the user will
publish `feature/native-token-ids` for independent testing.

The user approved implementation of the
[reviewed local plan](tsre-full-token-id-migration-task.md).
[Native SIMIS token IDs](../features/native-token-ids.md) documents the final
allocation, API, compatibility break and remaining binary-codec limits.

## Changes

- Complete unsigned uint32 IDs throughout the enum, registry, terrain/world/
  shape readers, writer and network-token boundary. Removed the world-file
  offset state/subtraction and numeric SIMIS token use sites, including Core
  terrain/shape dispatch and AS/US ordering. Ordinary payload numbers remain.
- Native sparse forms and all 34 brake-order corrections; canonical names
  replace the invented `...2` aliases. Added recovered `terrain_sample_usbuffer`
  (282), `Soundsource` (`0x00040043`) and `Soundregion` (`0x00040044`).
- Three procedural terrain file blocks use `TSRETerrainMaterialBuffer`,
  `TSRETerrainBakedMaterial`, `TSRETerrainMaterialMap` at `0x00061000–02`.
  No prototype aliases or test-tile conversion. Eight network names keep their
  underscores and use `0x00060001–08` on both ends.
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
- API/allocation documentation, actual world-extension inventory, portable
  generated-fixture tests and current procedural terrain docs were updated.

No ACE implementation, procedural material catalogue/bitmap format, selection
ID scheme, ORTS source, MSTS executable or existing route/test tile was changed.
No Windows-host filesystem/registry/process access or Wine run was performed.
Static executable table verification read the already-provided Linux-local
copies as data; it did not execute them.

## Verification results

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

Ask the next agent to **test the completed change, not reimplement the plan**:

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
