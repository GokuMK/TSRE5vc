# Task 01 - Texture Format and Upload Refactor

## Objective
Move "encoded format handling + conversion decisions" out of per-format loaders and into `Texture` (or a dedicated helper), so new formats (including GLB embedded images and future compressed formats) can reuse one pipeline.

Target outcomes:
- represent both **encoded** sources (DXT blocks, future KTX2/Basis, etc.) and **decoded** sources (RGB/RGBA pixels)
- decide once per texture whether to:
  - upload encoded data directly (if GPU supports), or
  - decode/convert to RGBA for upload and/or editing
- make `editable` behavior explicit and predictable

## Context (Current State)
- `AceLib` decodes uncompressed ACE into RGB/RGBA `Texture::imageData`; for DXT1-compressed ACE it can keep DXT1 blocks in `Texture::compressedData` for direct upload.
- `DdsLib` stores DXT1/DXT3/DXT5 as encoded blocks (`Texture::compressedData`) and marks `loaded = true` (uncompressed DDS still decodes to `imageData`).
- `ImageLib` decodes into RGB/RGBA `Texture::imageData` and marks `loaded = true`.
- `Texture::GLTextures()` can upload either uncompressed pixels (`glTexImage2D`) or certain encoded blocks (`glCompressedTexImage2D` when available); after upload it frees CPU-side sources and makes textures non-editable by default (`src/tsre/texture/Texture.cpp:351`).
- TSRE already supports procedural/non-file-backed textures through `TexLib` pseudo-types like `.:paintTex` (`src/tsre/texture/TexLib.cpp:90`, `src/tsre/texture/PaintTexLib.cpp:20`).

This works, but scaling it to more formats will duplicate conversion logic and miss opportunities to keep GPU-native encodings.

## Scope
- Introduce an internal "source representation" for textures (name TBD):
  - example: `TextureSource` with variants:
    - `RawPixels` (RGB/RGBA8)
    - `CompressedBlocks` (DXT1/DXT3/DXT5 blocks, later other formats)
    - optional: mip levels, cube faces, array layers (defer if too large)
- Add a single "upload/prepare" flow used by `Texture::GLTextures()` (or a successor API).
- Add a central GPU capability probe:
  - query supported compressed internal formats once after GL context creation
  - expose `supports(format)` queries to the texture pipeline
- Define explicit editor-facing rules for `editable`:
  - textures are non-editable by default (current behavior after upload)
  - if the editor requests editability, the texture must end up with CPU-side RGBA pixels resident:
    - use existing decoded pixels if already present
    - else decode/convert if encoded and GPU upload isn't possible
    - else download from GPU (`glGetTexImage`) as a slow fallback
  - if a texture is GPU-only (encoded and not decoded), it is non-editable until explicitly requested

## Suggested Touch Points
- `src/tsre/texture/Texture.h/.cpp` (new source storage + upload decision logic)
- `src/tsre/texture/TexLib.cpp` (optional: new registration API for content-hash textures)
- `src/tsre/texture/AceLib.*`, `src/tsre/texture/DdsLib.*`, `src/tsre/texture/ImageLib.*` (switch from "always decode" to "fill source representation")
- GL init location for capability query (likely after GL context exists; candidates include `GLUU::initShader` or renderer init)

## Design Notes / Decisions

### A) Content Hash for Embedded Textures (GLB)
We want de-duplication across GLBs when embedded image bytes are identical.

Recommended:
- compute a hash on the **encoded image bytes** (not decoded RGBA), e.g. SHA-256 using Qt (`QCryptographicHash`)
- build a stable identity string like: `glbimg:sha256:<hex>:len:<n>`
- add it to `Texture::hashid` so `TexLib::getTex` can find it without special casing
- register the resulting in-memory texture via `TexLib::addTex(Texture*)`

Collision policy:
- SHA-256 collisions are effectively negligible for our use.
- if a faster non-crypto hash is preferred later, add byte-compare on match as a safety check.

### B) Editable Rule
Keep the existing editor workflow:
- Textures are treated as non-editable by default after upload.
- When editability is requested, prefer the cheapest path:
  1) if decoded RGBA pixels are already present (e.g. loaded via `QImage`, or already converted), keep them and mark editable
  2) else if encoded data can be decoded to RGBA on CPU, decode once and keep RGBA in RAM
  3) else if texture is already on GPU, download RGBA from GPU (`Texture::setEditable()` slow path)

### C) GPU-native upload
If the source is in a GPU-supported compressed format:
- upload compressed data directly (e.g. via `glCompressedTexImage2D`)
- keep or discard CPU-side encoded blocks based on memory policy
- if later made editable, either:
  - download + convert to RGBA, or
  - force a decode path earlier when `editable` is requested

This task should implement direct upload at least for DDS DXT1/DXT3/DXT5 when the GPU supports S3TC/BC1-3.

## Requirements
- Existing textures still load and render:
  - MSTS `.ace`
  - `.dds` (DXT1/3/5 + existing uncompressed support)
  - common image files via `QImage`
  - `.:paintTex` procedural textures
- OpenGL upload remains on the GL context thread.
- The engine can still request `setEditable()` and paint on textures as before.
- Provide a documented, stable API path to register in-memory/embedded textures with a content hash identity.

## Acceptance Criteria
- No regression in route/editor texture rendering in legacy mode.
- `TexLib` can register and de-duplicate an in-memory texture by content hash (design-level acceptance; implementation tested later).
- Direct upload is used for DDS DXT textures when supported by GPU; fallback decode-to-RGBA path still works when not supported.
- When editability is requested, the engine ends up with CPU-side RGBA pixels (using existing pixels / decode / GPU download).
- Non-editable textures can discard CPU data after upload (matching current memory behavior).

## Notes / Known Limitations
- `Game::AARemoveBorder` (alpha-border clearing) is only applied on the CPU RGBA upload path; it is skipped for direct compressed uploads because it would require decoding/re-encoding. This is accepted (feature is not required going forward).
- Texture saving is ACE-focused today (`TexLib::save` -> `AceLib::save`), primarily used for terrain/map-texture workflows; generalizing save support is out of scope for this task.

## Out Of Scope (for this task)
- Adding new external dependencies for texture compression (offline encoders).
- Full mipmap chain handling for all formats (can be added incrementally after the pipeline exists).
- New texture formats like KTX2/Basis (enabled by this refactor, but implemented as follow-up tasks).
