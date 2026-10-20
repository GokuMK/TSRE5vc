# Imagery data: aerial photographs, satellite images and custom sources

Imagery supplies the **colour image** used as a terrain overlay or map
background. It does not change the terrain's height. For numerical ground data,
use the separate [elevation guide](elevation-data.md).

This guide covers the source catalogue, the most useful parameters, and complete
examples for WMS, WMTS, STAC/COG and a static-map URL, including Google Static
Maps. Public-service examples follow the built-in catalogue; providers can
change their layers, URLs and access requirements.

## 1. Understand what you are downloading

| Term | Meaning |
| --- | --- |
| Orthophoto | An aerial image corrected to line up with map coordinates. Usually a good source for detailed route scenery. |
| Satellite mosaic | Images combined across an area. Coverage and detail vary by product. |
| Ground resolution | Approximate metres represented by one source pixel. A 0.25 m image contains finer detail than a 10 m image. |
| WMS | Requests a rendered image for a geographic rectangle. TSRE supplies the bounds, CRS and image dimensions. |
| WMTS | Requests pre-rendered tiles at defined zoom levels, rows and columns. TSRE's provider supports the standard Web Mercator grid through KVP requests. |
| Static-map URL | Requests an image centred on latitude/longitude at a zoom level and size. It must use the Web Mercator geometry expected by TSRE. |
| COG | A georeferenced TIFF whose internal blocks can be read over HTTP without fetching the whole file. |
| STAC | A catalogue used to discover image assets for an area. The actual pixels come from those files. |
| CRS / EPSG code | Coordinate system used to place an image. TSRE converts between the route and supported source coordinates. |

A website's map-viewer URL is usually not its imagery service endpoint. Find
the provider's WMS/WMTS capabilities, STAC API or documented static-image API.

**WCS and WMS have different roles here.** Use WCS for numerical
[elevation](elevation-data.md#5-example-wcs-elevation). The imagery catalogue
has no WCS provider; it expects supported rendered images or image COGs.

## 2. Load imagery in the editor

1. Open the route and use **Load Imagery** on the terrain tile.
2. Select a source available at that location.
3. Choose the source resolution option if the source provides one.
4. Click **Load preview** and inspect the image and download report.
5. Click **Apply** to put the image into the terrain overlay.

Applying imagery does not automatically generate final terrain textures. Use
the existing **Make Tile Texture from Map** action separately when appropriate
for your source. The map mode's imagery background is another use of compatible
tiled sources; adding a catalogue entry does not make every provider a
continuously streaming map background.

The route's geographic placement must be correct. If all sources line up with
each other but appear in the wrong place in the route, check the route's
georeferencing before changing the catalogue.

### Resolution and request sizes

These settings solve different problems:

| Setting | What it controls |
| --- | --- |
| `nativeResolution` | Declared useful source detail, in metres per pixel. It helps tiled sources avoid requesting unnecessarily high zoom levels. |
| `requestSizes` / `defaultRequestSize` | Options for the source sampling resolution across the terrain tile. These are not necessarily the dimensions of one HTTP response. |
| `tilePixels`, or `maxRequestPixels` / `requestBlockPixels` | Size of an individual source tile/static image, or limits used to split rendered-image requests. |
| **Generated map resolution** (`core.maps.imageResolution`) | Size of the final composed overlay. Increasing it cannot create detail absent from the source. |

For a 2048 m terrain tile, 4096 samples across imply about 0.5 m per sample;
2048 imply 1 m, and 1024 imply 2 m. The actual source resolution can differ
because of zoom levels, available assets and reprojection.

A 4096 source option can use many 640-pixel static-map requests, or four
2048-pixel WMS requests. It is not a request for a single 4096-pixel image from
every server.

## 3. Create the user catalogue

Use this exact file in your TSRE working directory, normally beside the EXE:

```text
TSRE/
  TSRE5vc.exe
  assets/
    geo/
      imagery-datasets.json
```

There is one user imagery catalogue. TSRE does not scan arbitrary JSON filenames
under `assets/geo`. This location is independent of the selected route and
`appdata/0.7`. It is relative to the TSRE working directory, not your drive root.

The file has this container:

```json
{
  "version": 1,
  "datasets": []
}
```

Add complete dataset objects to the array. Each example below is a complete file;
combine examples by copying their objects into a single `datasets` array,
separated by commas. Do not paste several outer JSON documents into one file.

Use UTF-8 plain text, double quotes, decimal points and lowercase booleans.
Comments and trailing commas are invalid. Check that Windows has not added
`.txt` to the filename.

Built-ins load first. New IDs add sources; matching IDs replace the **entire**
built-in definition. An invalid override is skipped and the built-in remains.
Give experiments a new `user.*` ID so they can be compared with the original.
Restart TSRE after edits to refresh the source lists reliably.

### Where images are cached

Settings → **Geodata and elevation cache directory** (`core.paths.geoData`)
controls the geodata root. Imagery caches live under:

```text
<geodata root>/cache/imagery/<directory>/...
```

For example, `"directory": "my_ortho"` with geodata root `D:/TSRE-Geodata`
uses `D:/TSRE-Geodata/cache/imagery/my_ortho/`. This has no effect on the
catalogue's `assets/geo/imagery-datasets.json` location.

## 4. Shared parameters

| Parameter | Meaning |
| --- | --- |
| `id` | Unique stable name using letters, digits, `.`, `_` or `-`, such as `user.myarea.ortho`. |
| `name` | Display name in the source list. |
| `provider` | One of the supported methods in the next table. |
| `format` | `image/jpeg` or `image/png` for rendered/tiled sources; `image/tiff` for imagery COGs. Must match the returned data. |
| `boundsWgs84` | `[west, south, east, north]`, **always longitude/latitude degrees**, even when `crs` is projected. Limits must be inside ±180° longitude and approximately ±85.05112878° latitude. |
| `nativeResolution` | Positive source spacing in metres per pixel. A smaller number does not improve the actual imagery. |
| `detailedTerrainApproved` | Set `true` to offer the source for normal detailed terrain. Defaults to `false`. |
| `distantTerrainApproved` | Defaults to `false`. Enable only after checking resolution, coverage and request costs for distant terrain. |
| `directory` | Cache subdirectory using letters, digits, underscores and hyphens; `/` can separate nested components. Do not use an absolute path or `..`. |
| `revision` | Version label, using letters, digits, `.`, `_` or `-`; defaults to `current`. Used in tiled/rendered and projected-COG cache identities. STAC COG parts use asset URL and publication timestamp. |
| `cacheMaxAgeDays` | Nonnegative expiry for ordinary cached tiles/rendered images. `0` means no age expiry. COG parts follow their asset/revision identities instead. |
| `persistentCache` | Defaults to `true`. For tiled, static-map and WMS/ArcGIS rendered images, `false` prevents persistent source-image caching. It does not disable COG part caches or overlay/export operations. |
| `requestSizes` | Optional distinct integer source sampling sizes, `256`–`8192`. |
| `defaultRequestSize` | Must be a member of `requestSizes` when that list is present. Omit both fields for automatic selection. |
| `attribution`, `license`, `information` | Credits, licence description and user-facing source information. |
| `attributionUrl`, `informationUrl` | Optional HTTPS links. |

The bounding rectangle filters the source list; it does not guarantee imagery
exists everywhere inside the rectangle. Keep the coverage realistic.

| Provider | Additional configuration |
| --- | --- |
| `wms-kvp` | WMS 1.3.0 endpoint, layer, CRS, axis order and request-size limits. |
| `wmts-kvp-webmercator` | WMTS endpoint, layer, matrix set/template and zoom limits on a standard Web Mercator grid. |
| `arcgis-mapserver-export` | ArcGIS MapServer endpoint, CRS and request-size limits. |
| `arcgis-imageserver-export` | ArcGIS ImageServer endpoint, CRS and request-size limits. |
| `stac-cog-image` | STAC API root, collection in `layer`, projected CRS and compatible JPEG-compressed image COGs. |
| `projected-cog-image` | Regular projected image sheets, `download.tileSize`, and an HTTPS `download.urlTemplate` with `{easting}` and `{northing}`. |
| `static-map-url` | HTTPS centre/zoom template, returned image size and zoom limits. |

There is no generic XYZ `{z}/{x}/{y}` URL provider in this catalogue. A tile
server must match the supported WMTS interface, or another implemented provider.
Similarly, adding an unfamiliar EPSG code does not implement a new CRS.

## 5. Example: WMS aerial imagery

This example follows the configured Netherlands PDOK RGB layer:

```json
{
  "version": 1,
  "datasets": [
    {
      "id": "user.nl.pdok.ortho",
      "name": "My Netherlands aerial imagery",
      "provider": "wms-kvp",
      "endpoint": "https://service.pdok.nl/hwh/luchtfotorgb/wms/v1_0",
      "layer": "Actueel_ortho25",
      "style": "default",
      "format": "image/jpeg",
      "version": "1.3.0",
      "bboxAxisOrder": "xy",
      "crs": 3857,
      "maxRequestPixels": 2500,
      "requestBlockPixels": 2048,
      "requestSizes": [4096, 2048, 1024],
      "defaultRequestSize": 2048,
      "nativeResolution": 0.25,
      "boundsWgs84": [3.0, 50.6, 7.3, 53.7],
      "detailedTerrainApproved": true,
      "distantTerrainApproved": false,
      "directory": "user_nl_pdok_ortho",
      "revision": "rolling",
      "cacheMaxAgeDays": 30,
      "attribution": "Samenwerkingsverband Beeldmateriaal / PDOK",
      "license": "CC BY 4.0",
      "informationUrl": "https://www.pdok.nl/"
    }
  ]
}
```

The outer `"version": 1` is the catalogue format. The inner
`"version": "1.3.0"` is the WMS protocol version.

| WMS parameter | How to choose it |
| --- | --- |
| `endpoint` | Base WMS URL. TSRE builds `GetMap`; do not include fixed `BBOX`, `WIDTH` or `HEIGHT`. |
| `layer` | Exact WMS layer name from capabilities, not its descriptive title. |
| `style` | Published style name, or `""` when the service expects its default style that way. |
| `crs` | Supported CRS accepted by the service. This example requests EPSG:3857. |
| `bboxAxisOrder` | `xy` or `yx` for the service's WMS bounding-box order. EPSG:4326 in WMS 1.3.0 often needs `yx`; inspect the provider. This does not change the order of `boundsWgs84`. |
| `maxRequestPixels` | Maximum side length of one request, `256`–`8192`; keep within the server's limit. |
| `requestBlockPixels` | Optional split-block size, at least `256` and no larger than `maxRequestPixels`. Here a 4096 option can be split into 2048-pixel blocks. |
| `dimensions` | Optional object of extra string parameters, for example `{"TIME": "2021-01-01"}`. |

To adapt it, inspect the service's `GetCapabilities`, choose an image layer,
style, format and supported CRS, and copy its extent as WGS84 degrees. Try one
tile at 1024 or 2048 before choosing larger requests.

```text
https://service.pdok.nl/hwh/luchtfotorgb/wms/v1_0?SERVICE=WMS&VERSION=1.3.0&REQUEST=GetCapabilities
```

## 6. Example: WMTS satellite mosaic

This follows the global WorldCover Sentinel-2 RGB source. It is a colour mosaic,
not the categorical WorldCover land-cover classification.

```json
{
  "version": 1,
  "datasets": [
    {
      "id": "user.worldcover.rgb",
      "name": "My WorldCover Sentinel-2 RGB 2021",
      "provider": "wmts-kvp-webmercator",
      "endpoint": "https://wmts.terrascope.be/",
      "layer": "esa-worldcover-s2rgbnir-10m-2021-v2_tcc",
      "style": "default",
      "format": "image/png",
      "tileMatrixSet": "EPSG:3857",
      "tileMatrixTemplate": "{zoom}",
      "dimensions": {
        "TIME": "2021-01-01"
      },
      "tilePixels": 256,
      "minZoom": 0,
      "maxZoom": 24,
      "nativeResolution": 10,
      "boundsWgs84": [-180, -85, 180, 85],
      "detailedTerrainApproved": true,
      "distantTerrainApproved": true,
      "directory": "user_worldcover_rgb",
      "revision": "2021_v2",
      "attribution": "ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021) processed by ESA WorldCover consortium",
      "license": "CC BY 4.0",
      "informationUrl": "https://esa-worldcover.org/en/data-access"
    }
  ]
}
```

`tileMatrixSet` is the exact matrix-set identifier. It may be `EPSG:3857`,
`GoogleMapsCompatible`, or a provider-specific name. That name alone is not
enough: the matrix must have the standard Web Mercator origin, zoom spacing and
tile addressing expected by TSRE.

`tileMatrixTemplate` builds the matrix ID from `{zoom}`. Some services use
`"{zoom}"`, others `"EPSG:3857:{zoom}"`. `tilePixels` must match the returned
square tile and be a power of two between `64` and `1024`. Zoom limits must
satisfy `0 <= minZoom <= maxZoom <= 24`.

Do not assume the highest advertised zoom contains new detail.
`nativeResolution: 10` keeps this source near its useful ground resolution even
though the service offers higher rendered levels.

## 7. Example: STAC and image COGs

This follows Switzerland's SWISSIMAGE source:

```json
{
  "version": 1,
  "datasets": [
    {
      "id": "user.ch.swissimage",
      "name": "My SWISSIMAGE orthophotos",
      "provider": "stac-cog-image",
      "endpoint": "https://data.geo.admin.ch/api/stac/v1/",
      "layer": "ch.swisstopo.swissimage-dop10",
      "format": "image/tiff",
      "crs": 2056,
      "maxRequestPixels": 4096,
      "requestSizes": [4096, 2048, 1024],
      "defaultRequestSize": 2048,
      "nativeResolution": 0.1,
      "boundsWgs84": [5.8, 45.7, 10.7, 47.9],
      "detailedTerrainApproved": true,
      "distantTerrainApproved": false,
      "directory": "user_ch_swissimage",
      "revision": "rolling",
      "attribution": "Federal Office of Topography swisstopo - SWISSIMAGE; lake imagery includes material (2016-2018) Planet",
      "license": "swisstopo terms; source attribution required",
      "attributionUrl": "https://www.swisstopo.admin.ch/en/terms-of-use-free-geodata-and-geoservices"
    }
  ]
}
```

For this provider, **`layer` is the STAC collection ID**. TSRE constructs
`collections/<layer>/items` under `endpoint`, queries the geographic area and
selects suitable assets. The imagery schema is different from elevation STAC:
do not copy elevation's nested `download.collection`, `assetKey` or `range`
fields into this entry.

The current image-COG reader requires:

- A compatible STAC API and assets advertising a cloud-optimized TIFF media
  type, `gsd` or `eo:gsd`, and matching `proj:epsg` metadata.
- HTTPS asset URLs and working HTTP byte-range responses.
- Supported projected georeferencing.
- Tiled, three-channel, 8-bit JPEG-compressed imagery with RGB or YCbCr colour
  interpretation and the required TIFF metadata.

An arbitrary multispectral Sentinel TIFF, a 16-bit reflectance product or a
Deflate RGB TIFF does not become compatible merely by setting
`format: "image/tiff"`. Use a supported rendered WMS/WMTS product for those
datasets, or prepare a compatible image product separately.

TSRE selects an appropriate asset/overview and downloads required blocks.
STAC part caches are separated by asset URL and publication timestamp.
There is no generic single-file image-COG URL provider equivalent to elevation's
`fileGrid: "cog"`; the other imagery COG method is a regular projected sheet
grid. See the Austrian `projected-cog-image` entry in the
[built-in catalogue](../../src/tsre/geo/imagery-datasets.json) for that layout.

## 8. Example: Google Static Maps URL

The `static-map-url` provider replaces the old single imagery-URL setting.
This example requests satellite images from the documented Google Maps Static
API and keeps the API key outside the catalogue.

You need your own Google Cloud project with Maps Static API enabled, billing
enabled and a suitable API key. Requests can incur charges. See Google's
[setup and request documentation](https://developers.google.com/maps/documentation/maps-static/start).
This is a technical configuration example; use it only for workflows allowed
by your account's [Maps Static API policies](https://developers.google.com/maps/documentation/maps-static/policies).
Disabling TSRE's download cache does not by itself permit exporting imagery or
making redistributable terrain textures.

```json
{
  "version": 1,
  "datasets": [
    {
      "id": "user.google.satellite",
      "name": "Google Maps satellite (my API key)",
      "provider": "static-map-url",
      "urlTemplate": "https://maps.googleapis.com/maps/api/staticmap?center={lat},{lon}&zoom={zoom}&size={res}x{res}&maptype=satellite&format=png",
      "format": "image/png",
      "tilePixels": 640,
      "minZoom": 0,
      "maxZoom": 21,
      "nativeResolution": 0.1,
      "requestSizes": [4096, 2048, 1024],
      "defaultRequestSize": 2048,
      "boundsWgs84": [-180, -85, 180, 85],
      "detailedTerrainApproved": true,
      "distantTerrainApproved": false,
      "persistentCache": false,
      "directory": "google_static_satellite",
      "revision": "current",
      "authentication": {
        "type": "query-api-key",
        "secret": "maps.imageryApiKey",
        "parameter": "key"
      },
      "attribution": "Google Maps and its imagery providers; retain the image's provider credits",
      "informationUrl": "https://developers.google.com/maps/documentation/maps-static/start"
    }
  ]
}
```

`nativeResolution: 0.1` is the example's acquisition setting, not a claim of
uniform 10 cm Google coverage. Available imagery and useful zoom vary by place.
Reduce the requested resolution/zoom if the service returns blank or
unnecessarily enlarged images.

### URL placeholders

| Placeholder | Substituted value |
| --- | --- |
| `{lat}` | Centre latitude in degrees. |
| `{lon}` | Centre longitude in degrees. |
| `{zoom}` | Chosen Web Mercator zoom level. |
| `{res}` | Configured `tilePixels`, here `640`. |
| `{width}`, `{height}` | Alternative to `{res}`; both become `tilePixels`. Requests are square. |

The template must include latitude, longitude, zoom, and either `{res}` or both
width/height placeholders. Unknown placeholders are rejected. There is no
`{apikey}` placeholder: authentication adds the query parameter separately.

In the example, a request has a centre such as `52.2297,21.0122`,
`zoom=17`, and `size=640x640`; TSRE appends `key` only when sending it.
`tilePixels` for static maps can be `64`–`1024` and must match the returned image
exactly. Keep Google's default `scale=1`: adding `scale=2` changes the returned
size and breaks this configuration's expected size/geometry.

Unlike the WMTS example, 640 does not need to be a power of two. The 2048/4096
options control coverage sampling across the terrain, while individual Google
requests remain 640 × 640.

### Store the key in your active profile

Close TSRE before manually editing profile files. Find the active profile's
`settings.json` and put the secret in **`secrets.json` beside it**. A portable
profile might use:

```text
profiles/default/settings.json
profiles/default/secrets.json
```

Use the actual active profile, which may have another name or location.
If `secrets.json` already exists, add the key to its existing `secrets` object
and preserve the other entries:

```json
{
  "format": "tsre-secrets",
  "schemaVersion": 1,
  "secrets": {
    "maps.imageryApiKey": "REPLACE_WITH_YOUR_OWN_API_KEY"
  }
}
```

The placeholder above is not a working key. The name `maps.imageryApiKey` must
match `authentication.secret` exactly. `authentication.parameter: "key"` is the
HTTP parameter Google expects; it is not the name of the stored secret.

Restart TSRE, select the source, and preview a small area at the lower source
resolution first. The secret file contains plaintext credentials: keep it local
and leave it out of examples or bug reports.

TSRE currently supports query-key imagery authentication for WMTS and static-map
sources. It does not generate Google's per-request digital signatures or run an
OAuth login flow. Account restrictions, signature requirements, quotas and
regional availability can therefore affect whether a key works with this example.

## 9. Adapting a source and checking the result

1. Pick the implemented provider matching the service.
2. Copy a complete example, choose a new `id` and `directory`, and set a
   descriptive name.
3. Replace the service-specific fields using its documentation/capabilities.
   Keep `boundsWgs84` in longitude/latitude order.
4. Set `detailedTerrainApproved: true`; keep distant-terrain use disabled while
   trying a new detailed source.
5. Restart and preview one tile inside known coverage. Inspect alignment,
   useful detail, download count and any errors.
6. For cache-enabled sources, repeat the preview to check reuse. Then try a
   neighbouring tile and an area near the coverage edge.

Keep the actual source selected in the UI/settings. Optional catalogue root
fields `defaultDetailedSource` and `defaultDistantSource` identify defaults, but
do not replace an existing profile selection. User and built-in default flags
can coexist after merging, so these fields should not be treated as a reliable
way to force a newly appended source to become the selected default.

## 10. Troubleshooting

| Symptom | What to check |
| --- | --- |
| Source not listed | Correct filename/working directory, valid JSON, restart, realistic bounds and `detailedTerrainApproved`. |
| Built-in remains after an override | The replacement may be incomplete or invalid. Overrides replace whole objects. |
| Missing secret | Edit `secrets.json` beside the active profile's `settings.json`; check the reference string, then restart. |
| HTTP 401/403 | API enabled, key permissions/restrictions, authentication method and provider requirements. Keep the actual key out of logs you share. |
| HTTP 429 | Service quota/rate limit. Reduce requested detail and wait before retrying. |
| “Expected … × …” image-size error | `tilePixels`, server image-size limits or a scale parameter does not match the returned image. |
| Blank image | Coverage, layer, zoom availability, time dimension or account-specific content availability. |
| Shifted, rotated or stretched imagery | Route coordinates, WMS axis order, source CRS, or a tile matrix/static-map projection that is not compatible. |
| High zoom but blurry terrain | The source has reached its native detail. Larger output images cannot recover missing detail. |
| Unsupported imagery COG raster profile | Check the image-COG restrictions above; numerical/multispectral TIFFs are different formats. |
| Many downloads on every preview | The source may have `persistentCache: false`, an expired cache, or a different request area/resolution. |
| Old imagery remains | Check the service's layer/date and the cache age/revision. A layer called “current” can still serve an older mosaic. |

For further examples, see the
[built-in imagery catalogue](../../src/tsre/geo/imagery-datasets.json).
For terrain heights, continue with [elevation data](elevation-data.md).
