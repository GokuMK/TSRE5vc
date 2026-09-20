# User-managed elevation sources: directory layout and next provider

Recorded 2026-09-19; updated 2026-09-21. Catalogue-defined file providers now
cover one-degree HGT plus the first two downloaded GeoTIFF profiles: Austria
projected range-COG tiles and Switzerland STAC-discovered 2 m tiles.

## Directory ownership

```text
geoPath/
  world_hgt/               N52E019.hgt and/or N52E019.hgt.gz
  at_bev_als_dgm1/         optional official TIFFs and downloaded COG block parts
  ch_swissalti3d_2m/       preserved official 2 m source TIFFs
  <stable-product-name>/   later downloaded originals, managed by the user
  cache/
    <dataset-id>/<hash>/   disposable service downloads, managed by TSRE
```

Use a stable, readable directory per local product. Native source files must not
live in hashed request-cache directories. Keep them unchanged; generated indexes,
overviews and derived rasters must be clearly disposable and separate from each
original. Service blocks use `cache/`; Austria range pieces use a sibling
`<official-name>.tif.parts/<revision>/` because they reconstruct portions of that
specific official file. Changing request settings or clearing disposable data
must never remove the user's downloaded source files.

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
same `Elevation::Source` samples used by WCS and ArcGIS. Supported combinations
are HGT/`degree` and GeoTIFF with `projected` or `stac` file discovery.

The current catalogue fields are:

- Direct relative `directory` under `geoPath`; parser validation rejects absolute
  paths and traversal.
- `format` and `fileGrid`, selecting the validated HGT, projected COG, or STAC
  GeoTIFF profile.
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

## Austria and Switzerland milestone

Austria BEV ALS-DGM uses fixed 50 km EPSG:3035 files from the dated 2025 mosaic.
Each official file is about 6.8 GB, so TSRE reads the BigTIFF index first and
requests only the LZW-compressed 512 x 512 blocks intersecting the terrain points.
Four block ranges may run concurrently. A complete official TIFF with the expected
name can be supplied locally and is window-read directly. Downloaded block parts
live under the readable product/file path and an explicit `20250915` revision,
preventing a later catalogue URL revision from silently reusing old blocks.

Switzerland swissALTI3D is discovered through the official STAC collection. TSRE
selects EPSG:2056 assets whose declared resolution is 2 m, chooses the newest item
for each repeated footprint, and preserves the complete roughly 1 MB TIFFs under
`ch_swissalti3d_2m`. The 0.5 m assets are deliberately skipped. Local EPSG:2056
conversion uses the official swisstopo approximation. Discovery includes a small
pixel margin, and adjacent files are assembled into one aligned sampling raster so
bilinear interpolation crosses 1 km file boundaries without HGT seam lines.

Both profiles reuse the common `Raster`, LZW decoder, NoData/fallback path and
sampling code. No Austria- or Switzerland-specific terrain-generation branch was
added. The only product knowledge is catalogue configuration plus the generic
projected-template and STAC discovery schemes.

Validation on 2026-09-21: **371 standalone checks passed**. A Vienna live probe
downloaded a 256 KiB index and one 586,993-byte internal block, returned about
171.6 m and repeated from cache. A Bern probe downloaded one 975,801-byte current
2 m TIFF, returned about 540.3 m and repeated from cache. A later exact 1 km
boundary probe used both adjacent tiles for two primary samples with no fallback.

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

- Exercise Austria and Switzerland through the main terrain UI, including a tile
  boundary and a coverage edge; the current live probes cover one interior asset.
- Add another product only after checking its actual compression, predictor,
  BigTIFF/IFD metadata placement, scale/offset, NoData and update identity. The
  present reader is intentionally narrower than a general TIFF library.
- For user-downloaded arbitrary file collections, add a rebuildable spatial index
  instead of scanning or loading every file. Preserve originals.
- Implement explicit overview/coarse selection and separate cache identity before
  enabling these detailed sources for 32 km distant terrain.

See [global source review](global-sources-review.md) and the separate Europe source
tracker for candidate order. A small static TIFF dependency remains an option if
additional products require substantially more codec/profile work.
