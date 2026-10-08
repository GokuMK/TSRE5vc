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
