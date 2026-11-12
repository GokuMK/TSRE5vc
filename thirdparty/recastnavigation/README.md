# recastnavigation

Walk areas for TSRE's people (task humans 04): Recast builds a navigation
mesh from the triangles of a station's platforms, buildings and terrain
(rasterized into voxels, filtered by a person's height, step and radius,
traced into polygons); Detour finds places and paths on it; DetourCrowd
moves many people along those paths, keeping them apart (tasks 05 and 06).

- Version: 1.6.0
- Source: https://github.com/recastnavigation/recastnavigation/archive/refs/tags/v1.6.0.tar.gz
  (SHA-256 `d48ca0121962fa0639502c0f56c4e3ae72f98e55d88727225444f500775c0074`)
- Files: from the archive, unchanged: `Recast/`, `Detour/` and
  `DetourCrowd/`, each its `Include/` and `Source/`; `License.txt`.

  Left out: `DetourTileCache/` (navigation meshes changed at run time),
  `DebugUtils/` (TSRE draws its own overlay), `RecastDemo/`, `Tests/`,
  `Docs/`, and every `CMakeLists.txt`.
- Licence: zlib, see `License.txt`

TSRE builds it as the static library `tsre_recast` (`CMakeLists.txt`), with
the three `Include/` folders as system include roots: `<Recast.h>`,
`<DetourNavMesh.h>`, `<DetourCrowd.h>`. To update, replace these folders with
a new release's, check that no source file was added to or removed from
their `Source/` folders' CMake lists, and update this file.
