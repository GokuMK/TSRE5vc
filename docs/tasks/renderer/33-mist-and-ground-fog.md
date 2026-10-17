# Task 33 - Mist and Ground Fog

Status: planned 2026-10-14 on `feature/mist` (from `main` after the clouds
merge), reviewed by the user the same day (Decisions); steps 1 to 3 done. QRhi renderer with the physical
sky. Builds on tasks 28 (clouds, shadows, light shafts), 32 (aerial haze)
and environment 01 (weather data).

## Objective

Fog that lies on the land the way real fog does: thick in valleys and over
rivers on calm, clear mornings, thin or gone on hilltops, burning off as the
sun climbs, never on a windy or cloudy night. Seen from a train it matters
most far ahead, as banks of fog in the valleys the line runs into, and up
close as a white wall with the track, signals and lamps fading into it.
Sunlight through trees in fog gives beams (task 28 could not: the air was
too thin).

It is automatic: the weather and the terrain decide where and when. A route
designer can force it or turn it off per weather, without drawing areas.

## Decisions (user, 2026-10-14)

| Topic | Decision |
|---|---|
| Scope | Radiation and valley fog and rain mist; sea fog later, if it makes sense |
| Humidity | Temperature and dew point per keyframe: the values users know |
| Designer control | The fog override per keyframe and the Environment panel's session setting; no painted areas for now |
| Near and far | Both at once: the ground fog of a morning is seen up close as much as far ahead (step 3) |
| Setting | A separate "Mist and fog" setting, on in Advanced Environment |

## What exists

- Aerial haze (task 32): per colour, from the weather's visibility, scaled by
  the fog density setting (0.3 in Advanced Environment). It thins with
  height (a scale height of 1.5 km), the same in valleys and on hills.
- Volumetric light shafts (task 28): a far march (24 steps, up to 1.5 km, or
  6 km of sky to the cloud base) and a near layer of haze lying on the
  ground (12 steps over 500 m, ten times the weather's haze at the ground,
  thinning over 120 m of height). Half resolution, no frames averaged.
- Weather import (Open-Meteo) fetches temperature and dew point at 2 m and
  the weather code. Fog codes (45, 48) give 300 m visibility and a low cloud
  base 30 m above the ground; the humidity is not kept.
- Weather keyframes: wind per layer, precipitation, visibility, cloud
  layers. No temperature or humidity.
- Terrain: detailed tiles around the camera (`TerrainLib::getHeight`),
  distant tiles beyond, water levels per tile.
- QRhi: 3D textures (the cloud noise); no compute shaders yet.

## Fog in nature (what the model follows)

- **Fog and mist.** Fog: visibility under 1 km. Mist: 1 to 5 km in damp air.
  Extinction is 3.912 / visibility (as the haze): 0.02 /m at 200 m, 0.008 at
  500 m, 0.002 at 2 km. Droplets scatter without absorbing and mostly
  forward (Mie). In thick fog, light scattered many times makes it a nearly
  even white-grey.
- **Radiation fog** forms when the ground cools under a clear night sky and
  chills the air above it to its dew point. It needs:
  - few clouds over the night (clouds send heat back to the ground);
  - light wind, about 1 to 3 m/s (calm air gives dew or fog only a few metres
    deep; above about 5 m/s the air mixes, or the fog lifts into low stratus);
  - moist air (a small spread between temperature and dew point by evening,
    damp soil after rain, rivers and lakes).
  It thickens towards dawn and is densest around sunrise. Once the sun warms
  the ground it thins from the edges and lifts, typically within 1 to 3
  hours. In late autumn and winter, with a low sun, it can stay all day in
  valleys. Most common in autumn.
- **Cold air drains downhill** and pools in valleys and basins, so the fog
  fills them to a nearly level top: a sea of fog seen from above, with hills
  standing out of it. Valley fog is often 50 to 300 m deep; fog over open
  flat land often 10 to 100 m.
- **Over rivers and lakes** fog forms first and densest. In autumn, water
  warmer than the air steams.
- Out of scope here: sea and advection fog (moist air over a cold surface,
  even with wind), and fog in mountains from low cloud touching the slopes.
  The second already happens when the cloud base lies below a hill.

## Options considered

1. **Height fog only.** An exponential fog in height in the lit shaders,
   added to the haze. Cheap, but the same in valleys and on hills, with no
   light and shade in it and no beams. Too little: the look depends on fog
   following the land.
2. **Per-pixel march** of a fog density field, as the volumetric shafts
   march the haze: half resolution, one result per pixel. It covers near and
   far, but needs many steps near the camera to catch beams and lamps, and
   gets transparent surfaces wrong (one depth per pixel).
3. **Froxel volume** (Wronski 2014, Hillaire 2015): a 3D grid fitted to the
   view frustum. Each cell gets its density and light (sun with shadows,
   sky, lamps), the grid is averaged over frames and summed front to back.
   Every lit shader reads its fragment's fog from the grid, transparent
   surfaces too. About 1 ms for 160 x 90 x 64 cells on a 2013 console, a GPU
   class like the Steam Deck's. It reaches a few hundred metres to a few km.
4. **Hybrid** (proposed): the froxel volume near the camera, and a coarse
   march through the fog layer beyond it, to 10 km or more. The march stays
   between the lowest valley floor and the highest fog top, so it is short.

## Design

### When: the fog from the weather

A fog state, computed from the weather at the camera and the time of day.
It is computed when the weather's time moves on (a few times a minute of
weather time), not per frame.

- **Potential (0 to 1)**, as a product of:
  - clear: the low and mid cloud cover averaged over the night (fog fades
    out above about 0.5);
  - calm: the low wind over the night (best at 1 to 3 m/s, none above
    about 5);
  - moist: the dew point spread at evening (likely under about 2 °C, none
    above about 6 °C);
  - the hours since sunset (it grows over the night).
- **Burn-off** after sunrise: the depth and density fall with the sun's
  height and the time since sunrise, slower with a low sun (winter). The
  base may lift off the ground into a thin stratus layer before it clears.
- **Out of the potential**:
  - the visibility in the fog at the valley floor (from 1 km in mist down to
    about 100 m);
  - its depth above the valley floor (from a few metres to about 200 m,
    deeper with a longer night and more potential);
  - how patchy it is (more while it forms and while it clears).
- **Rain and drizzle** add mist: the same layer, deep and thin, from the
  precipitation.
- **The weather can override it** per keyframe: off, or a set amount, depth
  and visibility. A forced fog without humidity values still works.

### Where: the terrain's floor map

A map around the camera that says how far each place lies above the ground
that cold air drains to.

- **Size:** 256 x 256 cells of 64 m (16 km, 8 km each way). Sampled from
  the detailed and distant terrain a few tiles a frame on the main thread
  (the terrain is not safe to read elsewhere), again when the camera has
  moved 1 km, swapped in when complete (see step 2). Tiles whose edges lie
  far off their neighbours' are left out (wrong terrain tiles, below).
- **Floor:** the lowest ground within about 1.5 km, then smoothed over a few
  hundred metres. The fog's top is the floor plus the fog's depth: it fills
  valleys to a level top, while slopes and hills above that stay clear.
- **Height above the region:** how far the floor stands above the lowest
  floor within about 5 km. Fog thins with it, since broad hills and plateaus
  shed their cold air into the valleys around them.
- **Water:** cells under a tile's drawn water get moisture; fog lies on
  water and wet ground wherever they are (a pond on a hill), up to half its
  depth.
- **Patches:** the cloud noise volume (task 28) at a few hundred metres'
  scale, drifting with the low wind, breaks the fog into patches. The pooled
  shape itself stays where the terrain puts it.
- **Uploaded** as a 2D texture: floor, height above the region, moisture.
  Past the map's edge the fog fades out.
- **Map view:** the weather layer (environment 01) can show where fog lies,
  for designers.

The fog's density at a point is then the fog's density at the floor, times
the share at its height above the fog's top (full below it, a soft edge of
5 to 20 m), times the region and moisture factors and the patch noise. The
aerial haze stays as it is: it is the dry air. The fog adds to it.

### Near: the froxel volume

- **Grid:** about 160 x 90 x 64 cells at 1280 x 800 (Low maybe 128 x 72 x
  48). Slices spaced exponentially to about 1.5 km.
- **Built with compute shaders**, the first in the renderer (`QRhi::Compute`;
  Vulkan on the Deck, Direct3D 11, OpenGL 4.3). The fallback is fragment
  passes into a 2D atlas of the slices.
- **Light in each cell, from:**
  - the sun, through the near and distant shadow maps and the cloud shadow
    map, which gives beams through trees and buildings;
  - the sky, from the sky's light through the clouds (task 28's sky light
    factor);
  - lamps, from the light grid (task 21), which gives halos around lamps and
    signals at night in fog;
  - the moon (task 31).
  The phase is two Henyey-Greenstein lobes (forward about 0.8 and a weak
  back lobe), with a term for light scattered many times, as the clouds have.
- **Averaged over frames:** the cell positions are jittered, and each frame
  blends with the last frame's grid moved with the camera (as the clouds'
  history is).
- **Summed front to back** into light added and light let through per cell.
- **Applied** where the haze is applied (`Haze.glsl`): each lit shader reads
  the volume at its fragment, transparent surfaces and water included. The
  sky and the clouds read it at the volume's far end.
- **The near haze layer** of the volumetric shafts (task 28) goes when the
  fog is on: its haze lying on the ground becomes the thinnest case of this
  volume.

### Far: the march through the fog layer

- Half resolution, from the froxel volume's end to the scene depth or about
  12 km. The ray is cut to the band between the lowest floor and the highest
  fog top on the map, so a view across a valley marches only the stretch
  inside the fog.
- 8 to 16 steps, lit by the sun with the cloud shadows (no object shadows
  that far) and the sky. Upsampled with the depth and blended in at the end,
  as the clouds are.
- Valley fog seen from a hill, and fog banks down the line, come from this
  part.
- Not built (step 3): one volume with its slices spaced in the logarithm of
  the distance reaches 10 km, past the floor map's 8 km, with slices under
  a metre near the camera; the march was not needed.

### Colours and order

- **Fog light:** sunlit fog takes the sun's colour, so at dawn the tops are
  pink and orange while the valleys below stay blue-grey; it is grey under
  overcast.
- **Exposure:** auto exposure (task 29) sees the fog as part of the image.
- **Order along a ray:** the surface is hazed by the dry air first, then
  fogged (fog lies low, in front of what the haze reaches).
- **Signals:** their glow splats are dimmed by the fog in front and widened
  by its scattering.
- **Classic sky and OpenGL:** no change; they keep the env file's fog. The
  OpenRails presets do not get this fog.

### Weather data

- **Per keyframe:** temperature and dew point at 2 m, and an optional fog
  override:

  ```text
  tsre_weather_air ( 12.5 9.0 )             // temperature, dew point (°C)
  tsre_weather_fog ( auto )                 // or: off, or amount visibility depth
  tsre_weather_fog ( 1.0 150 80 )           // forced: amount 0-1, visibility in it (m), depth above the floor (m)
  tsre_weather_fog ( auto 0 )               // after either, the pooling 0-1 (1 if left out): 0 lies evenly
                                            // on all ground as a film, its top's soft edge deep
  ```

- **Presets:** each gets typical values. New presets `morning_mist` and
  `valley_fog` (a clear calm night into a foggy dawn that clears by mid
  morning) serve designers and tests.
- **Import:** keeps the temperature and dew point it already fetches, and
  turns the fog codes into a forced fog instead of a 30 m cloud base.
- **Generator:** moist air ahead of fronts, drier behind cold fronts.
- **Env files** without the block: the preset's values. MSTS has no
  humidity.
- **Editor session:** the Environment panel (F5) can set Auto, Off, Light or
  Thick fog to preview a route, without saving it.

### Settings and cost

- New setting "Mist and fog": Off / Low / High (the grid, the far march's
  steps). Physical sky only; on in Advanced Environment. Off keeps today's
  look, with the near haze layer of the shafts.
- Steam Deck target: 1.5 ms at Low, nothing when no fog is in view (the
  passes are skipped when the potential is zero).

## Steps

1. **The fog from the weather:** the keyframe fields (air, fog override),
   presets, import, generator; the fog state over time (potential, depth,
   visibility, burn-off). Unit tests: a clear calm moist night gives fog at
   dawn, gone by mid-morning; wind, a cloudy night or dry air give none; the
   format round trips.
2. **The floor map:** built on a worker, uploaded, shown in the map view.
   Unit tests on a made-up terrain: a V valley fills to a level top, a
   plateau stays thin, water adds.
3. **The fog near and far, together:** one density and lighting function
   shared by both parts.
   - The froxel volume: compute passes, the sun with its shadows, the sky,
     frames averaged, applied in the lit shaders.
   - The half-resolution march through the fog layer from the volume's end,
     and its composite.
   - The near haze layer of the shafts goes.
   First look: ground fog around the camera and on the line ahead, valley
   fog from hills.
4. **Lamps and signals in fog:** the light grid in the volume; the signals'
   glow. (Done, below.)
5. **Patches and motion:** noise drift, edges, burn-off lifting into stratus.
   (Done, below.)
6. **Setting, preset, panel control, docs;** timing on the Steam Deck at
   each quality.

Later: steam fog over water in autumn, sea fog, fog in the OpenGL renderer
(a height fog in the classic fog), painted fog areas if a route needs one
the terrain cannot explain.

## Step 1 (done, 2026-10-14)

- **Weather data:** `WeatherKeyframe` has `air` (temperature, dew point)
  and `fog` (auto, off, or set: amount, visibility, depth), written as
  `tsre_weather_air` and `tsre_weather_fog`. Files without them take the
  preset's values, or 15 °C and 8 °C.
- **Presets:** each has its air (clear 16/6, rain 10/9, the front damp in
  its rain and drier behind). `morning_mist` (14/8, low wind 2.5 m/s) and
  `valley_fog` (12/9, 2 m/s) are clear, calm and damp; their fog comes from
  the model.
- **Import:** keeps the hourly temperature and dew point. Fog codes (45, 48)
  set the keyframe's fog (the share of samples reporting it, 200 m
  visibility, 80 m deep) instead of a cloud base 30 m up and 300 m
  visibility everywhere; the haze around it is 5 km.
- **Generator:** the air across each front (warm sector 15/12 to 16/13,
  rain nearly saturated, behind a cold front 10/2), 4 °C warmer at 15:00
  and cooler at night.
- **The model** (`WeatherFog`), from the night that made the fog (since the
  last sunset, or the night before the last sunrise; at most 16 h):
  - the night's low cloud (and mid, at 0.7), low wind, and rain in the 12 h
    before (wet ground: 1.5 °C damper);
  - cooling up to 8 °C over a long night: `8 x clear x mixing x (1 - exp(-night / 5 h))`,
    clouds taking up to 80 % of it, wind up to half;
  - the gap left: the evening's spread less the cooling, or the weather's
    own spread at the end of the night where smaller (hourly data);
  - amount: saturation (the gap from 1.5 to 0 °C) times the wind's mixing
    (none above 8 m/s at about 100 m); depth 20 m + 40 m per degree below
    the dew point (up to 250 m), shallower in very light wind; visibility
    from 3 km down to 150 m;
  - after sunrise: the sun's warmth (hours times the sine of its height)
    burns it off, deeper fog taking longer; it thins, lifts (up to half its
    depth), loses up to a third of its depth (drawing back from the slopes)
    and breaks up as it clears;
  - mist in rain: half the amount at 3 mm/h, 2 km visibility, 150 m deep;
    the denser of the two counts;
  - keyframes blend: off, set, or the model's.
- **Results** (tests, a sun up 3:55 to 20:05 in summer, 7:10 to 16:50 in
  winter):

  | Weather, time | Fog |
  |---|---|
  | Morning mist, 4:30 | 100 %, 33 m deep, 890 m visibility |
  | Morning mist, 9:00 and 19:00 | none |
  | Valley fog, 4:30 | 100 %, 136 m deep, 150 m |
  | Valley fog, 7:30 | 41 %, 108 m deep, lifted 26 m, 505 m |
  | Valley fog, winter noon | 71 %, 162 m deep, 323 m (summer noon: none) |
  | Valley fog with 9 m/s wind, overcast, or the clear preset | none |
  | Rain preset | 35 %, 150 m deep, 2 km |

- **Environment panel:** the air, and the fog at the camera now ("set by
  the weather" when a keyframe sets it).
- Nothing drawn yet: steps 2 and 3.

## Step 2 (done, 2026-10-14)

- **`FogFloor`** (environment): 256 x 256 cells of 64 m from the ground
  heights (unknown where no terrain is loaded) and water.
  - Floor: the lowest ground within 1.5 km (a running minimum, van Herk /
    Gil-Werman, along rows then columns), smoothed twice over 5 cells
    (about 300 m), never above the ground.
  - Height above the region: the floor less the lowest floor within 5 km.
  - Moisture: water within 2 cells, three times the share, at most 1.
  - The fog's top: the floor plus the depth times the region's share (all
    up to 30 m above the region's lowest floor, none from 250 m), or the
    ground plus up to half the depth over water and wet ground, whichever
    is higher. About a millisecond for the whole map.
- **`FogFloorSampler`** (world): fills the cells a few world tiles a frame
  (8 of the 81 a 16 km map touches) from `TerrainLib::groundTile` (the
  detailed tile, else the distant one; nothing loaded) and
  `Terrain::waterAt` (patches with drawn water, levels as the water mesh
  takes them).
- **The editor** (`updateFog`): the fog at the camera is computed again
  when the weather's time moves a minute, the camera 1 km or the weather
  changes; while there is fog, a floor map is sampled about the camera, again
  when it has moved 1 km or tiles have loaded or gone (a new availability
  count in `TerrainLib`, also bumped by the distant tiles the drawing
  creates), at most once a second.
- **Map view** (`FogMapLayer`): with the weather layer on, misty blue-grey
  (pale on a dark palette) where the ground lies under the fog's top, the
  more opaque the deeper (full from 30 m), over the clouds.
- **Routes have terrain only along the line:** PeakRail 20 of 81 world
  tiles around Darley Dale, SPAIN-CANFRANC 1, SWITZERLAND 2; their distant
  terrain does not reach further at those places. Fog lies only where the
  ground is known, which is all a camera can see.
- **PeakRail, valley fog at 4:30:** the Derwent valley from Rowsley to
  Matlock and its side valleys under 126 m of fog, the hills out of it; at
  7:30 thinner and drawn back from the side valleys; the morning mist (33 m)
  only along the river and the valley floor.
- `TSRE_FOG_LOG=1` logs each floor map built (cells known, ground and floor
  heights, cells under fog, tiles detailed, distant and missing).

## Step 3 (done, 2026-10-14)

- **Setting** `core.rendering.mist` (Mist and fog): Off, Low, High; kept by
  the editor (as lamps by day) and passed to the renderer with the fog
  (`Renderer::Fog`: the fog now, the floor map's cells, their revision);
  Advanced Environment sets High.
- **The volume** (`RhiFog.cpp`): one per frame for the main view, x and y by
  the screen (High a cell of 8 x 8 pixels, Low 16 x 16), 64 slices spaced in
  the logarithm of the distance from 1 m to 10 km (at 10 m a slice is 1.5 m
  deep, at 100 m 15 m, at 1 km 155 m). Two compute passes after the cloud
  shadow map, before the view's geometry:
  - injection: each cell's fog (the floor map, `FogFloor::top`, a soft edge
    of 15 % of the depth, 3 to 20 m) and its light, blended with last
    frame's cell at that point (0.9 on High, 0.85 on Low; dropped after a
    50 m jump), sampled at a Halton point in the cell (its middle in
    captures, with the clouds still);
  - integration: each column front to back, light added up to each slice's
    end and the share let through (Hillaire's energy-conserving step).
  Vulkan and Direct3D 11 (compute and 3D storage textures); elsewhere off.
- **Its light:**
  - the sky's: the horizon's colour of the aerial haze in that direction
    (linear): far fog meets the hazy horizon without a seam (the first try,
    the lit shaders' ambient, made fog darker than the sky it hid);
  - dimmed deeper in the fog (half at the floor of a thick one);
  - the sun's or the moon's (the lit shaders' light): through the fog above
    the point towards it, the clouds' shadow map and the three sun shadow
    maps (the near one, else the middle, else the far, as the lit shaders);
    0.7 of a Henyey-Greenstein lobe of 0.85 and 0.3 isotropic, times 0.8:
    a warm glow around a low sun, the fog's own shadows faint beams.
- **Applied:** in `applyHaze` (Haze.glsl), at each lit fragment, the fog
  first and the air over it (far fog banks turn blue with the distance);
  the glow through it with `hazeThrough`. The sky and the clouds behind the
  fog: a pass after the clouds' composite, the far end of each column
  mixed over pixels at the sky's depth. Secondary views (the water
  reflection, the environment cube) have no volume.
- **The sun under the fog** (ShadowSampling.glsl, `cloudShadow`): every lit
  surface's sun goes through the fog above it along the sun (the floor map
  in the lit shaders): under thick fog at a low sun no shadows, lifting fog
  dims the ground under it.
- The near haze layer of the volumetric shafts is off while the fog is on.
- **PeakRail, valley fog** (captures, High): inside the fog at dawn a pale
  beige-grey, the trees faint; from 150 m up a sea of fog over the valley
  under a blue sky, the far ridge out of it; 60 m up towards the rising sun
  its disc pale through the fog with a warm glow; at 7:00 the fog lifted
  26 m, the ground under it dim, the trees fading into it; at 8:00 thin
  remnants over the valley; at noon none. Morning mist at 5:00: a low mist
  over the fields under a blue sky, the sun glowing behind the trees.
- **Low against High:** the same in these views (mean under 0.5 of 255;
  at most 36 on the fog's edge against the hills from above). The fog's
  shadows (trees in front of the sun) give at most 14 of 255 in thin fog.
- **Cost on the Steam Deck** (1280 x 800, Advanced Environment, frame
  medians of three rounds): Low about 0.5 ms, High (64 slices) about 1 to
  1.5 ms in thick fog, under 1 ms seen from above; with 128 slices High
  took 2 to 3 ms. Nothing without fog (the passes are skipped).
- **Fog off is pixel-identical** to the merge's captures (the CMK preset
  views, fog density 0.3 as then).

### Flashing mist (user, 2026-10-14)

- CMK at 4:15, morning mist, looking into the rising sun: the mist flashed
  dark and bright fast. 40 frames in a row: a mean change of 2.8 of 255 a
  frame, a quarter of the pixels unlike both neighbours (fog off 0.12;
  the clock stopped, the same; with the cells' jitter fixed it settled
  after a few frames and stayed still).
- The cause: each cell took its fog at one point, jittered each frame over
  its width and its slice's depth. The mist is 30 m deep and seen at a
  grazing angle; at 1 km a slice is 155 m long, about 10 m of height, so
  the point crossed the mist's top and switched the cell's fog on and off.
- Now each cell's fog is the mean of four points along its slice, shifted
  each frame, on the cell's middle ray; its light at one point (shifted along
  the slice); the history 0.95 (was 0.9 High, 0.85 Low). The mean change is
  0.04 a frame, nothing flashing, High and Low; the look the same. Cells
  whose slice lies above the highest fog top on the map take no fog at once
  (the same images). High about 1.5 to 2 ms in thick fog on the Steam Deck,
  Low about 0.5 ms.

### The terrain's shade of a low sun (user, 2026-10-15)

- EUROPE1 at 20:00, clear, the sun 3.3 degrees up behind a hill about
  1 km off: the volumetric light shafts lit the haze in front of the hill
  in a bright glow with rays from under the ridge, as if through it; also
  in main (mist off the same). The fields there were lit too.
- The cause: the sun's shadows are the shadow maps', which hold what
  stands within 700 m of the camera (the far map: ±700 m, casters to
  1 km). The shafts march 1.5 km (6 km over the sky); past the map every
  point counted lit, and a hill farther off cast no shadow at all.
- **`TerrainShade`** (environment): over the floor map's grid (16 km, cells
  of 64 m), the height under which the ground towards the light hides it.
  - From each cell's lowest ground (`Terrain::lowestHeight`: its height
    samples and corners), so a valley narrower than a cell stays open.
  - Lines of cells from the light's side (along the axis nearer the
    light's): a cell's top is the higher of the ground and the top one step
    towards the light (the lower ground of the two cells it lands between,
    their tops blended), less the light's fall over the step.
  - A cell lit down to its lowest ground keeps as its top the lowest ground
    within a cell of it: a lit valley's sides (their tops just under their
    own ground) walled it in as the tops blended between cells (a test:
    a one cell valley running towards the sun).
  - The light fades over 4 m under the top (the sun's disc past a ridge
    1 km off).
- **Read by** the lit shaders (`ShadowSampling.glsl`, with the cloud
  shadow), the volumetric shafts' march and the fog's light (both past the
  shadow maps), as a 256 x 256 half float texture from a reference height
  near the camera's ground. The sun's light, or the moon's at night.
- **Made** with shadows, the volumetric shafts or mist on: the floor map is
  now sampled for it too, without fog. Made again when the floor map is new
  or the light has moved 0.03 degrees (a ridge 1 km off moves its shade
  half a metre).
- **Cost on the Steam Deck:** the floor map built in 5.8 ms in one frame
  (since step 2, with fog only) and the shade in 1.2 ms; both now on a
  worker thread, the last ones used meanwhile. The tiles are sampled for
  up to 1.5 ms a frame (8 a frame took 7 ms where all had terrain); the
  lowest ground adds about 0.25 ms a detailed tile.
- **EUROPE1 at 20:00:** the glow and the rays through the hill are gone;
  the haze over the ridge still glows. Facing east the slopes and the
  viaduct facing the sun darken a little (at 3 degrees the sun lights
  them faintly). At 19:30, the sun on the ridge, the rays from under it
  are gone. At noon nothing changes; CMK at sunrise next to nothing (flat
  around the line). `TSRE_NO_TERRAIN_SHADE=1` leaves it out (comparisons);
  `TSRE_FOG_LOG=1` logs the sampling's time and the light at the camera.
- Left for later: the aerial haze's own glow towards the sun (task 32)
  assumes a lit path; with the volumetric shafts they take it back where
  the march finds shade, without them it stays.

### Night: a turn brought brighter fog into view; banding (user, 2026-10-15)

- CMK at 3:00, valley fog, the clock running: after a while a turn showed
  the newly seen part of the fog much brighter than the rest; colour
  banding in the fog and on the sky.
- The cause: the cells hold their light in half floats, in the scene's
  linear scale. At night the scene's light is 0.03 of the day's (auto
  exposure), and a cell's light per metre was a few of the half floats'
  smallest steps; a frame's share of the new light (1 - 0.95) rounded to
  nothing, so the cells kept what they had while the dawn brightened.
  Newly seen cells, without history, showed the light as it was. The few
  steps were the banding too. Reproduced with the time moved from 3:00 to
  3:15 and 200 frames still: the fog 32.4 against a fresh render's 36.2
  (mean of 255).
- Now the cells keep their light scaled by 16 / (the scene's light x the
  fog's extinction at the floor), the sums by 16 / the scene's light (a day
  in thick fog by the sun stays under 3000, a dark night above the half
  floats' smallest normal); the history is scaled from last frame's to
  this one's, the lit shaders and the sky's pass scale the sums back. The
  same test: 39.1 against 39.1, the turned view's new and old parts as a
  fresh render's. Night fog is also brighter than before (the steps had
  rounded it down): at 3:00 a mean of 20.4 against 16.3.
- The rest of the banding (the night sky, at six times the contrast) was
  the 8 bit frame: the final pass adds a triangular dither of one step,
  fixed on the screen.
- The capture views can now settle on their own (one frame after a turn).

### Beams through trees, a low sun (user, 2026-10-15)

- Measured at PeakRail's start, 2 m up in the morning mist, the sun beside a
  big tree: the fog's shadows (beams) at 5:00 a mean of 2.1, at most 19 of
  255; at 5:20 at most 71. In thick valley fog none: a sun 10 degrees up
  crosses hundreds of metres of it to any point (as it should).
- Not the sky's light (the horizon's colour towards the sun, unshadowed): an
  even sky light gave 15 % more.
- The sun's: the fog took the lit shaders' sun, which fades by the horizon
  as a look (`Daylight`, at 10 degrees 0.044 against the sky's 0.25 linear).
  Now the brighter of it and the day's sun through the air towards it
  (`Atmosphere::sunLight`; two to three times at 10 degrees, less red): at
  5:00 the beams a mean of 4.1, at most 37, rays from the tree into the
  field, a warmer glow around the sun (the view a mean of 7.6 brighter);
  from 7:00 on the same images (at most 1 of 255).
- Left: rays thinner than a cell (8 pixels) blur; streaks along the lines
  to the sun (as the screen-space shafts) would sharpen them.

## Step 4: lamps in fog (2026-10-15)

- **The lamps light the fog:** the injection reads task 21's grid of lamps
  (the lit shaders' `LocalLights.glsl`: the same lamps, strengths, ranges
  and spot cones; signals are not in it), as strong as the lit shaders have
  them in the open (`localLightDay`), with the sun's phase towards each
  lamp: halos about lamps, light pooled in the fog under them.
- **Summed along each cell's slice exactly:** a lamp's inverse square from
  the slice's start to its end has a closed form (the arc tangent of the
  ends over the ray's distance from the lamp, softened by the emitter's
  size); its fade to the range, cone and phase are taken at the slice's
  point nearest to it. Taken at one point of the slice, as the sun is, a
  slice 30 m deep missed the few bright metres by a lamp (CMK, 200 m off:
  at a thousand times the lamps' strength the halos showed as they should).
- **The grid's cells at the slice's two ends,** the second's lamps not
  counted twice: at one point of the slice, lamps the slice passed in the
  next cell were left out, the halos cut straight along the cells' edges.
- The cells' light is kept under the half floats' largest (next to a lamp
  at night, with the cells' scale, it went past them).
- **CMK at 3:00, valley fog** (the user's camera with signals and two glTF
  lamps 200 m off): soft halos about the lamps, the platform's fog lit
  under them; at most 104 of 255. The halos are a few cells wide there (8
  pixels each): blocky up close at four times. Frame times the same within
  their spread (about 1 ms) on the Steam Deck.
- Capture files can hide the editor's helpers (`hideAll`, as 3D View >
  Unselect All).
- **Signals light the fog too** (a try the user may take back; their glow
  splats are as they were): their emitters (the splats', task 26) binned
  into a grid of their own, read by the injection as the lamps' are; the lit
  shaders do not see it. Each reaches a lamp's usual 30 m. As the splat's
  even emitter of the lens's brightness a signal's halo did not show
  (colour about 0.0025, as a dim lamp's); a lens gathers its light into a
  beam along the track, many times brighter along it: at 50 times a soft
  halo of its colour, the fog about the signals tinted (at most 66 of 255 at
  the user's camera). Reaching as far as they show with the exposure, as
  lamps do, bright halos lowered the exposure, shortened the reach, and the
  halos went.

### Lamps' light in the fog strobing and dragged (user, 2026-10-17)

- Moving or turning the camera in fog with bright lamps (glTF lamps, the
  train lights' beams of task 34) broke the frame: the lit fog dragged with
  the camera, parts of it dark.
- Three fixes made it worse and were reverted (user, 2026-10-17):
  - `328b2c5`, the lamps' light in a volume of its own, outside the
    history, through the cells' averaged density: newly seen parts of the
    view came in black and lit up over about a second.
  - `1679e64`, through this frame's density: the lamps strobed.
  - `3dd9094`, through the density at sixteen fixed points: still
    strobing, everywhere.
- The cause, found once the capture harness stopped hiding it: the
  signals' fog grid (the glow emitters of signal and train lamps,
  `prepareSignalGrid`) was gathered in the fog's preparation, from the draw
  queue the views drawn before it had already taken: empty in most frames,
  full only in frames without those views first (the capture's pointer
  read; in the editor the frames reading the pointer). So the fog had the
  signals' and train lamps' light only now and then: through the history a
  weak halo left behind, fresh each frame (`1679e64`, `3dd9094`) a strobe.
  The glTF lamps' grid is gathered with the lamps (`prepareLights`) and
  stayed steady (user: "in your fix, glb lamps doesn't blink").
- Fix: the signals' grid gathered with the lamps' in `prepareLights`, every
  frame. The flush `beginSelection` makes of the view no longer prepares
  the clouds and the fog either: it took the fog's history from a pass
  without the view's items.
- Harness: a capture's probes (a selection pass and a pointer read per
  view) are frames of their own between a sequence's views, and the only
  ones that had the signals' grid; `"probes": false` leaves them out of
  motion sequences.
- Captures (CMK, 1:00, valley fog and ground mist, the user's profile, the
  clock running): 40 frames from a still camera within 0.01 of 255 of each
  other, probes on or off; at the user's view the camera turned 0.05 rad a
  frame, 16 frames right and 32 back left: frames at the same angle from
  the two directions within 0.4 of 255 on average (neighbouring angles 5),
  no dark parts.
- The blinking gone, the user (2026-10-17) still saw new stripes not
  matching the old part when moving the camera up (CMK, 0:01, valley fog,
  at the train's head, the headlights shining at the camera); the sun the
  same, but only with the clock running fast. The lamps' light scattered
  towards the camera depends on the direction it is seen from (the
  phase's 0.85: mostly forward), and the history keeps a point's light as
  seen from where the camera was. A turn leaves the directions from the
  camera to every point as they were (the turns above showed nothing);
  moving changes them, the more the nearer the point. Captures, the camera
  rising 0.3 m a frame there: each moving frame against the same view
  settled, mean 16 of 255, at most 114 (a column of lit fog under the
  lamp, from the fog in front of the camera).
- Fix: the lamps' and signals' light out of the history again (`328b2c5`
  brought back), this frame's, through the cells' averaged density, in a
  volume of its own that the integration adds; the sun and the sky keep
  the history. The same captures: rising mean 1.6 at most 3 (the
  exposure's lag in the first frames), at most 1 from the tenth frame;
  turning at most 2 (19 before); a still camera steady (0.01); settled
  views as before (at most 2). The newly seen black parts the user saw
  with `328b2c5` the first time did not show in these runs; then the
  signals' grid was filled only now and then. The user to confirm on
  screen.
- Lamps shining their own way (user, 2026-10-17): a lamp's glow lit the
  fog all about it, behind the car's body too, and a signal seen from
  behind showed its aspect from afar like one facing the camera. The
  emitters of signal and train lamps are spots now, facing the way their
  lenses do with a cone of 90 degrees: in fog by the cosine squared off
  their axis, none behind (`gridLight` takes spots already); their glow
  splats by the cosine, a flat lens's area seen (`RhiImage.cpp`). A train
  lamp's emitter takes its lenses' direction. Captures (CMK, 1:00, ground
  mist): two signals from 25 m in front glow and light the fog, from 25 m
  behind dark (only signals further on that face the camera show); the
  locomotive's red lamps, which glowed through the car's end when seen
  from its head, are gone there; its head lamps' halos narrower.
- A beam seen from afar lay in slabs moving with the camera (user,
  2026-10-17, CMK, 0:00, valley fog): a lamp's fade, cone and phase were
  taken at the slice's point nearest to it, so a spot's narrow cone was in
  or out for all of a slice tens of metres deep (the history had blurred
  it while the camera moved). Now each lamp's part of the slice is cut to
  its reach and its cone (a ray against a cone of up to 90 degrees), and
  the fade, the cone's soft edge and the phase are taken at four points
  over it, each standing for an even share of the inverse square
  (equiangular, as seen from the lamp). Captures, the camera coming 1 m a
  frame towards the beam: an even cone in every frame, its middle's
  brightness changing smoothly (before, the part by the lamp jumped
  between 14 and 25 of 255); brighter, the cone no longer missed. Left: by
  the lamp, surfaces next to it take the fog between the volume's slices,
  which move with the camera, a few levels from frame to frame.
- The sky's fog in linear light (user, 2026-10-17, CMK, 3:10, ground mist,
  the headlights shining at the camera: far roofs and a tower flat grey
  against a dark sky, a sharp line). The lit shaders add the fog's light
  in linear light; the sky's pass mixed the fog's own colour by its share
  in display colours, which for a thin fog lit brightly adds a fifth as
  much: the roofs took the headlights' light in the mist, the sky over
  them hardly any. It now reads a copy of the view and adds the fog as the
  lit shaders do. The roofs and the sky over them meet softly in the mist;
  dawn fog (valley fog at 5:00 and 6:00, ground mist at 7:00) as before
  (mean below 1 of 255).
- Cost (Steam Deck, offscreen, 1280 x 800, four views of these): within
  the runs' spread (medians within 1 ms, the same build's runs 1.5 ms
  apart).

## Step 5: patches and motion (2026-10-15)

- **Patches:** value noise at 600, 240 and 90 m (weights 0.55, 0.3, 0.15)
  in world metres, wrapped every 36 km (each scale repeats over it exactly,
  hashed modulo its period, so the floats keep a centimetre and the wrap
  does not show). The fog's top swells by it, a deep fog's by about 13 % of
  its depth, a patchy one's by up to 30 %; a patchy fog thins into holes
  where the noise is low (`smoothstep(0.3, 0.6)`, as much as the
  weather's patchiness: 0.25 for a fog saturated well past the dew point,
  1 for a marginal one or one burning off). A hole goes through the fog
  above too, so the sun reaches the ground under it.
- The lit shaders' sun under the fog (`fogSunThrough`) takes the same noise,
  the highest-top skip the swell's margin.
- **Motion:** the patches drift with the weather's low layer (its drift,
  about 100 m up, by 0.4 for the wind near the ground), the three scales
  at 1, 0.6 and 1.4 times it, so the patches change as they move; with the
  weather's clock (a timelapse faster, a stopped clock still).
- **Cost:** the noise once per cell, at its slice's middle (at each of the
  four density points about 1 ms on the Steam Deck; now within the frames'
  spread).
- **PeakRail, valley fog** (from 150 m up): at dawn (patchiness 0.25) the
  sea of fog's top gently uneven (a mean of 1.6 of 255 against a flat one);
  at 7:00 and 8:00, burning off, the remnants lie in patches with gaps
  between (5 to 6). Forced to patchiness 1 at dawn the top dips to the
  trees in places, at 7:00 a hole opens onto the sunlit valley.
- Lifting into stratus stays as step 1 has it: burning off, the fog lifts
  off the ground and thins from its top, its patchiness the burn.

### Rings looking down on a thin fog; High per pixel (user, 2026-10-15)

- CMK at 3:30, valley fog, from a hill 130 m up looking down: rings about
  the camera over the fog, like waves; and a turn brought fog of another
  brightness into view.
- **The thin fog:** the fog's top is the higher of the pooled top and the
  ground (plus up to half the depth on wet ground), and its soft edge
  (15 % of the depth, here 18 m) lies across it: wherever the weather has
  fog, a film up to 18 m deep, half the fog's density at the ground, lies
  on the ground (there the only fog: bad tiles to the north-west, flat at
  1 m and down to -35 m, made the place stand high above its region, and
  nothing pools). It looks good and stays, now on purpose.
- **The rings:** seen from above, each pixel's share of the film depends on
  where its ray meets the ground; the volume's columns (8 pixels) and
  slices (50 to 70 m a few hundred metres off) cannot follow that, and its
  slices showed (as finer, weaker rings with 128 slices). Not the
  density's sampling (16 points the same), the light's fall into the fog,
  the jitter or the history: found by drawing the volume's light and
  share let through alone, and a surface's place in its slice. The noise's
  swell made them waves.
- **High (Mist and fog):** a surface seen from above the fog takes the fog
  its ray crosses from a slice before the fog's top over its place down to
  it per pixel, in four steps, each over its own place of the floor map,
  the patches as at the surface; before it the volume's; its light the
  volume's fog's in front of the surface on average (smooth where the
  amounts step). Off near the ground's level (rays descending under 2
  degrees, or the camera within 5 to 30 m of the fog's top there), where
  the volume works. **Low** keeps the volume alone, with the rings.
- **Cost on the Steam Deck,** looking down on the film over the whole view:
  first 6 ms (the fog asked for two or three times a fragment, eight
  steps, the noise twice); now about 1 ms (the answer kept for the
  fragment, four steps, the noise shared with the sun under the fog).
  Nothing at the ground's level.
- **The turn:** with the clock stopped a turn's new part is within 2 of
  255. Running at ten times, the patches' drift smeared the history (the
  three scales drifted at their own paces); now they drift together and
  the history follows them, the fog's state from the volume up to a slice
  before the film: the new part and the rest within 1 to 3 of a fresh
  view's.
- On the way: within a slice a surface's fog falls exponentially (it was
  blended evenly, up to a third out in a thick fog's slice); the patches'
  noise is continuous along each ray (blended between the slice's ends).

### Ground mist; none deep in the ground (user, 2026-10-15)

- **Pooling:** the fog setting of a keyframe has a pooling, 0 to 1 (1 if
  left out of the file): how much of the depth pools in valleys. At 0 the
  fog lies evenly on all ground as the film its top's soft edge makes
  (15 % of the depth, up to 20 m), whatever the terrain. Carried by the fog
  state, blended between keyframes; the renderer, the map view and the
  floor map's log take it.
- **Ground mist** (preset): valley fog's night with pooling 0: the look of
  valley fog at CMK while the bad tiles kept it from pooling there, which
  the user liked, now anywhere. Valley fog pools as before (at CMK too,
  once the tiles were left out: below).
- **None deep in the ground:** the fog goes from 15 m under the floor map's
  ground, gone at 25 m (the map's cells are 64 m: a cutting narrower than
  that lies under it and keeps its fog). Tunnels under hills and deep
  stations stay clear, shallow ones keep a little; in the volume and in
  High's per-pixel fog alike.

### Wrong terrain tiles (user, 2026-10-15)

- **The tiles:** north-west of CMK's lamps, -5352, 14857 lies flat at 1 m
  (a blank tile) and -5351, 14857 slopes from it down to -35 m and up to
  40 m, among ground at 200 to 350 m. The floor map took them for the
  region's lowest ground: everything for 5 km around stood high above its
  region, and valley fog pooled nowhere there. They are the route's to
  fix; the fog now leaves them out meanwhile, warning once a tile:
  `Terrain tile -5352, 14857 lies far off its neighbours' at their edges;
  the fog's floor leaves it out`.
- **The check** (`FogFloorSampler::outlierTiles`), once all tiles are
  sampled, before the map is built: terrain is continuous across tile
  edges, so two tiles agree if their cells either side of the edge (64 m
  apart) differ by 80 m or less on average. Tiles that agree make pieces of
  terrain; the largest is trusted, and a piece apart from a larger one at
  any edge is wrong (a blank tile, a pair sunk together, a blank tile whose
  only loaded neighbour is the sloping one), unless that larger one is
  wrong itself. A seam between two tiles joined around it drops nothing;
  nor does a tile with no known neighbour, or two tiles apart and alone.
  A slope of 37 degrees along a whole edge stays (unit-tested); a steeper
  cliff the whole way round a piece would be taken for wrong.
- **Left out:** the wrong tiles' cells count as without terrain for the
  fog's floor and the terrain's shade only; drawing and the terrain are
  untouched. `TSRE_FOG_LOG` adds how many were left out to each map's log.
- **What changes at CMK:** valley fog pools there now. It is 120 to 130 m
  deep at night, more than CMK's relief: it fills the land, the lamps' spot
  in thick fog, the hill of the 3:30 view under it, and seen from above it
  is a flat sea of fog. Ground mist keeps the old look. Kept so (user):
  too deep for a flat route, but other presets suit those; later the
  weather generator could let the depth be set.

## Verification

- **Captures** on hilly routes with valleys and rivers (POLAND, BNSF_SCENIC,
  EUROPE1), aimed at the sun where the sun matters:
  - from a hill over a valley at sunrise (a sea of fog, lit tops);
  - in a valley on the line (visibility about 200 m, the track fading);
  - at 9:00 and 11:00 (clearing);
  - a windy morning and a cloudy night (no fog);
  - at night with lamps and signals in fog;
  - pines with a low sun behind them in mist (beams).
- **Motion:** turning and driving without crawling or smearing; the floor
  map's swap without a jump.
- **Timelapse:** the fog forming over the night and clearing, smoothly.
- **Unit:** the fog state, the floor map, the format.
- **Steam Deck:** time per frame at each quality, with and without fog in
  view.

## References

- Wronski: "Volumetric Fog: Unified compute shader based solution to
  atmospheric scattering", SIGGRAPH 2014 (Assassin's Creed IV)
- Hillaire: "Physically Based and Unified Volumetric Rendering in
  Frostbite", SIGGRAPH 2015
- Bauer: "Creating the Atmospheric World of Red Dead Redemption 2",
  SIGGRAPH 2019 (fog and clouds in one system)
- Koschmieder's visibility (3.912 / extinction), as task 32
- WMO, International Cloud Atlas: fog and mist
- Gultepe et al.: "Fog Research: A Review of Past Achievements and Future
  Perspectives", Pure and Applied Geophysics, 2007
