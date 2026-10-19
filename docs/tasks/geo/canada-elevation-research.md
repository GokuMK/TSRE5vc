# Canada elevation: HRDEM 1 m integration and source review

Research date: 2026-10-17. Reviewed in the main `TSRE5vc` worktree on `main`.
The built-in `src/tsre/geo/elevation-datasets.json` now contains
`ca.nrcan.hrdem1`. Research findings below retain the original bounded service
probes; the implementation and validation sections describe the resulting work.

## Decision

Use NRCan's public **HRDEM Mosaic terrain (DTM), 1 m** through STAC discovery
and the existing range-COG provider. The user selected only this detailed source:
the existing world sources already provide the required coarse coverage, so
**MRDEM is not added**.

Coverage is incomplete; the existence of a large STAC tile does not establish
valid terrain at every pixel. Gaps use the existing selected global fallback.
The 2 m collection and project-specific products remain outside this change.
DSM, hillshade and northern ArcticDEM assets are never substituted for DTM.

This uses the existing Height window, dynamic source settings, cache, sampling,
NoData reporting, and single selected fallback. No Canada-specific terrain
generator, login workflow, new raster library, or full-country download is
introduced. The existing global defaults remain unchanged. The source is not
approved as a fallback or for distant terrain.

## Official products and access

| Product | Useful role | Delivery and limitations |
|---|---|---|
| [MRDEM](https://open.canada.ca/data/en/dataset/18752265-bda3-498c-a4ba-9dfe68cb98da) | Investigated, excluded: existing world sources cover the coarse-terrain need | DTM and DSM are separate public COGs. Replaces the unsupported CDEM. Combines HRDEM and Copernicus-derived data; outside lidar coverage, terrain is modelled rather than a direct ground survey. |
| [HRDEM Mosaic](https://open.canada.ca/data/en/dataset/0fe65119-e96e-4a57-8bfe-9d9245fba06b) | Detailed terrain where available | Public WCS and STAC/COGs. Collections `hrdem-mosaic-1m` and `hrdem-mosaic-2m`; select the `dtm` asset explicitly. Mosaic incorporates the latest available projects. |
| HRDEM acquisition projects | Possible later specialist source | STAC also exposes `hrdem-lidar` and `hrdem-arcticdem`. Individual projects add overlap/version/CRS decisions that the national mosaic already resolves. |
| CDEM | Legacy only | Still discoverable in STAC, but NRCan identifies MRDEM as its replacement. Do not start a new integration around CDEM. |

Both researched products use CGVD2013 orthometric heights. The inspected COGs use
NAD83(CSRS) / Canada Atlas Lambert, EPSG:3979, in metres. HRDEM collection metadata
identifies Open Government Licence - Canada 2.0. The catalogue includes NRCan
attribution, licence and official product links.

Discovery: <https://datacube.services.geo.ca/stac/api/collections>

## Bounded live findings

Requests were anonymous. This section records the initial metadata/header and
small-service probes; subsequent TSRE tests appear under Validation below.

### COG profiles

For each URL below, a GET with `Range: bytes=0-262143` returned HTTP 206,
262,144 bytes, and the exact matching `Content-Range`.

| Observed property | MRDEM DTM | HRDEM 1 m DTM sample |
|---|---|---|
| URL | `https://canelevation-dem.s3.ca-central-1.amazonaws.com/mrdem-30/mrdem-30-dtm.tif` | `https://canelevation-dem.s3.ca-central-1.amazonaws.com/hrdem-mosaic-1m/9_2-mosaic-1m-dtm.tif` |
| Total object bytes | 83,854,027,098 | 309,309,153,393 |
| Last-Modified | 2026-06-24 | 2026-05-26 |
| Raster dimensions | 185,220 x 166,668 | 500,000 x 500,000 |
| Native spacing | 30 m | 1 m |
| Encoding | Little-endian BigTIFF, one Float32 band, LZW, predictor 1 | Same |
| Internal tiles | 512 x 512 | 512 x 512 |
| CRS / registration | EPSG:3979 / PixelIsArea | Same |
| NoData | -32767 | -32767 |
| Native offset/count tables | 944,096 + 472,048 bytes | 7,636,232 + 3,818,116 bytes |
| Linked reduced-resolution images | 9 | 10 |

Headers fit the existing range reader's supported profile and table-size guards.
Compressed block decoding and TSRE sampling were subsequently validated below.
The HRDEM example requires roughly 11 MiB of native tile tables on a cold cache
before terrain blocks; budget for this even for a small requested area. No whole
object was downloaded. Preserve valid zero and negative elevations.

MRDEM's projected raster rectangle is
`[-2500020, -1000020, 3056580, 4000020]`. This is a file envelope, not a Canadian
land/valid-data mask. Its official
[VRT](https://canelevation-dem.s3.ca-central-1.amazonaws.com/mrdem-30/mrdem-30-dtm.vrt)
also publishes the projection definition and grid.

### STAC compatibility gaps

Example query:

```text
https://datacube.services.geo.ca/stac/api/collections/hrdem-mosaic-1m/items?bbox=-75.71,45.40,-75.69,45.42&limit=1
```

This returned item `9_2-mosaic-1m`. Its properties contain `proj:epsg: 3979`,
`proj:shape: [500000,500000]`, `proj:geometry`, and a GDAL-order
`proj:transform: [1500000,1,0,0,0,-1]`. Assets include both `dsm` and `dtm`, plus
hillshades. The inspected item has no asset `eo:gsd` and no `proj:bbox`.

Before this integration, `CogElevationSource.cpp::prepareStac()`:

- selects the first COG matching resolution and CRS, with no asset-key filter;
- requires asset `eo:gsd` or one configured scalar item resolution property;
- requires asset/item `proj:bbox` for range reads;
- chooses `created`, then `datetime`, for revision identity, ignoring `updated`.

Consequently a JSON-only Canadian STAC entry could not work. Merely relaxing the
resolution check would also risk choosing DSM before DTM. The observed item was
created in 2024 but updated in 2026, so cache revision handling matters when an
asset URL is reused.

### WCS alternative

The advertised `/ows/elevation` URL redirects with HTTP 308 to
`https://datacube.services.geo.ca/wrapper/ogc/elevation-hrdem-mosaic`.
Capabilities advertise **WCS 1.1.1**, including `dtm`, `image/geotiff`, and output
CRSs 3979, 4326, 3857, 3978 and 4617. TSRE currently implements WCS 1.0.0 and
2.0.1 only. This is not a drop-in existing WCS catalogue entry.

A small request returned HTTP 200 and a 262,534-byte numeric TIFF:

```text
SERVICE=WCS&VERSION=1.1.1&REQUEST=GetCoverage
&FORMAT=image/geotiff&IDENTIFIER=dtm
&BOUNDINGBOX=1500000,-100,1500100,0,urn:ogc:def:crs:EPSG::3979
&GRIDBASECRS=urn:ogc:def:crs:EPSG::3979
&GRIDOFFSETS=1,-1&GRIDORIGIN=1500000,0
```

The response was big-endian classic TIFF, Float32, uncompressed, 100 x 100,
256 x 256 storage tiles, EPSG:3979, NoData -32767. However, its pixel spacing was
**1.01 m**, with upper-left corner `(1499999.5, 0.5)`. That does not satisfy the
current exact request-grid validator. This single probe is not proof that an
exact grid cannot be requested, but origin/envelope semantics must be resolved
before adopting this provider. `allowExpandedGrid` is not a justification for
accepting an unexplained pixel-size change.

NRCan's [WCS usage guide](https://download-telecharger.services.geo.ca/pub/elevation/dem_mne/HRDEMmosaic_mosaiqueMNEHR/HRDEM_Mosaic_WCS-WMS_instructions_EN.pdf)
documents explicit grid offsets and recommends smaller requests for large
high-resolution areas. Prefer COGs initially; generic WCS 1.1.1 can remain an
alternative if later benchmarks justify it. Do not assume a WMS visualization
layer returns numerical heights.

## Implementation

The catalogue entry is `ca.nrcan.hrdem1`, displayed as **Canada - NRCan HRDEM
Mosaic terrain 1 m (STAC/COG)**. It uses `hrdem-mosaic-1m`, four concurrent range
requests, native 1 m spacing and directory `geoPath/ca_nrcan_hrdem1/`. The source
bounds `[-2500000,-1000000,3050000,3000000]` are the union of all 56 DTM item
rectangles returned by the collection on the research date, not a coverage mask.

### Projection

`CrsTransform.cpp` adds EPSG:3979 using its existing ellipsoidal Lambert
Conformal Conic 2SP implementation: GRS80, latitude of origin 49 degrees,
central meridian -95 degrees, standard parallels 49 and 77 degrees, false
easting/northing zero. Independent Esri GeometryServer controls in Vancouver,
Ottawa, Halifax and Yellowknife, plus the projection origin, agree within 1 mm.
Controls used `inSR=4617`, `outSR=3979` to test the projection itself.

Geographic input is treated as NAD83(CSRS). No epoch-dependent WGS84-to-NAD83(CSRS)
datum shift is implemented; these controls do not establish metre-level global
positioning accuracy. Source heights remain CGVD2013. The selected HGT/GEDTM30
fallback may use a different vertical datum; the existing independent offsets
remain available and no automatic vertical conversion is implied.

### Generic STAC changes

- Optional `download.assetKey` selects exactly the requested COG asset. Canada
  sets `dtm`; missing terrain never selects DSM or hillshade. This option requires
  `range: true`, preventing accidental whole-file acquisition of large mosaics.
- Named assets may omit resolution metadata, but explicit metadata CRS/resolution
  conflicts remain rejected. The COG reader always validates the actual CRS and
  configured resolution before loading samples.
- For named range assets, read bounds directly from the validated COG header and
  intersect the requested window with its pixel-centre extent. There is no need
  to interpret the nonstandard STAC transform or require `proj:bbox`. Nonoverlapping
  assets need only a cached header, not their large tile tables. Adjacent assets
  retain interpolation neighbours through the existing common-grid mosaic.
- Range-cache identity includes `updated` when available, in addition to the
  existing created/datetime revision and asset URL. Items without `updated`
  preserve their existing identity. Existing Swiss/Swedish metadata-based
  selection remains supported and is covered by regression checks.

No new dependency, WCS protocol, MRDEM source or fallback chain was added.
Both `fallbackApproved` and `distantTerrainApproved` remain false for HRDEM.

## Validation

The focused Release elevation suite passes **475 checks, zero failures**.
New checks cover catalogue restrictions, DTM-only asset selection, metadata
conflicts, revision updates, Swiss/Swedish compatibility, and independent
Canadian projection controls.

Live `tsre_elevation_tests --live-at` / `--live-grid` probes on 2026-10-17:

| Probe | Result |
|---|---|
| Ottawa, `(45.41,-75.7)` | Two primary heights, 70.2224 m and 70.0635 m; one downloaded block; 6.99 s. |
| Ottawa repeat | Identical values; one cache block, zero raster downloads; 0.91 s. |
| Ottawa terrain-sized area, `(45.405,-75.73)`, latitude span 0.0184, longitude span 0.0262, 2048 x 2048 output | All 4,194,304 samples primary; 36 blocks downloaded, zero fallback; 40.07 s. |
| Same area repeated | All 4,194,304 samples primary; 36 cached blocks, zero raster downloads; 5.15 s. |
| 500 km mosaic seam, starting `(45.438479151459205,-75.82963305052047)` | Two primary heights, 121.942 m and 121.907 m, one block from each of `8_2` and `9_2`; zero fallback; 8.29 s. |
| Vancouver, `(49.28,-123.12)` | Two primary heights, 26.5974 m and 26.6904 m; one downloaded block; 15.35 s. |
| Uncovered Manitoba, `(55,-100.4)` | Two HRDEM NoData samples resolved by the existing HGT fallback; zero unavailable samples or issues; heights 309 m and 308.82 m; 18.20 s. |

The seam probe starts at projected x=1,499,999 m and the second sample crosses
x=1,500,000 m. Each `--live-at` probe uses the stated coordinate and a second
point offset by +0.00005 degrees in both axes. The grid probe spans approximately
2 km and requests 1 m output sampling; it is not a 32 km distant-terrain test.
Repeat probes still perform STAC discovery; zero raster downloads does not mean
fully offline operation. Timing includes concurrent application-build activity
for the larger probes and is not a dedicated performance benchmark.

The agent's full application build was stopped before linking to avoid overlap
with the user's Run/build. The user subsequently confirmed that the source works
well in the application on 2026-10-17. CGVD2013-to-global-fallback visual seam
assessment and survey accuracy remain outside the standalone checks above.
Distant terrain stays disabled.
