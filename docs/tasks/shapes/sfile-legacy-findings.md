# SFile Legacy Findings

Historical source audit supporting [Task 04 - SFileComplex](04-sfile-complex-implementation.md). Legacy classes remain unchanged; implementation requirements and the ParserX review live in Task 04.

## Audit scope and result

Reviewed on 2026-09-09 at commit `7381cd3`. Inspected `SFile.h/.cpp`, both C/X readers, `ComplexShape`, `ShapeLib`, editor/viewer consumers, and shape parser tests. Repository searches covered concrete `SFile` references, member accesses, nested types, and commented-out references; unrelated classes with identically named fields were excluded. This is a source audit, not runtime validation or a guarantee about consumers outside this repository.

**No active production consumer outside the legacy shape implementation and its C/X readers was found directly accessing an `SFile` data field.** Application consumers already use `ComplexShape` methods. Direct external access remains in the C/X readers and `TokenWorldTestSuite`.

Therefore, the new class does not need public mutable data fields to satisfy the observed application callers. Its implementation data can be private, provided the existing behavioral and inspection methods remain supported. Making the old class private today would break the readers and tests; that is neither necessary nor part of this task.

## Complete top-level public field inventory

“Internal” below means accessed inside `SFile` methods, including inline getters. “C/X” means both helper classes access the field directly. Tests are listed separately from application consumers.

| Public field(s) | Observed access | Implication for `SFileComplex` |
| --- | --- | --- |
| `pathid` | Internal; applications obtain identity through `getPathId()` and preview helpers | Private identity; preserve queries |
| `texPath` | Internal; exposed through `getTexPath()` and preview helpers | Private texture root; preserve queries |
| `nazwa` | Internal constructor and content inspection output | Private display name; retain inspection output |
| `sdName` | Internal `.sd` parser assignment | Private metadata if needed; no external accessor justified |
| `sciezka` | Declaration only; no active use found | Do not carry over without a demonstrated need |
| `isinit` | Internal load/render/inspection guards | Private lifecycle state |
| `loaded` | Internal; tests read it directly; applications call `isLoaded()` | Private lifecycle state; preserve loaded query |
| `loadedSd` | Internal `.sd` loading | Private metadata load state |
| `texloaded`, `ref` | Declaration only; no active use found | Do not carry over without a demonstrated need |
| `esdDetailLevel` | Internal `.sd` parser and getter; world objects call `getEsdDetailLevel()` | Private metadata; preserve detail-level query |
| `esdAlternativeTexture` | Internal `.sd` parsing and seasonal texture selection | Private metadata; preserve texture behavior |
| `esdBoundingBox` | Internal metadata parsing and bounds helpers | Private bounds data; preserve box/floor/snap behavior |
| `animations`, `animated` | Internal loading, simulation and rendering; tests use the nested animation type, not these top-level fields | Private animation asset/runtime data; retain state methods |
| `tpoints` | C/X allocate, populate, read and free points, normals and UV arrays; internal bounds computation | Private parsing/geometry data with explicit ownership |
| `iloscm`, `macierz` | C/X populate matrix count/data; internal transforms, subobject control and inspection | Private transforms and hierarchy data |
| `ilosci`, `image` | C/X populate image count/data; internal texture loading, caching and inspection | Private image references and texture state |
| `ilosct`, `texture` | C/X populate texture count/data; internal rendering and inspection use texture mappings | Private material/texture mappings |
| `iloscv`, `vtxstate` | C/X populate vertex-state count/data; internal rendering and inspection use vertex states | Private vertex-state data; top-level `iloscv` is distinct from part vertex counts |
| `iloscps`, `primstate` | C/X populate primitive-state count/data and use it when building meshes; internal rendering and inspection | Private primitive/material data |
| `iloscd`, `distancelevel` | C/X populate LODs, hierarchy, parts and GPU buffers; internal runtime/inspection; tests inspect LODs and mesh buffers | Private LOD/mesh ownership; preserve LOD selection and inspection methods |
| `currentDistanceLevel` | Declaration only; active LOD selection uses private `State::distanceLevel` | Do not duplicate the unused field; preserve per-state LOD behavior |
| `ishaders`, `shader` | C/X populate shader count/data and read it for mesh alpha; tests read count/name | Private shader/material data |
| `size`, `bound` | Internal calculation and getters; applications call `getSize()` / `getBound()` | Private bounds; preserve read-only queries |

### C/X coupling and nested structures

Both readers directly access the same 15 top-level fields: `tpoints`, `iloscm`, `macierz`, `ilosci`, `image`, `ilosct`, `texture`, `iloscv`, `vtxstate`, `iloscps`, `primstate`, `iloscd`, `distancelevel`, `ishaders`, `shader` (seven count/data pairs plus `tpoints`). They also traverse nested members. These are implementation dependencies, not requirements for public application access.

The public nested types are `SObjHeader`, `fshader`, `czes`, `sub`, `dist`, `matrt`, `primst`, `text`, `vtxs`, `imgs`, `fpoint`, `punlist`, `EsdBoundingBox`, `AnimFrameId`, `AnimNode` (with `TcbKey`, `SlerpRot`, `LinearKey`), and `Animation`.

- C/X construct and fill the geometry/material types, including matrix parameters, shader alpha, image names, primitive arguments, LOD hierarchies, `header.geometryNodeMap`, part counts/indices/offsets, and VAO/VBO objects. See [binary reader](../../../src/tsre/shape/SFileC.cpp) and [text reader](../../../src/tsre/shape/SFileX.cpp).
- Their LOD readers also upload GPU buffers and release temporary geometry. Parsing and GPU work are coupled today; private CPU parsing data and a separate upload stage are a useful proposed boundary for the new implementation.
- `EsdBoundingBox` and animation runtime/index structures have no application-side direct consumers. `Animation::loadC/loadX` are `SFile` nested-type methods, not dependencies from unrelated application classes.
- New equivalents of these structures can be private or confined to implementation-only parser data. Do not expose mutable arrays or GPU buffers merely to let parsers or tests access them. Leave the old C/X classes unchanged.

### Direct test dependencies

[TokenWorldTestSuite.cpp](../../../src/tsre/tests/TokenWorldTestSuite.cpp):

- Around line 213: creates `SFile`, calls `SFileC::odczytajshaders`, and reads `ishaders` and `shader[0].name`.
- Around lines 226 and 241: constructs `SFile::Animation`, invokes `loadC`, and reads `frames`, `node`, linear key frame/position, TCB quaternion/parameters, and slerp quaternion values.
- Around line 255: passes a shape to the point reader to check malformed-input rejection; it does not inspect the resulting point fields.
- Around line 298: calls `load()`, reads `loaded`, `iloscd`, and `distancelevel`; traverses `subobiekty`, `iloscs`, `iloscc`, `czesci`, and part `iloscv`; binds, reads and releases the subobject VBO to verify actual uploaded vertex bytes.

These tests must continue to exercise the legacy implementation during coexistence. New tests should verify the new parser's results and observable rendering behavior through a deliberate internal test boundary, without turning runtime storage into a public API.

## Existing application boundary to retain

[ComplexShape.h](../../../src/tsre/shape/ComplexShape.h) already supplies identity, loaded status, bounds, loading/reloading, per-instance state, animation/subobject/part control, LOD selection, rendering, gather submission, cache invalidation, inspection and snapping methods.

Concrete evidence:

- [ShapeLib.cpp](../../../src/tsre/shape/ShapeLib.cpp): deduplicates through `getPathId()`, stores `ComplexShape*`, and constructs `SFile` for MSTS shapes. No raw field access is needed for registration.
- [WorldObj.cpp](../../../src/tsre/world/objects/WorldObj.cpp): reads `isLoaded()` / `getSize()` and controls animation through methods.
- [StaticObj.cpp](../../../src/tsre/world/objects/StaticObj.cpp) and [TrackObj.cpp](../../../src/tsre/world/objects/TrackObj.cpp): query preview paths and detail level through methods.
- [ShapeViewerGLWidget.cpp](../../../src/shapeViewer/ShapeViewerGLWidget.cpp): stores `ComplexShape*`, reads bounds via `getBound()`, and fills hierarchy/texture information through methods.
- [ShapeViewerWindow.cpp](../../../src/shapeViewer/ShapeViewerWindow.cpp): selects LOD through `setCurrentDistanceLevel()`.
- `SFile::fillShapeHierarchyInfo()` copies hierarchy indices, matrix names and part summaries into `ShapeHierarchyInfo`. Texture inspection likewise creates `ShapeTextureInfo` records. These outputs already avoid exposing loader arrays directly.

Several files still include `SFile.h`, but includes alone are not field dependencies. `RouteEditorGLWidget.h` retains an `SFile* sFile` declaration; its construction/load examples in the implementation are commented out and no active use of that member was found. No active production downcast to `SFile` was found. Older task 01 wording about MSTS inspection using raw `SFile` details is not evidence of a current caller.

## Design consequences and behaviors to settle later

- Accepted class boundary (2026-09-10): `SFileComplex` implements `ComplexShape` directly, with private storage. Deriving it from `SFile` would inherit the public data surface this task aims to avoid.
- Keep public operations and inspection outputs driven by existing caller needs. No getter for every legacy field is warranted.
- The legacy public non-const `void getSize()` computes bounds; it is distinct from the const `float getSize()` query. Bounds computation belongs inside the new implementation.
- `getBound()` returns a const pointer. This prevents ordinary mutation through the query, but a new implementation must document its lifetime across reloads.
- Legacy `enablePart/disablePart` select a LOD using `stateId` but change the shared part's `enabled` flag. Per-instance isolation must be tested and any intentional behavior change documented; the state-id signature alone does not prove isolation.
- `sciezka`, `texloaded`, `ref`, and `currentDistanceLevel` have no active uses found. This does not authorize removing them from the legacy class.
