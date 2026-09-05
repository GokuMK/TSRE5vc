# Terrain task status

Reviewed against the source and recorded/user-reported tests on 2026-09-05.
This is a status review, not a fresh exhaustive interactive acceptance run.
The long task files retain historical proposals and rejected alternatives;
their old future-tense checklists do not by themselves mean work is missing.

## Current tasks

| Task | Implementation status | Remaining work / qualification |
|---|---|---|
| [Heightmap resolution](terrain-heightmap-resolution.md) | Core support, validation, profiles and shared creation UI implemented | Explicit track-line strip breaks remain; deprecated simple lookup is a separate migration below. Older defect descriptions are historical, not current code. |
| [Patch count](terrain-patch-count.md) | Regular grids through P32 load, view, edit and save; picking implemented | Larger/rectangular/custom grids are outside scope, not unfinished P32 work. |
| [Paged mesh/shared maps](terrain-paged-mesh-and-shared-map.md) | Selected 8-byte float-height/derived-coordinate layout, shared vertices, page traversal and dirty-patch updates implemented | Exhaustive interactive gap/map/selection/shadow and Gather coverage is not recorded. Raw-height and 10/12-byte alternatives remain comparison designs, not selected implementation requirements. |
| [Adjacent edges](terrain-adjacent-edge-cache.md) | Native edge sections, interpolation and invalidation implemented; consumed by LOD | User confirmed cross-tile behavior. Full seam/normal test matrix is not recorded. Rare missing diagonal-owner corner uses the agreed best-effort fallback; no forced unload broadcast. |
| [Basic discrete LOD](terrain-basic-discrete-lod.md) | Tile-local and cross-tile milestones implemented and user-tested | Larger-than-2:1 transitions and mixed spacing along one patch edge remain deliberately best effort. No additional ratio templates or E/AS refinement promised. |
| [Height brushes](terrain-height-brush-performance.md) | Direct tile brush, reusable float area, example, profiling and exact normal optimization implemented and tested | User accepted speed. Populated-route/multiplayer performance coverage is not established by empty-route tests. Old brush remains for comparison/unusual-grid fallback; old paged normal implementation is removed. |
| [Simple lookup migration](terrain-simple-lookup-migration.md) | Design reviewed; preliminary private-helper cleanup implemented | Synthetic detailed lookup and removal of `TerrainLibSimple` remain to implement. |

## Actual follow-up implementation

- Replace deprecated `TerrainLibSimple` with Qt's common terrain machinery and
  deterministic 2 km lookup with no TD reads/writes. Explicit QuadTree
  regeneration is optional repair work, never a side effect of tile creation.
  Resolve multiplayer behavior as part of migration, not merely a class rename.
- KEY_F connected points still use an 8 m discontinuity guard in
  `TerrainLibQt::setTerrainToTrackObj()`. An explicit line-strip/break API would
  remove this heuristic. This is separate from the now-fast action raster.
- More area tools can adopt `TerrainHeightArea`, but only where useful or
  measured; universal conversion is not required to finish height painting.

## Deferred designs, not blockers for current terrain

- Quantized GPU height, tile/patch-local floor/scale and rebase policies;
  10-byte SoA, 12-byte baseline and the other 8-byte candidates. Keep their
  descriptions in the paged-mesh task for future comparisons.
- E/AS/error-bias-driven adaptive triangulation. Basic distance LOD is already
  implemented independently. The
  [MSTS analysis report](../../msts/msts-terrain-adaptive-lod-analysis.md)
  contains a static-analysis milestone; it is not an implemented TSRE renderer.
- Global memory management, expanded custom layouts, and exact stitching for
  deliberately unsupported configurations.

## Related renderer tasks

[High-resolution Gather](../renderer/04-terrain-highres-gather.md) has terrain
packet submission in the current source, including paged geometry and LOD.
Its full visual acceptance cannot be inferred from that alone.
[Distant terrain/water/sky](../renderer/05-terrain-distant-water-sky.md) is a
separate renderer-integration task and is not marked complete here. Gather
itself remains unfinished; terrain completion does not close that workstream.

## Historical text corrected by this review

The heightmap task formerly described fixed-grid route-merge loops, equal-grid-
only seam filling, fixed-size AS loading, and spacing-validation decisions as
current problems. Code now uses destination sample/patch steps, cached world-
space edge sampling, opaque length-driven AS/unknown-buffer preservation, and
explicit integral-spacing/zero-rotation validation. Those original review
sections must not trigger duplicate implementation. The MSTS report also
supersedes early speculation that zero ErrorBias explicitly bypasses E.
