# Textures Task Roadmap

This folder contains ordered tasks for refactoring texture decoding/upload and adding content-hash based de-duplication for non-file-backed textures (GLB embedded images, procedural sources, etc.).

## Execution Order
- [x] `01-texture-format-and-upload-refactor.md`
- [x] `02-texture-memory-stats-and-debug-dump.md`
- [x] `03-ace-converter.md` — GUI and console texture conversion.
- [ ] `04-texture-memory.md` — memory measured jumping across Prudnik (textures 54% of the growth); safe releasing, residency under a budget and the terrain release done; BC7 benchmarked, compression open.
- [x] Mipmaps on shape textures (2026-10-26, `docs/features/textures-and-texlib.md` 3.2.1): off by default; the file's own, made again or improved (coverage kept for cutouts); a sharpness bias. Settings `core.rendering.shapeTextures.*`, `core.rendering.textureLodBias`.

## Ground Rules For All Tasks
- Keep existing formats (`.ace`, `.dds`, common image files, `.:paintTex`) working throughout.
- Maintain the current threading split:
  - loader threads may do file IO + decode/prepare data
  - OpenGL upload must remain on the GL context thread.
- Keep `TexLib` identity matching stable (existing `pathid`-based behavior must not regress).
- Prefer incremental change: introduce a new internal pipeline while keeping old loader paths working until parity is reached.
