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
     gradient noise, horizons searched on both sides within 2 m (at most
     15 % of the view's height on screen; in a 3D cab a smaller, steadier
     radius near the camera, below), and the cosine-weighted visible arc
     integrated. Occlusion fades out between 150
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
| High | full | 3 x 6 (4 x 8 until 2026-10-22) | yes |

Cost on llvmpipe (EUROPE1, 960 x 540, frame time with shadows, which the
geometry dominates there): Low +4 %, Medium +7 %, High +9 %.

Steam Deck (2026-10-22, QRhi Vulkan; EUROPE1 parity view `down`, ground
over the whole view; frame time over Off, medians of two runs; the CPU is
the same at every level):

| Quality | 1280 x 800 | 2123 x 1948 |
|---|---|---|
| Low | +0.6 ms | +1 ms |
| Medium | +1.6 ms | +7.6 ms |
| High (4 x 8) | +9.8 ms | +39 ms |
| High (3 x 6, optimised loop) | +6.9 ms | +22-25 ms |

(Off itself varies by about 2.5 ms between runs at the larger size.) Since
2026-10-22 blended MSTS parts are in the depth the occlusion reads ([shapes
task 07](../shapes/07-blended-msts-parts.md)); the pass costs the same.

### Optimising High (2026-10-22)

Where High's 4 x 8 went at 2123 x 1948 (by switching parts off): the depth
reads for the position and normal and the composite about 8 ms, the 5 x 5
blur about 6 ms, the taps about 30 ms.

- Shrinking the largest screen radius from 0.15 to 0.02 of the view's
  height changed nothing: the taps are not limited by the texture cache, so
  a depth mip chain (XeGTAO) would not help.
- The occlusion pass now reads view texels by integer pixel and takes the
  depth-to-distance and unprojection terms as constants (no divisions per
  tap), and the slices and steps are compiled into the shader per preset, so
  the loops unroll (a tap outside the view is masked, not a break). The
  occlusion is unchanged (0.1 % of pixels by rounding); High 4 x 8 went from
  +43.5 to +37 ms.
- Fewer taps, compared on the visibility term (`TSRE_AO_DEBUG=1`) with
  full-resolution 4 x 8 (mean difference in 0-255 levels, EUROPE1 three
  views / Prudnik's station):

  | Variant | Difference | Frame over Off |
  |---|---|---|
  | full, 4 x 6 | 0.3-0.6 / 0.8 | +31.5 ms |
  | full, 3 x 6 | 0.4-0.7 / 1.0 | +24.4 ms |
  | full, 3 x 4 | 0.7-1.3 / 1.9 | +22.7 ms |
  | half, 4 x 8 | 0.6-1.4 / 2.2 | +15 ms |
  | Medium (half, 3 x 6) | 0.7-1.5 / 2.4 | +7.6 ms |

  Half resolution loses what High is for: the thin lines of occlusion along
  rails and the detail in grass and foliage; 3 x 6 at full resolution keeps
  them, its difference a fine grain the blur evens out. High is now that.

Later: a depth-aware upsample in the composite for Low and Medium (tried: it
keeps their occlusion from bleeding across silhouettes, cost not measured);
a separable blur (the 5 x 5 one costs about 6 ms at full resolution).

### Previews (2026-10-23)

The Shape Viewer, the Consist Editor's views and the route editor's shape
window (`ShapeViewerGLWidget`) take the setting too, but their light hid
it: the windows set a level sun from behind the camera (0.7) with the
default ambient light (0.3), about 13 % of a sunlit side's light in linear
terms, and the occlusion takes only the ambient share; the underframes,
where most of it lies, are dark anyway. They now draw with a studio light
of their own (`ShapeViewerGLWidget::Light`), set for each of their frames
and restored after it, so the route's sun stays for the route editor: the
sun above, behind the camera and to one side (towards it (-0.6, 0.7, 0.4)
in the view's frame, the camera looking along +x), grey 0.6, the ambient
light 0.5. Of four lightings tried (`tests/renderer/preview-lighting.json`;
the capture's per-item `light`), this keeps the sun's modelling and
doubles the occlusion's effect (ST44, three-quarter view, High: 0.9 to 1.5
levels on average). It stays subtle: the previews have no ground, the
strongest occluder in a route.

### Echo rings in a 3D cab (user, 2026-10-23)

- In a 3D cab (feature/cab3d) small parts cast occlusion as several
  shifted copies: the wheel's rim echoed in three or four rings on the
  desk behind it, knobs had double halos (`TSRE_AO_DEBUG`, the cab's shape
  placed in CMK, the camera at its head position, Medium).
- Causes: everything in a cab is within the 2 m radius, which reaches the
  screen limit (15 % of the view's height, about 290 pixels at 2123 x
  1948), so the 6 steps a side lie 30 to 80 pixels apart; and one noise
  value per pixel turned the slices and shifted the steps, so the pixels
  whose steps caught a thin part lay on rings around it (in a 5 x 5 blur
  window the pairs of the two filled a fifth of their combinations).
- Now the steps take the same interleaved gradient noise on the
  transposed, shifted pixel (uncorrelated with the slices' noise, 84 % of
  the combinations filled): the rings are gone, a fine faint weave left
  in the occlusion term; a hashed white noise instead gave no weave but a
  coarser grain. Outdoors only the grain changed (TRAINS at most 9 of 255,
  0.07 % of pixels over 2; Prudnik's station 0.56 %).
- Pixels past the fade's end (300 m) return at once: their result was 1
  after the full march.
- Not done (user: try this first): a smaller radius close to the camera
  (done 2026-11-04, below), and the thin-occluder rule of XeGTAO (strong
  shadows in the cab look good to the user).

### Radius in a 3D cab (user, 2026-11-04)

- In a cab the halos changed size as the head moved; the still look was
  acceptable. The 2 m radius is cut to 15 % of the view's height on
  screen, so near the camera it follows the distance (0.16 m a metre at a
  55 degree view): from 1 m to 0.5 m it halved, and parts at 30 cm had a
  third of the halo of parts at 1 m.
- Now, while the camera rides a 3D cab (`Renderer::setInCab`, set by the
  scene paint from the ridden consist's `cab3DVehicleRidden()`), the radius
  near the camera is 0.15 m at 1 m and grows as the distance to the power
  0.3 (0.10 m at 0.3 m, 0.12 m at 0.5 m, 0.19 m at 3 m); between 3 and 6 m
  it joins the world's in log space (what the windows show). The screen
  limit in the cab is 35 % of the view's height, so the cab's radius holds
  down to about 0.3 m. From 1 m to 0.5 m the radius now changes by 19 %,
  from 30 cm to 1 m 1.4 times (3.3 before). Outside a cab nothing changed:
  the world's radius is the same formula (the user's Prudnik bench, at the
  startup camera `-5373,14874,793.64,334.29,-806.32,299.51,-15.32`: the
  same image).
- Measured (`tests/renderer/cab-ao.json`, CMK's New Activity1 with a Czech
  3D cab, Medium, 1280 x 800, `TSRE_AO_DEBUG=1`): the still cab's
  occlusion term 1.8 levels of 255 on average from before (2.5 % of pixels
  over 8, most along the desk's edge against the window's world); a frame
  as before (median 52.2 to 52.9 ms either way, Off 50.3 ms).
- Tried first: one curve by distance for every view (the cab's up to 2 m,
  joined to the world's by 13 m). The cab looked right, but the world
  within 10 m did not: a bench at Prudnik lost its occlusion from close up
  (user). Fading occluders over the radius reached instead of 2 m: the
  wheel no longer shaded the desk.
- Options for later:
  - The cab's curve itself: a lower power (0.2) keeps the radius steadier
    but needs a larger screen limit close up (sparser taps); a longer join
    (3 to 10 m) for what the windows show.
  - When the player can leave the cab (stand up, open the door, climb
    down), the switch moves to the cab's door: the cab's share fades out
    as the head leaves the cab's box (a number instead of `setInCab`'s
    flag), or, exact, each pixel is tested against the cab's box in the
    occlusion pass (the box in the view's frame as a uniform), so that the
    cab keeps its radius and the platform the world's while one stands in
    the doorway.
- Captures: a view's `motionMove` while riding now moves the head in the
  vehicle's frame, within the cab's move box (`CameraFree::stepBy`). It set
  the riding frame's position to a world one before, and the camera left
  the cab.

### A smaller cab radius (user, 2026-11-11)

The user: in the MaSzyna ET22's cab, the controller wheel's shadow looks
broken. The CD cab's wheel looks right.

**Checked first.** The MaSzyna cab rides through `Consist::rideCab3D` like
an ENG's 3D cab, so the cab radius applies to it.
- A temporary log showed `setInCab` false outside and true in the ET22's
  cab.
- With it forced off, 46 % of the pixels of the seated view changed. The
  largest changes were 1 to 6 m away.

**The cab radius was not a smaller radius close up, only a steadier one.**
- The world's radius is cut to 15 % of the view's height on screen: about
  0.08 m at 0.5 m and 0.16 m at 1 m.
- The cab's was 0.12 m at 0.5 m and 0.15 m at 1 m.
- At the desk, the two were alike.

**Cause.** From the ET22's seat, the wheel's rim and spokes lie 10 to 20 cm in
front of the plate under it. GTAO takes an occluder as solid behind its
face, so the whole plate under the wheel went dark. Occluders fade over
the world's 2 m (`distanceFromDepth.w`), not the radius reached. Fading
them over the radius reached was tried before (above) and lost the CD's
wheel shadow.

**Measured** (`et22-wheel` and `cd-wheel` cases: CMK's activity 2, ET22 at
its seated place looking 20° down, 12:00; activity 1, CD 3D cab looking
25° down, 06:00; the user's profile; mean 0 to 255 of the region):

| Region | Radius 0.15 | 0.08 | 0.05 | Off |
| --- | --- | --- | --- | --- |
| ET22: plate under the wheel | 12.4 | 21.1 | 31.3 | 38.9 |
| ET22: desk right of the wheel | 52.5 | 93.0 | 102.3 | 95.0 |
| CD: inside the wheel | 18.2 | 27.1 | 32.6 | 30.8 |
| CD: under the rim | 24.3 | 25.6 | 28.8 | 31.7 |

Auto exposure lifts the frame when occlusion darkens it, so open surfaces
come out brighter with occlusion on than off.

A weaker curve in the cab alone helped less, on the ET22's seated view at
06:00 (the plate: 7.7 at power 1.5, 11.6 at 1.0, 14.4 at 0.75; 28.0 off).

**Now** `CabRadius` is 0.08 m at 1 m: 0.056 m at 0.3 m, 0.065 m at 0.5 m,
0.11 m at 3 m.
- At the seat, the ET22's plate keeps its shadow near the rim and spokes.
- The CD keeps its ring under the rim.
- 0.05 m would lose both wheels' shadows.

**Not done.** XeGTAO's thin-occluder rule: samples in front of the surface
fade sooner. The user's order: the strength, then the radius, then this, if
needed.

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

- Medium at large views, to profile (bbb at 2123 x 1948: 8.8 of 77 ms,
  task 25; it is already the half-resolution march): which of the march,
  the blur and the full-resolution composite takes it; the separable blur
  above may be most of the gain.
- Depth-aware upsampling in the composite (bilinear now, slight halos at
  silhouettes at Low and Medium).
- Occlusion in the Shape Viewer and in the water reflection.
- Tuning with HDR and tone mapping, which change how strong it reads.
