# Task 18 - Shaded Water

## Objective

Draw route water as one shaded surface instead of the ENV file's stacked
texture layers: moving waves, reflections of the surroundings and sun
glints, keeping the route's water textures as its colour. The ENV wave
fields (`world_water_wave_height`, `_speed`) and layer animation are ignored
for now.

## Legacy Water

MSTS routes list water layers in the ENV file (`world_water_layers`), each
with a height offset, a texture and an animation (UV scroll). BNSF Scenic has
three: an opaque brown bottom (`waterbot.ace`, -1 m), a dark green middle
(`watermid.ace`, -0.5 m, scrolling) and a blended blue-grey top
(`WaterTop.ace`, 0 m). TSRE draws each layer as flat quads over the terrain
patches flagged as water, at the tile's corner water levels plus the layer
height; the animation is not drawn.

## Design

### Surface

With `core.rendering.water.shaded` on, only the top layer (the highest; the
last of equal ones, `Environment::surfaceWaterLayer`) is gathered. Its
packets carry a `RenderItem::Water` block with the bottom and middle layer
textures (`Environment::lowerWaterLayers`), and draw with the
`StandardFogWater` program (`TSRE_WATER`, `WaterShading.glsl`).

- Colour: the layers stacked as the legacy drawing shows them (bottom,
  middle over it by its alpha, top over that), static, lit by the sun and
  ambient light like the legacy layers. The textures already include the sky
  they usually reflect, so 60% of the colour is kept as the water itself and
  the reflection is added for the actual view.
- Waves: `WaterNormalMap` generates a tileable 256 x 256 map of wave slopes
  once per context: 64 waves in random directions with whole cycles across
  the map. It stores the slopes and their squares, so the mipmaps keep the
  slope variance. The shader samples it at two scales (16 m and 5.12 m per
  repeat, both dividing the 2048 m tile, so waves continue across tiles),
  drifting in different directions. Waves too small for a pixel turn into
  roughness (the variance from the mipmaps).
- Reflection: Fresnel (F0 0.02) between the colour and the surroundings:
  - the mirrored scene (below) where the water lies in its plane;
  - otherwise the environment cube (task 17) when it is on;
  - otherwise a gradient from the dark banks at the horizon to the sky.
- Sun glints: GGX with the wave roughness, in the scene's shadows.
- Animation: `GLUU::animationSeconds()`, constant within a frame. Capture
  suites set `Game::animationFrozen`, so the waves stand still and frames
  settle.

### Why A Planar Reflection

The cube is rendered from the camera. At grazing angles water reflects what
stands on its banks, seen from the water level: from a bridge 20 m up, the
cube sees fogged hills and sky in those directions instead of the trees on
the bank. The cube is right for the sky and distant terrain, and is used for
water away from the reflection plane.

`PlanarReflection` renders the gathered queue once more from the camera
mirrored in a plane fitted to the water around it. MSTS water is not level:
its height is interpolated between each tile's corner water levels, so a
river slopes (BNSF: about 4 m across one view). `PlanarReflection::fitPlane`
fits a plane by weighted least squares through the bounding sphere centres
of the gathered water patches within 400 m of the camera
(`Renderer::visibleBounds` without a view, all of them when none is that
near), nearer patches weighing more; the tilt is limited to 10%, and
patches in one row keep the plane level across the row. The mirror and the
clip plane use that tilted plane. The fit does not depend on the view
direction: fitted to the patches in view only, a sloping river seen through
one or two patches got a level plane a metre off at the camera, and turning
the camera flipped nearby water between the reflection and plain shading
(BNSF, the camera half a metre above the river). Far reaches of a bending
river are left out for the same reason. Water must still be in view for the
pass to run.

The mirrored view:

- half the screen resolution;
- sky, distant terrain, then terrain and objects up to 500 m. Water,
  overlays and objects smaller than about a texel are left out;
- geometry below the plane is clipped (`clipPlane` and
  `gl_ClipDistance[0]` in `StandardFog.vs`, enabled only in this pass);
- front faces turned the other way round (`glFrontFace(GL_CW)`).

Water samples it at its screen position, offset by the wave slope and
blurred through the mipmaps by the roughness. Water off the plane would
mirror the scene shifted by twice its distance from it, so water fades to
the cube or gradient where that distance passes 0.3 m plus 0.6% of its
distance from the camera (about half a degree of shift); on BNSF the whole
visible river stays within it. The pass is skipped when the camera is under the plane, and when
the previous frame drew no water pixels: water under the banks passes the
view test, so the main water pass is counted with a `GL_SAMPLES_PASSED`
query (`RenderStats::pauseSamples` / `resumeSamples` keep the scene phase's
own query, which cannot nest, and its totals). A view turning to the water
reflects from the second frame. `RenderStats` reports the pass as phase
`reflection`.

Cost on llvmpipe (BNSF captures, 960 x 540): views without water are
unchanged (one water surface instead of three layers saves a few draws);
views over the river take about 1.7 times the frame time, the reflection
drawing about as many draw calls as the main view.

### Texture Units

| Unit | Water program |
| --- | --- |
| 0 | top layer |
| 4, 5 | bottom and middle layers (terrain material units elsewhere) |
| 6 | planar reflection |
| 10 | environment cube |
| 15 | wave map |

## Settings

Under Rendering > Water, applied while running:

| Setting | Default | Meaning |
| --- | --- | --- |
| `core.rendering.water.shaded` | on | One shaded surface; off draws the ENV layers as before |
| `core.rendering.water.reflection` | on | Mirror the scene in water in view |

## Not Done

- ENV wave height and speed, layer UV animation.
- Refraction or depth-based colour: the bottom is a texture, not the
  terrain under the water.
- Water at a different level from the fitted plane (a lake above a river)
  reflects the cube or gradient instead of the mirrored scene.
- Cheaper reflections if hardware needs them: a smaller target, leaving out
  distant terrain, or limiting the mirrored view to the water's screen area.

## Verification

- Captures against the previous baseline: only views with water change
  (BNSF `yaw90`, `yaw180`); EUROPE1, USA1, PROCEDURAL, the other BNSF views
  and the Shape Viewer are pixel-identical, and scene sample totals are
  unchanged. With `core.rendering.water.shaded` off, BNSF is
  pixel-identical to the baseline.
- `water-gl` suite: wave map statistics (level on average, full range,
  squared slopes, no seam), level and tilted mirror matrices, the plane fit
  (a sloping river, one row of patches, no patches, a far reach of another
  slope), the water program and its
  variants, frozen animation, and a mirrored render in which an object above
  the plane shows below the horizon and geometry under the plane is clipped.
- `settings` suite: the two settings and their translations.

## Next: Realistic Water (Crysis Target)

User, 2026-10-09: the shading is nice overall, but the wave texture is poor
and its repetition clearly visible. Make the waves procedural and dynamic;
the first CryEngine (Crysis, 2007), also used for architecture and GIS
views, is the target. Ideas and progress for the next water work are kept
here.

### Review Of The Current Waves

- **One small static map, repeated.** `WaterNormalMap` is 256 x 256 texels
  of 64 waves with 3 to 48 cycles across the map. At 16 m per repeat the
  waves are 0.33 to 5.3 m long, and nothing is larger, so at a distance
  every 16 m looks the same and the repeats line up into a grid.
- **The two scales repeat together.** 16 m and 5.12 m are 25:8, so their sum
  repeats exactly every 128 m (8 large, 25 small repeats). Transposing the
  second scale hides it only up close.
- **The waves slide instead of moving.** Each scale is one picture moved at
  one drift velocity, so it reads as a scrolling texture. Real waves
  disperse: each wavelength travels at its own speed (deep water
  omega = sqrt(g k), long waves faster), so the pattern changes all the time
  and never repeats in time.
- **No large-scale variation.** Real water is uneven over tens to hundreds
  of metres: gusts roughen patches, calm strips stay smooth. Its absence is
  what makes any tiling stand out at a distance.
- **The colour textures repeat too.** The body colour is the route's layer
  textures at their patch coordinates, static. That is right for the colour
  of MSTS water, but with plain shading on top their repeats show as well.
- **Kept**: the slope variance from the mipmaps turned into roughness
  (waves too small for a pixel blur the reflection), the Fresnel mix,
  planar reflection, GGX sun glints. These stay and take the new waves.
- **Positions**: `vWorldPosition` is relative to the camera's tile, so
  every wave period must divide 2048 m, or the waves jump when the camera
  crosses a tile. Periods 2048/n with n prime to one another (for example
  2048/7, 2048/47, 2048/331 m) keep that and repeat together only every
  2048 m, out of sight.

### What Crysis Did

CryEngine 2 (2007) water, as Crytek described it in its 2007 talks
(recalled, not re-checked; Tessendorf's "Simulating Ocean Water" for the
waves):

- Ocean waves from an FFT (Tessendorf) on the CPU, a small grid (64 x 64)
  each frame, displacing a screen-space grid; normal maps for the detail.
- Lakes and rivers ("water volumes"): several normal map scales, flowing
  along the volume.
- Refraction of what is under the water, with depth fog: colour absorbed
  with depth, shallow water clear.
- Soft shores: the water fades where it meets the ground (depth
  difference), with foam along the shore.
- Reflection (a planar pass, cheaper than the main view), sun glints,
  caustics on the bed, light shafts under water.

Far Cry (2004) already had reflection, refraction and soft shores; these
are what make water look like water more than any one texture.

### Plan

Each step keeps the current water as the fallback, and works on OpenGL
and QRhi.

1. **Waves (the user's complaint)**:
   - An FFT wave field (Tessendorf, Phillips or JONSWAP spectrum from a wind
     speed and direction): slopes, height and, later, the Jacobian for
     foam, each frame from the spectrum, so every wavelength moves at its
     own speed.
   - Three cascades with periods 2048/n (n prime to one another, see
     above), each covering its own band of wavelengths, from about 300 m
     (big lakes, sea) down to about 0.3 m.
   - A large-scale variation map (tileable noise, 100 to 500 m) scaling
     wave strength and roughness, for gust patches and calm strips.
   - The slope variance kept in the textures and their mipmaps (as now), so
     the far water turns rough instead of aliasing.
   - Wind speed and direction from the ENV's `world_water_wave_height` and
     `_speed` when set, a default otherwise; later from weather, and as
     Environment window sliders.
   - Recommended first: the FFT on the CPU in a worker thread (Crysis did
     the same), 64 or 128 per cascade, the results uploaded as textures.
     Both renderers only bind textures, so nothing backend-specific is
     needed, and the FFT can be unit-tested. Cost to measure on the Deck:
     about 1 to 2 ms per frame of worker time and about 0.4 MB of upload at
     128. A GPU version (fragment passes, or compute on QRhi) later only if
     the CPU cost matters; the shading does not change.
   - Alternative, no per-frame work: the spectrum's frequencies rounded so
     the animation loops (for example every 32 s), baked into a texture
     array once (tens of MB).
2. **Depth: refraction, absorption and soft shores**:
   - The scene's depth and colour before the water pass, read by the water
     shader (QRhi has a depth texture for AO; OpenGL needs a copy).
   - Water depth along the view: colour absorbed per channel
     (Beer-Lambert; red first), so shallow water shows the bed, deep water
     its own colour.
   - Refraction: the bed seen through the waves (screen offset by slope),
     not taking objects in front of the water.
   - Soft shores: opacity fades over the first few centimetres of depth,
     removing the hard line where terrain meets water.
   - The route's layer textures then tint the deep colour, with a setting
     for the legacy look. MSTS terrain under water is often untextured or
     shallow, so routes need checking.
3. **Foam and light in the waves**:
   - Shore foam from the depth of step 2; whitecaps where the FFT Jacobian
     folds (strong wind only); a foam texture, also broken up by the
     variation map.
   - Light through wave crests facing away from the sun (the green glow of
     Crysis water), from the wave height and the view and sun angles.
4. **River flow**: MSTS has no flow data, but its water levels slope down
   the river (the planar reflection already fits the tilt). Flow along the
   slope, drawn with two-phase flow sampling (the waves move with the flow
   without stretching); still water when level.
5. **Later**:
   - Displaced wave geometry (a projected or tessellated grid), for the sea
     and big lakes; rivers do not need it.
   - Screen-space reflection for water off the fitted plane (a lake above a
     river).
   - Moon glints at night (with the moonlight of the environment work).
   - Caustics on the bed, the view under water.

### Progress

- 2026-10-09: review and plan (this section). Nothing implemented yet.
