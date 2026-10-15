# Task Environment 01 - Weather Data

Status: steps 1-3 and 3b done on `feature/weather` (2026-10-10); route-scale
generator (fronts) added 2026-10-11. **Paused**
(user, 2026-10-11) for renderer task 28: the data sources are to be
rethought first (see "Sources rethink"), then step 4.

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
route gets clouds without editing. The visibility is ten times the fog
distance: MSTS set it for a view of about 2 km (2000 gives a fair 20 km,
rain's 450 4.5 km, near the presets' 40 and 6 km; read as it was, the haze
of the physical sky, renderer task 32, hid the far terrain; user,
2026-10-14).

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
        tsre_weather_air ( 14 8 )                 // 2 m above the ground: temperature, dew point (°C)
        tsre_weather_fog ( auto )                 // auto, off, or amount 0-1, visibility (m), depth (m);
                                                  // then pooling 0-1 if not 1 (0: lying evenly on all ground)
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

The `//` notes explain the example; env files have no comments (MSTS uses
`comment ( ... )` and `skip ( ... )` blocks, and names starting with `_`).

- The air and the fog (added 2026-10-14, task renderer 33): auto fog comes
  from the air, the night's clouds and wind and the time of day
  (`WeatherFog`); off and a set fog override it for the keyframe.
- A preset is a named set of these values in TSRE's code (fair cumulus,
  scattered, broken, overcast, rain, snow, thunderstorm, a passing front as
  several keyframes, morning mist, valley fog); a weather can start from one and override any
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
3b. **Regional models and map detail** (inserted, user 2026-10-10): the
   high-resolution European models (historical forecasts and forecasts,
   the date not mattering), chosen automatically; the map showing the
   coverage zoomed out and the generated clouds only zoomed in.
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

## Step 1 (done, 2026-10-10)

- `src/tsre/environment/`: `EnvNode` (token trees, the reader on
  `SimisTextReader`, the writer: tab indents, UTF-16 LE with a byte order
  mark, CRLF), `EnvFile` (`Env::File`, the whole env file as objects),
  `Weather` (the block, keyframes, advection-corrected blending, drift,
  PNG maps, sampling), `WeatherPresets` (clear, fair_cumulus, scattered,
  broken, overcast, rain, snow, thunderstorm, front), `WeatherDetail`
  (seeded gradient noise; cloud placement thresholded through the noise's
  measured distribution, so the covered share matches the coverage).
- `Environment` reads through `Env::File` and keeps the file and its
  weather (`file`, `weather`); its water layers are as before: parity
  captures of EUROPE1 and BNSF_SCENIC with the old and the new reader are
  pixel-identical.
- Found in real files: an empty block used as a flag
  (`world_sky_satellite_light ( )`) stays a token tree, so it is written as
  it was; six stock snow env files end with one `)` too many, skipped with a
  warning as MSTS and Open Rails accept it; declared list counts do not
  always match (kept as declared); the env files carry MSTS wind layers
  (`world_wind`), which the derived weather uses for the low wind.
- Known simplification: unknown items inside a list block (between its
  entries) are kept in the enclosing block.
- `weather` test suite (42 cases): the model's values, round trips, unknown
  and commented-out blocks kept, saving, the weather block with preset
  overrides, derived weather, presets, blending and looping, drift, fields
  moving with the wind instead of fading, maps, the covered share
  (within 0.004 at 0.2, 0.5, 0.8). With `--test-cases <dir>` it reads every
  env file below it: all 97 on this machine (stock MSTS, starter route,
  test routes) round-trip, and every value of each file is in its written
  model.

## Step 2 (done, 2026-10-10)

- **Map > Weather** (`WeatherMapLayer`): the low, mid and high clouds at
  the weather's time (low and mid filled, high hatched; dark clouds on the
  light palette, light ones on the dark), rain and snow in radar colours
  over them, the low wind as arrows every 150 pixels, the weather's cells
  when they are 8 pixels wide or more. The image (512 x 512 texels, the
  view's ground and 60 % more) is made on a worker thread and anchored to
  the ground: panning keeps the last image in place until the next is
  ready; a new one is made for another time (half a minute apart), another
  weather, or a view it does not cover or fit. Cloud edges are softened to
  a texel at least, so the image does not step. Map captures wait for it
  (`mapLayersBusy`).
- **Precipitation** (`WeatherDetail::precipitation`): the weather's rate
  varied over about 12 km (mean factor 1) and, for convective clouds,
  falling only under the cells (the area's mean kept), so rain is patchy
  as on a radar. The 3D renderer takes the same.
- **Sampling speed**: `Weather::Frame` holds what depends only on the time
  (the keyframes' blend, every layer's drift), computed once for an image;
  the noise uses a table of 256 gradients (the renderer's shader takes the
  same) instead of trigonometry per corner.
- **The F5 Environment panel** (`EnvironmentTools`), the fifth tool panel:
  - Environment File: the TRK's season and weather slots with their files,
    then every file in `ENVFILES`; Reload; Save (after a question) writes the
    weather into the env file as its block, with its maps.
  - Weather: a preset ("From the file" for the file's own or derived
    weather) and seed, "Show on the map" (Map > Weather), and the values
    at the editor's time: source, keyframes, each layer's type, coverage
    and heights, the low wind, precipitation, visibility.
  - Editor Session: the former environment window's controls, unchanged
    (its fog section now headed "Fog").
  - Window > Environment (F5) shows it; the environment window no longer
    floats. All tool panels and the tool box are 260 px wide (Objects and
    Terrain together 520).
- **The weather's time** in the editor: a relative weather starts at
  midnight, so the environment window's time of day moves it (a 15-hour
  front passes over the day); an absolute one counts from its start, the
  editor's time taken as UTC until time zones come.
- Numbers in written env files are plain decimals (230, 0.25), the fewest
  that read back as the same float.
- Checked: `tests/renderer/map-weather.json` (EUROPE1: every preset, the
  front at 0, 4, 8 and 12 h, fair cumulus an hour apart moving east); in the
  editor under Xvfb: F5, a preset, Show on the map, map mode; the file list
  (twelve TRK slots, four files) and switching to `UKsnow.env` (derived
  snow); Save on a copy of MINI (the block written, the MSTS contents
  kept). Suites: weather (43), map-view, editor-tools, tokens.

## Step 3 (done, 2026-10-10)

- `WeatherImport` (`src/tsre/environment/`): Open-Meteo's archive
  (`archive-api.open-meteo.com/v1/archive`), models ERA5 (0.25 degrees,
  from 1940; samples every 25 km) and ECMWF IFS (9 km, from 2017). The
  area is the route's track (TDB vector sections, whole tiles; the camera's
  surroundings without track) and a margin (30 km by default: an hour of
  wind). Sample points on a regular grid in route metres, converted to
  latitude and longitude through the route's converter, all in one request
  (at most 1000 points), hourly from the chosen start (UTC) for the chosen
  hours. Answers are cached on disk (`<cache>/TSRE5/weather`, by URL), as
  the archive does not change.
- Variables: cloud cover low, mid, high; precipitation; snowfall;
  temperature and dew point at 2 m; wind at 100 m; the WMO weather code.
  Derived: low cloud type from the code (thunderstorm: cumulonimbus to
  10 km; showers: cumulus; steady rain, drizzle, snow: stratus) or else the
  cover (under half: cumulus, under 85 %: stratocumulus, more: stratus);
  low base 125 m a degree of dew point spread above the point's elevation
  (fog codes: 30 m); mid at 3-4.3 km (5 km in steady rain), high at
  8.5-9.5 km; snow share from snowfall (7 cm of snow a mm of water);
  visibility 300 m in fog, else 40 km reduced by the precipitation; mid and
  high winds twice and three and a half times the 100 m wind, veering
  10 and 20 degrees.
- The weather: absolute, starting at the request's start, a keyframe each
  hour; the grid tile-aligned over the area at the chosen cell size
  (4096 m by default); maps interpolated from the samples per cell; the
  keyframes' uniform values the samples' means, the wind their mean
  vector turned from true north to the route grid's (its bearing at the
  area's centre). Source and attribution go with it ("Weather data by
  Open-Meteo.com (CC BY 4.0); contains modified Copernicus Climate Change
  Service information <year>" for ERA5); maps are saved to
  `ENVFILES/WEATHER/<env file name>/`.
- F5 > Import Historical... opens the dialog: day, start (UTC), length,
  model, margin, cells, with the area, sample count and keyframes shown;
  the download can be cancelled. The imported weather is unsaved until
  Save. Weather Details names it ("Open-Meteo era5, <day> <hour> UTC").
- Checked: the weather suite's import cases (a canned answer of four
  samples: rain from stratus, fair cumulus, showers, snow; the grid's
  alignment, the wind turned by the grid bearing, the cloud base from the
  dew point, snow share, visibility in rain, an error answer, too many
  points); a live import for EUROPE1 on 2026-06-21 (87 x 146 km, 35 points,
  13 keyframes; fair weather, cumulus building to about 60 % in the early
  afternoon) shown on the map; on a copy of MINI an import saved (the
  block with start, grid and maps; 16-bit maps of 21 x 21 cells x 13
  keyframes) and read back from the file.
- Not yet: time zones (the editor's time is taken as UTC for an absolute
  weather), so on a route far from Greenwich the hours are off by its
  offset.

## Step 3b (done, 2026-10-10)

Imports from ERA5 looked like noise: its 25 km cells carry little of a
route's weather, and the visible structure came from the generator. The
user's call: the import is an alternative to a good generator, so dates do
not matter, and free forecasts at high resolution are worth more than
reanalyses. Europe first.

- Models, finest first: Meteo-France AROME France HD (1.5 km), UK Met
  Office UKV (2 km), DMI HARMONIE-AROME Europe (2 km), DWD ICON-D2
  (2.2 km), KNMI HARMONIE-AROME Europe (5.5 km), DWD ICON-EU (7 km), ECMWF
  IFS (9 km), ERA5 (25 km). Past days come from Open-Meteo's historical
  forecasts (`historical-forecast-api.open-meteo.com`), the last two days
  and the coming two weeks from the forecast service
  (`api.open-meteo.com`), ERA5 from the archive. Only the archives are
  cached.
- Automatic (the dialog's default): the finest model with data at the
  area's centre and four corners (one small request per model tried).
  AROME HD has no weather code: it is inferred from the cover and the
  precipitation.
- Samples every model cell, 4 km at least (or 2-16 km as chosen), in
  requests of 200 points; at most 3000. Open-Meteo counts each point as a
  call (600 a minute, 5000 an hour, 10000 a day free), so downloads keep
  under 500 points a minute, wait out the minute (cancellable) when going
  over, and on a refusal (429) wait a minute and retry, three times at
  most. Cached answers do not count.
- Map: zoomed out, the coverage itself (as weather apps show cloudiness);
  the generated clouds come in where a texel is between a third and a
  tenth of the clouds' size, and the showers keep to the convective cells
  likewise. Noise octaves finer than about three texels are left out,
  which would only speckle the image.
- Past the grid the maps fade to the keyframe's values over three cells;
  before, the grid's edge stretched out in stripes.
- The noise is sampled at fixed scales (5000, 2500, 1200, 3000 m: stratus,
  stratocumulus, cumulus, cumulonimbus), the two either side of the cloud
  type mixed. It was sampled at a scale following the type, and a route
  far from the origin (EUROPE1 is 12 000 km out) is thousands of noise
  cells away, so the slightest change of type from texel to texel moved the
  noise by whole cells: pixel noise wherever the type varied, which was
  most of the noise in the earlier imports. The renderer (task 28) must do
  the same.
- Checked: the weather suite (60 cases; new: the forecast URL, regional
  batches, the fade past the grid, the noise staying whole far from the
  origin, which fails with the old scaling); a live import for EUROPE1 on
  2026-06-21, Automatic picking AROME France HD (its domain reaches
  55 degrees north; UKV can be chosen by hand), 874 points in 5 requests
  paced without a refusal, then the map wide (the model's cloud field,
  smooth) and close (cloud shapes).
- Later: raster sources. Open-Meteo publishes its model data on AWS open
  data (`s3://openmeteo`, `data/<model>/<variable>/chunk_N.om`: ICON-D2
  back to late 2023, UKV, AROME HD, HARMONIE, IFS, ERA5), with no call
  limit; a chunk is one variable over the whole domain for a few days,
  about 40 MB, read in parts over HTTP ranges with the `.om` file format
  library. Worth it for large areas or fine sampling.

## Generator: fronts over the route (2026-10-11)

The user's direction after the sources review: real data is not as simple
as it looked; rework the procedural generator so it makes route-scale
weather, such as a front coming through: part of the route clear, part
cloudy. Fronts first (user's choice).

- `WeatherGenerator` (`src/tsre/environment/`): a scenario is a front's
  cross-section, stations of cloud layers, rain, visibility and wind at
  distances ahead of its line at the ground (km; behind negative):
  - cold front: the warm sector's stratocumulus ahead; a band of
    cumulonimbus at the line (to 9.5 km, up to 9 mm/h) about 50 km wide;
    clearing behind with scattered cumulus, the wind veering;
  - warm front: cirrus 650-950 km ahead, cirrostratus and altostratus
    lowering, nimbostratus and steady rain over the last 100 km, the warm
    sector's stratocumulus behind.
  The line moves towards a heading (the route's grid) at a speed (km/h),
  crossing the area's centre at a chosen hour; it bends (up to 45 km,
  noise over 250 km) and its cover and rain vary along it (noise over
  120 km); heaps grow through the day (fewer in the morning and at night).
  Stations are blended linearly; a layer appearing takes the other's
  heights and type.
- The result is a day (25 hourly keyframes, relative, looping) with
  per-cell maps over the route's area and margin, as an import, so the map
  layer, saving and the 3D clouds take it unchanged. Keyframes hold the
  area's means and the wind at the centre.
- F5 > Generate Weather...: scenario, moving towards (degrees, the summary
  names the compass points), speed, over the route at (hour), seed,
  margin, cells. Not saved until Save.
- Capture views: `weatherScenario` (`cold_front`, `warm_front`),
  `weatherHeading`, `weatherSpeed`, `weatherArrival`
  (`tests/renderer/map-front.json`).
- Checked: weather suite (66: the cross-sections, a cold front passing a
  100 km area at noon with rain at the centre then, more cloud ahead than
  behind, none in the morning); EUROPE1 on the map at 11, 13, 15 and 17 h
  (the band approaching over the warm sector, beside the route, passed
  with the route clearing, clear) and a warm front at 8 and 13 h.
- Next ideas: occluded fronts and a low's centre; showers (air-mass
  cumulus) and fair days as scenarios; snow by temperature; several
  scenarios a day.

## Sources rethink (paused, 2026-10-11)

The user's review of step 3b: the imports still sample points, while
weather data comes as rasters, and the earlier source research (Open-Meteo's
point API only) was too narrow. The user's report
(`weather-source-report-1.md`) proposes DWD ICON-EU model fields (3D cloud
cover, cloud water and ice per level), EUMETSAT satellite cloud shapes and
CERRA. Simple files can be read without libraries. Before going further:
choose the sources, then remove the ones that turn out useless.
Automatic keeps choosing the finest model (user).

Checked on 2026-10-10/11:

- **DWD open data** (`opendata.dwd.de/weather/nwp/icon-eu/grib/<run>/<var>/`,
  likewise `icon-d2`): free, no account. ICON-EU on a regular 1377 x 657
  grid (0.0625 degrees); ICON-D2 both icosahedral and regular-lat-lon.
  Single-level fields (`clcl`, `clcm`, `clch`, `clct`, `hbas_con`,
  `htop_con`, `ceiling`, `tot_prec`, `rain_gsp`, `snow_gsp`, `t_2m`,
  `td_2m`, winds) and model-level ones (`clc`, `qc`, `qi`, `t`, `p`, `u`,
  `v`, about 74 levels; `hhl` gives their heights). Files are GRIB2 in
  bzip2; the data is CCSDS/AEC packed (data template 5.42, 16 bits), so
  reading them takes a bzip2 and an AEC (adaptive Rice) decoder besides
  the GRIB2 sections: no library, but two decoders to write. Only the
  latest runs are kept (about a day: 8 runs a day): forecasts only, no
  history. Each file covers the whole domain, with no subsetting: about
  1.2 MB a single-level field and hour, 0.1-0.6 MB a model level and hour,
  so 12 hours of a 3D cloud field (40 levels x `clc`, `qc`, `qi`) is about
  400 MB, of the single-level fields about 150 MB.
- **EUMETSAT view service** (`view.eumetsat.int/geoserver/ows`, WMS
  1.3.0): free, no account, PNG images for any bounding box and time:
  MTG FCI high-resolution visible 0.6 um (`mtg_fd:vis06_hrfi`, about 1 km,
  from 2024-09-16, every 10 min, daytime) and infrared 10.5 um
  (`mtg_fd:ir105_hrfi`, day and night), MSG cloud mask (`msg_fes:clm`,
  about 4-5 km over Europe, from 2020-09, every 15 min), MTG cloud type and
  cloud phase RGBs (from 2025-06), MSG blended precipitation
  (`msg_fes:h60b`, from 2022-12). Cloud top height (`msg_fes:cth`) is too
  coarse (blocks of about 15 km). A visible image for the Settle-Carlisle
  area on 2026-06-21 12:00 UTC shows the real cloud field at about 1 km.
  Images are colour-mapped (the legend gives the mapping); a satellite sees
  the tops only; parallax shifts clouds poleward (about 3 km for a 2 km
  cloud at 54 degrees north); the licence of derived data is to be checked.
  The report's OCA products need a Data Store account and come as NetCDF-4
  (HDF5), which does need a library.
- **CERRA** (Copernicus, 5.5 km, 3-hourly analyses): a CDS account, API key
  and queued requests; GRIB. Heavier to set up; history is not needed now.
- **Open-Meteo**: the point API (current import) has history and every
  European model, but counts each point as a call. Its own model data is
  also on AWS open data (`s3://openmeteo`, `data/<model>/<var>/chunk_N.om`,
  with history, readable by HTTP ranges), in its own compressed format.

Proposal (to decide when resuming):

- Cloud shapes from the satellite (WMS), about 1 km: the coverage maps
  directly, for a chosen past time (daytime visible; infrared at night).
- Heights, wind, precipitation from a model at a coarse spacing (10-20 km,
  a few dozen points, under the point API's limits), as they vary smoothly.
- Later, DWD files (bzip2 + AEC + GRIB2 decoders) for forecasts and 3D
  cloud fields.
- Remove: ERA5 and ECMWF IFS (too coarse; the early imports' noise), the
  2 km point sampling. Keep the request pacing (small).

## Timelapse (2026-10-13)

The Environment panel's time section has a Timelapse row (user: to watch
the weather and the light change, with auto exposure): a speed (5, 10 or
30 s, 1, 2, 3 or 4 min of the day a real second) and Start / Stop. While on, a timer
runs the session's time of day on (`EnvironmentWindow::advanceTimelapse`);
the weather follows, as its hours come from the date and the time, and past
midnight the date moves on. A stalled editor goes on from where it was (at
most one second's step). The clouds' weather is sampled again whenever its
hours change, so with the timelapse every frame. First a fixed 4 min/s: the
clouds showed complete pixelation at that speed (the user), hence the
speeds, to find where it starts.

The panel's combo boxes (env file, weather preset, timelapse speed) take
TSRE's combo box style (`combobox-popup: 0`: a list under the box).

At the speeds (the user): 5 s/s invisible, 10 s/s visible but not breaking
immersion, 30 s/s clear but not broken.

## The clock (2026-10-13)

The user: the clouds always move naturally, in the simulation's update; the
timelapse becomes the clock's speed. The time of day runs on in real time by
default:

- Setting `core.rendering.timeOfDay.running` (default on), kept by
  `RouteEditorGLWidget` as it changes. The speed is the editor's for the
  session only, not a setting (the user): real time at every start. The
  panel's Clock row: the speed (1, 5, 10, 30 s, 1 to 4 min a second;
  `EnvironmentWindow::clockSpeedChanged` to `setClockSpeed`) and Stop /
  Resume; Stop writes the stopped time into the session (and so the
  profile on Save).
- With the clouds following the wind (task renderer 28) the user found up
  to 1 min/s fine; above, the clouds refresh faster (see there).
- The editor's update (`timerEvent`, with the simulation) runs
  `Game::timeOfDayHours` on (`advanceClock`, in a double so that real-time
  steps do not round off); the settings hear of it only past midnight (the
  date). The panel shows the running time four times a second; the
  Weather Details follow once a second while open. Captures pause the
  simulation and with it the clock.
- Nothing is redrawn each frame for the clouds' drift: see task renderer 28,
  "Clouds that move with the clock".

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
