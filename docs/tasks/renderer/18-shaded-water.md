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
mirrored in the plane of the nearest visible water
(`Renderer::nearestVisible`):

- half the screen resolution;
- sky, distant terrain, then terrain and objects up to 500 m. Water,
  overlays and objects smaller than about a texel are left out;
- geometry below the plane is clipped (`clipPlane` and
  `gl_ClipDistance[0]` in `StandardFog.vs`, enabled only in this pass);
- front faces turned the other way round (`glFrontFace(GL_CW)`).

Water samples it at its screen position, offset by the wave slope and
blurred through the mipmaps by the roughness. Water more than about half a
metre away from the plane (other water levels) fades to the cube or
gradient. The pass is skipped when the camera is under the plane, and when
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
- Several water levels in view reflect correctly only at the nearest one.
- Cheaper reflections if hardware needs them: a smaller target, leaving out
  distant terrain, or limiting the mirrored view to the water's screen area.

## Verification

- Captures against the previous baseline: only views with water change
  (BNSF `yaw90`, `yaw180`); EUROPE1, USA1, PROCEDURAL, the other BNSF views
  and the Shape Viewer are pixel-identical, and scene sample totals are
  unchanged. With `core.rendering.water.shaded` off, BNSF is
  pixel-identical to the baseline.
- `water-gl` suite: wave map statistics (level on average, full range,
  squared slopes, no seam), the mirror matrix, the water program and its
  variants, frozen animation, and a mirrored render in which an object above
  the plane shows below the horizon and geometry under the plane is clipped.
- `settings` suite: the two settings and their translations.
