# Task 01 - Complex Shape Abstraction (Make `SFile` Pluggable)

## Objective
Enable multiple "complex shape" formats (MSTS `.s`, glTF/GLB, future formats) to be managed by `ShapeLib` and used by world objects **without hard-coding `SFile` everywhere**.

## Context
Today, "complex shape" == `SFile`:
- `ShapeLib` stores `std::unordered_map<int, SFile*> shape` (`src/tsre/shape/ShapeLib.h:12`).
- `WorldObj` and many subclasses store `SFile* shapePointer` and call `SFile::newState/pushRenderItem/render` (`src/tsre/world/objects/WorldObj.cpp:590`).

To add glTF/GLB as a first-class model format, we need a stable abstraction boundary.

## Key Decision: `ComplexShape` vs `OglObj`
**Do not try to make "everything inherit from `OglObj`".**
- `OglObj` is a single-primitive draw helper (one VAO/VBO, tiny material model).
- `SFile` is a shared asset with LOD, per-instance state, subobject toggles, and optional `.sd` metadata.

Recommended direction:
- Keep `OglObj` as a *render primitive/procedural* utility.
- Introduce a new *asset/model* abstraction for file-backed models (MSTS + glTF).

## Scope
- Introduce a new "complex shape" base type (name TBD, e.g. `ComplexShape`, `ModelAsset`, `IComplexShape`).
- Make `SFile` implement/inherit from it.
- Update `ShapeLib`, `WorldObj`, and other call sites to use the new base type instead of `SFile*`.
- Ensure all existing MSTS shape usage continues to function.

## Suggested Touch Points
- `src/tsre/shape/ShapeLib.h/.cpp`
- `src/tsre/shape/SFile.h/.cpp`
- `src/tsre/world/objects/WorldObj.h/.cpp` and shape-based derived objects
- `src/shapeViewer/*` (expects `SFile*` today)
- `src/tsre/world/Skydome.*`, `src/tsre/trains/Eng.cpp` (loads shapes)

## Requirements
- Define a minimal, format-agnostic contract required by route/world objects:
  - shared asset identity (`pathid` equivalent)
  - lazy load / reload hook
  - per-instance state allocation (`newState()` returning an id/handle)
  - per-instance simulation tick (`updateSim(deltaTime, stateId)`)
  - selection rendering support (`render(selectionColor, stateId)` and/or gather equivalent)
  - gather submission (`pushRenderItem(selectionColor, stateId)`)
  - cache invalidation (`invalidateRenderState(...)`) for renderer changes
  - bounds/size query (so world LOD culling and box rendering can stay generic)
- `ShapeLib::addShape(...)` becomes a small factory (extension dispatch similar to how `TexLib` picks loaders):
  - `.s` -> MSTS `SFile`
  - `.gltf`/`.glb` -> glTF implementation (Task 02)
- Keep MSTS-specific features accessible without leaking into generic world code:
  - options:
    - expose optional query interfaces (e.g. `IMstsShapeExtras`)
    - or use safe `dynamic_cast` in the few tools that need MSTS-only data (shape viewer, ESD detail level)
- Update `ShapeLib` storage type from `SFile*` to the new base type.
- Update `Game::currentShapeLib` users to work with the base type.
- Keep `ShapeViewer` functional:
  - It must store the new base type (not `SFile*`).
  - It may keep MSTS-only UI panels via `dynamic_cast<SFile*>` for hierarchy/texture inspection.
  - Non-MSTS shapes can initially show a reduced UI ("format does not expose MSTS hierarchy").
- No behavioral change for:
  - loading existing `.s` + `.sd`
  - route/world object placement and rendering
  - selection rendering (selectionColor path)

### Public Surface Audit (SFile -> ComplexShape)
Before introducing `ComplexShape`, capture which parts of `SFile` are actually used outside `SFile` and classify them:
- **Must be in `ComplexShape` (engine-wide use):** `load/reload`, `newState`, `updateSim`, `render`, `pushRenderItem`, `invalidateRenderState`, bounds/size query.
- **Maybe optional capability interfaces:** MSTS-specific `.sd` features (ESD detail level, snapable points), MSTS hierarchy/texture inspection for the viewer.
- **Should become private/protected implementation detail:** parsing structs, token fields, raw arrays, and MSTS-only state that other code should not touch.

Initial "used outside `SFile`" checklist to validate by grep (non-exhaustive):
- Fields: `pathid`, `texPath`, `loaded`, `size`, `bound`, `esdDetailLevel`
- Methods: `newState`, `setAnimated`, `setCurrentDistanceLevel`, `updateSim`, `render`, `pushRenderItem`, `reload`, `getBoxPoints`, `getFloorBorderLinePoints`, `isSnapable`, `addSnapablePoints`, `fillShapeTextureInfo`, `fillShapeHierarchyInfo`, `fillContentHierarchyInfo`

Goal: reduce direct field access over time (prefer getters on the base type) so new formats do not need to emulate MSTS internals.

## Acceptance Criteria
- Project builds.
- Existing routes render MSTS shapes as before (legacy path and/or gather path depending on current renderer mode).
- `ShapeLib::addShape(...)` still de-duplicates MSTS shapes by normalized pathid.
- World objects still maintain per-instance state (`shapeState`) and pass it through correctly.
- Shape viewer uses the new base type and still opens MSTS shapes and shows previous information.

## Out Of Scope
- Adding glTF/GLB loader (next task).
- Improving `ShapeLib`/`TexLib` refcounting or lifecycle management.
- Renderer feature parity work (tracked under `docs/tasks/renderer/`).
