# Terrain-conforming world transfers

## Review and chosen implementation

Transfers are world objects, not terrain textures. Keep their existing W-file
Width/Height/FileName/Position/QDirection representation and editor tools.
The old `TransferObj::drawShape()` sampled a separate 1 or 2 metre rectangular
grid, rotated with the transfer. Its triangles crossed the terrain's alternating
diagonals and sample-grid edges. Correct heights at those sampled vertices cannot
make such triangles fit a non-planar terrain cell. It also oversampled ordinary
8 m terrain and repeated terrain lookup for every emitted vertex. The lookup itself
used bilinear height interpolation (plus an optional height adjustment), which is
also not the piecewise-planar surface drawn by terrain triangles.

Use the actual terrain triangles, clipped against the transfer rectangle:

1. Discover intersected loaded terrain tiles through World-cell lookup, deduplicate
   larger terrain tiles, and restrict work to intersected patches.
2. Reuse the terrain index-template builder, including alternating diagonals,
   current discrete LOD and stitched edges. By default include hidden patches and
   hole triangles: transfers are allowed to cover missing ground. Retain optional
   hole-following (`TransferObj::respectTerrainHoles = true`) for a future UI switch.
   The option is runtime-only, default false, and copied when cloning; no W-file
   extension or user-facing toggle is introduced in this task.
3. Transform each triangle's X/Z into transfer-local coordinates, clip to its four
   rectangle sides, interpolate height and normals at intersections, then triangulate
   the resulting convex polygon. A triangle stays on its original terrain plane.
4. Derive UVs from that same inverse transform: `u = localX / width + 0.5`,
   `v = localZ / height + 0.5`. This preserves a single texture across rotation,
   terrain cells, patches, unequal-resolution tiles and differently sized tiles.
5. Keep the existing 5 cm vertical offset and texture/alpha/selection render paths.
   Transfer Y remains ignored, as in the existing ground-projected object.
6. Partition emitted triangles into disjoint ground and hole-cover buffers in the
   same generation loop. F-hole triangles and hidden patches go only to the hole
   buffer; all others go only to the ground buffer. Clipping, normals, UVs, lift,
   selection outline and invalidation are shared. No second terrain scan or full
   duplicate carpet is generated. The 64 MiB output limit applies to both together.

The detailed terrain is submitted before world objects in both renderers. Retain
its selected patch LOD state for surface consumers, without another LOD calculation
or GPU readback. Transfers cache intersected patch identities, height/gap revision,
hidden state and LOD step/edge mask. Rebuild only when those or the object's placement
change. Unloaded/missing terrain **tiles** produce no carpet at height zero; later availability
is detected on subsequent rendering. No terrain or file data is modified.

For non-yaw quaternions, use the horizontal heading of the rotated X axis; this
remains a ground projection, not a tilted floating rectangle. Invalid/zero dimensions
must produce no mesh rather than division by zero. Resource bounds must protect
against malformed enormous dimensions/allocations.

Implementation files: `src/tsre/world/objects/TransferMesh.{h,cpp}` (CPU geometry
and cache), `TransferObj.{h,cpp}` (existing render/selection integration), and the
small surface-state API in `Terrain.{h,cpp}`. No shader or serialized field changes.
The clipping uses double precision in transfer-relative coordinates, interpolates
normals without renormalizing at cut points (preserving terrain interpolation),
and emits the existing nine-float VNTA layout. Only the selection boundary and
dependency records remain on the CPU after upload, not a duplicate vertex mesh.

Scope/limits:

- Only loaded detailed terrain contributes. No new synchronous disk loads from
  world-object rendering; missing tiles are retried on later frames.
- Height/gap invalidation is conservatively tile-wide for transfers; LOD comparison
  concerns only intersected patches. Terrain texture edits do not rebuild transfers.
- Use the latest submitted topology, not a camera-independent full-resolution mesh.
  Terrain beyond the submitted neighbourhood may have only its previous/native
  topology; this is not an independent distant-terrain rendering system.
- AABB discovery is bounded to 4096 World cells and 65536 m half-extents; emitted
  mesh data is bounded to approximately 64 MiB. Over-budget output is not partially
  rendered. These protect against malformed/absurd transfer sizes.
- At native 1 m resolution an exact transfer can contain more geometry than the old
  2 m carpet; fidelity follows terrain topology. Ordinary 8 m terrain and reduced
  LODs avoid the old oversampling. No separate transfer LOD algorithm is introduced.

## Checklist

### Incremental depth handling (2026-09-10)

Both `Tile::render()` and `Tile::pushRenderItems()` scan objects twice: transfers
first, then non-transfers, with the existing draw logic and selection indices.
This is per-tile ordering, not a route-wide transfer pass; neighbouring tiles can
still interleave transfers and objects. Gather's existing generic/VNTA grouping
is unchanged. The extra scan does not repeat LOD calculations or draw calls.

Normal colour draws of the ground portion of every transfer disable depth writes while
retaining depth testing and applying polygon offset **(-2, -2)**. The shared
`ScopedTerrainDecal` guard restores the previous write mask, polygon-offset enable
state, factor and units after the draw in both legacy and Gather. `terrainDecal`
on the draw item represents this combined behaviour, not a generic write-mask
override. Selection receives neither the offset nor suppressed depth writes.
Hole-covering triangles use a separate `OglObj` with ordinary depth writes and no
polygon offset. Both parts share the same texture through TexLib and the same
transform and selection ID; an entirely hole-covering transfer can still provide
its texture through `getTexId()`.

The extra `OglObj` is created lazily on the first nonempty hole mesh. Its GPU buffers
are released when holes disappear or covering is disabled. The small wrapper and
material handle are retained for reuse, avoiding repeated material acquisition as
holes are edited. An empty part is not drawn or queued. Plain transfers still need
only one draw; mixed transfers need two. With `respectTerrainHoles = true` the
hole-cover output is empty and only the clipped ground part is generated.

The existing 5 cm geometric lift is retained. The initial depth-writes-only step
did not improve the user's `bbb` flickering: it protected later objects, but did
not resolve the transfer's depth test against terrain. Polygon offset supplies
that missing part; the user confirmed that it fixed the tested `bbb` flickering.
The subsequent split preserves this improvement on ground even when the same
transfer also covers a hole. Only the hole portion retains ordinary depth writes
without added polygon offset. Route-wide ordering remains a separate
follow-up: a biased transfer may still overdraw very close objects submitted
from an earlier neighbouring tile. Removing the geometric lift is not part of
this incremental change.

Interactive checks: ground-contact objects, cross-tile transfers, overlapping
transfers, selection, and transfers covering holes, in both renderers.

Offset-step verification: Release build passed; `transfer-mesh` **48/48** and
`transfer-depth-gl` **36/36**. The preceding depth-only step also passed terrain
grid **66/66** and terrain edge **52/52** regressions. The GL suite exercises
production `OglObj` draws and Gather submission with decal mode enabled/disabled,
selection IDs, both incoming write-mask states, and both incoming polygon-offset
enable states with non-default factor/units. It reads back depth and colour and
checks restoration of all modified state. Four additional checks draw coplanar
ground and transfer: the ordinary draw fails `GL_LESS`, the offset draw succeeds,
and the ground depth remains unchanged, in both renderers.
Run `build/TSRE5vc.exe --test --test-suite transfer-depth-gl` with
`QT_QPA_PLATFORM=windows`; it uses an offscreen framebuffer, not an editor window.
This does not replace interactive ordering/Z-fighting acceptance.

The isolated extra scan of 10,000 separately allocated objects averaged
**0.104 ms** over 1,000 runs on this machine. This is warm-cache traversal only,
not a guarantee for every populated route or a whole-frame performance result.
Logs: `build/terrain-material-undo-transfer-depth-cpu.log` and
`build/terrain-material-undo-transfer-depth-gl.log` (ignored build outputs).
Offset verification logs: `build/terrain-material-undo-transfer-offset-cpu.log`
and `build/terrain-material-undo-transfer-offset-gl.log`.

Split-mesh verification: Release build, `transfer-mesh` **53/53** and
`transfer-depth-gl` **44/44** passed. CPU checks cover complete area partitioning,
the common UV transform, all-hidden terrain, cache hits preserving both outputs,
and disabling covering without changing ground vertices. Added GL integration
checks submit a real `TransferObj`: solid/mixed/all-hidden terrain, a shared
texture, identical picking IDs, disabling/re-enabling covering, and removing the
second draw when a hole is filled. Existing legacy/Gather depth-state and coplanar
tests also pass. Logs: `build/terrain-material-undo-transfer-split-cpu.log` and
`build/terrain-material-undo-transfer-split-gl.log`.

- [x] Pure CPU rectangle clipping / affine UV generation.
- [x] Terrain topology, height/gap revision and current-LOD integration.
- [x] Transfer renderer, cache invalidation and conforming selection outline.
- [x] CPU coverage: planar and non-planar cells, rotation/UVs, boundaries, mixed
      layouts, LOD/edge masks, gaps, edits, missing terrain and invalid dimensions.
- [x] Build and relevant terrain regression suites.
- [x] Interactive acceptance on real transfers (user).

User acceptance (2026-09-10): the terrain-conforming mesh looked correct; polygon
offset resolved the reported `bbb` flickering; the final ground/hole split was
subsequently reported to work really well. This records acceptance of the tested
scenes, not an exhaustive visual matrix for every renderer and terrain layout.

## Initial mesh verification (2026-09-10)

These are the initial milestone results; current split-mesh results are **53 CPU
and 44 OpenGL checks** above.

Release / Qt 6.10.1 / MinGW 13.1 build passed. Automated fixtures only: no route
data was changed. Commands (from repository root, Qt runtime on PATH):

```text
build/TSRE5vc.exe --test --test-suite transfer-mesh
build/TSRE5vc.exe --test --test-suite terrain-grid
build/TSRE5vc.exe --test --test-suite terrain-edges
build/TSRE5vc.exe --test --test-suite terrain-material-gl
```

The first three can run with `QT_QPA_PLATFORM=offscreen`. The OpenGL regression
uses the Windows platform plugin and an offscreen GL surface. Preserve `log.txt`
when running these tests alongside a working editor installation.

- Transfer mesh: **42 passed, 0 failed**. Includes triangle-interior height checks
  over a spike, six rotations, quaternion sign invariance, UV continuity/coverage,
  outline perimeter, mixed-resolution four-tile corner, 4 km terrain deduplication,
  2048/P32, LOD and stitched edges, default hole covering and opt-in hole removal,
  terrain edits, texture-only edits, unload/reappearance and malformed dimensions.
- Terrain grid: **66 passed, 0 failed**.
- Adjacent terrain edges / cross-tile LOD: **52 passed, 0 failed**.
- Existing procedural terrain OpenGL regression: **0 failures**. This is a
  regression check, not interactive visual acceptance of the new world transfer.

Illustrative local measurements (not cross-machine performance guarantees):

- Axis-aligned 100 x 100 m transfer on native 8 m terrain: **1,320 vertices**,
  compared with **15,000** for the old independent 2 m grid.
- Cached rotated 200 x 200 m transfer: **1.22 microseconds** per dependency check
  averaged over 1,000 calls, no rebuild/upload.
- Rotated 100 x 100 m transfer on native 1 m / 2048-P32 terrain: **62,580 vertices**,
  **17.09 ms** average CPU rebuild over 20 runs. GPU upload is not in this timing.

Logs (ignored build output): `build/terrain-material-undo-transfer-mesh.log`,
`build/terrain-material-undo-transfer-grid-regression.log`,
`build/terrain-material-undo-transfer-edges-regression.log`, and
`build/terrain-material-undo-transfer-gl-regression.log`.

Suggested future regression checks: rotate/resize/move a textured transfer over
sharp relief and a terrain/tile boundary, cross LOD distances, edit underlying
heights, confirm selection outline and alpha appearance, and cover a terrain hole.
Repeat with legacy and paged terrain and both object renderers. The user acceptance
above does not specify an exhaustive renderer-by-renderer test matrix.

Related: [basic discrete terrain LOD](../terrain/terrain-basic-discrete-lod.md),
[adjacent edges](../terrain/terrain-adjacent-edge-cache.md).
