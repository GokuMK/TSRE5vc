# Task 31 - Moonlight

Status: implemented (2026-10-13, `feature/clouds`). Full moon 0.15, the clouds
0.18 of it (user: after 0.2 with moonlit clouds still strong; the clouds' lit
surfaces then still too strong at a quarter).

## Objective

The moon lights the night: moonlit nights differ from moonless ones, with
the moon's phase and height, and the moon casts shadows. In every mode
(Dimmed, Full strength, Auto exposure), with time of day on.

## Today

- The moon is drawn on the sky (`MoonPosition`: direction, elevation,
  illuminated fraction; `SkySatellites`) but lights nothing.
- The night's light is a constant bluish ambient (`Daylight::forElevation`,
  `NightAmbient`), the same at full moon and new moon; the sun's diffuse
  light is gone 1 degree under the horizon.
- One direction, `Game::sunLightDirection`, is both the light of the
  shading (`lightDirection`) and the sun of the sky (the sky's sun and the
  sky satellites take it). Shadows take their own direction
  (`shadowSunDirection`) and are cast while `sunCastsShadows`.
- Cloud shadows, light shafts and the clouds' light follow the sky's sun.

## Design

- Moonlight (`Daylight::moonlight`): a directional light in display units
  as the sun's (0.7 by day). Its strength: full moon high up
  `FullMoonLight` 0.15 (0.1 and 0.2 tried, see Results), times the phase (illuminated fraction to the
  power 2.5: a half moon about a fifth of a full one, physically about a
  tenth), times the moon's height (as the sun's, from 1 degree under the
  horizon to 20 degrees up), times the night (from the sun 1 degree to 8
  degrees under the horizon, after the sun's own light is gone, so the
  direction changes when both are dark). Bluish white.
- The moonlit sky: the ambient gains a quarter of the moonlight, bluish.
- Without a moon the night ambient stays as the floor.
- With the moon up at night, the light of the shading and the shadows take
  the moon's direction (shadows with the sun's minimum elevation); the sky,
  the sky satellites, the clouds, cloud shadows and light shafts keep the
  sun's.
- Auto exposure: night views stay at the night's exposure (the adaptation
  curve is flat there), so moonlit nights show brighter than moonless ones.

## Results (2026-10-13)

CMK, the user's camera (-5350,14859,-778.45,278.47,-208.37,82.16,-5.60),
clear sky, 22:00. Full moon 3 January 2026 (51 degrees up, 99.6 % lit,
strength 0.99), first quarter 26 January (38 degrees, 57.5 %, 0.25), new
moon 18 January (below the horizon). Mean brightness of the lower 40 % of
the picture (0-255):

| | No moonlight | Moon 0.1 | Moon 0.2 |
|---|---|---|---|
| Full moon, Auto exposure | 1.2 | 7.1 | 16.0 |
| Full moon, Dimmed | 0.5 | 2.7 | 6.3 |
| Half moon, Auto exposure | 1.2 | 2.4 | 3.9 |
| New moon | unchanged | unchanged | unchanged |

At 0.2 a full moon shows fields, trees and track as a moonlit night; 0.1
left them barely readable. The CMK underground station is unchanged (fully
covered); in the hall the roof keeps the moon off the tracks, the lamps
dominate, the open sides and the roof's edges are moonlit. By day the
picture is unchanged (mean difference 0.006); at 20:30 (the sun 1.4 degrees
under the horizon) the ground gained one level of 255. Suites: time-of-day
(two new cases), rhi-shaders, local-lights, sky, water-gl,
environment-map-gl, editor-tools, map-view pass.

## Moonlit clouds and cloud shadows (2026-10-13)

- The clouds have their own light (`RhiRenderer::cloudLight`): the sun,
  also under the horizon while it still reaches them (high clouds see it to
  about 3 degrees under; the afterglow), until 5 degrees under; then the
  moon, fading in to 9 degrees under. The moon's light on them is the
  scene's moonlight relative to the sun (linear, `Renderer::Sky::moon`,
  `moonLight`), times `CloudMoonlight` (a quarter, then 0.18). The march, the flat high
  layer and the cloud shadow map take that light's direction and colour
  (through the atmosphere's transmittance: a low moon reddens); the sky's
  light on the clouds keeps the sun (its table is built around the sun).
- Cloud shadows follow the same light: the moon's at night, so the moonlit
  ground is patched by the clouds.
- CMK, the user's camera, full moon 3 January 22:00 (Auto exposure; mean
  of the upper 30 % and the lower 40 % of the picture):

  | | Sky | Ground |
  |---|---|---|
  | Clouds unlit, no cloud shadows (before) | 4.4 | 21.1 |
  | Moonlit clouds, full moonlight | 95.1 | 7.4 |
  | Half | 68.5 | 7.4 |
  | Quarter (kept) | 46.3 | 7.4 |

  Broken clouds; scattered alike (85 / 60 / 39, ground 18.7 to 5.7). In full
  the cloudy sky read as a grey overcast dusk. The ground drops under the
  cloud shadows (auto exposure keeps the night's exposure). The dark navy
  patches in the clouds are gaps in the deck, the night sky behind.
- Unchanged: a new-moon night (the clouds unlit), the afterglow at 20:30
  (pixel-identical), the day.
- Then the moon itself lowered to 0.15, the clouds keeping their quarter
  (user: the full moon still strong; the clouds too bright without the
  quarter). Full moon, clear: ground 16.0 to 11.3; half moon 3.9 to 3.1;
  broken clouds: sky 46.3 to 30.5, ground 7.4 to 6.1; scattered: sky 38.7 to
  24.7, ground 5.7 to 4.7.
- The clouds then lowered to 0.18 of the moonlight (user: their lit surfaces
  still too strong): broken sky 30.5 to 24.1, scattered 24.7 to 19.1; the
  ground unchanged.

## Later

- A brighter physical sky at full moon; moonlit scenes bluer and less
  saturated (the eye's night vision).

## Verification

- CMK, clear sky, 22:00 on dates of a full, a half and a new moon, Dimmed
  and Auto exposure, against the build without moonlight.
- The station and the hall at night: lamps still dominate under cover.
- By day and at dusk: unchanged (the moon adds nothing with the sun up).
