# Task 32 - Aerial Haze

Status: implemented (2026-10-13, `feature/clouds`).

## Objective

Distant terrain seen through the air as it is: dark mountains blue and
darker than the sky behind them, farther ridges lighter, with any time of
day and weather. The user's old renderer got this with a dark blue fog
colour and a fog that stopped at its density setting, tuned for one light;
with the physical sky the fog took the horizon's averaged colour, and at
full density mountains were sky-coloured, at noon brighter than the sky.

## Before

- Fog (`StandardFog.vs`): linear in the horizontal distance over 1.4 view
  distances, capped at the fog density setting (0.7: 70 % reached at about
  2.9 km with a 3 km view distance, the same for a ridge at 5 and at
  40 km), one share for every colour.
- Its colour: the fog colour setting, or with a physical sky the horizon's
  colour averaged around it (`Atmosphere::horizonColour`), the bright side
  towards the sun included.
- Mixed in display space; the weather's visibility unused (the clouds'
  haze takes it since task 28).

## Why far mountains are dark blue

The air between the eye and a mountain dims the mountain's light and adds
its own, scattered sunlight, blue most (Rayleigh, about the fourth power of
the frequency). A dark mountain (forest, a slope in shade) sends little of
its own, so what is seen is mostly that blue light. It stays darker than the
sky at the horizon, which is the light of hundreds of km of air, while the
mountain has tens: each farther ridge lighter. Blue also fades first from
the mountain's own light, so bright far things (snow, clouds) turn warm or
white. Clean air gives deep blue; aerosols (humid or hazy air) scatter all
colours alike and turn it grey. At a low sun the air in a mountain's shadow
is unlit too (not modelled here).

## Design (`Haze.glsl`)

- What the air lets through, per colour: exp(-sigma d), sigma the
  atmosphere's Rayleigh (scale height 8 km) and Mie (1.2 km) at sea level
  and the weather's haze (1.5 km), the same air as the clouds' (`airTo`),
  each layer's density averaged exactly over the straight path between the
  camera's and the surface's altitudes.
- The air's own light: the horizon's colour in that direction, a ring of 8
  colours by the angle from the sun's direction across the ground
  (`RhiRenderer::setSky`: `skyRadiance` 2 degrees up, as the fog colour's,
  drawn again when the sun moves about a hundredth of a degree or the camera
  20 m), greyed under an overcast as the fog colour is, at least the night
  sky's floor, in the scene's scale (auto exposure).
- Mixed in linear light: (display^2.2 T + air^2.2 (1 - T))^(1/2.2). Under
  cover the air's light is scaled as the ambient is (no bright haze deep in
  a tunnel), water over its bed takes the bed's share less (as before).
- Glow (bloom) of surfaces and the lamps' glow splats are dimmed by the same
  transmittance; the ambient occlusion share likewise.
- The fog density setting scales the weather's haze (terrain, clouds and
  light shafts alike, `sky.haze`): (density / 0.7)^2, 0.7 the weather's own, 0
  clear air (Rayleigh and Mie only), 1 about twice. Default 0.3, about a
  fifth (user, 2026-10-14: above it the far mountains' blue goes; it was
  0.7, the classic fog's).
- Only with a physical sky on QRhi and not in the map view (`hazeParams.z`);
  otherwise the classic fog as before, and the sky band never.

## Results (2026-10-13)

POLAND, camera 15,-133,343.48,850.02,-275.01,-1.87,2.81 (the user's terrain
textures), `build/haze/POLAND/grid-*.png`:

- Clear (visibility 40 km) at noon, 7:30 and 18:00: the range dark blue
  against the sky, farther ridges lighter, the snow on the peaks bright;
  before a pale band of the averaged horizon colour. Density 0.35: deeper
  blue; 0: deepest, the air's Rayleigh blue alone.
- 20:30 (dusk): dark blue-grey silhouettes against the twilight; before a
  reddish band of the fog colour. 21:15 and 23:00: as dark as before.
- The env file's weather: its `world_fog_distance` (2000 in POLAND's
  editor.env) is its visibility, 2 km: at the default density the town
  fades at a few km and the range is gone; at 0.35 (a quarter, about 8 km)
  it shows faintly, at 0 clearly.
- CMK, broken (visibility 25 km): at the track the near scene as before,
  the far fields hazier; from 550 m looking down a veil over the whole
  ground, the 25 km haze lifting the dark ground already at 1 to 2 km
  (mixed in linear light it lifts dark colours more than the old fog's
  display mix). The river and its reflection as before.
- Cost on the Steam Deck (POLAND view, 60 frames): fastest frames 16.1 and
  15.2 ms with the old fog, 15.9 and 15.0 with the haze.
- Suites: rhi-shaders, sky, weather, time-of-day, water-gl,
  environment-map-gl, local-lights, map-view, editor-tools,
  consist-preview-gl, environment-window, settings, translations.

## The env file's visibility (2026-10-14)

MSTS `world_fog_distance` values were set for MSTS's view of about 2 km;
read as visibility they make the env files' weather very hazy. POLAND's
editor.env (2000) at the density 0.3, 7:30, 12:00 and 18:00
(`build/haze/POLAND/envvis-comparison.png`): as read (2 km), the range
faint blue-grey and the near town hazy; scaled ten times (20 km) and the
fair cumulus preset's own (40 km) nearly the same, the range dark blue. Left
as read for the user to choose.

## Later

- The air in mountains' shadows at a low sun (a march along the terrain's
  height towards the sun), and an aerial perspective volume (Hillaire 2020)
  for flights above the haze and an exact match with the sky.
