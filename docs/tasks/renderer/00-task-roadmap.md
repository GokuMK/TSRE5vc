# Renderer Task Roadmap

This folder contains ordered tasks for migrating the TSRE renderer from legacy immediate drawing to gather-then-render.

## Execution Order
1. `01-runtime-pipeline-switch.md`
2. `02-renderer-core-generic-queue.md`
3. `03-selection-and-picking-parity.md`
4. `04-terrain-highres-gather.md`
5. `05-terrain-distant-water-sky.md`
6. `06-world-objects-shape-based.md`
7. `07-world-objects-procedural-and-helpers.md`
8. `08-overlays-tdb-activity-markers.md`
9. `09-shadows-hud-compass-pointer.md`
10. `10-parity-automation-and-performance-gate.md`

## Ground Rules For All Tasks
- Keep a runtime fallback to legacy pipeline until Task 10 sign-off.
- Do not remove legacy code path early.
- Prefer incremental, reversible changes.
- Preserve editor selection behavior.
- Keep existing asset formats and object model.
