# Task Environment 01 - Weather Data

Status: design (user, 2026-10-10). Step 1 in progress on `feature/weather`.

## Objective

Weather a route or activity designer prepares once and ships with the route:
cloud layers, wind, precipitation and visibility over the route's area and
over the hours of an activity. Made either procedurally from presets or from
historical weather data, stored in the route, and shown in the editor's map
mode as a weather-app style layer. The 3D clouds are task renderer 28, which
reads this data.

Not simulated weather: the weather is one or more prepared fields that drift
with the wind (see "Drift and keyframes").

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
  presets); see "Drift and keyframes". The grid covers the route plus a
  margin for the drift over the weather's duration.
- **Grid**: aligned to the world tiles (user's question, 2026-10-10): its
  corner on a tile corner, its cell 2048 m times a power of two (512, 1024,
  2048, 4096 m ...). A cell then maps to whole tiles or a tile to whole
  cells, positions stay in tile plus local coordinates as everywhere else
  (no large floats), and the map layer and the renderer find a cell with
  integer arithmetic. The default is 2048 m: one cell per tile, finer than
  any source (9-25 km), coarse enough to keep the files small.
- **Sources**: procedural presets (a type and a few parameters, no maps:
  fair cumulus, broken, overcast, rain, a passing front); historical data
  from Open-Meteo first (ERA5 0.25 degrees from 1940, ECMWF IFS 9 km from
  2017, CERRA 5 km over Europe 1985-2021; hourly `cloud_cover_low/mid/high`,
  wind at 10 and 100 m, precipitation, snowfall, weather codes; free for
  non-commercial use); Copernicus CDS (ERA5 per pressure level: cloud base,
  cloud tops, upper winds; free with visible attribution) later, if needed.
  Real data sets the cells' values; the generated detail stays procedural.

## Drift and keyframes

The user's simple model and keyframes are one mechanism (decided
2026-10-10, answering the user's question):

- **One keyframe**: one field, larger than the route by the drift margin,
  moved by wind x elapsed time. The user's simple model.
- **Several keyframes** (real data, a passing front): between keyframes
  A (time a) and B (time b), at time t both fields are drawn moved by the
  wind, A forward from its time and B back from its time, and blended by
  `(t - a) / (b - a)`:
  `field(t, p) = mix(A(p - w (t - a)), B(p + w (b - t)), (t - a) / (b - a))`,
  with w the wind interpolated between the keyframes. At a the result is A
  exactly, at b it is B exactly; in between, features move with the wind
  instead of fading in place. This is the advection-corrected interpolation
  of radar and satellite frame interpolation. With real data it suits that
  the fields at a and b are already where the weather moved to.
- **The generated detail** (where the clouds are inside a cell) drifts
  continuously with the wind from the weather's start and never jumps; only
  the coverage it is thresholded against changes between keyframes. Clouds
  then form and dissolve where coverage rises and falls, and keep moving
  meanwhile.
- Each layer uses its own wind, so low and high clouds move apart.

## Storage and ENV files (user, 2026-10-10)

The env file stays the entry point: the TRK's season and weather defaults
and an activity naming an env file directly (MSTS does not read
activity-referenced env files; Open Rails reads env files as text). One env
file holds **one weather** (user, 2026-10-10): an activity designer wants
one weather flow matching the scenario. The random choice belongs to the
TRK's generic seasonal defaults, as a list of env files with weights (see
"The TRK"). Larger data sits in files the env file references. If the block
in shared, TRK-referenced env files causes problems in MSTS or Open Rails,
an advanced weather block in the TRK is the fallback.

The exact format is step 1's proposal (below), reviewed before the loader is
written.

## Step 1 proposal: format (for review)

### How the existing env content fits

| Env content | Read by | With a weather block |
|---|---|---|
| `world_fog_distance`, fog colours | MSTS, Open Rails | The weather's visibility replaces the distance in TSRE; the colours stay (until the physical sky, task renderer 28, takes over) |
| `world_precipitation` (type, density, speed) | MSTS, Open Rails | TSRE takes precipitation from the weather; the token stays for the others |
| `world_sky` layers (cloud and sky textures) | MSTS, Open Rails | Unchanged: the clouds MSTS and Open Rails draw. Later generated from the weather (task 28, step 9) |
| `world_sky_satellites` | MSTS, Open Rails | Unchanged; TSRE places sun and moon by date and place |
| `world_water` | MSTS, Open Rails, TSRE | Unchanged |
| none | | `tsre_weather` block (below) |

Without a block, TSRE derives a weather from the env file: the TRK slot it
fills (clear, rain or snow), its fog distance and its precipitation pick a
preset (fair cumulus for clear, overcast with rain or snow otherwise). Every
route gets clouds without editing.

### The block

A top-level block after `world ( ... )`, in the env file's own style
(lower case, underscores, a `tsre_` prefix). Times are hours; heights metres
above sea level; wind direction in degrees the wind comes from, measured
from the route grid's north (an import turns true north into it); speeds
m/s.

```text
tsre_weather (
    tsre_weather_version ( 1 )
    tsre_weather_preset ( fair_cumulus )      // optional: preset values, overridden below
    tsre_weather_seed ( 1234 )                // the generated detail
    tsre_weather_source ( "preset" "" )       // source, attribution
    tsre_weather_start ( 2024-06-21T04:00:00Z )  // optional: absolute UTC; without it, relative and looping
    tsre_weather_grid ( -6200 14950 2048 80 55 )  // NW corner tile x, z; cell size (2048 m x 2^n); columns; rows
    tsre_weather_maps ( "WEATHER/fair" )      // optional: per-cell maps, below
    tsre_weather_keyframe (
        tsre_weather_time ( 0 )
        tsre_weather_wind ( low 250 6 )
        tsre_weather_wind ( mid 260 12 )
        tsre_weather_wind ( high 270 25 )
        tsre_weather_precipitation ( none 0 )     // none, rain, snow; mm/h
        tsre_weather_visibility ( 30000 )
        tsre_weather_variation ( 0.25 30000 )     // coverage varied by noise: amount, scale (m)
        tsre_weather_layer ( low cumulus 0.35 900 2200 1.0 )  // type, coverage, base, top, density
        tsre_weather_layer ( mid altocumulus 0.10 3500 4200 0.6 )
        tsre_weather_layer ( high cirrus 0.20 9000 9500 0.3 )
    )
    tsre_weather_keyframe ( ... )
)
```

Draft decision (user, 2026-10-10): the block stays separate, at the top
level. Whether it later moves into `world` (for example the weather into
`world_sky`) is decided when the format has settled in practice.

- A preset is a named set of these values in TSRE's code (fair cumulus,
  scattered, broken, overcast, rain, snow, thunderstorm, a passing front as
  several keyframes); a weather can start from one and override any
  value. A preset weather has no grid or maps: it is the same everywhere
  except for the variation noise, and covers the whole world.
- Layer values in a keyframe are uniform; per-cell maps (below) replace
  them where given.
- Keyframes blend as in "Drift and keyframes"; each layer drifts with its
  own wind.
- A relative weather (no start) begins at the activity's start or, in the
  editor, at the environment window's time; it loops after its last
  keyframe.

### Per-cell maps

Imported weather (and hand-made weather, later) varies per cell:

```text
ENVFILES/WEATHER/<name>/
|-- low.png       per keyframe: R coverage, G type, B base, A top
|-- mid.png
|-- high.png
`-- surface.png   R precipitation (mm/h), G snow share, B visibility, A spare
```

- 16-bit RGBA PNG (`QImage::Format_RGBA64`), one image per layer with the
  keyframes stacked as rows of cells: `columns` wide, `rows x keyframes`
  high. Viewable in any image viewer, lossless, small for smooth fields,
  four files per weather.
- Scales: coverage and snow share 0-1; type 0-1 along stratus,
  stratocumulus, cumulus, cumulonimbus (Nubis' continuous type); heights in
  metres (0-65535); precipitation in 0.01 mm/h; visibility in metres / 2.
- Wind, the variation and the layer densities stay per keyframe: one value
  per layer drifts the whole field rigidly.
- Imports resample their source grid (latitude and longitude, 9-25 km) to
  the route grid (about 2 km cells) through the route's coordinate
  converter.

Options considered for the maps: one binary file with a header and half
floats (compact, not viewable), JSON (large), NetCDF or EXR (new
dependencies). PNG fits Qt and can be checked by eye. Draft decision (user,
2026-10-10): PNG; reconsidered once it has been used.

### The TRK

The TRK's `Environment ( SpringClear ( UKsun.env ) ... )` stays as it is for
MSTS and Open Rails. A TSRE block next to it lists, per season and weather,
env files with weights; one is chosen at random when no activity names an
env file:

```text
TsreEnvironment (
    SpringClear ( 3 "UKsun.env" 1 "UKsunCumulus.env" )
    SummerClear ( 2 "UKsun.env" 2 "UKsunCumulus.env" 1 "UKsunStorm.env" )
)
```

A season and weather missing there uses the `Environment` entry. The method
is reconsidered in step 5 (user, 2026-10-10); left as is for now.

### Activities

A TSRE token in the activity header names an env file (MSTS does not read
it; the season and weather tokens stay for MSTS and Open Rails):
`TsreEnvironment ( "fair.env" )`.

### Code

- `Weather` (`src/tsre/environment/`): an env file's weather, read and
  written with ParserX; its keyframes and maps;
  `sample(time, x, z, layer)` for a cell value and `drift(time, layer)`.
- Parser (user, 2026-10-10): a new env file reader and writer on
  `SimisTextReader`, the bounded lexer `SFileComplex`, `SigCfg` and the
  quadtree recovery use, not `ParserX`. It reports line and column for
  errors, which hand-written weather needs.
- **The whole file is read into objects** (user, 2026-10-10), not only the
  parts TSRE uses: fog, sky layers (geometry, fades, animated shaders,
  textures), horizon, satellites, water (layers, terrain patch map, waves),
  precipitation, and the `tsre_weather` block. Writing writes the model
  out in the env file's layout; reading the stock env files, writing them
  and reading them again gives equal models (round trip test). A token the
  model does not know (another tool's extension) is kept as text where it
  stood, so nothing is lost. `Environment` (water layers today) uses the
  new model.
- `WeatherPresets`: the named presets.
- `WeatherDetail`: the generated detail, a seeded noise field thresholded
  by coverage, evaluated on the CPU for the map and on the GPU by the
  renderer from the same parameters.
- `Environment` gains the block's reading; nothing else in it changes.

### Open questions

- Grid north or true north for wind directions in the file: grid north
  makes the renderer's drift direct; true north makes the file readable
  against weather maps. Proposed: grid north, with the import converting.
- The map layer's colours for three cloud layers at once (proposed: low
  white, mid light blue, high thin hatching; precipitation in radar
  colours over them).

## Map mode weather layer

A `Weather` map layer, early (step 2), so every later step shows its
result: coverage of each cloud layer (low, mid, high in distinct colours,
transparent), precipitation in radar colours, wind arrows on the grid,
the grid's cells when zoomed in, a legend, and the time from the
environment window (the drift and keyframes follow it). The shown cells and
the generated detail can be toggled.

## Controls (user's question, 2026-10-10)

An **Environment panel on F5**, a fifth panel of the tool box that holds
Objects (F1), Terrain (F2), Geo (F3) and Activity (F4) and floats with it,
replacing the floating environment window (editor 05):

- **Top**: the env file being shown and edited: the TRK's season and weather
  slot, any file in `ENVFILES`, or the open activity's. Actions: new
  weather from a preset, import (Open-Meteo, step 3), save, reload.
- **Weather**: preset, seed, the keyframe at the current time and its
  values (layers, wind, precipitation, visibility); later a timeline of the
  keyframes.
- **Editor Session** (user, 2026-10-10): the environment window's controls
  (time, date, sun, moon, fog, local lights, exposure, bloom), moved over as
  they are, still session values with Reset and Save to profile. The time
  drives the weather's drift and keyframes, in 3D and in map mode.

The window's widgets move into the panel rather than being rebuilt; its
menu entry and shortcut go to F5. Width (user, 2026-10-10): the F1-F4
panels and the tool box are 250 px wide, the environment window 260 px; all
become 260 px (the combined Objects and Terrain view 520 px), so switching
panels does not shift the 3D view. No panel grows beyond the Environment
panel's width.

## Steps

1. **Format and base**: the format proposal (review), then the `Weather`
   model, the env block reader and writer (`SimisTextReader`), the
   generated detail, and the preset generator; a weather derived from a
   plain env file (fog distance, precipitation, clear/rain/snow) when it has
   no block. Unit tests.
2. **Map mode layer** and the **F5 Environment panel** (env selection,
   preset, time).
3. **Open-Meteo import**: the route's area as a grid of point requests
   (spacing by model), cached; times in UTC, the grid's north turned to the
   route's grid; attribution stored with the weather.
4. **Keyframes and drift** polished: cross-fades, margins, a weather longer
   than its data (looping presets). **Stop for review** (user, 2026-10-10).
5. **Random choice** in the TRK's `TsreEnvironment`; **activities** naming
   an env file (TSRE token); the editor picking the season and weather
   (editor 05).
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
