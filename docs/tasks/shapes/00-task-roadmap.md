# Shapes Task Roadmap

This folder contains ordered tasks for generalizing TSRE "complex shapes" (currently MSTS `SFile`) and adding new model formats (starting with glTF/GLB).

## Execution Order
- [x] `01-complex-shape-abstraction.md`
- [x] `02-gltf-glb-shape-loader.md`
- [ ] `03-complex-shape-metadata-sidecar.md`
- [ ] [04-sfile-complex-implementation.md](04-sfile-complex-implementation.md) — new MSTS implementation alongside legacy SFile/C/X; class boundary accepted, ParserX review and load/save requirements documented; implementation not started. Independent of Task 03.

## Ground Rules For All Tasks
- Keep MSTS `.s` + `.sd` support working throughout.
- Keep route/world object semantics stable (existing route files should continue to load/render).
- Preserve the "shared asset + per-instance state id" model (`shapeState` concept).
- Avoid blocking the renderer modernization work in `docs/tasks/renderer/`:
  - prefer changes that work in both legacy and gather pipelines, or at least do not regress legacy.
- Add new formats incrementally with clear feature gates and logging.

## Related Dependencies
- Texture pipeline work is a practical prerequisite for embedded textures and future compressed formats:
  - `docs/tasks/textures/01-texture-format-and-upload-refactor.md`

## Supporting Reviews

- [SFile legacy findings](sfile-legacy-findings.md) — field exposure and caller audit for Task 04.
