# Shapes Task Roadmap

This folder contains ordered tasks for generalizing TSRE "complex shapes" (currently MSTS `SFile`) and adding new model formats (starting with glTF/GLB).

## Execution Order
- [x] `01-complex-shape-abstraction.md`
- [x] `02-gltf-glb-shape-loader.md`
- [ ] `03-complex-shape-metadata-sidecar.md`
- [ ] [04-sfile-complex-implementation.md](04-sfile-complex-implementation.md) — new MSTS implementation alongside legacy SFile/C/X; opt-in implementation, stock comparison and typed-storage/pre-load Compact optimization implemented; SFileLegacy is the default, with original SFile/C/X retained as an explicit fallback. Independent of Task 03.

- [x] [05-sfile-legacy-load-gl.md](05-sfile-legacy-load-gl.md) — SFileLegacy consolidates legacy parsing/rendering with separate CPU loading and GL initialization; stock regression checks and repeated SFileX/C comparisons complete.

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

Supporting audits, investigations and benchmark reports live in [reports/](reports/); numbered implementation tasks remain in this directory.

- [SFile legacy findings](reports/sfile-legacy-findings.md) — field exposure and caller audit for Task 04.
- [Parser performance investigation](reports/sfile-parser-performance.md) — UTF-16 stage measurements, local Open Rails timings and proposed typed storage optimization for Complete/Compact.
- [Optimization results](reports/sfile-optimization-results.md) — native storage, early Compact selection and packed Compact tables, with loading profiles, repeated comparisons and preservation/rendering checks.
- [UTF-16 optimization results](reports/sfile-utf16-optimization-results.md) — reusable reader improvements, direct numeric arrays, paired timings and byte-identical Complete exports.

- [Third-party SFileLegacy comparison](reports/sfile-legacy-third-party-comparison.md) — read-only Windows trainset collection, independent old/new runs and replacement assessment.
- [Legacy / Compact / Complete comparison](reports/sfile-three-mode-comparison.md) — three-mode compatibility and diagnostic loading timings across the read-only third-party collection.
