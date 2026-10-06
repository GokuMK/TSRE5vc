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
  Extensions: `KHR_materials_emissive_strength`, `KHR_materials_unlit`,
  `KHR_texture_transform` (offset, rotation, scale and coordinate set per
  texture), `KHR_materials_clearcoat` (strength, roughness and normal,
  each with its map), `KHR_materials_specular` (strength and colour, each
  with its map), `KHR_materials_ior`, `KHR_materials_transmission` and
  `KHR_materials_volume` (thickness with its map, attenuation).
- Vertices use the `RenderItem::PBR` layout (19 floats): position, normal,
  texture coordinates, alpha, tangent with handedness, second texture
  coordinates and colour (`COLOR_0`). Missing tangents are generated per
  source vertex from the texture coordinate gradients; missing normals were
  already generated.
- Packets carry a `RenderItem::Pbr` block. The renderer draws them with the
  `StandardFogPbr` program (`TSRE_PBR`, `PbrShading.glsl`) and binds the
  metallic-roughness, normal, occlusion and emissive maps on units 11-14,
  the clearcoat maps on units 4-6 and the specular maps on units 7 and 15
  (used only by terrain and water programs otherwise): all 16 units OpenGL
  3.3 guarantees are taken.
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
- Image-based light from the environment cube, prefiltered with the GGX
  lobe per roughness (32 importance samples, each read from the source mip
  matching its footprint, averaged in linear colour; levels down to 4 x 4
  texels). The Route Editor prefilters the faces it rendered that frame;
  the Shape Viewer's warehouse is prefiltered once. Diffuse light uses the
  roughest level, scaled so open sky gives the legacy ambient light; the
  split-sum BRDF is an analytic fit instead of a lookup texture. Without a
  cube (Route Editor cube off), the sky over dark ground stands in, with a
  horizon as sharp as the roughness allows: smooth surfaces show the horizon
  line, rough ones a soft gradient.
- Clearcoat: a dielectric GGX layer (F0 0.04) over the base, which receives
  the light the layer transmits.
- Specular and IOR: the dielectric F0 is ((ior - 1) / (ior + 1))^2 times
  the specular colour (at most 1), and F0 and F90 are scaled by the
  specular strength; metals keep their base colour as F0.
- Transmission and volume: transmissive packets draw in `PASS_TRANSMISSION`,
  after water. When that pass starts the renderer blits the frame drawn so
  far (which also resolves multisampling) into a mipmapped copy on unit 1,
  unused by the PBR program otherwise. The shader refracts the view ray
  (IOR), follows it through the thickness (scaled by the model; zero is
  thin-walled), projects the exit point to the screen and reads the copy
  there, blurred by roughness (more for a higher IOR, as the Khronos sample
  viewer does) and clamped to the edge pixels off screen. A volume absorbs
  along the path (Beer-Lambert from the attenuation colour and distance).
  The light from behind replaces the diffuse part of non-metal areas,
  tinted by the base colour, under the clearcoat. Cube faces and the water
  reflection take no copy; their transmissive surfaces refract the
  environment. The transmission and thickness maps use units 16 and 17
  where the driver has them (current drivers have 32); with only the 16
  OpenGL 3.3 guarantees, their factors apply alone.
- The Route Editor cube is not sampled while its own faces are drawn; PBR
  objects in the cube use the gradient.

## Simplifications

| Feature | Status | Full support needs |
| --- | --- | --- |
| Diffuse environment light | Roughest prefiltered level, not cosine irradiance | Spherical harmonics or an irradiance cube (possible in GL 3.3) |
| Emission | Colours the surface only | Light from emitters: many-light or deferred renderer |
| `KHR_lights_punctual` | Not supported | Many lights per pixel: clustered forward or deferred renderer |
| High dynamic range | Clipped, no bloom | HDR target and tone mapping / bloom pass |
| `BLEND` | Sorted per packet (back to front by origin), without depth writes, so a shell drawn first (glass over lights) does not hide what lies behind it | Order-independent transparency |
| Transmission, volume (`KHR_materials_transmission`, `_volume`) | Screen-space: a copy of the frame drawn before the transmission pass, read along the refracted ray | Glass behind glass and other blended surfaces behind it are not seen; lookups off screen read the edge pixels |
| Sheen, iridescence, anisotropy | Not supported | Extra lobes in the forward shader (possible in GL 3.3); no texture units are left for their maps |
| Sampler filters | TexLib filtering (trilinear); wrap modes honoured | Min/mag filters per sampler (possible in GL 3.3) |
| Animation, skinning, morph targets | Not drawn | Animation runtime |

## Verification

- `gltf-pbr-gl` suite (needs `TSRE_GLTF_SAMPLE_ASSETS`): material factors,
  wrap modes, `doubleSided`, second texture coordinates, emissive strength,
  unlit, alpha modes, vertex colours, file and generated tangents, texture
  transforms, clearcoat, specular, IOR, transmission and volume, and the
  routing of transmissive packets to their pass. `environment-map-gl` checks the prefiltered
  levels.
- `tests/renderer/gltf-viewer-views.json`: 32 Khronos samples in the Shape
  Viewer, viewed from the front (`yaw`). TextureSettingsTest passes every
  row, TextureCoordinateTest and VertexColorTest show their pass marks,
  NormalTangentMirrorTest lights every sphere from one side, and
  MetalRoughSpheres matches the reference layout. TextureTransformTest
  points every arrow at its green marker, and ClearCoatTest shows the
  coated column with the layer's sharp reflection. SpecularTest brightens
  along each row and tints the yellow rows, and IORTestGrid's IOR 1.0 row
  reflects nothing.
  CommercialRefrigerator shows the bottles through its glass door,
  TransmissionRoughnessTest blurs the backdrop more with roughness and not
  at all for IOR 1.0, and CompareVolume tints the thick bowl green. Small
  backdrops in the 800 x 500 captures blur to grey; at 1920 x 1200 their
  patterns show through the glass.
- MSTS content does not use the PBR program; route captures are unchanged.
