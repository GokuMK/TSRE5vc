# Task 50 - Caching what casts shadows

Status: step 1 done (2026-11-24, `feature/car-interiors`): the cab's and a
car's maps cached (B below, with the small map for passing trains).

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

Tried (2026-11-24, the cache on; env only, for the user's eye):
`TSRE_CAB_SHADOW_SIZE` (512 to 4096: the cab's map square, the car's map
as long, a quarter as high) and `TSRE_CAB_SHADOW_KERNEL` (7 the default, 5,
3, 0 the disc, and 1: one filtered 2 x 2 comparison, added for this).
Every timed pass at 1920 x 1080 on the Deck, ms (two views each):

| Map, filter | ET22 cab | Bhp coach |
| --- | --- | --- |
| 2048, tent 7 (today) | 55.1, 61.2 | 38.8, 32.0 |
| 2048, tent 3 | 54.2, 60.0 | 37.5, 31.1 |
| 2048, one tap | 53.8, 59.9 | 37.0, 30.8 |
| 4096, tent 7 | 56.7, 63.1 | 39.5, 32.7 |
| 4096, tent 3 | 54.9, 60.5 | 38.2, 31.8 |
| 4096, one tap | 54.5, 59.9 | 37.5, 31.4 |

The cheaper filters save 1 to 1.8 ms; 4096 gives back 0.5 to 0.8 (larger
texture, the environment cube 0.3 to 0.5 more). Drawing the cab's map at
4096 costs 7.2 to 8.3 ms against 2.1 to 2.5 at 2048 (measured with the
cache off; cached, only when it is drawn again, every frame or two in
curves). The cab's map at 4096 takes 64 MB (D32F), the coach's 16 MB.
Images (`item6-coach-sheet.png` in the session's scratchpad, the coach at
noon): one tap leaves stair-steps; 4096 with tent 3 sharp and smooth,
nearer a real sun's edge close to a window frame (about 1 cm at 1 m) than
today's soft one (about 8 cm). For the user's tests: `TSRE_CAB_SHADOW_SIZE=4096
TSRE_CAB_SHADOW_KERNEL=3`; cheapest: `TSRE_CAB_SHADOW_KERNEL=3` at 2048.

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

## Step 1: the cab's and car's maps cached (done 2026-11-24)

The user's choices (2026-11-24): B with the small map for passing trains;
our whole train cached (its cars within the map); the turn threshold
0.1 degree; the cab's map centred on the cab's middle (its centre line at
the eye's height and place along it, taken when the cab is first seen: the
eye moving does not draw it again); a car the camera is in never standing.

Built:

- `Eng::viewerTrain`: the cars of the train the camera is in (its 3D cab
  ridden, the walking character's car), set by the view each frame
  (`stepCharacter`); never standing, so the static caches leave them out.
- `computeShadowMatrices`: the map's clip space kept in the vehicle's
  frame (the cab's car's or the car's box, rigid) and drawn again when the
  vehicle, the map's size, the train's content (`CarBox::content` of its
  cars: shapes, doors at rest, worked cab controls) or the sun's direction
  in the vehicle's frame (more than `TSRE_CAB_SHADOW_CACHE_TURN` degrees,
  0.1) changes; otherwise the cached matrix times the vehicle's inverse.
- `renderShadowMaps`: when drawn, the train's own casters alone
  (`Renderer::renderTrainShadowCasters`, `QueueRenderer::planTrainCasters`);
  each frame the other trains' moving casters in the column
  (`renderPassingShadowCasters`) into `TARGET_SHADOW_PASSING`, half the
  map's texels each way, bound, cleared and drawn only when there are any.
- `ShadowSampling.glsl`: the cascades keep their static maps' share apart
  (`occludedStatic`); inside the box the train's map (and the passing
  map's four taps while one passes) against the static maps' world
  (`cabShadowWorld`: x cached, y passing, z and w a passing texel).
- `TSRE_CAB_SHADOW_CACHE=0`: as before (everything every frame, the cab's
  map on the eye); `TSRE_CAB_SHADOW_CACHE_LOG=1` logs each drawing;
  `TSRE_CAB_SHADOW_CACHE_VEHICLE=1` (testing) caches the vehicle alone and
  draws the rest of its train as passing trains.

Results (the Deck, the user's profile):

- Still views (842 x 755): the coach's map 0.66 ms to none, the ET22 cab's
  2.6 to 3.0 ms to none (drawn once each), every timed pass 0.6 and 2.6 to
  3.1 ms less. Images as with the map drawn every frame: the coach within 2
  levels but the patches' edges (18 pixels more), the cab its shadows'
  edges a texel over (the map's centre on the cab's middle, not the eye).
- CMK act 2's ride (the ET22 cab, 15 s standing and 60 s driven out),
  twice each: drawn 87 to 96 times in about 3,500 frames. At the user's 30
  fps limit the GPU's median 28.2 against 28.4 ms (the Deck lowers its
  clock in the slack: the saving shows as less work, not shorter passes);
  without the limit 19.1 and 19.2 against 20.4 and 20.5 ms, 6 % more
  frames; the processor's paint 14.4 and 14.5 against 14.9 ms, its shadow
  phase 1.5 against 1.9.
- The passing map (`TSRE_CAB_SHADOW_CACHE_VEHICLE=1`, the coach's
  neighbours drawn as passing trains every frame): 0.14 ms, the images as
  with the whole train cached (within 2 levels).
- The ET22's lamp-lit cab by night pixel for pixel as on 2026-11-23.
- bbb's circle ride (`perf-bbb-ride.json`) no longer runs: its train has
  finished its path by 07:01:30 since the activity changed.

Not yet: a real train passing seen from a cab (no such scene at hand); a
finer cached map with a cheaper filter (next, for the user's eye).

## The dynamic maps' early-out (done 2026-11-24)

The user's go (2026-11-24, its own commit). Each dynamic map keeps a 32 x
32 grid of cells over its square (`RhiRenderer::markDynamicCells`, a bit a
cell), set around every caster drawn into it when it was drawn: in the
light's view a caster's shadow lies within its own outline, so the bounding
sphere, 1.5 m wider for the filter and the normal offset, marks where the
map can hold anything. The lit shaders test the cell under the point
(`dynamicShadowCells`) and take the dynamic taps only there; an empty map is
skipped everywhere, and inside a cached cab's or car's box (faded in) none
are taken (its own map and the static maps make it there).

A caster without bounds (an animated MSTS shape's moving parts:
`SFileLegacy::pushPosed` gives its posed packets none, so the renderer cannot
cull them either) marks 50 m round its object's origin: the first version
set every cell for one, and Prudnik's four animated objects filled the
middle and far maps. Giving posed packets their bounds in the shape
backends would be the proper fix (open).

Results (1920 x 1080, the Deck, early-out off and on, every timed pass,
ms; the Deck ran slower this late session than in the earlier tables, so
only the pairs compare):

| View | Off | On |
| --- | --- | --- |
| ET22 cab, two views | 56.4, 61.9 | 54.5, 60.8 |
| Bhp coach, lower and upper deck | 39.1, 32.7 | 32.6, 31.0 |
| Prudnik forest | 43.5 | 42.7 |
| Prudnik freight station | 75.7 | 66.0 (the cube 11.3 to 8.9) |

At Prudnik the maps marked 10, 29 and 62 of 1,024 cells (the freight
station), 14 and 19 (the forest). The forest gained less than a shader
without the dynamic taps did earlier (4.2 ms). Images as before (Prudnik
pixel for pixel, the coach and cab within 2 levels).

Reviewed (2026-11-24, checks only, the user: no implementation change;
shader copies swapped in and put back). Every timed pass at 1920 x 1080,
Prudnik:

| Shader | Forest | Freight station |
| --- | --- | --- |
| The cells off (`TSRE_DYNAMIC_SHADOW_CELLS=0`) | 44.7 | 78.9 |
| The early-out as committed | 44.2 | 69.4 |
| The near map's test out of its loop (two loops) | 40.6 | 63.5 |
| No dynamic taps at all | 39.0 | 58.4 |

A probe (full shadow where the map in use takes its dynamic taps) shows
the cells right: in the forest a small patch far off, at the freight
station two patches round moving objects. So the cells are not the cause:
the test inside the unrolled 16-tap loop does not spare the dynamic
lookups on the Deck's driver (the branch flattened, or the samples issued
for the whole wave); taken once outside the loop, a loop with the dynamic
taps and one without, the near map alone recovers 3.6 ms in the forest
and 6 ms at the freight station. Doing so for all three maps should come
near the shader without dynamic taps (about 5 and 11 ms).

Done for all three maps (the user's go, 2026-11-24): `cascadeTaps` takes a
near or middle map's 16 taps in one of two loops, with the dynamic taps or
without, chosen once before them; the far map's 2 taps the same way inline;
vegetation's taps in a loop of their own after. Every timed pass at 1920 x
1080, the three shaders back to back per scene (Prudnik's committed one run
twice, before the others and after: within 0.4 ms), ms:

| View | Committed | Test outside the loops | No dynamic taps at all |
| --- | --- | --- | --- |
| ET22 cab, two views | 54.0, 60.0 | 43.7, 47.9 | 38.4, 42.0 |
| Bhp coach, lower and upper deck | 37.9, 31.2 | 36.4, 29.7 | 35.0, 28.6 |
| Prudnik forest | 42.4 | 36.7 | 36.6 |
| Prudnik freight station | 65.4 | 55.1 | 54.5 |

The cab gains most: inside its box the dynamic taps were meant to be
skipped (above) but were fetched all the same, over most of the screen.
What remains there against no dynamic taps is presumably the world outside
the box round the train, where the train's own cells are set. Images the
same: Prudnik pixel for pixel, the cab within 1 level, the coach within 3
(one pixel of the compass, which differs as much between two runs of one
shader); one run of the coach's upper aisle stood the camera elsewhere, run
again identical. The OpenGL renderer's shaders compile (its path keeps one
loop a map, without dynamic or vegetation maps).

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
