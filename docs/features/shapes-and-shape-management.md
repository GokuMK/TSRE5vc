# TSRE Shapes and Shape Management (Current State)

## Scope
- Project: TSRE5vc
- Goal: document how shapes are represented, loaded, cached, and rendered today.
- Non-goal: implement new formats (see `docs/tasks/shapes/`).

## Terminology (used in this doc)
- **Complex shape (asset/model):** file-backed, multi-part, can have LOD and per-instance state (today: `SFile`, MSTS `.s` + optional `.sd`).
- **Simple shape (render primitive):** a single VAO/VBO draw call with a very small material model (today: `OglObj`), typically procedural/editor helpers.

## Evidence Base (Code Anchors)
- Shape cache and lifetime: `src/tsre/shape/ShapeLib.h:12`, `src/tsre/shape/ShapeLib.cpp:54`
- MSTS shape implementation: `src/tsre/shape/SFile.h:29`, `src/tsre/shape/SFile.cpp:52`
- MSTS `.sd` metadata handling: `src/tsre/shape/SFile.cpp:504`
- Per-instance state for shared shapes: `src/tsre/shape/SFile.cpp:843`
- Gather queue submission for shapes: `src/tsre/shape/SFile.cpp:970`
- World object -> shape submission path: `src/tsre/world/objects/WorldObj.cpp:590`
- Typical world object load path: `src/tsre/world/objects/StaticObj.cpp:81`, `src/tsre/world/objects/TrackObj.cpp:85`
- Simple render primitive contract: `src/tsre/ogl/OglObj.h:18`, `src/tsre/ogl/OglObj.cpp:135`
- Procedural shapes using OBJ templates: `src/tsre/procedural/ProceduralShape.h:39`, `src/tsre/shape/ObjFile.cpp:19`
- Shape viewer depends on `ShapeLib`/`SFile`: `src/shapeViewer/ShapeViewerGLWidget.cpp:553`
- Procedural/in-memory textures via `TexLib`: `src/tsre/ogl/TextObj.cpp:64`, `src/tsre/texture/TexLib.cpp:90`, `src/tsre/texture/PaintTexLib.cpp:20`

---

## 1. Complex Shapes: `SFile` (MSTS `.s`)

### 1.1 What `SFile` Represents
`SFile` is both:
1) a **loader/parser** for MSTS shape data (`.s`), and
2) a **renderable asset** that owns GPU resources (VAO/VBO per subobject) and can submit draw packets.

Key capabilities embedded in `SFile`:
- **Multiple distance levels (LOD):** `distancelevel[]` plus `state[stateId].distanceLevel`.
- **Subobjects and parts:** each distance level has `subobiekty[]` with `czesci[]` parts, each part has `offset`, `iloscv`, `prim_state_idx`.
- **Matrix/node hierarchy and toggles:** matrix list `macierz[]` and per-instance `enabledSubObjs` mask (see `enableSubObjByName` queue usage).
- **Animations:** parsed into `animations[]` and advanced by `updateSim(deltaTime, stateId)`.
- **Bounds/size:** `bound[6]` and `size` via `getSize()`.

### 1.2 File Variants Parsed by `SFile`
`SFile::load()` supports multiple MSTS encodings:
- **Binary** (token stream) path: detected by reading an int at offset 32, then delegated to `SFileC::*` parsing.
- **Text/XML-like** path: delegated to `SFileX::*` parsing for the same conceptual sections.

This matters because "format support" in TSRE is currently intertwined with `SFile` internals.

### 1.3 `.sd` Metadata (`loadSd`)
`SFile::loadSd()` reads `pathid + "d"` (MSTS `.sd`) and populates:
- `esdDetailLevel` (used by some objects as default detail level),
- `esdAlternativeTexture` and seasonal `texPath` adjustments,
- `esdBoundingBox` (complex bounding boxes),
- `snapable` flag.

This is also where per-season texture path rewriting happens today.

### 1.4 Shared Asset + Per-Instance State (`newState`)
`SFile` objects are **shared** via `ShapeLib`, but each world object instance can have different runtime state.

This is handled by an internal vector of `State` entries keyed by `stateId`:
- `newState()` allocates a state slot and returns an id.
- world instances store that id as `WorldObj::shapeState` and pass it back on render/update calls.

This is a critical design point to preserve when adding new model formats: shared GPU asset + per-instance state.

### 1.5 Rendering Paths
`SFile` supports both:
- **legacy immediate draw** (`render(selectionColor, stateId)`), and
- **gather-then-render submission** (`pushRenderItem(selectionColor, stateId)`).

The gather path builds `RenderItem` packets, and caches them per `stateId` when not animated:
- caching is invalidated when texture addresses change or when `invalidateRenderState()` is called.
- animated shapes avoid caching shared packets to prevent dangling matrix pointers.

---

## 2. Shape Cache / Manager: `ShapeLib`

`ShapeLib` is the central cache of complex shapes used by both the editor and runtime:
- primary store: `std::unordered_map<int, SFile*> shape` (`src/tsre/shape/ShapeLib.h:12`)
- id allocation: monotonic `jestshape` counter
- de-duplication: `addShape(path, texPath)` normalizes `pathid` and linearly scans existing shapes to match `SFile::pathid` (`src/tsre/shape/ShapeLib.cpp:54`)

Notable characteristics:
- **The cache is keyed by pathid string**, but stored by int id.
- **Refcount exists** (`SFile::ref`) and is incremented on re-use in `addShape`, but `ShapeLib::delRef/addRef` are currently stubs (shape lifetime is effectively "process lifetime" today).
- `invalidateRendererCaches()` is a bulk hook to tell all shapes to rebuild cached packets/matrices.

---

## 3. World Objects: How Route Instances Bind to Shapes

### 3.1 Data Stored on `WorldObj`
Each placed world object typically stores:
- `int shape` (ShapeLib id)
- `SFile* shapePointer` (direct pointer; may be set in `load`)
- `unsigned int shapeState` (per-instance `SFile` state id)

See `src/tsre/world/objects/WorldObj.h:82`.

### 3.2 Common Render Submission Path
The base implementation `WorldObj::pushRenderItems(...)` assumes the "complex shape" is an `SFile`:
1) resolve `shapePointer` or lookup `Game::currentShapeLib->shape[shape]`
2) compute size/LOD cull
3) multiply instance transform into `Game::currentRenderer->mvMatrix`
4) call `shapeToRender->pushRenderItem(selectionColor, shapeState)`

See `src/tsre/world/objects/WorldObj.cpp:590`.

### 3.3 Typical Load Flow (example: `StaticObj`)
`StaticObj::load` shows the most common binding pattern:
- `shape = Game::currentShapeLib->addShape(resPath + "/" + fileName)`
- `shapePointer = Game::currentShapeLib->shape[shape]`
- `shapeState = shapePointer->newState()`
- `shapePointer->setAnimated(shapeState, isAnimated())`

See `src/tsre/world/objects/StaticObj.cpp:81`.

This exact pattern is what a new model format must fit into if we want "use glTF in route files" without changing route object semantics.

---

## 4. Simple Shapes: `OglObj` + Procedural Generation

`OglObj` is a lightweight render primitive:
- owns one VAO + VBO
- supports a small material set: `NONE | TEXTURE | COLOR`
- can submit a `RenderItem` via `pushRenderItem(selectionColor, lod)` (`src/tsre/ogl/OglObj.cpp:135`)
- is widely used for procedural/editor visuals (lines, quads, markers, selection boxes, etc.)

Procedural track shapes and helpers are often built from OBJ templates:
- `ObjFile` parses a very small subset of Wavefront `.obj` (v/vt/vn + triangle faces), producing interleaved VNTA-like arrays (`src/tsre/shape/ObjFile.cpp:19`)
- `ProceduralShape` loads/caches these templates and emits `QVector<OglObj*>` batches (`src/tsre/procedural/ProceduralShape.h:39`)

This is conceptually a *different abstraction layer* than `SFile`:
- `OglObj` is "a draw primitive"
- `SFile` is "a shared model asset with per-instance state"

---

## 5. Shape Viewer Dependency

`ShapeViewerGLWidget::showShape(...)` uses `ShapeLib::addShape(...)` and stores the result as an `SFile*` (`src/shapeViewer/ShapeViewerGLWidget.cpp:553`).

Any attempt to generalize `ShapeLib` away from `SFile*` will need a deliberate compatibility plan for:
- the shape viewer UI (hierarchy/texture inspection),
- any other tools that assume MSTS-only shapes.

---

## 6. Implications for Adding glTF/GLB (Design Constraints)

This repo currently has no "format-agnostic model asset" type. New model formats (glTF/GLB, OBJ-as-asset, etc.) will require at least one of:
- a new base class/interface for complex shapes (preferred), or
- a wrapper/composition object that makes different loaders look like the existing `SFile` contract.

Major constraints to plan for:
- **Shared asset + per-instance state must remain** (world objects already rely on `shapeState`).
- **Renderer packet contract today is VAO/VBO + glDrawArrays-style offsets** (`RenderItem` has no index buffer). This is already how MSTS shapes are handled: parsing expands indexed geometry into non-indexed vertex arrays (see `SFileC`/`SFileX`). glTF/GLB can follow the same approach initially.
- **Texture system is pathid + hashid based** (`Texture::hashid` is used for de-duplication in `TexLib`). While most textures are file-backed, TSRE already supports procedural/in-memory textures (e.g. `TextObj` uses `.:paintTex` handled by `PaintTexLib`). This means GLB embedded images can be supported by registering them in `TexLib` under a content hash (and optionally decoding in a worker, similar to other texture loaders).
- **Material/alpha model is simple and shader-driven**. The current shader uses the `alpha` vertex attribute with sign semantics:
  - positive value (e.g. `1.0`) forces opaque output alpha
  - negative value encodes an alpha cutoff (`discard` when `texAlpha < -vAlpha`)
  This maps naturally to glTF `OPAQUE` and `MASK`. glTF `BLEND` needs correct draw ordering (renderer modernization work), but can be represented without cutout by using a non-negative alpha attribute (e.g. `0.0`) so discard never triggers.
- **Full PBR is out of scope** unless the renderer pipeline is extended (metal/rough, normal maps, IBL, etc.).
