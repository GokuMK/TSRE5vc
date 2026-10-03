# Task 12 - Parity Automation And Performance Gate

> **Legacy pipeline removed (2026-10-03).** The gather renderer is the only
> pipeline and `core.rendering.pipeline` no longer exists, so commands below
> that select a pipeline do not apply. To check a renderer change, capture a
> baseline and compare against it; see `00-task-roadmap.md`.

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
  Legacy records the scene as `sceneTerrain`, `sceneWorld` (objects, overlays,
  pointer) and `sceneWater`; `sceneTotal` sums the scene phases for both.
- Heap churn (both pipelines): `RenderItem` constructions and `Mat4::clone`
  calls during the frame. The counters are process-wide and always active.
- Gather queue (gather only): queued frame-owned items, packet instances,
  packet and texture runs in the draw loop, draw calls, `renderFrame` flushes, and items,
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

The capture pauses the simulation (`RouteEditorGLWidget::setSimulationPaused`),
because traffic and animation advance with wall-clock time that separate
processes do not share. Content loading continues. The report still notes
views that did not settle or changed during capture.

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

### After the packet/instance split (task 02 contract)

Same routes and views. Worst view per route; counts per gather frame.

| Route | Max RMSE | Max diff px | Pick mismatches | Matrix clones | Items created |
|---|---|---|---|---|---|
| EUROPE1 | 4.35 -> 3.23 | 0.35% -> 0.18% | 0/864 | 2512-2836 -> 210 | 204-546 (unchanged) |
| JAPAN1 | 1.95 -> 1.76 | 0.05% -> 0.01% | 0/864 | 5467-5904 -> 718 | 796-1208 (unchanged) |
| USA1 | 3.34 -> 3.06 | 0.37% -> 0.24% | 0/864 | 4061-4382 -> 677 | 813-1149 (unchanged) |
| BNSF_SCENIC | 3.68 -> 3.64 | 0.78% -> 0.75% | 0/864 | 3031-3537 -> 287 | 159-900 (unchanged) |

The remaining matrix clones are constant per route and come from `OglObj`
and animated shapes; the items created are the producers that still build
frame-owned items (terrain, `OglObj`, animated shapes).

### After moving producers to persistent packets

`OglObj`, terrain patches, animated shapes and `SFileComplex` submit reused
packets. Worst view per route; counts per gather frame.

| Route | Max RMSE | Max diff px | Pick mismatches | Items created | Matrix clones |
|---|---|---|---|---|---|
| EUROPE1 | 3.29 | 0.18% | 0/864 | 204-546 -> 0 | 210 -> 22 |
| JAPAN1 | 1.76 | 0.01% | 0/864 | 796-1208 -> 0 | 718 -> 25 |
| USA1 | 3.06 | 0.24% | 0/864 | 813-1149 -> 0 | 677 -> 46 |
| BNSF_SCENIC | 3.64 | 0.75% | 0/864 | 159-900 -> 16-266 | 287 -> 183 |

Gathered producers no longer allocate in steady frames. What remains comes
from passes that the gather frame still draws directly: on BNSF_SCENIC the
paged distant terrain (`TerrainLibQt::renderLo` -> `Terrain::render` ->
`TerrainMeshPaged::drawPatch`) builds a temporary `RenderItem` per patch, as it
does in legacy, and the direct passes (sky, water, pointer) clone matrices
through `GLUU::mvPushMatrix`.

### After pass buckets and texture handles (task 11), simulation paused

Worst view per route; counts per gather frame. Every view settled.

| Route | Max RMSE | Max diff px | Pick mismatches | Items created | Matrix clones | Draws |
|---|---|---|---|---|---|---|
| EUROPE1 | 2.19 | 0.18% | 0/864 | 0 | 22 | 581-2207 |
| JAPAN1 | 1.91 | 0.04% | 0/864 | 0 | 25 | 2014-4616 |
| USA1 | 2.89 | 0.24% | 0/864 | 0 | 46 | 1752-3664 |
| BNSF_SCENIC | 3.64 | 0.74% | 0/864 | 16-266 | 183 | 895-6028 |

Before the simulation was paused, EUROPE1 `down` differed by a car-spawner
bus at another position in each process (RMSE 5.03, one pick mismatch).

### After moving sky, distant terrain, water, pointer and UI to the renderer

The gather frame no longer draws anything directly. Worst view per route;
counts per gather frame. Every view settled; distant-phase primitives equal
legacy in every view.

| Route | Max RMSE | Max diff px | Pick mismatches | Items created | Matrix clones | Draws |
|---|---|---|---|---|---|---|
| EUROPE1 | 2.19 | 0.18% | 0/864 | 0 | 0 | 877-2503 |
| JAPAN1 | 1.91 | 0.04% | 0/864 | 0 | 0 | 2016-4618 |
| USA1 | 2.89 | 0.24% | 0/864 | 0 | 0 | 1868-3780 |
| BNSF_SCENIC | 3.64 | 0.74% | 0/864 | 0 | 0 | 1250-6543 |

Draw counts rose because distant terrain and water are now counted as
renderer draws.

### With the gather shadow pass

Shadows-off results are unchanged from the table above. With shadows on
(`parity-views-shadows.json`) gather matches legacy within RMSE 3.49 and 1.02%
of pixels, with no picking mismatches and shadow-pass primitives within
0.15%; see task 10. Gather frames still create no render items and clone no
matrices with shadows on.
