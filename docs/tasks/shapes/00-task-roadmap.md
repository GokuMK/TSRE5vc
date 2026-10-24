# Shapes Task Roadmap

This folder contains ordered tasks for generalizing TSRE "complex shapes" (currently MSTS `SFileLegacy`/`SFileComplex`) and adding new model formats (starting with glTF/GLB).

## Execution Order
- [x] `01-complex-shape-abstraction.md`
- [x] `02-gltf-glb-shape-loader.md`
- [ ] `03-complex-shape-metadata-sidecar.md`
- [ ] [04-sfile-complex-implementation.md](04-sfile-complex-implementation.md) — new MSTS implementation alongside legacy SFile/C/X; opt-in implementation, stock comparison and typed-storage/pre-load Compact optimization implemented; SFileLegacy is the default; the original SFile/C/X classes have been removed. Independent of Task 03.

- [x] [05-sfile-legacy-load-gl.md](05-sfile-legacy-load-gl.md) — SFileLegacy consolidates legacy parsing/rendering with separate CPU loading and GL initialization; stock regression checks and repeated SFileX/C comparisons complete.
- [x] [06-gltf-pbr-materials.md](06-gltf-pbr-materials.md) — glTF metallic-roughness materials with image-based light from the environment map; simplifications and new-renderer requirements listed.
- [x] [07-blended-msts-parts.md](07-blended-msts-parts.md) — MSTS blended parts with opaque textures drawn as opaque (`core.rendering.blendedParts`); blended parts drawn in two as Open Rails draws them, their opaque texels with the alpha-tested pass (a window no longer hides a cab behind it; ambient occlusion on blended parts; the translucent texels blended down to alpha 4 of 255, faint glass no longer cut at 0.3); next: such roofs in the sky enclosure.
- [ ] [08-threaded-shape-loading.md](08-threaded-shape-loading.md) — MSTS and glTF shapes load on worker threads into detached copies the main thread adopts; the first view loads before it is shown; editor review of synchronous expectations.
- [ ] [09-shape-seasonal-textures.md](09-shape-seasonal-textures.md) — seasonal texture directory of MSTS shapes: clear and rain seasons fixed; the rule is still to compare with Open Rails (small fixes only, no seasonal rework).

- [x] [11-msts-distance-levels.md](11-msts-distance-levels.md) — MSTS distance levels chosen per object as Open Rails does (the default; `TSRE_SHAPE_LOD`); Prudnik measurements; rolling stock too (2026-10-23).

- [ ] [12-shape-and-rolling-stock-animation.md](12-shape-and-rolling-stock-animation.md) — keyframes posed as Open Rails poses them (one evaluator for both MSTS backends, the conjugate quaternion, rotation replacing the rest rotation), parts posed per shape state; rolling stock: wheels, bogies, running gear, pantographs, doors, cars on their bogie pivots; Shape Viewer controls.

- [ ] [10-gltf-native-lod.md](10-gltf-native-lod.md) — native glTF levels of detail, internal (MSFT_lod) and external (`_lod01` files), chosen by screen coverage, with tree impostors, the LOD bias and coverage-preserving mipmaps for trees; merged from `feature/gltf-lod`. Coverages proposed for tree-gen: [0.444, 0.167, 0.060]. Open: instance scale, instance profiles, mipmaps for other content. Recommended first vegetation experiment before Forest/STF impostors or terrain scattering.

- [ ] MaSzyna `.t3d` / `.e3d` models as another `ComplexShape` (planned, the user 2026-10-24): drawn directly with TSRE's engine and content paths in mind, not converted; MaSzyna locos and cars in TSRE through a script making their ENG files (`docs/tasks/driving/00-task-roadmap.md`, decisions of 2026-10-24).

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
