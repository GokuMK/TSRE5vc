Draw-cost breakdown for the current `MapDataOSM::draw()` styles, 2026-10-08.
`osmbench.cpp` here is the 2026-10-07 tool plus draw variants: build it like
`../2026-10-07-osm/build.sh` does, after generating `draw_tail_v.inc` from
`draw_tail.inc` by replacing `gg->fillPath(path, *brush);` with `DRAW_FILL;`,
`gg->drawPolyline(ww);` with `DRAW_LINE;` and `gg->drawPath(path);` with
`DRAW_OUTLINE;`. Variants: -DPOLYFILL, -DNOFILL, -DNOSTROKE; env IMGFMT=32
selects QImage::Format_RGB32 instead of RGB888.

Best of 3, warm, 4096 px, pomorskie grid cache, load avg ~1.5:

| Variant | Gdańsk centre | Tczew |
|---|---:|---:|
| current (fillPath, RGB888) | 0.302 s | 0.177 s |
| current, RGB32 | 0.198 s | 0.097 s |
| drawPolygon instead of fillPath, RGB888 / RGB32 | 0.295 / 0.197 s | 0.174 / 0.106 s |
| no fills, RGB888 / RGB32 | 0.246 / 0.160 s | 0.134 / 0.082 s |
| no strokes, RGB888 / RGB32 | 0.072 / 0.064 s | 0.047 / 0.041 s |
| nothing drawn (clear + projection only) | 0.025 / 0.027 s | 0.016 / 0.018 s |

Gdańsk tile, RGB32: one 4096 px tile on 1 thread 0.211 s; the same area as
4 quadrants of 2048 px: 0.24 s on 1 thread, 0.09 s on 4 threads.
