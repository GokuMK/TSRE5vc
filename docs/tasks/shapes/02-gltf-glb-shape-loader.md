# Task 02 - glTF/GLB Complex Shape Loader

## Objective
Add glTF 2.0 (`.gltf`) and GLB (`.glb`) as supported "complex shape" assets, loadable through `ShapeLib` and usable by world objects (e.g., as `Static` shapes in routes).

## Dependencies
- Task 01 must be complete (format-agnostic complex shape abstraction).
- Texture pipeline refactor is recommended before implementing embedded textures and future compressed formats:
  - `docs/tasks/textures/01-texture-format-and-upload-refactor.md`
- TSRE metadata for editor-only features (snapables, overrides, etc.) is tracked separately:
  - `docs/tasks/shapes/03-complex-shape-metadata-sidecar.md`
- Renderer pipeline work may affect delivery strategy:
  - If legacy rendering is still the default, glTF should implement both `render(...)` and `pushRenderItem(...)`.
  - If gather pipeline is the target path, ensure glTF submits valid `RenderItem` packets compatible with current shaders.

## Constraints
- No external tools are allowed to install/use for this task (no requirement for third-party command-line converters, importers, texture packers, etc.). Implement using TSRE code + Qt only.

## Scope
- Implement a new complex-shape type for glTF/GLB (e.g. `GltfShape`) that conforms to the Task 01 abstraction.
- Extend `ShapeLib::addShape(...)` to instantiate the correct loader based on file extension:
  - `.s` => MSTS (`SFile`)
  - `.gltf`/`.glb` => new glTF loader
- Ensure selection rendering works (flat selection color when `selectionColor != 0`).
- Shape Viewer support (for fast iteration/testing):
  - allow browsing/selecting `.gltf`/`.glb` in the Shape Viewer file list
  - treat `.gltf`/`.glb` as "shape" items in the viewer (similar to `.s`)
  - viewer panels may be partially empty in v1 (hierarchy/texture info are optional capabilities), but 3D rendering + bounds-based camera framing must work

## Suggested Touch Points
- `src/tsre/shape/ShapeLib.cpp` (factory/extension dispatch)
- new files under `src/tsre/shape/` for glTF (loader + runtime asset)
- `src/tsre/renderer/RenderItem.h` and renderer pipeline docs (only if packet contract must be extended)
- `src/tsre/texture/TexLib.*` (embedded images, content-hash de-dup, optional background decode)
- Shape Viewer:
  - `src/shapeViewer/ShapeViewerWindow.cpp` (detect `.gltf/.glb` as shapes)
  - `src/shapeViewer/ShapeViewerNavigatorWidget.cpp` (include `.gltf/.glb` in directory filtering)

## Format/Feature Support (Initial Target)
Aim for a "useful minimum" first:
- Geometry:
  - `POSITION` required
  - `NORMAL` optional (generate if missing)
  - `TEXCOORD_0` optional (default 0,0)
  - indexed primitives: expand indices into non-indexed vertex arrays (this matches current MSTS shape handling and keeps the renderer on `glDrawArrays`)
- Materials:
  - use `pbrMetallicRoughness.baseColorTexture` as "diffuse"
  - ignore metallic/roughness initially (renderer is not PBR)
  - alpha modes:
    - `OPAQUE` supported (force output alpha = 1)
    - `MASK` supported (map `alphaCutoff` into TSRE's existing "negative alpha attribute = cutoff" convention)
    - `BLEND` supported with known limitations:
      - represent "no cutoff" by using a non-negative alpha attribute (e.g. `0.0`) so discard never triggers
      - visual correctness depends on render ordering; proper sorting/pass-bucketing is part of renderer modernization
- Textures:
  - external image files referenced by URI (PNG/JPG/etc) via `TexLib` (v1)
  - embedded GLB images supported by registering a `Texture` in `TexLib` under a content hash:
    - compute a stable hash of the encoded image bytes (recommended: SHA-256 via Qt `QCryptographicHash`) + length
    - store identity as `glbimg:sha256:<hex>:len:<n>` and add it to `Texture::hashid` so identical embedded textures across GLBs de-duplicate
    - decoding can be synchronous for v1, or moved to a worker similar to existing loaders
  - explicitly log unsupported formats in v1 (KTX2/BasisU, DDS via extensions, etc.)
- Node transforms:
  - support static node transforms and parent-child composition
  - bake node transform into per-primitive `msMatrix` (similar role to MSTS internal matrices)
- Animation/skin/morph:
  - out of scope for v1 (log "unsupported" cleanly)

## Requirements
- Must compute `size` and `bound` equivalents for LOD culling and selection boxes.
- Must support `reload()` for developer iteration (at minimum: drop GPU buffers and re-parse).
- Must not break existing `.s` loading and performance characteristics.
- Keep a clean hook point for future TSRE metadata sidecar (Task 03). v1 can ignore it, but avoid locking the loader into "pure glTF only" assumptions (snapables, bounds overrides, detail level hints may come from TSRE metadata, not glTF itself).
- Must keep logs actionable on failure:
  - file not found
  - unsupported feature encountered (e.g., skinning, Draco compression)
  - texture load failures and fallback behavior

### Embedded Texture Implementation Options (Recommended to Decide Early)
Two reasonable approaches that fit the existing `TexLib` design:

1) **Direct registration API (simplest conceptually)**
   - Use the `TexLib` entrypoint that accepts a pre-populated `Texture*` (and returns an existing id if already present) (`TexLib::addTex(Texture*)`).
   - Benefits: no extra global blob registry; easy content-hash de-dup; works for both GLB and future in-memory sources.
   - Cost: new API surface in `TexLib`; decide threading (sync decode vs background decode that flips `Texture::loaded` later).

2) **Pseudo-loader via `pathid` suffix (mirrors `.:paintTex`)**
   - Create a new `TexLib` type token like `.:glbTex` that routes to a `GltfTexLib` loader, similar to `PaintTexLib`.
   - Store embedded image bytes in a registry keyed by hash; `GltfTexLib` looks up bytes by hash and decodes into `Texture`.
   - Benefits: keeps the "TexLib chooses loader by type" pattern; can integrate with existing threaded loaders.
   - Cost: requires a blob registry and clear lifetime rules (who owns bytes; when they can be freed).

### Embedded Texture Decode Reuse (Preferred)
Avoid duplicating PNG/DDS decode logic in the glTF loader. Instead, centralize "decode these bytes into a `Texture`" in `TexLib`, and reuse existing loaders:

- Add a helper like `TexLib::decodeFromBytes(Texture* texture, const QByteArray& encodedBytes)` that:
  - detects the encoded format (via magic bytes and/or MIME type passed by the caller if desired),
  - routes to the appropriate decoder (initially: reuse Qt image decoding for PNG/JPG, and reuse `DdsLib` logic for DDS if/when we support `MSFT_texture_dds`),
  - fills `texture->imageData` or `texture->compressedData` + `texture->compressedGLFormat`,
  - returns a success flag or an "unsupported format" result for actionable logging.

This keeps embedded GLB image support aligned with file-backed `TexLib` decoding (same color/alpha conventions, same compressed upload path when supported), and makes later KTX2 support an isolated texture task rather than glTF-specific complexity.

### Alpha/Transparency Notes (TSRE Shader Convention)
TSRE's current shader uses a per-vertex `alpha` attribute with sign semantics:
- **positive** value (typically `1.0`) forces opaque output alpha
- **negative** value encodes an alpha cutoff (`discard` when `texAlpha < -vAlpha`)

Suggested glTF mapping:
- `OPAQUE` -> `vAlpha = 1.0`
- `MASK` -> `vAlpha = -alphaCutoff`
- `BLEND` -> best-effort while sorting is missing:
  - default: `vAlpha = -gluu->alphaTest` (discard low alpha like MSTS shapes do; remaining pixels still blend)
  - optional: `vAlpha = 0.0` (no discard; relies fully on blending + draw ordering)

Best-effort policy for `BLEND`:
- Render after opaque geometry (even without per-object sorting this is usually less wrong than mixing).
- Keep an option to downgrade `BLEND` to `MASK` (debug/perf/workflow escape hatch until sorting is implemented).

### Texture Base Path Resolution
TSRE uses `texPath` as a "texture root" for complex shapes (MSTS-style). For glTF, decide a consistent rule for resolving external image URIs:
- If a caller passes a non-empty `texPath` to `ShapeLib::addShape(path, texPath)`, resolve glTF texture URIs relative to that `texPath`.
  - Route/world objects can keep the MSTS behavior: default `texPath` is `routes/<route>/textures`.
- If `texPath` is empty, resolve glTF texture URIs relative to the glTF file directory (tool/preview behavior).

Important: avoid passing **relative** paths into `TexLib` directly. Qt resolves relative `QFile` paths against the process working directory, so the glTF loader must always resolve to an absolute `pathid` (or provide a non-empty base `texPath`) before calling `TexLib::addTex(...)`.

This keeps route objects compatible with existing "route textures folder" assumptions, while allowing tools (shape viewer) to preview a standalone glTF with its adjacent textures.

## Acceptance Criteria
- A `.gltf` or `.glb` placed as a route `Static` renders in the editor.
- A `.gltf` or `.glb` can be opened in Shape Viewer and renders correctly (used as the primary dev/test tool for Task 02).
- Selection/picking mode renders a solid color for the model (no texture).
- Materials with alpha cutout (MASK) correctly discard pixels using `alphaCutoff`.
- A model with multiple primitives/materials renders all submeshes.
- Routes that contain only MSTS shapes continue to behave as before.

## Out Of Scope
- Full PBR shading and glTF extensions for lighting/IBL.
- Transparent blend sorting and per-material render pass buckets (handled under renderer modernization).
- Skinning, morph targets, animation playback.
