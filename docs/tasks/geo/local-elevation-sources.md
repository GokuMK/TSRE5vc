# User-managed elevation sources: directory layout and next provider

Recorded 2026-09-19. The first catalogue-defined file provider is implemented
for one-degree HGT files. The next provider should extend this contract for
downloaded GeoTIFF products rather than reintroducing format-specific terrain logic.

## Directory ownership

```text
geoPath/
  world_hgt/               N52E019.hgt and/or N52E019.hgt.gz
  <stable-product-name>/   future downloaded originals, managed by the user
  cache/
    <dataset-id>/<hash>/   disposable service downloads, managed by TSRE
```

Use a stable, readable directory per local product. Native source files must not
live in hashed request-cache directories. Keep them unchanged; generated indexes,
overviews and derived rasters belong under `cache/`, separate from originals.
Changing request settings or clearing disposable caches must never remove the
user's downloaded source files.

HGT lookup now uses only the catalogue entry's direct `directory` field,
currently `world_hgt`. There is no special search of legacy `hgt/` or the
geodata root; users move old files explicitly. `geoPath` points to the parent
directory. Uncompressed user files take precedence over compressed downloads.
The elevation source, missing-file checker and legacy `GeoHgtFile` use the same
catalogue directory and support both forms.

For this workspace, 114 HGT files were moved from `C:/hgst` to
`C:/hgst/world_hgt` after the successful Release build. No destination files
were overwritten; filenames and byte lengths were verified. The migration
manifest is in ignored `build-ee-gb-research/hgt-migration.json`.

## Implemented file-source contract

Provider identity remains separate from country. `provider: "file"` returns the
same `Elevation::Source` samples used by WCS and ArcGIS. Its first supported
combination is `format: "hgt"` plus `fileGrid: "degree"`.

The current catalogue fields are:

- Direct relative `directory` under `geoPath`; parser validation rejects absolute
  paths and traversal.
- `format` and `fileGrid`; currently only HGT and one-degree hemisphere naming.
- CRS, coverage bounds, vertical-datum description, zero/NoData behavior,
  attribution and concurrency.
- Optional `download` object. Its absence makes a source manual-only. World HGT
  uses an HTTPS URL template with `{latitudeBand}` and `{tile}`, plus gzip.
- Catalogue-level `defaultFileSource`. If omitted or invalid, the first valid file
  source is used. The default is both the replacement for the former empty HGT
  selection and the current fallback for service sources.

World HGT has exact global bounds `[-180,-90,180,90]`, so it remains visible for
every valid route location. HGT dimensions determine sample spacing at runtime;
the file source does not pretend that all user files have one fixed resolution.
Zero is valid and `-32768` remains NoData.

Mapzen/Tilezen Skadi supplies missing tiles automatically from the public AWS
Open Data bucket. Downloads remain compressed as `.hgt.gz`; bounded decompression,
HGT dimensions, gzip CRC and ISIZE are validated before an atomic write. A JSON
sidecar records dataset ID, public URL, retrieval time, datum, attribution and
SHA-256. Existing files are never replaced. Four downloads may run concurrently.

Generation prepares the primary source first, samples it once, and passes only
unresolved points to the fallback's `prepare()`. This prevents automatic World
HGT downloads for regions already covered by the selected WCS/ArcGIS source.
Sampling itself remains local and never performs HTTP.

The old empty setting value migrates to the catalogue default. Source lists no
longer add a hardcoded HGT item, and reports use generic source/fallback wording.

Validation on 2026-09-19: **359 standalone checks passed**. A live point probe at
`(50.05,19.05)` downloaded one `N50E019.hgt.gz` file (5,447,758 bytes), stored its
metadata sidecar and returned two source heights of 241 m. Repeating used that
compressed file with zero downloads. The Release build,
246 Settings checks and 61 elevation-UI checks also passed.

## What later local datasets need

Later catalogue entries should also describe:

- Stable dataset ID, display name, attribution and product version/date.
- Relative directory under `geoPath`, with no traversal outside that root.
- File format/decoder, e.g. HGT or numeric GeoTIFF; supported compression and
  sample types must be established against real files before enabling a product.
- Horizontal CRS, vertical datum, coverage bounds and nominal source spacing.
  Prefer the file's georeferencing; configuration must not override conflicting
  embedded metadata silently. Preserve valid zero/negative heights.
- File indexing scheme: how the required geographic area maps to file paths.
  This is distinct from the pixel grid inside each raster.
- NoData/fallback preference and, separately, distant-terrain eligibility.

Two file-indexing schemes should cover the initial work:

1. **Regular named tiles.** HGT uses one-degree latitude/longitude cells with
   hemisphere-based names; projected products may use fixed kilometre cells.
   Describe the naming convention, grid CRS, origin, tile spans and axis order.
   Do not assume the raster dimensions, halo or pixel registration from names.
2. **Georeferenced file collection.** GeoTIFFs may have arbitrary names and
   overlapping extents. Read metadata once into a rebuildable spatial index;
   select files by intersection with the terrain tile. Invalidate changed files
   by path/size/modification time and offer an explicit refresh. Avoid scanning
   every large file on each height lookup.

Use the simplest scheme matching the first actual downloadable product. Do not
invent a general filename-template language or require a regular grid for every
source. Define overlap precedence explicitly (product/version/resolution), rather
than allowing filesystem enumeration order to determine heights.

## Next implementation milestone

- Choose one concrete degree-grid GeoTIFF product and inspect representative
  originals, including adjacent files and NoData/coverage edges.
- Extend the existing file provider with the minimum new format/index fields;
  keep download optional and keep originals outside hashed HTTP caches.
- Read only intersecting windows from large files, with bounded memory and
  cancellation. Current TIFF decoding reads whole uncompressed rasters and is
  insufficient for arbitrary large/compressed/BigTIFF downloads. Assess that
  separately; if a small statically included library is substantially better,
  propose it before adding the dependency.
- Verify neighbouring-file seams, overlap precedence, missing files, index
  refresh, valid zeros/negative heights and raw-file preservation.
- Plan coarse/windowed access for distant terrain from the start. Do not solve
  32 km acquisition by loading entire high-resolution source collections.

Do not jump directly to advanced global COG products until their required range
requests, compression, sample types and scale/offset behavior are implemented and
bounded. See [global source review](global-sources-review.md) for the staged order.
