# Terrain task status

Reviewed against the source and recorded/user-reported tests on 2026-09-08.
This is a status review, not a fresh exhaustive interactive acceptance run.
The long task files retain historical proposals and rejected alternatives;
their old future-tense checklists do not by themselves mean work is missing.
Includes the ACE v2 integration, procedural save optimizations and independent
detail distance. Test counts in historical sections remain milestone-specific.

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
| [Procedural materials](terrain-procedural-materials.md) | Experimental demo; four-worker loading and synchronous painting tested | Compressed ID plane, F2 tools, shader import, BC1/RGB sharing, tile-level cache release and bounded background generation. Nearest-visible-first requests/uploads within each tile; draw order unchanged. Other production features deferred. |
| [Procedural baked fallback / catalogue](terrain-procedural-baked-fallback.md) | Stage A and DXT1 follow-up implemented/tested; B remains design only | A: one 1024-square opaque DXT1 ACE bake on save through [AceLib/AceDocument](../../features/ace-library.md), keep-bake-on-disable, reserved source palette, incremental saves and independent 2048 m per-patch detailed-texture distance. B: per-route material catalogue alternatives. |

## Tracked milestone and sub-task checklist

This is the current checklist. Unchecked items are not implied authorization to
implement them; deferred designs are not blockers for the working terrain tools.

- [x] Heightmap resolutions, validation and shared tile-profile UI.
- [x] Regular patch grids through P32, including editing/picking/saving.
- [x] Paged shared terrain/map meshes with the selected 8-byte layout.
- [x] Native adjacent-edge cache and cross-tile discrete LOD stitching.
- [x] Fast height brushes, reusable float-area API and exact normal optimization.
- [x] Procedural ID-map painting/fills, cross-tile shader import and shared output.
- [x] Four-worker lazy generation, per-tile nearest-first scheduling and residency release.
- [x] Stage A bake/disable fallback, miniature caching and incremental saves.
- [x] Independent 2048 m detailed-texture distance and load-time bake prefetch.
- [x] DXT1 baked fallback, now 1024-square: 421 CPU checks in both near-output modes and procedural GL pass; old-size regeneration, legacy RGB loading and unchanged-block preservation covered.
- [x] [Stage B route material catalogue](../../features/terrain-material-library.md): UTF-16 `.dat`, stable UiDs, tile-local byte-ID mapping, Choose/From image, legacy no-table fallback and mapping-aware undo. User confirmed the revised separate-button and mixed-material history workflow works.
- [x] [Procedural undo](terrain-procedural-materials.md#procedural-undo-follow-up): one deep ID-map snapshot per tile/action; existing two-second stroke segmentation; background level-1 compression; restore IDs, toggle/palette/UV state, not generated textures. Region deltas remain an alternative.
- [ ] Procedural settings UI: detail distance, output sizes and debug/restore validation; replace internal-only controls when the production settings design is agreed.
- [ ] [Simple lookup migration](terrain-simple-lookup-migration.md): remove `TerrainLibSimple`, using synthetic no-TD lookup in the common backend.
- [ ] KEY_F explicit line-strip boundaries instead of the 8 m break heuristic.

Deferred production features / testing (not requirements to close Stage A):

- [ ] Richer material/UV/detail/mixing properties, source alpha and physical-scale policy.
- [ ] Seasonal materials and live source reload.
- [ ] Procedural route merge, portable export/sidecar cleanup and multiplayer support.
- [ ] Broader Gather, gap/seam/shadow and populated-route/multiplayer acceptance coverage.
- [ ] E/AS-driven adaptive triangulation.
- [ ] Quantized heights/rebasing and other retained vertex-layout experiments, if measurements justify them.
- [ ] Global memory management and expanded/custom-layout policies, as separate tasks.

## Actual follow-up implementation

- Replace deprecated `TerrainLibSimple` with Qt's common terrain machinery and
  deterministic 2 km lookup with no TD reads/writes. Explicit QuadTree
  regeneration is optional repair work, never a side effect of tile creation.
  Resolve multiplayer behavior as part of migration, not merely a class rename.
- KEY_F connected points still use an 8 m discontinuity guard in
  `TerrainLibQt::setTerrainToTrackObj()`. An explicit line-strip/break API would
  remove this heuristic. This is separate from the now-fast action raster.
  The input is a flat list of points sampled around every 4 m and can contain
  separate paths without separators. Consecutive points at most 8 m apart are
  joined; more distant pairs are stamped individually, without a connecting
  terrain strip. This guesses where paths end: nearby unrelated paths can still
  be joined, and a legitimate widely spaced segment can be split. The threshold
  is not terrain heightmap resolution. Explicit path boundaries remove that guess.
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
sections must not trigger duplicate implementation. For ErrorBias, distinguish
code execution from its practical effect: the recovered MSTS path does not
explicitly bypass E/error calculations when ErrorBias is zero, but multiplying
the error contribution by zero removes its effect on LOD selection, yielding
maximum detail. This is consistent with the MSRE dialog descriptions reported
by the user: **0 = maximum detail, 1 = standard detail, 2 = half detail**. The
absence of a special bypass branch must not be interpreted as disproving that
zero effectively disables error-based mesh simplification. These UI descriptions
are not a claim that triangle counts scale exactly with the value.
