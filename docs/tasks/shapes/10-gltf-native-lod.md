# Task 10 - Native glTF mesh LODs

Status: steps 1, 2 and 4 implemented on `feature/gltf-lod`; step 3's metric
defined, its values still to tune. Updated 2026-10-19.
Both asset layouts are supplied; this task covers engine work only.

## Engine scope

1. Load external mesh LODs through the existing generic shape LOD machinery.
   `FileName ( "tree.gltf" )` is LOD0; discover `tree_LOD01.gltf`,
   `tree_LOD02.gltf`, etc. Retain the unsuffixed entry when companions exist,
   sort numerically and validate missing/invalid levels. Define discovery/case
   rules explicitly. External `.glb` discovery is additional TSRE capability,
   not something established by the inspected OR implementation.
2. Load internal `MSFT_lod` references on the highest-detail node. The supplied
   files have root node 0 with ids [1,2]; only node 0 is in the ordinary scene.
   Support both glTF and embedded GLB and reject invalid/cyclic references.
3. Read root extras.MSFT_screencoverage for either layout, without requiring
   MSFT_lod on external files. Establish the coverage metric and map it to the
   native selector, accounting for viewport/FOV, bounds and instance scale.
   Fixture values [0.2,0.05,0.001] are provisional OR defaults/example values,
   not converted generator projected-height thresholds. Tune after captures.
4. Reuse shared meshes, materials and texture resources; choose LOD per instance
   while batching compatible instances. Preserve pivots, transforms and bounds.
   Keep the final mesh end boundary available to a future appended impostor;
   do not make it an unconditional whole-object visibility cutoff.

## Implementation (feature/gltf-lod, 2026-10-19)

- Internal (step 2): `GltfShape` reads `MSFT_lod.ids` (with `MSFT_lod` in
  `extensionsUsed`) and `extras.MSFT_screencoverage` per node. From the
  scene's roots each node is common (drawn at every level) or of one level;
  the nodes a group names stand in for it under its parent. A group naming
  invalid nodes (out of range, itself, nodes of the scene or of another
  group, a cycle), nested in a level, or without the extension in
  `extensionsUsed` draws level 0 only, with a warning; the nodes it names
  outside the scene are not drawn. Before, every node with a mesh was drawn:
  all levels on top of each other.
- External (step 1): beside `tree.gltf` (or `tree_LOD00.gltf`, the same level
  0) the files `tree_lod01.gltf`, `tree_lod02.gltf`... of the entry's format
  (`.glb` beside `.glb`). Their names are not referenced, so by TSRE's file
  name rules the part TSRE adds is lowercase (the referenced stem kept, as an
  `.sd` beside its `.s`) and no other spelling is looked for: the supplied
  `_LOD01` fixtures open on Windows only, an asset defect to report to
  tree-gen. Consecutive from 1: a missing number or a file that does not load
  ends them, with a warning. Internal levels take precedence over level
  files. Level files load on the worker into the same shape.
- Selection (step 3): `ShapeLod` (shared with the MSTS distance levels,
  [task 11](11-msts-distance-levels.md)) picks each object's level by screen
  coverage: the largest extent of level 0's bounds over the height the view
  spans at the 3D distance to the bounds' middle, `2 d tan(fov / 2)` (Unity's
  relative height, which Open Rails says it follows; it projects the box's
  corners). Level i draws above `MSFT_screencoverage[i]`, Open Rails'
  defaults 0.2, 0.05, 0.001 without it; past them all the last level, so the
  final threshold hides nothing (`TSRE_SHAPE_LOD=2` does). The view's field
  of view is `core.camera.fieldOfView`; instance scale is not included yet.
- Sharing (step 4): packets per level, shared by the objects drawing it, so
  instances of a level stay instanced. Pivots and transforms kept; the
  selection box and the coverage use level 0. The Shape Viewer lists the
  levels.
- Tests: suite `gltf-lod` (34): internal groups in glTF and GLB, invalid
  references, default coverages, the level by coverage, the external file
  rules, the TREES_EXT spruce loaded as on a worker; diffuse transmission's
  parsing ([task 36](../renderer/36-foliage-backlighting.md)).
- Steam Deck, 1280x800, QRhi: TREES, 479 black locusts (levels of 23,472,
  10,552 and 452 triangles), 74-101 ms to 34-43 ms a frame (before, all
  three levels drawn); the TREES_EXT spruce line, 19.1 to 17.4 ms; the levels
  differ by a few pixels at their switch.

Open: tune the coverages after captures; instance scale; the 1, 100 and 1,000
instance profiles with GPU time and memory; an impostor after the final
level (world task).

## Supplied fixtures

[Catalogue](../../../../tree-gen/docs/runtime-catalogue.md) and
[asset contract](../../../../tree-gen/docs/runtime-fixtures.md) are owned by tree-gen.
Generated binaries are excluded from the tree-gen source repository; follow its
[rebuild instructions](../../../../tree-gen/docs/rebuilding.md) to prepare them.
All seven species have these alternative route-style roots:

- `out-v2-runtime/external`: unsuffixed LOD0 plus `_LOD01`/`_LOD02` glTFs and bins.
- `out-v2-runtime/internal`: one all-LOD glTF/bin, plus an equivalent embedded GLB.

Each has SHAPES and TEXTURES; do not merge the alternative SHAPES directories.
Start with `scots-pine-v2-edge.gltf`, then `silver-birch-v2-edge.gltf`. Tree-gen
handles generation, packaging and asset validation; report asset defects there.
The separate draft `.impostor` files are inputs to the world-object task below,
not part of the generic mesh LOD loader. The old fixed-frame preview GLB is not
an engine impostor.

The selected naming convention stays unsuffixed even if an OR build skips LOD0.
[Source inspection and compatibility decision](../../../../tree-gen/docs/orts-gltf-lod.md)
are maintained in tree-gen; do not copy that omission into TSRE.

## Acceptance

Compare all layouts as ordinary Static objects: selected geometry should agree
at equal thresholds, with no pivot jump or premature disappearance. Exercise
FOV/scale changes, malformed references and companion gaps; keep ordinary glTF
and MSTS shapes working. Check real PBR, cutout mips, normals, shadows and texture
sharing on supported backends. Unsupported optional diffuse transmission must
retain the core-PBR fallback until [task 36](../renderer/36-foliage-backlighting.md).

Capture pine/birch and profile one tree, 100 and 1,000 instances on target
hardware. Record selected levels, draw calls, triangles, CPU submission/GPU time
and resource memory. Those counts are experiment sizes, not performance promises.

Follow-up: [Forest/Static and impostors](../world/vegetation-objects.md).
Asset plans and export tooling are outside this TSRE task.
