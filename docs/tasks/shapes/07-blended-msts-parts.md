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

## Drawn in two (2026-10-22)

The PKP ST44 (`TRAINS/TRAINSET/PKP_ST44-OR/PKP_ST44-002.s`) marks almost
every part `BlendATexDiff`, its windows and its cab interior too, and its
textures have partial alpha. Blended parts were sorted by their matrix's
origin and wrote depth. The interior hangs on `Inside`, 3.1 m above the
shape's origin, the windows mostly on `Main` (at rail level). Seen from
above, the windows were the farther and drew first, and the interior behind
them failed the depth test; from below the order turned and it showed.

Now such packets are drawn in two, as Open Rails' `SceneryMaterial` does
(both renderers):

- their opaque texels (alpha 250 of 255 or more, Open Rails' threshold)
  at the end of the alpha-tested pass, grouped by texture, depth written;
- the other texels in the blended pass, back to front as before, without
  depth writes.

So glass never hides what lies behind it, and translucent parts sort against
each other only for their blending order. `RenderItem::blendsInTwoParts`
picks the packets: textured, blended (by `drawSurface`), of full opacity, not
PBR, water or decals. The shaders' `blendedTexels` (`StandardFog`,
`StandardFast`) picks the texels. The selection pass draws them whole. A
call that draws the blended pass without the alpha-tested one before it
draws the opaque texels at its start (`QueueRenderer::opaqueTexelsIn`; no
caller does today). `TSRE_BLENDED_IN_TWO=0` draws them whole everywhere, as
before.

As the opaque texels are in the depth buffer before the ambient occlusion,
the clouds' composite and the sky's fog (QRhi), blended parts now take and
cast ambient occlusion: rolling stock as its `TexDiff` parts, the ground and
track under a train, blended track shapes along their sleepers.

The translucent texels are blended down to alpha 4 of 255 (Open Rails: 10).
Before, they kept their vertex alpha's threshold (`GLUU::alphaTest`, 0.3),
needed while blended parts wrote depth; it hid faint glass. The shaders
take the lower of the two for `blendedTexels` 2 only; the opaque texels,
parts drawn whole and the shadow passes keep theirs. On TRAINS' stock it
shows the soft edges of rails and of the shadow planes painted under cars
(the Eanos' is 82 % texels under 0.3); the coaches' windows checked were
above 0.3 already.

Results (`tests/renderer/shape-viewer-blended.json`, Shape Viewer views
with the new `pitch`, `fov` and `look` keys):

- ST44 from above: the cab interior shows through the windows, in QRhi and
  OpenGL; from below and level, unchanged but for a few edge pixels.
- With the opaque texels in the blended pass (the first version), EUROPE1's
  parity views (QRhi and OpenGL) and Prudnik's station view changed in
  single pixels at most, at the opaque threshold. Moved to the alpha-tested
  pass, with the profile's ambient occlusion (low): the ST44's body takes it
  (under the sunshade, the window recesses, the grille); Prudnik's and
  EUROPE1's blended track along the sleepers (3% and up to 6.5% of pixels,
  1-2 levels). OpenGL, without ambient occlusion: unchanged by the move.
- Cost, Prudnik's station view at 1280x800: 5369 draws instead of 5059; GPU
  about 3.7 ms more and CPU 1-1.7 ms (two pairs of runs, either place of the
  opaque texels).

## Later

- The sky enclosure (renderer task 27) could take the opaque texels of
  blended parts, and alpha-tested parts from above too, as roofs: the CMK
  station hall's thin roofs are alpha-tested, kept out of the downward map
  to keep tree canopies out. Since renderer task 37 vegetation is flagged
  and kept out of the enclosure anyway; MSTS trees without the cruciform
  light materials would count as roofs.

- The per-vertex alpha mode written by `SFileLegacy` still encodes
  "blended" (alpha test at `GLUU::alphaTest`). Alpha-tested parts in mode 2
  keep that threshold. It dates from drawing blending and alpha testing in
  one pass; the material's surface class (`RenderItem::drawSurface`) and
  `blendedTexels` now say the same per draw, so it could go.
- Partial-alpha textures where only a few texels are partial (soft edges
  of an otherwise opaque texture) stay blended.
