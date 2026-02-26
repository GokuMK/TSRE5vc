# TSRE Textures and Texture Management (Current State)

## Scope
- Project: TSRE5vc
- Goal: document how textures are identified, loaded (threaded), cached, and uploaded to OpenGL today.
- Non-goal: fully generalize the texture pipeline for all encodings (see `docs/tasks/textures/`).

## Evidence Base (Code Anchors)
- Texture cache and loader dispatch: `src/tsre/texture/TexLib.cpp:68`
- Texture identity/de-dup mechanism: `src/tsre/texture/TexLib.cpp:94`, `src/tsre/texture/Texture.cpp:22`
- OpenGL upload path: `src/tsre/texture/Texture.cpp:351`
- ACE loader (CPU decode for RGB/RGBA; can keep DXT1 blocks): `src/tsre/texture/AceLib.cpp:30`
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
- `AceLib` decodes uncompressed ACE into RGB/RGBA `imageData`; for DXT1-compressed ACE it can keep the encoded blocks in `compressedData` (and only CPU-decode when needed, e.g. for downscaling).
- `DdsLib` stores DXT1/DXT3/DXT5 as encoded blocks (`compressedData`) and handles uncompressed DDS variants by decoding to RGB/RGBA `imageData`.
- `ImageLib` uses `QImage` conversion to RGB888/RGBA8888.

### 4.2 GPU Upload is Usually Uncompressed (But Can Be Compressed)
`Texture::GLTextures()` uploads either:
- uncompressed pixels via `glTexImage2D(..., GL_UNSIGNED_BYTE, imageData)`, or
- encoded DXT1 blocks via `glCompressedTexImage2D` when `compressedData` is present and S3TC/BC1 is supported.

After upload, `Texture::GLTextures()` deletes `imageData` (if present), clears `compressedData`, and sets `editable = false`.

Note:
- `Game::AARemoveBorder` (alpha-border clearing) is not applied for the compressed upload path (we do not decode/patch/re-encode compressed blocks).

Implications:
- GPU textures can stay **compressed** for sources that provide compatible blocks (currently: DXT1-in-ACE).
- `editable` becomes `false` after upload (but `setEditable()` can read pixels back from GPU later via `glGetTexImage`).

---

## 5. Known Limitations (Motivating Refactor)
- DXT/ACE decode work is duplicated across loaders (and will grow as more source formats are added).
- There is no centralized concept of "encoded texture source" vs "decoded pixels" (we have the first hook via `compressedData`, but it is not generalized yet).
- GPU capability-based choice is only partially implemented (currently: DXT-in-ACE and DXT-in-DDS; other formats still decode to RGBA8 before upload).
- Editor workflows need predictable behavior for `editable` textures (keep CPU RGBA when required).

See `docs/tasks/textures/01-texture-format-and-upload-refactor.md` for the proposed next step.

---

## 6. Texture Saving (Editor)
`TexLib::save(type, path, id)` is currently ACE-focused:
- it ensures the texture is editable (may trigger a GPU upload + `glGetTexImage` readback),
- then calls `AceLib::save(...)` to write an `.ace` file.

This is used by terrain/map-texture workflows; saving other formats is not implemented today.

---

## 7. Debugging / Memory Stats
TSRE can dump a quick texture summary to the console/debug output:
- hotkey: `Ctrl+Shift+F10` (Route Editor and Shape Viewer)
- output: total texture count + CPU pixel bytes, CPU encoded bytes, and estimated GPU bytes

Notes:
- GPU bytes are estimated from the uploaded internal format recorded at upload time (`Texture::gpuInternalFormat`).
- The dump is intended for quick comparisons (e.g. before/after refactors), not as a precise GPU profiler.
