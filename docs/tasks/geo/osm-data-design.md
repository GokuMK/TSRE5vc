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
- The converted file is always `<name>.tsre.osm.pbf` beside the download.
- In delete mode, the download is removed after the converted file is
  complete. This is safer than renaming over the original: on Windows the
  original would have to be deleted first, and an interruption in between
  would lose both files.
- A later Geofabrik download placed in the directory is matched to its
  converted copy through the source identity in the header, not by name. A
  newer download is offered for conversion again.

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
| `geo.osm.originalAfterConversion` | Enum `keep` / `delete` | `keep` | What happens to the downloaded file after a successful conversion. |

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

## Implementation status

**Steps 1–2 done (2026-10-08).** New code is in `src/tsre/geo/osm/`; tests
are in `tests/osm` (`ctest -R osm`, plus the opt-in `--count`, `--convert`
and `--verify` modes for local data).

Format layer:
- Reading all of Poland (34,599 blocks, every entity and tag) takes 3.1 s on
  12 threads with miniz; libosmium takes 6.5 s.
- Counts are identical to libosmium: 242,829,698 nodes, 33,661,806 ways,
  280,493 relations, 135,986,026 tags, 316,454,747 way refs, 5,483,134
  members.
- It also reads libosmium's `LocationsOnWays` output.

Converter:
- Relations are sorted by member extent.
- Output is byte-identical across thread counts and for shuffled, unsorted
  input.
- Level 1 is the default compression.

| Source | Level | Time (12 threads) | Output | Notes |
|---|---:|---:|---:|---|
| pomorskie | 1 | 2.5 s | 125 MiB | |
| pomorskie | 6 | 3.1 s | 119 MiB | |
| Poland | 1 | 24.8 s | 2,281 MiB | node table 3.7 GiB, temp 6.2 GiB |
| Poland | 2 | 30.4 s | 2,202 MiB | |
| Poland | 6 | 37.1 s | 2,175 MiB | |

- `--verify` on Poland: 316,431,023 way coordinates, 0 mismatches; tags,
  relations and members identical to the source.
- Other sessions kept the load average at 3–10 during these runs.
- The write phase is mostly compression (57 % of its CPU at level 6). Record
  parsing and block building take most of the rest; they can be tightened
  later.

**Step 3 done (2026-10-08).**
- `OsmDirectory` scans top-level files by header only: 14 Poland downloads
  in 7 ms from cold storage. It pairs downloads with their converted copies
  through the source identity and lists pending conversions per area.
- New settings `core.paths.osmData` and `geo.osm.originalAfterConversion`,
  claimed in `Game.cpp`, with EN/PL translations.
- `Osm::ensureConverted()` asks before the first use of an area. The
  question shows the files, the needed memory and disk against what is free,
  and a delete-after-conversion checkbox that saves the setting. Conversion
  runs on a worker thread behind a cancellable progress dialog. A declined
  file is not offered again until restart.
- Tests: `tests/osm` (64 checks) and the application suite
  `TSRE5vc --test --test-suite osm-data`, which drives the dialog headless
  (`TSRE_OSM_UI_SNAPSHOTS=<dir>` saves snapshots).
- The prompt is wired into the tile map in step 7.

**Step 4 done (2026-10-08).**
- `OsmStore` is the query interface: `forEach(area, filter, callback)`.
  `Feature` gives tags, coordinates, node ids and members; `Filter` selects
  by type, keys and ids.
- `SortedPbfStore` implements it over converted files:
  - reads only the blocks whose `indexdata` bbox meets the query, decoding
    them in parallel;
  - keeps decoded blocks in an LRU cache (512 MiB budget by default);
  - reports a feature present in overlapping files once, taking the newest
    file.
- Relations now carry their member extent in a TSRE extension field
  (`Relation` field 16001, packed sint32). Standard readers skip it, and
  libosmium still reads the files.
- Without the extent, a relation query matched every relation of a 1° cell.
  Relations without a resolvable member are not returned by area queries.
- `.idx` sidecar of the block table next to each converted file: opening
  Poland from cold storage takes 0.006 s with it and 0.79 s without; the walk
  now reads each header in one unbuffered read, down from 2.1 s.

Queries, converted files not in the page cache / warm, 12 threads:

| Query | Not cached | Warm | Blocks | Result |
|---|---:|---:|---:|---|
| Tczew 2 km tile (pomorskie) | 51 ms | 1.5 ms | 16 | 3,851 nodes, 5,965 ways, 232 relations |
| Gdańsk centre tile | 84 ms | 5 ms | 28 | 7,555 nodes, 13,589 ways, 672 relations |
| Rural tile | 61 ms | 4 ms | 19 | 179 nodes, 489 ways |
| 60 × 60 km area | 0.19 s | 0.02 s | 141 | 562,331 ways, 4.9 M points |
| Warsaw centre tile (Poland) | 175 ms | 11 ms | 45 | 31,743 nodes, 22,486 ways, 1,379 relations |

Tests: 140 checks in `tests/osm`. Sixty random area queries match a
brute-force scan of the fixture exactly, geometry included; filters, dedupe
and the cache budget are covered. Opt-in local data:
`--query <minLon> <minLat> <maxLon> <maxLat> <files...>`.

**Step 5 done (2026-10-08).**
- `OsmMultipolygon` joins the member ways of a relation into closed rings by
  end node id, in the outer and inner groups; an empty role counts as outer.
- Each inner ring goes to the smallest outer ring that contains it.
- Orientation is normalised: outer counter-clockwise, inner clockwise.
- Broken chains and missing members are dropped and counted; nothing is
  repaired.
- `assembleMultipolygons()` fetches the member ways of many relations in one
  store query.
- Tests cover split and reversed outers, holes, two polygons, an island in a
  hole, gaps, orphan inners, roles, touching rings, and a store round trip.

Results on the converted pomorskie file:

| Area | Relations | Assembled | Time |
|---|---:|---:|---:|
| Whole voivodeship | 7,758 | 7,619 (98 %): 10,902 polygons, 15,273 holes | 0.31 s |
| 60 × 60 km | 1,271 | 1,240 | 0.24 s |

- In the Tczew tile every `type=multipolygon` relation assembled.
- The failures there are `type=boundary` relations that are not areas at this
  scale:
  - religious administration made of sub-relations;
  - NUTS, electoral and maritime boundaries whose ways lie mostly outside the
    regional extract.
- **Consumers should request `type=multipolygon`** and only the boundary
  kinds they draw (e.g. `protected_area`). Huge boundaries also cost time,
  because their members span the whole file.

**Step 6 done (2026-10-08).**
- `src/tsre/geo/osm/osm-map-classes.json` (Qt resource `:/osm/osm-map-classes.json`)
  holds:
  - the classification rules;
  - 636 classes (`key=value` and draw layer);
  - 66 styles: fill, outline, casings, line, a bridge variant, widths in
    metres;
  - the default style and the background colour.
- It was generated from the legacy `OSMFeatures` table and a branch-by-branch
  transcription of `MapDataOSM::draw()`
  (`evidence/2026-10-08-osm-draw/gen_osm_map_classes.py`).
- Legacy pixel widths become metres at the legacy 2 px per metre (4096 px over
  a 2048 m tile), so the default tile looks the same, and other resolutions
  and renderers scale.
- `FeatureClasses` loads it, classifies a feature with the legacy loader's
  rules and resolves its style. An exact tag beats `key=*`; the first style
  listing a class wins.

Parity:
- The legacy rules, run against the real `OSMFeatures` tables, agree with
  the new classifier on every legacy class and on mixed tag sets.
- `--classes` on converted pomorskie: all 2,356,948 ways and tagged nodes
  classify identically.
- One intended change: `bridge=no` is no longer a bridge (the legacy loader
  set the flag for any `bridge*` key). It affects one feature in pomorskie.

Known legacy quirks not carried over:
- `amenity=*` keys are still skipped by the rules, as before; the
  school, parking and place-of-worship styles become reachable only if
  `amenity` is removed from `skipKeyPrefixes`.
- The colour (157, 256, 108) was clamped to 255.
- The primary_link branch did not reset its pen width, which made later
  outlines thick; that is not reproduced.

**Step 7 done (2026-10-08).**
- `MapDataOSM::load()` uses the local OSM directory when a converted file
  covers the tile. It asks first through `ensureConverted()`, keeps one
  `SortedPbfStore` over all converted files (block cache shared across
  tiles), and falls back to the four OSM API requests otherwise.
- Both paths produce the same items: features classified by
  `FeatureClasses`, every point projected once, `type=multipolygon`
  relations with a fill style assembled with their holes.
- Drawing, with `MapWindow` now creating `Format_RGB32` /
  `ARGB32_Premultiplied` images:
  - four threads paint the quadrants of the same image buffer;
  - each item is culled to its quadrant;
  - each legacy slot draws in batches per style: fills (winding rule,
    consistent ring orientation), then outlines, casings and lines;
  - pen widths come from the style in metres.
- The API path no longer accumulates nodes and ways across tiles,
  deduplicates the four overlapping responses, uses one network manager,
  frees replies, and replaces the 5 s busy wait after "No data" with a timer.
- `OSMFeatures` left the application. It lives on in
  `tests/osm/legacy/` as the classification parity reference.

At 4096 px on real data, on 12 threads:

| Tile | Items | Load (query, projection, multipolygons) | Draw | Legacy draw (study §3/§5) |
|---|---:|---:|---:|---:|
| Tczew | 5,462 | 76 ms | 43 ms | 0.16–0.18 s |
| Gdańsk centre | 10,180 | 111 ms | 49 ms | 0.30 s |
| Warsaw centre | 23,500 | 145 ms | 82 ms | 0.43–0.48 s |

- Drawing is 4–6× faster.
- The whole tile now takes 0.12–0.23 s, against the legacy 2–6 s
  (network, XML, draw).
- Parsing the four saved Tczew API responses takes 443 ms (legacy 570 ms).

Differences from the legacy map image, all intended:
- multipolygons (lakes, forests, parks) are drawn;
- untagged ways are no longer drawn as thin dark lines (they are relation
  members);
- open ways with an area style are not filled;
- within a slot, all casings are drawn before all road lines, so junctions
  join cleanly;
- line widths scale with the image resolution (identical at the default
  4096 px for a 2048 m tile).

Tests:
- The `osm-data` suite renders a fixture tile (road, building, lake with
  island, rail bridge) from the store and from API XML, and checks pixel
  colours.
- Opt-in `TSRE_OSM_TEST_DIR` renders the real tiles above;
  `TSRE_OSM_API_XML_DIR` renders saved API responses;
  `TSRE_OSM_UI_SNAPSHOTS` saves the images.

**Step 8 done (2026-10-08).**
- User guide `docs/features/osm-data.md`.
- An OpenStreetMap section in `docs/settings-system.md` and an entry in
  the geo `README.md`.
- An ODbL attribution line in the About window (EN/PL); the `osm-data`
  suite checks it.
- Final converter on Poland with 4 threads: 47 s (level 1, load average
  about 3).

## Overview layers for large-scale views (2026-10-08)

**Problem.** The sorted file is ordered by place, not by theme. "All railways
of an area" decodes every way block and keeps under 1 % of what it reads:

- Pomorskie: 0.27–0.37 s.
- Poland: 4.9–6.2 s, and 2.6–4.7 s even when repeated. Decoded Poland is about
  12 GB, so no block cache holds it.

Railway-only blocks would be 14.9 MB for Poland and decode in 0.03 s.

**Decision.** Each converted file gets overview levels: small sorted PBFs
`<base>.tsre.overview.<level>.pbf` next to it.

- **Content.** Only the features selected by the level's rules, with
  Douglas–Peucker simplified geometry; end points stay, so multipolygon
  rings still join.
- **Rules** live in the `overview` section of `osm-map-classes.json`:
  key=value or `key=*` patterns, `unless` patterns, feature types, and a
  minimum area for closed ways and multipolygons.
- **Building.** Levels are built in one pass over the converted file, by
  `OsmDirectory::convert` right after converting, and by `ensureConverted`
  without asking when they are missing or stale.
- **Staleness.** Each file records the converted file's size and mtime and a
  hash of the rules, so editing the rules or reconverting rebuilds them.
- **Not a download.** Overview files carry `TSRE-Overview-1` and do not end
  in `.osm.pbf`, so the directory scan ignores them.
- **`OsmLayers`** opens the detail store plus one store per level and picks
  by view resolution:
  - `forScale(metersPerPixel)` returns detail below the first level;
  - otherwise it returns the finest built level at or below the wanted one;
  - a level is used only when every converted file has it.
- **Relation support.** Only multipolygon and boundary relations are
  supported in overviews.

**Levels.** Configurable; these are the defaults.

Detail (the sorted file) serves views finer than 20 m per pixel. At that
resolution a 4096 px view covers about 80 km. A 60 km area still queries in
0.02–0.19 s from the detail file, while buildings and residential streets are
already only 1–2 px wide.

| Level | From | Tolerance | Content |
|---|---:|---:|---|
| `regional` | 20 m/px | 5 m | rail, light rail, narrow gauge, subway and preserved railways; stations and halts; roads motorway to tertiary with links; rivers, canals, coastline; water, forest, residential, industrial, commercial, retail, railway land and aerodromes of 5 ha or more; cities, towns, villages |
| `national` | 150 m/px (a province on a 2000 px screen) | 40 m | rail and narrow gauge without `service=*`; motorway, trunk, primary; rivers, canals, coastline; water and forest of 1 km² or more; cities and towns |

Measured (Poland built under load average 6–8 from other sessions):

| | pomorskie | Poland |
|---|---:|---:|
| Build both levels | 0.77 s | 12.5 s |
| Regional | 6.8 MB, 56,120 ways, 942 relations; points 1.50 M → 0.81 M | 113 MB, 888,619 ways, 16,417 relations; points 26.7 M → 14.2 M |
| National | 1.4 MB, 15,595 ways, 333 relations; points 0.57 M → 0.14 M | 20 MB, 238,660 ways, 5,514 relations; points 9.4 M → 1.9 M |
| All railways: detail / regional / national | 0.27–0.37 s / 0.020 s / 0.012 s | 4.9–6.2 s / 0.20 s / 0.063 s |
| Whole level, warm: regional / national | 4 ms / 1 ms | 47 ms / 14 ms |

Tests in `tests/osm` (183 checks) cover:
- rule selection per level (sidings, small areas, hamlets, stations);
- simplification and multipolygons assembled from an overview file;
- staleness after rule or file changes;
- scale selection and the fallback when a level is missing.

Opt-in: `tsre_osm_tests --overview <converted files...>`.
