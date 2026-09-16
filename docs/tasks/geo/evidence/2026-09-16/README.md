# Geoportal investigation evidence

Retrieved 2026-09-16 (Europe/Warsaw). These small files are test/design evidence,
not a terrain cache. No authentication tokens or HTTP cookies are retained.

## Endpoints

- TIFF: `https://mapy.geoportal.gov.pl/wss/service/PZGIK/NMT/GRID1/WCS/DigitalTerrainModelFormatTIFF`
- ASCII: `https://mapy.geoportal.gov.pl/wss/service/PZGIK/NMT/GRID1/WCS/DigitalTerrainModel`

Published by GUGiK in the [official WCS directory](https://www.geoportal.gov.pl/pl/usluga/uslugi-pobierania-wcs/).
The [official NMT description](https://www.geoportal.gov.pl/pl/dane/numeryczny-model-terenu-nmt/)
describes the elevation products and free reuse of NMT data.

## Retained responses

| File | Request / interpretation |
|---|---|
| `tiff-capabilities.xml` | TIFF endpoint, `SERVICE=WCS&REQUEST=GetCapabilities` |
| `ascii-capabilities.xml` | ASCII endpoint, same capabilities request |
| `tiff-description.xml` | TIFF endpoint, WCS 2.0.1 DescribeCoverage, `COVERAGEID=DTM_PL-KRON86-NH_TIFF` |
| `ascii-description.xml` | ASCII endpoint, WCS 2.0.1 DescribeCoverage, `COVERAGEID=DTM_PL-EVRF2007-NH` |
| `native-sample.tif` | TIFF endpoint, KRON86, native subsets below, `FORMAT=image/tiff`; float32 elevation |
| `evrf-sample.asc` | ASCII endpoint, EVRF2007, native subsets below, `FORMAT=image/x-aaigrid`; multipart numeric grid |
| `evrf-sample.tif` | ASCII endpoint, EVRF2007, same subsets, `FORMAT=image/tiff`; RGB image, deliberately rejected |
| `sea-sample.tif` | TIFF endpoint, KRON86, offshore subsets below; unmarked zero values |
| `sea-sample.asc` | ASCII endpoint, EVRF2007, offshore subsets below; unmarked zero values |
| `geographic-subset.tif` | TIFF endpoint, geographic subset around 52 N, 19 E, projected output; projection reference only |
| `projection-west.tif` | TIFF endpoint, geographic subset around 53 N, 14.2 E; projected output has user-defined CRS |
| `projection-east.tif` | TIFF endpoint, geographic subset around 50 N, 24 E; same CRS issue |

Native sample query, appended to the relevant endpoint:

```text
SERVICE=WCS&VERSION=2.0.1&REQUEST=GetCoverage
&COVERAGEID=<coverage from table>
&SUBSET=x(566600,566632)&SUBSET=y(243100,243132)
&FORMAT=<format from table>
```

The offshore query replaces the subsets with
`x(500000,500032)` and `y(780000,780032)`.

Projection probes use `COVERAGEID=DTM_PL-KRON86-NH_TIFF`, `FORMAT=image/tiff`,
`SUBSETTINGCRS=http://www.opengis.net/def/crs/EPSG/0/4326` and
`OUTPUTCRS=http://www.opengis.net/def/crs/EPSG/0/2180`:

- Central: `x(19,19.0001)`, `y(52,52.0001)`.
- West: `x(14.2,14.2001)`, `y(53,53.0001)`.
- East: `x(24,24.0001)`, `y(50,50.0001)`.

The returned projection probes have non-unit pixel spacing due to server
reprojection. They are not the production 1 m request pattern. West/east TIFFs
are intentionally rejected by the production reader because they do not encode
a recognized EPSG code; their raw tiepoints were inspected separately.

## Independent/synthetic fixtures

- `projection-reference.json`: 130 forward-projection control points generated
  with pyproj 3.8.0 / PROJ 9.8.1 using the explicit projection parameters recorded
  in that file. This checks projection arithmetic, not datum/epoch conversion.
  Tests consume the constants and require no Python/PROJ installation.
- `signed16-big-endian.tif`: synthetic classic TIFF, EPSG:2180, 2 x 2, two
  uncompressed one-row strips, signed int16 values `[-2, 0; 10, -9999]`, NoData
  `-9999`, PixelIsArea, scale `(1,1,0)` and tiepoint `(0,0,0,100,200,0)`.
- `signed16-pixel-is-point.tif`: same synthetic data with PixelIsPoint registration.

The JSON manifest records sizes and SHA-256 hashes of the retained evidence.

## Request-size benchmark

`request-size-benchmark.json` records 18 sequential live requests for 512, 1024
and 2048 m cores (each with a one-pixel margin), both numeric formats and three
rounds. It contains URLs, metadata and measurements, not downloaded rasters.
See [analysis and reproduction](../../geoportal-request-size-benchmark.md).
