# Forest/Static shape and impostor integration

Status: engine implementation draft, not started. Updated 2026-10-19.
Prerequisite: [native glTF LOD loading](../shapes/10-gltf-native-lod.md).

## Engine changes

1. Extend Forest with optional `Shape ( "<id>.gltf" )` and
   `Impostor ( "<id>.impostor" )`; extend Static with the same optional Impostor
   reference beside its ordinary FileName. Reuse shared shape/resources and
   generic mesh LOD selection. No separate vegetation world-object class.
2. Retain standard Forest fields and its 2D fallback; render only one path.
   Use authored metre dimensions plus ScaleRange for modern shapes; check
   fallback TreeSize agrees. Define missing-shape/impostor fallback, and preserve
   new tokens through save, load and undo. Verify actual legacy-reader behavior.
3. Parse the supplied versioned STF through the common parser. Resolve texture
   filenames through engine settings. Validate finite bounds/extents, unit frame
   directions, UV ranges, version and required resources; normal maps are optional.
4. Render the cutout camera-facing quad using selected-frame extents and UVs.
   Select the frame by tree-local camera direction, including instance rotation.
   Decode normals in the selected capture basis before the tree's normal
   transform; never rotate them with the facing quad. Include quad corners in
   culling bounds. Define picking and shadow-pass frame/visibility behavior.
5. Implement Append at the last mesh's end boundary and ReplaceLast at the final
   fallback mesh's start boundary. Current fixtures use Append. Keep object
   visibility separate from mesh disappearance; use existing hysteresis policy.

## Inputs, not engine deliverables

Tree-gen supplies [both LOD layouts and STF files](../../../../tree-gen/docs/runtime-catalogue.md).
The [STF contract](../../../../tree-gen/docs/impostor-stf.md) is the format reference;
the [full vegetation plan](../../../../tree-gen/docs/tasks/vegetation-runtime.md)
and generation/export work remain there. This task does not generate trees,
atlases, variants, glTF packages or MSTS shapes.

## Acceptance

Test pine and birch as Static and Forest instances, across view rotations,
elevation bands and LOD boundaries. Check resource sharing, selection, shadows,
alpha mip coverage and atlas bleed in supported backends. Exercise missing
resources, malformed STF and mesh-only objects. Compare fixed-path captures and
CPU/GPU time, draw calls, triangles and memory with/without impostors.

[Diffuse transmission](../renderer/35-foliage-backlighting.md) is separate.
STF BacklightTexture/BacklightStrength now reference a supplied whole-tree
RGBA atlas (sRGB tint in RGB, linear amount in A). Route it through that material
feature when supported; an absent/unsupported optional binding must still work.
