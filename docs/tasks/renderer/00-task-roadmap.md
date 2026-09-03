# Renderer Task Roadmap

This folder contains ordered tasks for migrating the TSRE renderer from legacy immediate drawing to gather-then-render.

## Execution Order
- [x] `01-runtime-pipeline-switch.md`
- [x] `02-renderer-core-generic-queue.md`
- [x] `03-selection-and-picking-parity.md`
- [x] `04-terrain-highres-gather.md`
- [x] `05-terrain-distant-water-sky.md`
- [x] `06-world-objects-shape-based.md`
- [x] `07-world-objects-procedural-and-helpers.md`
- [x] `08-overlays-tdb-activity-markers.md`
- [x] `09-hud-compass-pointer.md`
- [x] `10-shadows-gather-pass.md`
- [ ] `11-shader-pass-buckets-and-custom-shaders.md`
- [ ] `13-selection-renderer-and-id-redesign.md`
- [ ] `12-parity-automation-and-performance-gate.md`

## Ground Rules For All Tasks
- Keep a runtime fallback to legacy pipeline until Task 12 sign-off.
- Do not remove legacy code path early.
- Prefer incremental, reversible changes.
- Preserve editor selection behavior.
- Keep existing asset formats and object model.
