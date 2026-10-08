# OSM data in the map mode: design and implementation plan

Branch `feature/osm-rendering`, from `main` at `0ef878b` (2026-10-08). It builds
on the local OSM data ([design](osm-data-design.md)) and the Route Editor's map
mode ([task editor 04](../editor/04-map-mode.md)). The measurements are in
[evidence/2026-10-08-osm-rendering](evidence/2026-10-08-osm-rendering/results.md).

Status: design agreed with the user on 2026-10-08. No code yet.

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
- **Points**: railway stations and halts as small markers. Place names wait
  for labels.
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
