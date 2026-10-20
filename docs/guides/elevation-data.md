# Elevation data: loading terrain and adding your own sources

Elevation data supplies the **height of the ground**. TSRE uses it to shape
terrain. Aerial photographs supply colour instead; those belong in the separate
[imagery catalogue](imagery-data.md).

This guide describes the implemented source formats. Examples copied from the
built-in catalogue illustrate known configurations, rather than a promise that
an external service will remain unchanged.

## 1. Start with the basics

A raster is a rectangular grid of pixels. In an elevation raster, each pixel
contains a height rather than a colour.

| Term | Meaning |
| --- | --- |
| DEM | General term for a digital elevation model. Check what the product actually measures. |
| DTM / bare-earth terrain | Ground heights, with buildings and vegetation removed as far as the product allows. Usually the appropriate source for route terrain. |
| DSM / surface model | Heights including objects such as trees and buildings. These can become unwanted bumps in generated terrain. |
| Resolution | Spacing between source samples. A 1 m source has much more detail than a 30 m source, but needs more data. It does not automatically change your terrain tile's vertex spacing. |
| CRS / EPSG code | The coordinate system locating the raster. `2180`, for example, means EPSG:2180. This is separate from the route's own projection. |
| Vertical datum | The reference surface from which heights are measured. Two sources can be correctly positioned horizontally and still disagree in height. |
| NoData | A pixel for which the source has no usable height. Zero is a valid height unless that particular product explicitly uses it as NoData. |
| WCS | A service that returns numerical raster data for a requested area. |
| GeoTIFF / COG | A GeoTIFF carries raster coordinates. A Cloud Optimized GeoTIFF arranges its data so TSRE can request selected blocks rather than the whole file. |
| STAC | A catalogue describing available files and their coverage. TSRE finds an asset in the catalogue, then reads its raster data. |

A coloured relief map, hillshade image or PNG is not a numerical elevation
model. Changing its JSON `format` cannot turn it into one.

## 2. Try a built-in source first

1. Open the route and use **Load Height** on the terrain tile you want to edit.
2. Choose the main source and a fallback source. The list is filtered by the
   tile's location and whether you are editing detailed or distant terrain.
3. Initially leave both height offsets at zero.
4. Click **Load preview**. Inspect the preview and the report, particularly
   primary samples, fallback samples, missing data and downloads.
5. Click **Apply** when the result is suitable, then save your route normally.

The route must have the correct geographic placement/projection. A source JSON
entry cannot correct a route that maps to the wrong place on Earth.

The fallback fills samples the main source cannot supply. TSRE uses the selected
fallback only; it does not silently try a third source. Main and fallback height
offsets are independent. A constant offset may align a small area, but setting
`verticalDatum` in JSON only describes the heights: it does **not** convert them
between vertical datums.

## 3. Where your JSON file goes

Create this file in the TSRE working directory:

```text
TSRE/
  TSRE5vc.exe
  assets/
    geo/
      elevation-datasets.json
```

With a normal release installation, this is the folder containing the EXE.
Custom launch configurations can change the working directory.

Use the exact filename **`elevation-datasets.json`**. There is one user elevation
catalogue containing a `datasets` array; separate files such as `my-dem.json`
are not discovered automatically. This is a local file, not `/assets/geo` at the
root of your drive, and it is not stored under `appdata/0.7`.

TSRE reads its built-in catalogue first, then your file:

- A new `id` adds a source.
- An existing `id` replaces that **whole source definition**, not just the
  properties you specify. Copy the complete entry before overriding it.
- An invalid entry is skipped with a diagnostic. A rejected override leaves
  the built-in entry available.
- Removing your override restores the built-in source on the next catalogue
  load. Restart TSRE after editing to refresh the source lists reliably.

The JSON format is:

```json
{
  "version": 1,
  "datasets": []
}
```

This shows the container; add at least one complete dataset object to `datasets`
for a useful user catalogue. The examples below each show a complete file.
To combine them, put their dataset objects in one array, separated by commas,
with only one outer `version` and `datasets`.

Save as UTF-8 plain text. JSON uses double quotes, decimal points and lowercase
`true`/`false`. It does not allow comments or trailing commas. On Windows, check
that the filename did not become `elevation-datasets.json.txt`.

### Definitions and downloaded files have different locations

In Settings, **Geodata and elevation cache directory**
(`core.paths.geoData`) controls where raster files and caches are stored.
It does not move the JSON catalogue out of `assets/geo`.

For file sources, `directory` is a subdirectory of that geodata root. For
example, with `D:/TSRE-Geodata` selected and `"directory": "my_dtm"`, local
rasters belong in `D:/TSRE-Geodata/my_dtm/`. Service requests use a cache below
the geodata root; COG sources also keep partial-file caches beside their source
file locations. Let TSRE manage downloaded cache files.

## 4. Parameters shared by elevation sources

These are the main fields to understand before adapting an example.

| Parameter | Meaning |
| --- | --- |
| `id` | Stable source identifier. Use a simple unique name such as `user.myarea.dtm1`; avoid spaces, slashes and `..`. |
| `name` | Name shown in the source selector. Include the country/area, product and useful resolution. |
| `provider` | Selects an implemented acquisition method; see the table below. |
| `format` | Decoder format. Service rasters use `image/tiff` or supported `image/x-aaigrid`; file GeoTIFF sources use `geotiff`, HGT uses `hgt`. |
| `crs` | Numeric EPSG code for the source raster. The CRS must be implemented by TSRE. Entering an arbitrary EPSG code does not add projection support. |
| `bounds` | `[minX, minY, maxX, maxY]` in the **source CRS**, normally easting/northing for projected data. For EPSG:4326 these are longitude/latitude degrees. |
| `resolution` | Source/request spacing. For projected examples here, metres per pixel. Geographic single-COG products also need their specific overview configuration; use the matching built-in example. |
| `verticalDatum` | Human-readable height reference. No automatic height conversion is implied. |
| `concurrentRequests` | Requests allowed at once, from `1` to `4`; defaults to `1`. Start conservatively for an unfamiliar server. |
| `zeroIsNoData` | Defaults to `false`. Set `true` only when the product actually uses zero for missing heights. |
| `fallbackApproved` | Defaults to `false`. Allows selection as a fallback. Leave false for a new regional source. |
| `distantTerrainApproved` | Defaults to `false`. Allows use for distant terrain. Large areas need appropriate resolution and practical download testing. |
| `noDataPolicy` | `fallback` by default; `fill` enables supported neighbour-based hole filling before unresolved samples fall back. Start with `fallback`. |
| `attribution`, `license`, `information` | Provider credit, licence description and useful notes shown with the source. |
| `attributionUrl`, `informationUrl`, `downloadPage` | Optional HTTPS links for credits, documentation and manual downloads. |

`fallbackApproved` and `distantTerrainApproved` are eligibility switches, not
proof of data quality. A 1 m national source is not automatically a sensible
choice for a 32 km distant-terrain tile.

| Provider / file grid | Intended input |
| --- | --- |
| `wcs-2.0.1`, `wcs-1.0.0` | Supported WCS raster requests. WCS 1.1.1 is not supported. |
| `arcgis-imageserver` | Numerical elevation from an ArcGIS ImageServer. |
| `wms-1.3.0` | A specifically configured numerical TIFF WMS, not a rendered colour map. |
| `file` + `fileGrid: "cog"` | One known remote COG URL. |
| `file` + `fileGrid: "projected"` | Regular projected COG sheets addressed by easting/northing in a URL template. |
| `file` + `fileGrid: "stac"` | GeoTIFF assets found through a supported STAC API. |
| `file` + `fileGrid: "directory"` | Local supported GeoTIFFs. |
| `file` + `fileGrid: "degree"` | One-degree HGT tiles, optionally downloaded as gzip files. |
| `wfs-file-catalog` | A configured WFS index pointing to elevation sheets; requires additional catalogue-field mappings. |

Use the [built-in elevation catalogue](../../src/tsre/geo/elevation-datasets.json)
as a reference for the less common providers. `coordinateTransform`, for
example, is a specific supported transformation configuration for British
National Grid data, not a general-purpose projection definition.

## 5. Example: WCS elevation

This is a separate user entry based on Poland's configured Geoportal KRON86
source. Its unusual-looking origin and bounds are intentional provider values;
do not replace them with latitude/longitude.

Save as `assets/geo/elevation-datasets.json`:

```json
{
  "version": 1,
  "datasets": [
    {
      "id": "user.pl.geoportal.kron86",
      "name": "My Poland Geoportal DTM 1 m",
      "provider": "wcs-2.0.1",
      "endpoint": "https://mapy.geoportal.gov.pl/wss/service/PZGIK/NMT/GRID1/WCS/DigitalTerrainModelFormatTIFF",
      "coverage": "DTM_PL-KRON86-NH_TIFF",
      "format": "image/tiff",
      "axisX": "x",
      "axisY": "y",
      "crs": 2180,
      "verticalDatum": "PL-KRON86-NH",
      "resolution": 1,
      "origin": [160828.34326572, 796521.669409553],
      "bounds": [160828.34326572, 98928.8977745594, 876029.97009323, 796521.669409553],
      "blockPixels": 1024,
      "concurrentRequests": 2,
      "zeroIsNoData": true,
      "fallbackApproved": false,
      "distantTerrainApproved": false,
      "attribution": "GUGiK / Geoportal.gov.pl"
    }
  ]
}
```

### WCS-specific fields

| Parameter | Meaning |
| --- | --- |
| `endpoint` | Service base URL. TSRE adds the coverage request parameters. Do not paste a complete one-off `GetCoverage` URL here. |
| `coverage` | Exact coverage identifier published by the service, including spelling and case. |
| `axisX`, `axisY` | WCS 2 subset axis labels corresponding to TSRE's X and Y. Read them from the coverage description; they are not necessarily `x` and `y`. |
| `scaleAxisX`, `scaleAxisY` | Optional WCS 2 scaling-axis names if the service uses different names from its subset axes. Default to `axisX`/`axisY`. |
| `origin` | `[X, Y]` reference corner of TSRE's request-block grid: columns grow toward increasing X, rows toward decreasing Y. It is not the route origin. Match the service grid. |
| `blockPixels` | Base request-block size, `16`–`1024` pixels per side. TSRE includes interpolation neighbours around the block, so allow for a small border in server limits. |
| `requestFormat` | Optional exact wire-format string when the service's advertised format differs from the decoder's `format`. |
| `allowExpandedGrid` | Defaults to `false`. Enable only for a verified service that returns a larger enclosing grid. This is not a general fix for incorrect georeferencing. |

At 1 m resolution, a 1024-pixel base block covers about 1024 m per side. Larger
blocks reduce request count but increase the size of each response.

### Adapting this to another WCS

1. Find its `GetCapabilities` document and a numerical DTM coverage.
2. Request `DescribeCoverage` for that identifier. Record the CRS, bounds,
   grid spacing, grid alignment, axis labels and formats.
3. Pick the matching supported WCS version and copy those values into your entry.
4. Preview one tile inside known coverage before increasing concurrency or area.

For the example above, these URLs help inspect the metadata:

```text
https://mapy.geoportal.gov.pl/wss/service/PZGIK/NMT/GRID1/WCS/DigitalTerrainModelFormatTIFF?SERVICE=WCS&VERSION=2.0.1&REQUEST=GetCapabilities
https://mapy.geoportal.gov.pl/wss/service/PZGIK/NMT/GRID1/WCS/DigitalTerrainModelFormatTIFF?SERVICE=WCS&VERSION=2.0.1&REQUEST=DescribeCoverage&COVERAGEID=DTM_PL-KRON86-NH_TIFF
```

WCS 1.0.0 uses its own request syntax, so use `provider: "wcs-1.0.0"` for that
version. Merely changing a 1.1.1 service's version string cannot make it compatible.

## 6. Example: a direct COG URL

This example uses the configured Wallonia terrain COG hosted for TSRE. A direct
URL identifies the file; TSRE requests the blocks needed for the selected area.
It does not download the entire national raster.

```json
{
  "version": 1,
  "datasets": [
    {
      "id": "user.wallonia.dtm1",
      "name": "My Wallonia DTM 1 m",
      "provider": "file",
      "format": "geotiff",
      "fileGrid": "cog",
      "directory": "user_wallonia_dtm1",
      "crs": 3812,
      "verticalDatum": "DNG / EPSG:5710",
      "resolution": 1,
      "bounds": [542188, 521074, 795273, 667801],
      "concurrentRequests": 2,
      "zeroIsNoData": false,
      "download": {
        "revision": "20260923-cog",
        "urlTemplate": "https://koniec.org/geo/wallonia_mnt_1m_cog.tif"
      },
      "attribution": "Service public de Wallonie - MNT 2021-2022",
      "license": "CC BY 4.0",
      "informationUrl": "https://geoportail.wallonie.be/catalogue/fe13bc84-e371-46ca-9632-8ad4139f1ee5.html"
    }
  ]
}
```

Here `download.urlTemplate` is a literal HTTPS file URL with **no placeholders**.
`download.revision` identifies the published file version and separates cached
parts. Change it when replacing a file at the same URL. `directory` uses letters,
digits, underscores or hyphens; nested directories can use `/`, but not `..`.

The server must support valid HTTP byte-range responses (`206 Partial Content`
with matching `Content-Range`). The TIFF must also use a supported numerical
raster layout and compression. A `.tif` extension or the word “COG” alone is not
enough. For EPSG:4326 single COGs, `download.overviewFactor` is required: a power
of two from `1` to `1024`, with `1` selecting the base image. Omit it for projected
single COGs such as this example.

For a regular projected sheet grid, use `fileGrid: "projected"`,
`download.tileSize` in CRS units, and a URL containing both `{easting}` and
`{northing}`. The Austrian built-in entry demonstrates the filename convention.
These placeholders address source sheets; they are not map-tile `{x}/{y}/{z}`.

## 7. Example: STAC discovery with COG range reads

Canada's HRDEM entry demonstrates choosing a named bare-earth asset from a STAC
collection:

```json
{
  "version": 1,
  "datasets": [
    {
      "id": "user.ca.hrdem1",
      "name": "My Canada HRDEM terrain 1 m",
      "provider": "file",
      "format": "geotiff",
      "fileGrid": "stac",
      "directory": "user_ca_hrdem1",
      "crs": 3979,
      "verticalDatum": "CGVD2013 orthometric height",
      "resolution": 1,
      "bounds": [-2500000, -1000000, 3050000, 3000000],
      "concurrentRequests": 2,
      "zeroIsNoData": false,
      "fallbackApproved": false,
      "distantTerrainApproved": false,
      "download": {
        "endpoint": "https://datacube.services.geo.ca/stac/api",
        "collection": "hrdem-mosaic-1m",
        "assetKey": "dtm",
        "range": true
      },
      "attribution": "Natural Resources Canada and contributing elevation data partners",
      "license": "Open Government Licence - Canada 2.0",
      "informationUrl": "https://open.canada.ca/data/en/dataset/0fe65119-e96e-4a57-8bfe-9d9245fba06b"
    }
  ]
}
```

| Inside `download` | Meaning |
| --- | --- |
| `endpoint` | STAC API root. TSRE constructs the collection-items request itself. This is not an individual TIFF or STAC item URL. |
| `collection` | Exact collection ID. |
| `assetKey` | Exact key inside an item's `assets` object. Here `dtm` selects terrain instead of the surface model or hillshade. Requires `range: true`. |
| `range` | `true` reads COG blocks. The default is `false`, which can acquire whole selected GeoTIFF assets; retain `true` for large mosaics. |
| `searchRoot` | Optional `true` uses the API's root `/search` instead of collection items. Omit `collection` when using this mode. |
| `resolutionProperty` | Optional item-property name containing resolution for a provider with different metadata conventions. |
| `assetCrs` | Optional EPSG code expected in asset metadata, for example a compound CRS whose horizontal part is the dataset's `crs`. |

STAC is not a universal adapter for every catalogue. The API and asset metadata
must match TSRE's implemented discovery and selection rules. Named range assets
such as Canada's obtain their authoritative grid from the COG header. Assets
must advertise a cloud-optimized TIFF media type and matching `proj:epsg`.
The ordinary elevation selector expects asset `eo:gsd` (or the configured item
`resolutionProperty`) to equal `resolution`. Named range assets can omit that
resolution metadata because TSRE validates the grid from the file itself.

The configured rectangle is only an approximate area of availability. Canadian
1 m LiDAR terrain does not cover every pixel inside it. Keep a global fallback
selected and inspect the report. Even a small cold request may first download
megabytes of TIFF index tables before reading the required height blocks.

## 8. Example: your own local GeoTIFFs

For supported local numerical GeoTIFFs, a directory entry needs no download URL.
This example assumes Polish EPSG:2180 rasters; replace the metadata with your
files' actual CRS, extent, spacing and height datum.

```json
{
  "version": 1,
  "datasets": [
    {
      "id": "user.local.dtm",
      "name": "My local terrain GeoTIFFs",
      "provider": "file",
      "format": "geotiff",
      "fileGrid": "directory",
      "directory": "my_dtm",
      "crs": 2180,
      "verticalDatum": "PL-EVRF2007-NH",
      "resolution": 1,
      "bounds": [160000, 90000, 880000, 800000],
      "zeroIsNoData": false
    }
  ]
}
```

Place the files under `<geodata root>/my_dtm/`. Their embedded georeferencing must
agree with the entry. Renaming a file or changing `crs` does not reproject it.
Use a GIS conversion tool first when the raster layout or CRS is unsupported.

## 9. Credentials and defaults

Credentials are referenced by name, not embedded in a source URL. Elevation
supports `query-api-key`, `basic-api-key`, and, for supported STAC file sources,
`basic-user-password`. Support depends on the provider; direct single-COG entries
above do not accept an authentication object.

For a service using a query-string API key, the dataset fragment is:

```json
{
  "authentication": {
    "type": "query-api-key",
    "secret": "elevation.myServiceKey",
    "parameter": "api-key"
  }
}
```

This is a **fragment**, not a complete dataset. For Basic authentication with
separate credentials, the corresponding fragment is:

```json
{
  "authentication": {
    "type": "basic-user-password",
    "usernameSecret": "elevation.myUsername",
    "passwordSecret": "elevation.myPassword"
  }
}
```

Store the actual strings in the active profile's `secrets.json`, beside its
`settings.json`, following the [secret-file example](imagery-data.md#store-the-key-in-your-active-profile).
Use your chosen reference names as keys in its `secrets` object. The
`basic-api-key` variant uses `secret` and sends that key as the Basic username
with an empty password. Never include real credentials in a shared catalogue.

Usually you should select the source in **Load Height** rather than changing
catalogue defaults. The optional top-level `defaultFileSource` names a `file`
provider and is not a general default for WCS, nor the fallback-selection setting.

## 10. Troubleshooting

| Symptom | What to check |
| --- | --- |
| New source is missing | Exact filename and working directory; JSON syntax; restart; location bounds and terrain eligibility. Read the catalogue diagnostic in the source window/log. |
| Existing built-in still appears | Your override may have been rejected. A partial object is not a valid override. |
| Terrain comes entirely from fallback | Inspect primary-source errors, location, asset selection and actual valid-data coverage. A completed preview does not prove the primary source supplied heights. |
| Hills appear in the wrong place | Route georeferencing, source CRS, X/Y order, WCS axis labels and request-grid alignment. |
| Terrain is uniformly too high/low | Source and fallback vertical datums, units and the two height offsets. |
| Sea-level terrain disappears | `zeroIsNoData` may be incorrectly set to `true`. |
| XML/HTML returned instead of a raster | Wrong endpoint, coverage, version, request format or authentication; the server may be returning an error document. |
| Unsupported TIFF/grid/CRS | A valid file may use a layout TSRE cannot decode. Compare with a supported source or convert local data first. |
| Download is unexpectedly large | Check whole-file STAC versus `range: true`, source resolution, area and TIFF index overhead. |
| Old data keeps appearing | Check the provider's publication version and COG `download.revision`; do not change unrelated fields hoping to force a refresh. |

For a new source, start with one tile in known coverage and a modest concurrency.
Compare a few recognisable landforms/heights, then try a coverage edge and a cache
repeat. Share the complete entry, the report and the location when asking for
help, with credentials removed.

See also: [imagery data](imagery-data.md),
[built-in source definitions](../../src/tsre/geo/elevation-datasets.json) and
[elevation implementation status](../tasks/geo/elevation-current-status.md).
