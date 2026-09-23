# Geo elevation: start here

Agent handoff, updated 2026-09-23. This directory is sufficient introductory
context **when read in the order below**. The implementation and current service
responses remain authoritative for code changes; historical milestone reports
are not a current specification.

## Reading order

1. [Current status](elevation-current-status.md): newest sections first. Later
   sections retain older review snapshots and their original counts/results.
2. [Distant terrain](distant-terrain-elevation.md): critical acquisition problem;
   detailed-service validation does not approve distant terrain.
3. Choose the relevant track:
   - New WCS/WMS/ArcGIS service: the workflow below, then
     [England/Estonia](england-estonia-validation.md) and
     [Denmark/authentication](denmark-validation.md). These cover recent protocol
     differences; use [Czech ArcGIS](czech-arcgis-validation.md) for that provider.
   - Downloaded/offline sources: [file-source implementation and next step](local-elevation-sources.md).
     One-degree HGT, GEDTM30 global range COG, Austria projected range-COG tiles,
     Luxembourg and Wales single national range COGs,
     Switzerland/Liechtenstein STAC/GeoTIFF tiles, Sweden authenticated
     root-STAC/range-COG assets, and indexed user-managed
     GeoTIFF directories are implemented; other
     GeoTIFF families still require profile validation.
   - Indexed online source sheets: [Poland GUGiK EVRF2007](poland-gugik-source-sheets.md).
     This describes the generic WFS asset catalogue, direct ASCII/ZIP acquisition,
     one-time local GeoTIFF conversion and cache behavior.
   - Current national-source research: [Sweden, Wallonia and France](sweden-wallonia-france-research.md).
     This records the authenticated Swedish STAC path, Wallonia's original bulk
     delivery limitation and later COG integration, and the French MNT LiDAR HD
     numeric-WMS route.
   - Extracted national BigTIFF research: [Slovakia and Wallonia](slovakia-wallonia-bigtiff-review.md).
     This records bounded server-range and raster-layout probes, the completed
     COG conversions, and their generic TSRE range-COG integrations.
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
| Catalogue | Built-in `src/tsre/geo/elevation-datasets.json`, optional user `assets/geo/elevation-datasets.json`; `Dataset`, `datasets()`, `builtInDatasets()`, `parseDatasets()` and `mergeDatasets()` in `ElevationSource.h/.cpp` |
| Providers/cache | `ElevationSource.cpp`: `FileHgtSource`, WCS/WMS/ArcGIS service providers, `RasterSource`, `generate()`; `CogElevationSource.cpp`: projected, single-file range-COG, STAC and indexed local-directory GeoTIFF sources; `WfsElevationSource.cpp`: WFS sheet discovery, ASCII/XYZ import and persistent converted GeoTIFF cache |
| Requests/grid checks | `coverageUrl()`, `imageServerUrl()`, `validateRasterGrid()`, `cacheRelativePath()` |
| CRS conversion | `src/tsre/geo/CrsTransform.h/.cpp`: small compiled-in forward transforms, constructed once per source; see `crs-transform.md` |
| Numeric raster | `src/tsre/geo/ElevationRaster.h/.cpp`: `Raster`, ASCII/XYZ/HGT readers, axis normalization, sampling and `fillNoData()`; `ElevationTiffCodec.cpp`: bounded uncompressed/LZW/Deflate block decoding, Int16/Int32/Float32/Float64 conversion, predictors and the narrow local tiled-GeoTIFF writer; `FileHgtSource`: direct signed-16-bit HGT sampling with a bounded shared tile cache |
| HTTP/auth | `src/tsre/geo/ElevationDownload.h/.cpp`: `downloadWave()`, strict `downloadRangeWave()`; query and Basic credentials added only inside transport |
| Height UI | `src/tsre/geo/HeightWindow.cpp`; 10 km location filter via `nearDataset()` |
| Settings | `src/settings/SettingsRegistration.cpp`: dynamic source options and reference-valued setting |
| File-source lookup | `defaultFileSourceId()`, `findHgtFile()`, `readHgtFile()`; also used by `GeoHgtFile.cpp` and the missing-file checker |
| Tests | `tests/geo/`; `src/tsre/tests/ElevationUiTestSuite.cpp` |
| User-facing text | `translations/tsre_en.ts`, `translations/tsre_pl.ts`; use existing translation IDs/style conventions |

Actual C++ common type is `Elevation::Raster` (called ElevationRaster in design
diagrams). Acquisition prepares local data; height sampling does not perform HTTP.
Source IDs are dynamic Settings reference values, so new entries need no hardcoded
Settings enum changes. Invalid objects are skipped with diagnostics. Missing
selected sources fail explicitly.

## User elevation catalogue

Users may add or override elevation sources in
`assets/geo/elevation-datasets.json`. This stable local path is deliberately
outside `appdata/<Game::AppDataVersion>` so a TSRE data-version update does not
move or replace it. The file is optional and is not shipped or tracked by TSRE.

It uses the same top-level format and dataset schema as the built-in catalogue:

```json
{
  "version": 1,
  "datasets": [
    {
      "id": "example.my-source",
      "name": "My elevation source"
    }
  ]
}
```

Copy a similar complete built-in entry and change its ID, service details,
bounds and notes. The short example above only illustrates the enclosing shape;
it is not a valid source by itself. An optional `defaultFileSource` may name a
valid built-in or user-defined file source.

Built-ins load first. A valid user entry with the same ID replaces that entry at
the same list position; a new ID is appended. Invalid user objects are reported
and skipped, so an invalid override leaves the built-in entry available. A
malformed or unreadable user file also leaves the complete built-in catalogue
available. The Height window information panel identifies the selected entry as
`Built-in` or `User-defined`.

Authentication values remain in the secret profile. User catalogue entries may
refer to secret keys through the normal `authentication` fields, but must not
contain API keys, usernames or passwords directly.

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
   | `provider`, `endpoint`, `coverage`, `format` | Service providers: `wcs-2.0.1`, `wcs-1.0.0`, `wms-1.3.0`, `arcgis-imageserver`; decoder formats TIFF/ASCII as supported by the provider. File sources use the separate contract below |
   | `style` | Optional WMS style. France uses `normal`, the raw numeric style; do not select a hillshade style |
   | `requestFormat` | Optional wire-format spelling, independent of decoder format; WCS 1 defaults TIFF to `GeoTIFF`, Estonia uses `image/tiff`, Denmark `GTiff` |
   | `axisX/Y`, `scaleAxisX/Y` | WCS 2 subset and optional separate scaling axes; scaling defaults to subset axes |
   | `crs`, `resolution`, `origin`, `bounds` | Request/cache grid, not necessarily native source grid; bounds are conservative rectangles, not coverage masks |
   | `blockPixels`, `concurrentRequests` | Core size 16..1024, concurrency 1..4; requests include a one-pixel halo on every side |
   | `allowExpandedGrid` | Opt-in bounded server-expanded envelope; retains exact dimensions/CRS and actual returned transform; do not turn it on to conceal an unexplained mismatch |
   | `zeroIsNoData`, `noDataPolicy` | Default valid zero; policy `fallback` or explicit `fill`; fill does not fabricate wholly empty regions or bridge unavailable downloads |
   | `fallbackApproved`, `distantTerrainApproved` | Explicit UI eligibility flags; both default to false. Distant terrain currently offers only World HGT and GEDTM30 |
   | `authentication` | Secret references only: `basic-api-key`, `query-api-key` with `parameter`, or STAC `basic-user-password` with `usernameSecret` and `passwordSecret`; see Settings and Denmark/Sweden notes |
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
  GEDTM30, Austria, Luxembourg, Wales and Sweden, including signed Int32 scale/offset,
  Float64-to-Float32 conversion, explicit geographic overview factors, exact
  projected-resolution overview selection, Float32 horizontal predictor 2 and
  external block tables. This is
  still a narrow numeric elevation profile; RGB and arbitrary TIFF layouts are absent.
- Ordinary service responses are bounded to 32 MiB. WFS-discovered source sheets
  allow 96 MiB compressed downloads and 256 MiB bounded ZIP expansion. TIFF dimensions remain limited to 16 Mi pixels; requests
  to 2048 blocks; fill mosaics to 32 Mi pixels. These are emergency guards, not
  sufficient distant-terrain budgeting. Check source constants before changing them.
- Coarser output-vertex sampling currently reduces neither acquisition resolution
  nor download volume. Distant editing is restricted to entries with
  `distantTerrainApproved: true`; per-source coarse acquisition profiles remain
  future work.
- Cache identity hashes the dataset definition except `noDataPolicy`. Changing
  notes or concurrency can therefore also select a new cache directory. Keys are
  resolved separately and never belong in that definition or cached request URLs.
- User files belong in stable named directories. World HGT uses only
  `geoPath/world_hgt/`; no legacy root or `hgt/` search remains. Raw `.hgt` files
  take precedence over `.hgt.gz`. Preserve originals and keep derived indexes disposable.
- The Height window and automatic generation use one selected fallback. Only
  catalogue entries with `fallbackApproved: true` are offered; currently these
  are World HGT and GEDTM30. A fallback never receives a third recursive source.
  The main and fallback rows have independent Y offsets, applied according to
  which source supplied each sample. Retaining existing terrain heights over
  unresolved samples remains open.

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

For an explicit performance grid, use:

```powershell
./build/tsre_elevation_tests.exe --live-grid ROOT DATASET_ID LATITUDE LONGITUDE SIDE LATITUDE_SPAN LONGITUDE_SPAN
```

This remains an opt-in live/cache probe. `SIDE=2048` creates exactly 4,194,304
points and is useful for detecting work accidentally repeated per output point.

After a change, run appropriate standalone checks and the relevant build when
allowed. The current elevation milestone passes **419 standalone checks**. Bounded
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

GEDTM30 returned 241.95 m and 242.01 m near `(50.05, 19.05)` from one signed
Int32/Deflate COG block with its 0.1 m scale applied. The repeat was cache-only in
103 ms.

The all-provider hot-path audit found per-output filesystem discovery only in the
former HGT implementation. Cached 2048 x 2048 probes now take about 0.36 s for
HGT, 0.66 s for GEDTM30 and 3.32 s for Austria. WCS/ArcGIS sampling has a
current-block fast path; COG preparation retains compact bounds instead of a
second projected-point array. See `elevation-current-status.md` for scope.

France MNT LiDAR HD returned 35.6531 m and 35.3925 m near Paris from one
anonymous 1 m numeric-WMS block. Its repeat was cache-only in 72 ms. A bounded
four-block probe downloaded three blocks concurrently in 7.1 s and produced
1,024 primary samples without fallback.

Poland EVRF2007 now uses the official WFS source-sheet catalogue instead of the
slow ASCII WCS. A four-sheet junction probe downloaded and converted 132.7 MiB
in about 54.6 s. Its offline repeat returned two primary heights with no fallback
in 546 ms; a one-sheet repeat took 120 ms. See the dedicated source-sheet report.

## File-source status

Five file-source profiles are implemented:

- `format: "hgt"`, `fileGrid: "degree"`: local or automatically downloaded
  Mapzen Skadi cells and the catalogue default/fallback. Sampling uses the
  correct shared-border grid and reuses decompressed cells across generation calls;
- `format: "geotiff"`, `fileGrid: "projected"`: fixed projected file tiles with
  an HTTPS name template, explicit revision and strict range-COG reads; Austria
  is the first verified profile;
- `format: "geotiff"`, `fileGrid: "cog"`: one large direct COG URL with exact
  projected-resolution or explicit geographic-overview selection and range-only
  block caching; GEDTM30, Luxembourg and Wales are verified profiles;
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
