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

- [ ] [12-shape-and-rolling-stock-animation.md](12-shape-and-rolling-stock-animation.md) (merged into main 2026-10-24, the user's hardware review left) — keyframes posed as Open Rails poses them (one evaluator for both MSTS backends, the conjugate quaternion, rotation replacing the rest rotation), parts posed per shape state; rolling stock: wheels, bogies, running gear, pantographs, doors, cars on their bogie pivots; Shape Viewer controls.

- [ ] [13-msts-pbr-materials-and-editor.md](13-msts-pbr-materials-and-editor.md) (merged into main 2026-10-28; small items after it, listed under Open) — PBR materials for S files (base colour, ORM with a clearcoat mask, normal map through the glTF shading, reflective and transmissive glass; factors in a short `.sd` line), MSTS specular, a materials editor in the Shape Viewer (reviewed by the user: help beside numbers, 60:40), shape-part selection ids shared with the consist editor; the legacy shading matched to PBR's (the shade in the sky's colour, sun and sky mixed in linear light, the sun in the atmosphere's colour, the sky's light held at saturation 0.75); fixes for main: cutout mipmaps kept opaque, the environment cube in half floats.

- [ ] [10-gltf-native-lod.md](10-gltf-native-lod.md) — native glTF levels of detail, internal (MSFT_lod) and external (`_lod01` files), chosen by screen coverage, with tree impostors, the LOD bias and coverage-preserving mipmaps for trees; merged from `feature/gltf-lod`. Coverages proposed for tree-gen: [0.444, 0.167, 0.060]. Open: instance scale, instance profiles, mipmaps for other content. Recommended first vegetation experiment before Forest/STF impostors or terrain scattering.

- [ ] [14-shape-lights.md](14-shape-lights.md) — point and spot lights for S shapes in the `.sd` (`ESD_TSRELight`), drawn as glTF punctual lights; reviewed with Open Rails' glTF lights; steps 1-5 done (2026-11-05), merged into main d72b9bc7 with task 15 (2026-11-06): both MSTS backends, discs where local lights are off or absent (glTF lights too), glTF light names, the Shape Viewer's list, guide; an `.sd` light lights the scene as the same glTF light, pixel for pixel; the ENG binding done 2026-11-06 (ENG lights switch shape lights by name, `On`, per car; checked on a Dash 9); next: ENG lights placed on a matrix.

- [ ] [15-shape-emitters.md](15-shape-emitters.md) — smoke and steam from shapes in the `.sd` (`ESD_TSREEmitter`), drawn by task renderer 41's particles: scenery per placed object with its own particle pool, the Shape Viewer showing exhaust on QRhi; steps 1-5 done 2026-11-06, merged into main d72b9bc7 (2026-11-06) after a review: both MSTS backends, scenery per placed object (`Variation`, its own pool 1,000/3,000, 2,000 m, nearest first), the standalone Shape Viewer's smoke (shapes, an engine's at the test values), guide; the binding to ENG emitters a follow-up.

- [ ] [16-distance-level-extension.md](16-distance-level-extension.md) — what of an object is drawn on past its shape's last level (`ESD_TSREDistanceLevelExtension`: `None`, `MeshOnly`, `All`, `Default`; glTF `TSRE_distanceLevelExtension`) and a light's `ViewDistance` (glTF `TSRE_viewDistance`), for interior lamps that cost drawing time from afar; done 2026-11-06, merged into main 5a8b8fba (2026-11-06). The light grid's coarse cells around the camera: renderer task 45.

- [ ] [17-automatic-shadow-levels.md](17-automatic-shadow-levels.md) — a level of detail for shadow and reflection maps made at load where a shape has none (few parts, its outline kept), drawn through `ComplexShape::shadowLevel()`; planned (the user, 2026-11-09): the lamps' tiles over moving trains could draw cars in detail cheaper, but most rolling stock has one level.

- [x] MaSzyna `.e3d` models as another `ComplexShape` (built 2026-11-09 on `feature/maszyna`, not merged): drawn directly, not converted; their vehicles read as `Eng` from their own `.mmd` and `.fiz`, no ENG files made; `.t3d` left out (`docs/tasks/maszyna/00-task-roadmap.md`).

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
