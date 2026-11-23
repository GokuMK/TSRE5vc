# Task 50 - Caching what casts shadows

Status: noted (2026-11-24), not started.

The user (2026-11-24, after task 49's car map): "Caching shadow geometry is
one big task on itself as it would help us greatly."

## Problem

Several shadow maps draw the same geometry again every frame although it has
not changed where it matters:

- The cab's sun map (task 10) and a car's (task 49): everything in their
  column towards the sun, every frame, the train moving through it. Most of
  that is the vehicle itself (its floors, seats, roofs, cab and controls),
  which stays put in its own frame while the sun's direction in that frame
  holds: on straight track, and standing, for minutes.
- The sun's dynamic maps (task 48): every moving train in range, every
  frame, in all three maps.
- Moving cars' lamps (task 46 A) draw their tiles each frame; a car's own
  lamps (task 46 C) are already cached in the car's frame.

Measured on the Steam Deck (task 49, bbb, before the stop-gap below): the
ET22's cab map 3.3 ms of the GPU a frame, a double-deck coach's map (2048 x
512) 1.2 ms, at 4096 x 1024 4.4 ms. Drawing them is bound by fill: the
vehicle's layers over each other.

## The stop-gap (task 49)

Opaque casters drawn without the shadow shader's `discard` (early depth) and
first, so their depth rejects the alpha-tested and blended casters behind
them: the cab's map 1.9 to 2.4 ms, the coach's 0.6 to 0.76 ms (task 49
has the measurements, at 842 x 755 and 1920 x 1080). It helps every shadow
map; the per-frame redraw stays.

How much of it is the train (task 49's measuring run, 842 x 755, with the
stop-gap): the coach's map 0.78 ms, 0.22 without the trains (its clear and
the world's casters); at 4096 x 1024 2.86 and 0.75; the cab's 2.2 to 2.7
ms, 0.16 to 0.22 without.

## Two ways for the cab and car maps alone (estimated 2026-11-24)

- **A: the vehicle cached, copied in each frame.** The vehicle's own
  casters drawn once into a cache in its frame; each frame a copy into the
  map (task 37's old copy pass cost 0.72 ms at 2048 x 2048, so about 0.18
  at the coach's 2048 x 512), then the world's casters on top (0.16 to
  0.22). The look as now. Saves about 1.3 to 1.8 ms in the cab, 0.4 in the
  coach.
- **B: the vehicle cached, the world from the cascades.** The map holds the
  vehicle alone; inside its box the world's shadows come from the sun's
  static cache, whose taps the lit shaders already take there (the
  cascades' loop samples it before the cab map replaces them). No per-frame
  drawing while the cache holds: saves the whole map, 2.2 to 2.7 ms in the
  cab, 0.78 in the coach. The look changes: the world's shadows inside the
  vehicle (poles, wires, trees) at the static map's texels (3.9 cm at 4096),
  soft as a far caster's real penumbra; moving trains' shadows (the dynamic
  maps) left out inside the box; a standing car the camera is in kept out
  of the static cache (the cab view already is).
- Both: the cache drawn again when the sun's direction in the vehicle's
  frame turns by more than a threshold (curves; the sun itself is slow),
  the vehicle's content changes (`Eng::contentHash`: doors, worked cab
  controls, shapes loaded) or the map moves in its frame. Running straight
  changes nothing: the map follows the vehicle unsnapped, so the vehicle
  stays on the same texels (task 10's reason for not snapping it), with
  the world's axes too; a curve turns it against the sun at v / R (100
  km/h at R 1000 m: 1.6 degrees a second, a redraw every few frames at a
  threshold of a few tenths of a degree). The cab's map is centred on the
  eye: cached, on a fixed point of the cab (the eye moves with the head and
  the cab's places).

The user (2026-11-24): B, the cab's map has not that much range to catch
many world objects anyway. Moving trains' shadows are lost inside the box
with B: they are in the dynamic maps with the train itself, whose coarse
copy the cab and car maps replace there (standing trains are in the static
caches and stay).

The processor (1920 x 1080, 30 frames, two runs, `TSRE_CAR_SHADOW_OFF` and
`TSRE_CAB_SHADOW_OFF` against on): the map's planning and drawing (the
coach's about 245 draws) about 0.5 ms a frame; with B none while the
cache holds. The view's lookup of the map costs more than its drawing at
1920 x 1080: every timed pass 37.7 against 34.0 ms in the coach (the map
0.8), 42.1 against 37.1 in the cab (the map 2.2); B keeps that lookup, but
inside the box it would drop the dynamic maps' taps.

### Moving trains inside the box (the user, 2026-11-24)

B as above leaves a passing train's shadow out inside the cab or car, and a
long slow freight train passing makes that unacceptable. Ways:

- **A small map of the foreign moving trains in the column**, drawn only
  while one is there (casters with an owner outside our train that move),
  read inside the box with a cheap filter only then (a flag): nothing while
  none passes; while one does, its few draws in a 6 m column and a few
  taps a pixel. Their shadows are soft anyway (metres from the receiver).
- Copying the cache into the frame's map while one passes, and drawing it
  on top (A only then): the copy costs 0.72 ms at 2048 x 2048, 2.8 at
  4096 (task 48), against a finer cached map.
- Reading the dynamic cascades inside the box: brings back the coarse copy
  of our own train the box replaces; keeping our train out of them loses
  its shadow on the ground outside the box. Not this.

### A finer cached map, a cheaper filter (the user, 2026-11-24)

Cached, a map's drawing is paid only on redraws, so it can be finer and
read with a cheaper filter: the cab's at 4096 (1.45 mm texels; D16 is
enough for a map holding the vehicle alone, its depth range a few metres:
32 MB), read with 4 taps (tent 3) or one hardware 2 x 2 comparison instead
of tent 7's 16. Task 10 chose tent 7 against steps re-forming as the sun
turned against the cab; at half the texel size they are half as large: to
check by eye.

### The lookups at high resolution (measured 2026-11-24)

Task 10 and task 48 were tuned at 842 x 755 (the maps doubled, static and
dynamic apart, because drawing cost more than a second lookup there).
At 1920 x 1080 the lookups cost more than the drawing (the view pass,
`TSRE_GPU_PASS_TIMES`, the case's `"shadows"` off, the cascades only
(`TSRE_CAB_SHADOW_OFF`), the cab's map too; ms):

| View | No shadows | Cascades | With the cab map | Drawing the cab map |
| --- | --- | --- | --- | --- |
| ET22 cab, 842 x 755 | 8.75, 9.52 | 10.09, 10.97 | 10.66, 11.52 | 2.2 to 2.7 |
| ET22 cab, 1920 x 1080 | 27.96, 32.21 | 32.65, 36.63 | 34.17, 38.59 | 2.2 |
| Prudnik forest, 1920 x 1080 | 23.80 | 28.15 | | |
| Prudnik freight, 1920 x 1080 | 36.40 | 48.48 | | |

The freight station's environment cube 3.70 against 8.30 ms more. The
cascades' lookups: 1.4 ms of the cab's view at 842 x 755, 4.4 to 4.7 at
1920 x 1080; at the freight station (many surfaces over each other, each
shaded with its lookups) 12 ms of the view and 4.6 of the cube. Worth a
task of its own: fewer taps (the static and the dynamic map each take 16),
an early-out where the taps agree (task 48's 4-tap test cost nothing),
the lookups once a pixel (a depth pre-pass, or a shadow mask at half
resolution).

The dynamic maps are read whatever they hold: every tap samples the static
map and the dynamic one (`ShadowSampling.glsl`), also where nothing moves.
Without the dynamic taps (a shader copy swapped in, 1920 x 1080; every timed
pass): Prudnik's forest 40.7 against 36.5 ms, its freight station 62.5
against 54.5 (its dynamic maps empty there: their passes 0.03 to 0.05 ms,
the clears), the ET22's cab 38.1 and 41.8 against 35.3 and 38.1 (the cab's
train is in them: a train ridden in its cab never counts as standing). Cheap
ways: skip a map's dynamic taps when nothing was drawn into it this frame (a
flag a map); skip them outside the light-space rectangles of what was drawn
(a few a map: a ride's own train covers a small part of each map, so most of
the view skips them too).

## Ideas to weigh

- A vehicle's own map in its frame (as its lamps' since task 46 C): drawn
  once for the sun's direction in that frame and kept until that direction
  turns by more than a threshold (curves), the vehicle's content changes
  (its doors, a control the driver works: `Eng::contentHash`) or its shapes
  load. Each frame only the world's casters in the column (a pole, a tree,
  a station roof) are drawn, over a copy of it or into a map of their own
  read beside it (the static and dynamic maps' split, task 48: lit where
  both are).
- For the dynamic maps: the same per car, or a car's cached map reused for
  its shadow on the ground nearby; to measure how much of the dynamic maps'
  cost trains are on a ride (Prudnik's freight, CMK).
- What breaks a cache and how often: the sun's motion (slow), curves,
  animations (pantographs, doors, wipers), the camera's head moving in the
  cab (the cab map is centred on the eye: its extent would have to cover
  the cab instead).

## Measuring

`TSRE_GPU_PASS_TIMES=1` ("sun shadow cab", "sun shadow near/middle/far",
"shadow cache"); rides, not still views (still scenes need no speedup):
task 49's `bhp-noon` and `cab-day` cases for a standing train, a driven
ride for the moving case.
