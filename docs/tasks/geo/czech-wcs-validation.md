# Stage A: Czech DMR 4G through the shared WCS provider

> Historical Stage A report. See [current status and review](elevation-current-status.md)
> for later source additions and the broader bare-WCS-TIFF CRS inference policy.
> The verification results below apply to this milestone, not the current catalogue.

2026-09-16, `geo-terrain`. Stage B is recorded separately in
[the ArcGIS DMR 5G follow-up](czech-arcgis-validation.md).

## Result

DMR 4G works through the same `WcsProvider`, `RasterSource`, cache, sampling and
HGT fallback as Polish GUGiK. There are no Czech-specific terrain branches and
no new application dependencies. The dataset is `cz.cuzk.dmr4g` in the embedded
catalogue and appears in the elevation dialog and Settings.

Configuration alone did **not** work with the original reader: the second service
exposed missing generic capabilities. We added tiled numeric TIFF, WCS multipart
TIFF/GML decoding, and ETRS89 / UTM 33N projection support. With those shared
capabilities, service differences are expressed in dataset configuration.

## Official service and observed contract

The [official WCS metadata](https://geoportal.cuzk.cz/Default.aspx?mode=TextMeta&metadataID=CZ-CUZK-WCS-EL&metadataXSL=metadata.sluzba&side=WCS.INSPIRE&lng=EN)
identifies the INSPIRE Elevation service based on DMR 4G. The
[official product catalogue](https://geoportal.cuzk.cz/dokumenty/katalog_produkce_zu.pdf)
describes the 5 m INSPIRE grid and EVRS heights. This derivative uses EVRS;
the native DMR 4G product's Bpv label should not be applied to it.

| Configuration | Value |
|---|---|
| Endpoint | `https://ags.cuzk.gov.cz/arcgis2/services/INSPIRE_Nadmorska_vyska/ImageServer/WCSServer` |
| Protocol / coverage | WCS 2.0.1 / `MD_LAS` |
| Format | `image/tiff`, returned inside multipart with GML |
| Horizontal CRS | EPSG:3045, ETRS89 / UTM zone 33N (N-E axes) |
| Native spacing | 5 m, confirmed by DescribeCoverage and returned TIFF transforms |
| Named subset / scaling axes | `x` for easting, `y` for northing |
| Native corner origin, internal E,N | 292001.732277163304, 5657998.94293634221 |
| Cache block | 256 x 256 source pixels = 1280 x 1280 m |
| Request including sampling halo | 258 x 258 pixels = 1290 x 1290 m |
| Concurrency | 1; Czech concurrency/size limits have not been benchmarked |
| Missing-data policy | Dataset-level `zeroIsNoData: true`; absent TIFF tiles also produce NoData |

Despite the endpoint's `ImageServer` path, requests use WCS, not ArcGIS REST
`exportImage`. The generic named `SUBSET` and `SCALESIZE` builder needed no changes.
The alternative advertised coverage `MD_LAS_sluzbaEL` failed DescribeCoverage
during the probe; the tested configuration uses `MD_LAS`.

[EPSG:3045](https://epsg.org/crs/gml/id/3045) declares northing/easting axis order.
Named WCS axes avoid relying on positional request order. The raster's TIFF
affine coordinates and internal `XY` use easting/northing. The shared bounded
Transverse Mercator routine now also supports EPSG:3045 and its E-N counterpart
EPSG:25833, using central meridian 15 degrees and scale 0.9996. Forty independent
PROJ controls at latitudes 48–52 and longitudes 12–19 agree within 1 mm.
PROJ was used only as a development oracle, not added as a runtime/build dependency.

## Reader and missing-data findings

- Responses contain LF-delimited MIME parts: GML and uncompressed float32 TIFF.
  TIFF uses 128 x 128 storage tiles, padded beyond the requested image edges.
- These TIFFs contain pixel scale/tiepoint but no GeoKeyDirectory. CRS is accepted
  from the accompanying GML only after checking its EPSG identity and `cid:`
  reference to the TIFF part. Bare TIFF without an identifiable CRS still fails;
  explicit conflicting TIFF/GML CRS values fail too. Binary data is not trimmed.
- TIFF affine registration is used; the provider still verifies exact requested
  dimensions, origin, spacing, north-up orientation and CRS before caching.
- A fully uncovered request returned tile offsets and byte counts both zero.
  Such absent tiles become NaN samples; inconsistent offset/size pairs fail.
- A border request returned a mixture of populated tiles, an absent tile, and
  unmarked zero pixels in populated tiles. The existing configurable zero policy
  handles these conservatively. Zero remains valid in the generic raster reader.
- No vertical datum conversion is performed. HGT blending retains the existing
  visible provenance report and vertical-datum notice.

## Validation

Only the small standalone `tests/geo` executable was built/run. **The main TSRE
application and its main test suites were not built/run for this change.**
The standalone run completed **274 checks with zero failures**. Evidence hashes
and updated translation XML were also checked.

Live production-path requests (two nearby sample points per location):

| Probe | Result |
|---|---|
| Prague, 50.08 / 14.42 | One download, two primary samples, approximately 197.444 / 199.154 m; repeat: one cache hit, zero downloads |
| Brno, 49.195 / 16.607 | One download, two primary samples, approximately 217.138 / 216.793 m |
| Ostrava, 49.835 / 18.292 | One download, two primary samples, approximately 214.570 / 214.534 m |
| Plzen, 49.747 / 13.377 | One download, two primary samples, approximately 313.497 / 313.434 m |
| Germany, 48.6 / 13.5 | Two uncovered blocks; two reported NoData samples and two HGT fallbacks using a synthetic constant-height HGT fixture |
| Polish KRON86 regression, 52 / 19 | One download, two primary samples, approximately 105.417 / 105.383 m |

Offline regressions cover both existing Polish formats, MIME LF/CRLF framing and
binary preservation, tiled/padded/sparse rasters, malformed offsets and lengths,
CRS/reference conflicts, adjacent grid registration, projection controls, cached
production Czech sampling and HGT fallback. See the retained
[responses, hashes and logs](evidence/2026-09-16/czech/README.md).

These probes validate the software/service contract, not survey accuracy or
complete national availability. Requests crossing the service's outer rectangular
extent may fail (InvalidSubsetting); the current generic fixed-block implementation
reports unavailable blocks and uses HGT. Country-edge clipping and authoritative
coverage masks remain follow-ups. Compressed TIFF is still unsupported.

## User acceptance still needed

Build the main application in **Release**, then run the `settings` and
`elevation-ui` test suites and try a Czech route in the editor. Select
**Czechia - CUZK DMR 4G (5 m, EVRS)**, load a preview, inspect the report, apply,
and check undo/save/reload. Repeat the area to confirm cache reuse. The source
uses 5 m samples; existing MSTS terrain output spacing is unchanged.

Optional small live probe (uses its supplied cache directory):

```text
tsre_elevation_tests --live-at <temporary-geodata-root> cz.cuzk.dmr4g 50.08 14.42
```
