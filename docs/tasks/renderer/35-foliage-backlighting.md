# Task 35 - glTF diffuse transmission for foliage

Status: TSRE implementation draft, not started. Updated 2026-10-19.
Asset bindings are already supplied by tree-gen.

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
