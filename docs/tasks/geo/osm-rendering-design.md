# OSM data in the map mode: design and implementation plan

Branch `feature/osm-rendering`, from `main` at `0ef878b` (2026-10-08). It builds
on the local OSM data ([design](osm-data-design.md)) and the Route Editor's map
mode ([task editor 04](../editor/04-map-mode.md)). The measurements are in
[evidence/2026-10-08-osm-rendering](evidence/2026-10-08-osm-rendering/results.md).

Status (2026-10-09): the OSM layer, forest generalization, country-scale
speed work, the Windows thread fix and map labels (markers, route data, OSM
places and stations) are done and on `main` up to `c0c8ccc`; the labels
(`3fccc45` to `227207f`) are on `feature/osm-rendering`, not merged yet. The
user's test on a GPU is pending. See "Implementation status" and "Open
items" at the end.

## Goal

The map mode draws OSM vector data as a layer above the terrain and under the
route's own data (track, roads, objects, activity). It shows the whole route's
surroundings at any zoom, from single buildings to a whole country, read from the
user's OSM directory.

It is also the first step towards a general vector renderer: the class and style
table, the scale levels and the geometry built here are meant to be reused.

## Decisions (user, 2026-10-08)

| Topic | Decision |
|---|---|
| Layer order | Terrain, then OSM, then the fade. Map > Faded Terrain becomes **Faded Overlay** and fades every layer under the route data, OSM included. |
| Triangulation | Vendor `earcut.hpp` (Mapbox, ISC, one header), as miniz is vendored. Accepted on the condition that it is fast; measured below. |
| Scale ranges | Per style, in `osm-map-classes.json`. |
| Lines | Built on the CPU in the first version. A dedicated shader is a later step. |
| Transparency | Areas can be drawn semi-transparent. The user toggles it; no automatic switch when terrain is shown. |

"Faded Overlay" is the name read from the user's answer ("rename it to Overlay").
It is one string to change if another name is wanted.

## Current state

- **Map layers** (`src/tsre/map`) each build their geometry for the view plus a
  margin, on the GUI thread, and rebuild on a tile change, a scale step of 25%,
  or a palette change. `RouteEditorGLWidget::paintMap` pushes them into one
  orthographic view; both renderers (OpenGL, QRhi) draw it.
- **Order** comes from fixed heights and the depth test: terrain 10 to 45, the
  fade square at 50 (inside `TerrainMapLayer`), roads 100, track 200, markers
  above.
- **Geometry**: `OglObj` with position-only vertices (`RenderItem::V`) and one
  colour per object, alpha below 1 for blended drawing (back to front). No
  vertex colours, no GPU instancing.
- **Lines** are quads built on the CPU (`TrackMapLayer::appendRibbons`). The
  renderer also draws 1-pixel line primitives on every backend.
- **OSM**: `OsmLayers::forScale(metresPerPixel)` picks the detail file or an
  overview level (regional from 20 m/px, national from 150 m/px).
  `FeatureClasses` classifies features and gives their styles (widths in
  metres). `MapDataOSM` projects through `Game::GeoCoordConverter`; the map
  does the same, so OSM lines up with the route's tiles.
- **Zoom** runs from 0.02 to 500 m/px; at 500 m/px a 1920-pixel view spans
  about 960 km, so the national level is used.

## What a view holds (Warsaw, Poland file)

| View width | Way points (all) | Filled polygons | Their vertices | Points on stroked ways |
|---|---:|---:|---:|---:|
| 4 km | 0.66 M | 23 k | 0.40 M | 0.10 M |
| 10 km | 2.5 M | 100 k | 1.36 M | 0.46 M |
| 38 km | 13 M | 730 k | 8.0 M | 1.9 M |

- Reading is not the limit: 0.09 to 0.32 s cold, 0.01 to 0.15 s warm.
- Filled areas (mostly buildings) hold 3 to 4 times the points of stroked ways.
- A 1920-pixel view at 20 m/px (the current regional switch) is 38 km wide.
  The detail level there is far over any sensible budget; the per-style ranges
  below fix that.
- Multipolygon assembly took 0.24 to 0.33 s, more than the read at 4 to 10 km.

## Triangulation: earcut.hpp

Measured on all filled areas of the views above, one thread:

| View | Vertices | earcut | Rate | Convex polygons fanned instead |
|---|---:|---:|---:|---:|
| 4 km | 0.40 M | 0.039 s | 10.2 M vertices/s | 0.038 s |
| 10 km | 1.36 M | 0.109 s | 12.4 M/s | 0.105 s |
| 38 km | 8.0 M | 0.563 s | 14.2 M/s | 0.546 s |

- It is built for speed: Mapbox uses it to triangulate vector tiles while
  rendering. Ear clipping with z-order hashing for larger polygons.
- Every polygon gave triangles, holes included.
- A cheaper path for simple shapes gains nothing: fanning the convex third
  of the polygons saved 3%. So there is no performance reason to accept edge
  cases; earcut it is.
- Polygons are independent, so the work splits across threads.
- Vendored as `src/earcut/earcut.hpp` with its licence, upstream commit
  `177bd66` (2026-09-28).

## Design

### 1. Layer order and heights

From the bottom:

1. **Terrain textures**: distant, detailed, procedural, map overlays
   (heights 10 to 35, unchanged).
2. **OSM** (heights 50 to 79): per draw slot (bridges and OSM `layer`, as the
   tile map orders them), in each slot fills, outlines, casings, lines.
3. **Terrain aids**: missing-tile tint, quadtree lines, tile borders, the
   quadtree tool's highlight. Moved above OSM so the quadtree and tile tools
   stay usable with OSM shown.
4. **Faded Overlay** (height 90): the background colour over everything below,
   at the palette's alpha. Drawn by `paintMap` rather than the terrain layer,
   so it also works with terrain hidden.
5. **Route data**: roads 100, track 200 and the rest, unchanged.

Renames:
- `MapLayer::FadedTerrain` becomes `MapLayer::FadedOverlay`, with the menu text
  "Faded Overlay".
- The palette value `terrainFade` becomes `overlayFade`. Custom palette files
  that still name `terrainFade` keep working.

### 2. Scale ranges per style

- **A style gets `maxMetersPerPixel`**: drawn at this resolution and finer, at
  every resolution when absent. The map layer skips features whose style is
  out of range before projecting them.
- **Starting values**, to be tuned against the vertex budget below:
  - buildings: 2.5 m/px;
  - footways, paths, cycleways, steps, service roads, tracks: 5 m/px;
  - small landuse and leisure areas: 5 m/px.
- **The overview switch** (`overview.levels[].fromMetersPerPixel`) is set for
  the map, the only `forScale` user; the tile map always reads detail. With
  buildings out of range, regional may move from 20 to about 10 m/px; to be
  measured.
- **Budget**: about 1 to 2 M line segments and 2 M fill triangles a view,
  which keeps a build at a few hundred milliseconds and the vertex buffers
  under about 100 MB.
- **The 2D tile map** ignores the ranges: it draws a fixed 4 km tile at
  0.5 m/px, where everything is shown.
- `gen_osm_map_classes.py` writes the starting values; the table stays
  hand-editable.

### 3. Geometry

- **Source**: `OsmLayers::forScale(view.metresPerPixel)` over the view's
  ground rectangle plus a margin, converted to a lat/lon box.
- **Projection**: through `Game::GeoCoordConverter` into the view tile's
  coordinates, as `MapDataOSM::project` does. A route without a geographic
  reference has no OSM layer, and its menu entry is disabled.
- **Areas**: closed ways with a fill, and assembled multipolygons, triangulated
  by earcut. Triangles do not depend on the scale.
- **Lines**: width = max(style width in metres, the style's minimum in pixels
  times metres per pixel).
  - Lines of about one pixel are drawn as line primitives: two vertices a
    segment instead of six.
  - Wider ones are quads, with round joins so bends of thick roads have no
    gaps.
  - Casings and outlines are the same, wider or under.
- **Small and fine detail**: features under 2 pixels across are left out,
  and geometry is simplified to half a pixel before projecting (step 4
  measurements: every view 6 to 39 MiB of vertices instead of up to
  159 MiB).
- **Points** (stations, places) wait for labels: a marker without a name says
  little.
- **Batches**: one `OglObj` per slot, style and part (fill, outline, casing,
  line) that has geometry: the class table's colours, about 100 to 300 draws.
- **A geometry builder separate from OpenGL** (`Osm::MapGeometry` in
  `src/tsre/geo/osm`): store, box, scale and a projection function in, vertex
  arrays per batch out. It is tested in `tests/osm` without the app.

### 4. Building in the background

- **A worker thread runs the build**: query, classify, project, assemble,
  triangulate, and build lines. The GUI thread only uploads finished arrays
  and pushes them. The previous geometry stays on screen until the new one is
  ready.
- **A generation counter** drops results the view has since moved past.
- **Zoom-only steps** reuse the last build's projected polylines and
  triangles and rebuild only the line widths; fills are scale independent.
- **Pan rebuilds** happen when the view leaves the built rectangle. The
  margin is half the view each side, so ordinary panning does not wait.
- **Assembled multipolygons** are cached by relation id between builds,
  given their measured cost.
- **One store**: the map layer and the 2D tile map share a single store, so
  they share one block cache. Today `MapDataOSM` holds its own static store.

### 5. Transparency and colours

- **Map > Transparent OSM Areas** (off by default): fills are drawn at the
  palette's `osmAreaAlpha` (default 0.5); lines and outlines stay opaque. The
  user switches it.
- Overlapping transparent fills (a building on a residential area) blend
  twice. A whole-layer opacity would need an off-screen pass; it is not part
  of this work.
- **Colours** come from the class table, which uses light, OSM-style colours.
  A dark style for the dark palette is a later addition; until then the
  Faded Overlay toggle tones OSM down.

### 6. Menu, settings and conversion

- **Map > OSM Data** shows the layer; off by default. **Map > Transparent OSM
  Areas** sits under it.
- **No OSM directory set**: switching the layer on says so and points to the
  setting `core.paths.osmData`.
- **Unconverted files** covering the view: the conversion prompt
  (`Osm::ensureConverted`) is shown when the layer is switched on, never while
  panning. Overview levels that are missing or stale are rebuilt there too.

### 7. Later: a dedicated line shader

Requested by the user for a later step, with less memory as the goal:

- **Now, CPU quads**: about 72 bytes a segment (six vertices of 12 bytes),
  rebuilt on every zoom step.
- **Shader-expanded segments**: each segment drawn as one instance of a quad,
  with its two end points, width and colour index as instance data. About
  16 to 20 bytes a segment, 3.5 to 4.5 times less, and nothing is rebuilt
  when zooming.
- **Polylines read from a buffer by the shader**: each point stored once, about
  8 bytes, 6 to 9 times less.
- **Colour index**: all styles in one draw, through a colour table in the shader.
- **Needs** GPU instancing or buffer reads in both renderers. Neither has them
  today: the renderer's instances are CPU-side draw packets. This is renderer
  work in its own right, so it comes after the CPU version works.

### 8. Out of scope here

- Labels: place, station and street names. They need the map's glyph-atlas
  text, which is planned for the map mode.
- Selecting OSM features: later, for the procedural tools, through the
  selection pass.
- OSM in the 3D view, draped on terrain.

## Steps

1. **Layer order**:
   - move the fade to `paintMap` as Faded Overlay;
   - rename the toggle and the palette value, reading the old key too;
   - new height constants, with the terrain aids above the OSM band;
   - map-view suite and captures unchanged apart from the order.
2. **Vendor earcut**: `src/earcut/earcut.hpp` with its licence; record it
   where miniz is recorded.
3. **Scale ranges**:
   - `maxMetersPerPixel` in the styles, `FeatureClasses` and the generator;
   - tests in `tests/osm`.
4. **Geometry builder**: `Osm::MapGeometry`:
   - fills, lines, joins, line primitives, batches;
   - tests with synthetic data;
   - a timing mode on the Warsaw views.
5. **Map layer**:
   - `OsmMapLayer` in `src/tsre/map`: worker, generation counter, upload;
   - the Map menu entries, translations, conversion prompt;
   - one store shared with `MapDataOSM`.
6. **Tuning and checks**:
   - measure builds and memory on Warsaw and a whole-Poland view;
   - tune the ranges and the regional switch;
   - opt-in captures with local data; docs (`features/osm-data.md`, map-mode
     task).
7. **Later**: the line shader, labels, selection.

## Verification

- `tests/osm`: scale ranges; geometry (triangle counts, holes, join shapes,
  width switching, slot order).
- **App suite `map-view`**:
  - layer heights and order;
  - Faded Overlay over OSM;
  - the old `terrainFade` key in a palette file.
- **Captures**: map views over a route with a geographic reference and local
  OSM data. Opt-in, as the data is not in the repository.
- **Manual**: a route in Poland, zooming from a station to the whole country,
  OSM over terrain with and without transparency and fade.

## Implementation status

| Step | Commit | Notes |
|---|---|---|
| 1. Layer order | `f499ac1` | Faded Overlay (`MapOverlayFade`, height 90); terrain aids moved to 81 to 87; USA2 map-terrain captures match main. |
| 2. earcut | `9c68474` | `src/earcut/earcut.hpp` with its licence and a README. The About window credits it and shows the licence (user, 2026-10-08); a release notices file is for later. |
| 3. Scale ranges | `56d68fa` | `maxMetersPerPixel` in styles; ranges 2.5, 5 and 10 m/px. Unstyled ways (the default style) got 5 m/px with step 4. |
| 4. Geometry | `8c440ac` | `Osm::MapGeometry`; measurements in the evidence. |
| 5. Map layer | `5fd7cd1` | `OsmMapLayer`, `Osm::sharedLayers`, menu, prompt, captures. |

What changed from the design while building:

- **Small and fine detail** (step 4): culling under 2 pixels and
  simplification to half a pixel were added; without them the views took up to
  159 MiB. Only closed ways and areas are culled: an open way may be a short
  piece of a long road (the national view showed main roads dashed).
- **Points** wait for labels.
- **Assembled multipolygons are not cached** between builds yet. In the app
  they cost 1 to 26 ms at detail scales and 136 ms of an 0.87 s national
  build; the cache stays open until it matters.
- **Built area**: the view's bounding box (rotated views included) plus a
  quarter of its size each side. The geometry is relative to the tile it was
  built for and drawn shifted to the view's tile.
- **One store** (`Osm::sharedLayers`) serves the layer and the tile map; it
  reopens when the converted or overview files change.

Measured in the app on TEST_PROFILES (Transverse Mercator, 50.77 N 16.30 E),
1280 x 800 at pixel ratio 1.5, the route's own projection, OpenGL (llvmpipe):

| View | m/px | File | Build | of it read (with projection) | Triangles | Polylines |
|---|---:|---|---:|---:|---:|---:|
| close | 1 | detail | 0.10 s | 0.09 s | 15 k | 5 k |
| town | 4 | detail | 0.11 s | 0.10 s | 31 k | 11 k |
| detail-wide | 15 | detail | 0.46 s | 0.37 s | 194 k | 13 k |
| regional | 40 | regional | 0.35 s | 0.25 s | 194 k | 25 k |
| national | 300 | national | 0.87 s | 0.53 s | 245 k | 122 k |

- Captures: `tests/renderer/map-osm.json` (opt-in, needs
  `--set core.paths.osmData=<converted Poland files>`). The harness waits while
  a map layer builds (`RouteEditorGLWidget::mapLayersBusy`) and takes the view
  keys `osmData` and `osmTransparentAreas`.
- QRhi (Vulkan, llvmpipe) against OpenGL: under 1% of pixels differ in every
  view, all of them one-pixel lines and tile borders (rasterization rules).
- Not checked here: the conversion prompt from the menu (the dialog itself is
  covered by the `osm-data` suite), and a real GPU.

### Forests at country scale (user report, 2026-10-08)

"Half of the forests disappear on the country size view", for example around
Piła. Measured in a 1 x 0.5 degree box around Piła (detail file): 1,737 km2
of forest in 10,353 closed ways and 52 multipolygons, but 90% of it in
parcels of 0.05 to 1 km2 (forestry compartments). The national level kept
areas of 1 km2 or more one by one: 8% of the forest there. Across Poland
the parcels and small woods under 1 km2 are 35% of 110,346 km2.

- **Merging parcels by shared borders** (same node ids) would bring Piła to
  89%, but Poland-wide only from 65 to 67%: most small woods share no nodes.
- **Chosen: generalization** (`OsmGeneralize`): the national rule for
  `landuse=forest` and `natural=wood` draws them on a 100 m grid, closes
  gaps of up to about 200 m, traces the outlines with their holes (clearings
  from 0.05 km2) and simplifies them; merged forests from 0.25 km2 are written
  as new `landuse=forest` areas with negative ids (per file and level, so
  overlapping files never drop each other's). Rule key `generalize`
  (`cellMeters`, `closeMeters`, `minHoleKm2`, `tag`).
- **Result**: the national level holds 121,122 km2 of forest for Poland (97%
  of it in areas of 1 km2 or more), about 10% more than the detailed data
  because of the closed gaps; the file shrank from 20 to 11.6 MB. Building
  the Poland overviews takes 16.6 s (12.5 s before; load 4), peak memory
  4.4 GB, under the conversion's own peak.
- **Drawing**: the large merged forests with many holes triangulate more
  slowly: a national view takes 0.9 to 1.5 s to build (earcut 0.4 s of it),
  0.87 s before. Splitting very large polygons before triangulating would
  help if it matters.
- Captures `pila-regional`, `pila-national` and `poland` in
  `tests/renderer/map-osm.json` (views centred by `latLon`).
- Water stays exact (lakes are single polygons); riverbank pieces under
  1 km2 are still left out at the national level.

### Country-scale speed (user, 2026-10-08)

After the forest generalization a country view took 0.9 to 1.5 s to build.
Measured on TEST_PROFILES (route projection, llvmpipe), in four steps:

| Step | Commit | Piła national (160 m/px) | Poland (450 m/px) |
|---|---|---:|---:|
| Before | `be638e3` | 0.94 s | 1.51 s |
| 1. Generalized areas cut into 25.6 km blocks | `30f4fb5` | | |
| 2. Multipolygons from the ways already read | `db7b495` | | |
| 3. Simplify, project, triangulate on all threads | `dfc9779` | 0.31 s | 0.60 s |
| 4. Coarsest level loaded whole, in 128 km chunks | `68d07da` | 0.30 s view, then 0.70 s whole | 0 when panning; 0.58 s after a 2x zoom |

- **Step 1**: the largest merged forest had 68,807 points and 2,662 holes
  (150 ms of earcut). In blocks, triangulation fell from about 0.3 to 0.08 s
  and assembly from 0.14-0.25 to 0.02-0.04 s (smaller relation extents).
  Pieces meet exactly: points on block lines stay through simplification;
  small areas are judged whole before cutting.
- **Step 2**: relations are read first; the ways pass keeps their members;
  only members outside the area are read again, skipping blocks wholly
  inside it (`Filter::readAlready`). Mostly helps detail views (Warsaw at
  19 m/px: 0.19 -> 0.12 s).
- **Step 3**: reading only classifies and copies; contiguous shares go to
  worker threads and merge in order (same result on any thread count,
  tested). Also halves regional and detail builds (0.35-0.49 -> 0.16-0.22 s,
  0.46 -> 0.23 s at 15 m/px).
- **Step 4**: the coarsest level (national, 12 MB for Poland) loads the
  view first, then the whole level in the background; panning at that scale
  never loads again, and a zoom step of 2x reloads it whole at once (the old
  picture stays). Batches are split by 128 km squares so the renderer culls
  those off screen: without that the whole level cost 275 ms a frame on
  llvmpipe (1.24 M primitives); with it 84 to 125 ms, less than the views
  alone before (98 to 162 ms).
- **This machine renders in software** (ASPEED, no 3D GPU, llvmpipe over
  Chrome Remote Desktop), so primitives and filled pixels set the frame
  time here; on a GPU these frames are trivial.
- Captures against the forest commit: national views differ in 0.1-0.4% of
  pixels (block cuts, simplification); the rest are identical.
- Not done: the detail file's read at 10-20 m/px (1 s at 19 m/px over 55 km
  of Warsaw), which the regional switch at 20 m/px bounds.

### Worker threads on Windows (user report, 2026-10-09)

On the Steam Deck (Windows, MinGW build) converting a province took minutes
and the editor then crashed; on the Linux server all was fine.

- **Cause**: a `std::thread` that used a QObject (the converter's workers
  open a `QFile` each) crashes in Qt 6.10's per-thread cleanup when it ends
  (`Qt6Core.dll+0x255fbc`, reading the thread's data after it was cleared).
  Windows writes a crash report for each, about a second, and lets the
  program go on; after some 20 of them the heap was corrupted (`c0000374`).
  A ten-line program with no TSRE code shows the same; with `QThread` it
  does not.
- **Fix**: every OSM worker runs on `Osm::Thread` (`OsmThread.h`), a
  `std::thread` look-alike over `QThread::create`: the converter, the
  conversion dialog's worker, overview builds, block reads, map geometry,
  the map layer, the tile map's quadrants and the OSM tests.
- `tsre_osm_tests` on the Deck: 20.8 s and 20 crash reports before, 9.6 s and
  none after.
- Two overview tests failed on Windows, before and after: they deleted an
  overview file that open layers still had mapped, which Windows refuses.
  The tests now close their layers first; on Windows the check that users
  of old shared layers keep them is left out, as there the file cannot go
  while they do. The editor never meets this: layers leave out stale
  overviews, and only stale or missing ones are rebuilt; conversion writes
  only files that are not converted yet.

### Labels (user, 2026-10-09)

Agreed: no glyph atlas yet. The 3D view's `TextObj` paints each label into
its own texture 16 characters wide (512 x 32 RGBA, 64 KB, clipping long
names), on the GUI thread, drawn one quad and one texture a label, sized in
metres: fine for a few 3D markers, not for map names at a fixed screen size.
Instead (`MapLabelLayer`, `MapLabelAtlas`):

- Whole names painted once by QPainter (fonts, Polish letters, shaping by
  Qt) into atlas pages, one mesh a page; quads at whole pixels, upright,
  rebuilt when the view changes (a few hundred names, about 1 ms).
- Placement by priority with four positions around the dot; overlapping
  names left out.
- First user: the marker set (Map > Markers). Country Places now store the
  GeoNames feature code and population for ranking.
- Measured on TEST_PROFILES with Polish Country Places (363 places): all of
  Poland places 152 names, 21 ms the first time (font set-up included), a
  region 49 names, 5 ms with new names, 1 ms without.
- Route data (2026-10-09): station, platform and siding names and location
  events join the same layer (see the map-mode task).
- **OSM names** (2026-10-09): nodes tagged `place` (city, town, village,
  hamlet, suburb or quarter) or `railway` (station, halt) with a `name`, read
  by `Osm::MapGeometry` with the geometry (a keys filter over the node blocks;
  converted files keep tagged nodes only) and handed to the label layer by
  `OsmMapLayer::labels()`.
  - Ranked below the route's names and any marker set shown (Map > Markers,
    off by default, is turned on for a purpose): city (bold), town, station,
    village, halt, suburb, hamlet, each by its `population` tag;
    shown from 200 m/px for towns, 60 stations, 40 villages, 20 halts,
    15 suburbs, 10 hamlets, cities always. The overview levels hold what each
    scale needs (national: cities and towns; regional: villages and
    stations too).
  - A name already placed within 120 pixels is not placed again, so a town in
    both the Country Places and OSM shows once (the marker's). Different
    spellings (GeoNames "Warsaw", OSM "Warszawa") are not matched; overlapping
    sources are not a concern (user, 2026-10-09).
  - Palette colours `place` and `osmStation` for the dots.
  - Captures (`map-labels.json`, osm-* views): Poland's cities at 450 m/px,
    Kraków's towns and stations at 60 m/px, its districts and every station
    at 12 m/px. A build with new names took up to 11 ms (257 candidates),
    1 to 2 ms without.
- Street names along roads would need a glyph atlas (QTextLayout shaping,
  QRawFont glyph images) and are left for later.

## Open items (2026-10-09)

In rough order of what the user asked about:

1. **The user's test on a GPU** (Windows, Steam Deck): frame rate at country
   scale, transparency and the fade, both renderers, the labels.
2. **Merge the labels to `main`** after that test.
3. **KML marker files need redoing** (`CoordsKml`, the 3D marker lines); the
   findings and a first example file are in the map-mode task
   ([04-map-mode.md](../editor/04-map-mode.md), "KML marker files need
   redoing"). A real development-project file is wanted as test data.
   With it: draw KML lines and areas on the map, rank custom files from
   `ExtendedData` (`priority`, `population`).
4. Done 2026-10-09: the "Route: Stations" / "Route: Sidings" marker sets
   (`CoordsRoutePlaces`) mirrored their markers within the tile (database z
   not negated) since the repository's first commit; fixed (a 3D capture over
   Carlisle shows the pole on the platform; its 3D name label did not show,
   the old `TextObj` path, not looked into). The map leaves these sets out: it
   labels the same names from the database.
5. **Selecting OSM features** on the map, the start of procedural tools that
   use OSM data (for example track along an OSM railway). Through the
   existing selection pass (IDs drawn as colours), user's question
   2026-10-09:
   - IDs are 32 bits, 5 for the kind and 27 of payload; OSM way IDs are
     larger, so a new kind (`OsmFeature`) carries an index into a table of
     the features drawn for that pass, which maps back to type and ID.
   - Drawing every feature in the view with its own ID would be one draw
     each (the OSM batches mix features), so the pass draws only the
     features near the pointer: a small query (the layer's projected
     polylines and fills, or the store) builds per-feature shapes into
     `MapSelection`, lines widened by its margin as markers are now.
   - The result goes to the select tool as other picks do; a later tool
     (follow this railway) reads the feature's geometry from the store.
6. **Street names along roads**: a glyph atlas (QTextLayout shaping, QRawFont
   glyph images) placing letters along lines.
7. **Line shader** (instanced segments): 3.5 to 9 times less line memory, no
   rebuild on zoom; needs instancing in both renderers.
8. Smaller: a dark style for the OSM colours (now toned by the palette's fade
   only), an option to start with OSM Data on, the multipolygon cache (little
   gain since the speed work), the detail read just below the regional switch
   (about 1 s at 19 m/px over a big city; or move the switch to about
   10 m/px), names in a chosen language (GeoNames vs OSM spellings).
9. Map mouse buttons changed (user, 2026-10-09): left and right drag move
   the map, middle drag turns it, so a right click for a context menu never
   turns it. (Terrain painting on QRhi, suspected from the code, works: the
   user tested static and procedural painting.)
