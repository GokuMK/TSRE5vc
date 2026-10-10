# Task Environment 01 - Weather Data

Status: design (user, 2026-10-10). Step 1 in progress on `feature/weather`.

## Objective

Weather a route or activity designer prepares once and ships with the route:
cloud layers, wind, precipitation and visibility over the route's area and
over the hours of an activity. Made either procedurally from presets or from
historical weather data, stored in the route, and shown in the editor's map
mode as a weather-app style layer. The 3D clouds are task renderer 28, which
reads this data.

Not truly dynamic weather: each moment of the weather is a static field that
drifts with the wind; changes come from keyframes cross-faded in time.

## What exists

- **ENV files**: the TRK names an env file per season and weather
  (`Environment ( SpringClear ( UKsun.env ) SpringRain ( UKrain.env ) ... )`);
  any number of env files may sit in `ENVFILES`. An env file holds
  `world_fog_distance`, fog colours, sky layers (textured domes with UV
  scroll and day/night fades: MSTS's clouds), satellites, water layers and
  `world_precipitation` (type, density, speed). `Environment`
  (`src/tsre/world/Environment.cpp`, ParserX) reads only the water layers.
- **Activities**: the activity header has a season and a weather (clear,
  rain, snow), which pick the env file through the TRK. Open Rails adds
  weather change events (`ORTSWeatherChange`: overcast, fog, precipitation
  intensity and liquidity, with transition times) and, in timetable mode,
  `.weather-or` files in the route's `WeatherFiles` directory.
- **Time and place**: `SunPosition`, `MoonPosition` and `Daylight` (task
  renderer 22, editor 05) light the scene for a date and time at the
  camera's latitude and longitude, from `Game::GeoCoordConverter`
  (`ConvertToInternal`, `ConvertToLatLon`). The environment window
  (editor 05) sets the time and date as session values.
- **Network sources**: elevation and imagery downloads
  (`src/tsre/geo/ElevationDownload.cpp`, `ImagerySource.cpp`) with
  `QNetworkAccessManager`, caching and attribution.
- **Map mode** (editor 04): layers drawn from the renderer queue
  (`MapLayer`, `src/tsre/map/`), e.g. terrain, OSM data and imagery.

## Concepts

- **Layers**: low (base up to about 2 km) and mid (2-6 km) clouds are
  volumetric in the renderer; high clouds (cirrus, above 6 km) are a flat
  layer. A cloud that grows from low to high (cumulonimbus) belongs to the
  low layer with a tall type.
- **Coarse grid, generated detail**: the data describes the sky per cell
  (about 1-10 km): coverage (0-1), cloud type (stratus, stratocumulus,
  cumulus, cumulonimbus), density, base and top heights. Where the clouds
  are inside a cell is generated: a seeded noise field thresholded so the
  covered share matches the cell's coverage. Nothing at cloud scale is
  stored, and the same file always gives the same clouds.
- **Global values per keyframe**: wind per layer (direction, speed),
  precipitation (type, intensity), visibility, and derived lighting values
  (overcast share).
- **Time**: keyframes at times (absolute for real data, relative for
  presets), cross-faded; between and within them the field drifts with the
  wind. The grid covers the route plus a margin for the drift over the
  weather's duration.
- **Sources**: procedural presets (a type and a few parameters, no maps:
  fair cumulus, broken, overcast, rain, a passing front); historical data
  from Open-Meteo first (ERA5 0.25 degrees from 1940, ECMWF IFS 9 km from
  2017, CERRA 5 km over Europe 1985-2021; hourly `cloud_cover_low/mid/high`,
  wind at 10 and 100 m, precipitation, snowfall, weather codes; free for
  non-commercial use); Copernicus CDS (ERA5 per pressure level: cloud base,
  cloud tops, upper winds; free with visible attribution) later, if needed.
  Real data sets the cells' values; the generated detail stays procedural.

## Storage and ENV files (user, 2026-10-10)

The env file stays the entry point: the TRK's season and weather defaults
and, later, an activity naming an env file directly (MSTS does not read
activity-referenced env files; Open Rails reads env files as text). A TSRE
weather block in the env file, with larger data in files it references.
If a block in shared, TRK-referenced env files causes problems in MSTS or
Open Rails, an advanced weather block in the TRK is the fallback. A generic
env file can list several weathers with weights, one chosen at random.

The exact format is step 1's proposal (below), reviewed before the loader is
written.

## Map mode weather layer

A `Weather` map layer, early (step 2), so every later step shows its
result: coverage of each cloud layer (low, mid, high in distinct colours,
transparent), precipitation in radar colours, wind arrows on the grid,
the grid's cells when zoomed in, a legend, and the time from the
environment window (the drift and keyframes follow it). The shown cells and
the generated detail can be toggled.

## Steps

1. **Format and base**: the format proposal (review), then the `Weather`
   model, the env block reader and writer, the generated detail, and the
   preset generator; a weather derived from a plain env file (fog distance,
   precipitation, clear/rain/snow) when it has no block. Unit tests.
2. **Map mode layer**, with time from the environment window.
3. **Open-Meteo import**: the route's area as a grid of point requests
   (spacing by model), cached; times in UTC, the grid's north turned to the
   route's grid; attribution stored with the weather.
4. **Keyframes and drift** polished: cross-fades, margins, a weather longer
   than its data (looping presets).
5. **Random choice** in generic env files; **activities** naming an env file
   (TSRE token); the editor picking the season and weather (editor 05).
6. **Open Rails export**: weather change events for activities and
   `.weather-or` files for timetables from the keyframes (overcast, fog,
   precipitation).
7. Later, if needed: Copernicus CDS import (cloud base and tops, upper
   winds).

## Verification

- Unit: the env block round trip, generated coverage matching the cell
  values, the drift, keyframe interpolation, the import's grid geometry and
  north, a plain env file's derived weather.
- Map captures (renderer-capture `mode: map`) of presets and of an imported
  day on a test route, at several times.
- An env file with the block loads in Open Rails (when available), and the
  exported events behave there.

## References

- [Open-Meteo Historical Weather API](https://open-meteo.com/en/docs/historical-weather-api)
- [ERA5 licence](https://apps.ecmwf.int/datasets/licences/era5)
- [Open Rails manual: timetable weather files](https://open-rails.readthedocs.io/en/1.6.1/_sources/timetable.rst.txt)
- Nubis weather maps: [Schneider 2017](https://advances.realtimerendering.com/s2017/Nubis%20-%20Authoring%20Realtime%20Volumetric%20Cloudscapes%20with%20the%20Decima%20Engine%20-%20Final%20.pdf)
