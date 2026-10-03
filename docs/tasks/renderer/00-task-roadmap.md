# Renderer Task Roadmap

This folder contains ordered tasks for migrating the TSRE renderer from legacy immediate drawing to gather-then-render.

## Execution Order
- [x] `01-runtime-pipeline-switch.md` (superseded: the switch was removed with the legacy pipeline)
- [x] `02-renderer-core-generic-queue.md`
- [x] `03-selection-and-picking-parity.md`
- [x] `04-terrain-highres-gather.md`
- [x] `05-terrain-distant-water-sky.md` (gather draws sky, distant terrain and water through the renderer)
- [x] `06-world-objects-shape-based.md`
- [x] `07-world-objects-procedural-and-helpers.md`
- [x] `08-overlays-tdb-activity-markers.md`
- [x] `09-hud-compass-pointer.md` (gather draws pointer, compass and HUD through the renderer)
- [x] `10-shadows-gather-pass.md` (gather draws both shadow maps from the gathered queue; three-map review in the task)
- [ ] `11-shader-pass-buckets-and-custom-shaders.md` (pass buckets implemented; per-pass shaders pending)
- [ ] `13-selection-renderer-and-id-redesign.md`
- [x] `15-shape-viewer-gather.md` (Shape Viewer and Consist Editor draw through the renderer)
- [x] `14-windows-hardware-validation.md` (manual Windows checks passed; further hardware runs only when they block work)
- [x] `12-parity-automation-and-performance-gate.md` (harness compares a capture with a baseline capture)

## Legacy Pipeline Removed

The gather renderer is the only pipeline. `paintGL2`, the validation mode,
runtime switching, the `core.rendering.pipeline` and `pipelineHotSwap`
settings, and every object's immediate `render()` draw are removed. Profiles
that still store the pipeline setting drop it on load.

Objects draw by submitting persistent `RenderItem`s to `Game::currentRenderer`
from their `pushRenderItems()` functions; see `Renderer.h` for the contract.
New drawing code implements only that path.

## Checking Renderer Changes

The parity harness compares two captures made with the same renderer:

```bash
scripts/renderer-parity.sh <game-root> <route> baseline              # before the change
scripts/renderer-parity.sh <game-root> <route> current baseline      # after the change
```

`renderer-capture` writes `<output>/<route>/<label>/` (`--test-label`,
default `current`); `renderer-compare` compares `--test-baseline` (default
`baseline`) with `--test-label`. `shape-viewer-capture` and
`shape-viewer-compare` take the same options.

## Ground Rules For All Tasks
- Prefer incremental, reversible changes.
- Capture a baseline before a renderer change and compare after it.
- Preserve editor selection behavior.
- Keep existing asset formats and object model.
