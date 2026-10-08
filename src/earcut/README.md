# earcut.hpp

Polygon triangulation (ear clipping with z-order hashing) from Mapbox,
<https://github.com/mapbox/earcut.hpp>, upstream commit `177bd66`
(2026-09-28). ISC licence, see `LICENSE`. Vendored unchanged as a single
header, as `src/mzip/miniz` is; include it as `<earcut/earcut.hpp>`.

Used to fill OSM areas in the map mode
(`docs/tasks/geo/osm-rendering-design.md`, where it was measured at 10 to
14 M vertices a second on OSM data).
