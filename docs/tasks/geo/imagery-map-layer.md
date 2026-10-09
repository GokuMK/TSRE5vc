# Imagery in map mode

Created 2026-10-09 on `feature/osm-rendering`. Map > Imagery draws aerial
imagery between the terrain and the OSM data, from the imagery catalogue's
default world source (ESA WorldCover Sentinel-2 RGB 2021, a Web Mercator WMTS).
Load Imagery ([imagery-source-implementation.md](imagery-source-implementation.md))
stays the way to make a terrain tile's overlay image; this layer only shows the
imagery.

## Decisions

- **Source.** The catalogue's `defaultDistantSource`, if it is a keyless
  `wmts-kvp-webmercator` world source; otherwise the first such source.
  Regional sources (Poland's ArcGIS export, WMS) and a source setting can come
  later.
- **Download tile size: the service's own 256-pixel tiles.** The WMTS offers
  no other size. Terrascope's WMS (any size) failed from here (HTTP/2 stream
  errors, no capabilities over HTTP/1.1), and it is not in the catalogue.
  Tiles also cache well: the same files as Load Imagery
  (`cache/imagery/<directory>/<revision>/EPSG_3857/z/x/y.png` under
  `core.paths.geoData`), so either fills the cache for the other.
- **Requests: 16 in flight**, on one HTTP/2 connection as a web map viewer's.
  Measured: 16 fresh tiles took 1.9–3.6 s four at a time, 1.2 s eight and
  0.75 s sixteen (curl). 36 fresh tiles through `fetchTiles` took 3.3 s eight
  at a time and 1.7 s sixteen. The service's first render of a tile dominates
  (0.2–2 s a tile) and varies: one fresh area took 32 s at eight, then 0.4 s
  when fetched again. Load Imagery keeps four.
- **Zoom** follows the view's scale (`Imagery::chooseZoom`: the zoom whose
  ground resolution is nearest the metres per pixel), capped at the source's
  native 10 m. A coarser zoom is used when the view would need more than half
  the texture pages.
- **GPU tiles: texture pages of 2048 × 2048 holding 8 × 8 tiles.** A page is
  one draw; up to four pages (256 tiles, 64 MB RGBA), the tile drawn longest
  ago is replaced. Tiles are sampled half a texel inside their square, so
  neighbours in a page do not bleed in.
- **Projection.** Each tile is a mesh of 2 × 2 cells (zoom above 10) up to
  16 × 16 (zoom 4 and under), its points converted from latitude and longitude
  through the route's converter once and kept relative to a tile. Captures show
  OSM roads on the imagery's roads at 10 m/px (Wałbrzych, TEST_PROFILES).
- **While tiles load**, a missing tile shows its four children (when zooming
  out) or the nearest loaded ancestor's part (up to eight levels up).
- **Attribution** (CC BY 4.0): the source's name, attribution and licence in a
  label at the bottom right corner while the layer shows.
- **Off by default**: it downloads from a web service.

## Implementation

- `Imagery::fetchTiles` (`src/tsre/geo/ImagerySource`): the tile loop of Load
  Imagery, made public and shared. It reads the persistent cache, else
  requests the tile; retries, validation and the cache write are the same.
  Requests run in a sliding window (a new one starts as soon as one ends, not
  in waves of four). Tiles are asked for by a callback, so the caller can
  change what it wants while tiles load. `Imagery::generate` uses it for WMTS
  and static-map sources.
  - Checked against the code before (scratch benchmark of `generate`,
    WorldCover, a 16 km terrain tile at 4096 pixels, 36 tiles): the same image
    (same hash) and byte-identical cache files. Cached: 0.94–0.97 s both.
    Fresh (areas alternated; network variance is large): before 5.2, 21.8,
    5.2, 23.5 and 10.0 s; after 3.7, 4.7, 3.6, 22.7, 5.3 and 5.3 s. The geo suite passes (455 checks).
- `ImageryMapLayer` (`src/tsre/map/ImageryMapLayer`): a fetch thread
  (`Osm::Thread`) running `fetchTiles` with the missing tiles, nearest the
  view's centre first. The newest list replaces the waiting one, and the
  connection is kept 30 s after the last tile. The layer draws at height 40,
  between the terrain (10–35) and the OSM band (50–79). A failed tile is asked
  for again after 30 s; the first error goes to the log.
- `Texture::updateRegion`: replaces part of an RGBA texture on either
  renderer (`glTexSubImage2D`, or `RhiTextures::updateRegion`).
- Capture key `imagery`; `tests/renderer/map-imagery.json` (TEST_PROFILES:
  1, 10, 60 and 450 m/px, turned, with OSM data, with Faded Overlay). QRhi
  against OpenGL: under 0.2% of pixels differ, in the views with OSM labels.
  Cached views settle in 0.4–0.8 s on llvmpipe; a frame's tile choice takes
  about 0.2 ms.

## Open items

- WorldCover has a black first pixel row in its zoom 8 row 81 tiles (about
  53.3° N), a line across Poland at country scale. It is in the service's
  tiles, so Load Imagery has it too.
- The tiles are PNG, 64–180 KB each. The service also returns JPEG
  (20 KB, not advertised in its capabilities). Switching the catalogue entry
  would make the cache about seven times smaller, but Load Imagery's terrain
  textures would then be made from JPEG.
- A source choice (regional orthophoto where available), and sources with
  keys.
- Texture pages are not freed when the layer goes (as the label atlas):
  `TexLib::delRef` does not delete GL textures.
