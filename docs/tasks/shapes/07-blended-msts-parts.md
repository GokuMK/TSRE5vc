# Task 07 - Blended MSTS Parts With Opaque Textures

## Problem

An MSTS shape part takes its surface from the shader name: `TexDiff`
parts are opaque or alpha-tested (by their other attributes), and the rest
(usually `BlendATexDiff`) are blended. Much content uses the blended shader
for parts whose texture has no transparency at all, rolling stock most of
all. Such parts are drawn in the blended pass, after the opaque scene and
sorted back to front. They then sort against each other where they need
not, and the QRhi renderer's ambient occlusion (applied after the opaque
passes) leaves them out.

## Change

- `TextureAlpha` classifies a texture's level 0 when it loads, on the
  loader thread:
  - **opaque**: all alpha 250 or more;
  - **binary**: alpha only 5 or less, or 250 or more;
  - **partial**: anything else.
- Each format is read cheaply:
  - DXT1 blocks: block headers only. DXT1 without alpha is opaque.
  - DXT3: its 4-bit alpha.
  - DXT5: the alpha endpoints and indices.
  - Uncompressed textures: the alpha bytes.
- Every loader calls `Texture::classifyAlpha` before it publishes the
  texture (ACE, legacy ACE, DDS, images).
- `RenderItem::drawSurface` gives the surface a packet draws with. A
  blended, textured, non-PBR packet:
  - draws as opaque when its texture is opaque;
  - draws as alpha-tested when the texture alpha is binary and the setting
    allows it.
- It reads the class through the packet's TexLib id. `SFileComplexGL`, which
  resolves textures itself, copies it into `material.textureAlpha`. Until a
  texture has loaded, its part stays blended.
- Both renderers use `drawSurface` when they choose a pass.
- Setting `core.rendering.blendedParts`:
  - 0: as marked in the shape;
  - 1: opaque when the texture is opaque (default);
  - 2: also alpha-tested when the texture alpha is binary.

The passes differ only in order: blending is the same global state in every
pass, and MSTS blended parts already wrote depth. So mode 1 changes the
image only where the blended order was wrong. Mode 2 draws binary-alpha
parts before sorted ones, which can change their filtered edges against what
lies behind.

## Results (2026-10-07)

- EUROPE1 and BNSF_SCENIC (parity views): every blended part's texture has
  partial alpha, so the images are unchanged in every mode.
- MSTS Class 50 consist, SD40 and 310 tender (Shape Viewer): blended parts'
  texture lookups over the captured frames were 2514 opaque, 209 binary,
  576 partial and 88 not yet loaded. The images change by up to 0.24 RMSE
  (draw order).
- Classes of hand-made texels and DXT1, DXT3 and DXT5 blocks are checked
  by `tsre_ace_tool --self-test`.

## Later

- The per-vertex alpha mode written by `SFileLegacy` still encodes
  "blended" (alpha test at `GLUU::alphaTest`). Alpha-tested parts in mode 2
  keep that threshold.
- Partial-alpha textures where only a few texels are partial (soft edges
  of an otherwise opaque texture) stay blended.
