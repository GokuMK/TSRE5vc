# Task 12 - Parity Automation And Performance Gate

## Objective
Create repeatable validation to decide when legacy pipeline can be retired.

## Scope
- Deterministic A/B capture workflow (legacy vs gather).
- Image diff reporting (color and optional depth).
- Picking parity sampling report.
- Render counters by category and pass.
- Performance summary (draw calls, frame time, state-change proxies).

## Suggested Touch Points
- Add scripts/tooling under a new tools folder or build scripts
- Optional logging hooks in renderer and gather traversal
- `docs` instructions for running parity checks

## Requirements
- Validation can be run by AI agents and humans.
- Artifacts are easy to compare in PR reviews.

## Acceptance Criteria
- A small representative route set passes parity thresholds.
- Selection/picking parity has no critical mismatches.
- Gather mode has no major performance regression vs legacy on test set.
- Decision note recorded: keep legacy fallback or proceed with deprecation.

## Out Of Scope
- Removing legacy path without passing all gates.

## Measurement Harness (implemented)

Status: counters, per-pipeline capture, image diff and picking comparison are
implemented. Depth comparison and the performance gate are not.

### Render counters

`src/tsre/renderer/RenderStats.*` records one `FrameStats` per rendered frame
while enabled. Selection passes are not recorded.

- GPU phase counters (both pipelines): `GL_PRIMITIVES_GENERATED` and
  `GL_SAMPLES_PASSED` query totals for the phases `shadow`, `sky`, `distant`,
  `scene` and `ui`. The phases are marked in `paintGL2` and `paintGLGather`
  around code that runs in the same order in both pipelines. `scene` covers
  high-res terrain, world objects, near water, overlays and the pointer.
- Heap churn (both pipelines): `RenderItem` constructions and `Mat4::clone`
  calls during the frame. The counters are process-wide and always active.
- Gather queue (gather only): queued frame-owned items, grouped shape packets
  and instances, texture groups, draw calls, `renderFrame` flushes, and items,
  draws and CPU-side primitives per producer category (`terrain`, `world`,
  `overlay`, `other`). `paintGLGather` sets the category around each producer.

Stats are off in normal editor runs; the capture suite enables them.

### Capture and compare

Runtime pipeline switching is not used: it invalidates caches while textures
load in worker threads and leaves state from the other pipeline (for example
shadow maps). Each pipeline is captured in its own process instead.

`--test --test-suite=renderer-capture` uses the pipeline selected at startup
(`--set core.rendering.pipeline=legacy|gather`) and fails if the active
pipeline changes during the run. It opens the real `RouteEditorGLWidget` on a
route without showing it and, for each configured view:

1. Renders until the image is unchanged for `stableFrames` frames or a limit is
   hit, so streamed content can load.
2. Renders `timingFrames` frames with no events processed and keeps the last
   image and counters.
3. Reads selection IDs on a grid through
   `RouteEditorGLWidget::probeSelectionIds`, which renders a selection pass
   without applying the selection.

It writes `<output>/<route>/<pipeline>/capture.json` and one PNG per view.

`--test --test-suite=renderer-compare` needs no GL context. It reads both
captures for the same cases file and route and writes `report.json`,
`report.md` and `<view>-diff.png` to `<output>/<route>/`: RMSE, the share of
pixels whose largest channel difference exceeds `diffTolerance`, picking
mismatches (a difference in part only counts as `sameTargetOtherPart`), and
both pipelines' counters. The exit code is 1 only when a configured threshold
(`maxRmse`, `maxDiffPixelRatio`, `maxPickMismatches`) is exceeded.

The two processes do not share simulation time, so animated content can differ;
the report notes views that did not settle or changed during capture.

Run both captures and the comparison with:

```bash
scripts/renderer-parity.sh <game-root> <route> [cases.json] [extra args...]
```

The script uses `xvfb-run` when no display is available. Cases files:

- `tests/renderer/parity-views.json`: shadows off; the main parity check.
  Output `build/renderer-parity/<route>/`.
- `tests/renderer/parity-views-shadows.json`: shadows on; shows the missing
  gather shadow pass until it exists. Output `build/renderer-parity-shadows/`.

Cases file keys: `width`, `height`, `output`, `pickGrid`, `timingFrames`,
`diffTolerance`, `hud`, `compass`, `pointer`, `shadows`, `settle`
(`minFrames`, `stableFrames`, `maxFrames`, `maxSeconds`), `thresholds`, and
`views`. A view uses the route start position unless it gives `tile` and `pos`;
`offset` moves the start position and `rot` is `[yaw, pitch]` in radians, with
positive pitch looking up.

Software GL (Mesa llvmpipe) is sufficient for image, picking and counter
comparisons. Frame times from it are not representative; the performance gate
must be measured on hardware GL.

### Baseline (2026-10-01, llvmpipe, `parity-views.json`, separate processes)

Six views per route, rendered in sequence, 144 picking samples per view.
Ranges are across views; allocation counts are per frame.

| Route | RMSE | Max diff px | Pick mismatches | Max scene primitive delta | Gather draws | Items created L / G | Matrix clones L / G |
|---|---|---|---|---|---|---|---|
| EUROPE1 | 1.22-4.35 | 0.35% | 0/864 | 11.5% | 629-2250 | 16-358 / 204-546 | 2097-2200 / 2512-2836 |
| JAPAN1 | 0.99-1.95 | 0.05% | 0/864 | 1.2% | 2090-4758 | 103-515 / 796-1208 | 3770-4059 / 5467-5904 |
| USA1 | 1.61-3.34 | 0.37% | 0/864 | 0.8% | 1802-3863 | 182-518 / 813-1149 | 2479-2598 / 4061-4382 |
| BNSF_SCENIC | 1.36-3.68 | 0.78% | 0/864 | 0.1% | 895-6029 | 55-796 / 159-900 | 2789-2801 / 3031-3537 |

Findings:
- With shadows off, images differ only in scattered pixels on alpha-tested
  foliage and ground clutter. Grouped shape items are drawn in hash order, not
  traversal order. Picking matches in every sample.
- With shadows on (`parity-views-shadows.json`, EUROPE1) gather is darker over
  most of the image (RMSE 8.9-14.7, 4-11% of pixels): gather has no shadow pass,
  and its shader samples shadow maps that were never rendered.
- Legacy draws extra world primitives after the camera has moved: EUROPE1
  `yaw270` has 50,558 legacy world/overlay primitives against 32,728 in gather
  when rendered after the other views, but 28,400 against 28,432 when rendered
  alone. Content loaded during earlier views is drawn by legacy (+22k) far more
  than by gather (+4k), and the images do not show it, which points to a
  culling difference for off-screen objects. Not yet investigated per object.
- Several views did not settle within 120 frames in one or both processes.
- Gather creates hundreds to over a thousand `RenderItem`s per frame where
  legacy creates tens to hundreds; both clone thousands of matrices per frame.

An earlier version of this harness compared the pipelines in one process by
switching at runtime. That produced false results (magenta textures in gather,
gather shadows borrowed from the legacy frame) and is no longer used.
