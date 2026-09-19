# Elevation: current implementation and review

Reviewed 2026-09-19 on `feature/geo-terrain`, at `a88f7f8`.
R1/R2 follow-up implemented and verified the same day, on top of `1ee5aaa`.
This is the current status entry point. The Polish and Czech milestone reports
retain their historical measurements; they are not validation of every later change.

## Review scope and result

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
  CRS. Cache paths hash the complete dataset definition, so changing DMR 5G from
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
is still no vertical datum conversion, secondary online source, interpolation
fallback or zero-fill Apply policy. The
[border/fallback issue](tsre_geoportal_generic_elevation_ideas.md) remains open.

### Two separate projection layers

1. `ElevationRaster::project()` converts geographic sample positions into a
   dataset CRS: EPSG:4326, 2180, 3857, 3794, 3045, 3067 and 25828..25838 are
   accepted, with bounded geographic domains. These are raster lookup transforms.
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

R1 and R2 are complete. Next validate the new sampling/protocol/CRS behavior
listed in the open checklist. Keep New Route in its own acceptance scope. Return
to selectable border fallback after those checks.
