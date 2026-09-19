# Geo elevation: start here

Agent handoff, updated 2026-09-19. This directory is sufficient introductory
context **when read in the order below**. The implementation and current service
responses remain authoritative for code changes; historical milestone reports
are not a current specification.

## Reading order

1. [Current status](elevation-current-status.md): newest sections first. Later
   sections retain older review snapshots and their original counts/results.
2. [Distant terrain](distant-terrain-elevation.md): critical acquisition problem;
   detailed-service validation does not approve distant terrain.
3. Choose the relevant track:
   - New WCS/ArcGIS service: the workflow below, then
     [England/Estonia](england-estonia-validation.md) and
     [Denmark/authentication](denmark-validation.md). These cover recent protocol
     differences; use [Czech ArcGIS](czech-arcgis-validation.md) for that provider.
   - Downloaded/offline sources: [file-source implementation and next step](local-elevation-sources.md).
     One-degree HGT is implemented; GeoTIFF collections remain staged work.
4. Read the relevant source files and tests from the code map. Credentials and
   source-setting behavior are documented in [Settings](../../settings-system.md).

The original ideas, initial design review and Geoportal implementation report
are historical background. New Route and route projection documents are separate
workstreams; read them only when changing coordinate-converter integration.

## Workspace and working preferences

- Current work is in sibling `TSRE5vc-geo-terrain`, branch `feature/geo-terrain`,
  not the main `TSRE5vc` directory. Check `git status` and the current branch first.
- The country integrations, UI filtering and local-directory preparation are
  committed together with this handoff. Start another worktree from the updated
  feature branch, and still check for subsequent uncommitted work before handing
  off. Preserve other agents' changes; do not reset the working tree.
- Keep fixes simple and provider-generic; no country-specific terrain logic.
  Prefer existing Qt facilities and no new dependency. If a suitable small static
  dependency is substantially better for a large subtask, propose it first.
- The user does not need finer than 1 m ground resolution. Request coarser output
  from finer products when supported; distinguish map metres from ground metres.
- Prefer small standalone checks and bounded endpoint experiments. Respect the
  current user's build/test constraints; recent Release builds are allowed, while
  main application UI checks have been left for the user. Never close their app
  or overwrite their local source files to complete a test.

## Code map

Paths below are relative to the repository root.

| Area | Files / entry points |
|---|---|
| Catalogue | `src/tsre/geo/elevation-datasets.json`; `Dataset`, `datasets()` and `parseDatasets()` in `ElevationSource.h/.cpp` |
| Providers/cache | `ElevationSource.cpp`: `FileHgtSource`, `CachedRasterProvider`, `WcsProvider`, `ArcGisImageServerProvider`, `RasterSource`, `generate()` |
| Requests/grid checks | `coverageUrl()`, `imageServerUrl()`, `validateRasterGrid()`, `cacheRelativePath()` |
| Numeric raster/CRS | `src/tsre/geo/ElevationRaster.h/.cpp`: `Raster`, readers, `project()`, `supportedCrs()`, `fillNoData()` |
| HTTP/auth | `src/tsre/geo/ElevationDownload.h/.cpp`: `downloadWave()`; query credentials added only inside transport |
| Height UI | `src/tsre/geo/HeightWindow.cpp`; 10 km location filter via `nearDataset()` |
| Settings | `src/settings/SettingsRegistration.cpp`: dynamic source options and reference-valued setting |
| File-source lookup | `defaultFileSourceId()`, `findHgtFile()`, `readHgtFile()`; also used by `GeoHgtFile.cpp` and the missing-file checker |
| Tests | `tests/geo/`; `src/tsre/tests/ElevationUiTestSuite.cpp` |
| User-facing text | `translations/tsre_en.ts`, `translations/tsre_pl.ts`; use existing translation IDs/style conventions |

Actual C++ common type is `Elevation::Raster` (called ElevationRaster in design
diagrams). Acquisition prepares local data; height sampling does not perform HTTP.
Source IDs are dynamic Settings reference values, so new entries need no hardcoded
Settings enum changes. Invalid objects are skipped with diagnostics, while invalid
JSON/top-level structure rejects the file. Missing selected sources fail explicitly.

## Adding a WCS source

1. Inspect official capabilities and coverage metadata. Confirm terrain rather
   than surface/shaded imagery, product date, horizontal CRS, vertical datum,
   native spacing, coverage, access rules and authentication.
2. Use an existing supported CRS, or verify server-side reprojection. Do not
   implement a new local projection merely because the native CRS differs.
3. Start with a small request. Inspect the actual numeric TIFF/ASCII response:
   dimensions, sample type, compression, CRS, transform and NoData. An HTTP 200
   can contain XML or HTML errors. Preserve legitimate zero/negative heights.
4. Configure the existing provider. Key fields and exceptions:

   | Fields | Meaning |
   |---|---|
   | `provider`, `endpoint`, `coverage`, `format` | Service providers: `wcs-2.0.1`, `wcs-1.0.0`, `arcgis-imageserver`; decoder formats TIFF/ASCII as supported by the provider. File sources use the separate contract below |
   | `requestFormat` | Optional wire-format spelling, independent of decoder format; WCS 1 defaults TIFF to `GeoTIFF`, Estonia uses `image/tiff`, Denmark `GTiff` |
   | `axisX/Y`, `scaleAxisX/Y` | WCS 2 subset and optional separate scaling axes; scaling defaults to subset axes |
   | `crs`, `resolution`, `origin`, `bounds` | Request/cache grid, not necessarily native source grid; bounds are conservative rectangles, not coverage masks |
   | `blockPixels`, `concurrentRequests` | Core size 16..1024, concurrency 1..4; requests include a one-pixel halo on every side |
   | `allowExpandedGrid` | Opt-in bounded server-expanded envelope; retains exact dimensions/CRS and actual returned transform; do not turn it on to conceal an unexplained mismatch |
   | `zeroIsNoData`, `noDataPolicy` | Default valid zero; policy `fallback` or explicit `fill`; fill does not fabricate wholly empty regions or bridge unavailable downloads |
   | `authentication` | Secret reference only: `basic-api-key`, or `query-api-key` with `parameter`; see Settings and Denmark notes |
   | `notes`, `attribution` | Product caveats, live findings, unresolved limits and provenance |

5. Probe a full configured block through TSRE, then a full terrain grid crossing
   block boundaries. Repeat with cache only. Exercise NoData/coverage edges and
   inspect reports. A successful two-point check is useful but not full acceptance.
6. If shared behavior must change, add a small generic option with conservative
   defaults and regression coverage. Do not special-case country IDs.
7. Document exact request parameters, measured results and remaining limits. Keep
   useful small fixtures in `evidence/`, never credentials. Mark unverified source
   and distant-terrain claims explicitly.

Service options cannot be copied blindly: e.g. Finland rejects some uppercase
GeoTIFF options that work for Netherlands. England needs different subset/scaling
axes and returns expanded envelopes. See individual service notes for evidence.

## Constraints to preserve

- TIFF support currently covers classic uncompressed single-band Float32, Int16
  and UInt16, strips/tiles, including padded final strips. No general compressed
  TIFF, BigTIFF, RGB or large-file windowed reader exists yet.
- HTTP responses are bounded to 32 MiB; TIFF dimensions to 16 Mi pixels; requests
  to 2048 blocks; fill mosaics to 32 Mi pixels. These are emergency guards, not
  sufficient distant-terrain budgeting. Check source constants before changing them.
- Coarser output-vertex sampling currently reduces neither acquisition resolution
  nor download volume. No distant-specific profile/restriction is implemented.
- Cache identity hashes the dataset definition except `noDataPolicy`. Changing
  notes or concurrency can therefore also select a new cache directory. Keys are
  resolved separately and never belong in that definition or cached request URLs.
- User files belong in stable named directories. World HGT uses only
  `geoPath/world_hgt/`; no legacy root or `hgt/` search remains. Raw `.hgt` files
  take precedence over `.hgt.gz`. Preserve originals and keep derived indexes disposable.
- Selectable secondary-source fallback / retaining existing heights over missing
  areas remains an open task in the original ideas. Do not mistake it for the
  implemented per-dataset NoData fill option.

## Focused validation commands

PowerShell, from the feature worktree; use the local Qt/toolchain versions where
different. The existing `build` directory is configured for **Release**.

```powershell
$env:PATH='C:/Qt6/Tools/mingw1310_64/bin;C:/Qt6/6.10.1/mingw_64/bin;'+$env:PATH
& C:/Qt6/Tools/CMake_64/bin/cmake.exe --build build --config Release --target tsre_elevation_tests -j 4
./build/tsre_elevation_tests.exe
```

The ordinary standalone suite is offline. Optional public-service smoke test:

```powershell
./build/tsre_elevation_tests.exe --live-at build-source-probe/cache DATASET_ID LATITUDE LONGITUDE
```

`--live-at` samples **two points at 1 m output spacing**, not a full terrain tile,
and does not load profile secrets. Authenticated/full-tile checks need a small
probe calling `generate()` with representative points, actual output spacing and
only the selected secret reference/value. Read secrets locally; do not print them
or put key-bearing URLs in commands/logs. Ignored `build-*-research/` probes in this
worktree are conveniences, not evidence guaranteed in another checkout.

After a change, run appropriate standalone checks and the relevant build when
allowed. The current file-source milestone passes **359 standalone checks**, a
live Mapzen HGT download/sample probe, 246 Settings checks, 61 elevation-UI checks
and the Release application build.
Flanders has only a small download/cache probe; Estonia 1024 was tested but its
catalogue still uses 512. Consult current source/status rather than treating these
counts or pending decisions as permanent.

## File-source status

`provider: "file"`, `format: "hgt"`, `fileGrid: "degree"` is implemented. The
catalogue defines its directory, global bounds, default/fallback identity and
optional gzip URL-template downloader. Manual-only sources omit `download`.
Service sampling now prepares this fallback only for unresolved primary samples.

The next format should be a concrete degree-grid GeoTIFF product. Current TIFF
limits still prevent treating arbitrary compressed/large/BigTIFF or COG
collections as supported merely by adding JSON. Extend parser validation,
provider dispatch and bounded/windowed decoding together.
