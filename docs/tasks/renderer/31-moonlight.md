# Task 31 - Moonlight

Status: implemented (2026-10-13, `feature/clouds`); strength to confirm by the user.

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
  `FullMoonLight` 0.2 (0.1 tried first, see Results), times the phase (illuminated fraction to the
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

## Later

- Moonlit clouds; cloud shadows of the moon; a brighter physical sky at
  full moon.

## Verification

- CMK, clear sky, 22:00 on dates of a full, a half and a new moon, Dimmed
  and Auto exposure, against the build without moonlight.
- The station and the hall at night: lamps still dominate under cover.
- By day and at dusk: unchanged (the moon adds nothing with the sun up).
