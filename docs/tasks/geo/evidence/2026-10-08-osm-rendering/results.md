# OSM rendering measurements, 2026-10-08

Release build, 12-thread machine, load average 2.4-2.7 (other sessions).
Poland converted file (`poland-261006.tsre.osm.pbf`), views centred on Warsaw
(52.23 N, 21.01 E), each run a fresh process (cold block cache).

## Detail file query (`tsre_osm_tests --query`, all features)

| View | Nodes | Ways | Way points | Relations | Cold | Warm |
|---|---:|---:|---:|---:|---:|---:|
| 4 km | 92,681 | 65,844 | 661,237 | 2,295 | 0.093 s | 0.010 s |
| 10 km | 263,417 | 275,485 | 2,476,128 | 4,882 | 0.151 s | 0.018 s |
| 20 km | 510,243 | 757,681 | 6,688,843 | 8,433 | 0.214 s | 0.055 s |
| 38 km | 733,532 | 1,452,414 | 12,997,437 | 11,680 | 0.317 s | 0.150 s |

## Filled areas and triangulation (`earcut_bench`)

Read includes classification with the standard class table. Filled polygons:
closed ways with a fill style plus assembled multipolygons. Times are the
second of two passes, one thread.

| View | Read + classify | Multipolygons | Filled polygons | Vertices | Holes | Points on stroked ways | earcut | Convex fan + earcut |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 4 km | 0.12 s | 0.33 s (797) | 23,162 | 398,863 | 1,402 | 104,396 | 0.039 s (10.2 M vertices/s) | 0.038 s |
| 10 km | 0.23 s | 0.27 s (1,708) | 99,861 | 1,358,376 | 2,885 | 456,839 | 0.109 s (12.4 M/s) | 0.105 s |
| 38 km | 0.80 s | 0.24 s (4,586) | 729,700 | 8,025,604 | 7,907 | 1,933,159 | 0.563 s (14.2 M/s) | 0.546 s |

- earcut returned triangles for every polygon (no empty results).
- About a third of the polygons are convex; fanning them instead saves 3%.
- Filled areas (mostly buildings) hold 3-4 times the points of stroked ways.
- Multipolygon assembly costs more than the read itself at 4-10 km.

## Map geometry builds (`tsre_osm_tests --map-geometry`, step 4)

`Osm::MapGeometry` on Warsaw, the area a 1920-pixel view plus 25% margin on
each side covers, from the file `OsmLayers::forScale` picks. Second of two
passes (cached blocks). Local equirectangular projection, so the app's own
projection adds to "read". Load average 1.3-1.9.

| m/px | Area | File | Load | of it read / multipolygons / earcut | Strokes | Triangles | Polylines (points) | Culled | Vertex memory |
|---:|---:|---|---:|---|---:|---:|---|---:|---:|
| 1 | 2.9 km | detail | 0.058 s | 0.044 / 0.004 / 0.009 | 0.006 s | 83 k | 31 k (171 k) | 3 k | 7.6 MiB |
| 2.4 | 6.9 km | detail | 0.19 s | 0.15 / 0.009 / 0.028 | 0.013 s | 243 k | 111 k (522 k) | 24 k | 20.6 MiB |
| 4.9 | 14 km | detail | 0.37 s | 0.31 / 0.018 / 0.036 | 0.013 s | 315 k | 194 k (694 k) | 124 k | 27.1 MiB |
| 9.9 | 29 km | detail | 0.75 s | 0.57 / 0.11 / 0.061 | 0.002 s | 517 k | 48 k (149 k) | 76 k | 21.0 MiB |
| 19 | 55 km | detail | 1.20 s | 0.89 / 0.19 / 0.10 | 0.001 s | 880 k | 34 k (104 k) | 152 k | 32.3 MiB |
| 20 | 58 km | regional | 0.095 s | 0.052 / 0.007 / 0.028 | 0.001 s | 149 k | 20 k (52 k) | 12 k | 6.3 MiB |
| 60 | 173 km | regional | 0.25 s | 0.13 / 0.022 / 0.075 | 0.001 s | 456 k | 26 k (80 k) | 40 k | 17.6 MiB |
| 149 | 429 km | regional | 0.84 s | 0.45 / 0.14 / 0.14 | 0.003 s | 980 k | 76 k (229 k) | 217 k | 39.1 MiB |
| 150 | 432 km | national | 0.29 s | 0.11 / 0.051 / 0.086 | 0.001 s | 449 k | 24 k (95 k) | 62 k | 17.4 MiB |
| 500 | 1440 km | national | 0.40 s | 0.20 / 0.11 / 0.034 | 0.001 s | 207 k | 19 k (60 k) | 158 k | 8.2 MiB |

How it got there:

- First version: the same views took up to 159 MiB (19 m/px) and 4.1 M
  triangles, and an exact `reserve` per polygon made triangulation quadratic
  (1.2 s at 1 m/px, 50 s at 4.9 m/px).
- Leaving out features under 2 pixels across cut the strokes; simplifying to
  half a pixel (Douglas-Peucker, `simplifyIndices`) before projecting cut the
  fills 2 to 6 times, so every view is now 6 to 39 MiB.
- Lines dominate up to 5 m/px (one-pixel outlines and minor ways), fills
  beyond. Strips (wide strokes) appear only below about 3 m/px.
- Reading the detail file is what grows at 10 to 19 m/px (all features of a
  30 to 55 km area are decoded); the regional level reads 10 times less. The
  switch stays at 20 m/px for now: below it the detail file still shows small
  landuse areas that the regional level drops.
