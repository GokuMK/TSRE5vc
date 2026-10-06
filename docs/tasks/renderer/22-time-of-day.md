# Task 22 - Time of Day (first step of the environment task)

## Objective

Light the Route Editor as the sun would at a chosen time and date over the
camera's place, instead of the fixed editor light. A first step of the
environment task: no moon, stars, clouds, weather or ENV file data yet.
Both renderers; off by default.

## Settings

- `core.rendering.timeOfDay.enabled` (off).
- `core.rendering.timeOfDay.time` (12): local mean solar time in hours at
  the camera; 12 is when the sun is highest on average. Clock time needs the
  route's time zone, which routes do not record.
- `core.rendering.timeOfDay.date` (2026-06-21), as yyyy-MM-dd.

## Sun

`SunPosition` (src/tsre/geo) implements the NOAA solar position equations:
elevation and azimuth from latitude, longitude, date and UTC time (about a
hundredth of a degree, no refraction). The camera's latitude and longitude
come from the route's coordinate converter, once per tile. MSTS tiles are
laid out on a projection grid (interrupted Goode homolosine) whose north
turns away from true north off its central meridians (about 12 degrees at
the test tile), so the sun's azimuth is turned by the grid's bearing,
measured from a point 100 m along -z.

World axes: x east, y up, z south (MSTS z points north; TSRE flips it).

## Light

`Daylight` maps the sun's elevation to the light the shaders take:

- the sun (diffuse light): the usual 0.7 above 20 degrees, warmer and weaker
  towards the horizon, none 1 degree under it;
- ambient light: the usual 0.3 by day, a dark blue night light below civil
  twilight;
- sky and fog colours: the configured colours by day, a warm band around
  sunset, twilight blue to -6 degrees, night below -18.

Shadows follow the sun; below the horizon the shadow maps are not drawn, and
a sun lower than 6 degrees casts shadows as if at 6 degrees, so they do not
stretch across the maps. Local lights (task 21) show at dusk and night on
the QRhi renderer: they scale from full at night (sun under -6 degrees) to
3 % above 10 degrees, as eyes adapt to daylight.

## Verification

- `time-of-day` suite: solstice declination, noon height and direction in
  both hemispheres, equation of time, sunrise in the east, midnight below
  the horizon, solar time against UTC, world axes against the MSTS
  projection, daylight colours.
- Captures of EUROPE1 at 07:00, 12:00, 20:00, 21:00 and 23:30 on both
  renderers: RMSE about 2 between them; with the setting off, captures are
  identical to before.

## Later (environment task)

Clock time with time zones, moon and stars, sky gradients and the MSTS sky
textures by time, ENV weather and fog, night textures of MSTS shapes, a
time-of-day control in the editor.
