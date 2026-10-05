# Task 06 - glTF PBR Materials

## Objective

Draw glTF/GLB models with their metallic-roughness materials, reflecting the
environment map (renderer task 17), in the existing OpenGL 3.3 forward
renderer. Where full support needs a different renderer, use a documented
simplification.

## Implementation

- `GltfShape` reads the whole core material: base colour, metallic and
  roughness (factors and texture), normal map with scale, occlusion with
  strength, emissive (factor and texture), alpha mode and cutoff,
  `doubleSided`, each texture's coordinate set, and sampler wrap modes.
  Extensions: `KHR_materials_emissive_strength`, `KHR_materials_unlit`.
- Vertices use the `RenderItem::PBR` layout (19 floats): position, normal,
  texture coordinates, alpha, tangent with handedness, second texture
  coordinates and colour (`COLOR_0`). Missing tangents are generated per
  source vertex from the texture coordinate gradients; missing normals were
  already generated.
- Packets carry a `RenderItem::Pbr` block. The renderer draws them with the
  `StandardFogPbr` program (`TSRE_PBR`, `PbrShading.glsl`) and binds the
  metallic-roughness, normal, occlusion and emissive maps on units 11-14.
  The base colour texture stays the packet's texture (unit 0), so texture
  grouping is unchanged.
- `RenderItem::Material::doubleSided` turns off back-face culling for a
  packet. Wrap modes other than repeat use GL 3.3 sampler objects on the
  units of those maps only; TexLib textures themselves are unchanged.
- The Shape Viewer fits the camera to the shape's bounding sphere and scales
  the near plane with it, so small and large models are framed whole.

### Shading

- Linear lighting: base and emissive colours are decoded from sRGB, the
  result is encoded again; no tone mapping, so bright light clips.
- Sun: GGX specular with Smith visibility and Lambert diffuse, using the
  scene's shadows. Its strength matches the legacy diffuse light, so a white
  matte surface facing the sun is as bright as with the legacy shading.
- Image-based light from the environment cube: reflections blurred by
  roughness through the cube's mipmaps, diffuse light from its most blurred
  levels, and the split-sum BRDF as an analytic fit instead of a lookup
  texture. Diffuse light is scaled so open sky gives the legacy ambient
  light. Without a cube (Route Editor cube off), a sky-to-ground gradient
  stands in.
- The Route Editor cube is not sampled while its own faces are drawn; PBR
  objects in the cube use the gradient.

## Simplifications

| Feature | Status | Full support needs |
| --- | --- | --- |
| Reflection blur | Box-filtered mipmaps, not GGX-prefiltered | Prefiltering pass (possible in GL 3.3) |
| Diffuse environment light | Most blurred mip, not irradiance | Spherical harmonics or an irradiance cube (possible in GL 3.3) |
| Emission | Colours the surface only | Light from emitters: many-light or deferred renderer |
| `KHR_lights_punctual` | Not supported | Many lights per pixel: clustered forward or deferred renderer |
| High dynamic range | Clipped, no bloom | HDR target and tone mapping / bloom pass |
| `BLEND` | Sorted per packet (back to front by origin) | Order-independent transparency |
| Transmission, volume (`KHR_materials_transmission`, `_volume`) | Not supported | Scene colour copy behind transparent objects: new renderer |
| Clearcoat, sheen, specular, IOR, iridescence, anisotropy | Not supported | Extra lobes in the forward shader (possible in GL 3.3) |
| `KHR_texture_transform` | Not supported | Per-map UV transform uniforms (possible in GL 3.3) |
| Sampler filters | TexLib filtering (trilinear); wrap modes honoured | Min/mag filters per sampler (possible in GL 3.3) |
| Animation, skinning, morph targets | Not drawn | Animation runtime |

## Verification

- `gltf-pbr-gl` suite (needs `TSRE_GLTF_SAMPLE_ASSETS`): material factors,
  wrap modes, `doubleSided`, second texture coordinates, emissive strength,
  unlit, alpha modes, vertex colours, file and generated tangents.
- `tests/renderer/gltf-viewer-views.json`: 19 Khronos samples in the Shape
  Viewer, viewed from the front (`yaw`). TextureSettingsTest passes every
  row, TextureCoordinateTest and VertexColorTest show their pass marks,
  NormalTangentMirrorTest lights every sphere from one side, and
  MetalRoughSpheres matches the reference layout.
- MSTS content does not use the PBR program; route captures are unchanged.
