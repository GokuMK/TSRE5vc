# OSM data from local PBF files: performance study

Design stage, branch `feature/osm-data`, measured 2026-10-07. No application
code was changed. Benchmarks, raw output and a build script are in
[evidence/2026-10-07-osm](evidence/2026-10-07-osm/).

## Question

TSRE draws the per-tile OSM map (`MapWindow` + `MapDataOSM`) from four
`api/0.6/map` HTTP requests per terrain tile. We want much larger areas and
procedural generation from OSM vectors, with the user pointing TSRE at a
directory of downloaded `.osm.pbf` files (example: `/home/arch/OSM`, Geofabrik
Poland 2.1 GB plus 16 voivodeship files 55–202 MB).

Can TSRE use the PBF files directly, with an in-memory cache? How long does the
first use take, and how fast is a tile map afterwards?

## Answer in short

- **Do not query PBF files per tile.** Geofabrik files are ordered by entity
  type and ID, not by location (`Sort.Type_then_ID`). A single 2 km tile touches
  42–64 % of all data blocks, so every spatial query has to decode the whole
  file. That takes 0.44 s for a voivodeship file and 6.5 s for Poland on 12
  threads, and a query with complete ways needs 2–3 such passes.
- **Use the PBF file as an import source.** Read it once into a TSRE spatial
  cache: a memory-mapped multi-level lat/lon grid. The OS page cache then
  serves as the in-memory cache at no extra cost.
- **First use (import)**, measured on this machine:
  - Voivodeship file: **0.7–1 s** with a parallel importer (3.7 s with a plain
    single-threaded libosmium handler).
  - All of Poland: 13–18 s with the parallel importer, but 11–15 GB RAM in its
    current form; the plain handler takes 77 s and 10.7 GB.
  - A national file has to be imported per region, or with a disk-backed
    node index.
- **Tile map after import:** gathering a tile from the cache takes 2–40 ms. The
  current `draw()` code then needs 40–480 ms at 4096 px, so a tile costs
  **0.04–0.5 s, compared with about 2–6 s today** (network + XML + draw).
  Rendering all 870 tiles of a 60 × 60 km area takes 5.8 s on 12 threads and
  9.8 s on 4.
- After import, drawing is the dominant cost and data access is negligible.

## Machine and data

- Xeon E5-1650 v3 (6 cores / 12 threads, 3.5 GHz), 62 GB RAM, SATA SSD
  (≈480 MB/s cold read), Arch Linux, GCC 16, Qt 6.11. Software GL does not
  matter here: everything measured runs on the CPU, and the user's machines
  draw through QPainter on the CPU too.
- The Steam Deck test machine (4 cores / 8 threads, Zen 2) was not measured.
  The 4-thread rows below are the closest proxy.
- Data files:

  | File | Size | Contents |
  |---|---:|---|
  | `poland-261006.osm.pbf` | 2.1 GB | 243 M nodes, 33.7 M ways, 280 k relations; 5.15 GB raw after inflate |
  | `pomorskie-261006.osm.pbf` | 118 MB | 13.1 M nodes, 1.73 M ways, 17 k relations |

- Each file header carries a bbox and a timestamp. Reading every block header
  of the Poland file takes 0.8 s in Python, so selecting the source file
  for a route costs nothing.

## Measurements

### 1. Decoding PBF (libosmium, all entities)

| File | 12 threads | 3 decode threads (≈ 4-core machine) | Cold page cache |
|---|---:|---:|---:|
| pomorskie | 0.44 s | 0.75 s | – |
| Poland | 6.5 s (68 CPU-s) | 13.1 s | 6.5 s |

Decoding is CPU-bound, and a cold disk adds nothing: inflate and protobuf
decoding are slower than the SSD. Reading only nodes or only ways saves
little, because every block still has to be inflated.

Inflate alone, all Poland blocks (5.15 GB raw), warm page cache:

| Library | 12 threads | 1 thread |
|---|---:|---:|
| zlib | 2.1 s | 16.6 s |
| miniz (already in `src/mzip`) | 1.85 s | 15.2 s |
| libdeflate | 0.86 s | 6.8 s |

### 2. Spatial locality of PBF blocks (pomorskie)

Share of decoded node/way buffers whose bbox intersects the query. The buffers
are finer than PBF blocks, so real blocks are worse.

| Query | Node buffers | Way buffers |
|---|---:|---:|
| 2 km tile, Tczew | 42 % | 48 % |
| 2 km tile, Gdańsk centre | 58 % | 64 % |
| 2 km tile, rural (Kashubia) | 58 % | 64 % |
| 60 × 60 km route area | 82 % | 86 % |

A sidecar index of block bboxes would therefore not avoid the full scans. Only
re-sorting the data spatially, which is what the import does, makes queries
cheap.

### 3. Today's path, per 2 km tile at the default 4096 px

`MapDataOSM::loadData()` and `draw()` were compiled unchanged into the
benchmark. The input was the four API responses the current code requests for
each tile.

| Tile | API data | Network (4 parallel requests) | XML parse | Draw | Total |
|---|---:|---:|---:|---:|---:|
| Tczew | 14 MB | ≈1.4 s | 0.57 s | 0.16 s | **≈2.1 s** |
| Gdańsk centre | 44 MB | ≈3.5 s | 1.95 s | 0.31 s | **≈5.8 s** |

The network time is a single sample from the public API.

### 4. Import: PBF → grid cache

The prototype cache stores, for each feature, a TSRE type (the current
`OSMFeatures` classification), a layer, and rings as int32 lat/lon (1e-7°).
Features sit in a 5-level lat/lon grid with cells of 1/32° up to 8°, each on
the finest level where its bbox spans no more than 2 × 2 cells. Multipolygon
relations are assembled with libosmium; today's code drops all relations.
The file is used through `mmap` without any decoding step.

| Source → scope | Single-threaded libosmium handler | Parallel prototype¹ (12 / 4 threads) | Peak RAM | Cache |
|---|---:|---:|---:|---:|
| pomorskie → whole file | 3.7 s | 0.70 s / 0.95 s | 0.57 GB | 187 MB (113 MB zstd) |
| pomorskie → whole file, all tags kept | 4.2 s | – | 0.78 GB | 296 MB |
| pomorskie → 60 × 60 km route | 3.7 s | – | 0.44 GB | 57 MB |
| Poland → 60 × 60 km route | 66.6 s | ≈13 s (estimate: 2 passes) | 4.4 GB | 58 MB |
| Poland → whole file | 77 s | 12.8 s / 17.7 s | 10.7 GB (12–15 GB parallel) | 3.6 GB |

¹ Nodes are collected into a sorted (id, location) array during the decode.
Way buffers are then resolved and classified by worker threads. These figures
cover ways only: multipolygons add about 0.3 s (pomorskie) and 4.5 s (Poland)
for the relation pass, plus assembly time. The prototype holds all output in
RAM; a production importer would stream it to disk.

Observations:

- The plain single-threaded handler runs at a sixth of decode speed. The time
  goes into tag classification (string upper-casing and hash lookups, 0.6 s
  for pomorskie) and node-location lookups (≈1.3 s). Both parallelise.
- With a national file the cost is the RAM for node locations: 243 M nodes ×
  16 B = 3.9 GB before any output. A route-area import from the Poland file
  costs 18× the time of the same import from the voivodeship file, with the
  same 57–58 MB result. **Which file is used matters more than any
  optimisation.**
- Size is about 11.5 bytes per point plus 20 bytes per feature. Keeping all tags
  adds 58 %. Delta-coding coordinates per cell would roughly halve the
  size, at a small decode cost per cell.

### 5. Tile from the cache

Grid lookup, ring bbox rejection, and the current style code; cold page cache
on the first query.

| Tile (4096 px) | Features | Points | Gather, first / warm | Draw |
|---|---:|---:|---:|---:|
| Tczew | 5 083 | 66 k | 6 ms / 2 ms | 0.13–0.16 s |
| Gdańsk centre | 11 750 | 119 k | 10 ms / 4 ms | 0.27–0.28 s |
| Rural | 484 | 29 k | 2 ms / 1 ms | 0.04 s |
| Warsaw centre (Poland cache) | 17 498 | 347 k | 39 ms / 12 ms | 0.43–0.48 s |
| Gdańsk centre at 2048 px | – | – | 10 ms / 4 ms | 0.19 s |

Opening the cache is a single `mmap` (< 0.1 ms). The full index search is
included in the gather times.

Rendering a whole area (60 × 60 km, 870 tiles of 2 km):

| Resolution | 12 threads | 4 threads |
|---|---:|---:|
| 4096 px | 5.8 s (150 tiles/s) | 9.8 s (89 tiles/s) |
| 2048 px | 3.0 s (294 tiles/s) | – |

The cache render matches the current render of the same tile
([comparison](evidence/2026-10-07-osm/tczew-current-vs-cache.jpg), today's
path on the left). It also contains multipolygon areas, such as ponds and
forest relations, that today's path drops.

### 6. Hybrid variants: small index in memory, data read from a PBF

Question: a full grid cache next to the PBF more than doubles disk use. Can a
small in-memory index (feature ID → PBF location) replace it, with full data
read from the PBF on demand?

Measured with an own block reader (protozero + libdeflate). It builds the
block table (type and ID range of every block) in 0.11 s for pomorskie and
1.8 s for Poland, against 0.44 s / 6.5 s for a libosmium decode. Another
session's build kept the load average at 2–4 during these runs, so treat the
numbers as indicative.

The obstacle: a PBF way holds only node IDs. Its coordinates sit in node
blocks scattered by ID, and way blocks (about 8,000 ways each) are ordered by
ID, not by place. Any variant that leaves the PBF as it is must inflate a large
share of both kinds of block for every tile.

| Variant | Extra disk | In-memory index | Tile read (compressed) | Tile time, 12 / 4 / 1 threads |
|---|---:|---:|---:|---:|
| Grid cache (§4–5) | +187 MB (+106 MB delta-varint) | – (mmap) | 0.1–1 MB | 2–10 ms gather |
| A. ID index + original PBF, pomorskie | ≈0 | 2.2 MB IDs + 45 kB block table | 9–76 MB | Gdańsk 0.07 / 0.14 / 0.46 s |
| A. ID index + original PBF, Poland | ≈0 | 42 MB + 0.8 MB | Warsaw 379 MB | 0.32 / 0.58 s; ≈0.8 s+ when the file is not in the page cache |
| B. Spatially re-sorted PBF, pomorskie | −8 MB if it **replaces** the original | 7 kB block bbox table | 5–8 MB | 0.03 / – / 0.09–0.13 s |

Notes on A (the literal hybrid):

- The first use still needs a full import pass with all node locations in
  memory (210 MB for pomorskie, 3.9 GB for Poland). Only the stored result
  is smaller.
- Skipping unwanted ways without parsing them made way blocks 4× cheaper.
  Inflating still dominates.
- Rendering the whole 60 × 60 km area (870 tiles) costs 11 s (12 threads) or
  23 s (4 threads) in decoding alone, against effectively zero for the grid
  cache.
- An LRU of decoded blocks helps only when it holds nearly the whole file:
  1 GB gives 2.6 s for the area. A 256 MB LRU thrashes, because cached blocks
  must be fully decoded, and it was slower than having no LRU (23 s).
- The index becomes invalid whenever the user replaces the PBF with a fresh
  download, so it is keyed by header timestamp and size.

Notes on B:

- TSRE rewrites the file once into a standard PBF with locations on ways
  (`locations_on_ways`, readable by osmium/QGIS):
  - typed ways only, untagged nodes dropped;
  - sorted by grid level and then by Morton order of the 1/32° cell;
  - blocks of about 8,000 ways;
  - a block bbox table as the only index.
- The result was 110 MB against the original 118 MB, built in 4.3 s on a
  single thread.
- The prototype does not yet write relations, their untagged member ways, or
  tagged POI nodes, which will add a few percent. Smaller blocks would cut
  the 5–8 MB read per tile further.
- For Poland the conversion has the same node-location memory problem as any
  import, so it has to run per region or with a disk-backed node index.

Compact variants of the grid cache: delta + varint coordinates give 106 MB
(pomorskie) and 2.0 GB (Poland); zstd per cell on top gives 89 MB / 1.7 GB.
Both are still about the size of the PBF, because shared nodes are repeated
per way and multi-cell features are stored more than once.

### 7. Full conversion to a sorted PBF (model B), measured 2026-10-08

Model B was chosen. This section measures a complete converter, not the
typed-ways prototype of §6.

**What is kept and dropped:**
- Keeps every tagged way, every way that is a relation member, all relations
  (blocks copied verbatim), and every tagged node, with all tags.
- Way node IDs stay, so road and rail graph topology is preserved.
- Coordinates move onto the ways (`LocationsOnWays`).
- Untagged nodes and metadata are dropped.

**Pipeline:**
- Parallel own reader (protozero + libdeflate).
- Node locations held in RAM as sorted per-block arrays, 16 bytes per node.
- Ways are resolved in parallel and written as varint records into temporary
  bucket files, one per 1° cell (plus one per large-feature level), on the SSD.
- The node arrays are freed after the way pass.
- Buckets are then sorted by (kind, level, Morton cell) and encoded in
  parallel into blocks of 8,000 entities, deflate level 6. Each block's bbox
  goes into `BlobHeader.indexdata`, which other readers ignore.

**Checks:**
- libosmium reads the output.
- Every way-node location equals the original node's location: 16.8 M refs
  in pomorskie and 316 M refs in Poland, 0 mismatches.
- Tag totals are identical to the input (7,803,177 and 135,986,026).

Times for SATA SSD with btrfs zstd:1 and input not in the page cache. Other
sessions kept the load average at 2–7 (including the converter's own threads).

| Source | Threads | Total | Phases | Peak heap | Temp disk | Output |
|---|---:|---:|---|---:|---:|---:|
| pomorskie 118 MB | 12 | **1.9 s** | nodes 0.1, ways 0.4, sort+encode+write 1.0 | 0.35 GB | 0.32 GB | 128 MB |
| Poland 2.1 GB | 12 | **34 s** | block types 0.9, relations 0.3, nodes 1.8, ways 7.8, sort+encode+write 18.1 | 4.7 GB | 5.9 GB | 2.33 GB |
| Poland 2.1 GB | 4 | **59 s** | block types 1.9, relations 0.4, nodes 3.5, ways 14.6, sort+encode+write 33.6 | 4.1 GB | 5.9 GB | 2.33 GB |
| Poland, deflate level 1 | 12 | 29 s | sort+encode+write 12.8 | 4.7 GB | 5.9 GB | 2.41 GB |

- The output is 8–11 % larger than the input: coordinates on ways are
  repeated for nodes shared between ways. Replacing the original still uses
  far less disk than any cache beside it.
- Peak heap is the node-location table (243 M × 16 B = 3.9 GB) plus write
  buffers. It is freed before the sort phase, which needs about 4 × the
  largest bucket (350 MB).
- Temporary disk use is 2.8 × the input. A machine with less free space needs
  either a compressed temp format or regional files.
- Converting a voivodeship file is effectively instant (≈2 s, ≈4 s
  estimated on 4 threads).
- The block-type pass (an extra inflate of every block) can be folded into the
  node and way passes.
- The remaining write time is reading the temp files back, parsing and
  deflating. The Steam Deck (4 cores / 8 threads, NVMe) should land between the
  4- and 12-thread rows; this is not measured.

Using the converted Poland file:

| Operation | Not in page cache | In page cache |
|---|---:|---:|
| Read block table, walking headers through `mmap` | 6.8 s (readahead pulls in most of the file) | 0.02 s |
| Read block table with `pread` + `FADV_RANDOM` | **0.74 s** | 0.007 s |
| Warsaw tile: 38 of 5,685 blocks, 19.6 MB, 22 k ways, 32 k tagged nodes | +≈0.04 s I/O | 0.046 / 0.061 / 0.138 s (12 / 4 / 1 threads) |

A 180 kB sidecar copy of the block table, keyed by file size and mtime, would
make opening instant.

### 8. Dependencies, and the grid cache compared with model B

The model B converter (§7) uses protozero (header-only) and libdeflate.
libosmium appears only in the verification tool. Neither is needed:

- **Protobuf:** PBF uses varints, packed fields and nested messages only.
  An own reader and writer is about 300–400 lines at the same speed; the
  block scanners in this study already parse headers that way.
- **Deflate:** miniz is already vendored in `src/mzip`. The converter
  recompiled on miniz (`-DUSE_MINIZ`) was verified identical in content.

| Poland | libdeflate | miniz |
|---|---:|---:|
| Conversion, 12 threads | 34.2 s | 38.1 s (level 1: 31.0 s, +6 % size) |
| Conversion, 4 threads | 59.4 s | 69.1 s |
| pomorskie conversion, 12 threads | 1.9 s | 2.6 s |
| Warsaw tile decode, 12 / 4 / 1 threads | 0.046 / 0.061 / 0.138 s | 0.053 / 0.073 / 0.174 s |

So model B needs **no new dependency**: own protobuf code plus miniz, 10–25 %
slower than libdeflate. libdeflate (MIT, a handful of C files) could be
vendored later the same way as miniz, if that speed ever matters.

Multipolygon assembly was the one thing libosmium gave the grid cache. Model B
keeps relations as they are, so their rings are assembled when the data is
used. Because member ways keep their node IDs, rings join by matching end
nodes, and roles say outer or inner. An own assembler is a few hundred lines;
libosmium's extra value is repairing broken geometry.

The grid cache figures come from §4 and §5; the model B figures from §7.

| | Grid cache (first proposal) | Model B: sorted PBF |
|---|---|---|
| Disk, pomorskie | 118 MB PBF + 187 MB cache = 305 MB | 128 MB (replaces the PBF) |
| Disk, Poland | 2.1 GB + 3.6 GB = 5.7 GB | 2.33 GB |
| First use, pomorskie | 3.7 s (libosmium, 1 thread); 0.7 s parallel prototype, typed ways only | 1.9 s, everything (2.6 s on miniz) |
| First use, Poland | 77 s and 10.7 GB (libosmium); 12.8 s parallel prototype held all output in RAM (12–15 GB) | 34 s, 4.7 GB heap, 5.9 GB temporary disk (38 s on miniz) |
| Content | Features with a TSRE type only, multipolygons pre-assembled, tags optional | Everything tagged, all tags, way node IDs (topology), relations as they are |
| Tile data access | 2–40 ms, no decoding (`mmap`) | 50–60 ms on 12 threads, 0.14–0.17 s on 1 thread (decode) |
| Tile draw at 4096 px | 40–480 ms | Same |
| Other tools can read it | No | Yes (libosmium, osmium-tool, QGIS/GDAL) |
| Dependencies without vendoring | Own PBF reader + own multipolygon assembly | Own PBF reader/writer + miniz |

The import times above are not like for like: the grid prototypes kept less
and some held everything in RAM. Built with the same parallel pipeline as
model B, a grid cache would take about as long to import; it skips deflate on
output.

### 9. Supporting both behind one interface

Both stores answer the same question: the features in a bbox, optionally
filtered by kind or tag. Consumers can therefore be written against one
interface:

```
OsmStore::forEach(bbox, filter, callback(const OsmFeature&))
OsmFeature: kind (node/way/relation), id, tags (string views),
            coordinates (int32, 1e-7°), node ids, relation members
```

- **`SortedPbfStore`** (model B) is the canonical store. It is converted once
  from the user's downloaded PBF and kept in or next to the OSM data directory.
- **`GridStore`** is an optional accelerator derived from the sorted PBF, not
  from the original. Ways in the sorted file already carry their coordinates,
  so building it needs no node-location table: one decode pass (5.8 s for all
  of Poland with libosmium), classification and a write, with low RAM. Its
  build time was not measured.
- Only the conversion step needs a lot of memory; every derived store is
  cheap to rebuild.

Decode time is already well below drawing time for one tile. The grid store
only pays off for bulk work: repeated procedural passes over the same cells,
or many small queries. Even rendering every tile of an area mostly decodes
each block once, given a small block cache. Decoding the whole converted
pomorskie file takes 0.33 s (libosmium, 12 threads). Suggested order: implement `SortedPbfStore` first,
measure real consumers, and add `GridStore` only if they show a need.

### 10. Current tile drawing (`MapDataOSM::draw`), measured 2026-10-08

These are the existing styles drawn from the cache at 4096 px
([evidence](evidence/2026-10-08-osm-draw/README.md)). Times are the best of
3 runs, warm:

| Variant | Gdańsk centre | Tczew |
|---|---:|---:|
| Today: `fillPath` on a `Format_RGB888` image | 0.30 s | 0.18 s |
| Same, `Format_RGB32` image | **0.20 s** | **0.10 s** |
| Strokes skipped (RGB32) | 0.06 s | 0.04 s |
| Fills skipped (RGB32) | 0.16 s | 0.08 s |
| Nothing drawn: clear + projection | 0.03 s | 0.02 s |

- **Strokes cost 75–80 % of the time.** Road casings use wide pens
  (10–14 px) with round joins, and each road is stroked twice. Fills are
  about 20 %, and `drawPolygon` against `fillPath` makes no difference.
- **`Format_RGB888` is a slow path for QPainter's raster engine.** Drawing
  into `Format_RGB32` (`ARGB32_Premultiplied` when alpha is used) is
  1.5–1.8× faster. `TexLib` converts map images to RGB888/RGBA8888 for upload
  anyway; that conversion costs about 20–30 ms for 4096², not measured.
- **Parallel quadrants:** drawing one tile as four 2048 px quadrants on four
  threads takes 0.09 s against 0.21 s (2.3×; geometry crossing quadrant
  borders is drawn twice). Four painters can each target a sub-rectangle of
  the same `QImage` buffer (constructor with full-width `bytesPerLine`), so
  nothing needs composing afterwards.
- **Not measured, likely smaller gains:**
  - Clip long ways to the tile plus pen margin before stroking; today
    off-tile segments are stroked too.
  - Draw per style instead of per feature: all casings of a class, then all
    inner lines, in one path each. Fewer state changes, and junctions draw
    correctly (today a later road's casing covers an earlier road).
  - Replace the 60-branch `if` chain with a data-driven style table. That
    makes per-style drawing possible, and the table can be shared with the
    planned vector renderer.
- **Projection** is one virtual call per point: linear for TSRE projections,
  trigonometry for MSTS IGH and TM. For 350 k points that is up to about
  0.1–0.2 s for IGH/TM (estimate). Project each point once per query, not
  once per use.
- **Other painting libraries:**
  - Blend2D, Skia and AGG are faster rasterisers, but each is an external
    dependency.
  - The natural next step is the planned GPU vector rendering in TSRE's own
    renderer: triangulated fills and line meshes, so tile images are no longer
    needed.
  - Until then, the cheap wins above (RGB32 plus quadrant threads) give
    roughly 3× on single-tile latency.

## Agreed design (2026-10-08)

- **Storage:** model B. Each source `.osm.pbf` is converted once into a
  spatially sorted PBF with locations on ways, kept in or next to the OSM data
  directory.
- **Settings:** the OSM data directory becomes one directory setting,
  `core.paths.osmData` (`SettingType::Directory`, group "maps"), read
  through `Settings::string(...)`.
  - No legacy `settings.txt` key and no `Game::` field. `geoPath` exists only
    as the legacy key and code symbol of `core.paths.geoData`, kept for
    migrating old `settings.txt` files.
  - New settings such as `geo.elevation.source` register both as empty.
  - A console argument only if wanted.
- **No external dependencies.** Own protobuf reading and writing, compression
  through the vendored miniz, and own multipolygon assembly (the old TSRE map
  code managed without libraries too).
- **Backend prepared for more stores:** consumers use an `OsmStore`
  interface (§9). `SortedPbfStore` is implemented first; a `GridStore`
  derived from the sorted file can be added behind the same interface later.

## Fidelity notes

- `MapDataOSM::r()` (IGH → tile through `Game::GeoCoordConverter`) was
  replaced by a Mercator projection with comparable trigonometry per point.
- The cache keeps only ways with a recognised type. Today's path also draws
  untyped ways as thin grey lines (6 216 ways drawn today against 5 083 in the
  cache for Tczew).
- Multipolygon inner rings were drawn as separate filled polygons. That is a
  cost proxy only; production code needs odd-even fill.
- Region imports clipped the output, but node locations were still held for
  the whole source file. A real region import also bounds that memory.

## Proposed design (for discussion)

1. **OSM source directory.** Add a setting that points to a directory of
   `*.osm.pbf` files. Scanning reads only file headers (bbox, timestamp, size).
   For a route bbox, choose the smallest file that covers it. If several files
   are needed, merge them and drop duplicate IDs: Geofabrik extracts overlap at
   their borders.
2. **Importer**, running in the background with progress reporting:
   - PBF → TSRE OSM cache.
   - Decoding and way resolution are parallel; multipolygon assembly uses
     libosmium's `MultipolygonManager`.
   - Classification into TSRE feature classes, building on today's
     `OSMFeatures`, plus the tags that procedural generation needs.
   - The cache key is source file identity (name, size, header timestamp),
     region, and schema version.
3. **Cache format.** A multi-level lat/lon grid with int32 coordinates, so it
   is independent of the route projection; each consumer projects at use time.
   Read through `QFile::map` (Linux and Windows), so the page cache is the
   in-memory cache. Consumers keep only small decoded caches of their own.
4. **Consumers.**
   - A cache-backed `MapData` implementation replaces the HTTP path in
     `MapWindow`, keeping today's style table.
   - Procedural generators query features by bbox and class.
   - Batch rendering of tile maps becomes practical (≈100–300 tiles/s).

Also seen in today's code:

- Nodes and ways accumulate across tile loads and are never cleared.
- `draw()` allocates its painter, colours and pens and never frees them.
- The API refuses bboxes with more than 50 000 nodes, which dense city tiles
  can hit.
- A hard-coded `F:/OSM/tczew.osm` fallback remains.

## Decisions needed

0. **Storage model.**
   - Grid cache beside the PBF: fastest, about 2× disk.
   - A. ID index + original PBF: no extra disk, but every tile reads tens to
     hundreds of MB.
   - B. The PBF replaced by a TSRE-sorted PBF: no extra disk, near-cache
     speed, and still a standard file. It is no longer the pristine Geofabrik
     download, and an update means download + convert.
   - Suggested: B. The sorted file keeps all tags of the features it holds,
     so procedural generation needs no second source.
   - **Decided 2026-10-08: B.** The user accepts about 4 GB RAM for a
     country-size conversion that is freed afterwards; lower-end systems use
     regional files. Measured in §7.
1. **Import scope.**
   - (a) Whole source file: one cache shared by all routes. This suits regional
     extracts (≈1 s, ≈190 MB per voivodeship). A national file would need a
     disk-backed node index and a cache of ≈3.6 GB.
   - (b) Route region plus margin: small and fast to import, but it has to be
     re-imported when a route grows.
   - Suggested: (a) for files up to a few hundred MB, (b) for larger files.
     Users would be advised to download regional extracts.
2. **Reader dependency.** **Decided 2026-10-08: no new dependencies.** Use
   own protobuf code plus the vendored miniz (§8); libdeflate may be vendored
   later if speed matters.

   Original options:
   - libosmium + protozero: header-only, BSL-1.0, needs zlib (an MSYS2 package
     for the MinGW CI). It brings the multipolygon assembler.
   - An own minimal reader: about 500 lines on miniz or a vendored libdeflate.
     Multipolygon assembly would then have to be written as well.
   - Suggested: libosmium for the importer.
3. **Tags kept.** Type only (today), a curated subset, or all tags (+58 %).
   This depends on what procedural generation will need first.
4. **Cache location.** **Decided 2026-10-08: in or next to the OSM data
   directory**, shared by all routes.
5. **Original file after conversion.** Delete the original download
   automatically, ask the user, or keep both (about 2× disk). Converting
   again needs the original or a fresh download.
6. **Second backend.** A `GridStore` behind the same interface (§9): now, or
   only once a consumer shows a need. Suggested: later.

## Still to agree

All points were resolved with the user on 2026-10-08. The outcome is in
[osm-data-design.md](osm-data-design.md).
