# Textures Task Roadmap

This folder contains ordered tasks for refactoring texture decoding/upload and adding content-hash based de-duplication for non-file-backed textures (GLB embedded images, procedural sources, etc.).

## Execution Order
- [x] `01-texture-format-and-upload-refactor.md`
- [x] `02-texture-memory-stats-and-debug-dump.md`
- [x] `03-ace-converter.md` — GUI and console texture conversion.
- [ ] `04-texture-memory.md` — memory measured jumping across Prudnik (textures 54% of the growth); safe releasing, residency under a budget and the terrain release done; BC7 benchmarked, compression open.
- [x] DDS first, as Open Rails (`feature/dds-first`, 2026-11-10): the `.dds` of an `.ace` or `.dds` name when there is one, else the `.ace` (`docs/features/textures-and-texlib.md`); one entry a file by either name; an unreadable `.dds` falls back to its `.ace`; names written into MSTS files stay `.ace`. Checked: Prudnik, Nysa and back with a 200 MB texture budget, main and the branch the same entries (610, 1328, 1328) and memory, no duplicate entries (the capture's new `libraryTexturesDuplicate`).
- [ ] `05-dds-dx10-and-bc7.md` — planned 2026-11-10, for later: DDS with the DX10 header and BC4, BC5, BC6H and BC7 read and uploaded as blocks (bcdec for the CPU's pixels), as Open Rails content made with modern tools ships them; meanwhile such a file falls back to its `.ace`.
- [x] Mipmaps on shape textures (2026-10-26, `docs/features/textures-and-texlib.md` 3.2.1): off by default; the file's own, made again or improved (coverage kept for cutouts); a sharpness bias. Settings `core.rendering.shapeTextures.*`, `core.rendering.textureLodBias`.

## Ground Rules For All Tasks
- Keep existing formats (`.ace`, `.dds`, common image files, `.:paintTex`) working throughout.
- Maintain the current threading split:
  - loader threads may do file IO + decode/prepare data
  - OpenGL upload must remain on the GL context thread.
- Keep `TexLib` identity matching stable (existing `pathid`-based behavior must not regress).
- Prefer incremental change: introduce a new internal pipeline while keeping old loader paths working until parity is reached.
