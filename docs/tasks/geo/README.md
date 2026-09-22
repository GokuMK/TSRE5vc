# Geo elevation: start here

Agent handoff, updated 2026-09-21. This directory is sufficient introductory
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
     One-degree HGT, Austria projected range-COG tiles, Luxembourg and Wales
     single national range COGs, Switzerland/Liechtenstein STAC/GeoTIFF tiles,
     and indexed user-managed GeoTIFF directories are implemented; other
     GeoTIFF families still require profile validation.
   - Projection implementation: [CRS transform extraction and shared projection path](crs-transform.md).
     This describes the small internal converter and the planned shared
     sixth-order Transverse Mercator kernel.
   - National datum/grid support: [deferred PROJ integration](proj-integration.md).
     Luxembourg's explicit LUREF2020 conversion and Wales's small OSTN15 Lite
     grid are handled by the internal converter. Read the deferred task before
     expanding this into a broad CRS/datum framework.
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
| Providers/cache | `ElevationSource.cpp`: `FileHgtSource`, service providers, `RasterSource`, `generate()`; `CogElevationSource.cpp`: projected, single-file range-COG, STAC and indexed local-directory GeoTIFF sources |
| Requests/grid checks | `coverageUrl()`, `imageServerUrl()`, `validateRasterGrid()`, `cacheRelativePath()` |
| CRS conversion | `src/tsre/geo/CrsTransform.h/.cpp`: small compiled-in forward transforms, constructed once per source; see `crs-transform.md` |
| Numeric raster | `src/tsre/geo/ElevationRaster.h/.cpp`: `Raster`, readers, sampling and `fillNoData()`; `ElevationTiffCodec.cpp`: bounded uncompressed/LZW/Deflate block decoding, Float32/Float64 conversion and predictors |
| HTTP/auth | `src/tsre/geo/ElevationDownload.h/.cpp`: `downloadWave()`, strict `downloadRangeWave()`; query credentials added only inside transport |
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

- Whole-file TIFF support covers classic single-band Float32, Int16 and UInt16,
  uncompressed, LZW or Deflate, strips/tiles and the floating predictor. The
  range reader additionally supports the verified BigTIFF/COG profiles used by
  Austria, Luxembourg and Wales, including Float64-to-Float32 conversion, exact
  configured-resolution overview selection and external block tables. This is
  still a narrow numeric elevation profile; RGB and arbitrary TIFF layouts are absent.
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
allowed. The current elevation milestone passes **382 standalone checks**. Bounded
live probes returned 171.6 m in Vienna from one Austria internal COG block and
540.3 m in Bern from one current Swiss 2 m tile; both cache repeats used no data
download. Luxembourg returned 306.786 m from one 1 m Float64 overview block;
Wales returned 132.857 m from one Deflate block after downloading the official
OSTN15 Lite grid. Their cache repeats used no downloads. An exact Swiss 1 km seam
probe used both adjacent tiles without HGT fallback. Earlier validation includes
Mapzen HGT, Settings/UI suites and a Release application build; those larger suites
were not repeated for this milestone. Flanders, Sachsen-Anhalt and Wales were
subsequently confirmed working by the user; Sachsen-Anhalt was tested near
Magdeburg at `52.1310, 11.6390`, and Wales at `52.1394178, -4.5713131`.
Estonia 1024 was tested but its catalogue still uses 512.

## File-source status

Five file-source profiles are implemented:

- `format: "hgt"`, `fileGrid: "degree"`: local or automatically downloaded
  Mapzen Skadi cells and the catalogue default/fallback;
- `format: "geotiff"`, `fileGrid: "projected"`: fixed projected file tiles with
  an HTTPS name template, explicit revision and strict range-COG reads; Austria
  is the first verified profile;
- `format: "geotiff"`, `fileGrid: "cog"`: one large direct COG URL with exact
  resolution-overview selection and range-only block caching; Luxembourg and
  Wales are verified profiles;
- `format: "geotiff"`, `fileGrid: "stac"`: STAC discovery with resolution/CRS
  asset selection and preserved complete source TIFFs; Switzerland is the first.
- `format: "geotiff"`, `fileGrid: "directory"`: recursively indexed local
  GeoTIFF collections with arbitrary filenames; Portugal DGT MDT-2m is the first.
  `.tsre-elevation-index.json` caches bounds and georeferencing so only files
  overlapping the requested terrain are decoded.

These profiles validate a reusable direction, not arbitrary GeoTIFF compatibility.
Each new product still needs its real compression, metadata placement, sample
type, tiling, update/version behavior and request limits checked before catalogue
enablement. Distant terrain needs explicit overview/coarse acquisition work.
