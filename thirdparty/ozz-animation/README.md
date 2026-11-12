# ozz-animation

Skeletal animation for TSRE's skinned glTF shapes (task humans 02): a
skeleton, clips sampled at a time, blended, joint matrices from local poses;
later inverse kinematics for the player's humans. TSRE's own glTF reader
fills ozz's raw skeletons and animations and ozz's builders make the
run-time data at load, so there are no `.ozz` files and no importer.

- Version: 0.16.0
- Source: https://github.com/guillaumeblanc/ozz-animation/archive/refs/tags/0.16.0.tar.gz
  (SHA-256 `a7a34322344e9d839eaf637bbc463404c6aed3f52583dea95c856fea580c2693`)
- Files: from the archive, unchanged:
  - `include/ozz/base/`, `include/ozz/animation/runtime/`, and the headers
    directly in `include/ozz/animation/offline/` (not its `fbx/` and
    `tools/`);
  - `src/base/`, `src/animation/runtime/`, and the files directly in
    `src/animation/offline/` (not its `fbx/`, `gltf/` and `tools/`);
  - `LICENSE.md`.

  Left out: the geometry library (TSRE skins on the GPU), the options
  library, the importers and tools, samples, tests, `extern/`, and every
  `CMakeLists.txt`.
- Licence: MIT, see `LICENSE.md`

TSRE builds it as the static library `tsre_ozz` (`CMakeLists.txt`), with
`include/` as a system include root: `<ozz/animation/runtime/skeleton.h>`.
To update, replace these files with a new release's (the same ones left
out), check the source lists in its `src/*/CMakeLists.txt` against what the
`tsre_ozz` glob picks up, and update this file.
