# Renderer Task Roadmap

This folder contains ordered tasks for migrating the TSRE renderer from legacy immediate drawing to gather-then-render.

## Execution Order
- [x] `01-runtime-pipeline-switch.md`
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
- [ ] `14-windows-hardware-validation.md` (hardware parity, performance and interactive checks on Windows)
- [ ] `12-parity-automation-and-performance-gate.md` (measurement harness implemented; gate pending on task 14)

## Current Default

`core.rendering.pipeline` defaults to `gather` for new profiles; `legacy`
remains selectable until it is removed. Existing profiles keep their stored
value.

## Ground Rules For All Tasks
- Keep a runtime fallback to legacy pipeline until Task 12 sign-off.
- Do not remove legacy code path early.
- Prefer incremental, reversible changes.
- Preserve editor selection behavior.
- Keep existing asset formats and object model.
