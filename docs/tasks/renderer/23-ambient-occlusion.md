# Task 23 - Ambient Occlusion

## Objective

Screen-space ambient occlusion on the QRhi renderer: ambient and
environment light darken in corners, under objects and where surfaces meet,
while sun light, lamps (task 21), fog and sky stay as they are. One
pipeline for old and new hardware, with a quality setting; no extra
geometry pass.

## Design

- The lit shaders (legacy shading, terrain, PBR) write a second colour
  output under `TSRE_RHI`: the share of their final colour that ambient and
  environment light make, after fog (`ambientOut`). The legacy shading's
  share is exact (texture times ambient light); PBR takes the display-space
  difference its environment light makes. Water and unlit draws give none.
  The view gets that second attachment only while ambient occlusion is on.
- After the opaque passes of the main view (before the first blended,
  overlay, water, transmission or UI pass, or at the end of the frame),
  `RhiRenderer::applyAmbientOcclusion` runs three full-screen passes:
  1. GTAO (ground-truth ambient occlusion, Jimenez et al. 2016) at a
     reduced resolution from the view's depth: positions are rebuilt at
     depth texel centres from the scene band's projection and slice of the
     depth range (distant terrain and sky get none), normals from the nearer
     neighbours, then per pixel a few slice directions turned by interleaved
     gradient noise, horizons searched on both sides within 2 m, and the
     cosine-weighted visible arc integrated. Occlusion fades out between 150
     and 300 m.
  2. A 5 x 5 blur weighted by depth similarity.
  3. A composite that subtracts `ambient share x (1 - visibility)` from the
     view with a reverse-subtract blend.
  Blended surfaces, water, transmission and overlays draw afterwards,
  unoccluded. Secondary views (environment faces, water reflection), the
  selection pass and the Shape Viewer (no scene band) get none.
- A depth pre-pass was considered: it gives the same occlusion but draws the
  opaque geometry twice, which costs most on the older hardware TSRE users
  have, so it was left out.

## Setting

`core.rendering.ambientOcclusion`: Off (default), Low, Medium, High.

| Quality | Resolution | Slices x steps per side | Blur |
|---|---|---|---|
| Low | quarter | 2 x 4 | yes |
| Medium | half | 3 x 6 | yes |
| High | full | 4 x 8 | yes |

Cost on llvmpipe (EUROPE1, 960 x 540, frame time with shadows, which the
geometry dominates there): Low +4 %, Medium +7 %, High +9 %.

## Debugging

`TSRE_AO_DEBUG=1` shows the visibility term in place of the view;
`TSRE_AO_DEBUG=normal` and `TSRE_AO_DEBUG=depth` show the rebuilt normals and
the read-back distance (10 m bands in red, Low quality only: the blur
rewrites the channels).

## Verification

- `rhi-shaders`: the three passes bake for SPIR-V and GLSL 330.
- EUROPE1 captures: Off identical to the renderer before this task; Medium
  and High darken canopies, trunks and house bases; QRhi on OpenGL matches
  Vulkan (RMSE 1.6); no Vulkan validation messages. A first version showed
  stripes on sloping ground: positions were rebuilt at the tap instead of
  the depth texel's centre, so slopes turned into steps.

## Later

- Depth-aware upsampling in the composite (bilinear now, slight halos at
  silhouettes at Low and Medium).
- Occlusion in the Shape Viewer and in the water reflection.
- Tuning with HDR and tone mapping, which change how strong it reads.
