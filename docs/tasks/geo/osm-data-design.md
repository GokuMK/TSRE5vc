# OSM data from local PBF files: agreed design and implementation plan

Branch `feature/osm-data`. Agreed with the user on 2026-10-08. The measurements
behind each decision are in the [performance study](osm-data-pbf-performance.md).

## Goal

The user points TSRE at a directory of downloaded `.osm.pbf` files (Geofabrik
extracts). TSRE converts each file once into a spatially sorted PBF and then
answers bbox queries from it quickly. These queries serve:

- the existing tile map, which today uses the OSM HTTP API;
- procedural generation over large areas;
- the planned vector renderer.

## Decisions

| Topic | Decision |
|---|---|
| Storage | Convert once into a spatially sorted PBF with coordinates on ways (`LocationsOnWays`). It stays a standard PBF that other tools can read. |
| Backends | Consumers use an `OsmStore` interface. `SortedPbfStore` comes first; a derived `GridStore` can be added later behind the same interface. |
| Dependencies | None new. Own protobuf code, deflate through the vendored miniz (`src/mzip/miniz`), own multipolygon assembly. |
| Directory | One modern setting, `core.paths.osmData` (directory, group "maps"). No legacy key and no `Game::` field. Only top-level `*.osm.pbf` files are read, never subdirectories. A different data set means a different directory. |
| Original file | Setting: keep both, or delete the original after a successful conversion. |
| Overlapping files in one directory | Query all files whose header bbox intersects the request. Drop duplicates by (type, id), preferring the newer header timestamp. |
| Naming | Detection uses the file header, not the name (below). `.idx` is only an optional cache. |
| Conversion trigger | When a query first needs an unconverted file, the user is prompted. Conversion then runs in the background with progress and cancel. |
| HTTP API | Kept as a fallback for the tile map when no OSM directory is set or nothing covers the tile. |
| Classes and styles | A data-driven class and style table replaces the `OSMFeatures` string table and the `if` chain in `MapDataOSM::draw()`, designed so the future vector renderer can use it. |
| Drawing speed-ups | In this branch: 32-bit images, project once, clip, draw per style, parallel quadrants. |
| Tests | A small fixture PBF written by our own writer; round-trip tests; an opt-in benchmark. |
| Licence | Docs and the About window state that OSM data is © OpenStreetMap contributors (ODbL). Attribution in a route is the route designer's responsibility. |

## Converted file format

A converted file is an ordinary PBF with these properties.

**`HeaderBlock`:**
- `required_features`: `OsmSchema-V0.6`, `DenseNodes`.
- `optional_features`: `LocationsOnWays`, plus `TSRE-Spatial-1` as the
  format-version marker.
- `source` (field 17) records the original file's name, size and header
  timestamp. That is how the directory scan matches a converted file to
  its original.
- `bbox` and the replication timestamp are copied from the original.

**Contents:**
- Tagged nodes as `DenseNodes`.
- Ways with `keys`, `vals`, `refs` (node ids, keeping topology) and `lat`/`lon`
  (fields 9/10).
- Relations, unchanged except for ordering.
- Dropped: untagged nodes, metadata, and the few ways that have no tags and
  belong to no relation.

**Ordering:**
- Blocks hold at most 8,000 entities.
- Features go into buckets: one per 1° cell for features up to 1/8° in size,
  and one per level for larger features.
- Within a bucket, features are sorted by kind (node, way, relation), then
  size level, then the Morton code of the 1/32° cell at their bbox centre.
- **Relations are sorted by their members' bbox** (known after the way pass)
  in the same buckets. A relation without resolvable members goes into a
  final unbounded block.

**Index:**
- Each data block's `BlobHeader.indexdata` holds 18 bytes: a version byte, a
  kind byte, and the block bbox as 4 × int32 little-endian at 1e-7°. Other
  readers ignore the field.
- `<file>.idx` is an optional sidecar copy of the block table, keyed by file
  size and mtime. Without it, a `pread` walk over the headers takes 0.74 s
  for Poland from cold storage and 7 ms warm.

**Names:**
- In delete mode, the converter writes `<name>.part` and renames it over the
  original, so the name stays the same. On Windows the source must be
  unmapped and closed before the rename.
- In keep mode, the output is `<name>.tsre.osm.pbf` beside the original.
- A later Geofabrik download that overwrites a converted file is detected by
  its header (no `TSRE-Spatial-1`), and conversion is offered again.

## Components

New code goes in `src/tsre/geo/osm/`, added to the CMake glob.

| Component | Responsibility |
|---|---|
| `OsmProto` | Protobuf primitives: varint, zigzag, packed fields, nested messages; reader over a byte span, writer into a `QByteArray`/`std::string`. |
| `OsmPbfReader` | Blob walk (`pread`/`QFile`, readahead off), header parsing, block decoding into reusable structs (string table, dense nodes, ways with or without locations, relations). Inflate through miniz. |
| `OsmPbfWriter` | Header block and data blocks (string table, dense nodes, ways with locations, relations), deflate through miniz, `indexdata`. |
| `OsmConverter` | The parallel pipeline from §7 of the study, plus relation sorting. Temporary bucket files sit beside the output; the job checks memory and disk first, reports progress, can be cancelled, finishes atomically, then deletes the original (if the setting says so) only after the result passes its checks. |
| `OsmDirectory` | Scans top-level `*.osm.pbf` files, reads headers, classifies each file as converted or original, pairs them, and lists the files covering a bbox. Raises the "conversion needed" request that the UI turns into a prompt. |
| `OsmStore`, `OsmFeature`, `OsmFilter` | The query interface: `forEach(bbox, filter, callback)`. A feature is a node, way or relation with id, tags (views), int32 coordinates, node ids and members. |
| `SortedPbfStore` | Implements `OsmStore` over the converted files: block table, bbox selection, parallel block decoding, an LRU of decoded blocks with a byte budget, dedupe across files. |
| `OsmMultipolygon` | Assembles rings from member ways by matching end node ids; outer/inner from roles, checked by containment; tolerates open or broken rings by dropping them, as the old TSRE map code did. Results are cached per relation id. |
| `OsmClasses` | Data-driven tag → class rules and class → style entries: fill, casing, inner line, width, layer, bridge/tunnel variants. Widths are stored in metres, with pixel equivalents derived for the tile map, so a vector renderer can use the same table. |
| `MapDataOSM` (reworked) | Queries the store, projects each point once, clips to the tile plus pen margin, draws per layer and per style, uses `Format_RGB32`/`ARGB32_Premultiplied`, and draws four quadrants in parallel. Falls back to the HTTP path. Fixes the node/way accumulation between tiles and the per-draw allocations that are never freed. |
| UI | Directory setting in the settings dialog; the conversion prompt; a progress dialog with cancel; an ODbL note in the About window and the docs. Translation IDs are added to `tsre_en.ts`/`tsre_pl.ts` by hand (`lupdate` on this machine rewrites the whole catalogues). |

`GridStore` is not part of this branch. The interface and the converted
format are designed so it can be built later from a converted file in one
pass, without the node-location table.

## Settings

| Key | Type | Default | Meaning |
|---|---|---|---|
| `core.paths.osmData` | Directory | empty | Directory of `.osm.pbf` files; top level only. |
| `core.osm.originalAfterConversion` | Enum `keep` / `delete` | `keep` | What happens to the downloaded file after a successful conversion. |

The default is `keep` because deleting a user's file must be an explicit
choice. The conversion prompt shows the current choice and lets the user
change it.

## Implementation steps

Each step is one or more commits on `feature/osm-data`. Each step is verified
before the next one starts.

1. **PBF format layer:** `OsmProto`, `OsmPbfReader`, `OsmPbfWriter`.
   - Tests: round trip of every message type; reading the local Geofabrik
     files gives the counts recorded in the evidence (pomorskie: 13,112,881
     nodes, 1,727,754 ways, 17,271 relations, 7,803,177 tags). Runs as an
     opt-in local-data test.
2. **Converter**, including relation sorting and the checks.
   - Tests: a synthetic fixture in Geofabrik style (ID-sorted, no
     locations) converts to the expected output.
   - Local data: every coordinate matches the original and tag totals are
     unchanged, as in the study.
   - Opt-in benchmark target (`EXCLUDE_FROM_ALL`). Target: miniz build
     within about 10 % of the study's numbers (pomorskie about 2.6 s,
     Poland about 38 s on 12 threads).
3. **`OsmDirectory` and settings**, the conversion prompt and progress UI,
   and translations.
   - Settings tests in `SettingsTestSuite`.
   - Directory classification tests: original, converted, pair, overwritten
     download, subdirectories ignored.
4. **`OsmStore` and `SortedPbfStore`.**
   - Tests: bbox queries return exactly the features a brute-force scan
     finds; duplicates across overlapping files are removed; the block cache
     respects its budget.
5. **`OsmMultipolygon`.**
   - Tests: simple polygon, holes, rings joined from several ways, broken
     and unclosed rings, nested outers.
6. **`OsmClasses`.** Port today's classification and styles into the table.
   - Parity: render the Tczew and Gdańsk tiles with the old `draw()` code
     and with the table, and compare. Differences must be explained
     (multipolygons are new; untyped ways may be dropped).
7. **`MapDataOSM` on the store**, with the drawing speed-ups and the HTTP
   fallback kept.
   - Measure against the study: at least 3× on a single tile.
8. **Documentation:**
   - user doc for the OSM directory and conversion;
   - settings doc entries;
   - the ODbL note in About;
   - update the geo `README.md` reading order.

## Notes and risks

- **Windows:** use `QFile` for reads and maps, close every map before a
  rename or delete, and use 64-bit offsets throughout (files over 2 GB).
- **Memory:** about 16 bytes per node in the source, freed after the way
  pass. The pre-check warns and suggests regional extracts when RAM is short.
- **Disk:** temporary files take about 2.8× the input; with keep mode the
  output adds about 1.1×. The pre-check covers both.
- **Speed:** miniz is 10–25 % slower than libdeflate. libdeflate (MIT, a few C
  files) can be vendored later the same way if needed.
- **Line endings:** the tile map code sits in CRLF/LF-mixed files. Edit them
  in binary-safe mode and compare `git diff --shortstat` with and without
  `--ignore-cr-at-eol` before committing.
