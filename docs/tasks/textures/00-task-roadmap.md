# Textures Task Roadmap

This folder contains ordered tasks for refactoring texture decoding/upload and adding content-hash based de-duplication for non-file-backed textures (GLB embedded images, procedural sources, etc.).

## Execution Order
- [x] `01-texture-format-and-upload-refactor.md`
- [x] `02-texture-memory-stats-and-debug-dump.md`

## Ground Rules For All Tasks
- Keep existing formats (`.ace`, `.dds`, common image files, `.:paintTex`) working throughout.
- Maintain the current threading split:
  - loader threads may do file IO + decode/prepare data
  - OpenGL upload must remain on the GL context thread.
- Keep `TexLib` identity matching stable (existing `pathid`-based behavior must not regress).
- Prefer incremental change: introduce a new internal pipeline while keeping old loader paths working until parity is reached.
