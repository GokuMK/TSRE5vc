# Elevation: current implementation and review

Updated 2026-09-21 for the current `feature/geo-terrain` working tree.
Start with the [agent handoff guide](README.md) for reading order, code map and
source-addition workflow. These follow-ups are committed together with the guide.

Newest follow-ups appear first. Sections headed as the original review retain
historical findings from `a88f7f8` and the R1/R2 follow-up on `1ee5aaa`; their
counts and validation claims are scoped to those milestones. They do not replace
the newest status or the current source code.

## Luxembourg and Wales single-file range COGs, 2026-09-21

- `fileGrid: "cog"` now represents one direct national COG. It follows linked
  overview IFDs, selects an exact configured resolution, downloads external tile
  offset/count tables separately, and caches only required compressed blocks.
- Luxembourg ACT DTM 2024 uses the exact 1 m overview of the roughly 40 GB,
  0.5 m EPSG:2169 national COG. The source is LZW Float64; decoded values are
  converted into the existing Float32 raster buffer. The local converter applies
  ACT's official ETRF2000-to-LUREF2020 Molodensky-Badekas parameters followed by
  Luxembourg TM. Four official converter controls agree within 2 cm.
- Wales uses the official 1 m EPSG:27700 Welsh Government DTM COG, approximately
  48.6 GB. Its Float32 blocks use Deflate. Accurate placement uses Ordnance
  Survey's OSTN15/OSGM15 Lite 20 km shift grid, downloaded on first use from the
  official 1.26 MB developer ZIP. Only the 99,959-byte text grid is retained in
  `assets/geo/`; the archive is not retained. OSGM15 heights are not applied to
  the already-orthometric ODN raster values.
- A bounded Luxembourg probe downloaded 262,144 bytes of header, 3,440,640 bytes
  of index tables and one 52,805-byte block. It returned 306.786 m and 306.62 m;
  the repeat was cache-only in 10 ms.
- A bounded Wales probe downloaded 262,144 bytes of header, 8,166,204 bytes of
  index tables and one 76,600-byte block, plus the one-time transform archive.
  It returned 132.857 m and 132.837 m; the repeat was cache-only in 20 ms.
- The user subsequently confirmed the Wales source in the application at
  `52.1394178, -4.5713131`.
- **378 focused Release checks pass.** The full application build and main UI
  suites were not run for this milestone.
- The user confirms Flanders and Sachsen-Anhalt work in the application.
  Sachsen-Anhalt was tested near Magdeburg at `52.1310, 11.6390`; no separate
  quantitative block/cache/coverage-edge report was retained.

These detailed profiles remain unapproved for distant terrain.

## Projected COG and STAC GeoTIFF file sources, 2026-09-21

- Austria BEV ALS-DGM 1 m is the first `fileGrid: "projected"` source. Its 2025
  EPSG:3035 mosaic consists of roughly 6.8 GB, 50 km BigTIFF COG files. TSRE reads
  a bounded index range and fetches only required LZW internal blocks, up to four
  concurrently. A complete locally supplied official TIFF is window-read directly.
- Switzerland and Liechtenstein swissALTI3D is the first `fileGrid: "stac"` source. TSRE queries the
  official collection, selects the newest 2 m EPSG:2056 asset for each footprint,
  skips 0.5 m files, and preserves each roughly 1 MB official TIFF in the named
  source directory.
- STAC discovery includes the source-pixel interpolation margin and joins adjacent
  files on their common grid. An exact 1 km boundary probe used both Swiss source
  tiles for both samples, with zero HGT fallback; this fixes the visible one-value
  seam lines found during the first main-application test.
- Shared raster support now includes LZW Float32 blocks and the floating predictor.
  HTTP ranges require an exact 206/Content-Range response. EPSG:3035 and EPSG:2056
  were added locally; there is no new external dependency or country-specific
  terrain-generation path.
- **371 standalone checks passed**. Vienna returned about 171.6 m after a 256 KiB
  index and one 586,993-byte block; Bern returned about 540.3 m after one
  975,801-byte 2 m tile. Both repeats were cache-only. These are bounded interior
  probes; the seam was subsequently reproduced and fixed as described above.
- Distant terrain remains disabled/unapproved for these detailed acquisition
  profiles until overview/coarse selection and separate cache identity exist.

See [local file sources](local-elevation-sources.md) for the implemented contract
and the [Europe tracker](tsre_europe_elevation_sources_tracker_v3.md) for source
status and next candidates.

## Catalogue file source and automatic World HGT, 2026-09-19

- The former empty-ID, hardcoded HGT backend is now the first normal catalogue
  dataset: `world-hgt`, `provider: "file"`, `format: "hgt"`, `fileGrid: "degree"`.
  Its bounds are the whole geographic world. `defaultFileSource` selects it by
  ID; array order is only a fallback when that field is absent or invalid.
- The entry directly defines `directory: "world_hgt"`. Runtime no longer searches
  legacy `hgt/` or the geodata root. Users move legacy files explicitly. Raw
  `.hgt` files take precedence over compressed `.hgt.gz` files.
- An optional catalogue `download` object makes a file source automatic. World
  HGT downloads missing Mapzen/Tilezen Skadi tiles anonymously from AWS, up to
  four at once. Files stay gzip-compressed. Decompressed size, HGT grid, gzip CRC
  and ISIZE are checked before an atomic write; provenance and SHA-256 are stored
  in a sidecar. Manual-only file sources omit `download`.
- Service generation now samples the primary source first and prepares the file
  fallback only for unresolved points. This avoids downloading global fallback
  tiles where WCS or ArcGIS supplied all heights. The selected file source has no
  recursive fallback.
- Settings and the height window use the catalogue entry directly. Old empty
  profile values migrate to `world-hgt`; reports and missing-file checks use
  generic file-source/fallback wording. The legacy `GeoHgtFile` reader shares the
  same catalogue directory and compressed reader.
- **359 standalone checks passed**. A live probe at `(50.05,19.05)` downloaded
  `N50E019.hgt.gz` (5,447,758 bytes), wrote its metadata sidecar and sampled two
  241 m heights with one download and no fallback. Repeating used the stored
  compressed file with zero downloads. The Release application build
  succeeded; Settings passed 246 checks and elevation UI passed 61 checks.

See [local file-source implementation](local-elevation-sources.md) and the
[global source review](global-sources-review.md). This 2026-09-19 milestone was
subsequently extended by the COG/STAC work recorded above.

## Critical open issue: distant terrain

[Distant-terrain elevation acquisition](distant-terrain-elevation.md) currently
reuses the detailed dataset's download resolution for roughly 32 x 32 km tiles.
Local averaging does not reduce downloads. Each service needs an explicitly
validated coarse request profile and separate cache identity, or must be denied
in distant mode. The linked review records current call paths, size estimates,
late NoData-fill limits and required preflight checks. **Not implemented yet**;
ordinary country-service validation does not establish distant-terrain suitability.

## Height selector and local-file preparation, 2026-09-19

- Height source combo uses the existing TSRE `combobox-popup: 0` style.
- On opening, the height dialog filters sources against a projected 3 x 3 sample
  of the actual tile footprint, with a **10 km ground buffer**. Mercator scale
  and geographic degree units are accounted for. Bounds are coarse availability
  hints, not exact coverage masks. HGT remains available; Settings lists all sources.
- A saved known source outside the filtered area leaves the combo unselected and
  preview disabled until the user chooses a local source. This does not silently
  overwrite the saved source or change automatic-generation settings. Unknown
  saved references remain visible and fail explicitly as before.
- HGT lookup now prefers `world_hgt/`, then `hgt/`, then the root. The legacy HGT
  reader uses this same lookup. See the [local-source design](local-elevation-sources.md)
  for named user-managed directories and regular-grid versus metadata indexing.
- The catalogue includes **19 valid online datasets**, including Flanders.
  A production probe at `(51.05,3.72)` returned two primary samples, no fallback,
  one download (about 1.1 s), then one cache hit/zero downloads (70 ms). Heights
  were 7.14634 and 7.21656 m. This is a small integration probe, not national
  coverage or full-tile validation.
- **353 standalone checks passed**, including the ground-distance filter and
  HGT directory precedence. Release build passed. Moved 114 existing HGT files
  into `C:/hgst/world_hgt`, with no overwrites and byte-length verification.
  Application UI regression assertions were updated; main UI suites were not run.
  The user subsequently confirmed that source hiding works correctly.

## Denmark and catalogue isolation, 2026-09-19

At this milestone all **18 datasets** loaded. Invalid catalogue objects are skipped individually,
with named diagnostics, rather than hiding every online source. Valid entries
remain usable; malformed JSON is still a file-level error.

[Denmark validation](denmark-validation.md) records the new query-key authentication,
`GTiff` request format and server-resampled **1 m** grid. Full-tile generation
returned 65,536 primary samples with no fallback (nine downloads); repeating used
only cache. **347 standalone checks passed**, including catalogue isolation and
query-key handling without credential exposure in metadata or errors. Release
application build succeeded; application UI suites were not run.

## England / Estonia integration, 2026-09-19

At this integration the catalogue contained **17 online datasets**. The new entries and the other
agent's suggestions have been checked with live requests and implemented in the
shared provider. See [England / Estonia validation](england-estonia-validation.md)
for reproduced failures, configuration, fixtures and remaining limits.

- England stays on WCS 2.0.1, with EPSG:3857 subset axes `X/Y` and separate scaling
  axes `i/j`. The server expands output bounds; opt-in `allowExpandedGrid`
  validates coverage and bounded expansion while retaining actual georeferencing.
- Estonia stays on WCS 1.0.0, with `requestFormat=image/tiff`. The TIFF decoder
  now accepts padding of the final strip to its declared RowsPerStrip.
- Both retain 2 map m request grids, valid zero/negative heights and default HGT
  fallback. No local EPSG:27700/3301 conversion or dependency was needed.
- Each generated **65,536 primary samples, zero HGT, zero unavailable samples**
  across cache-block boundaries near the user's test areas. England downloaded
  four blocks; Estonia twenty. Repeats used only caches with identical ranges.
- **337 standalone checks passed**, including live TIFF fixtures, separate axes,
  format defaults, expanded-grid limits, NoData and padded/truncated strips.
  The Release application build passed; application UI suites were not run.
- Follow-up block-size tests: Estonia also accepts 1024 cores. On the same tile,
  9 requests took 14.5-15.5 s versus 20 requests at 24.0-24.4 s for 512, with
  79% more downloaded bytes. Recommend 1024; catalogue remains 512 pending the
  configuration change. See the linked validation report for numeric differences.
- Distant-terrain suitability remains unvalidated and its restriction remains
  unimplemented: see the [critical open issue](distant-terrain-elevation.md).

## Original review: scope and result

Read-only source review of the changes after `2aaae65`: additional datasets,
WCS 1.0, uint16 TIFF, more raster CRSs, downsampling, and the committed route
projection work. Documentation alone was changed. No builds, application tests,
standalone tests or live endpoint probes were run during this review.

At review time, another agent was building New Route / RouteCreator / GeoPresetData
and route-loading changes. That work was subsequently committed as `1ee5aaa`.
Its source and task document were left untouched by this follow-up. The route
projection summary below describes the earlier committed projection milestone;
this report does not extend acceptance to the separate New Route feature.

The provider separation remains intact: national differences are predominantly
catalogue entries, while WCS and ArcGIS share download/cache management and the
same raster sampler. The two concrete integration issues below are now resolved;
the broader validation gaps remain open.

## R1/R2 follow-up: implemented and verified

- Settings definitions now support `withOptionsProvider(...)` and `asReference()`
  as code-owned, per-key properties. The elevation setting supplies HGT plus all
  catalogue entries, shared by the Settings UI and height dialog.
- Profiles store the selected string ID without an `options` list. Old stored
  lists are removed on load/save while preserving the ID. Unknown string IDs are
  permitted in saved settings, session values and CLI overrides. Ordinary enums
  remain strict; numeric values are rejected for string references.
- Both dropdowns preserve an unavailable ID explicitly. Elevation generation
  reports an unknown dataset and applies no heights; it never silently selects
  HGT instead. Normal missing coverage within a known dataset still uses HGT.
- Tests compare catalogue IDs and selector/Settings contents rather than fixed
  historical sizes. The standalone suite locates its required fixture datasets
  by ID, so additional entries and catalogue ordering do not break its setup.
- Release build succeeded. Offline standalone elevation: **301 checks, 0 failures**.
  Application Settings: **232 cases, 0 failures**. Application elevation UI:
  **44 cases, 0 failures**. GUI suites ran offscreen with isolated profile files.
  Logs are in ignored `build-r1-r2/settings.log` and `build-r1-r2/elevation-ui.log`.
- Checks cover every configured source, dialog reopening, unknown-ID handling,
  old-profile migration, save/reload, CLI references, and strict static enums.
  No additional live service requests were made for this follow-up.

See [runtime choice semantics](../../settings-system.md#runtime-choices-and-reference-values)
for the reusable settings API. The original findings below retain their review
context; referenced source line numbers describe the pre-fix revision.

## Resolved review findings

### R1 - Resolved: new datasets could not become the active Settings source

At review time the embedded catalogue had **13 online datasets**, but
`src/settings/SettingsRegistration.cpp:368` registered only HGT and the
original four Polish/Czech IDs for `geo.elevation.source`.

The height dialog builds its list directly from the catalogue. Selecting a new
dataset calls `setSessionValue()` at `HeightWindow.cpp:98`, which rejects IDs
absent from the enum (`SettingsManager.cpp:568`); the dialog ignores that return
value. Manual preview reads the combo directly and can work, while automatic
generation reads Settings (`HeightWindow.cpp:147`) and retains the previous
source. Reopening the dialog also restores that previous selection. The Settings
dropdown and CLI enum parser cannot select the nine new IDs either.

- [x] Make the Settings choices and height dialog use the same catalogue, keeping
  HGT as the default. Handle a failed setting update visibly.
- [x] Check every catalogue ID can be selected, survives dialog reopening and
  reaches automatic generation; check saved Settings survive restart.

### R2 - Resolved: stale test counts stopped the standalone suite early

At review time `tests/geo/ElevationTests.cpp:140` required four datasets and immediately
returns failure otherwise. With the current 13 entries it exits before the
cached-generation, Czech WCS, ArcGIS and download-transport checks at the end.
`src/tsre/tests/ElevationUiTestSuite.cpp:44` likewise expects five selector items,
where the current UI constructs fourteen including HGT. The Settings test still
expects five enum choices, so it currently reinforces the R1 mismatch.

- [x] Replace historical counts with catalogue/selector/Settings consistency
  checks while retaining assertions for required known IDs and their behavior.
- [x] Run the standalone and application suites after those changes. Results are
  recorded above; the older **300 passing checks** belong to `2aaae65`.

### Validation gaps — new behavior is not covered by the existing fixtures

The changed test calls pass `targetSpacing=1.0`. Existing Polish/Czech cached
generation cases therefore retain point sampling and do not exercise the new
area filter used by normal coarser terrain grids. This is a coverage gap, not a
claim that filtering is broken.

- [ ] Cover fine-to-coarse sampling with known non-flat input, block boundaries,
  and NoData in a footprint whose center is valid. Check HGT/report behavior.
- [ ] Cover Web Mercator latitude scaling, unchanged equal/coarser-source sampling,
  and cancellation with the bounded maximum filter size.
- [ ] Add WCS 1.0 URL/response fixtures, uint16 values above 32767 and explicit
  NoData, and bare WCS TIFFs without CRS keys including conflicting-CRS rejection.
- [ ] Add independent coordinate references for newly supported raster CRSs.
  The previous CS92/UTM33 reference checks do not validate every new projection.
- [ ] Record successful download, cache reuse and coverage-edge behavior for the
  nine new dataset entries. Existing committed service fixtures cover Poland and
  Czechia; configuration alone is not a fresh endpoint availability check.
- [ ] Complete route TM / legacy IGH round-trip and TRK save/reload checks already
  listed in the [projection notes](geo_Projection_TM_Implementation_Notes.md).

## Configured dataset inventory

These values describe `src/tsre/geo/elevation-datasets.json` at the reviewed
revision. Resolution means configured raster/output spacing; this review did
not independently re-establish native source resolution or service coverage.
All entries use TIFF except Polish EVRF2007, which uses ASCII Grid.

| Dataset ID | Area / product | Provider | EPSG | Spacing | Core pixels | Connections |
|---|---|---|---:|---:|---:|---:|
| `pl.gugik.nmt1.kron86` | Poland NMT, KRON86 | WCS 2.0.1 | 2180 | 1 m | 1024 | 4 |
| `pl.gugik.nmt1.evrf2007` | Poland NMT, EVRF2007 | WCS 2.0.1 | 2180 | 1 m | 512 | 1 |
| `cz.cuzk.dmr4g` | Czech DMR 4G, EVRS | WCS 2.0.1 | 3045 | 5 m | 256 | 1 |
| `cz.cuzk.dmr5g` | Czech DMR 5G, Bpv | ArcGIS | 25833 | 2 m | 1024 | 4 |
| `us.usgs.3dep.conus` | US 3DEP CONUS, NAVD88 | ArcGIS | 3857 | 2 map m | 1024 | 4 |
| `no.kartverket.nhm.dtm1` | Norway NHM DTM, NN2000 | ArcGIS | 25833 | 1 m | 1024 | 4 |
| `es.ign.mdt05` | Spain MDT05, REDNAP | WCS 2.0.1 | 25830 | 5 m | 512 | 1 |
| `si.gurs.dmr1` | Slovenia DMR1, national heights | ArcGIS | 3794 | 1 m | 1024 | 4 |
| `de.lgb.dgm1.bb-be` | Brandenburg + Berlin, DHHN2016 | WCS 2.0.1 | 25833 | 1 m | 1024 | 1 |
| `de.nrw.dgm1` | Nordrhein-Westfalen, DHHN2016 | WCS 2.0.1 | 25832 | 1 m | 1024 | 1 |
| `de.hessen.dgm1` | Hessen, DHHN2016 | WCS 2.0.1 | 25832 | 1 m | 1024 | 1 |
| `de.bw.dgm1` | Baden-Wurttemberg, DHHN2016 | WCS 1.0.0 | 25832 | 1 m | 1024 | 1 |
| `de.st.dgm1` | Sachsen-Anhalt, DHHN2016 | WCS 2.0.1 | 25832 | 1 m | 1024 | 1 |

The US entry explicitly specifies a **request grid**. Web Mercator map metres
are not ground metres: the sampler scales its footprint by `1/cos(latitude)`.
Do not describe that entry as a guaranteed uniform native 2 m survey product.
Rectangular catalogue bounds select candidate blocks; they are not coverage masks.

## Shared implementation changes

### Protocols and decoding

- `WcsProvider` now handles WCS 2.0.1 and 1.0.0. The latter sends `COVERAGE`,
  `BBOX`, `CRS`, `RESPONSE_CRS`, `WIDTH` and `HEIGHT`; TIFF is requested as
  `FORMAT=GeoTIFF`. That format spelling is currently a provider convention,
  not an independently configurable service format alias.
- WCS 2.0.1 retains named axes, repeated `SUBSET` and `SCALESIZE`. Hessen uses
  `E`/`N`; other configured WCS 2.0 datasets use `x`/`y`. Existing endpoint query
  parameters survive URL construction; NRW uses them to request uncompressed,
  predictor-free, untiled GeoTIFF.
- The TIFF reader now accepts uint16 as well as int16 and float32, still one
  numeric band in classic uncompressed TIFF, using strips or storage tiles.
  RGB, compression, unsupported predictors and BigTIFF remain outside its contract.
- Bare WCS TIFFs may now inherit the configured EPSG when CRS keys are absent.
  This is broader than Stage A's GML-only inference. Explicit conflicting TIFF
  CRS still fails; multipart responses still require matching GML and TIFF parts.
  The plain `readGeoTiff` path used by ArcGIS still requires CRS metadata.
- Download/cache validation still requires exact dimensions, origin, spacing and
  CRS. Cache paths hash the dataset definition except the sampling-only
  `noDataPolicy`, so changing DMR 5G from
  one to four connections also produces a new cache directory. Old directories
  are retained; no cache was deleted during this review.

### Sampling and fallback

`generate()` now receives output vertex spacing from `HeightWindow`.
For a projected source finer than that spacing by more than 5%, `RasterSource`
averages a square grid of bilinear samples over one output footprint. There are
`ceil(footprint/sourceSpacing)` taps per axis, clamped to 2..32. Equal-resolution
and coarser sources retain the prior point/bilinear path. Geographic EPSG:4326
rasters bypass this filter; HGT retains its legacy sampler.

Preparation includes footprint corners to acquire neighboring cache blocks.
Any invalid tap rejects the primary result for that output vertex and invokes
the existing HGT fallback; partial footprints are not averaged. Consequently a
valid center close to a national boundary may still use HGT. Report counts are
output vertices, not the number of filter taps. Raw cached raster resolution
does not change with output spacing.

Polish and Czech entries keep `zeroIsNoData=true`; the nine new entries preserve
zero as a valid numeric height unless another NoData indication applies. There
is still no vertical datum conversion, secondary online source or zero-fill
Apply policy. Dataset-level NoData filling is described below. The
[border/fallback issue](tsre_geoportal_generic_elevation_ideas.md) remains open.

### Two separate projection layers

1. `Geo::CrsTransform` converts geographic sample positions into a dataset CRS:
   EPSG:4326, 2056, 2180, 3035, 3857, 3794, 3045, 3067 and 25828..25838 are
   accepted, with bounded geographic domains. It is constructed once per source
   and reused for raster lookups; see `crs-transform.md`.
2. `GeoCoordinates` maps route/world positions to geographic positions. The
   committed projection milestone adds local GRS80 Transverse Mercator with
   scale 1, alongside IGH and the legacy local equirectangular converter. A shared
   tile mapping and factory are selected through `TsreGeoProjectionType`; old
   TRKs infer IGH or the legacy local projection. This does not select an elevation
   source or replace the source's EPSG transform.

The route projection origin is distinct from the route start. Existing manual
TM observations are recorded in the projection notes; this review did not rerun
them. No new runtime/build GIS dependency is introduced by these changes.

## Recommended next work

### Dataset NoData policy, 2026-09-19

Catalogue entries accept `"noDataPolicy": "fallback"` (default when omitted)
or `"noDataPolicy": "fill"`. Netherlands AHN opts into fill.

- `fallback` retains the existing strict sampling and HGT fallback behavior.
- `fill` joins the available downloaded blocks in memory and fills NoData before
  bilinear sampling / terrain-footprint averaging. Each simultaneous layer uses
  the mean of adjacent valid heights, growing inward until no reachable holes
  remain. Original valid heights, including negative heights and legitimate
  zero, stay unchanged. Filled heights are estimates; the report counts filled
  source pixels separately from terrain samples.
- Joining cache blocks allows holes to fill across their boundaries. Missing
  downloads are masked out, not filled through. An entirely empty connected area
  remains NoData and fails generation rather than inventing a level or switching
  its NoData policy to HGT. Outside coverage / unavailable downloads retain the
  existing fallback behavior. The result can depend on the available surrounding
  area; this is local neighbour filling, not a globally interpolated product.
- Original cached rasters stay unchanged. `noDataPolicy` is excluded from cache
  identity, so switching policies reuses existing downloads. In-memory fill is
  bounded to 32 Mi source pixels; larger areas report an error and require a
  smaller generation area. Filling supports cancellation.
- A 256 x 256 terrain grid across four of the user's AHN cache blocks produced
  **65,536 valid primary samples, zero HGT, zero downloads**, after filling
  **407,119 source pixels**; cached generation took about **540 ms**. This tests
  a complete 2 km terrain grid and cache-block boundaries, not just two points.
- Offline standalone: **320 checks, 0 failures**, covering neighbouring means,
  multi-layer filling, preservation of measurements/zero/negative heights,
  explicit voids, unavailable barriers, all-empty areas, cancellation and cache
  identity. Probe source and logs are in ignored `build-fi-nl-research/`.
  Release application rebuild passed, including English/Polish fill-report text.

### Finland / Netherlands follow-up, 2026-09-19

At that follow-up the catalogue contained **15 online datasets**. Baden-Wurttemberg's entry
includes the research caveats below using the same `notes` array as the new entries.

- Finland `fi.nls.dem2` keeps native EPSG:3067 and 2 m spacing: the shared local
  TM conversion already supports that CRS. Its earlier unsupported-CRS note was
  incorrect. The catalogue now references `geo.elevation.fi.nls.apiKey` in the
  active profile's secrets file via `authentication.type=basic-api-key`.
  [Setup and credential handling](../../settings-system.md#elevation-service-api-keys)
  use HTTP Basic, with no API key in URLs or cache metadata. Authenticated live
  validation subsequently passed after removing the uppercase `GEOTIFF:*`
  endpoint parameters: NLS rejected each tested uppercase compression/predictor/
  tiling option with an HTTP 200 HTML "Request Rejected" page. Default output is
  already uncompressed Float32 TIFF. Block `312,-3419` returned 1026 x 1026 at
  exactly 2 m in EPSG:3067, with heights 94.348..216.098 m. The shared provider
  generated a full 256 x 256 terrain grid inside that block: **65,536 primary
  samples, one download, no HGT**, in about 2.5 seconds. This used the profile key
  only in Authorization; no secret was stored in the probe logs or cache metadata.
  The downloader now reports HTTP 200 HTML responses as service errors rather
  than passing them to the TIFF decoder. Release build and **321 standalone
  checks** passed. Evidence/logs are in ignored `build-fi-nl-research/`.
- Netherlands `nl.pdok.ahn.dtm05` uses WCS server-side horizontal reprojection
  from RD New to EPSG:25831 (ETRS89 / UTM 31N), already supported by TSRE. The
  catalogue's `crs`, conservative bounds and 1 m spacing now describe the
  request/cache grid; NAP heights remain unchanged. `SUBSETTINGCRS`, `OUTPUTCRS`
  and uncompressed TIFF options use the existing endpoint-query configuration.
  No local RD projection/datum implementation or new dependency was added.
- PDOK capabilities advertise EPSG:25831. Live requests returned exact 32 x 32
  and 1026 x 1026 grids at 0.5 m spacing. Default TIFF uses Deflate; explicitly
  disabling compression produces supported Float32 data, including negative
  heights and explicit NoData. The shared provider successfully generated two
  samples near `(52.095,5.184)` with heights 1.49769 and 1.54317 m, one download
  and no HGT fallback; repeating used one cache hit and zero downloads.
  A city-center probe at `(52.09,5.12)` encountered NoData at both samples and
  correctly rejected generation without local HGT. Coverage is not assumed complete.
- Follow-up: verified a 1026 x 1026 uncompressed Float32 response with exact
  **1 m** spacing (4,217,308 bytes). Changed the Netherlands request/cache grid
  to 1 m; native AHN coverage remains `dtm_05m`. Each block now covers four times
  the area at approximately the same file size, reducing data per unit area by
  about 75% (actual block counts depend on alignment). The changed definition
  selects a new cache identity; previously cached 0.5 m files are not deleted.
  Release rebuild and all 311 standalone checks passed with this configuration.
  Live generation at the same control location returned 1.49218 and 1.53933 m
  from one downloaded 1 m block, with no fallback.
- Release application build succeeded. Offline standalone suite: **311 checks,
  0 failures**, including missing/invalid key reporting, Authorization headers,
  credential isolation between waves and same-origin/cross-origin redirects.
  Main application test suites and manual UI testing were not rerun. Live
  responses and the build log are in ignored `build-fi-nl-research/`.

Sources: [NLS WCS technical description](https://www.maanmittauslaitos.fi/ortokuvien-ja-korkeusmallien-kyselypalvelu/tekninen-kuvaus),
[NLS API-key instructions](https://www.maanmittauslaitos.fi/en/rajapinnat/api-avaimen-ohje),
[PDOK WCS capabilities](https://service.pdok.nl/rws/ahn/wcs/v1_0?SERVICE=WCS&REQUEST=GetCapabilities&VERSION=2.0.1).

### Baden-Wurttemberg precision

The subsequent [Baden-Wurttemberg WCS 2 investigation](baden-wurttemberg-wcs-research.md)
reproduced separate subset/scaling axis names and a native-size rounding problem.
Both WCS versions returned the same integer-height pixels when their output grids
matched. Retain WCS 1 for BW; investigate original tile downloads for better
vertical precision. This was isolated endpoint research, with no application changes.

R1 and R2 are complete. Next validate the new sampling/protocol/CRS behavior
listed in the open checklist. Keep New Route in its own acceptance scope. Return
to selectable border fallback after those checks.
