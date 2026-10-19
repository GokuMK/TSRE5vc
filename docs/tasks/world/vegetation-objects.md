# Forest/Static shape and impostor integration

Status: impostors of glTF shapes drawn (steps 3 to 5, simplified) on
`feature/gltf-lod`; Forest and the world-file fields not started. Updated
2026-10-19.
Prerequisite: [native glTF LOD loading](../shapes/10-gltf-native-lod.md).

## Implementation (feature/gltf-lod, 2026-10-19)

- Found beside the shape, not referenced: "tree.gltf" has "tree.impostor"
  (the stem and a lowercase suffix, by TSRE's file name rules), loaded with
  the shape on the worker. No world-file field yet, so MSTS and Open Rails
  readers see no new token; a Static/Forest `Impostor` field can come when
  an object needs another one.
- `Impostor` reads version 1 through the common SIMIS reader (`EnvText`)
  and validates it: unit directions, cells inside the atlas, extents and
  bounds; another version or invalid values leave the meshes alone, with a
  warning. Textures are file names in the route's textures.
- Each frame is a quad in its capture plane (pivot + R x + U y, UVs with v
  down). Its tangent frame is the capture basis (normal D, tangent R,
  bitangent U), so the PBR program's normal mapping decodes the baked
  normals; colour with its cutout, roughness, specular, metallic 0, and the
  `BacklightTexture` as diffuse transmission ([task 36](../renderer/36-foliage-backlighting.md)).
- The impostor is a level of the shape (`ShapeLod`): after the last mesh
  level from its end coverage (Append; a shape without levels: its own
  MSFT_screencoverage, else 0.001), or in its place (ReplaceLast). At that
  level only the quad of the frame captured nearest to the camera's
  direction in the shape's space draws (instance rotation included); each
  frame's packet is shared, so instances showing it stay instanced.
- Quads are not turned to the camera: a frame is within about 15 degrees of
  the view; the frame switches as the view goes round. Shadows: the quad of
  the frame seen by the camera. The selection box stays the meshes'.
- Checking: `TSRE_SHAPE_COVERAGE_SCALE` scales the glTF coverages; the
  fixtures' 0.001 starts impostors some 17 km away for an 18 m tree. TREES_EXT
  with scale 60 and 1000: impostors in place, size and outline of the meshes,
  not mirrored, casting shadows; fuller than level 2's clusters. The near
  spruce view: 21.7 ms with meshes, 15.6 ms all impostors.
- Tests: suite `gltf-lod` (41): Append after internal levels, the frame by
  direction, cutout and maps, ReplaceLast, a shape without levels, another
  version; the TREES_EXT spruce with its impostor on a worker.

Open: Forest; world-file fields; atlas bleeding at coarse mipmaps (no cell
inset yet); the shadow pass's frame; crossfades; impostors of MSTS shapes.

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

[Diffuse transmission](../renderer/36-foliage-backlighting.md) is separate.
STF BacklightTexture/BacklightStrength now reference a supplied whole-tree
RGBA atlas (sRGB tint in RGB, linear amount in A). Route it through that material
feature when supported; an absent/unsupported optional binding must still work.
