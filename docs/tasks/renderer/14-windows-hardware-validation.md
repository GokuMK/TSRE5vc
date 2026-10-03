# Task 14 - Windows Hardware Validation Of The Gather Renderer

## Objective

Validate the gather renderer on Windows with hardware OpenGL before any legacy
code is removed. Collect parity, performance and interactive results that the
Linux development machine cannot produce (it has only Mesa llvmpipe software
GL).

This task produces evidence, not code changes. Do not modify source files,
shaders, settings files in the repository, or route content. If something
fails, record it and continue with the remaining steps.

## Background

Branch `feature/gather-renderer` moves all route-editor drawing into the gather
pipeline (tasks 02, 05, 09, 10, 11, 12). On llvmpipe, gather matches legacy
images within 1% of pixels on four routes, with no picking mismatches, and
allocates no render items or matrices per frame. Frame times from llvmpipe are
meaningless, and driver-specific behaviour is untested.

Rules that apply to every step:

- Start each run with one pipeline: `--set core.rendering.pipeline=legacy` or
  `--set core.rendering.pipeline=gather`.
- Never switch pipelines at runtime (no Ctrl+Shift+F12, no `validation`
  mode). Runtime switching causes texture and state artifacts that are not
  renderer defects.
- Run every command from the repository root, so the executable finds
  `appdata/0.7/` and relative output paths resolve under `build/`.

## 1. Build

1. Check out `feature/gather-renderer` and record the commit (`git rev-parse
   --short HEAD`).
2. Build a Release `TSRE5vc.exe` with your usual Windows toolchain. CI uses
   Qt 6.10.1, MinGW 13.1 and Ninja (`.github/workflows/release.yml`):

   ```powershell
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build --parallel
   ```

   Add the OpenAL options from the release workflow if your environment needs
   them. Use a Release build for every measurement.
3. Run the automated suites that need a GL context and record pass/fail:

   ```powershell
   foreach ($suite in "selection-id","signal-selection","consist-preview-gl",
                      "shape-complex","shape-complex-gl","token-shape-gl",
                      "transfer-mesh","transfer-depth-gl") {
       .\build\TSRE5vc.exe --test --test-suite=$suite *> "build\test-$suite.log"
       "$suite exit=$LASTEXITCODE"
   }
   ```

   Known result: `shape-complex-gl` fails one check, "unsaved edits block
   compaction", also on `main`. Report any other failure with its log lines
   containing `FAIL` or `OK false`.

## 2. Record the environment

For each machine and GPU tested, record:

- Windows version, CPU, RAM.
- GPU model, driver version, and the `glRenderer` string from any
  `capture.json` produced in step 3.
- Display resolution and scale factor.
- Values of these settings in the profile used (Settings window or the
  profile file): `core.rendering.tileRadius`,
  `core.rendering.objectLodDistance`, `core.rendering.antiAliasingSamples`,
  `core.rendering.shadows.enabled`, `core.rendering.shadow.primaryMapSize`,
  `core.rendering.shadow.distantMapSize`, `core.rendering.terrainMesh`,
  `core.rendering.threadedTextureLoading`.

Test every GPU vendor available (NVIDIA, AMD, Intel). One full set of results
per GPU.

## 3. Parity captures

Routes, if installed:

| Route | Game root |
|---|---|
| EUROPE1, JAPAN1, USA1 | MSTS installation |
| BNSF_SCENIC | Open Rails `TS_STARTER_ROUTE` content |
| One or two of your largest routes | your game root |

For each route and each cases file `tests\renderer\parity-views.json`
(shadows off) and `tests\renderer\parity-views-shadows.json` (shadows on):

```powershell
$root  = "D:\Train Simulator"     # game root containing ROUTES
$route = "EUROPE1"
$cases = "tests\renderer\parity-views.json"
foreach ($pipeline in "legacy","gather") {
    .\build\TSRE5vc.exe --game-root $root --route $route --test `
        --test-suite=renderer-capture --test-cases $cases `
        --set core.rendering.pipeline=$pipeline *> "build\capture-$route-$pipeline.log"
    "$pipeline exit=$LASTEXITCODE"
}
.\build\TSRE5vc.exe --game-root $root --route $route --test `
    --test-suite=renderer-compare --test-cases $cases *> "build\compare-$route.log"
"compare exit=$LASTEXITCODE"
```

The capture runs with the editor window hidden and the simulation paused;
each view waits until its image is stable. A route takes several minutes.

Output: `build\renderer-parity\<route>\report.md` and `report.json` for the
shadows-off set; `build\renderer-parity-shadows\<route>\...` for the
shadows-on set. Each also has `legacy\` and `gather\` images and
`<view>-diff.png` heatmaps (red: pixels that differ).

Expected on llvmpipe, for comparison: worst view RMSE up to about 3.6, up to
about 1% of pixels different, 0 pick mismatches, gather `Items created` 0.
Remaining differences are scattered pixels on alpha-blended foliage and
ground clutter.

Report for each route and cases file:

- The `report.md` table and its Notes section.
- Any view with RMSE above 5, more than 1% different pixels, or any pick
  mismatch: attach `legacy\<view>.png`, `gather\<view>.png` and
  `<view>-diff.png`, and describe what differs (missing object, wrong
  texture, shadow, colour, position).
- Capture failures: the last 30 lines of the capture log.

## 4. Performance

Use `tests\renderer\perf-views.json`: 1920x1080, shadows on, 30 timed frames
per view, few picking samples. Run it like step 3 for EUROPE1, JAPAN1 and your
largest route. Close other GPU-heavy programs and keep the machine on mains
power.

Each pipeline's `capture.json` (`build\renderer-perf\<route>\<pipeline>\`)
records per view:

- `stats.minCpuMs`: the fastest of the 30 frames. It is measured after the
  frame's GPU queries have been read back, so on hardware it approximates the
  full frame time, CPU submission plus GPU completion.
- `stats.drawCalls`, `passDraws` (per pass and per shadow map),
  `renderItemsCreated`, `matrixClones`.
- `stats.phases.<phase>.primitives` and `.samples` for `shadow`, `sky`,
  `distant`, `sceneTotal`, `ui`.

Report a table per route:

| View | Legacy minCpuMs | Gather minCpuMs | Gather drawCalls | Gather shadow0 / shadow1 draws |
|---|---|---|---|---|

Run the whole performance set twice and report both, so run-to-run variation
is visible. If gather is slower than legacy on any view by more than 10%,
say so explicitly.

## 5. Interactive checks

Start the editor normally, once with each pipeline, on one of your own
routes. The FPS label is off by default; these commands turn it on:

```powershell
.\build\TSRE5vc.exe --set core.rendering.pipeline=gather --set core.interface.hud.showEditorFps=true
.\build\TSRE5vc.exe --set core.rendering.pipeline=legacy --set core.interface.hud.showEditorFps=true
```

For each item, mark gather as Same as legacy, Different (describe), or
Broken (describe, with a screenshot):

1. Camera movement across several tiles while textures and shapes load:
   nothing stays missing or magenta after loading finishes.
2. Clicking objects selects the clicked object; Ctrl+click adds to the
   selection; clicking terrain selects the terrain patch.
3. Moving, rotating and placing objects; the 3D pointer follows the terrain
   and objects under the mouse.
4. Terrain height editing and terrain texture painting update immediately.
5. Placing track and road with the live flex tool, and the ruler tool.
6. Track and road database lines and items, markers, activity objects and
   paths (toggle them in the View menu).
7. Shadows on and off (Settings); shadows have no acne, missing casters or
   swimming. Compare shadows near the camera and at 300-700 m.
8. Water, sky and distant terrain on a route that has them.
9. Compass and the FPS label.
10. Play mode with an activity: the train HUD (speed, distance) appears and
    updates.
11. Window resize and moving the window between monitors with different
    scale factors: picking still hits what is under the mouse.
12. Note the FPS label in a dense area for both pipelines with the same
    camera position.

## Results

Write the results to
`docs/tasks/renderer/reports/windows-hardware-validation-<gpu>-<yyyy-mm-dd>.md`
with these sections, one file per GPU:

```markdown
# Windows hardware validation - <GPU> - <date>

## Environment
Commit, Windows, CPU, RAM, GPU, driver, glRenderer, resolution and scale,
rendering settings from step 2.

## Automated suites
| Suite | Exit | Notes |

## Parity
Per route and cases file: the report.md table, notes, and any flagged views
with attached images.

## Performance
Per route: the table from step 4, both runs.

## Interactive checks
| # | Check | Result | Notes |

## Problems
Anything that failed or looked wrong, with steps to reproduce.
```

Store referenced images next to the report in a folder with the same name.
Do not commit `build\` output.

## Acceptance

This task is done when at least one dedicated GPU and, if available, one
integrated GPU have a complete report. The decision to remove the legacy
pipeline (task 12) uses these reports.

## Status (2026-10-03)

One synthetic run on an integrated AMD GPU (route CMK) is recorded in
`reports/windows-hardware-validation-amd-custom-gpu-0932-2026-10-03.md`, and
the user checked the interactive items manually: no gather defects found.

Further remote hardware runs are deferred until hardware results block other
work. Before running again, improve the harness:

- Time CPU submission before the GPU query readback, time the GPU with a
  `GL_TIME_ELAPSED` query, keep every frame, and report median and p90
  instead of the minimum.
- Alternate the pipeline order between runs, repeat each at least five times,
  and use a High performance power plan.
- Read depth at each pick point; report differing IDs at equal depth as
  coplanar ties, not mismatches.
- Choose views that face dense content.
- Commit only the Markdown report, not images, logs or capture data.
