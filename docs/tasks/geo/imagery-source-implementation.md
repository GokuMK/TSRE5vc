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
- Do not add WMS, static-map URL templates, image COGs, STAC, authentication,
  cloud-scene selection or automatic imagery during tile generation yet.

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

## First milestone design

### 1. Catalogue

Add a built-in Qt resource such as
`src/tsre/geo/imagery-datasets.json` and an optional persistent user catalogue
at `assets/geo/imagery-datasets.json`. Apply the same safe merge rules as the
elevation catalogue: built-ins first, valid same-ID user entries replace in
place, new IDs append, and an invalid user object cannot remove a built-in.

Use deliberately narrow provider names. The implemented catalogue supports
`wmts-kvp-webmercator`, projected `wms-kvp`, `arcgis-mapserver-export`, and
`arcgis-imageserver-export`; it does not claim arbitrary capability discovery.
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
source image by `maxRequestPixels`; the Polish entry uses 4096.

Cache the complete response by source revision and a digest of the canonical
request URL. Apply the same expiry, atomic-write, decode-size, memory,
cancellation and bounded-retry rules as WMTS.

The same projected-image path supports ArcGIS MapServer `export`. It uses
ordinary easting/northing bounds plus `bboxSR`, `imageSR`, `size`, output format
and an optional visible layer from configuration. Poland uses this path because
its live endpoint was consistently more reliable than WMS.

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
2. **Additional WMS validation.** Reuse the implemented projected WMS provider
   for another national service and consider a separate Poland high-resolution
   entry after measuring its coverage and practical request limits.
3. **Additional WMTS/ArcGIS cached countries.** Czechia and Netherlands are the
   strongest generic-design checks after Poland.
4. **Image COG/STAC.** Add multiband UInt8/UInt16 and JPEG-in-TIFF support for
   WorldCover direct COG, SWISSIMAGE and later sources.
5. **Fallback and NoData composition.** Define transparent/missing coverage,
   source priority, acquisition-period consistency and attribution for mixed
   imagery.
6. **Automatic tile-generation integration.** Add only after manual preview,
   cancellation, cache limits and source-domain policy are proven.

## Current decisions

Recommended for milestone one:

- generic Web Mercator WMTS KVP, projected WMS and ArcGIS MapServer export
  providers;
- Poland through one MapServer export image; WorldCover through reusable WMTS
  tiles;
- one selected imagery source, without fallback;
- Preview then Apply to the in-memory overlay only;
- 4096 output through the existing setting, while source zoom follows useful
  ground resolution rather than output pixel count alone;
- named persistent source-tile cache;
- current Load Map code left intact except for an optional safe overlay setter.
- Apply does not write the route's `TERRAIN_MAPS` PNG automatically; the
  existing optional save action remains available.
