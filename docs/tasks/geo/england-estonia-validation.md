# England and Estonia elevation integration

Validated 2026-09-19 on `feature/geo-terrain`, following the user's failed
requests and the other agent's catalogue notes. Both use the existing generic
WCS provider, shared cache and raster sampler. No new dependency or country-specific
terrain code was added.

## England: WCS 2.0.1 axes and server-expanded grids

The other agent proposed server-side EPSG:27700 to EPSG:3857 reprojection,
separate coverage/scaling axes, preservation of valid zero elevations, and live
checks of conservative coverage bounds. Those checks found:

- Capabilities advertise EPSG:3857. DescribeCoverage describes the **native**
  grid with coverage axes `E/N` and scaling axes `i/j`. For **Web Mercator
  subsetting**, this service requires uppercase `X/Y`, with `i/j` for scaling.
  `E/N` subsets, lowercase `x/y` subsets, or `X/Y` scaling produced HTTP 500.
  The catalogue endpoint and the operation URL advertised by capabilities agree.
- Correct axes return uncompressed, big-endian, tiled Float32 TIFF, EPSG:3857,
  with the expected 1026 x 1026 dimensions. Fractional heights are preserved.
  NoData is explicitly tagged near `-3.4028235e38`; it is not a terrain height.
- Reprojection expands the envelope instead of preserving the requested grid.
  For block `-8,-3279`, requested bounds are
  `[-16386, 6713342, -14334, 6715394]` at 2 map m. Returned upper-left corner is
  `[-16438.23054782434, 6715445.835153859]`, spacing approximately
  `[2.10118976862, -2.10207763225]` map m. Merely fixing axes would still fail
  TSRE's original exact-grid checks. WCS 1 also expands the grid, so switching
  protocol does not solve this issue.

Configuration now uses `axisX=X`, `axisY=Y`, `scaleAxisX=i`, `scaleAxisY=j`,
and `allowExpandedGrid=true`. Scaling axes default to the subset axes for
existing sources. Expanded-grid acceptance is opt-in and still requires:

- Exact dimensions and CRS, finite north-up georeferencing, no rotation.
- All requested pixel centres, including the halo, inside the returned
  sampleable area; no clamping or silent uncovered edges.
- At most 10% of the requested span beyond each edge. Excessively large,
  shifted, truncated or differently projected responses remain rejected.

Raw caches retain the actual TIFF transform. Ordinary sampling uses it directly.
If a dataset combines expanded grids with NoData filling, the fill mosaic samples
each block onto the common requested grid before stitching; it must not copy
misaligned pixels by index. England currently keeps the default fallback policy.

The 2 map m setting describes the request grid, not guaranteed returned spacing
or native survey resolution. Output near the tested London area is about 1.3 m
on the ground. ODN heights receive no vertical datum conversion.

## Estonia: format spelling and final-strip padding

The other agent suggested a configurable WCS 1 format spelling, WCS 2 as an
alternative, or local EPSG:3301 support if server reprojection proved unsuitable.
The first option suffices:

- `FORMAT=GeoTIFF` returns HTTP 200 with an XML `InvalidParameterValue` exception.
  `FORMAT=image/tiff` returns uncompressed Float32 data with exact EPSG:3857
  bounds, 514 x 514 dimensions and 2 map m spacing, including fractional heights.
- Optional `requestFormat` controls the wire spelling independently of the
  decoder's `format`. Existing WCS 1 TIFF datasets retain `GeoTIFF` by default.
- Some responses store a final strip padded to three rows although only one
  image row remains. The decoder now permits that full-strip padding, decodes
  only the image rows, and checks that every declared byte exists. Arbitrary
  extra lengths and truncated padding remain errors.

No WCS 2 migration or local Lambert Conformal Conic implementation is needed.
EH2000 / EVRS heights, valid negative values and valid coastal zero remain intact.
Some sea areas return zero without a NoData tag; do not interpret those values as
independently verified seabed measurements. Rectangular bounds are not a national
coverage mask. Default HGT fallback still applies to marked gaps/unavailable data.

## Validation and evidence

A standalone probe exercised the production `generate()` path with a 256 x 256
grid at approximately 8 m ground spacing, crossing cache-block boundaries near
the user's reported blocks. It used isolated caches and no HGT files.

| Source | Primary samples | HGT / unavailable | Downloads | First run | Cached repeat | Height range |
|---|---:|---:|---:|---:|---:|---:|
| England | 65,536 | 0 / 0 | 4 | 21.9 s | 4 hits, 0 downloads, 646 ms | 3.10..29.75 m |
| Estonia | 65,536 | 0 / 0 | 20 | 23.8 s | 20 hits, 0 downloads, 721 ms | -0.98..26.13 m |

These are individual live runs, not a concurrency benchmark. Estonia retains
512-pixel cores and one connection, explaining its higher request count at this
latitude. The probe's map-space centres were `(-14336,6713344)` and
`(2755584,8278528)` respectively, not a reproduction of the user's route geometry.

**Release application build succeeded; 337 offline standalone checks passed.**
Application UI suites were not run. Added regressions cover format aliases,
separate scaling axes, expanded-grid acceptance/rejections, actual Float32 TIFFs,
the English NoData sentinel, preserved Estonian zero/fractional values and final
strip padding/truncation. Small raw responses, request URLs and independently
unpacked control values are retained in [evidence](evidence/2026-09-19/ee-gb/README.md).
Full responses, tile-probe source and logs are in ignored `build-ee-gb-research/`.

The user subsequently confirmed that both sources work. Remaining: broader
national/coverage-edge checks and separate
[distant-terrain validation](distant-terrain-elevation.md). These detailed-tile
results do not approve either source for distant terrain. No distant restriction
was implemented by this change.

## Estonia block-size comparison, 2026-09-19

After the user confirmed both sources work, tested 512 and 1024 cores through
production `generate()` using temporary resource catalogues. The application
catalogue remains at 512; this follow-up is a measurement, not a configuration
change. No main application build or test suite was needed.

Same 256 x 256 terrain grid as above, 8 m ground spacing near latitude 59.451,
2 map m source spacing, one connection, isolated empty disk caches. Runs were
ordered 512, 1024, 1024, 512, each followed by a cache-only repeat. Server-side
cache state is uncontrolled; these are two measurements per size, not a general
service-performance guarantee.

| Core size | Actual TIFF size with halo | Requests | Downloaded TIFF bytes | Fresh-cache times | Cache-only times |
|---|---|---:|---:|---|---|
| 512 | 514 x 514 | 20 | 21,193,404 | 24.00 / 24.40 s | 728 / 725 ms |
| 1024 | 1026 x 1026 | 9 | 37,955,475 | 15.50 / 14.52 s | 1232 / 887 ms |

Every run produced **65,536 primary samples, zero fallback**, with validated
EPSG:3857, exact bounds and 2 map m spacing. Cache-only repeats made no requests.
No 512-only service limit was found: 1024 succeeds through the same decoder and
strict grid validation.

For this placement, 1024 reduced requests by 55% and mean first-run time by
about **38%**, while downloading **79% more bytes** because larger aligned blocks
include more unused surroundings. Cache-only loading was slightly slower.
Request counts depend on area placement; four times the block area does not
necessarily yield one quarter of the requests for an isolated terrain tile.

The generated heights were stable across repeated runs of each size. Between
sizes, maximum difference was **0.167264 m**, mean absolute difference
**0.000250596 m** across all output samples. Thus different server request extents
are not numerically identical despite matching output grid registration; no
claim of bit-identical terrain is made. This coastal test includes valid zeros.

**Recommendation:** use 1024 for lower download latency, accepting extra cache
space for isolated tiles. Keep one connection; concurrency was not tested here.
No evidence from this experiment establishes distant-terrain suitability.

[Raw run results](evidence/2026-09-19/ee-gb/size-comparison.txt) are retained.
The standalone probe, temporary resources and downloaded blocks are in ignored
`build-ee-gb-research/size-test/`.

## Official service references

- [Environment Agency WCS capabilities](https://environment.data.gov.uk/spatialdata/lidar-composite-digital-terrain-model-dtm-1m/wcs?SERVICE=WCS&REQUEST=GetCapabilities&VERSION=2.0.1)
- [Environment Agency coverage description](https://environment.data.gov.uk/spatialdata/lidar-composite-digital-terrain-model-dtm-1m/wcs?service=WCS&request=DescribeCoverage&version=2.0.1&coverageId=13787b9a-26a4-4775-8523-806d13af58fc__Lidar_Composite_Elevation_DTM_1m)
- [Estonian WCS](https://teenus.maaamet.ee/ows/wcs-dtm?SERVICE=WCS&REQUEST=GetCapabilities&VERSION=1.0.0)
