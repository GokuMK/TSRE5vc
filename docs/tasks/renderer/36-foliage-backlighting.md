# Task 36 - glTF diffuse transmission for foliage

Status: glTF materials and the impostors' `BacklightTexture` implemented,
merged from `feature/gltf-lod`; crowns seen from below stay dark (see below).
Updated 2026-10-20.
Asset bindings are already supplied by tree-gen.

## Implementation (feature/gltf-lod, 2026-10-19)

- Parsed: both factors and both texture infos (texture coordinates,
  KHR_texture_transform and sampler wrap as for the other maps) into
  `RenderItem::Pbr` (`diffuseTransmission`, `diffuseTransmissionColor`,
  `MAP_DIFFUSE_TRANSMISSION` and `_COLOR`), on OpenGL units 21 and 20 (where
  the driver has them) and QRhi bindings 21 and 33.
- Shading (`PbrShading.glsl`, both renderers): amount = factor x the map's
  alpha (linear); colour = factor x the map's colour (sRGB), not multiplied
  by the base colour. The front diffuse keeps 1 - amount; the far side gets
  Lambert on -N from the sun (with its shadow), the lamps (QRhi) and the
  environment's irradiance from -N, each less the Fresnel of the light
  mirrored to the viewer's side (as the Khronos sample viewer). Metals get
  none; specular transmission takes its share first. No refraction, scene
  copy, blending or emission. Two-sided surfaces turn their normal to the
  viewer, so the far side is a light behind the leaf.
- Mipmaps: shape textures are uploaded without them (only terrain textures
  get mipmaps), so the amount is sampled from the full texture; nothing
  rescales its alpha.
- TREES captures: toward a low sun, the outer leaves the sun passes through
  turn green instead of black and the inner ones stay in their shadow; with
  the sun behind the camera nothing changes (no glow). About 1-2 ms more
  looking at the sun, none measurable otherwise.

Open: night and overcast captures; the texture traffic; tuning against real
leaves. The impostors' `BacklightTexture` drives their diffuse transmission
(amount and colour, `BacklightStrength` the factor).

Crowns stay dark seen from below at noon. The undersides do receive the
transmitted sun, but the shadow map holds leaves opaque: almost every leaf
seen from below lies in the shadow of others, so it gets none, where real
light passes several layers (without the shadow on it the whole crown turns
an even green, losing its depth). The fixtures also transmit little: the
black locust's leaves average sRGB 73 95 38 (about 0.1 linear) and pass
that colour at 0.55 (alpha 140), about 5 %. To try: leaves in the shadow
map passing a share of the light for transmission, a brighter transmitted
colour in the assets (tree-gen). Since [task 37](37-vegetation-shadows.md)
step 5 (QRhi): vegetation casts into its own shadow maps, which trees read
at 30%, so leaves in their own crown's shade keep most of the sun.

## Engine scope

Implement `KHR_materials_diffuse_transmission` (Khronos Release Candidate as
checked on 2026-10-19), rather than the abandoned custom TSRE extras proposal.
This is diffuse leaf/paper transmission, separate from the existing refractive
`KHR_materials_transmission` implementation.

1. Parse diffuseTransmissionFactor, diffuseTransmissionTexture,
   diffuseTransmissionColorFactor and diffuseTransmissionColorTexture with
   their specified defaults and texture coordinates. Carry them through shared
   material state, texture caching and supported OpenGL/QRhi paths.
2. Sample the amount from **linear A**, and tint from **sRGB RGB**. Fixtures use
   one RGBA texture for both infos: copied foliage colour plus the original mask
   in alpha. Do not read R as amount or multiply transmitted tint by base colour
   again. Keep cutout coverage on the existing base-colour alpha texture.
   Do not apply cutout alpha-coverage correction to transmission-amount mips.
3. Evaluate rear-hemisphere diffuse transmission with the standard diffuse
   reflection/transmission split, preserving specular/Fresnel and metal behavior.
   Keep cutout depth writes, correct two-sided normals and shadow visibility.
   Account for indirect light deliberately. Do not activate refraction, scene
   copies, blending or emission for this extension alone.
4. Handle zero factor, absent optional textures and unsupported bindings with
   defined fallback. Verify texture transforms/coordinates against the engine's
   existing material machinery; fixtures currently use TEXCOORD_0.

## Inputs and acceptance

[Asset contract and storage](../../../../tree-gen/docs/foliage-backlighting.md)
and [fixtures](../../../../tree-gen/docs/runtime-catalogue.md) live in tree-gen.
All seven species bind the extension on LOD0/1 foliage and LOD2 clusters.
Their STF files include BacklightTexture and BacklightStrength for matching
whole-tree RGBA atlases; map these to the same diffuse-transmission material
semantics. Existing base colours, cutout alpha and normals are unchanged.
The full chain is supplied; this task does not generate assets.

Use front/back-light, two-sided, shadow, night and factor-zero captures. Check
bark receives no added transmission, specular stays consistent, and there is no
unlit glow. Measure added GPU time, memory and texture traffic. Use the supplied
full LOD chain for implementation tests, including impostor
frame selection and its capture-basis normals. Gate normal use on engine
appearance checks across transitions; the presence of maps does not establish
lighting/shadow continuity by itself.

Reference: [Khronos specification](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_diffuse_transmission/README.md).
Related engine tasks: [native LODs](../shapes/10-gltf-native-lod.md),
[Forest/Static impostors](../world/vegetation-objects.md).
