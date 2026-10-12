# Task 28 - Volumetric Clouds and Sky

Status: in progress on `feature/clouds` (from `feature/weather`), started
2026-10-11; steps 1-3 and 5-8 done (7: task 27), step 4 in part; reviewed
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
- Clouds in the water reflection and the environment map; precipitation;
  cumulonimbus anvils.

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
  softer filter.
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
  light band with horizontal streaks.

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
