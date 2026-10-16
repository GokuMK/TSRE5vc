# Task 28 - Volumetric Clouds and Sky

Status: merged into `main` 2026-10-14 (from `feature/clouds`, itself from
`feature/weather`), started 2026-10-11; steps 1-3 and 5-8 done (7: task 27), step 4 in part; reviewed
and timed on the Steam Deck 2026-10-12 (presets by pixels, temporal update,
horizon, stable cloud shadows, cached enclosure: see "Review and
performance"); step 9
(OpenGL fallback, skydome) not started. Includes task 27 (sky enclosure). Reads its weather
from task environment 01. Authorized overnight through step 8 and task 27
(user, 2026-10-11); verified on lavapipe with captures, timed by the user on
the Steam Deck.

## Objective

Clouds that look like clouds from a train or the editor camera at any time
of day: volumetric low and mid clouds, a flat high layer, a sky that matches
them, their shadows on the ground, overcast light, and light shafts. QRhi
renderer first; the OpenGL renderer and a generated skydome later.

## Decisions (user, 2026-10-10)

| Topic | Decision |
|---|---|
| Low and mid clouds | Raymarched volumes (Nubis-style), with the usual shortcuts |
| High clouds | Flat layer |
| Renderer | QRhi; OpenGL fallback and skydome generation later |
| Sky | A physical sky and atmosphere model, part of this task |
| Task 27 | Included (shared top-down maps and shading terms) |

## Options considered

1. **Flat layers** (MSTS's textured sky domes): no volume, wrong at low sun.
   Kept for high clouds and for the fallback.
2. **Sprite clusters** (Wang 2003, CryEngine 2 / Crysis): clouds built from
   lit sprites, soft against scene depth, edge glow towards the sun; shadows
   in a full-screen pass; screen-space sun shafts. Hand-built clouds, hard
   to drive from weather maps, flat up close. Rejected.
3. **Raymarched volumes** (Schneider 2015-2022, Hillaire 2016): density
   from a weather map, a height profile per cloud type and 3D noise;
   rendered at a reduced resolution with temporal reprojection (under 2 ms
   on a PS4 in 2015, a GPU class like the Steam Deck's). Chosen.

## Design

### Sky and atmosphere

Hillaire 2020: a transmittance LUT, a multiple-scattering LUT, a sky-view
LUT per frame and an aerial-perspective volume. Sun and moon from
`SunPosition`, `MoonPosition`. Replaces the flat sky colour on QRhi when
the physical sky is on (setting); the environment map (17), the water
reflection (18) and the fog take their colours from it. HDR values (24).

### Cloud density

- Weather from environment 01: per layer coverage, type, density, base and
  top, sampled with the generated detail and the wind drift.
- Height profile by type (stratus thin and flat, cumulus rounded and taller,
  cumulonimbus to the high layer).
- Noise: a 128^3 Perlin-Worley shape volume and a 32^3 Worley detail
  volume, generated once at start (or cached on disk), plus 2D curl noise
  for distortion; detail erodes the shape at the edges.
- A shell around the planet (radius 6360 km), so far clouds sink below the
  horizon.

### Rendering

- The camera is below the clouds in normal use; above or inside (mountain
  routes) works but is not optimised.
- Quarter resolution, one pixel of each 4 x 4 block marched per frame
  (blue-noise order), reprojected and blended with the history; full
  resolution upsampled with the scene depth.
- Marched up to the scene depth, so clouds sit in front of or behind
  terrain and objects; aerial perspective applies to them.
- Lighting: a short march towards the sun (self-shadow), Beer-Lambert with
  the powder term, a two-lobe Henyey-Greenstein phase, an octave
  approximation of multiple scattering, ambient from the sky above and the
  ground below.
- Early out at full opacity and beyond a distance; step length grows with
  distance.
- High layer: lit 2D cirrus textures on a shell at about 8-10 km, coverage
  from the weather's high layer.

### Cloud shadows and overcast

- A top-down cloud transmittance map along the sun direction around the
  camera, updated over several frames. The lit shaders multiply the sun
  term by it (terrain, shapes, glTF, water).
- Overcast (from the weather's coverage) lowers the sun and raises the
  ambient share in `Daylight`.
- Task 27's sky enclosure map shares its coordinate system: one texture,
  two channels if it fits.

### Light shafts

1. Screen-space: a radial blur from the sun of the sky's visibility times
   the cloud transmittance towards the sun (Mitchell 2007, Sousa 2008).
   Only with the sun on or near the screen.
2. Volumetric: a low-resolution march through the lower atmosphere reading
   the cloud transmittance map and the shadow maps: shafts under the clouds
   from any view, and later lamps in fog (froxels, Hillaire 2015).

### Performance

Steam Deck target: 2-3 ms for clouds and sky at 1280 x 800. Quality setting
Off / Low / Medium / High (march steps, resolution, noise octaves). Off
keeps today's sky.

## Steps

1. Physical sky (LUTs) on QRhi, with the sun and moon; environment map,
   water and fog colours from it.
2. Noise volumes; raymarched clouds with constant coverage; temporal
   reprojection.
3. Weather from environment 01 (presets first): coverage, types, wind
   drift; high layer.
4. Lighting quality: phase, powder, multiple scattering, ambient.
5. Cloud transmittance map: cloud shadows in the lit shaders; overcast in
   `Daylight`.
6. Screen-space light shafts.
7. Task 27: sky enclosure, lamps and ambient (see task 27's steps).
8. Volumetric light shafts.
9. OpenGL renderer: a flat cloud layer from the weather; generated skydome
   textures for routes and for MSTS and Open Rails.

## Step 1 (done, 2026-10-11)

- `Atmosphere` (`src/tsre/environment/`): the model on the CPU (Hillaire
  2020 constants: Rayleigh, Mie with Cornette-Shanks phase, ozone, ground
  albedo 0.3; km; light relative to the sun's illuminance). It makes the
  transmittance (256 x 64) and multiple-scattering (32 x 32) tables once,
  and gives sky light along any direction, the sun's light at a height and
  the colours the classic shading takes: `horizonColour` (the fog of a
  physical sky, averaged around the horizon 2 degrees up) and
  `zenithColour`. Display colour: `1 - exp(-L * 16)` and the display curve,
  which keeps the zenith blue and rolls the bright horizon off to white.
- QRhi (`RhiSky.cpp`): the two tables go up once as half floats; each frame
  a sky-view table (192 x 108, Hillaire's mapping: zenith angle squeezed
  towards the horizon, azimuth from the sun) is marched for the camera's
  height and the sun (30 steps). Every view that clears its colour (main
  view, environment map faces, water reflection) instead starts with a
  full-screen pass of the sky from that table, before its geometry; the
  direction comes from the sky band's projection and the camera's rotation
  (`setSkyInverse`, recorded per target when the sky band begins). Below
  the horizon (past the distant terrain) the horizon's colour. Night keeps
  the classic night sky colour as a floor.
- `Renderer::Sky` / `setSky` / `drawsPhysicalSky`: the editor sets the sun,
  the camera's height and the night floor each frame (`applySky`); where the
  renderer draws the physical sky the editor leaves out the skydome and
  takes the fog and sky colours from `horizonColour`. The sun and moon discs
  (`SkySatellites`) stay.
- Settings: `core.rendering.sky.model` (Classic, default; Physical) and
  `core.rendering.clouds` (Off, default; Low, Medium, High; used from step
  2). The OpenGL renderer keeps the classic sky.
- Checked: `sky` suite (8 cases: table coordinates, transmittance up and
  along the horizon, sun set, noon zenith and horizon, sunset towards and
  away from the sun, night); `rhi-shaders` bakes the sky passes; EUROPE1 on
  QRhi Vulkan (lavapipe) with the physical sky at noon, 18:00, 19:30,
  20:15, 21:00 and 23:00 (blue noon sky, golden low sun, orange band at
  sunset, twilight, dark night; distant hills fade into the horizon's
  colour); defaults pixel-identical to the branch's start (parity views,
  picking 144 of 144).

## Steps 2, 3 and 5 (done, 2026-10-11)

Done together: the clouds read the weather from the start (presets and
imports alike), and their shadow map shares the march's code.

- The editor (`applyClouds`) samples the environment's weather around the
  camera, 128 x 128 km at 1 km a texel per layer (coverage, type, base,
  top), and hands it to the renderer (`Renderer::Clouds`, shared, rebuilt
  when the weather, its time or the camera's kilometre changes) with each
  layer's drift and the weather's seed.
- Noise volumes (`CloudNoise`, CPU, all cores, 0.6 s here): shape 128^3
  (Perlin-Worley and three Worley octaves, Schneider 2015) over 6 km,
  detail 32^3 (Worley octaves) over 900 m, tileable; 3D textures.
- QRhi (`RhiClouds.cpp`), at the start of the main view, before its
  geometry:
  - cover maps, 512^2 near (120 m texels) and far (600 m): for the low and
    mid layers the weather's own 2D noise in its even form (the shader
    ports `WeatherNoise`: the hash, the 256 gradients, the octave turns,
    the octaves limited to the texel, the quantile table), for the high
    layer `WeatherDetail::cover` itself; redrawn only when they move, the
    weather changes or its drift does;
  - the cloud shadow map, 256^2 over 12 km around the camera: the
    transmittance along the sun from a plane under the clouds (20 steps).
    The lit shaders (`ShadowSampling.glsl`, under `TSRE_RHI`: standard,
    PBR, water) multiply the sun by it, projecting each point along the
    sun to the plane; past the map it fades to 0.6 of its edge.
- After the opaque passes (with ambient occlusion):
  - the march, at full (High), half (Medium) or quarter (Low) resolution: a shell
    around the planet between the layers' lowest base and highest top, up
    to the scene's depth (the farthest of the 2 x 2 texels under it) or 60 /
    90 / 130 km; 48 / 72 / 128 steps spaced quadratically, each pixel
    starting at its own place (interleaved gradient noise, moved each frame
    unless still). Density: the weather's 2D noise plus 3D Perlin-Worley
    noise, thresholded at 1 - coverage (so the clouds stand where the map
    shows them, their edges billows instead of walls), less at the base and
    towards the top (cumulus narrow upwards, stratus is a sheet), eroded by
    the detail noise; extinction 18-30 per km. Light: 4-6 growing steps
    towards the sun, three octaves of multiple scattering (Wrenninge), a
    two-lobe Henyey-Greenstein phase, powder; the sun through the
    atmosphere's transmittance; ambient from the sky-view table, less low
    in the layer and where the sun's path is dense. The flat high layer
    (cirrus) behind them at the weather's high base. Haze over the distance
    (45 km) towards the sky's colour;
  - temporal: blended with the last frames' result in the same direction
    (rotation reprojection, clamped to the neighbourhood, 15 % new);
  - composited over the view in display colour with premultiplied alpha
    where nothing nearer stands; the glow (bloom) behind dims with the
    clouds, so the sun does not shine through them.
- Overcast (editor): the low and mid layers' cover within 10 km; above 30 %
  the ambient light grows (up to 1.5 times) and greys, and the fog and sky
  colours turn grey. The sun's own light is left to the shadow map.
- `TSRE_CLOUDS_STILL=1` (captures): every frame complete and the same.
  Capture views take `weatherPreset` and `time` (hours) in 3D too.
- Checked: `sky` suite (noise tiles, volumes' ranges), `rhi-shaders` (all
  cloud passes and the lit shaders bake), EUROPE1 captures
  (`tests/renderer/clouds-views.json`, Medium, still): fair cumulus at
  13:00 towards the horizon and up, broken, overcast, cumulus at 19:30,
  scattered at 17:00 with the sun behind clouds, cloud shadows from above
  and on a town. Temporal mode matches the still images, a little smoother.
  About 1 s a frame at 960 x 540 on lavapipe. Defaults pixel-identical.
- Not yet: clouds in the water reflection and the environment map (their
  sky is clear); the march's cost on the Steam Deck; step 4's tuning by
  eye (cloud brightness, overcast's base structure, the mid layer's
  smeared look from below).

## Step 6 (done, 2026-10-11)

- `RhiLightShafts.cpp`, after the clouds, at half resolution: a mask of
  the open sky (the sky band's depth) within 12 degrees of the sun, dimmed
  by the clouds' transmittance; a radial blur of 64 steps towards the sun's
  place on the screen (each 2 % weaker); added to the view in the sun's
  colour (the atmosphere's transmittance, normalised), less over the open
  sky next to the sun, which the bloom lights already. Weaker below 5
  degrees of elevation and as the sun leaves the screen (gone a third of
  the screen past its edge). Works with the classic sky too (no clouds:
  the skydome's depth alone).
- Setting `core.rendering.lightShafts` (off by default).
- Checked: EUROPE1, fair cumulus at 17:00 and 19:00 looking towards the
  sun: rays fan out under the cloud in front of the sun; `rhi-shaders`.

## Step 7 (done, 2026-10-11)

Task 27's steps 1-3: see "Done" in `27-sky-enclosure-and-tunnel-lighting.md`,
including a glow problem found on QRhi Vulkan with depth-only passes.

## Step 8 (done, 2026-10-11)

- `core.rendering.lightShafts` is now Off / Screen space (step 6) /
  Volumetric. Volumetric (`applyVolumetricShafts`): at half resolution a
  march of 40 steps through the low haze in front of the camera, up to the
  scene's depth or 1.5 km (6 km towards the sky, but not past the clouds:
  their distance from the march), the sun's light at each step through the
  cloud shadow map and the far sun shadow map (terrain, trees, buildings);
  single scattering, a forward-peaked phase. The haze's extinction comes
  from the weather's visibility at the camera (Koschmieder, 3.912 / V),
  passed in `Renderer::Sky::haze`.
- Composite: the fog already holds the haze's light as if the sun reached
  all of it, so the shafts are the shade: the view darkens by the share of
  the haze's light blocked beyond the clouds' average shade
  (`Sky::overcast`, from the editor's overcast measure) times the haze's
  share of the view, at most 40 %; lit haze towards the sun adds a little.
- Checked: EUROPE1 fair cumulus and broken at 17:00 (`tests/renderer/
  shafts-views.json`, sun shadows on): rays under a gap in broken cloud,
  a soft glow towards the sun, views across and away from the sun
  unchanged; defaults pixel-identical.
- To tune on hardware: the strength (subtle now), the step count and
  temporal blending (the march is jittered each frame without history);
  lamps in fog (froxels) remain later work.

## Cloud shapes, second pass (2026-10-11)

The user's review of the first images: the shapes looked 2.5D, artificial
(they were at Medium; High only marches more finely). Causes: the weather's
2D field, the same at every height, set each cloud's outline, so clouds
were columns extruded from the map with flat tops and, near the camera
(40 m cover texels keeping 150 m octaves), vertical flutes; the 3D noise
only nudged the edges; the half-resolution march blurred them. Now
(Schneider 2015's form):

- The 2D field gives a local coverage (a half at the map's cloud edge, all
  of it well inside a patch), read a little aside by the 3D noise and by
  height (up to 250 m), so sides bulge and lean.
- The billows (Perlin-Worley eroded by its Worley octaves, 2.2-3.2 times
  finer for heaps than before) are multiplied by the height profile of the
  cloud type before the coverage's threshold (`remap(base, 1 - coverage,
  1)`): only a patch's cores reach the top, so cumulus rise as domes from
  flat bases. Sheets vary in thickness with the 2D field and the billows,
  with a ragged base, so overcast shows lighter and darker patches.
- Worley detail erodes the edges more strongly and finer (wisps at the
  bottom, billows at the top); extinction 35-70 per km, so edges are
  crisp; a little less ambient light for more contrast.
- The cover maps keep only the weather's large scales (120 and 600 m
  texels); High marches at full resolution (Medium half, Low quarter).
- Checked at High: fair cumulus towards the horizon, from below and
  towards the sun, broken, overcast, scattered, cumulus in the evening.

## Against Schneider 2015 (Horizon Zero Dawn), 2026-10-11

The user's review: the small white clouds' colours were off (too dense? no
ambient? no air between?); compare with the talk (slide numbers; the PDF's
pages are two more).

| Talk | Here |
|---|---|
| Shape 128^3: Perlin-Worley + 3 Worley octaves (30) | Same layout (CPU-made, own frequencies) |
| Detail 32^3: Worley octaves (31) | Same |
| Curl noise 128^2 distorting the detail (32, 37) | Done (2026-10-11): 2 channels, up to 150 m, more at the base |
| Height gradients: stratus, cumulus, cumulonimbus presets blended by type (33) | Stratus and cumulus profiles blended by type within each weather cell's base and top; no separate cumulonimbus (anvil) profile |
| Coverage signal from the weather map (33, 39) | Local coverage from the weather's own 2D noise, so the clouds stand where the map shows them |
| Base: shape x height gradient, then coverage remap, less density at the base (35-36) | Same form; no "denser towards the top" yet |
| Erosion by detail at the edges, inverted Worley at the base (37, 76) | Same |
| Precipitation: cumulonimbus at 70 %, rain clouds darker (42, 67) | Not yet (the weather has precipitation; the clouds ignore it) |
| Cumulus pushed onto the horizon (45) | No: the weather decides |
| Wind (47) | Each layer drifts with its own wind (weather drift) |
| Beer's law, Henyey-Greenstein (51-55) | Same; two lobes, plus multiple-scattering octaves (Wrenninge) |
| Beer-powder, view-dependent (57-66) | Done (2026-10-11): from the light's optical depth, only looking away from the sun |
| Ambient from the sky, more with height (87) | Same, from the sky-view table; plus ground bounce where the ground is sunny (2026-10-11), and diffusion through thick layers (two-stream; overcast bases) |
| Direct light the sun's colour (87) | The sun through the atmosphere at the cloud's height |
| Atmosphere over depth (87) | Done (2026-10-11): Rayleigh and Mie per colour at the path's height and the weather's haze (visibility); before, a grey 45 km fade |
| Spherical shell (72-74) | Same, at the Earth's radius |
| Cheap march to the isosurface, step back, back to cheap after empty samples (75-80) | Not yet: only empty coverage is skipped (performance) |
| 64-128 steps by angle, early exit (81-82) | Steps by quality, quadratic spacing; early exit |
| Light: 6 samples in a cone and one far (83-84) | Done (2026-10-11): 4-6 cone samples growing 1.8x and one 2 km further |
| Light samples cheap after alpha 0.3 (84) | Light samples always without detail |
| 2D alto/cirrus textures scrolling (86) | One cirrus layer from the weather's high cover |
| Quarter res, 1 of 16 pixels a frame, reprojection (92-93) | Full / half / quarter res every frame, blended with history |

The colours: density stays (35-70 per km; real cumulus is about 50-100);
the ambient was the sky's alone and dark under a covered sky (now ground
bounce and diffusion); the air between was missing (now per colour). The
editor's general ambient (PBR rough surfaces not taking the environment's
colour) is a separate renderer question.

## Review before the hardware test (2026-10-11)

Fixed:

- Sky passes of one frame shared a dynamic uniform buffer per target: the
  environment map's faces drawn in one frame (all six at first, two or
  more with the faces-per-frame setting) took the last face's directions.
  Each sky pass now takes its own buffer and resource set from a pool.
- Light shafts rebuilt their textures and pipelines every frame: they were
  keyed to the clouds' history texture, which alternates. They read the
  march's texture now; the screen-space mask samples the depth nearest
  (linear filtering of D32F is optional in Vulkan).
- The weather map under the clouds was rebuilt each kilometre the camera
  moved (49 000 weather samples, a hitch about every 20 s on a train): now
  2 km texels (the weather's cells are 2 km or more) in 8 km steps, 12 000
  samples a rebuild.
- The clouds' history is dropped when the camera jumps (over 300 m in a
  frame), so the last place's clouds do not smear in.
- Clouds at night keep 0.8 of the night sky's floor instead of black.
- Past the cloud shadow map, terrain took its clamped edge (stripes into
  the distance); now the clouds' average shade (from the overcast measure).
- The enclosure pass is skipped entirely on renderers without it.

Known, not changed:

- QRhi Vulkan on lavapipe loses the glow after depth-only passes (task 27
  notes); the user's hardware showed lights fine with shadows.
- The environment map preview captures black on lavapipe with the classic
  sky as well (not from this task).
- Fog stays one colour all around (the horizon's average): at sunset
  terrain fades to the same colour towards and away from the sun. The
  sun's light on the scene still follows `Daylight`, not the atmosphere.
- Clouds are not in the water reflection or the environment map; no rain
  drawn in 3D; cumulonimbus bases blue-grey (ambient from the open sky).
- First use: the atmosphere tables and the noise volumes are made on the
  CPU (about 0.6 s here).
- High marches every pixel: heavy for the Steam Deck; Medium is half.
- Enclosure: thin overhead parts (catenary) count a little as cover; the
  pass draws all opaque surfaces within 300 m once more.
- On QRhi's OpenGL backend the lit programs gain two samplers (cloud
  shadow, enclosure); drivers with 16 texture units per stage may fail
  the PBR program there (Vulkan is the default).
- With the physical sky the Editor Session's fog colour has no effect.

## Review and performance on the Steam Deck (2026-10-12)

The user's notes after trying the branch: the enclosure tanks performance;
the lamp multiplier works but is a stopgap until HDR exposure (the enclosure
stays needed either way); presets look partly right; the cloud shadow's
edge dances when the camera moves; volumetric shafts are heavy and need
work; High is too heavy (move Medium to High, Low to Medium, a lower Low);
clouds stop too high above the horizon in some presets.

### How it was measured

The capture harness now times whole frames (`minFrameMs`,
`medianFrameMs`; a grab waits for the GPU and reads the image back, so the
time is CPU + GPU + a fixed readback): QRhi reports no GPU timestamps for
offscreen frames. Differences between settings are the GPU's cost. CMK
(the lake by the bridge, the station hall at the route start), shadows on,
at the Deck's 1280 x 800 (`QT_ENABLE_HIGHDPI_SCALING=0`) and at 3200 x 1800:
the user's display is scaled 250 %, and every per-pixel cost grows 5.6
times there.

### Before (added ms over the classic sky, horizon / sun views)

| | 1280 x 800 | 3200 x 1800 |
|---|---|---|
| Physical sky | +1.8 / +1.9 | |
| Clouds Low | +6 / +6 | +20 / +14 |
| Clouds Medium | +15 / +16 | +52 / +56 |
| Clouds High | +57 / +64 | +220 / +262 |
| Volumetric shafts (over Medium) | +2 | +4 |

Causes: the march's buffer followed the window (High every pixel at 128
steps; at 3200 x 1800 that is 5.8 million rays a frame) and was marched
whole every frame.

### Changes

- **Presets** are the cloud image's pixels, whatever the window or its
  scaling: Low 320 x 180, Medium 480 x 270, High 640 x 360 (in the view's
  shape, never more than the view); march steps 48 / 64 / 96, light steps
  3 / 4 / 6. New High is about old Medium at the Deck's size, new Medium
  between, new Low about old Low (the user's reshuffle).
- **Temporal update** (Schneider 2015): each frame marches one pixel of
  every 4 x 4 block of the image (a Bayer order over 16 frames) into a
  march buffer of one texel per block; the temporal pass rebuilds the
  image: marched pixels blend half into the history, the others are the
  last frames' (rotation reprojected, clamped loosely to the marched
  neighbourhood), off the last image the march's result. Still captures
  and the frame after a jump (no history) run all 16 passes at once. The
  pass's pixel comes from a per-pass uniform block picked by a dynamic
  offset (`Update`, binding 10). The march's start jitter goes by the
  image's pixel (by the march texel, a block's 16 pixels shared it and
  showed as 4 x 4 steps). The scene depth under an image pixel is the
  farthest of 3 x 3 samples over it.
- **Horizon**: the weather around the camera reaches about 58 km (64
  texels of 2 km, moved in 8 km steps), and the march stopped at 60 / 90 /
  130 km, so a 1.5 km base ended about 1.2 degrees above the horizon, a
  4 km mid layer about 3 degrees. The cloud shell now sits on a smaller
  planet (`cloudShellRadius`): its radius puts the clouds' top, seen from
  the camera's height, on the horizon 55 km away (s^2 = 2 R (top - h),
  120 km to the Earth's radius), so the clouds reach the horizon within
  the weather; the march goes to 60 km at every quality. The sun's light
  on a cloud still uses the Earth's geometry at its height (the smaller
  shell would tilt a far cloud's sky away from the sun and set it early).
  No outer weather ring needed.
- **Cloud shadow map**: its centre stays on whole texels of the ground
  (46.9 m), the plane under the clouds no longer follows the camera's
  height, and the map is drawn only when its centre moves a texel, the sun,
  the weather or the cover maps change. The lit shaders and the volumetric
  shafts take the same centre (`cloudShadowCentre`). The dance came from
  the map recentred on the camera each frame (resampling its 47 m texels),
  there since step 5, not from the last review's edits.
- **Sky enclosure** (task 27): the map is drawn from a point snapped to
  cells of 80 texels (31.25 m; 16 m in height) and kept while the camera
  stays in its cell, drawn anyway once a second for edits. Same result as
  drawing it every frame (the lamp and ambient difference on and off is
  identical, pixel for pixel).

### After

| | 1280 x 800 | 3200 x 1800 |
|---|---|---|
| Clouds Low | about 0 / +3 | +6 / +1 |
| Clouds Medium | +4 / -2 (noise) | +2 / +6 |
| Clouds High | +2 / 0 (noise) | +5 / +4 |

Within the run-to-run noise (2-3 ms) at the Deck's size: the target of
2-3 ms for sky and clouds holds at all three. The image is softer than
before at High (640 x 360 for the whole view).

Enclosure, at the user's camera in the CMK station hall with 10 glTF lamps:
before, on added about 6.5 ms at noon and 8 ms at night, and 1.6 ms of CPU
(the second pass over everything within 300 m, every frame); now no CPU,
1-3 ms at night (the shaders' 16 taps), about 5 ms at noon. At noon the
hall is enclosed, so its lamps shine at full strength by day as at night:
that cost is the lamps', the feature working. A trial of four corner taps
first (skipping the rest in the open) changed the shade under tree
canopies (patchy cover leaves the corners open) and was dropped.

### Still open

- Lamps are gathered at full strength whenever the enclosure is on (their
  ranges too), and dimmed per pixel in the open: by day, lamps in the open
  cost their full night range. Needs each lamp's enclosure on the CPU (a
  readback of the map at the lamps); later, with HDR exposure replacing the
  multiplier.
- Volumetric shafts: 40 steps at half resolution, a new jitter each frame
  without history (noisy). Quarter resolution, about 24 steps and temporal
  blending, as the clouds.
- The cover maps' redraw key holds each layer's drift: with the weather's
  time running they would be redrawn every frame. Snap each layer's
  offset to whole texels and pass the remainder to the march.
- Still captures show the march's dither (frozen jitter); in motion the
  history averages it.
- Settings in `Game.h` (sky model, cloud quality, shafts, enclosure,
  enclosed ambient): to move to their classes when a full rebuild happens
  anyway (user, 2026-10-12: settings read every frame may live in `Game`,
  but their own class is better).
- Clouds in the water reflection and the environment map (done below);
  precipitation; cumulonimbus anvils.

## Second round on the Steam Deck (2026-10-12)

The user's notes on the above: horizon, performance and look much better;
the clouds very soft even at High (is a higher level worth it?); the cloud
shadow no longer dances, but the map's 47 m texel edge shows on the
terrain; where the sky meets terrain or objects, large blocky artifacts in
motion that fade over time ("unacceptable"); the light shafts broken by
them too, the screen-space ones nice but short, the volumetric ones cast
from every cloud far too strongly; a grid of lit patches around lamps
(task 27, "Only wide cover").

### Edges in motion

The march stopped at the scene's depth under each pixel and the composite
cut the clouds where the scene was nearer than the clouds' distance. With
the temporal update, a pixel over a building or a hill keeps "no cloud"
(stopped at it) until its next march, up to 16 frames later, after the
camera has moved the object off it, and the per-block distance cut the
image in 4 x 4 blocks along every edge. Now:

- Only distant terrain (the distant depth band) stops the march. The
  scene band (objects, terrain within a few km) never stands behind a
  cloud: the march goes through it, so the history holds the clouds there
  too.
- The composite draws the clouds only over the sky and the distant band,
  cut at the full resolution by the scene's depth, with no distance test.
- The flat high layer is skipped over distant terrain only.

Checked with captures of the camera moving along the track and turning
(`clouds-motion.json`, `clouds-turn.json`): no blocks along roofs, masts or
the hills.

### The finished image for the shafts

The temporal pass writes the finished image to a fixed texture besides the
alternating history (and rebuilds the clouds' distance the same way:
marched pixels the new distance, the others the reprojected one, read
nearest). The composite and both kinds of shafts read these; the shafts
read the march buffer before, which holds one pixel of each block per
frame (the blocks around the sun).

### Light shafts

- Screen space: the glow the rays start from reaches 30 degrees around the
  sun (12), and the rays fade more slowly along the blur (0.99 a sample,
  0.98): longer shafts.
- Volumetric: the haze's shade now follows forward scattering, full
  towards the sun and a fifth away from it (the cube of the cosine), at
  about half the strength (0.8, 1.5) and at most 35 % of the view (60 %).
  Shafts from clouds away from the sun are faint.

### Ultra

A fourth preset: 960 x 540 pixels, 128 steps, 6 light steps; sharper
clouds. Added ms over the physical sky without clouds (horizon / sun
views):

| | 1280 x 800 | 3200 x 1800 |
|---|---|---|
| Clouds High | +2.8 / +2.6 | |
| Clouds Ultra | +6.8 / +7.9 | +8.3 / +7.1 |

About 4-5 ms more than High at the Deck's size: for faster machines.

### Still open (this round)

- The cloud shadow map's edge: 47 m texels show on the terrain as a grid
  edge (user: "note it, leave alone in this step"). A finer map near the
  camera (a second cascade, or 512 x 512 over the same 12 km) and a
  softer filter. (2026-10-13: 512 x 512 over 14 km, 27 m texels; see
  "Cloud shadows that drift with their layers".)
- Slight steps at the bright edges of clouds right by the sun, in motion.
- The other items of the first round's list above.

## Clouds in reflections (2026-10-12)

The user: water reflects only the blue sky. The clouds are drawn in the main
view only; the water reflection (PlanarReflection, the scene mirrored in the
water's plane at half the window's size) and the environment cube (water off
that plane, glTF metals) draw the physical sky alone.

Options considered:

1. **March the clouds in the reflection too**: its own small image and
   history, with the same update of one pixel per 4 x 4 block a frame; the
   mirrored view's directions, its own depth for the composite. Matches the
   sky, holds on calm water; about 0.5-1 ms estimated on the Deck. Water off
   the plane and glTF metals still read the cube.
2. **Clouds in the environment cube**, marched per face as the round robin
   draws it (about 128 x 128 per face; no reprojection, the cube does not
   turn with the camera). Everything that reads the cube gets them (also
   the sky's ambient under overcast later), but soft: about 1.4 cloud
   pixels per degree, against about 5 at Low in the sky, so calm water
   would show blurred clouds.
3. **Screen space**: the water reads the main view's finished cloud image
   along the reflected direction (the clouds are as good as infinitely far,
   so one lookup; since the edge fix the image holds clouds behind near
   objects too). Nearly free and as sharp as the sky, but only where the
   reflected direction is on screen: looking down from a bridge, the
   reflected zenith is off screen and the clouds would vanish mid-river.
   Needs 2 behind it.

Decision (user): 1 now; 2 later as its own step (glTF reflections, ambient,
water off the plane); 3 in reserve.

### Done (option 1)

- A view's own cloud resources (march, image, histories, uniform buffers,
  passes) are a `CloudView`; the main view's are part of `CloudResources`
  (which holds the shared weather, cover and shadow maps), the reflection
  has `reflectionClouds`.
- The reflection is drawn before the main view prepares this frame's maps,
  so it takes the main view's last march parameters (the camera moved a
  step since, invisible at the clouds' distance) with its own view: the
  mirrored view's directions and reprojection, its own history, its depth
  for the composite. Run after the reflection's opaque passes, or at its
  end when no later pass has draws.
- Its image has a quarter of the main view's preset pixels (the reflection
  has a quarter of the window's): 320 x 180 at High. It marches with Low's
  steps (48, light 3) at every quality: the waves blur it, and the look in
  captures matched the preset's steps.
- Cost at 1280 x 800, CMK lake (two views, two runs, medians): about
  +1-2 ms over the reflection without clouds (about 1.4 on average; the
  run-to-run noise is 2-3 ms). The reflection itself costs 5-6 ms.
- Checked: the lake at noon (fair cumulus, broken) and in the evening, and
  the turning sequence (`clouds-turn.json`): the reflected clouds follow
  the sky, no blocks in motion.
- Seen on the way, not from this change (the same with the clouds off):
  low over the lake, the far water just under the horizon shows a flat
  light band with horizontal streaks. The user does not see it on screen:
  kept as a note only.

## Light shafts, third pass (2026-10-12)

The user, looking straight at the sun: screen-space shafts overblown
(without HDR); the volumetric ones pixelated along the clouds' outlines,
as the cloud edges were before. The earlier test views had the sun off
their centre; views centred on it (CMK, 17:00, sun at 27.7 degrees) showed
both plainly.

- Screen space: half the strength (0.2) and a glow falling off faster
  towards its 30 degree edge (cubed): the rays keep their reach, the
  clouds by the sun no longer wash out white.
- Volumetric: the haze march went 6 km into open sky, up through the
  cloud layer (where the cloud shadow map, a plane under their base, says
  nothing), and stopped at each cloud's own distance where the cloud
  image had any cloud: the path changed length at every outline, in the
  cloud image's pixels, with stray squares at wisps. It now ends at the
  plane under the clouds' base (the crepuscular rays are under the
  clouds) and the map is read only below it. The clouds' distance image
  had no other reader: removed from the march and the temporal pass
  (two render targets fewer per pass).
- Changing tone mapping at run time in the Settings Editor can crash
  TSRE (user): see "Crash on changing tone mapping" below.

### Overexposure by the sun without tone mapping (2026-10-12)

The user: looking at the sun with clouds nearby and shafts on, the view
overexposes with tone mapping off. Measured on CMK views centred on the sun
(17:00, share of fully white pixels within 200 px of it): no shafts 0.4 /
2.8 % (fair cumulus / scattered), volumetric 0.7 / 1.1 % (slightly darker
than none), screen space 3.8 / 15-18 %. Bloom makes no difference (it is
the lamps' glow). So it is the screen-space shafts, added onto a sky that
is nearly white already.

Without tone mapping the view holds display colours up to 1: the shafts
are now screened over it (view + shafts (1 - view)), so they brighten
without clipping, at 0.4 again (screening limits itself); with tone mapping
(float view) they are added at 0.2. Scattered, by the sun: 4.4 % white.

## Clouds in the environment cube (option 2, 2026-10-12)

The user: render the clouds into the cube too, with a preset below Low (the
reflections are distorted anyway). Test place: reflective spheres on `bbb`
(`core.startup.camera=-5306,14961,-468.29,69.05,-58.96,-175.54,2.80`).

- Each face has its own `CloudView`: 96 x 96 pixels, 32 steps, 2 light
  steps, its own history (a face does not turn: the reprojection is the
  identity). Faces are drawn round robin, one a frame by default: each draw
  marches 4 of a block's 16 pixels, so a face is renewed every 4 of its
  draws. The faces share face 0's pipelines (same formats, same layouts):
  no extra pipeline builds.
- After each face's opaque passes (or when the next face begins), with the
  main view's last march parameters, like the reflection; the face's
  projection scale and rows (OpenGL's) are taken from its own band.
- Everything reading the cube gets the clouds: glTF metals (smooth spheres
  show them sharp, rough ones blurred and greyer under broken cloud, as the
  sky), water off the reflection's plane.
- Cost on the Deck at 1280 x 800 (bbb spheres, two runs): the cube costs
  3.0-3.4 ms with the clouds and 3.8-4.7 ms without; no measurable cost.
- Found on the way: with the first face of a frame, the check that the
  sky-view table was drawn came before the face's sky was flushed, so the
  faces never got clouds: the view is flushed first now.

## Crash on changing tone mapping (2026-10-12)

Reproduced headless by switching tone mapping every 5 frames during a
capture (a temporary hook): an access violation in the driver at the
frame's submit, with the clouds and the reflection or the cube on. Two
causes, both fixed:

- Tone mapping on or off changes the view's format, so its textures are
  made anew. The clouds' composite (and march resource sets) and the light
  shafts decided whether to rebuild by comparing the view's colour texture
  pointer; a new texture can take the old one's address, and they kept
  targets and resource sets of deleted textures. The view's recreation now
  releases them (and resets the water depth copy's and the depth probe's
  bound depth).
- The reflection and the cube's faces use the cloud resources early in the
  frame; a rebuild later in the same frame (the view recreated, the
  quality or window changed) deleted them while that frame's commands
  still referred to them. The cloud resources are released with
  `deleteLater()` now (kept until the frame is submitted).

With both, the stress run passes with everything on; either alone still
crashed. (The Settings Editor's own path was not run on screen.)

## Clouds that move with the clock (2026-10-13)

With the time of day running (task environment 01, "The clock"), the
weather's hours change every frame. Before, that sampled the weather again
(12 000 samples), redrew both cover maps and the cloud shadow map each
frame: on the Steam Deck at 1280 x 800, broken clouds, 6.7 ms a frame more
(median 31.4 to 38.1 ms, 2.6 ms of it CPU), measured by forcing it.

- Between keyframes each layer's field moves only with its layer's wind
  (`Weather::frame`: the keyframes' shifts and the variation follow the
  drift). The weather texture and the cover maps are drawn at the drift
  they were sampled with (`Renderer::Clouds::built`); the march and the
  cover pass read them shifted by the drift since (`shiftLowMid`,
  `shiftHigh`, per layer). They are sampled and drawn again for another
  place or weather, a shift of 2 km (the near cover map reaches 2.7 km
  past where it is read; the high layer drifts four times as fast as the
  low one), keyframes blending on (2 %), or an hour later: in real time
  about every 70 s. Shifted against sampled again (12:01 after 12:00, the
  high layer 1.7 km on): mean difference 0.09 of 255, at most 9.
- The cloud shadow map is drawn at the low layer's drift and read shifted
  (`cloudShadowCentre`), drawn again after 500 m. (Replaced by a map per
  layer: "Cloud shadows that drift with their layers" below.)
- The history follows the clouds (option 1 of the user's question about
  timelapse pixelation): the temporal pass reprojects each pixel's cloud,
  at the clouds' middle height along its ray, back by the low layer's move
  since the view's last frame (`motion`; each view, the reflection and the
  cube faces too, its own). At 30 s/s, broken clouds, against a still
  capture: pixels more than 8 off from 3.7 % to 1.8 %. What remains: the
  mid and high layers drift two and four times as fast and their own way,
  and the height is the clouds' middle, not the pixel's cloud; the march
  could give its depth and layer for an exact reprojection (later).
- Up to 1 min/s the user found it fine. From 2 min/s the main view marches
  two pixels of each block a frame, at 4 min/s four (`Renderer::Clouds::passes`,
  set by the editor while its clock runs), and a new sample replaces the
  pixel instead of blending half with its history. On the Deck, broken
  clouds: 31.4 ms a frame, 33.4 with two passes, 34.8 with four. Against a
  still image, pixels more than 8 off at 30 s a second: one pass 1.8 %,
  two 1.0 %, two with whole samples 0.5 %; at 4 min a second 0.2 %, 0.01 %,
  0.00 %. (In the capture test four passes measured no better than two:
  it steps the time once a capture, not once a frame.)

## Cloud shadows that drift with their layers (2026-10-13)

The user, with the clouds moving in real time and the camera still: a
slight flash at the edges of the cloud shadows every second or so. Three
causes:

- The map was drawn again whenever the light's direction moved by 1e-4,
  about every 1.4 s with the sun's motion, on a grid fixed to the ground:
  each draw sampled the moving clouds at other points, and the edges of
  its 47 m texels stepped.
- Between draws the whole map moved with the low layer; the mid layer,
  drifting twice as fast, jumped back at each draw.
- The lit draws take the map's place as they are queued, and the map is
  drawn later in the frame (`prepareClouds`, before the view's geometry).
  On a redraw frame the new map was read where the old one lay, off by
  the drift since the last draw, for one frame.

Now:

- A map per layer in one 512 x 512 texture over 14 km (27 m texels): the
  low layer's transmittance in red, the mid layer's in green
  (`layerDensity`, the march's density split by layer). The lit shaders
  and the volumetric shafts multiply the two (`cloudShadowMid`,
  `cloudsMid`).
- Each map's centre stays on whole texels of its layer's own ground, its
  drift taken off (`shadowCentre`): between draws the map moves with its
  layer, and when it is drawn again its texels fall on the same points of
  the clouds.
- The centres move back near the camera when either map has drifted 1 km
  from it (`placeCloudShadow`, in `setClouds`, before the frame's draws
  take the place). The map is drawn again then, or when the weather, the
  cover maps, the plane or the light's slant change: the light in steps
  that move the shadows of the clouds' top 5 m (`CloudShadowLightStep`;
  the lit shaders project with the light as it is).
- Cost of a draw on the Steam Deck (1280 x 800, broken, forced every
  frame): 1.7 ms (median 29.3 to 31.0 ms). In real time it is drawn every
  few seconds at most. In a fast timelapse it is drawn nearly every frame,
  as the light and the drift move on.

Captures (CMK from 550 m looking down over fields, broken clouds at noon,
`TSRE_CLOUDS_STILL`, a frame every 0.5 s of the clock; mean change of the
ground between frames, of 255):

- Before: every third frame (1.5 s) a step, 0.47 to 0.58 against 0.15 to
  0.18 between, with 1.3 to 1.8 % of the pixels changing by more than 6.
- After: an even 0.23 to 0.30 each frame with no steps. Between steps it
  is a little more than before: the shadows are sharper, and the mid
  layer moves at its own speed.
- Steps of 4 s (4 min/s): the change rises and falls with the cloud edges
  in view, with no steps.
- A camera jump of 6 km and back (the maps moved there and back): the
  same image to the bit.
- The captures grab each view's second frame, so they cannot show the
  one-frame placement error. That error cannot happen now, since the place
  is set before the draws.

## The clouds' depth (2026-10-13)

The user's review point 2: near terrain and objects always stood in front
of the clouds (the composite cut the clouds at the whole scene band), so
the clouds could not be flown into or over, and a mountain did not reach
into their base. Point 0 too: the timelapse from 2 min/s showed little
gain from the history following the clouds.

- The march gives each pixel where its clouds are: the front (where 2 %
  of the light is taken), the mean distance by the light each sample
  takes, the layer (low 0, mid 1, high 2) the same way, each times the
  cover, and the cover (a second target, `marchDepth`). Times the cover,
  the history blends them as it blends the clouds, and a pixel without
  clouds adds nothing. The history and the composite's image keep them
  (`historyDepth`, `imageDepth`; the temporal pass writes four targets).
- The composite, at the view's resolution: none of the clouds where the
  scene lies before their front, all of them past their mean distance,
  in part between. The march still stops only at distant terrain, so a
  moving object leaves no outline in the history. Objects in front of the
  clouds stay sharp, as before (the user's hybrid: the near scene is cut
  each frame at the full resolution, the far one the march stops at).
- The history follows each pixel's own clouds: at the march's mean
  distance along the ray, moved by its layer's drift (low, mid or high,
  between them by the layer). Before, every pixel moved with the low
  layer at the clouds' middle height, about twice the low layer's.

Captures (CMK, broken clouds at noon, `TSRE_CLOUDS_STILL` for the views):

- The field view from 550 m and the lake view: the same image to the bit.
- In the cloud base (1000 m) and inside a cloud (1300 m): the cloud's
  inside, where before the terrain showed through. At 900 m, in the thin
  base, the terrain is hazy. Above the clouds (3200 m): their tops over
  the terrain below (beyond 3 to 4 km the editor's fog greys the terrain,
  and as distant terrain it was clouded before too).
- The history against a still image, one pass a frame (the capture
  harness steps the time once a view), pixels more than 8 off: at 30 s/s
  3.0 % before, 1.9 % after; at 4 min/s 3.3 %, 2.4 %. The still images
  are the same as before, so the march is unchanged.
- Cost on the Steam Deck at High (lake view, 60 frames): median frame
  30.1 and 30.8 ms before, 30.5 to 31.7 after (one run 34.7). That is
  about half a millisecond, within the noise of the moment (clouds off
  measured 26.4 to 39.3). The capture harness has no GPU time:
  `lastCompletedGpuTime` stays 0 for its frames.

Clouds over hazy terrain (user, 2026-10-13: with the env file's weather on
POLAND, camera 15,-133,343.48,850.02,-275.01,-1.87,2.81, the clouds blend
with the sky's colour, not the terrain behind them):

- The march fades the clouds towards the sky's light along the ray (the
  air in front of them, `airTo`). Over the sky that is right, and mixed in
  linear light a bright cloud still shows through haze. Over terrain it
  turned far clouds into patches of sky over the mountains, and they hid
  the ranges' tops. The file's weather takes its visibility from
  `world_fog_distance`: 2000 in POLAND's editor.env, a strong haze; the
  presets have 40 km.
- Over the scene the composite now takes the air's light back out and
  fades the clouds into the scene instead, as far as the clearest colour
  lets them (Rayleigh's blue keeps its share). It recomputes the same air
  from the clouds' mean distance (all layers, as the march now uses), the
  sky-view table (bound to the composite) and the shell radius
  (`motionHigh.w`).
- Fading the clouds into what is behind them in the march itself, sky
  included, was tried first: it mixes in display space, and hazed clouds
  over the sky washed out (15 to 25 levels nearer the sky's colour on
  POLAND). Over the sky the march's result is kept.
- CMK views: as before (mean difference 0.06 of 255 at most). POLAND, the
  file's weather: the whole range shows with faded clouds over it.
- The terrain's fog is the fog setting's, not the weather's visibility, so
  hazy mountains can stand out more clearly than the clouds in front of
  them. (Since task 32 the terrain takes the same air.)

## Light scattered through the clouds (2026-10-14)

The user, after the aerial haze and the sky weighted down in auto exposure
(task 29): cloud shadows still a little too dark; later, with rain and
storms, clouds should shade by their density, storms dark.

- The shadow map held what passes straight through (exp(-depth)): nothing
  for any ordinary cumulus, so in its shadow only the sky's ambient lit the
  ground, 0.3 against 1.0 in the sun, about 3.8 stops (real cumulus shadows
  are about 1.5 to 2.5).
- It now also holds what passes in all, the light scattered through the
  cloud too (two-stream: 1 / (1 + 0.75 (1 - g) depth), g about 0.85, the
  march's diffused light), in its blue (low) and alpha (mid). The lit
  shaders take that, the two layers' depths added; the light shafts keep the
  direct light (crisp rays). By depth: a thin cloud's shadow light, a
  cumulus's about a quarter of the sun, the depth of a storm's dense tower
  almost nothing.
- Past the map, the clouds' average shade 1 - 0.7 overcast (was 0.85).
- Captures, auto exposure (`build/cloud-light/cloud-light.png`): from 550 m
  over CMK the sunlit and shaded ground (the darkest and brightest tenth,
  linear) 2.6 to 3.0 stops apart before (fair, scattered, broken), 1.7 to
  2.1 after; ground at the track, broken 41 to 52 (of 255), overcast 47 to
  60; POLAND scattered 61 to 73.
- Under an overcast the light through it (a quarter to a third of the sun)
  falls from the sun's direction: slopes facing it lighter. The editor's
  overcast ambient (1.5 times) was meant to stand for it; to watch.
- Storms: the presets' layer density (thunderstorm 1.2) does not reach the
  march (its extinction is the type's, 35 to 70 per km); a storm's tower is
  as dense as a cumulus, and its shadow came out brighter now (the track
  under the thunderstorm preset 46 to 64). With rain and storms the
  density goes into the weather texture and the extinction.

## The sky's light through the clouds (2026-10-14)

Under a storm or a heavy overcast it is dark mostly because the sky's light
is blocked, not the sun's alone; the ambient stayed the clear sky's, and
under an overcast the editor raised it by half to make up for the sun,
whatever the clouds' depth.

- After the shadow map, a pass marches straight down through both layers
  over its extent (64 x 64 around the camera; two-stream, as the shadow
  map's), a second takes their cover (the share letting through under
  0.95) and the mean of what they let through; read back a frame or two
  later (`cloudSkyLight`), drawn again with the map.
- The ambient (`ambientColor`, so standard, PBR and water alike) is scaled
  by the open sky, plus under the cover the light through it, plus the sun's
  light the clouds' sunlit sides scatter down into a broken field: the light
  through them again, by the sun on the ground against the sky (cover (1 -
  cover), none under a closed deck, where the sun's light through it is the
  sun's share already). The editor's raise is gone; its grey stays.
- Measured at noon over CMK (cover, through, the ambient's factor, linear):
  fair 0.46, 0.55, 1.58 (the cumulus' sides); scattered 0.74, 0.38, 0.96;
  broken 0.84, 0.43, 0.87; overcast 1.0, 0.35, 0.35; thunderstorm 0.68,
  0.23, 0.76.
- Ground with auto exposure (`build/cloud-light/sky-light.png`): at the
  track overcast 60 to 45 (of 255), broken 52 to 44, fair 74 to 80; from
  550 m overcast 86 to 65. The sky's clouds stay bright over the darker
  ground.
- A storm grows dark with the clouds' density (the rain and storms task):
  the map, the shadows and this follow it.

## Cloud shadows through a window resize (2026-10-14)

The user: resizing the window flashed a frame without cloud shadows. A new
view target (another size or format) released every cloud resource with
the view's textures (the crash fix of 2026-10-12, above), the shadow map
and the sky's light included: the frame's lit draws took no cloud shadow
and the clear sky's ambient. Now it releases only the main view's own (its
march, image and composite, and the two pipelines made with its resource
set); the shared maps and what they hold stay (`releaseMainCloudView`,
`createSharedCloudMaps`). The reflection's and the cube's clouds keep
theirs (their own targets release them). Auto exposure keeps its last
measurement through it too.

- The first frame after a resize, the ground from 550 m (broken, 1280 x 800
  to 1200 x 760 and back): 96 of 255 instead of 78 before (no shadows),
  78 now, as every frame.
- 14 resizes in a row at the CMK river with the reflection and the cube on:
  no fault, every frame alike.

## Light shafts after auto exposure and the aerial haze (2026-10-14)

The user: volumetric shafts almost without effect, still costly; screen
space ones weaker with auto exposure. Measured on the user's CMK camera
facing the sun (17:00 and 19:00, fair cumulus and broken; mean difference
shafts on against off, of 255):

- Volumetric: 0.55 at the fog density 0.3, 2.6 at 0.7. Their haze was the
  aerial haze's, scaled to a fifth since the density's default went to 0.3
  (task 32). They take the weather's own haze again (`Sky::shaftHaze`):
  2.6 at 0.3.
- Screen space: 7.4 with auto exposure, 11.5 in Dimmed. The exposure
  measured the view with the shafts in it and took back what they added
  (and gave back what the volumetric ones darkened). The shafts now go over
  the view after the measurement, as bloom does (`present`): 11.9 with auto
  exposure. They now also cover water and blended surfaces (drawn after
  them before), as the air in front of them would.
- Cost on the Steam Deck (median frame, 60 frames): screen space about
  1.3 ms, volumetric 1.6 ms at 40 steps, 1.0 ms at 24; 16 save nothing more
  (the rest is the full resolution composite).
- 24 steps against 40, the user asking whether nothing is lost (the march:
  along each ray up to the scene or 1.5 km, 6 km to the clouds' plane over
  the sky, whether the air at each step is in the sun's light, by the far
  sun shadow map, 1.4 m texels over 1.4 km, and the cloud shadow map). A
  view with the sun 5 degrees up behind pines (CMK river, 19:30; effect
  against off: mean 5.9, at most 27 of 255): against 128 steps, 40 differ
  by 1 at most, 24 by 2; with the jitter moving, the spread over 8 frames at
  most 0.8 (40) and 1.4 (24).
- What the volumetric shafts show: the air under the clouds in their shadow,
  over the 6 km to the clouds' plane, greying the sky and clouds towards
  the sun. No rays through the trees at any step count (128 too): the trees'
  shadows are in the map, but the 50 to 150 m of air in front of them
  scatters about 1 % of the light at a 25 to 40 km visibility. Visible tree
  rays would want mist near the ground (or stronger forward scattering
  there).

### The cloud map's edge at a low sun; haze on the ground (2026-10-14)

- Found trying the near layer: the shafts read the cloud shadow map where
  the air meets the plane under the clouds, along the sun. At a low sun that
  lies many km towards it (6 degrees, 900 m under the clouds: 8.6 km), past
  the map (7 km each way), and its clamped edge stood for all of it: at
  sunset all the air counted lit or (here) shaded at once, and the "greying"
  of the sky above was mostly that. Past the maps the shafts now take the
  clouds' average open share, as the lit shaders do (`cloudVisible`): the
  pine view at 19:30 changes by up to 122 of 255, the far views at the
  user's CMK camera (17:00, 19:00) by under 0.1 on average.
- The far march stays (the user: far shafts matter most, the driver looks
  far ahead). Besides it, haze lying on the ground: ten times the weather's
  own haze at the ground under the camera (`Sky::groundHeight`), thinning
  over 120 m of height, over the first 500 m of each ray in 12 steps closer
  near the camera, scattering forward (Henyey-Greenstein 0.85), its lit
  light added (strength 0.1), nothing taken away.
- Pines with the sun 6 degrees up behind them: a warm glow of lit haze
  around the sun and over the treeline, the crowns dark in front (mean 2.9,
  at most 98 of 255); the user's CMK camera at 17:00 facing the sun, mean
  1.0; away from the sun at noon at most 1, in the evening 0.
- No rays through the trees: a beam through a gap is a few metres wide; 12
  steps over 100 m, at half resolution and without frames averaged, find it
  now and then. Ten times denser haze shows a strong glow, still no beams.
  Streaks would want the near light blurred along the lines to the sun (as
  the screen-space shafts), or many more steps and frames averaged
  (volumetric fog).
- Cost on the Steam Deck (median frame, noisy): about 1.6 to 2 ms with the
  near layer, against 1.0 without it. Its clouds taken once over the 500 m
  instead of per step saved nothing measurable and changed the glow (mean
  0.9): left per step.

#### Left as they are for now (the user, 2026-10-14)

Two next steps for the near layer, to come back to:

1. Beams through the trees from the near layer. Blur its light along the
   lines to the sun's place on the screen, as the screen-space shafts blur
   the image: the scattered hits on lit air between the trunks then run
   into streaks. The near light needs its own half-resolution target
   (today it leaves the march in the same pixel as the far light, `s.a`),
   two or three blur passes towards the sun, then the add. It works only
   with the sun on the screen or near it; elsewhere the glow stays as now.
   Cheaper than more steps, since the march stays at 12.
2. The near layer optional. It costs up to 1 ms on the Steam Deck for a
   glow around a low sun, nothing away from it. Either its own setting (on
   by default with the volumetric shafts), a fourth choice of Light shafts
   (volumetric without the ground haze), or off below a cloud quality.

Both may be overtaken by volumetric fog for mist and ground fog (next
task): the same haze lying on the ground, denser in valleys and on calm
clear mornings, marched in a grid of cells over the view and averaged over
frames. That gives beams through trees by itself, and the near layer would
become its lowest, thinnest case. Mist there is to come from the weather and
the terrain on its own (wind, cloud over the night, humidity, the time; how
far a place lies below the ground around it), with optional controls per
weather keyframe and painted areas only for special places.

### Through a hill at sunset (user, 2026-10-15)

EUROPE1 at 20:00: with the sun set behind a hill 1 km off, the march lit
the haze in front of it in a glow with rays from under the ridge. Past the
far shadow map (700 m about the camera) every point counted lit. The march
now also takes the terrain's shade over 16 km (`TerrainShade`, task 33),
as do the lit shaders and the fog.

## Still open (2026-10-15)

What remains of the "Still open" lists above, checked against the code:

- **Volumetric light shafts too weak** (user, 2026-10-15): done (below,
  "Rays from the volumetric shafts"), kept as they are (user, 2026-10-16:
  still not fully liked; fine tuning later, it took too long now). Open:
  frames not averaged (a fine shimmer by the sun); the rays' look; a
  shadow lookup blurred by the distance the shadow travels (real cloud
  shadows wash out far from the cloud) as a cleaner cure for the stripes
  than the rule on the rays' run.
- **Storms:** dark by their density and the cloud above, towers and anvils,
  rain under the clouds (done, below). Still: rain near the camera and
  from above, the rain's streaks.
- **A little moiré on storms at High with the camera still** (user,
  2026-10-16; the slightest move takes it away, so driving may hide it: to
  check in practice). The march's start is blue noise now (below): what
  error remains is grain, not ripples. Still for later if it shows: three
  samples a long step at High (Medium has three; `high.w`, about 1 to 2
  ms more in storms); the history's clamp to the marched neighbourhood,
  which keeps the error while still (a moving camera resamples the
  history, blurring it). Lowering the clouds was considered and
  rejected: steps shorten only in proportion, storms reach 10 to 12 km.
- **OpenGL renderer:** step 9 (a flat cloud layer from the weather).
- **Settings in `Game.h`** (sky model, cloud quality, shafts, enclosure,
  enclosed ambient): to their classes at the next forced full rebuild.
- Done since those lists: the cover maps follow the clock's drift instead
  of being redrawn each frame ("Clouds that move with the clock"); lamps
  reach at most 30 m and have a "Lamps by day" setting (task 27); the
  shafts' blocky cloud outlines ("Light shafts, third pass").

## Rays from the volumetric shafts (2026-10-16)

The user: the volumetric shafts far too weak. Measured at CMK from the hill
towards the sun (17:00 and 19:00, fair cumulus, scattered, broken): 2 to 4
of 255 on average against shafts off, a faint greying, no rays.

- **Why:** (1) the air's sunlight through the clouds came from the cloud
  shadow map, 7 km about the camera; at a low sun the clouds that cast the
  rays stand 10 to 50 km towards it, past the map, where the clouds'
  average stood for them: no rays at sunset. (2) The rays' contrast was
  the air's share in shade against the clouds' average cover, darkening
  only: at a low sun most air lies in some cloud's shade, more than they
  cover, and the air lit through the gaps never showed. Drawn in false
  colours (the share in shade, the average, the haze) the march held the
  rays all along; the composite took them out.
- **Far cloud shadows:** past the map the air takes the clouds from their
  weather and the far cover map (205 km), as the clouds' march reads them
  (`RhiRenderer::cloudFarMaps`; bindings 5, 6).
- **Contrast against the surroundings:** the march's result's mips (some
  30 pixels around) give the neighbourhood's share in shade; air lit
  through a gap more than around it is brightened (the rays, `direction.w`
  6), air shaded more darkened only a little (`sun.w` 0.8; the user:
  stronger, the image looked darkened around the rays), the haze's square
  root (thin haze keeps its rays), towards the sun most.
- **Rays that fall, not across the sky** (user: rays falling below the
  clouds good, running horizontally not, at any sun): at a low sun (8.6
  degrees, user, CMK 5:00, fair cumulus,
  `-5350,14860,542.10,286.98,-960.66,1168.04,7.80`) the clouds' shadows,
  stretched 1 / tan(elevation) times their height, drew long
  near-horizontal stripes across the sky and its clouds. A sun ray at a
  pixel runs on the screen along the sun's direction less its part along
  the view: the rays show where that is steeper than some 35 degrees from
  the vertical and go flatter than 60. Tried on the way, all rejected
  (debug output showed the low sun: the hills shading most of the air, the
  air still lit striped by the far clouds): the haze's phase (all rays
  fainter, the stripes kept); the lit share compressed (the stripes and the
  good rays differ alike, 5 to 15 %); no rays over cloud faces (near the
  horizon the good rays lie over distant clouds too); the spread narrowed
  to a fan about the sun below 10 degrees of elevation (fitted to two
  views; what matters is the rays' run). Only against
  surroundings of a like path (the haze each crosses): at the horizon the
  terrain's short path beside the sky's long one made a dark band along
  the hills.
- **Noise:** the march starts from the clouds' blue noise (the old noise's
  diagonal pattern showed in the drawn-out contrast) and is read 2 x 2 (a
  mip).
- Tried on the way: raising all the strengths (rays at sunset, but the
  whole view under scattered clouds 13 of 255 darker); the light added
  alone (a glow, no rays); the lit air against the clouds' average (no
  effect: the average is no measure at a low sun).
- **Captures** (`build/shafts/fall.png`, `fall-zoom.png`): broken at 17:00
  (27.5 degrees), two bright rays falling from the gaps; fair cumulus at
  17:00, rays falling from the clouds by the sun; at 5:00 (8.6) no stripes,
  a glow under the sun at the horizon; broken at 19:00 (9.1) a glow and
  rays at the sun on the horizon; away from the sun, overcast and noon as
  before; under a storm the view no longer 9 of 255 darker. Between frames 1.7 % of pixels
  change by more than 2 of 255 (0.1 % with shafts off): fine grain in the
  lower sky by the sun.
- **Cost on the Steam Deck** (High, median frame, three rounds, old
  against new): fair cumulus 29.1 against 29.4 ms, broken 27.0 against
  27.7, broken at 19:00 24.7 against 24.7.

## Storms dark by their density (2026-10-16)

The user: heavy storm clouds should look dark. They were lit as cumulus:
the weather's layer density never reached the march (the extinction was
the type's alone), and the sky's light inside a cloud fell only with the
height in the layer and the sun's path, marched a few km, so an 11 km
tower's base was lit as a 1 km cumulus's.

- **Density:** the weather texture has a fourth column beside the three
  layers' (`Renderer::Clouds::extra`): the low and mid layers' density (1
  the type's usual), the area's precipitation (mm/h) and its snow share
  (for rain under the clouds, next). The extinction is the type's times
  the density, in the march, the cloud shadow map and the sky's light
  through the clouds alike. Presets: thunderstorm 1.2, rain 1.0, overcast
  0.8 (its low sheet a little thinner: 2 of 255 brighter at noon over
  CMK); imported weather 1.
- **The cloud above** (`cloudAbove`): for each sample in a cloud, the
  optical depth of the low and mid layers above it and of its whole
  column, estimated from the weather and the cover as `layerDensity` makes
  them, the billows' noise at its mean, by the type's profile at three
  heights. No pass and no reach: far towers as near ones. A fair cumulus's
  column comes to about 20, a storm's tower about 250.
- **What it darkens:** the sky's light inside falls by what the cloud
  above lets through (two-stream), as far as that is less than a
  cumulus's at the same height (the shares were tuned on cumulus: thinner
  clouds stay as they were); through a deep cloud it is the cloud's own
  light, grey, not the sky's blue. The ground's light from below goes
  under columns of 20 to 150 (a storm is as wide as it is deep: the ground
  under its base lies in its shadow). The sun's light diffused through
  takes at least the cloud above (its path was marched only a few km).
- **Captures** (`build/storms`, CMK from the hill 130 m up, still): a storm
  against the sun at 17:00 has a dark grey base under its sunlit edges
  (mean 93 to 87 of 255, up to 46 darker); with the sun behind, towers lit
  cream-white over a dark base band; looking up, the base overhead dark
  grey. Broken 0.4 of 255 on average, rain 1.6 darker.
- **Cost on the Steam Deck** (High, median frame, two rounds each, old
  against new): fair cumulus 38.4 / 37.2 against 39.1 / 39.3 ms,
  thunderstorm 43.4 / 42.5 against 43.4 / 42.8, overcast 34.9 / 35.0
  against 35.3 / 35.6: about half a millisecond, within the runs' noise.
- Rain under the clouds needs the march below their base, in front of the
  scene (the composite takes the clouds only over the sky and the distant
  band): a step of its own, with the anvils.

### The storms' hatching (user, 2026-10-16)

The user: a moiré clearly visible on storm clouds (CMK,
`core.startup.camera=-5354,14854,-931.35,325.05,-355.80,-81.23,22.41`).

- **Cause: the march's steps.** Its steps (96 at High) spread with the
  square of the distance along the ray through the layers: through a
  cumulus layer some 3 km of ray, steps of 85 m at most; through a storm's
  slab, 10 km tall, some 27 km at 22 degrees up, 400 to 800 m. Too coarse
  for its edges and its lit surface (far brighter than a few hundred metres
  in): where each pixel's samples fell (its start, interleaved gradient
  noise, of diagonal structure) showed as hatching on the bases and the
  towers, in motion too. Averaging more frames (a new sample a quarter, not
  half) kept it: it is the coarse steps' error, not noise. Ultra (128
  steps, more pixels) mostly removed it.
- **Fix:** in a layer 3 km deep or more, a step longer than 120 m or 1 % of
  its distance that meets cloud is split into samples no longer than that,
  each lit on its own: two at most at Low, High and Ultra, three at Medium
  (its steps half as long again as High's; `high.w`). A probe without the
  detail first (it only erodes), so empty steps cost as before; 16 samples
  more at most a ray, or a sixth of the steps (`high.z`). Every quality, in
  the main view (user: the fewer the steps, the plainer the hatching; Low
  may keep a little; not the water's reflection, blurred by the waves, nor
  the cube, read blurred). Tried on the way: four
  samples a step (no better seen, 2.5 ms more at High in a storm); lit once
  a step, at its end or where it meets cloud (the hatching stayed on lit
  towers).
- **A variant of the march** (`DEEP_SPLIT`, `CloudView::marchDeepPipeline`):
  its larger loop slowed every view by 0.7 to 1 ms even where nothing was
  split, so it is compiled apart and used only while a layer of the
  weather is 3 km deep somewhere (`Renderer::Clouds::deepest`).
- **Captures** (as in use, frames averaged; `build/moire`): the user's view
  (`your-view-towers.png`, `your-view-base.png`) and a lit tower at 17:00
  (`lit-tower.png`) at High, the hatching gone; each quality before and
  after (`CMK/q-all.png`: Low's hatching nearly gone, a little grain left;
  Medium and High smooth, within 0.1 of 255 of four samples a step); fair
  cumulus as before (0.04 of 255).
- **Cost on the Steam Deck** (median frame, old against new): at High with
  two samples a step (three rounds) thunderstorm 42.8 against 45.5 ms,
  looking up 31.7 against 34.0; fair cumulus 37.8 against 38.3 and overcast
  35.6 against 35.4, within the runs' noise. Every quality, with the caps
  (two rounds each, against the march split at High only; noisy, fair
  cumulus anywhere from 38 to 46 ms): thunderstorm at Low 42.5 against
  44.6, at Medium 42.3 against 44.9; fair cumulus and overcast as before.
  (With the lake's reflection split too, High was 1.5 ms dearer: taken
  back.)
- The towers' vertical streaks (pillars of billows) stay: their shape, with
  the anvils (below).

### Storm shapes (2026-10-16)

The storm clouds stood as pillars of billows streaked straight up.

- **Causes:** cumulonimbus cells came from noise of 3 km features, so
  towers some 2 km wide stood 10 km tall; the heaps' profile (only a
  patch's core reaches the top) made them spikes; the cover map's finer
  detail, the same at every height, was drawn straight up their 10 km into
  grooves; and they had no anvils.
- **Cells of 8 km** for cumulonimbus (`WeatherDetail::cloudScales` and the
  cover pass alike: the map view and the showers under the cells follow),
  their map without the detail finer than 2 km: towers 5 to 10 km across,
  their walls made by the billows.
- **Towers** (storm clouds, type 0.8 to 1): each cell solid from its flat
  base, each part of it as high as it lies deep in the cell (its edge half
  the layer, its core to 0.9): towers narrow upwards into domes and differ
  in height; the billows move their edge at every height; less eroded
  (hard edges).
- **Anvils:** the top of the layer (from 0.74) spread from the cores that
  reach it, 6 km downwind (the high layer's drift so far, `shadowMid.zw`),
  a quarter of the sky at the top where the cells cover a third; the top
  flat, the underside rising from the core to a thin edge; thinner (0.3,
  ice). `cloudAbove` takes both.
- Tried on the way: anvils spread over half the sky (a dark ceiling over
  everything); the walls moved a km by the billows' finer noise (walls
  full of holes) or by the curl noise read along a slant (towers leaning
  on diagonal streaks).
- **In the storm variant** of the march (`STORMS` with `DEEP_SPLIT`; the
  code slowed fair cumulus by about 1 ms though it never ran there), used
  while the weather has storm clouds (`Renderer::Clouds::stormiest`); the
  reflection and the cube take it too, without the split; the shadow map
  and the sky's light always have the shapes.
- **Captures** (`build/shape`, CMK from the hill, still): from 13 km, towers
  of several heights with anvils spread over them; from the hill, massive
  towers with billowing walls over flat dark bases, without streaks (as in
  use too, the user's view); against the sun at 17:00, one dark tower with
  lit edges.
- **Cost on the Steam Deck** (High, median frame, old against new, four
  rounds): thunderstorm 46.1 against 45.5 ms (the solid towers end rays
  sooner), looking up 33.8 against 35.2 (more cloud overhead); fair
  cumulus and overcast as before.
- **Mixed with heaps** (user: alone the towers look stark, among other
  clouds natural): a weak cell stays a heap a km or two tall (from 0.12 of
  the layer, was 0.45), stronger ones congestus, only the cores reach the
  anvil; the thunderstorm preset's low cover 0.4 (was 0.3; its variation
  spreads that from few storms to many). From the hill: a storm with its
  dark base among cumulus and blue gaps (`build/shape/CMK/mix1`). Cost
  within the runs' noise.

### Rain under the clouds (2026-10-16)

- **Where:** the march, before the clouds, marches the rain from under the
  low clouds' base (camera below it): 16 samples, closer near, from the
  near scene's end (the scene's far plane along the ray: the composite
  draws the clouds over the sky and the distant scene only; begun at
  300 m, rain covered distant terrain and not near terrain, with a seam
  between them) to the base, the ground, distant terrain or 60 km (storms
  on the horizon stand 30 to 60 km off).
- **How much:** the weather's rain (column 4 of the weather texture, the
  area's rate), under the cells' cores for showers (as
  `WeatherDetail::precipitation`, hard edged as the towers), everywhere
  for a deck's rain; extinction 0.35 rate^0.63 per km (8 mm/h about
  1.3), half for a deck's; streaked along the fall (noise 2.5 km across,
  25 km tall), lighter near the ground; slanted by the low wind (half a km
  aside a km of fall: `sun.w`, `lightColour.w`).
- **Its light:** the sky's and the sun's through the cloud above
  (two-stream, the column as `cloudAbove`'s, a storm's as solid as its
  tower): dark under a storm, grey under a deck; snow lighter.
- **In the heavy-weather variant** (`RAIN` with `STORMS` and `DEEP_SPLIT`),
  used while the weather has rain (`Renderer::Clouds::rainiest`).
- **Captures** (`build/rain`, CMK from the hill, as in use): under a storm
  cell 5 to 15 km off (`sweep2`, `sweep3` against `sweep-old`) a grey
  curtain hangs under the dark base, hiding the bright horizon and the far
  towers behind it; a deck's rain thickens the grey to the horizon. Storms
  30 to 60 km off show little: their rain is a strip a degree or two high
  in the haze. The streaks do not show: seen through kilometres of rain
  they average out (and the cloud image is coarse).
- **Cost on the Steam Deck** (High, median frame, four rounds, noisy):
  rain 40.1 against 41.2 ms; thunderstorm, fair cumulus and overcast as
  before within the noise.
- Not yet: rain near the camera (within the near scene: the weather's haze
  stands for it; drops later), rain seen from above the base (flying),
  rain in front of near terrain.

### Blue noise for the march's start (2026-10-16)

- Where each pixel's march starts was interleaved gradient noise (cheap,
  no texture), whose diagonal structure the remaining error took: ripples
  on storms with the camera still. It is now blue noise
  (`CloudNoise::blue`: void and cluster, 64 x 64, 16-bit ranks, made at
  start with the other noise; binding 11), moved on each frame by the
  golden ratio as before: the error is fine grain, without a pattern.
- Captures at the user's CMK view at High (`build/blue`): with a fixed
  start (still) the old ripples across the towers become even grain; as
  in use both smooth; fair cumulus as before. Unit: each rank once, every
  8 x 8 window averages within 0.04 of a half (`cloud-blue-noise`).
- Cost: one texture read a marched pixel; on the Steam Deck within the
  runs' noise (thunderstorm 44.1 against 44.0 ms, rain 40.2 against 40.1).
- Compared without the split too (`build/bluecase`, Low and High, still
  and as in use): the old noise alone hatched diagonally, blue noise alone
  left even grain; with the split the old noise kept curved bands on the
  towers, blue noise fine grain. As in use, blue noise is the cleanest.
- Observation (user): the old noise's curved bands, still, looked like air
  rising. They are its samples' bands of equal depth through the towers
  (as with the rain off), moving with each frame and the camera, not the
  clouds' own. That look, if wanted, would be streaks along the updraft in
  the towers' noise (storms' fine tuning, later).

## Verification

- Captures (QRhi): presets (fair cumulus, broken, overcast, front) at noon,
  in the evening, at sunset and with the sun behind a cumulus (shafts);
  clouds against a mountain; looking along a long straight towards the
  horizon.
- Temporal stability: a camera moving and turning over frames, without
  smearing or crawling.
- Unit: noise generation, the weather sampling, the shell geometry.
- Steam Deck: time per frame at each quality.

## References

- Schneider: [Horizon 2015](https://www.advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf),
  [Nubis 2017](https://advances.realtimerendering.com/s2017/Nubis%20-%20Authoring%20Realtime%20Volumetric%20Cloudscapes%20with%20the%20Decima%20Engine%20-%20Final%20.pdf),
  [Nubis Evolved 2022](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-NubisEvolved-NoVideos.pdf)
- Hillaire: [Sky, atmosphere and clouds in Frostbite, 2016](https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016_pbs_frostbite_sky_clouds.pdf);
  "A Scalable and Production Ready Sky and Atmosphere Rendering Technique",
  EGSR 2020; "Physically Based and Unified Volumetric Rendering in
  Frostbite", SIGGRAPH 2015
- Wenzel: [Real-time Atmospheric Effects in Games, 2006](https://advances.realtimerendering.com/s2006/Chapter6-Real-time%20Atmospheric%20Effects%20in%20Games.pdf)
- Sousa: [Crysis Next Gen Effects, GDC 2008](https://www.slideshare.net/slideshow/crysis-nextgen-effects-gdc-2008/25051981)
- Mitchell: "Volumetric Light Scattering as a Post-Process", GPU Gems 3,
  chapter 13
