# Task 10 - Native glTF mesh LODs

Status: TSRE implementation draft, not started. Updated 2026-10-19.
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
retain the core-PBR fallback until [task 35](../renderer/35-foliage-backlighting.md).

Capture pine/birch and profile one tree, 100 and 1,000 instances on target
hardware. Record selected levels, draw calls, triangles, CPU submission/GPU time
and resource memory. Those counts are experiment sizes, not performance promises.

Follow-up: [Forest/Static and impostors](../world/vegetation-objects.md).
Asset plans and export tooling are outside this TSRE task.
