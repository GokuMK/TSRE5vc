# Geoportal 1 m elevation: implementation and verification

2026-09-16. Branch: `geo-terrain`. Supersedes the unresolved service assumptions
in the [initial review](geo-terrain-design-review.md). The original ideas remain
unchanged. The user authorized implementation after resolving design questions.

## Delivered behavior

- Terrain elevation offers local HGT, Geoportal NMT 1 m KRON86 (numeric GeoTIFF),
  and Geoportal NMT 1 m EVRF2007 (ASCII Grid).
- HGT remains the default. Choose a source in the elevation dialog for the current
  session, or set `geo.elevation.source` in Settings for a saved default. The same
  selection is used by manual and automatic terrain generation.
- Load preview prepares a complete result. Apply commits it; Close does not.
  Source/offset changes invalidate the preview. Failed or cancelled loads cannot
  apply stale results. Background loading has progress and cancellation.
- Missing coverage/NoData uses local HGT, with sample counts and reasons in the
  report. Download/decoding failures also use reported HGT fallback. If HGT cannot
  supply every remaining height, no heights from that tile are applied.
- Automatic generation shows a non-modal report for fallback, failures and
  cancellation. Cancelling an automatic tile load stops the enclosing GeoTools
  tile-creation batch. Previously completed tiles and any already-created empty
  terrain files remain; this is not a route-wide rollback.
- Applied heights use the existing terrain refresh/upload path and now capture
  the previous heightmap in Undo. Existing tile sample count, spacing, orientation
  and output format are retained. A 1 m source does not force a 1 m output tile.

## Confirmed Geoportal service contract

The [official service directory](https://www.geoportal.gov.pl/pl/usluga/uslugi-pobierania-wcs/)
lists separate NMT endpoints. Live capabilities, coverage descriptions and small
responses are retained in [the evidence directory](evidence/2026-09-16/README.md).

| Dataset | Coverage ID | Numeric transport |
|---|---|---|
| KRON86, 1 m | `DTM_PL-KRON86-NH_TIFF` | Dedicated `DigitalTerrainModelFormatTIFF` endpoint, `image/tiff` |
| EVRF2007, 1 m | `DTM_PL-EVRF2007-NH` | `DigitalTerrainModel` endpoint, `image/x-aaigrid` |

Both coverage descriptions specify EPSG:2180 with 1 m offset vectors. Native
32 x 32 samples cover 32 x 32 metres. The implemented block requests were also
verified live: 514 x 514 samples, exactly 1 m spacing, with numeric heights.

Important observed differences:

- TIFF from the dedicated endpoint is one uncompressed float32 band, with
  GeoTIFF registration/CRS tags. TIFF requested from the other endpoint returned
  three-channel RGB, which the reader rejects as non-elevation data.
- ASCII is returned in a MIME multipart body alongside auxiliary XML and a PRJ
  definition. The reader extracts the numeric grid and uses the dataset's CRS.
- WCS envelope axis order is `y x`; the request uses named `x` (easting) and `y`
  (northing) subsets. The application explicitly supplies both output dimensions.
- The coverage range metadata reports an inappropriate radiometric unit. Dataset
  configuration identifies terrain heights in metres; that unit string is not
  used to reinterpret the numeric samples.
- The descriptions and small probes do not establish a formal service-wide
  request quota or maximum size. Client requests are conservatively bounded.

## Missing-data policy and height references

An offshore probe returned zeros without a NoData tag in both numeric formats.
The first implementation therefore conservatively treats zero-valued Geoportal
contributors as unavailable and uses reported HGT fallback. This was the stated
initial assumption while the follow-up question was pending, not an explicit
user decision. Genuine sea-level values may also fall back. A footprint/catalogue
check would be needed to distinguish these cases reliably. Generic rasters and
HGT still accept zero as a valid elevation.

Declared NoData and non-finite samples are invalid. Bilinear interpolation does
not combine a valid height with an invalid contributing neighbor. It requests
HGT for that output point instead. Zero-weight neighbors do not invalidate an
exact post. HGT voids (`-32768`) are rejected, not written as terrain heights.

KRON86 and EVRF2007 are separate datasets/cache namespaces. HGT files do not
declare their vertical datum, and TSRE route heights have no new datum contract
in this feature. No automatic vertical transformation or seam blending is
claimed. The report explicitly identifies mixed-source output. The existing
manual Y offset applies once after sampling; it is not a datum transformation.

## Code organization and dependency choice

- `ElevationRaster`: bounded numeric GeoTIFF, ASCII Grid and HGT readers;
  raster registration, affine inversion, validity and interpolation.
- `ElevationSource`: source interface, local HGT implementation, shared raster
  source, provider interface, WCS provider, cache and generation result/report.
- `elevation-datasets.json`: versioned built-in dataset catalogue embedded as a
  Qt resource. Endpoint, coverage, format, axes, CRS, grid, block size, datum and
  zero policy are data, not terrain-generation branches. It is not yet a user
  catalogue editor or a promise of universal WCS compatibility.
- `HeightWindow`: prepares target geographic points with TSRE's existing route
  converter, runs loading/sampling in a worker, previews the result and exposes
  only complete heights to the terrain library. Sampling never performs HTTP.
- Qt terrain callers retain their existing output-grid logic. Legacy simple
  callers also use the selected source; their separate backend migration remains
  outside this feature.

No new application/build dependency. Readers use C++17 and Qt Core/Network.
TIFF support is deliberately bounded: classic TIFF, single float32 or signed
int16 height band, uncompressed strips, either byte order, top-down orientation,
recognized EPSG:2180/4326, scale/tiepoint or affine transform, PixelIsArea/Point,
and optional NoData. Compressed/tiled TIFF, BigTIFF, RGB, arbitrary user-defined
CRSs and multiple image directories fail explicitly. The old unused GeoTiffFile
stub is not used for this feature.

Horizontal conversion supports geographic coordinates and a bounded forward
CS92 projection using the fourth-order Krueger series. The formulas are described
in [PROJ's mathematical documentation](https://proj.org/en/stable/operations/projections/tmerc.html).
Input geographic coordinates are treated as ETRS89 for this projection; no epoch
transformation is performed. Numerical projection accuracy is distinct from the
geographic accuracy of the route, source survey or geodetic datum approximation.

For independent verification only, pyproj/PROJ was installed into an ignored
`build-geo-verification` directory. It is not linked, shipped or required by tests.
The retained reference values cover 130 points over the supported coordinate
domain. The C++ routine's maximum numerical difference was approximately
`2.006e-7 m`. Geoportal's geographic-subset output differed by a few centimetres
near the country's edges and sometimes emitted a user-defined CRS. Production
requests therefore use native projected bounds and native 1 m output, with no
server geographic reprojection step.

## Cache and resource bounds

```text
<geoPath>/
    hgt/N52E019.hgt
    N52E019.hgt                     legacy location, still supported
    cache/<dataset-id>/v1-<configuration-hash>/
        <column>_<row>.tif          or .asc (retained multipart response)
        <column>_<row>.tif.json     request, retrieval time, datum, SHA-256
```

HGT lookup prefers `hgt/`, then the root. No files are automatically moved.
An empty geoPath produces a clear failure instead of writing into the process
working directory. Source datasets use a deterministic grid aligned to the
advertised native origin. Each 512 m block includes a one-pixel sampling margin,
producing a 514 x 514 raster. Neighboring requests share sample registration.

Each request is bounded to 32 MiB, a 30-second transfer timeout and a 45-second
overall deadline. Cancellation aborts outstanding HTTP. After three consecutive
failed acquisitions, remaining uncached blocks fall back rather than repeatedly
waiting for an unavailable service. Valid cached blocks still work.

Raw data and metadata use atomic file replacement; hashes and grid validation
detect incomplete or inconsistent entries. In-memory caches are bounded to
64 MiB of raster heights and 128 MiB of HGT heights per job. Requests are deduplicated
within a job; one tile job is active at a time. There is a 2048-block preparation
limit. Large/distant tiles may still download substantial data at 1 m resolution.

Disk cache is persistent and has no automatic eviction/expiry in this version.
Deleting the relevant managed cache directory forces a refresh. HGT source files
are outside that directory. No persistent terrain-result cache is involved.

## Verification and practical limits

- Standalone offline tests cover real responses, malformed/unsupported files,
  byte order, signed heights, registration, interpolation, NoData, bounded
  projection, cache identity/reuse, HGT path precedence, fallback and cancellation.
- Opt-in live checks successfully fetched both datasets. Repeating the same
  native block used the disk cache without a download.
- Full MinGW/Qt application build passed. Offline elevation: 189 checks;
  headless dialog: 15; settings: 196; terrain-edge regression: 52. All passed.
  The dialog suite checks preview versus Apply, Close, offset invalidation,
  failure after a successful load and cancellation. Its populated preview was
  captured and visually inspected. Two existing settings assertions expected
  unfinished Polish strings that were already translated on main; those
  assertions now check the actual translations.
- In-editor visual comparison against a real route and undo/save/reload acceptance
  remain manual checks. Automated tests do not establish survey accuracy or
  universal Geoportal coverage. Other countries are architectural reference cases,
  not implemented providers.

Standalone tests: configure `tests/geo` with Qt Core/Network, then run `ctest`.
Live probe: `tsre_elevation_tests --live <temporary-geodata-root> <dataset-id>`.
Dialog check: `TSRE5vc --test --test-suite elevation-ui --test-cases <capture.png>`.

Potential follow-ups, not hidden first-version claims: authoritative coverage
masking, compressed GeoTIFF/COG and additional CRS support, disk-cache management,
provider retry policy, source-catalogue editing and vertical datum conversion.
