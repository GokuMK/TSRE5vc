# Generic terrain imagery: implementation task

Created 2026-09-24. This is the implementation task and design record for the
new **Load Imagery** workflow. The separate
[`tsre_imagery_sources_tracker_v2.md`](tsre_imagery_sources_tracker_v2.md) is a
source survey and status tracker, not this task's specification.

## Objective

Add a source-driven imagery workflow parallel to Load Height:

```text
imagery catalogue
       -> provider acquisition and persistent cache
       -> georeferenced RGB image sampling
       -> Load Imagery preview / report / Apply
       -> existing terrain map-overlay image
```

The first milestone must prove the same generic provider with:

1. **Poland - GUGiK Geoportal standard orthophoto**, for detailed terrain;
2. **World - ESA WorldCover Sentinel-2 RGB 2021 v2**, as the global source and
   the first distant-terrain source.

The result is an editor overlay for the selected terrain tile. Existing tools
may then show the overlay or make a terrain texture from it.

## Implementation status

Milestone one was implemented on 2026-09-24:

- `ImagerySource` loads and safely merges the built-in and optional
  `assets/geo/imagery-datasets.json` catalogues;
- the generic `wmts-kvp-webmercator` path supports WorldCover and USGS imagery,
  four concurrent downloads, strict JPEG/PNG validation and an atomic,
  readable tile cache under `cache/imagery/`;
- a 33 x 33 geographic control lattice keeps route coordinate conversion on
  the UI thread while the worker downloads and resamples the imagery;
- `Load Imagery` filters sources by location and detailed/distant approval,
  previews the result and applies it only to the existing in-memory overlay;
- Poland and WorldCover sample tiles were rechecked with bounded live requests;
- fast cached and high-resolution contiguous-US USGS/USDA sources were added
  after live capability, zoom-limit, tile-format and bounded export checks;
- Czech CUZK Ortofoto uses one ArcGIS export, while Netherlands PDOK current
  25 cm imagery validates four-request WMS mosaics for size-limited services;
- Portugal uses DGT's 2025 WMS, and Switzerland plus Liechtenstein use the
  generic STAC/JPEG-COG range provider with selectable source resolution;
- the focused geo suite covers catalogue merging, KVP requests, Web Mercator
  addressing, zoom choice, cache paths and offline cached composition.

The existing Load Map workflow, optional overlay PNG save and final terrain
texture generation remain separate.

## Scope boundaries

- Add a separate **Load Imagery** button, tool mode and window.
- Leave the existing **Load Map** window and its OSM/static-image choices
  operational and behaviorally unchanged.
- Reuse the existing in-memory tile overlay as the first output sink. A small
  ownership-safe setter may be added to `MapWindow`; the new acquisition code
  must not be added to `MapWindow` or `MapDataUrlImage`.
- Do not remove `core.maps.imageryUrl`, `MapDataUrlImage`, or the raster entries
  from Load Map in this milestone.
- Do not generate final ACE terrain textures automatically. The existing
  "Make Tile Texture from Map" action remains a separate user action.
- Do not add static-map URL templates, authentication, cloud-scene selection
  or automatic imagery during tile generation yet. The later SWISSIMAGE step
  added only the narrow RGB/JPEG STAC/COG profile described below.

## Existing implementation relevant to the task

### Overlay sink

`MapWindow::mapTileImages` stores one `QImage*` under
`tileX * 10000 + tileZ`. `Terrain` and `MapLib` already render that image, and
`MapWindow::saveToDisk()` writes it under the route's `TERRAIN_MAPS` directory.
The new window can therefore produce useful output without changing terrain
rendering or texture generation.

The raw public pointer map currently spreads replacement/deletion logic across
callers. Prefer a small `MapWindow::setTileImage(x, z, QImage)` helper that takes
or copies a complete image, replaces the previous image safely and becomes the
handoff used by both windows. Do not otherwise reorganize Load Map now.

### Old static imagery path

`MapDataUrlImage` downloads many centre-based static-map requests from
`core.maps.imageryUrl`, estimates each response's geographic extent and samples
them into the target image. It is coupled to MapWindow, uses retry timers and
does not have a source catalogue or persistent source-tile cache. It should not
be the base class for the new provider.

Its useful compatibility requirements are:

- output size currently comes from `core.maps.imageResolution` (default 4096);
- the result can be RGB or RGBA according to the overlay alpha;
- tile coordinates and Z signs must match the existing `MapWindow` hash;
- existing saved overlay PNGs and "Make Tile Texture from Map" must continue to
  work.

### Height-window lessons to reuse

- validated built-in JSON catalogue and dynamic source setting;
- optional user additions/overrides under stable `assets/geo/` storage;
- geographic availability filtering with a buffer;
- source information, attribution, licence and download report;
- worker-thread acquisition, bounded four-request waves, progress and cancel;
- Preview prepares data; Apply is the only action that replaces editor state;
- explicit detailed/distant eligibility, default false.

Do not reuse elevation raster or provider classes for RGB imagery. Coordinate
conversion, bounded HTTP transport and catalogue conventions may be shared when
their APIs remain neutral and simple.

## Live service validation, 2026-09-24

Only capabilities documents, bounded render requests and individual tiles were
used. No bulk imagery was downloaded.

### Poland

Official capabilities:

```text
https://mapy.geoportal.gov.pl/wss/service/PZGIK/ORTO/WMTS/StandardResolution?SERVICE=WMTS&REQUEST=GetCapabilities
```

Verified configuration:

| Property | Value |
|---|---|
| Layer | `ORTOFOTOMAPA` |
| Style | `default` |
| Format | `image/jpeg` |
| Matrix set | `EPSG:3857` |
| Matrix IDs | `EPSG:3857:{zoom}` |
| Tile size | 256 x 256 |
| Levels | 0 through 19 |
| Origin | standard Web Mercator `(-20037508.342787, 20037508.342787)` |

A Warsaw level-18 KVP request for tile `146372,86317` returned HTTP 200,
`image/jpeg`, a valid 256 x 256 image and 8,269 bytes in about 0.41 seconds.

The official WMS also returned a valid 2048 x 2048 JPEG for a bounded Warsaw
request. Follow-up measurements returned 2048 x 2048 images in 3.2--4.2 seconds
and a 4096 x 4096 image in about 9.7 seconds. Repeating an identical 1024 x
1024 request succeeded seven times and once returned an empty HTTP 404, so the
failure is intermittent rather than a deterministic image-size limit.

The capabilities also expose 512 x 512 tiles in the EPSG:2180 and EPSG:4326
matrix sets. The implemented EPSG:3857 matrix is fixed at 256 x 256. The
projected 512-pixel matrix would substantially reduce calls, but a terrain tile
would still require dozens of them.

Geoportal also exposes an official EPSG:2180 ArcGIS MapServer with 512-pixel
cached tiles and an `export` operation. Every tested export succeeded: roughly
1.0--1.5 seconds near 512/1024 pixels, 3.8--4.8 seconds at 2048 and 15--16
seconds at 4096. It also accepted odd dimensions rejected by the WMS front end.
The Polish built-in therefore uses the generic projected MapServer export
provider and requests one image for the complete terrain bounds.

Original orthophoto sheets are separately discoverable through the official
WFS index and its download-link attribute. Sample sheets are tens of megabytes
and the download server does not provide useful COG-style byte ranges, making
them better suited to a future offline/import cache than an interactive single
terrain preview.

### WorldCover

Official capabilities:

```text
https://wmts.terrascope.be/?service=WMTS&request=GetCapabilities
```

Verified configuration:

| Property | Value |
|---|---|
| Layer | `esa-worldcover-s2rgbnir-10m-2021-v2_tcc` |
| Style | `default` |
| Format | `image/png` |
| Matrix set | `EPSG:3857` |
| Matrix IDs | `{zoom}` |
| Dimension | `TIME=2021-01-01` |
| Tile size | 256 x 256 |
| Advertised levels | 0 through 24 |
| Useful native ground resolution | approximately 10 m |

A Warsaw level-14 KVP request for tile `9148,5394` returned HTTP 200,
`image/png`, a valid 256 x 256 image and 65,391 bytes in about 1.39 seconds.
Production zoom selection must cap acquisition at the product's approximately
10 m native resolution instead of following the advertised rendered pyramid to
level 24.

During a distant-tile test two otherwise valid level-13 requests returned HTTP
500. Repeating the exact URLs returned valid PNGs on all six probes. The WMTS
transport now retries only failed images up to three bounded attempts, with a
short backoff. Successful images from the same batch are retained and cached.

The WMTS layer is already a display-ready true-colour rendering. Direct
WorldCover COG support remains useful later, but would require UInt16 RGBNIR
band selection, stretch/gamma validation and multiband image-TIFF support.

Official WorldCover attribution for the selected product should be exposed in
the window and cached metadata:

```text
Copyright ESA WorldCover project 2021 / Contains modified Copernicus Sentinel
data (2021) processed by ESA WorldCover consortium
```

Recheck the final exact attribution and service terms immediately before
shipping the catalogue entry.

### United States

The built-in contiguous-US source uses the official cached USGS Imagery Only
service:

```text
https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/WMTS
```

Verified configuration:

| Property | Value |
|---|---|
| Layer | `USGSImageryOnly` |
| Style | `default` |
| Requested format | `image/jpeg` (accepted by the live KVP endpoint) |
| Matrix set | `GoogleMapsCompatible` |
| Matrix IDs | `{zoom}` |
| Tile size | 256 x 256 |
| Usable levels | 0 through 16; finer requests return HTTP 400 |
| Source mosaic | primarily 0.6 m USDA NAIP in the contiguous USA |
| Effective cache resolution | about 1.5--2.2 m/px across the configured bounds |

A Denver level-16 request returned a valid 256 x 256 JPEG. The MapServer export
operation also returned valid 1024 and 2048 images in about 1.8 and 3.4 seconds,
respectively. TSRE uses the WMTS path because its source tiles persist and are
reused across neighbouring terrain tiles.

The second source uses the official dynamic NAIP Plus ImageServer:

```text
https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPPlus/ImageServer
```

It validates the generic `arcgis-imageserver-export` imagery provider. The
service advertises 0.3 m dataset pixels, natural-colour rendering and a maximum
4000 x 4000 export. A correctly bounded Denver terrain-sized test returned:

| Request | Approximate terrain grid | Time | JPEG size |
|---|---:|---:|---:|
| 2048 x 2048 | 1.0 m | 7.1 s | 870 KB |
| 4000 x 4000 | 0.51 m | 20.2 s | 2.50 MB |

Both images contained real detailed imagery. The selectable 4000, 2048 and
1024 modes therefore complement rather than replace the faster reusable WMTS
cache.

The entry is detailed-terrain only and uses 30-day cache expiry because this is
a rolling mosaic. Its bounds deliberately cover the contiguous United States:
the same service contains other regional and licensed imagery whose coverage
and terms should be represented by separate entries after validation.

### Czechia

The built-in Czech source uses the export operation of the official cached
Ortofoto CR service:

```text
https://ags.cuzk.gov.cz/arcgis1/rest/services/ORTOFOTO_WM/MapServer/export
```

The initial WMTS entry needed roughly 400--500 individual 256-pixel requests
for one detailed terrain tile. The MapServer export produces the same cached
imagery as bounded images. The UI exposes 1024, 2048 and 4096 total sizes,
corresponding to approximately 2, 1 and 0.5 m terrain grids. At 4096, four
parallel 2048 blocks took 5.1 seconds versus 7.8 seconds for one export. The
source is detailed-terrain only, CC BY 4.0, and uses a 30-day rolling cache.

### Slovakia

The Slovak source uses the export operation of the official cached national
Ortofoto service:

```text
https://zbgis.skgeodesy.sk/zbgis/rest/services/Ortofoto/MapServer/export
```

The current third-cycle mosaic has 15 cm source pixels, acquired in 2023 in
western, 2024 in central and 2025 in eastern Slovakia. Its ArcGIS service
supports 4096-pixel Web Mercator exports. At a Bratislava test area, one 4096
export took 11.0 seconds and four concurrent 2048 blocks took 3.9 seconds; the
official WMS took 8.3 and 8.6 seconds respectively. TSRE therefore uses the
ArcGIS path with selectable 4096, 2048 and 1024 total sizes and 2048-pixel
blocks. The service declares CC BY 4.0 and identifies GKU and NLC as the data
providers. It is detailed-terrain only and uses a 30-day rolling cache.

### Netherlands

The built-in Dutch source uses the WMS form of PDOK's current complete 25 cm
RGB layer:

```text
https://service.pdok.nl/hwh/luchtfotorgb/wms/v1_0
```

The `Actueel_ortho25` layer always follows the latest complete nationwide year.
PDOK limits each WMS response to 2500 pixels per side. TSRE therefore makes one
request for the 1024 and 2048 modes, and splits 4096 into four balanced 2048
blocks downloaded in parallel. A bounded Utrecht test took about 1.5 seconds
at 1024, 3.3 seconds at 2048, and 2.8 seconds for all four parallel blocks at
4096. This replaces roughly 500 individual WMTS calls at maximum output detail.

PDOK also provides current 8 cm, and in places 5 cm, winter imagery. It is not
a separate built-in source yet because the current 4096-pixel terrain overlay
is approximately 0.5 m/pixel and cannot retain that additional detail. The
25 cm late-spring/summer product is already finer than the output and provides
more suitable vegetation for route textures. The rolling CC BY 4.0 source is
detailed-terrain only and uses a 30-day cache expiry.

### France

The French source uses the official Geoplateforme WMS-Raster layer for the
20 cm BD ORTHO product:

```text
https://data.geopf.fr/wms-r
layer: HR.ORTHOIMAGERY.ORTHOPHOTOS
```

TSRE requests EPSG:2154 Lambert-93, which is already supported by
`CrsTransform`, and avoids server-side Web Mercator reprojection. At a Paris
test area, native-projection requests took approximately 2.1 seconds at 1024,
7.5 seconds at 2048, and 47.8 seconds at 4096. Four parallel 2048 requests were
not an improvement: two were queued for 42--59 seconds. This source therefore
does not enable `requestBlockPixels`; it defaults to one 2048 request while
keeping 4096 available as an explicit maximum-quality choice.

The source covers metropolitan France, is detailed-terrain only, uses the
Licence Ouverte 2.0, and has a 30-day rolling cache.

### Belgium: Flanders

The Flanders entry uses the official Digitaal Vlaanderen WMS for the latest
completed winter orthophoto mosaic. The source mosaic is 15 cm and the WMS
limits each response to 2048 pixels. In an Antwerp-area test, one 1024 request
took 1.1 seconds, one 2048 request took 2.3 seconds, and four concurrent 2048
requests produced the 4096 result in 2.4 seconds. TSRE therefore exposes 4096,
2048 and 1024 and defaults to the four-block 4096 mode. The rolling source uses
a 30-day cache expiry and is detailed-terrain only.

### Belgium: Wallonia

The Wallonia entry uses SPW's complete spring 2025 orthophoto WMS in native
EPSG:3812. The source resolution is 25 cm. ArcGIS export tests took 1.5, 3.8
and 21.0 seconds at 1024, 2048 and 4096 respectively; four concurrent 2048
exports still took 11.9 seconds. The alternate WMS returned one 4096 image in
8.1 seconds and outperformed its own parallel requests, so TSRE uses a single
WMS request by default. The dated CC BY 4.0 edition is cached without expiry.

### Spain

The Spain entry uses IGN's official PNOA maximum-current WMS layer. Source
resolution is normally 25 or 50 cm depending on the area. In a Madrid test,
one 1024 request took 2.4 seconds, one 2048 request 5.5 seconds and one 4096
request 14.0 seconds. Four concurrent 2048 requests completed in 4.5 seconds,
so the 4096 default uses that block layout. The 2048 and 1024 alternatives
remain available. The CC BY 4.0 rolling source uses a 30-day cache expiry.

### Lithuania

The Lithuania entry uses the official cached ORT10LT ArcGIS MapServer in Web
Mercator. It currently combines 2022--2025 acquisitions. In a Vilnius test,
one 1024 export took 1.7 seconds, one 2048 export 5.2 seconds and one 4096
export 10.8 seconds. Four concurrent 2048 exports completed in 3.5 seconds,
so TSRE defaults to that 4096 arrangement and also offers 2048 and 1024. The
rolling source is detailed-terrain only and uses a 30-day cache expiry.

### Estonia

The Estonia entry uses the official latest `EESTIFOTO` WMS layer in EPSG:3301
(L-EST97). This required adding the published L-EST97 Lambert Conformal Conic
definition to `CrsTransform`; WMS 1.3 declares its bounding-box axes as
northing/easting, represented by `bboxAxisOrder: yx`. One 4096 request took
16.5 seconds near Tallinn, while four concurrent 2048 requests took 4.9
seconds. TSRE therefore uses the four-block 4096 default and offers 2048 and
1024 alternatives. The latest mosaic rolls forward and uses 30-day expiry.

### Croatia

The Croatia entry uses DGU's anonymous WMS for the complete 2023/2024 DOF5
cycle. The newer 2025/2026 endpoint currently covers only its first half-cycle,
so using it alone would leave large national gaps. EPSG:3765 HTRS96/Croatia TM
was added to `CrsTransform` from its published GRS80 Transverse Mercator
definition. At Zagreb, one 4096 request took 12.2 seconds and four concurrent
2048 requests took 4.5 seconds. The dated complete edition is cached without
expiry and includes selectable 4096, 2048 and 1024 modes.

### Slovenia

The Slovenia entry uses GURS's public INSPIRE WMS orthophoto coverage in its
native EPSG:3794 D96/TM grid, which `CrsTransform` already supported. The layer
serves the detailed DOF025 product at terrain scales. A Ljubljana-area 4096
request took 25.5 seconds, while four concurrent 2048 blocks took 6.5 seconds.
TSRE uses the parallel 4096 default and retains 2048 and 1024 alternatives.

### Luxembourg

The Luxembourg entry uses the official country-wide summer 2025 orthophoto
layer through the public map WMS in EPSG:3857. The source has at most 10 cm
ground pixels. One 4096 request took 8.3 seconds; four concurrent 2048 blocks
took 2.6 seconds. TSRE uses that parallel 4096 default and also offers 2048 and
1024. The dated edition is cached without expiry.

### Portugal

The Portugal entry uses DGT's official `Ortos2025-RGB` WMS layer in native
EPSG:3763 PT-TM06/ETRS89, already supported by `CrsTransform`. DGT's newer
catalogue confirms that the 25 cm 2025 edition covers mainland Portugal and is
distributed under CC BY 4.0. A Lisbon-area request took about 1.2 seconds at
1024, 1.4 seconds at 2048 and 3.0 seconds at 4096. Since one 4096 response is
already fast and minimizes server request count, this entry exposes only that
approximately 0.5 m terrain-image mode. The dated source is cached without
expiry.

### Switzerland and Liechtenstein

The SWISSIMAGE entry validates the generic `stac-cog-image` provider. TSRE
queries swisstopo's official STAC collection, keeps the latest item for each
exact 1 km footprint, and chooses the coarsest published asset that still
meets the selected terrain-image quality. It reads only the required internal
JPEG COG tiles through strict HTTP ranges. Nearby byte ranges are coalesced up
to 8 MiB, retaining four concurrent connections without issuing hundreds of
tiny requests. Compressed JPEG parts are cached by source filename, published
revision, overview level, row and column.

SWISSIMAGE provides 0.1/0.25 m RGB COGs with internal overviews and separate
2 m COGs in EPSG:2056. For a 2 km Bern-area tile, TSRE selected 0.4 m for the
4096 mode, 0.8 m for 2048, and the 2 m asset for 1024. The first 4096 load
transferred 13.2 MB in nine coalesced range requests and took about 10 seconds;
the cached repeat performed no downloads and took about 1.2 seconds. A separate
Liechtenstein probe confirmed that it is covered by the same collection.

## First milestone design

### 1. Catalogue

Add a built-in Qt resource such as
`src/tsre/geo/imagery-datasets.json` and an optional persistent user catalogue
at `assets/geo/imagery-datasets.json`. Apply the same safe merge rules as the
elevation catalogue: built-ins first, valid same-ID user entries replace in
place, new IDs append, and an invalid user object cannot remove a built-in.

Use deliberately narrow provider names. The implemented catalogue supports
`wmts-kvp-webmercator`, projected `wms-kvp`, `arcgis-mapserver-export`,
`arcgis-imageserver-export`, and `stac-cog-image`; it does not claim arbitrary
capability discovery.
The Polish entry uses:

```json
{
  "version": 1,
  "defaultDetailedSource": "world.esa.worldcover-s2-2021",
  "defaultDistantSource": "world.esa.worldcover-s2-2021",
  "datasets": [
    {
      "id": "pl.gugik.orto.standard",
      "name": "Poland - Geoportal standard orthophoto",
      "provider": "arcgis-mapserver-export",
      "endpoint": "https://mapy.geoportal.gov.pl/wss/service/PZGIK/ORTO/REST/StandardResolution/export",
      "layer": "3",
      "style": "",
      "format": "image/jpeg",
      "crs": 2180,
      "maxRequestPixels": 4096,
      "requestSizes": [4096, 2048, 1024],
      "defaultRequestSize": 4096,
      "boundsWgs84": [14.0, 48.8, 24.3, 55.1],
      "detailedTerrainApproved": true,
      "distantTerrainApproved": false,
      "directory": "pl_gugik_orto_standard"
    }
  ]
}
```

Additional validated fields should cover static dimensions such as WorldCover
`TIME`, immutable product revision, native ground resolution, attribution,
licence, information URL and cache age. Dataset approval flags default to
false. In milestone one:

- Poland: detailed approved, distant disabled;
- WorldCover: detailed and distant approved;
- WorldCover is the safe default in both domains because it is global;
- the Height/elevation settings remain entirely separate.

Register `geo.imagery.source` as a dynamic reference-valued setting. A separate
distant setting is unnecessary initially: opening a distant tile filters the
list and selects the distant default without overwriting the saved detailed
choice, following the Height window behavior.

Projected-image sources may define a finite `requestSizes` array and a member
`defaultRequestSize`. Load Imagery places a compact **Res.** combo beside the
expanding source combo. The value controls the longest side of the acquired
source image while preserving its projected aspect ratio; it does not change
the `core.maps.imageResolution` overlay dimensions. Poland offers 4096, 2048
and 1024 and defaults to 4096. Sources without configured sizes show disabled
automatic selection.

`requestBlockPixels` is an optional per-source performance setting. If the
selected total image is larger, TSRE divides it into balanced blocks, downloads
up to four blocks concurrently and joins them before terrain reprojection. It
must not exceed the service's `maxRequestPixels`. Poland, Czechia, Netherlands
and USGS NAIP Plus use 2048-pixel blocks: their highest quality mode takes four
parallel requests, while the 2048 and 1024 choices remain single requests.

### 2. Generic Web Mercator WMTS provider

Build standard WMTS 1.0.0 KVP `GetTile` URLs entirely from validated dataset
configuration. Support HTTPS, JPEG and PNG only. Validate decoded images as
exactly the configured tile dimensions; XML, JSON, HTML, empty and oversized
responses are failures even when the HTTP status is 200.

Choose zoom using ground resolution at the terrain centre:

```text
target ground metres/pixel = terrain physical size / output image pixels
Web Mercator ground metres/pixel =
    156543.03392804097 * cos(latitude) / 2^zoom
```

Select the closest useful level, bounded by configured min/max zoom and by the
dataset's native ground resolution. Do not fetch a finer rendered level when it
only oversamples WorldCover's 10 m source.

Download at most four missing tiles concurrently. Preflight the tile count and
decoded memory before HTTP. Cancellation must abort every outstanding reply.
Retry only failed requests, at most three attempts with short backoff, because
both tested services occasionally return transient HTTP 5xx or empty 404
responses.

### 3. Generic projected WMS provider

Build a WMS 1.3 `GetMap` request from catalogue fields, transform the control
lattice through `CrsTransform`, request its projected bounding rectangle and
resample that north-up image into the route tile. `bboxAxisOrder` is explicit
because WMS 1.3 EPSG:2180 uses northing/easting (`yx`) in `BBOX`. Bound each
server response by `maxRequestPixels`. The same configurable block acquisition
is shared by WMS and ArcGIS MapServer/ImageServer exports.

Cache each response block by source revision and a digest of its canonical
request URL. Apply the same expiry, atomic-write, decode-size, memory,
cancellation and bounded-retry rules as WMTS.

The same projected-image path supports ArcGIS MapServer `export`. It uses
ordinary easting/northing bounds plus `bboxSR`, `imageSR`, `size`, output format
and an optional visible layer from configuration. Poland uses this path because
its live endpoint was consistently more reliable than WMS.

Live comparisons for the highest quality setting showed similar total response
bytes and these elapsed times:

| Source | One large export | Four parallel blocks |
|---|---:|---:|
| Poland Geoportal | 23.2 s at 4096 | 8.0 s at 4 x 2048 |
| Czechia CUZK | 7.8 s at 4096 | 5.1 s at 4 x 2048 |
| USGS NAIP Plus | 18.5 s at 4000 | 7.3 s at 4 x 2000 |

The trade-off is four service requests instead of one. A failed block fails the
whole preview and may consume more request quota, so blocking remains an
explicit dataset choice rather than a global rule.

### 4. Persistent cache

Use readable per-source paths under the configured geodata directory, for
example:

```text
cache/imagery/<directory>/<revision>/<matrix-set>/<zoom>/<column>/<row>.jpg
```

Cache identity must include source ID/directory, immutable product revision or
time dimension, matrix set, zoom, row, column and image format. WorldCover 2021
is immutable. Poland is a rolling current mosaic, so its catalogue entry needs
an explicit refresh policy or user-visible cache-clear action; do not pretend a
permanent cached tile remains current forever.

Write files atomically. Decode cached files under the same validation rules as
downloads. A corrupt cached tile should be removed and fetched once, then
reported if the replacement is invalid.

### 5. Target image and reprojection

Do not assume a TSRE terrain tile is axis-aligned in Web Mercator. The provider
must create a north-up source-tile view and resample it into the route tile's
actual square orientation.

Avoid creating 16 million geographic coordinate objects for a 4096 image on the
UI thread. A practical first approach is:

1. compute a modest geographic control lattice over the route tile using
   `Game::GeoCoordConverter` on the UI thread;
2. copy the immutable lattice into the worker;
3. interpolate within each small control cell while mapping output pixels to
   Web Mercator global-pixel coordinates;
4. bilinearly sample cached source tiles, including neighbours at boundaries.

Validate the mesh approximation against exact converter samples for detailed
and 32 km distant tiles, including rotated routes and adjacent seams. Increase
mesh density only when measured error requires it.

Use `core.maps.imageResolution` for the first output image. Report output
metres/pixel, selected WMTS level, effective source ground resolution, cache
hits, downloads, bytes and missing/transparent pixels.

### 6. Load Imagery window

Add a new checkable **Load Imagery** tool beside Load Map. The window should use
the established TSRE combo style and the Height window interaction model:

- source row;
- source metadata/attribution/licence/information panel;
- output size and selected/effective resolution summary;
- Load Preview, Cancel, Apply and Close;
- modal progress for a user-requested preview, with work performed off the UI
  thread;
- Apply enabled only after a complete successful preview;
- reopening or changing source invalidates the prepared result.

The source list is filtered by geographic bounds and terrain domain. Detailed
tiles show Poland when in range plus WorldCover. Distant tiles show only sources
with `distantTerrainApproved`; initially that is WorldCover.

Apply replaces only the selected tile's existing in-memory overlay image. It
must not automatically save a route PNG or generate an ACE texture in the first
milestone. Existing Show/Hide Map, Save to disk and Make Tile Texture from Map
remain usable.

No imagery fallback/compositing row is planned for milestone one. Missing
required tiles or decode failures keep Apply disabled and produce a visible
report. Alpha/NoData composition and a selectable fallback can be designed once
real service-edge behavior is known.

## Validation plan

### Standalone provider tests

- strict catalogue parsing, invalid-object isolation and user merge behavior;
- KVP query generation for both matrix-ID conventions and the WorldCover TIME
  dimension;
- Web Mercator tile addressing in every hemisphere and near the latitude limit;
- latitude-aware zoom choice and WorldCover native-resolution cap;
- JPEG/PNG validation and rejection of XML/JSON/HTML/error images;
- cache identity, atomic writes, corrupt-cache replacement and rolling/immutable
  revision behavior;
- four-request scheduling, cancellation, size/tile-count guards;
- bilinear sampling across source tile edges;
- route-control-mesh error against exact converter samples.

Use a local HTTP fixture server for deterministic network tests. Live endpoint
tests remain opt-in and bounded.

### Application tests

- detailed Poland tile: preview, Apply, Show/Hide Map and Make Tile Texture;
- detailed tile outside Poland: Poland hidden, WorldCover available;
- distant tile: only WorldCover visible;
- reopening without Apply preserves the previous overlay;
- cancellation leaves the previous overlay intact;
- applying a replacement does not leak or leave a stale texture;
- neighbouring tiles have no one-pixel seam;
- cached repeat performs zero downloads;
- existing Load Map OSM and static-image choices still work unchanged.

## Later milestones

1. **Generic static-map URL source.** Move the capability represented by
   `core.maps.imageryUrl` into an imagery catalogue provider with secret
   references. After migration and compatibility testing, remove the image
   choices from Load Map and focus that window on OSM.
2. **Additional European services.** France, Lithuania, Flanders and Spain are
   the strongest next public national candidates.
3. **Poland high-resolution mode.** Revisit after measuring its coverage and
   practical request limits.
4. **Additional image COG profiles.** The current provider supports tiled RGB
   8-bit JPEG COGs with internal overviews. Add other band layouts, data types,
   compressions and masks only when a useful source requires them.
5. **Fallback and NoData composition.** Define transparent/missing coverage,
   source priority, acquisition-period consistency and attribution for mixed
   imagery.
6. **Automatic tile-generation integration.** Add only after manual preview,
   cancellation, cache limits and source-domain policy are proven.

## Current decisions

Recommended for milestone one:

- generic Web Mercator WMTS KVP, projected WMS and ArcGIS MapServer export
  providers;
- Poland and Czechia through one MapServer export image, Netherlands through
  one or four WMS blocks, and WorldCover through reusable WMTS tiles;
- one selected imagery source, without fallback;
- Preview then Apply to the in-memory overlay only;
- 4096 output through the existing setting, while source zoom follows useful
  ground resolution rather than output pixel count alone;
- named persistent source-tile cache;
- current Load Map code left intact except for an optional safe overlay setter.
- Apply does not write the route's `TERRAIN_MAPS` PNG automatically; the
  existing optional save action remains available.
