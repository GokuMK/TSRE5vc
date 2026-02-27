# Task 01 - Complex Shape Abstraction (Make `SFile` Pluggable)

## Objective
Enable multiple "complex shape" formats (MSTS `.s`, glTF/GLB, future formats) to be managed by `ShapeLib` and used by world objects **without hard-coding `SFile` everywhere**.

## Context
Today, "complex shape" == `SFile`:
- `ShapeLib` stores `std::unordered_map<int, ComplexShape*> shape` (`src/tsre/shape/ShapeLib.h:12`).
- `WorldObj` and many subclasses store `ComplexShape* shapePointer` and call `ComplexShape::newState/pushRenderItem/render` (`src/tsre/world/objects/WorldObj.cpp:590`).

To add glTF/GLB as a first-class model format, we need a stable abstraction boundary.

## Key Decision: `ComplexShape` vs `OglObj`
**Do not try to make "everything inherit from `OglObj`".**
- `OglObj` is a single-primitive draw helper (one VAO/VBO, tiny material model).
- `SFile` is a shared asset with LOD, per-instance state, subobject toggles, and optional `.sd` metadata.

Recommended direction:
- Keep `OglObj` as a *render primitive/procedural* utility.
- Introduce a new *asset/model* abstraction for file-backed models (MSTS + glTF).

## Scope
- Introduce a new "complex shape" base type (`ComplexShape`).
- Make `SFile` implement/inherit from it (`src/tsre/shape/SFile.h`).
- Update `ShapeLib`, `WorldObj`, and other call sites to use the new base type instead of `SFile*`.
- Ensure all existing MSTS shape usage continues to function.

## Suggested Touch Points
- `src/tsre/shape/ShapeLib.h/.cpp`
- `src/tsre/shape/SFile.h/.cpp`
- `src/tsre/world/objects/WorldObj.h/.cpp` and shape-based derived objects
- `src/shapeViewer/*` (renders via `ComplexShape`, MSTS inspection uses `SFile`)
- `src/tsre/world/Skydome.*`, `src/tsre/trains/Eng.cpp` (loads shapes)

## Requirements
- Define a minimal, format-agnostic contract required by route/world objects:
  - shared asset identity (`pathid` equivalent)
  - texture root path (`texPath` equivalent) for preview/tools
  - "shape preview path" string helper (`path|texPath`) for Route Editor shape preview
  - lazy load / reload hook
  - per-instance state allocation (`newState()` returning an id/handle)
  - per-instance simulation tick (`updateSim(deltaTime, stateId)`)
  - selection rendering support (`render(selectionColor, stateId)` and/or gather equivalent)
  - gather submission (`pushRenderItem(selectionColor, stateId)`)
  - cache invalidation (`invalidateRenderState(...)`) for renderer changes
  - bounds/size query (so world LOD culling and box rendering can stay generic)
  - optional but standardized complex features (no-op defaults in the base type):
    - part enable/disable (`enablePart/disablePart`)
    - subobject toggles by name (`enableSubObjByName*`)
    - distance-level selection (`setCurrentDistanceLevel`)
    - hierarchy/texture/content inspection for tools (`fillShape*Info`)
    - snapable helpers (`isSnapable/addSnapablePoints/getFloorBorderLinePoints`)
- `ShapeLib::addShape(...)` becomes a small factory (extension dispatch similar to how `TexLib` picks loaders):
  - `.s` -> MSTS `SFile`
  - `.gltf`/`.glb` -> glTF implementation (Task 02)
- Keep MSTS-specific features accessible without leaking into generic world code:
  - preferred: keep the *file-format-specific* details behind `SFile` (or future format class) methods/data that are only used by dedicated tools/UI panels.
  - avoid `dynamic_cast` in engine and common tools by putting shared complex-shape capabilities onto `ComplexShape` (with safe default implementations where a format does not support a feature yet).
- Update `ShapeLib` storage type from `SFile*` to the new base type.
- Update `Game::currentShapeLib` users to work with the base type.
- Keep `ShapeViewer` functional:
  - It must store the new base type (not `SFile*`).
  - Viewer panels should use `ComplexShape` methods where possible; only format-specific panels should downcast (e.g. `dynamic_cast<SFile*>`) when they need raw loader details.
  - Non-MSTS shapes can initially show a reduced UI if a capability is not implemented yet (base methods are safe no-ops by default).
- No behavioral change for:
  - loading existing `.s` + `.sd`
  - route/world object placement and rendering
  - selection rendering (selectionColor path)

### Public Surface Audit (SFile -> ComplexShape)
Before introducing `ComplexShape`, capture which parts of `SFile` are actually used outside `SFile` and classify them:
- **Must be in `ComplexShape` (engine-wide use):** `load/reload`, `newState`, `updateSim`, `render`, `pushRenderItem`, `invalidateRenderState`, bounds/size query.
- **Also in `ComplexShape` to avoid casts (tools + engine convenience):** `getTexPath`, `getShapePreviewPath`, `getEsdDetailLevel`, `fillShapeTextureInfo`, `fillShapeHierarchyInfo`, `fillContentHierarchyInfo`, `enablePart/disablePart`, `enableSubObjByName*`, distance-level selection, snapable helpers.
- **Should become private/protected implementation detail (format-specific):** parsing structs, token fields, raw arrays, and MSTS-only state that other code should not touch.

Initial "used outside `SFile`" checklist to validate by grep (non-exhaustive):
- Fields: `pathid`, `texPath`, `loaded`, `size`, `bound`, `esdDetailLevel`
- Methods: `newState`, `setAnimated`, `setCurrentDistanceLevel`, `updateSim`, `render`, `pushRenderItem`, `reload`, `getBoxPoints`, `getFloorBorderLinePoints`, `isSnapable`, `addSnapablePoints`, `fillShapeTextureInfo`, `fillShapeHierarchyInfo`, `fillContentHierarchyInfo`

Goal: reduce direct field access over time (prefer getters/methods on the base type) so new formats do not need to emulate MSTS internals.

## Acceptance Criteria
- Project builds.
- Existing routes render MSTS shapes as before (legacy path and/or gather path depending on current renderer mode).
- `ShapeLib::addShape(...)` still de-duplicates MSTS shapes by normalized pathid.
  - note: de-duplication currently ignores `texPath`; the same `pathid` cannot be cached under multiple texture roots without changing the keying strategy.
- World objects still maintain per-instance state (`shapeState`) and pass it through correctly.
- Shape viewer uses the new base type and still opens MSTS shapes and shows previous information.

## Out Of Scope
- Adding glTF/GLB loader (next task).
- Improving `ShapeLib`/`TexLib` refcounting or lifecycle management.
- Renderer feature parity work (tracked under `docs/tasks/renderer/`).
