# User-managed elevation sources: directory layout and next provider

Recorded 2026-09-19; updated 2026-09-23. Catalogue-defined file providers now
cover one-degree HGT plus downloaded GeoTIFF profiles: Austria projected range
COG tiles, Luxembourg, Wales, Slovakia and Wallonia national range COGs, and
Switzerland STAC-discovered 2 m tiles. Sweden adds authenticated root-STAC range
access to 1 m COG assets. Portugal is the first user-managed indexed GeoTIFF
directory. GEDTM30 is the first geographic global range COG.

## Directory ownership

```text
geoPath/
  world_hgt/               N52E019.hgt and/or N52E019.hgt.gz
  world_gedtm30/           GEDTM30 COG index tables and required block parts
  at_bev_als_dgm1/         optional official TIFFs and downloaded COG block parts
  sk_gku_dmr5_etrs89h/     Slovakia national COG index and required block parts
  be_wallonia_spw_mnt1/    Wallonia national COG index and required block parts
  lu_act_dtm2024/           Luxembourg national COG index and 1 m block parts
  gb_wales_lidar_dtm1/      Wales national COG index and 1 m block parts
  ch_swissalti3d_2m/       preserved official 2 m source TIFFs
  se_lantmateriet_markhojdmodell1/  Swedish COG indexes and required block parts
  pt_dgt_mdt2m/            user-downloaded DGT MDT-2m GeoTIFF tiles and index
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
are HGT/`degree` and GeoTIFF with `projected`, `cog`, `stac` or `directory`
file discovery.

The current catalogue fields are:

- Direct relative `directory` under `geoPath`; parser validation rejects absolute
  paths and traversal.
- `format` and `fileGrid`, selecting the validated HGT, projected COG, single
  COG, STAC GeoTIFF or indexed local-directory profile.
- CRS, coverage bounds, vertical-datum description, zero/NoData behavior,
  attribution and concurrency.
- Optional `download` object. Its absence makes a source manual-only. World HGT
  uses an HTTPS URL template with `{latitudeBand}` and `{tile}`, plus gzip.
- A geographic single COG declares `download.overviewFactor` because its source
  pixels are angular while the catalogue's displayed `resolution` is in metres.
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

HGT posts are sampled over the correct `side - 1` intervals of their one-degree
cell. The file provider retains signed 16-bit source bytes rather than expanding
every 3601 x 3601 cell into a full Float32 raster. A thread-safe 128 MiB process
cache reuses recently decompressed cells across automatic generation of adjacent
terrain tiles. This improves speed and corrects the former within-cell shift;
the source's original integer height quantization and heterogeneous 30–90 m
detail remain inherent limitations.

Preparation deduplicates geographic cells before checking the filesystem, and
sampling keeps a numeric last-cell fast path. A 2048 x 2048 probe produced all
4,194,304 samples from a cached `.hgt.gz` in about 0.36 seconds, including decompression;
before this correction the same path spent tens of seconds repeating the same
file lookup for every output point.

An audit of the other file modes found no per-sample filesystem or network
access. Range COG modes load required compressed blocks during preparation;
single/projected COG preparation now retains only the requested min/max bounds
instead of a second copy of every projected terrain point. STAC and directory
modes sample their prepared in-memory mosaics. A cached 2048 x 2048 GEDTM30
probe took about 0.66 seconds; a cached Austria probe spanning 25 internal COG
blocks took about 3.32 seconds, including per-point EPSG:3035 conversion.

Generation prepares the primary source first, samples it once, and passes only
unresolved points to the fallback's `prepare()`. This prevents automatic World
HGT downloads for regions already covered by the selected WCS/ArcGIS source.
Sampling itself remains local and never performs HTTP.

## GEDTM30 global bare-earth range COG

`world.gedtm30` uses the published GEDTM30 v20250619 filtered global COG through
the same `fileGrid: "cog"` provider as Luxembourg and Wales. Its catalogue bounds
match the actual COG, approximately 57 degrees south to 84 degrees north. World
HGT remains the configured fallback outside that coverage and for source voids.

The source is EPSG:4326 with EGM2008 orthometric heights. Its base image has
0.00025-degree pixels, signed Int32 decimetres, Deflate compression, horizontal
integer predictor and raw NoData `2147483647`. Generic COG support now reads GDAL
`SCALE`/`OFFSET` metadata and converts valid values to metres before sampling.
`download.overviewFactor: 1` selects the native image without confusing angular
pixel spacing with the catalogue's nominal 30 m resolution.

The complete object is about 261 GB. TSRE stores only its 256 KiB header, external
offset/count tables and intersecting compressed 2048 x 2048 blocks under
`world_gedtm30/<official-name>.tif.parts/v20250619/`. The per-block range limit is
8 MiB because verified GEDTM30 blocks can exceed the former 4 MiB limit. Two
ranges may run concurrently.

Validation on 2026-09-22: the focused Release suite passes **387 checks**,
including a generated offline geographic COG that verifies predictor, scale and
raw NoData behavior. A bounded live probe at `(50.05, 19.05)` downloaded one
terrain block and returned 241.95 m and 242.01 m. The repeat used one cached block,
made no terrain download and completed in 103 ms. The incremental Release
application build passed; the main UI suites were not run for this milestone.

## Portugal user-managed GeoTIFF directory

Portugal DGT MDT-2m is the first `fileGrid: "directory"` source. DGT's portal
currently requires an account and browser login to download raster assets, so
TSRE does not automate that site-specific Keycloak form session. The user
downloads MDT-2m GeoTIFF tiles and copies them anywhere below
`geoPath/pt_dgt_mdt2m/`; filenames and subdirectory layout are irrelevant.

On first use, TSRE reads each TIFF's embedded EPSG:3763 georeferencing and writes
`.tsre-elevation-index.json` beside the files. The index records relative path,
size, modification time, dimensions, pixel transform and bounds. Later requests
enumerate filenames and file metadata, reuse unchanged index records, and fully
decode only files whose indexed bounds overlap the requested terrain. New,
changed and removed files update the index automatically. It is disposable and
can be deleted to force a complete rebuild; source TIFFs are never modified.

This initial profile accepts north-up 2 m GeoTIFFs in the existing complete-file
decoder and retains its 32 MiB per-file guard. It validates EPSG and spacing
against the catalogue entry before indexing. The shared mosaic logic joins
adjacent files for seam-safe interpolation. EPSG:3763 uses the official
PT-TM06/ETRS89 GRS80 Transverse Mercator parameters; no datum-shift grid is
required. Cascais orthometric heights are passed through unchanged.

The Height window now shows available catalogue information when a source is
selected: attribution, licence, resolution, vertical datum, explanatory text,
official information/download links and, for directory sources, the local path.
Existing entries can add these fields incrementally when they are revalidated.
The focused Release suite passes **382 checks**, and the incremental Release
application build passes with this source and UI integration.

## Austria and Switzerland milestone

Austria BEV ALS-DGM uses fixed 50 km EPSG:3035 files from the dated 2025 mosaic.
Each official file is about 6.8 GB, so TSRE reads the BigTIFF index first and
requests only the LZW-compressed 512 x 512 blocks intersecting the terrain points.
Four block ranges may run concurrently. A complete official TIFF with the expected
name can be supplied locally and is window-read directly. Downloaded block parts
live under the readable product/file path and an explicit `20250915` revision,
preventing a later catalogue URL revision from silently reusing old blocks.

Switzerland and Liechtenstein swissALTI3D is discovered through the official STAC collection. TSRE
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

## Sweden authenticated STAC/range milestone

Lantmäteriet splits its national Markhöjdmodell catalogue across regional STAC
collections. The generic STAC source can therefore search the root endpoint,
read resolution from a configured item property and distinguish the published
compound EPSG:5845 asset metadata from its EPSG:3006 horizontal grid.

The catalogue remains anonymous. Asset index, table and raster-block range
requests use Basic username/password references resolved from the secret profile.
Authenticated redirects are restricted to the same origin, and credentials are
never stored in a URL, cache file or dataset definition. Each asset cache uses a
revision hash derived from its public URL and STAC publication time.

The authorized Stockholm probe verified a Float32 Deflate COG with 512 x 512
blocks and TIFF horizontal predictor 2. One compressed block supplied two primary
samples; the repeat used only the cached block. No Sweden-specific sampling or
terrain-generation branch was added.

## Luxembourg and Wales national COG milestone

`fileGrid: "cog"` takes one direct HTTPS COG URL and a revision. The generic
reader follows its overview directories, requires an image at the configured
resolution, range-downloads external tile offset/count tables, then retrieves
only the compressed blocks intersecting requested points. Cached paths include
the selected overview level.

Luxembourg selects the exact 1 m overview from the published 0.5 m national COG.
It adds verified Float64 LZW decoding and EPSG:2169 through ACT's published
LUREF2020 and Luxembourg TM parameters. No auxiliary datum file is needed.

Wales uses the native 1 m image in the official Float32 Deflate national COG.
Its catalogue entry declares an `ostn15-lite` coordinate-transform asset. If the
grid is absent, TSRE downloads the official Ordnance Survey developer ZIP through
the bounded Qt transport, extracts only `OSTN15_OSGM15_Lite_DataFile.txt`, and
stores it at `assets/geo/OSTN15_OSGM15_Lite_DataFile.txt`. The internal transform
projects ETRS89 to the National Grid pseudo-grid and bilinearly applies the 20 km
OSTN15 shifts. OS reports 0.08 m horizontal RMS against full OSTN15.

Validation on 2026-09-21: **378 focused Release checks passed**. Luxembourg and
Wales bounded live probes produced valid source heights; both cache repeats used
zero downloads. The user then confirmed Wales in the application. Exact byte
counts are recorded in `elevation-current-status.md`.

## Slovakia and Wallonia national COG milestone

Both large official bulk rasters were inspected with bounded HTTP ranges and
converted server-side to Float32 national COGs with 1024 x 1024 Deflate blocks
and eight internal overviews. The existing `fileGrid: "cog"` reader downloads
only the leading index, required offset/count tables and intersecting compressed
blocks.

Slovakia adds EPSG:3046 by reusing the GRS80 UTM-zone-34 transform. Its source
heights are ETRS89 ellipsoidal and remain unconverted. Wallonia adds EPSG:3812
through the shared Lambert Conformal Conic 2SP implementation; its source uses
DNG/EPSG:5710 orthometric heights. Small live probes for both sources downloaded
one block each and repeated entirely from cache. No country-specific acquisition
or sampling code was added.

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
2. **Georeferenced file collection.** Implemented as `fileGrid: "directory"`.
   GeoTIFFs may have arbitrary names and overlapping extents. Metadata is read
   into a rebuildable spatial index and files are selected by intersection with
   the terrain tile. Path/size/modification time invalidate changed files. The
   user can force a complete refresh by deleting the disposable index.

Use the simplest scheme matching the first actual downloadable product. Do not
invent a general filename-template language or require a regular grid for every
source. Define overlap precedence explicitly (product/version/resolution), rather
than allowing filesystem enumeration order to determine heights.

## Next implementation milestone

- Exercise GEDTM30 through the main terrain UI and with a representative distant
  terrain tile before marking it validated for distant-terrain generation.
- Add another product only after checking its actual compression, predictor,
  BigTIFF/IFD metadata placement, scale/offset, NoData and update identity. The
  present reader is intentionally narrower than a general TIFF library.
- Validate the indexed-directory profile with real DGT downloads, including
  adjacent files, a missing tile and a changed/replaced file.
- Add explicit distant-terrain eligibility and cache identity before enabling
  detailed national sources for 32 km distant terrain.

See [global source review](global-sources-review.md) and the separate Europe source
tracker for candidate order. A small static TIFF dependency remains an option if
additional products require substantially more codec/profile work.
