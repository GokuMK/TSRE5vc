# Geo-terrain: initial design review

Reviewed 2026-09-16 against main commit `0be6601` and
[the original ideas](tsre_geoportal_generic_elevation_ideas.md).
Design only; no implementation or build changes.

## Conclusion

Keep the dataset-oriented source/provider split. Geoportal should fetch raster
areas and share local sampling and coordinate conversion with future sources.
The main adjustment is to separate area preparation from point sampling:
network access must not be hidden inside the terrain vertex loop.

Agreed first milestone: preserve HGT through
an adapter, add the shared raster pipeline and one verified Geoportal NMT
dataset, and validate the abstraction against other services without promising
their implementation in the same milestone.

## What the current code actually does

- `src/tsre/geo/HeightWindow.cpp`: `load()` discovers integer-degree cells and
  constructs `GeoHgtFile` directly. `drawTile()` converts terrain samples to
  geographic coordinates and looks up those files in a static map. Provider
  selection therefore requires changes above the individual file class.
- `GeoTerrainFile.h`: the existing virtual interface includes degree-based
  `load(int lat, int lon)`, preview drawing and float coordinate arguments.
  It is a file abstraction, not yet a suitable area preparation contract.
- `GeoHgtFile.cpp`: reads raw signed heights and interpolates them. The sampler
  scales coordinates by `rowSize`, clamps edges, and has no explicit NoData
  result. Characterize this behavior before deciding whether HGT migration
  should also correct sampling; silently changing existing routes is undesirable.
- `GeoTiffFile.cpp`: not a working decoder. `load()` returns false after opening
  the file, and `getHeight()` immediately returns zero.
- `TerrainLibQt.cpp`, `setHeightFromGeoGui()` and `setHeightFromGeo()`: use the
  actual terrain sample count and sample size, then copy the prepared heights
  into terrain and refresh it. Do not assume a fixed 256-square output or
  fixed 8 m spacing. Preserve existing orientation/transposition conventions.
- `GeoTools.cpp`: missing-data checks call the HGT-specific
  `CheckForMissingGeodataFiles()`. Batch preflight must also use the selected
  source. `TerrainLibSimple.cpp` has additional HeightWindow callers; account
  for these without absorbing the separate simple-backend migration task.
- `CMakeLists.txt` has Qt Network but no GDAL/PROJ dependency. The Windows release
  workflow uses Qt's matching MinGW toolchain, so GIS library ABI compatibility
  and runtime packaging require an explicit evaluation.

## Proposed contracts and responsibilities

1. **Generation job:** captures selected dataset, target sample positions,
   sampling policy and Y offset. Prepares a temporary result, reports progress,
   supports cancellation, and applies validated heights through the existing
   terrain editing path. A failed download must not leave a half-written tile.
2. **Elevation source:** prepares coverage for the requested area, then provides
   local height lookup with double latitude/longitude inputs. A sample carries
   validity/status as well as height; zero is a valid height, not an error.
3. **Raster source:** locates prepared blocks, converts coordinates and samples
   them. No implicit network work in the sampling loop. A bulk sampling method
   may optimize transforms without changing the conceptual point interface.
4. **Provider:** determines assets or WCS requests, retrieves them and describes
   their dataset identity. Protocol behavior belongs here; terrain filling does
   not depend on WCS versions, URL layouts or archive contents.
5. **Raster decoder/CRS support:** produces a normalized raster view. Prefer a
   Qt-based implementation with a documented GeoTIFF subset matched to actual
   service output. Keep coordinate conversion in a shared component, separate
   from Geoportal. An owned float array is fine for bounded blocks; do not require
   whole-country datasets or every cached block to reside in memory.

Keep HGT adaptation and raster/service integration separate enough that existing
HGT behavior can be checked independently. Do not force HGT through a new decoder
merely to satisfy a class hierarchy.

## Correctness decisions missing from the ideas

- **Coordinates:** use double precision through geographic/projected conversion.
  Define latitude/longitude at the source boundary and x/y at the raster boundary;
  handle WCS axis order explicitly. The existing MSTS-to-geographic converter
  remains responsible for route coordinates.
- **Grid geometry:** include a full affine transform, row direction and explicit
  sample registration. Distinguish raster pixel centers from corners and HGT's
  shared boundary posts. Use a validity mask or equivalent representation,
  including NaN and declared NoData handling. GDAL documents the six-coefficient
  transform and half-pixel center convention in its
  [geotransform tutorial](https://gdal.org/en/stable/tutorials/geotransforms_tut.html).
- **Vertical reference:** horizontal EPSG:2180 does not establish how heights
  relate to the route or another dataset. Store height units and vertical datum.
  Geoportal lists PL-KRON86-NH and PL-EVRF2007-NH products on its
  [NMT page](https://www.geoportal.gov.pl/pl/dane/numeryczny-model-terenu-nmt/).
  Do not silently mix them or treat the existing Y offset as a datum conversion.
  First-version datum policy and any conversion grids still need a decision.
- **Sampling:** bilinear point interpolation and area averaging have different
  results when reducing 1 m terrain to a coarser target grid. Choose explicitly;
  proposed initial behavior is bilinear vertex sampling, with smoothing left as
  a separate decision. Preserve narrow earthworks as a visual acceptance case.
- **Missing data:** distinguish outside coverage, NoData, failed download,
  malformed raster and transform failure. Agreed behavior for missing coverage
  and NoData is HGT fallback with a visible report. Record affected samples/areas,
  fallback counts and source identities, and address potential datum differences.
  Proposed behavior if neither source provides a valid height is to stop the
  affected tile without applying it. Whether transport errors also trigger HGT
  fallback remains a design detail; never silently treat malformed data as valid.
- **Edges:** prepare the actual target footprint plus interpolation support.
  Do not rely only on a tile center or on transformed rectangle corners for
  nonlinear projection bounds. Neighboring tiles must sample consistent source
  data at the same coordinates, including different terrain resolutions.

## Cache and service configuration

Prefer a deterministic grid in the dataset CRS, with bounds snapped to its
sample lattice and a sampling margin, over arbitrary floating-point request
bounds. Block dimensions remain provisional until service limits are measured.
Provider-native tiles remain an alternative for file-based sources.

Cache keys include dataset/version, CRS, source grid/resolution, block index,
format and relevant request parameters. Record retrieval metadata and distinguish
source files from any derived rasters. Download to temporary files and publish
only validated results; deduplicate in-flight requests. Bound memory residency
separately from disk storage. Reopening a prepared area should work offline;
refresh should be explicit enough to avoid mixing revisions within one job.

Use a small versioned declarative dataset schema. In addition to the proposed
fields, represent WCS version/axis identifiers, vertical reference, units,
NoData interpretation and dataset revision where known. Do not add a scripting
language or promise all WCS implementations will work from configuration alone.
JSON is a reasonable initial fit with existing settings infrastructure; the
serialization format is secondary to validating the actual service contracts.

## External checks and their limits

- The official [Geoportal WCS directory](https://www.geoportal.gov.pl/pl/usluga/uslugi-pobierania-wcs/)
  lists distinct NMT services for GeoTIFF and Arc/Info ASCII Grid. This confirms
  the access direction, not every proposed product/resolution combination.
- [GDAL's GeoTIFF driver](https://gdal.org/en/stable/drivers/raster/gtiff.html)
  supports numeric raster bands and georeferencing. This makes it a strong
  reference/alternative, but the user's preference is to avoid adding a GIS stack
  if a Qt-based solution is practical. No build or deployment measurement was done.
- [Qt Image Formats](https://doc.qt.io/qt-6/qtimageformats-index.html) lists TIFF
  support through an optional plugin backed by a third-party codec. This alone
  does not establish that QImageReader preserves the required numeric heights
  and GeoTIFF metadata. Verify against the project's Qt version and sample data;
  alternatively use QFile/QDataStream and a bounded numeric TIFF/tag decoder.
  Specify sample types, compression/predictors, strips/tiles, endianness,
  georeferencing and NoData. Reject unsupported encodings explicitly. ASCII Grid
  remains a possible simpler transport if service metadata makes it suitable.
- Finland's [official WCS technical description](https://www.maanmittauslaitos.fi/ortokuvien-ja-korkeusmallien-kyselypalvelu/tekninen-kuvaus)
  provides a WCS 2.0.1 elevation request with named E/N subsets and GeoTIFF output.
  It is a useful second WCS design case; authentication and limits need review.
- [swissALTI3D](https://www.swisstopo.admin.ch/en/height-model-swissalti3d)
  offers COG terrain tiles. This supports retaining an asset/file provider path
  separate from WCS. Asset discovery has not been investigated here.

The Geoportal capabilities link could not be retrieved through the web reader
in this review. Exact coverage IDs, WCS versions, axis order, response CRS,
vertical datum, limits and advertised resolution remain unverified. No sample
coverage was downloaded and no live terrain generation was attempted.

## Next design work before implementation

1. Use the accepted scope, Qt preference and HGT fallback policy recorded below.
2. Retrieve and retain Geoportal capabilities/coverage descriptions; make one
   small sample request and inspect its numeric values, grid and metadata.
3. Specify the smallest Qt-based raster reader that covers the verified response.
   Independently assess coordinate conversion: server-provided output CRS where
   supported versus a shared implementation of the required projections. Check
   accuracy against independent reference points. Do not claim arbitrary EPSG
   support from dataset configuration alone. Revisit external dependencies only
   if concrete format/projection requirements justify them.
4. Specify source selection/persistence, progress/cancel behavior, cache location
   and bounds, height-datum policy, and interaction with terrain undo/batch edits.
5. Write an implementation plan and acceptance cases: known control points,
   pixel/post registration, NoData, negative/zero heights, adjacent blocks/tiles,
   mixed output resolutions, offline reuse, cancellation, failed responses,
   bounded memory and HGT regression behavior. Network-independent fixtures
   should cover normal verification; live service checks should be separate.

## Accepted user decisions

- Geoportal first; validate the shared design against other services.
- Prefer pure Qt where practical. A focused GeoTIFF implementation is acceptable;
  GDAL/PROJ are not the assumed dependency choice.
- Use HGT fallback for missing coverage/NoData and show a visible report.

Other proposals in this review remain design recommendations. No additional
user answer is required to continue service/format investigation. The original
ideas file is preserved unchanged beside this review.
