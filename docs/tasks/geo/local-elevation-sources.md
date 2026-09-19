# User-managed elevation sources: directory layout and next provider

Recorded 2026-09-19. Directory lookup preparation is implemented; the generic
local-raster provider and catalogue schema below are **proposed**, not implemented.

## Directory ownership

```text
geoPath/
  world_hgt/               user-managed N52E019.hgt etc.
  <stable-product-name>/   future downloaded originals, managed by the user
  cache/
    <dataset-id>/<hash>/   disposable service downloads, managed by TSRE
```

Use a stable, readable directory per local product. Native source files must not
live in hashed request-cache directories. Keep them unchanged; generated indexes,
overviews and derived rasters belong under `cache/`, separate from originals.
Changing request settings or clearing disposable caches must never remove the
user's downloaded source files.

HGT lookup now prefers `world_hgt/`, then legacy `hgt/`, then the geodata root.
The elevation provider, missing-file checker and legacy `GeoHgtFile` use the same
lookup. Existing HGT selections remain compatible; `geoPath` still points to the
parent directory, not directly to `world_hgt`. Runtime lookup does not relocate
files automatically. One-time reorganization of the user's files is separate.

For this workspace, 114 HGT files were moved from `C:/hgst` to
`C:/hgst/world_hgt` after the successful Release build. No destination files
were overwritten; filenames and byte lengths were verified. The migration
manifest is in ignored `build-ee-gb-research/hgt-migration.json`.

## What a local dataset needs

Keep provider identity separate from country. A future `local-raster` provider
should discover/read files and return the same `ElevationRaster` used by WCS and
ArcGIS. Shared coordinate conversion, height sampling, NoData policy and reports
remain outside the file-discovery code.

Catalogue fields should describe:

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

## Suggested first implementation milestone

- Choose one concrete downloadable product and inspect representative originals,
  including adjacent files and NoData/coverage edges.
- Finalize its minimal local catalogue entry, readable directory and index type.
- Add the generic local-raster provider and metadata inventory, without converting
  or renaming the user's originals into HTTP cache blocks.
- Read only intersecting windows from large files, with bounded memory and
  cancellation. Current TIFF decoding reads whole uncompressed rasters and is
  insufficient for arbitrary large/compressed/BigTIFF downloads. Assess that
  separately; if a small statically included library is substantially better,
  propose it before adding the dependency.
- Verify neighbouring-file seams, overlap precedence, missing files, index
  refresh, valid zeros/negative heights and raw-file preservation.
- Plan coarse/windowed access for distant terrain from the start. Do not solve
  32 km acquisition by loading entire high-resolution source collections.

No first downloadable product has been selected yet. The next country/product
example should drive the actual schema and decoder requirements.
