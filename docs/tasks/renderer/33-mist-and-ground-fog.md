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
  moved 1 km, swapped in when complete (see step 2).
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
   glow.
5. **Patches and motion:** noise drift, edges, burn-off lifting into stratus.
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
