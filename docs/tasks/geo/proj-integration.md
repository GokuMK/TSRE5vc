# Deferred task: PROJ integration for elevation CRS transformations

Recorded 2026-09-21. Research and design only; implementation is deliberately
staged for later. The small internal `Geo::CrsTransform` extraction is documented
separately in [crs-transform.md](crs-transform.md). This task does not block the
current COG overview/range work or sources that already use that bounded converter
or verified server reprojection.

## Decision

PROJ remains worthwhile once TSRE needs a broad set of datum operations or more
complex correction grids. The internal converter now covers Luxembourg EPSG:2169
with one published transformation and British National Grid with an injected
OSTN15 Lite grid. Resume this PROJ investigation before reproducing additional
national datum engines, especially compound horizontal/vertical pipelines.

The reason is the nature of the remaining work rather than the number of map
projection formulas. Both EPSG:2169 and EPSG:27700 use Transverse Mercator, but
accurate conversion from TSRE geographic coordinates also requires national
datum handling. Further downloadable sources use other national CRSs and
compound horizontal/vertical systems. PROJ already maintains these coordinate
operations, their areas of use and their required grids.

PROJ must remain limited initially to elevation CRS conversion. It does not
replace:

- the TSRE route projection and New Route coordinate-converter work;
- elevation acquisition, provider selection or caching;
- the numeric TIFF/COG reader;
- explicit dataset metadata and vertical-datum reporting.

Existing working sources must not begin to depend on PROJ merely as part of the
first experiment.

## Current TSRE position

`Geo::CrsTransform` is a small forward-only implementation, separated from raster
decoding and precomputed once per elevation source. It currently handles
geographic coordinates, Web Mercator, Polish CS92, Slovenia D96/TM, ETRS89/UTM
zones used by the catalogue, Finland TM35FIN, EPSG:3035 and the official Swiss
EPSG:2056 approximation. It intentionally is not a general CRS or datum engine.

This remains adequate for current services and the Austria, Switzerland,
Luxembourg and Wales file sources. Several WCS entries ask the server for a
supported output CRS, avoiding their native national projection. Direct COGs
have no server that can perform that step.

Candidate sources that can still make PROJ relevant include:

- Slovakia downloads: verify the product's declared horizontal and vertical
  CRSs and the conversion rasters supplied with the data;
- later native file sources in Sweden, Ireland, France and other national CRSs;
- any future decision to normalize heights between national vertical datums.

## What PROJ contributes

PROJ provides CRS definitions, projection calculations, datum transformations,
operation selection and grid-based horizontal/vertical corrections. The C API
can construct a transformation from authority codes and transform arrays of
coordinates. It also provides a context per worker thread.

Important limits remain:

- PROJ cannot infer missing or inaccurate catalogue metadata.
- A horizontal EPSG code does not describe the elevation values' vertical datum.
- The application must decide whether heights remain in the source datum or are
  explicitly transformed to a declared target vertical datum.
- A mathematically valid fallback operation can be too inaccurate for 1 m
  terrain. Missing required grids must not silently select a ballpark transform.
- WGS 84 and ETRS89 must not be treated as unnamed interchangeable inputs when
  metre-level positioning matters. The source geographic CRS must be explicit.

## Proposed constrained integration

Extend the existing `Geo::CrsTransform` boundary with one private PROJ-backed
strategy rather than exposing PROJ throughout terrain code.

```text
catalogue source/target CRS
          |
          v
Geo::CrsTransform
          |
          +-- current lightweight transform for already supported cases
          |
          `-- PROJ operation for an opted-in datum-sensitive CRS
                    |
                    v
             raster easting/northing
```

The first implementation should:

1. Create and validate the operation once per generation job, not per sample.
2. Normalize axis order for application use and keep TSRE input consistently
   longitude/latitude in degrees.
3. Transform batches with `proj_trans_array()` or the equivalent strided API.
4. Use a separate `PJ_CONTEXT` and operation object per worker thread.
5. Supply the source area of interest so PROJ selects an applicable operation.
6. Disallow ballpark transformations for sources that require a declared datum
   grid. Report the missing resource and source name visibly.
7. Transform horizontal coordinates only unless a separately declared vertical
   conversion has been requested and validated.
8. Preserve the current implementation as the initial fallback and comparison
   oracle. Decide about broader migration only after source and package tests.

## Network and resource policy

PROJ coordinate transforms are local. Its optional network layer is unsuitable
as the first TSRE integration because it uses curl, can perform synchronous
resource fetches and would bypass the existing Qt download reports and controls.

Use this policy:

- build PROJ with curl/network support disabled;
- never rely on `PROJ_NETWORK` or an implicit CDN request;
- install the version-matched `proj.db` with the application, or evaluate the
  newer static resource-embedding option;
- download optional grids explicitly through TSRE's Qt network layer;
- store downloaded grids in a stable named location such as `assets/geo/`, with URL,
  version, checksum, licence and attribution metadata;
- give each PROJ context explicit application and downloaded-grid search paths;
- preflight grid availability before terrain acquisition begins.

Keep core runtime data and user/downloaded data distinct. `proj.db` is part of
the application dependency and must remain compatible with the linked PROJ
version. Optional national grids are catalogue-managed source resources and can
be updated independently when their identity changes.

The full `proj-data` package must not be bundled or downloaded. Select only the
grids required by enabled TSRE sources.

## Wales implementation and Slovakia grid examples

The Welsh elevation publication does not contain OSTN15. The implemented source
downloads Ordnance Survey's official OSTN15/OSGM15 Lite developer archive and
retains only its 20 km text grid in `assets/geo/`. The internal converter applies
bilinear horizontal shifts; OS reports 0.08 m horizontal RMS against full OSTN15.
This keeps the 1 m source accurately placed without adding PROJ. OSGM15 values are
not applied to the DTM's existing ODN heights.

PROJ-data also distributes a full-resolution alternative:

```text
uk_os_OSTN15_NTv2_OSGBtoETRS.tif
```

OSTN15 is a horizontal easting/northing correction. OSGM15 is a separate
vertical model. Reading an existing Welsh DTM requires correct horizontal
placement; changing its stored heights is a different, explicit operation.

PROJ-data also currently lists Slovak resources including:

```text
sk_gku_JTSK03_to_JTSK.tif
sk_gku_Slovakia_ETRS89h_to_Baltic1957.tif
sk_gku_Slovakia_ETRS89h_to_EVRF2007.tif
```

Do not assume these are the exact resources needed by the source being reviewed.
Match the raster metadata and supplied conversion files to a documented CRS
pipeline before enabling them. A grid supplied alongside an elevation product
may be registered as a local auxiliary resource rather than downloaded again.

## Build and packaging findings

PROJ is not a header-only or single-file dependency. Its supported CMake use is
an imported `PROJ::proj` target. SQLite and `proj.db` are required. libtiff is
optional in PROJ generally but is needed for the modern GeoTIFF transformation
grids relevant here. curl is optional and should be disabled. nlohmann/json may
use PROJ's vendored copy.

A representative packaged PROJ library is only several megabytes, while
`proj.db` is roughly 8-9 MB. A Windows package with database, SQLite, libtiff,
zlib and selected grids should be expected to add tens of megabytes. Record the
actual MinGW Release footprint during the investigation; do not use this estimate
as an acceptance result.

Evaluate these packaging choices:

1. **Shared dependency:** ship `proj.dll`, dependency DLLs and a relative
   `share/proj` directory. This is easiest to update and inspect.
2. **Static PROJ with embedded core resources:** potentially simpler at runtime,
   but still has build dependencies and can enlarge/relink the main executable.
3. **Optional developer dependency:** `TSRE_WITH_PROJ` plus
   `find_package(PROJ CONFIG)`, with official packages enabling it and catalogue
   entries that require it hidden or rejected clearly in builds without it.

Directly copying PROJ sources into the existing source glob is not recommended.
If source-based reproducible builds are desired, use a pinned dependency build
or subproject with explicit CMake options, licences and update procedure.

The investigation must use TSRE's supported Qt 6/MinGW toolchain, including a
Release application launched outside the build directory. Verify Debug/Release
and static/shared ABI choices before selecting a distribution method.

## Licence findings

- PROJ uses the permissive MIT/X licence.
- SQLite is public domain; libtiff, zlib and curl use permissive licences.
- PROJ-data accepts only open, permissively licensed grids, but every selected
  grid retains its own copyright, licence and attribution requirements.

Maintain a manifest for all shipped or automatically downloaded grids. Do not
infer that the PROJ core licence covers third-party transformation data.

## Future investigation checklist

- [ ] Recheck the current stable PROJ release and minimum toolchain requirements.
- [ ] Build a minimal Release package with SQLite and TIFF, curl disabled, tools
      and upstream tests omitted from the TSRE runtime package.
- [ ] Decide shared versus static/embedded packaging and record exact installed
      files and sizes.
- [ ] Establish application-relative paths for `proj.db` without requiring user
      environment variables such as `PROJ_DATA`.
- [ ] Add a standalone wrapper probe without changing terrain generation.
- [ ] Transform 65,536 points as individual and batched calls; measure operation
      construction separately and verify worker-thread contexts.
- [ ] Compare existing EPSG:3035 and EPSG:2056 results against the current code.
- [x] Validate Luxembourg against official reference points and its current
      LUREF2020 definition; the internal result agrees within 2 cm.
- [x] Validate EPSG:27700 with official OSTN15 Lite controls and reject terrain
      acquisition when the declared transform asset cannot be prepared.
- [ ] Validate the precise Slovak source pipeline against supplied reference data.
- [ ] Test offline operation, missing/corrupt databases, missing/corrupt grids,
      cache relocation and an application started outside its build directory.
- [ ] Record source/grid licences and required acknowledgements.
- [ ] Decide whether official TSRE builds require PROJ or offer it as an optional
      elevation capability.

## Acceptance criteria for a later implementation

- No environment-variable setup is needed on an installed TSRE build.
- Projection initialization reports the exact selected operation and expected
  accuracy for diagnostics without exposing secrets or excessive normal logs.
- Required-grid absence fails before elevation downloads and names the missing
  resource; no silent metre-scale fallback is accepted.
- A normal 256 x 256 terrain grid has no material projection bottleneck.
- Parallel terrain work uses independent PROJ contexts safely.
- Existing working elevation sources and route projections retain their current
  results unless a separately reviewed migration changes them.
- Horizontal and vertical transformation policies remain independently declared.
- Only required transformation grids are shipped or downloaded, with attribution.
- Release packaging and clean-machine startup are documented and repeatable.

## Decision gate

Resume this task before adding the first file source that requires an accurate
unsupported national datum/grid, or before implementing common vertical-datum
normalization. Until then, continue COG overview/range work and sources that fit
the current projection layer. If the build/package probe is unacceptable, record
the measured reason before returning to a narrow local implementation.

## Primary references

- PROJ overview and MIT licence: https://proj.org/en/stable/about.html
- Build requirements and resource embedding:
  https://proj.org/en/stable/install.html
- CMake integration: https://proj.org/en/stable/development/cmake.html
- C API and batch/thread contexts:
  https://proj.org/en/stable/development/reference/functions.html
- Resource paths, database and grids:
  https://proj.org/en/stable/resource_files.html
- Network behavior and cache: https://proj.org/en/stable/usage/network.html
- PROJ-data grids and licence policy: https://github.com/OSGeo/proj-data
- Ordnance Survey transformation resources:
  https://www.ordnancesurvey.co.uk/geodesy-positioning/coordinate-transformations/resources
