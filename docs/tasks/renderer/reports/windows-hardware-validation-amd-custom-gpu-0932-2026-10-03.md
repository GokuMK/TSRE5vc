# Windows hardware validation - AMD Custom GPU 0932 - 2026-10-03

> **Evidence removed after review (2026-10-03).** The images, logs and capture
> data linked below were deleted from the repository to keep it small; the
> links no longer resolve. Review conclusions:
>
> - The aerial picking mismatch is a coplanar tie: overlapping track objects
>   z-fight in the station ballast, and each pipeline's draw order picks a
>   different winner. Not a gather defect.
> - The shadows-on `yaw90` tree difference was not seen when the user checked
>   the same scene manually; most likely shapes still loading at capture time.
> - The performance deltas are within run-to-run variation (up to 23%), and
>   `minCpuMs` mixes CPU submission with GPU completion. Gather does no extra
>   GPU work (equal shadow primitives, fewer passed samples). The view faced
>   away from the route's dense content, so the run says little about load.

Synthetic validation of CMK on the available integrated AMD GPU. The user authorized CMK and confirmed readiness before measurements. Visual/interactive testing was previously performed by the user, who reported that all seemed good; this report does not independently certify those checks.

**Result: issues remain.** The aerial target-picking mismatch reproduced with shadows off/on at both tested resolutions; shadows-on yaw90 also differed. Gather aerial timing was 19.54% and 12.83% slower than legacy in the two runs. Run 2 also exceeded 10% in start and yaw270. Seven automated suites passed under both pipeline settings; the eighth had only its documented failure. Gather recorded zero render-item creations and matrix clones in all 36 gather capture views.

## Environment

- Commit: `4b21f4f4f96164c0ff130c228239c0945228246f`, branch `feature/gather-renderer`; clean checkout before testing.
- Windows 10 Pro N, version 10.0.19045, build 19045.
- CPU: AMD Custom APU 0932, 4 cores / 8 logical processors. OS-visible RAM: 15,944,110,080 bytes (14.85 GiB).
- GPU and captured `glRenderer`: AMD Custom GPU 0932. Driver: 32.0.11002.3007 (2024-06-06). Windows exposed no second GPU; no dedicated GPU was available.
- Displays: primary 3840x2160, secondary 2560x1600. System DPI: 240 (250%). The system-aware monitor query returned 240 DPI for both displays. WMI also returned an 800x1280 controller mode; the desktop bounds above come from the display API.
- AC power confirmed; Balanced power plan. Runs were serial, after the user approved proceeding. No other TSRE process was active at the start. Other applications were not forcibly closed.
- Release build, Qt 6.10.1, MinGW 13.1, Ninja. `C:/Qt6/Tools/CMake_64/bin/cmake.exe --build build --parallel 4` reported no work to do. Hardware Windows OpenGL was used, with the AMD `atio6axx.dll` driver loaded; no offscreen/software platform.
- Executable SHA-256: `4A32FF6DD0D82A3D18A7DE76803729B1A2F706E41AC553FD7FCD653D83FC8BF3`.
- Root: `C:/trainsim`; route: `CMK` (1,059 `.w` files). EUROPE1, JAPAN1, USA1 and BNSF_SCENIC were not found in the checked installations. Scope was narrowed to CMK with the user.
- Each process started explicitly as legacy or gather; no runtime pipeline switches. Tests used an isolated copy of the existing profile under `%TEMP%`, with launch-only overrides. Repository settings, shaders, source and route content were not edited.

| Setting | Profile value |
|---|---|
| `core.rendering.tileRadius` | 2 |
| `core.rendering.objectLodDistance` | 3000 |
| `core.rendering.antiAliasingSamples` | 0 |
| `core.rendering.shadows.enabled` | True |
| `core.rendering.shadow.primaryMapSize` | 2048 |
| `core.rendering.shadow.distantMapSize` | 1024 |
| `core.rendering.terrainMesh` | paged |
| `core.rendering.threadedTextureLoading` | True |

The cases file overrides shadows off/on as specified. The first parity set inherited 250% DPI and produced 2400x1350 images. It is retained below as supplemental evidence. Required-resolution parity was rerun with `QT_ENABLE_HIGHDPI_SCALING=0`, `QT_SCREEN_SCALE_FACTORS=1;1`, and verified 960x540 PNGs. The same environment produced verified 1920x1080 performance images. No desktop setting was changed.

[Environment JSON](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/environment.json), [profile used](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/settings-used.json), [process arguments, timestamps and exits](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/runs.jsonl).

## Automated suites

| Suite | Legacy exit | Gather exit | Notes |
|---|---|---|---|
| selection-id | 0 | 0 | 16 passed |
| signal-selection | 0 | 0 | 16 passed |
| consist-preview-gl | 0 | 0 | 9 passed |
| shape-complex | 0 | 0 | 55 passed |
| shape-complex-gl | 1 | 1 | 111 passed; one known failure: unsaved edits block compaction |
| token-shape-gl | 0 | 0 | 70 passed (log label: token-world) |
| transfer-mesh | 0 | 0 | 54 passed |
| transfer-depth-gl | 0 | 0 | 44 cases, 0 failures |

Both failing logs contain exactly: `shape-complex FAIL unsaved edits block compaction`. The task documents this failure on main; main was not independently rebuilt here. No additional suite failures were found.

[Legacy failure log](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/logs/shape-complex-gl-legacy.app.log), [gather failure log](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/logs/shape-complex-gl-gather.app.log).

## Parity

All comparisons below use six views and 144 picks per view. Report tables are reproduced verbatim. **The generated `Thresholds: ok` column is not a parity verdict:** these cases files omit thresholds, and the harness defaults them to disabled. Task limits were reviewed separately: RMSE >5, differing pixels >1%, or any target-picking mismatch is flagged.

### CMK - shadows off - 960x540

| View | Settled L / G | RMSE | Diff px | Pick mismatches | Scene prims L / G | Scene samples L / G | Gather draws | Items created L / G | Matrix clones L / G | Thresholds |
|---|---|---|---|---|---|---|---|---|---|---|
| start | yes / yes | 0.24 | 0.00% | 0/144 | 735954 / 737160 | 802053 / 665611 | 4579 | 521 / 0 | 4209 / 0 | ok |
| yaw90 | yes / yes | 0.56 | 0.01% | 0/144 | 661577 / 664009 | 693466 / 585592 | 3792 | 521 / 0 | 4139 / 0 | ok |
| yaw180 | yes / yes | 0.79 | 0.04% | 0/144 | 1207205 / 1231707 | 792810 / 668783 | 11239 | 517 / 0 | 4491 / 0 | ok |
| yaw270 | yes / yes | 0.11 | 0.00% | 0/144 | 712248 / 713434 | 726086 / 624040 | 6133 | 371 / 0 | 4221 / 0 | ok |
| down | yes / yes | 0.30 | 0.01% | 0/144 | 737918 / 739200 | 1537014 / 1263001 | 4583 | 523 / 0 | 4189 / 0 | ok |
| aerial | yes / yes | 3.19 | 0.36% | 1/144 | 564862 / 566144 | 716419 / 730300 | 4245 | 185 / 0 | 4189 / 0 | ok |

Notes from generated report:

- None.

**Flagged aerial:** RMSE 3.193, differing pixels 0.365%, target pick mismatches 1/144.

[Legacy](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/parity/legacy/aerial.png), [gather](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/parity/gather/aerial.png), [difference heatmap](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/parity/aerial-diff.png).

- Pixel (390, 450): legacy `world tile(0,1) obj 20 part 0`, gather `world tile(0,1) obj 145 part 0`; same target: False.

### CMK - shadows on - 960x540

| View | Settled L / G | RMSE | Diff px | Pick mismatches | Scene prims L / G | Scene samples L / G | Gather draws | Items created L / G | Matrix clones L / G | Thresholds |
|---|---|---|---|---|---|---|---|---|---|---|
| start | yes / yes | 0.24 | 0.00% | 0/144 | 735954 / 737160 | 802053 / 665611 | 4579 | 521 / 0 | 7475 / 0 | ok |
| yaw90 | yes / yes | 2.44 | 0.34% | 1/144 | 654615 / 655551 | 696222 / 584253 | 3705 | 521 / 0 | 7345 / 0 | ok |
| yaw180 | yes / yes | 0.64 | 0.04% | 0/144 | 1204769 / 1204999 | 792810 / 668646 | 11092 | 517 / 0 | 7778 / 0 | ok |
| yaw270 | yes / yes | 0.09 | 0.00% | 0/144 | 712248 / 713434 | 726086 / 624040 | 6133 | 371 / 0 | 7427 / 0 | ok |
| down | yes / yes | 0.29 | 0.01% | 0/144 | 737930 / 739200 | 1537014 / 1263001 | 4583 | 523 / 0 | 7459 / 0 | ok |
| aerial | yes / yes | 2.69 | 0.30% | 1/144 | 564862 / 566144 | 716419 / 730300 | 4245 | 185 / 0 | 7455 / 0 | ok |

Notes from generated report:

- None.

**Flagged yaw90:** RMSE 2.436, differing pixels 0.337%, target pick mismatches 1/144.

[Legacy](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/parity-shadows/legacy/yaw90.png), [gather](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/parity-shadows/gather/yaw90.png), [difference heatmap](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/parity-shadows/yaw90-diff.png).

- Pixel (90, 270): legacy `world tile(0,0) obj 2164 part 0`, gather `world tile(0,0) obj 1539 part 0`; same target: False.

**Flagged aerial:** RMSE 2.694, differing pixels 0.304%, target pick mismatches 1/144.

[Legacy](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/parity-shadows/legacy/aerial.png), [gather](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/parity-shadows/gather/aerial.png), [difference heatmap](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/parity-shadows/aerial-diff.png).

- Pixel (390, 450): legacy `world tile(0,1) obj 20 part 0`, gather `world tile(0,1) obj 145 part 0`; same target: False.

### Supplemental CMK - shadows off - 2400x1350

| View | Settled L / G | RMSE | Diff px | Pick mismatches | Scene prims L / G | Scene samples L / G | Gather draws | Items created L / G | Matrix clones L / G | Thresholds |
|---|---|---|---|---|---|---|---|---|---|---|
| start | yes / yes | 0.39 | 0.01% | 0/144 | 735954 / 737160 | 4989907 / 4105138 | 4579 | 521 / 0 | 4208 / 0 | ok |
| yaw90 | yes / yes | 0.85 | 0.04% | 0/144 | 655211 / 662293 | 4313891 / 3593983 | 3758 | 521 / 0 | 4138 / 0 | ok |
| yaw180 | yes / yes | 0.58 | 0.03% | 0/144 | 1204789 / 1206253 | 4926047 / 4111628 | 11125 | 517 / 0 | 4490 / 0 | ok |
| yaw270 | yes / yes | 0.11 | 0.00% | 0/144 | 712248 / 713434 | 4502669 / 3850574 | 6133 | 371 / 0 | 4220 / 0 | ok |
| down | yes / yes | 0.28 | 0.00% | 0/144 | 737930 / 739200 | 9581924 / 7828835 | 4583 | 523 / 0 | 4192 / 0 | ok |
| aerial | yes / yes | 3.36 | 0.40% | 1/144 | 564862 / 566144 | 4457370 / 4499941 | 4245 | 185 / 0 | 4188 / 0 | ok |

Notes from generated report:

- None.

**Flagged aerial:** RMSE 3.359, differing pixels 0.404%, target pick mismatches 1/144.

[Legacy](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/native-parity/legacy/aerial.png), [gather](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/native-parity/gather/aerial.png), [difference heatmap](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/native-parity/aerial-diff.png).

- Pixel (975, 1125): legacy `world tile(0,1) obj 20 part 0`, gather `world tile(0,1) obj 145 part 0`; same target: False.

### Supplemental CMK - shadows on - 2400x1350

| View | Settled L / G | RMSE | Diff px | Pick mismatches | Scene prims L / G | Scene samples L / G | Gather draws | Items created L / G | Matrix clones L / G | Thresholds |
|---|---|---|---|---|---|---|---|---|---|---|
| start | yes / yes | 0.38 | 0.01% | 0/144 | 735954 / 737160 | 4989907 / 4105138 | 4579 | 521 / 0 | 7473 / 0 | ok |
| yaw90 | yes / yes | 2.53 | 0.33% | 1/144 | 654615 / 655551 | 4332814 / 3585357 | 3705 | 521 / 0 | 7343 / 0 | ok |
| yaw180 | yes / yes | 0.59 | 0.03% | 0/144 | 1204761 / 1204999 | 4926047 / 4111534 | 11092 | 517 / 0 | 7776 / 0 | ok |
| yaw270 | yes / yes | 0.09 | 0.00% | 0/144 | 712248 / 713434 | 4502669 / 3850574 | 6133 | 371 / 0 | 7425 / 0 | ok |
| down | yes / yes | 0.28 | 0.00% | 0/144 | 737930 / 739200 | 9581924 / 7828835 | 4583 | 523 / 0 | 7457 / 0 | ok |
| aerial | yes / yes | 2.78 | 0.33% | 1/144 | 564862 / 566144 | 4457370 / 4499941 | 4245 | 185 / 0 | 7453 / 0 | ok |

Notes from generated report:

- None.

**Flagged yaw90:** RMSE 2.530, differing pixels 0.331%, target pick mismatches 1/144.

[Legacy](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/native-parity-shadows/legacy/yaw90.png), [gather](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/native-parity-shadows/gather/yaw90.png), [difference heatmap](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/native-parity-shadows/yaw90-diff.png).

- Pixel (225, 675): legacy `world tile(0,0) obj 2164 part 0`, gather `world tile(0,0) obj 1539 part 0`; same target: False.

**Flagged aerial:** RMSE 2.777, differing pixels 0.334%, target pick mismatches 1/144.

[Legacy](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/native-parity-shadows/legacy/aerial.png), [gather](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/native-parity-shadows/gather/aerial.png), [difference heatmap](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/native-parity-shadows/aerial-diff.png).

- Pixel (975, 1125): legacy `world tile(0,1) obj 20 part 0`, gather `world tile(0,1) obj 145 part 0`; same target: False.

All capture and comparison processes exited 0. All 48 parity capture views reported settled images, stable timing frames and valid GPU queries. None exceeded RMSE 5 or 1% differing pixels: the maximum at 960x540 was RMSE 3.193 and 0.365%; including supplemental captures, RMSE 3.359 and 0.404%. Target picking nevertheless failed in the flagged views above.

The aerial differences are concentrated along the track/ballast/platform surfaces; the overall buildings, terrain and camera alignment agree. Shadows-on yaw90 heatmaps at both resolutions highlight a subset of trees whose visible foliage/silhouettes differ. These observations do not establish the cause of the picking differences.

## Performance

CMK, shadows on, 1920x1080, six views, 30 timed frames per view, 4x3 picking grid. Two complete passes, each legacy then gather, in separate processes. The table uses the minimum of 30 instrumented frames. Positive delta means gather is slower. This is neither average FPS nor an interactive frametime distribution.

### CMK - run 1

| View | Legacy minCpuMs | Gather minCpuMs | Gather delta | Gather drawCalls | Gather shadow0 / shadow1 draws |
|---|---|---|---|---|---|
| start | 21.210 | 22.977 | 8.33% | 4579 | 2264 / 2667 |
| yaw90 | 17.069 | 17.922 | 5.00% | 3705 | 1760 / 1814 |
| yaw180 | 33.944 | 30.471 | -10.23% | 11092 | 3650 / 5085 |
| yaw270 | 18.611 | 20.014 | 7.54% | 6133 | 2722 / 3211 |
| down | 26.722 | 28.420 | 6.36% | 4583 | 2264 / 2667 |
| aerial | 15.939 | 19.054 | 19.54% | 4245 | 2264 / 2667 |

Raw captures: [legacy](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/perf-run1/legacy/capture.json), [gather](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/perf-run1/gather/capture.json).

### CMK - run 2

| View | Legacy minCpuMs | Gather minCpuMs | Gather delta | Gather drawCalls | Gather shadow0 / shadow1 draws |
|---|---|---|---|---|---|
| start | 19.968 | 22.230 | 11.33% | 4579 | 2264 / 2667 |
| yaw90 | 18.154 | 19.111 | 5.27% | 3705 | 1760 / 1814 |
| yaw180 | 34.754 | 33.564 | -3.43% | 11092 | 3650 / 5085 |
| yaw270 | 18.877 | 22.686 | 20.18% | 6133 | 2722 / 3211 |
| down | 24.823 | 21.861 | -11.93% | 4583 | 2264 / 2667 |
| aerial | 17.152 | 19.353 | 12.83% | 4245 | 2264 / 2667 |

Raw captures: [legacy](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/perf-run2/legacy/capture.json), [gather](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/perf-run2/gather/capture.json).

**Gather exceeded the 10% slowdown threshold:**

- Run 1, aerial: 19.54% slower.
- Run 2, start: 11.33% slower.
- Run 2, yaw270: 20.18% slower.
- Run 2, aerial: 12.83% slower.

All 24 performance capture views settled, remained stable across timed frames and had valid GPU queries. Each of the 12 gather views recorded 0 render items created and 0 matrix clones; the corresponding legacy ranges were 185-523 items and 7,344-7,777 clones. Both pipelines recorded nonzero shadow primitives in every performance view. Sky and distant phase counters were zero throughout.

### Run-to-run variation

| View | Legacy run 2 vs run 1 | Gather run 2 vs run 1 |
|---|---|---|
| start | -5.86% | -3.25% |
| yaw90 | 6.35% | 6.63% |
| yaw180 | 2.39% | 10.15% |
| yaw270 | 1.43% | 13.35% |
| down | -7.11% | -23.08% |
| aerial | 7.61% | 1.57% |

All per-pass draw counts, allocation counters and requested phase primitive/sample counts are preserved in [performance-counters.csv](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/performance-counters.csv), as well as the unmodified capture JSONs. The instrumented `drawCalls` counter does not include the separate shadow counters; legacy draw-call zeros are an instrumentation limitation, not zero rendering.

### Harness review and coverage limits

- `RenderStats::endFrame()` reads blocking `GL_QUERY_RESULT` values before stopping its timer. `minCpuMs` therefore includes CPU submission and GPU-query completion, as described by the task; it is not pure CPU time or a GPU timestamp measurement. It excludes framebuffer copying/hashing, event pumping and picking outside that timed frame.
- Capture JSON retains the minimum time but its other counters come from the final timed frame, not necessarily the minimum-time frame. The full set of 30 durations is not retained, so no mean, median or tail-latency claim is possible.
- The minimum is a best-case sample. Both runs used the same pipeline order; filesystem caches, thermal/power variation and other background activity are not completely controlled.
- Stable-image detection and paused simulation do not prove that every asynchronous content load is finished. In particular, geometry counters vary slightly in some directions between processes.
- Sky, distant terrain and water coverage depends on this view set and route. CMK logs a missing `SHAPES/skydome.s`; zero phase counts cannot establish feature parity. This run does not certify routes with those features.

Reviewed implementations: [capture, timing and comparison harness](../../../../src/tsre/tests/RendererParityTestSuite.cpp), [frame statistics and blocking query reads](../../../../src/tsre/renderer/RenderStats.cpp).

## Interactive checks

User report before this run: "I already done visual tests, all seems good." No per-item results or screenshots were supplied. The assistant performed synthetic tests only, as requested. Each item below therefore remains user-reported in aggregate, not individually verified here.

| # | Check | Result | Notes |
|---|---|---|---|
| 1 | Camera movement and streaming | Not individually verified | User reported visual tests looked good. |
| 2 | Object, multiselect and terrain picking | Not individually verified | User reported visual tests looked good. |
| 3 | Move, rotate, place and 3D pointer | Not individually verified | User reported visual tests looked good. |
| 4 | Terrain height and texture editing | Not individually verified | User reported visual tests looked good. |
| 5 | Track/road flex and ruler tools | Not individually verified | User reported visual tests looked good. |
| 6 | Database lines/items, markers, activities and paths | Not individually verified | User reported visual tests looked good. |
| 7 | Shadows near and far | Not individually verified | User reported visual tests looked good. |
| 8 | Water, sky and distant terrain | Not individually verified | User reported visual tests looked good. |
| 9 | Compass and FPS label | Not individually verified | User reported visual tests looked good. |
| 10 | Play-mode train HUD | Not individually verified | User reported visual tests looked good. |
| 11 | Resize and mixed-DPI monitor movement | Not individually verified | User reported visual tests looked good. |
| 12 | Dense-area interactive FPS | Not individually verified | User reported visual tests looked good. |

## Problems

1. **Target-picking parity is not clean.** Flagged views and exact pixels/IDs are listed above with legacy, gather and heatmap images. Reproduce with CMK and the corresponding parity cases file, each pipeline in a fresh process. The aerial mismatch occurs at both tested resolutions. No source change or root-cause claim is made in this evidence-only task.
2. **Known automated failure:** `shape-complex-gl`, "unsaved edits block compaction", under both launch pipeline settings. Run `--test --test-suite=shape-complex-gl --set core.rendering.pipeline=legacy` (or `gather`) to reproduce.
3. **Capture dimensions depend on DPI.** The task dimensions are logical widget sizes unless high-DPI scaling is disabled for the test process. Verify the saved PNG dimensions. Supplemental high-DPI evidence is explicitly separated from the required-resolution results.
4. **Performance threshold exceeded:** aerial was 19.54% and 12.83% slower across the two runs; start and yaw270 were 11.33% and 20.18% slower in run 2. Reproduce with `perf-views.json` and the isolated profile. Run-to-run variation is substantial in some views (gather down: -23.08%), so these two passes do not establish a precise long-term speed ratio.
5. **Missing coverage:** no dedicated GPU, no additional GPU vendor, and no standard-route measurements. CMK has missing sky content. Interactive checks were not individually recorded. The full Task 14 acceptance gate is therefore still open; this report alone does not justify removing the legacy pipeline.

## Reproduction and evidence

[Serial runner](windows-hardware-validation-amd-custom-gpu-0932-2026-10-03/reproduce.ps1) records process exits and preserves both performance runs. It contains this machine's absolute paths; copy it to `build/hardware-validation.ps1` in this checkout. Its current DPI overrides reproduce the required-resolution tests. To reproduce supplemental 250% DPI parity, omit `QT_ENABLE_HIGHDPI_SCALING` and use the original `QT_SCREEN_SCALE_FACTORS=1`. Use the archived profile values and a quiet machine on AC power.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File build\hardware-validation.ps1 -Phase suites
powershell.exe -NoProfile -ExecutionPolicy Bypass -File build\hardware-validation.ps1 -Phase parity
powershell.exe -NoProfile -ExecutionPolicy Bypass -File build\hardware-validation.ps1 -Phase perf
```

All executable invocations run from the repository root and include `--settings <isolated-profile> --test --set core.system.consoleOutput=true`, plus an explicit pipeline. Capture commands include `--game-root C:\trainsim --route CMK --test-suite=renderer-capture --test-cases tests\renderer\<cases>.json`; comparisons use `renderer-compare`. Full local output remains under `build/hardware-validation`, `build/renderer-parity`, `build/renderer-parity-shadows` and `build/renderer-perf`. Only the report and its evidence folder are intended for version control; no build output is staged.

Final checks: all 32 recorded processes exited (30 with exit 0, two known suite failures with exit 1); no timeout. SHA-256 hashes for the repository profile, settings.txt and startup-args.txt were unchanged. No CMK file had a modification timestamp within the test window. Git showed only this new report/evidence directory. All linked files and capture dimensions were checked.
