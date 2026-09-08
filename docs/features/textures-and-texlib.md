# TSRE Textures and Texture Management (Current State)

Updated 2026-09-08 for the [new ACE library](ace-library.md). That guide contains
API examples, memory policy, format qualifications and verification commands.

## Scope
- Project: TSRE5vc
- Goal: document how textures are identified, loaded (threaded), cached, and uploaded to OpenGL today.
- Non-goal: fully generalize the texture pipeline for all encodings (see `docs/tasks/textures/`).

## Evidence Base (Code Anchors)
- Texture cache and loader dispatch: `src/tsre/texture/TexLib.cpp:68`
- Texture identity/de-dup mechanism: `src/tsre/texture/TexLib.cpp:94`, `src/tsre/texture/Texture.cpp:22`
- OpenGL upload path: `src/tsre/texture/Texture.cpp:351`
- ACE worker/document adapter: `src/tsre/texture/AceLib.cpp`
- ACE container and codecs: `src/tsre/texture/AceDocument.cpp`, `src/tsre/texture/DxtCodec.cpp`
- DDS loader (keep DXT blocks; uncompressed RGB decode): `src/tsre/texture/DdsLib.cpp:339`
- Standard image loader (QImage -> RGB/RGBA): `src/tsre/texture/ImageLib.cpp:21`
- Procedural/in-memory texture example (text rendering): `src/tsre/ogl/TextObj.cpp:64`, `src/tsre/texture/PaintTexLib.cpp:20`

---

## 1. Core Types

### 1.1 `TexLib`
`TexLib` is the global texture cache:
- store: `std::unordered_map<int, Texture*> TexLib::mtex`
- id allocation: monotonic `jesttextur`
- API shape:
  - `addTex(path, name)` and `addTex(pathid)` for cache + load dispatch
  - `addTex(Texture*)` for registering pre-populated in-memory textures (content-hash, procedural, embedded)
  - `getTex(pathid)` for lookup by identity
  - `cloneTex(id)` and `save(...)` for editor flows

Loader dispatch is based on file extension (or pseudo extension):
- `.ace` -> `AceLib`
- `.dds` -> `DdsLib`
- `.png/.bmp/.jpg/.tga` -> `ImageLib`
- `.:paintTex` -> `PaintTexLib` (procedural/in-memory)
- `.:mapTex` -> `MapLib` (procedural/in-memory)

### 1.2 `Texture`
`Texture` represents a single texture resource with:
- identity: `pathid` and `hashid[]` (multiple aliases supported)
- CPU-side pixels: `imageData`, `bytesPerPixel`, `type` (GL_RGB/GL_RGBA), `loaded`, `editable`
- optional encoded blocks for direct GPU upload: `compressedData` + `compressedGLFormat`
- transient authored mip staging: `sourceMipmaps`; released after successful upload
- small ACE metadata: `aceMetadata`; full source document: explicit opt-in `aceDocument`
- GPU-side handle: `tex[0]`, `glLoaded`

`hashid[]` is used for identity matching in `TexLib::getTex/addTex`. Example: `Texture(pathid)` adds `pathid` and may add an alias (e.g. `.dds` adds a corresponding `.ace` alias) (`src/tsre/texture/Texture.cpp:22`).

---

## 2. Identity, Hashing, and De-duplication

### 2.1 Current De-dup Key
`TexLib` de-duplicates by **string identity**, not by image content:
- it scans existing textures and compares `pathid` against each `Texture::hashid[]` entry.
- this supports aliases (e.g. treat `.dds` and `.ace` as equivalent in some cases).

### 2.2 Procedural/In-memory Textures Already Exist
`TextObj` creates textures via a pseudo path ending with `.:paintTex` (e.g. `"Hello.size:32.color:#ffffff.:paintTex"`) (`src/tsre/ogl/TextObj.cpp:64`).
`TexLib` routes that to `PaintTexLib`, which fills `Texture::imageData` directly using `QImage/QPainter` (`src/tsre/texture/PaintTexLib.cpp:20`).

This pattern is important for GLB embedded images: TSRE already has a mechanism for non-file-backed textures.

---

## 3. Threading Model (Load vs Upload)

### 3.1 Loader Threads
Most loaders are `QThread`-based (`AceLib`, `DdsLib`, `ImageLib`) and write into a shared `Texture*`:
- they read from disk (or generate pixels)
- they fill `Texture` fields (`width/height/type/imageData/...`) and may also fill `compressedData` for encoded sources
- they set `loaded = true` (and often `editable = true`)

### 3.2 OpenGL Upload Happens Later
OpenGL upload is not performed in the loader threads.
Instead, render code checks `Texture::loaded` and calls `Texture::GLTextures()` on demand (e.g. `SFile::pushRenderItem`, `OglObj::pushRenderItem`, terrain gather) to upload to GPU on the GL context thread.

---

## 4. Conversion and Upload (Current Behavior)

### 4.1 CPU Decode/Conversion Happens Per-Format
Each loader converts its input into CPU-side RGB/RGBA8:
- `AceLib` structurally parses ACE with `AceDocument`, decodes planar/packed/indexed images to RGB/RGBA, and keeps DXT1/3/5 blocks for direct upload. DXT2/4 normalize premultiplied colors on CPU. CPU-only callers explicitly request pixels. Authored mip staging is independent of eventual filtering.
- `DdsLib` stores DXT1/DXT3/DXT5 as encoded blocks (`compressedData`) and handles uncompressed DDS variants by decoding to RGB/RGBA `imageData`.
- `ImageLib` uses `QImage` conversion to RGB888/RGBA8888.

### 4.2 GPU Upload is Usually Uncompressed (But Can Be Compressed)
`Texture::GLTextures()` uploads either:
- uncompressed pixels via `glTexImage2D(..., GL_UNSIGNED_BYTE, imageData)`, or
- compatible DXT1/3/5 blocks via `glCompressedTexImage2D`, with CPU decoding when unsupported.

After successful upload, `Texture::GLTextures()` deletes `imageData`, clears `compressedData` and `sourceMipmaps`, and sets `editable = false`. Small metadata remains; a full document remains only when explicitly requested. `GLTextures(true)` uses authored mips when staged, otherwise generates them. `false` uploads only the base level.

Note:
- `Game::AARemoveBorder` (alpha-border clearing) is not applied for the compressed upload path (we do not decode/patch/re-encode compressed blocks).

Implications:
- GPU textures can stay **compressed** for sources that provide compatible blocks (ACE and DDS DXT1/3/5).
- `editable` becomes `false` after upload (but `setEditable()` can read pixels back from GPU later via `glGetTexImage`).

---

## 5. Remaining Texture-Pipeline Work

ACE and Texture now share CPU DXT decoding through `DxtCodec`; the DDS reader
still has its own format parsing and does not stage its authored mip chain.
Other image formats remain decoded RGB/RGBA sources. Full cache lifetime,
deferred GPU deletion and concurrent reload ownership are not redesigned here.

`editable` still means that CPU pixels are ready now. `setEditable()` decodes
retained blocks or reads back an existing GPU texture; it does not require
permanent duplicate storage. Pixel edits invalidate source blocks/document/mips.
`update()` regenerates mips only for a texture that uses them.

`TexLib::addTex(Texture*, true)` now moves content through
`Texture::takeContentFrom()` instead of manually copying selected fields.
Cache identity/aliases/reference count remain cache-owned. GPU replacement,
upload and readback require the owning context.

---

## 6. Texture Saving (Editor)
`TexLib::save(type, path, id)` is currently ACE-focused:
- it ensures the texture is editable (CPU decode, or existing-GPU `glGetTexImage` readback),
- then calls `AceLib::save(...)` to write RGB or RGBA ACE without mips by default;
- explicit `AceWriteOptions` select encoding/mips/zlib through the new API. Procedural baking uses the general QImage overload with explicit opaque RGB options.

This is used by terrain/map-texture workflows; saving other formats is not implemented today.

---

## 7. Debugging / Memory Stats
TSRE can dump a quick texture summary to the console/debug output:
- hotkey: `Ctrl+Shift+F10` (Route Editor and Shape Viewer)
- output: total texture count + CPU pixel bytes, other CPU source bytes, and estimated GPU bytes (the existing `cpuEncodedMB` label includes staged mips/ACE metadata now)

Notes:
- GPU bytes are estimated from the uploaded internal format recorded at upload time (`Texture::gpuInternalFormat`).
- The dump is intended for quick comparisons (e.g. before/after refactors), not as a precise GPU profiler.
