# Task 51 - The near shadow map in tiles

Status: design only (2026-11-24, the user's idea; review on
`feature/car-interiors`, nothing built).

## Problem

The near shadow map gives the track its fine self-shadows (rails, chairs,
sleepers), but it ends about 90 to 100 m ahead of the camera, where the
middle map takes over with texels 7 times larger: a clear line where those
shadows stop. Stations are 300 m and more.

Today's maps (`ShadowHalfExtent`, `ShadowHalfDepth`, `ShadowLead` in
`RouteEditorGLWidget.cpp`):

| Map | Square across the sun | Depth along the sun | Centred ahead | Texel (4096 / 2048 / 1024) |
| --- | --- | --- | --- | --- |
| Near | 160 m | ±200 m | 16 m | 3.9 cm |
| Middle | 600 m | ±600 m | 60 m | 29 cm |
| Far | 1,400 m | ±700 m | 140 m | 1.4 m |

- A point takes the near map within an ellipsoid 80 m across the sun and
  200 m along it, then the middle, then the far. On the ground at a low sun
  the square stretches along the sun's way: 160 / sin(the sun's height).
- The size settings change the resolution only, not the extent: a larger
  near map is sharper over the same 160 m.
- The static maps (caches) are drawn again when the camera has moved its
  lead from where they were drawn (16, 60, 140 m), the view turned 30
  degrees, the sun 0.1, every 4 s, when their content changes, and all
  three in a new tile (2,048 m); one a frame, the oldest first. At 120
  km/h: the near map every 0.48 s, the middle every 1.8 s, the far every 4
  s (the 4 s refresh first). Checked on bbb's circle at 92 km/h
  (`TSRE_SHADOW_CACHE_LOG=1`): 0.63, 2.37 and 4.03 s.
- The dynamic maps (near, middle, far, half the size by default) are drawn
  every frame with what moves; the middle and far leave out what a nearer
  map's ellipsoid holds (task 48), and their lookups are skipped where
  nothing moves (task 50's cells).

## The idea (the user, 2026-11-24)

The static maps are already fixed to a grid: make the near one tiles. A
window of near-density tiles around and ahead of the camera, 3 x 2 or 3 x
3; moving forward draws a new row (at most 3 tiles), the rest stays. Then
perhaps drop the near map and enlarge the far one.

## Design

- **The grid:** squares fixed in the sun's view (its orthographic axes),
  not on the ground: a tile never moves with the camera. All tiles share
  the sun's direction, so one light-space transform serves them all.
- **Storage, two ways:**
  - One depth texture holding the tiles side by side (an atlas), each
    tile drawn into its own viewport. The shaders keep the hardware
    comparison (`sampler2DShadow`) and today's filter code.
  - A colour texture array (R32F), a tile a layer, as the lamp shadows
    do it (task 46): QRhi draws into no layer of a depth texture, so the
    depth goes into a colour layer, with one tile-sized depth buffer
    shared for the depth test while drawing. A tile is a slot (no packing),
    but the comparison is the shader's: `textureGather` gives the 2 x 2
    texels of one hardware comparison in one fetch, then four compares and
    the bilinear weights in ALU, for each of the 16 taps. The same fetches,
    more ALU and registers (see the risks).
  - Either way each tile is drawn with a border of a few texels (the
    filter's reach and the normal offset), so a tap near its edge reads
    its own tile, not a neighbour's slot or a clamped edge.
- **The lookup:** the point in light-space metres, then the tile (floor
  of it over the tile's size), a small table of the window's tiles (atlas
  place or layer, or none: the middle map), the texel. One map a point,
  as today.
- **Drawn again:** a tile entering the window; the sun turning 0.1 degree
  (every tile, one a frame: with the clock running about every 24 s); a
  change in that tile's content: per-tile signatures (today one a map:
  `shadowSignature`, `standingSignature(range)`), so a train stopping
  draws only the tiles it is in. The 4 s refresh to reconsider for tiles.
- **What moves:** the tiles hold the static casters. Moving trains keep
  today's dynamic near map (160 m around the camera, every frame), the
  dynamic middle map beyond it (59 cm texels at the default half size:
  coarse for a train at 150 to 300 m), or a dynamic map over the whole
  window (2048 over 480 m: 23 cm, every frame).
- **Vegetation** has caches of its own (task 37): tiles too, or the inner
  tiles only.
- **The edge:** no extra cascade between near and middle (the user). A
  fade over a band before the tiles end: there both the tiles and the
  middle map are read and blended by distance, so the line goes. The
  window's larger reach leaves room for a wide band. Two lookups only in
  the band.
- **The OpenGL renderer** keeps today's maps.

Sizes (D32F, a 2048 x 2048 tile 16 MB; today's near map at 4096 64 MB,
vegetation's as much again):

| Tile | Texel | 3 x 3 window | Reach ahead | Memory (vegetation's tiles twice) |
| --- | --- | --- | --- | --- |
| 80 m | 3.9 cm (a 4096 near map's) | 240 m | about 120 to 200 m | 144 MB |
| 160 m | 7.8 cm (a 2048 near map's, the default) | 480 m | about 240 to 400 m | 144 MB |

Drawing at 120 km/h with 160 m tiles: a row of 3 every 4.8 s, each a
2048 square; today the whole 4096 near map every 0.48 s. Tiles would also
cut what the near map costs on a ride.

A screen pixel at 1920 x 1080 covers about 1 mm per metre of distance (11
cm at 100 m, 32 cm at 300 m): near-map texels all the way out are finer
than the pixels there; the line at the near map's end comes from the jump
to 29 cm, which the fade answers.

## Dropping the near and middle maps, a larger far map

With tiles reaching about 400 m the middle map (600 m) mostly overlaps
them; tiles and a larger far map could replace near and middle. The
dynamic far map is drawn every frame (half the far map's size), with every
moving train in its 1.4 km that no nearer map holds: at a 2048 far map a
1024 dynamic one. Its cost to measure first (Prudnik's freight station and
ride, `TSRE_GPU_PASS_TIMES=1`, "sun shadow far").

## Risks

- The shaders: in task 50 a branch near the near map's filter, never
  taken, made the ET22 cab's view 9 ms slower (44 against 35 ms; wherever
  placed, also as a function at the call site). Every change to the near
  lookup to be measured in the cab (its PBR and MSTS programs) and at
  Prudnik from the first step.
- Memory on the Deck (1 GB dedicated, task 48's notes): the tiles and
  vegetation's beside the dynamic maps.
- Shadows between tiles drawn at different times (a tile drawn before a
  sun step, its neighbour after) at most 0.1 degree apart.

## First step

Test switches for the near map's extent and lead (for example 320 m at
4096, 7.8 cm): the user sees stations with near shadows to about 300 m,
and the whole-map redraws' cost at speed is measured, before tiles are
built.
