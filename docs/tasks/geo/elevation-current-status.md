# Elevation: current implementation and review

Updated 2026-09-23 for the current `feature/geo-terrain` working tree.
Start with the [agent handoff guide](README.md) for reading order, code map and
source-addition workflow. These follow-ups are committed together with the guide.

Newest follow-ups appear first. Sections headed as the original review retain
historical findings from `a88f7f8` and the R1/R2 follow-up on `1ee5aaa`; their
counts and validation claims are scoped to those milestones. They do not replace
the newest status or the current source code.

## Distant-terrain source gate, 2026-09-23

- Catalogue property `distantTerrainApproved` defaults to false. World HGT and
  GEDTM30 are the only currently approved entries.
- The Height window receives the current terrain domain from both terrain-library
  implementations. Distant editing filters both the main and fallback source
  rows to approved entries. Automatic distant generation also substitutes the
  approved default before acquisition when the detailed-terrain setting points
  to an unapproved source.
- This is the immediate request-safety gate. Dataset-specific coarse resolution,
  overview choice, cache identity, request budgets and full 32 km validation
  remain in the dedicated later task.
- **419 focused Release checks pass**, including approval defaulting and schema
  validation. The incremental Release application build succeeds; the embedded
  UI suite was compiled but not run.

## Optional persistent user catalogue, 2026-09-23

- TSRE loads the embedded elevation catalogue first, then optionally reads
  `assets/geo/elevation-datasets.json`. The location is independent of
  `Game::AppDataVersion`, is not packaged, and remains under user control.
- Valid user entries replace built-ins with the same ID without moving their
  list position; new IDs append. The optional user `defaultFileSource` may name
  either a built-in or user-defined file source.
- Invalid user objects are diagnosed independently. An invalid override leaves
  the built-in entry available, and malformed, unreadable, empty or oversized
  user catalogue files leave all built-ins available.
- The Height window source information identifies whether the effective entry
  came from the built-in or user catalogue. Authentication continues to use
  secret-key references rather than credentials stored in catalogue JSON.
- Stable tests parse the built-in catalogue directly, while focused merge tests
  cover replacement, append, default selection and failure isolation.
- **417 focused Release checks pass.** The main application build/UI check is
  left for the user.

## Poland EVRF2007 WFS source sheets, 2026-09-23

- `pl.gugik.nmt1.evrf2007` retains its ID and vertical datum but now uses the
  generic `wfs-file-catalog` provider. It discovers current 1 m records through
  the official GUGiK WFS index and downloads up to four original sheets at once.
- Arc/Info ASCII and bounded nested ZIP/XYZ inputs are normalized to conventional
  easting/northing and converted once to tiled Deflate Float32 GeoTIFFs under
  `pl_gugik_nmt1_evrf2007`. Per-file sidecars retain URL, sheet, year, source
  byte count, hash and bounds; a catalogue index enables offline cache use.
- Source axes are matched to the WFS footprint. This is necessary because the
  inspected files did not use one consistent apparent X/Y convention.
- Request-aligned mosaics provide interpolation neighbours across official sheet
  boundaries. An exact four-sheet junction returned two primary samples with no
  fallback after this fix.
- The four live 2025 source files totalled 132.7 MiB and completed discovery,
  parallel transfer and conversion in about 54.6 s. An offline repeat used four
  cache files in 546 ms; a nearby one-sheet repeat took 120 ms.
- **412 focused Release checks pass.** The main application build/UI check is
  left for the user. Full details are in
  [the source-sheet report](poland-gugik-source-sheets.md).

## Slovakia and Wallonia national range COGs, 2026-09-23

- `sk.gku.dmr5.etrs89h` exposes the converted 128.3 GB national Slovakia DMR
  5.0 COG through the generic file/GeoTIFF/range-COG provider. Shared EPSG:3046
  support reuses the GRS80 UTM-zone-34 transform. Its EPSG:4937 heights are
  ellipsoidal; no vertical conversion is applied.
- `be.wallonia.spw.mnt1` exposes the converted 39.7 GB Wallonia MNT COG through
  the same provider. Shared EPSG:3812 support uses the existing Lambert
  Conformal Conic 2SP implementation. Its DNG/EPSG:5710 heights are
  orthometric.
- Both COGs use Float32 samples, 1024 x 1024 Deflate blocks, floating-point
  prediction and eight internal overviews. Bounded server checks confirmed
  exact byte ranges across each file, including offsets beyond 4 GiB.
- A Bratislava live probe returned 199.628 m and 199.639 m from Slovakia; a
  Namur probe returned 83.5877 m and 83.5103 m from Wallonia. Each first request
  downloaded one compressed block, and each repeat used one cache block with no
  download.
- **408 focused Release checks pass**, and the incremental Release application
  build succeeds.
- Detailed-source distant-terrain use remains unapproved pending explicit
  overview selection and practical 32 km validation.

## Selectable approved fallback, 2026-09-23

- The Height window has separate rows for the main elevation source and its
  fallback. Both selections apply to manual previews and automatic terrain
  generation through dynamic reference-valued Settings entries.
- Each row has its own Y offset. The generator applies the main offset only to
  primary samples and the fallback offset only to samples supplied by the
  fallback, allowing vertical-datum or DTM/DSM alignment without shifting the
  other source.
- Catalogue property `fallbackApproved` controls the fallback list. World HGT
  and GEDTM30 are the first two approved choices; detailed national sources are
  not offered as fallbacks until explicitly reviewed for that role.
- The shared generator accepts exactly one fallback source and prepares it only
  for unresolved primary samples. It never adds HGT or any other third source
  after the selected fallback.
- World HGT remains the default for existing profiles. Invalid, unavailable or
  non-approved fallback IDs fail explicitly rather than silently changing the
  selection.
- Preserving existing terrain heights when both sources remain unresolved is a
  separate open Apply-policy task.
- **404 focused Release checks pass**, including an offline HGT-void to GEDTM30
  fallback fixture and proof that a missing fallback does not invoke a third
  source. The incremental Release application build succeeds.

## Sweden authenticated STAC/range COG, 2026-09-23

- `se.lantmateriet.markhojdmodell1` provides Lantmäteriet's national 1 m
  Markhöjdmodell through the generic file/STAC provider. Catalogue discovery is
  anonymous; COG asset bytes use Basic Authentication resolved from separate
  username and password secret references.
- Root-level STAC search supports datasets split across changing regional
  collections. Configurable STAC fields select Sweden's
  `geometriskupplosning` property and validate compound asset EPSG:5845 while
  sampling the horizontal SWEREF 99 TM grid as EPSG:3006.
- Authenticated range requests use the same strict 206/Content-Range checks as
  anonymous COGs and restrict authenticated redirects to the same origin.
  Credentials are absent from URLs, errors, dataset definitions and cache data.
- The generic COG decoder now supports the verified Float32/Deflate TIFF profile
  with horizontal predictor 2 and 512 x 512 blocks.
- A Stockholm probe returned 31.361 m and 31.2604 m entirely from the Swedish
  source. It downloaded one compressed block; the repeat used one cache block,
  made zero downloads and returned identical values. A 16 x 16 wider probe then
  exercised 36 downloaded compressed blocks in four-request waves; its repeat
  used 37 cached blocks and made zero downloads.
- **398 focused Release checks pass**, the Release application build succeeds,
  and the source is user-confirmed in the application. Distant-terrain use
  remains unapproved.

## France numeric WMS and Lambert-93, 2026-09-22

- `wms-1.3.0` is a generic cached raster provider. Dataset configuration supplies
  the endpoint, layer, style, wire format, native CRS and the existing service
  block grid; acquisition reuses WCS cache, concurrency, TIFF validation,
  reporting and sampling.
- `fr.ign.lidar-hd.mnt05` selects IGN's anonymous raw MNT LiDAR HD layer. The
  source is a 0.5 m bare-earth model; TSRE asks the server for a 1 m Float32
  GeoTIFF grid and avoids storing four times as many native pixels.
- The shared CRS converter now implements ellipsoidal Lambert Conformal Conic
  2SP and the GRS80 Lambert-93 definition (EPSG:2154). The published false origin
  maps exactly to `(700000, 6600000)`, and the official IGN numeric control at
  48 degrees north, 2 degrees west agrees within 2 mm.
- IGN's TIFF encodes Lambert-93 as a GeoTIFF user-defined projection. The decoder
  accepts that marker only when service metadata supplies a supported expected
  EPSG code. Standalone user-defined rasters and explicit CRS conflicts remain
  rejected.
- A Paris probe returned 35.6531 m and 35.3925 m from one primary block, without
  fallback. The cache repeat completed in 72 ms. A four-block probe reused one
  cached block, downloaded three blocks concurrently in 7.1 s and returned all
  1,024 samples from the primary source.
- **393 focused Release checks pass**, and the Release application build
  succeeds. The UI suites were not repeated.

## All-provider hot-path audit, 2026-09-22

The HGT problem prompted a review of every `Source::prepare()` and
`Source::sample()` implementation, plus shared fallback generation.

- HGT was the only source performing filesystem discovery per terrain point.
  File lookup, gzip expansion and file reads now happen once per required cell;
  sampling reads the retained in-memory signed 16-bit tile.
- WCS, WMS and ArcGIS acquisition checks/downloads each distinct service
  block once. Sampling reads and decodes a cached raster only on first use. A
  numeric current-block fast path now avoids repeated `QMap`, path-string and
  `QCache` lookups while consecutive samples remain in that block.
- Projected and single-file range COG preparation already performs file/range
  reads per required compressed COG block. It now accumulates only projected
  min/max bounds instead of retaining and rescanning every projected output
  point. Sampling uses the completed in-memory raster window.
- STAC downloads and indexed-directory scanning occur once during preparation.
  Both build an in-memory mosaic, so their sampling paths contain no filesystem
  or network access. The directory index avoids rereading GeoTIFF metadata for
  unchanged user files.
- Fallback preparation receives only unresolved primary points. Neither primary
  nor fallback sampling performs HTTP requests.

Full cached 2048 x 2048 probes after these changes produced 4,194,304 samples in
about **0.36 s for HGT**, **0.66 s for GEDTM30**, and **3.32 s for Austria**.
Austria's path applies an EPSG:3035 transform to each point and covered 25 native
COG blocks; its repeat used those cached blocks and made no download. Per-point
CRS conversion and intentional multi-tap area filtering remain CPU work in the
generic sampler, but no provider performs repeated disk or network I/O per
output point.

## HGT sampling and repeated-generation correction, 2026-09-22

- The old HGT-only sampler treated an `N x N` HGT raster as `N` geographic
  intervals. HGT files have `N - 1` intervals because adjacent one-degree files
  share border posts. This shifted interpolation within every tile and emphasized
  the source grid. HGT now uses the raster's actual georeferencing.
- `FileHgtSource` no longer converts every post in a 3601 x 3601 tile into a
  52 MB Float32 raster before sampling a small terrain area. It retains the
  validated signed 16-bit big-endian bytes and decodes only the four posts used
  by each bilinear sample.
- A bounded, thread-safe 128 MiB process cache keeps recently expanded HGT tiles
  across separate terrain-generation calls. Automatic creation of adjacent MSTS
  tiles therefore does not repeatedly inflate the same `.hgt.gz` file. File size
  and modification time are part of the cache identity.
- HGT preparation now checks each unique one-degree cell once. Previously it ran
  `findHgtFile()` for every requested output point, causing a 2048 x 2048 detailed
  terrain tile to perform more than four million filesystem searches for the same
  file. Sampling likewise retains the current numeric cell instead of formatting
  and looking up its filename for every output point.
- **388 focused Release checks pass**, including an asymmetric HGT interpolation
  control that detects the former `N` versus `N - 1` error.

An exact 2048 x 2048 standalone grid probe using cached `N39W009.hgt.gz` generated
all **4,194,304 HGT samples in about 0.36 s**, including gzip expansion. The
equivalent cached GEDTM30 source probe took about 0.66 s after its bounds-only
preparation correction. This confirms the former half-minute
delay came from repeated filesystem discovery rather than gzip decompression.

This removes TSRE's placement and repeated-decoding costs. It does not invent
detail absent from HGT: Mapzen Skadi is a heterogeneous integer-metre composite,
and some regions contain 30–90 m source structure. GEDTM30 remains the stronger
bare-earth global source where it has coverage.

## GEDTM30 global bare-earth range COG, 2026-09-22

- `world.gedtm30` is a selectable automatic file source using the existing
  single-COG range provider. It reads the anonymous GEDTM30 v20250619 object and
  stores only index tables and required compressed blocks under
  `geoPath/world_gedtm30/`; the roughly 261 GB complete object is never fetched.
- Geographic single COGs use an explicit `download.overviewFactor`. This keeps
  angular source pixels separate from the nominal metre resolution shown in the
  catalogue. GEDTM30 selects factor 1, its 0.00025-degree base image.
- The generic TIFF block decoder now supports signed/unsigned Int32 and the
  horizontal integer predictor. The COG reader accepts EPSG:4326 GeoKeys, reads
  GDAL `SCALE`/`OFFSET`, applies the GEDTM30 0.1 m scale, and recognizes the raw
  Int32 NoData value before scaling.
- The source uses EGM2008 orthometric heights and covers approximately 57 degrees
  south to 84 degrees north. World HGT remains the fallback elsewhere and at
  voids. GEDTM30 is a machine-learning-derived model and its catalogue text
  directs users to validate it independently for critical work.
- A bounded live probe at `(50.05, 19.05)` returned 241.95 m and 242.01 m from one
  downloaded block. Repeating it made zero terrain downloads, reported one cache
  hit and completed in 103 ms.
- **387 focused Release checks pass**, including a self-contained geographic COG
  fixture covering Int32 predictor, scale and NoData behavior. The incremental
  Release application build passes; the main UI suites were not run.

GEDTM30's native spacing makes it a strong distant-terrain candidate, but a real
32 km distant tile and explicit source eligibility/cache policy remain open.

## Portugal indexed local GeoTIFF source, 2026-09-22

- `fileGrid: "directory"` is a generic user-managed GeoTIFF collection. It
  recursively discovers TIFFs, reads embedded georeferencing once and stores a
  disposable `.tsre-elevation-index.json` in the source directory.
- Index entries contain relative filename, size, modification time, dimensions,
  transform and bounds. Unchanged files reuse that metadata; only rasters whose
  bounds overlap the requested terrain are decoded for sampling.
- Portugal DGT MDT-2m is the first entry. Users download its 1 km, 2 m Float32
  GeoTIFF tiles through the login-protected DGT Data Centre and copy them below
  `geoPath/pt_dgt_mdt2m/`. TSRE performs no portal login and stores no DGT
  credentials.
- EPSG:3763 support uses the official PT-TM06/ETRS89 GRS80 Transverse Mercator
  definition. A DGT tile control agrees within 5 cm.
- Selecting a source in the Height window now displays its available attribution,
  licence, resolution, vertical datum, explanatory text and official links. A
  directory source also displays its local path.
- **382 focused Release checks pass**, including index creation, overlap loading,
  catalogue validation and the EPSG:3763 control. The incremental Release
  application build also passes with the source-information UI and translations.

Automatic Portugal downloads remain deferred until DGT exposes a documented
public or token-based API. The current browser-login form workflow is specific
to the DGT portal and is unsuitable as a generic authentication provider.

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

[Distant-terrain elevation acquisition](distant-terrain-elevation.md) still
reuses each approved dataset's ordinary download profile for roughly 32 x 32 km
tiles. Local averaging does not reduce downloads. An explicit eligibility gate
now denies unreviewed sources; only World HGT and GEDTM30 are approved. The
remaining task is to validate and configure any source-specific coarse request
profile, cache identity and preflight budget. Ordinary detailed-tile validation
does not establish distant-terrain suitability.

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
- Distant-terrain suitability remained unvalidated and unrestricted at this
  historical milestone. The current eligibility gate is described above.

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
Normal service providers return TIFF except where older sections say otherwise.
Polish EVRF2007 imports original ASCII or ZIP/XYZ sheets and stores local TIFF.

| Dataset ID | Area / product | Provider | EPSG | Spacing | Core pixels | Connections |
|---|---|---|---:|---:|---:|---:|
| `pl.gugik.nmt1.kron86` | Poland NMT, KRON86 | WCS 2.0.1 | 2180 | 1 m | 1024 | 4 |
| `pl.gugik.nmt1.evrf2007` | Poland NMT, EVRF2007 | WFS source catalogue | 2180 | 1 m | source sheets | 4 |
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
   EPSG:4326, 2056, 2169, 2180, 27700, 3035, 3045, 3067, 3763, 3794, 3857 and
   25828..25838 are accepted, with bounded geographic domains. It is constructed
   once per source and reused for raster lookups; see `crs-transform.md`.
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
