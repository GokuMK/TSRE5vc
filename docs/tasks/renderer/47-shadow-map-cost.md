# Task 47 - Shadow map cost

Status: design (2026-11-16), not started. The user: "soon we are going to
start shadow map optimisation task, so we should gain more ms back" (after
the cab's shadow map, task 10, cost about 1 ms more at 2048).

## Problem

While a train is ridden, the sun's shadow maps take more of the Deck's GPU
than the view itself. Task 37 caches each map's static casters and copies
the cache into the frame's map every frame, then draws what moves on top.
That keeps the world's redraws rare, but every frame still pays:

- a copy of every cache: the three main maps and the three vegetation maps,
  at their full sizes (64 MB for a 4096 near map);
- every train in range, standing or moving, in all three main maps: trains
  are drawn in the overlay layer, which always counts as moving
  (`QueueRenderer`: `dynamicShadow` for anything not in the scene layer).
  The lamps' maps already keep standing trains in their cache (task 46 A:
  a car still for 2 s, `Eng::StandingMs`); the sun's do not.

## Today's frame (measured 2026-11-16)

bbb's circle, the ET22's cab, 92 km/h, `TSRE_GPU_PASS_TIMES=1` medians;
near map 2048 unless noted, middle 2048, far 1024 (the captures used the
main profile's sizes; the user plays at 4096 near):

| Pass | GPU a frame | Mostly |
| --- | --- | --- |
| Near | 3.6 to 3.9 ms | the train (about 3 ms) on the cache's copy (0.72 ms) |
| Near at 4096 | 10.4 ms | the train (about 7.6 ms) on the copy (2.8 ms) |
| Middle | 3.7 to 5.9 ms | the same train again |
| Far | 2.0 to 3.8 ms | the same train again |
| Vegetation, three maps | 1.5 to 2.4 ms | copies only: vegetation never moves |
| Cab (task 10) | 1.9 ms at 2048 | the loco and its cab |

The train: 364 draws, 415 k triangles a frame (the ET22 and three MaSzyna
coaches with their interiors). Empty, the near map costs 0.72 ms at 2048
and 2.8 ms at 4096: everything in it scales with its pixels. About 11 to
17 ms a frame while riding, 18 to 24 ms with a 4096 near map.

## Plan

Each step measured on its own before the next, against step 0. All steps
on one branch, merged together (the user, 2026-11-16).

### Step 0: the baseline at the user's settings

Before any change, the reference every step is compared with: the user's
sizes (near 4096, middle 2048, far 1024: the gather-renderer profile),
GPU pass times per map, the view's pass and the frame's time, on the three
rides of Measuring (bbb's cab ride, Prudnik's stations, CMK's station).
The table above is from the main profile's sizes (near 2048) and bbb
alone.

### Step 1: two quick wins

1. **Vegetation maps read from their caches.** Nothing moving casts into
   them (vegetation never moves), so the frame's vegetation maps are their
   caches' copies. Bind the caches in `frameSlot` instead and skip the
   vegetation maps' per-frame clear and copy, except while something
   dynamic is in the vegetation set (an edit). Expected: the three copies
   (1.5 to 2.4 ms at these sizes, about 2.8 ms more for the near one at
   4096); images identical.
2. **Moving casters only in the maps that light their shadows.** A point
   takes the near map wherever it lies in the near map's square (in the
   light's frame), the middle map outside it. A caster whose footprint in
   the light's frame lies inside the near map's square (less the blend
   zone and the filter's reach) shadows only points that take the near
   map: leave it out of the middle and far maps (`planShadowCasters`, the
   footprint from the instance's bounds through the light's matrix).
   Exact in the light's frame, so the images stay identical. Expected:
   most of the middle and far maps' per-frame cost on a ride (5 to 9 ms
   on bbb).

### Step 2: the cost of a second lookup

Before the split, measure what a second shadow lookup costs the view: the
near map sampled twice for every lit pixel (repetition, as in other cost
measurements), then with an early-out (one tap first, the full filter only
near something). Decides the dynamic maps' filter.

### Step 3: static and dynamic maps apart

- **Static maps:** today's caches, kept at their sizes (the world's
  shadows keep their quality), now also holding standing trains: a car
  still for 2 s (the lamps' rule, `Eng::StandingMs`; the user: ok) goes into the cache,
  out again when it moves; the cache is drawn again then (a train's state
  changes seldom: the user).
- **Dynamic maps:** what moves, drawn every frame into maps of their own:
  no copy. Their size by a setting (Graphics, Shadows), "Dynamic shadows"
  (the user's name, 2026-11-16): half the static maps' size (default) or
  the same (the user: "for those RTX owners"). The cab's map (task 10)
  follows it: half the near map, 1024 at 2048 and 2048 at 4096.
- **Receivers** look up both and add the two occlusions as the vegetation
  maps do (`main + s veg - main s veg`): the lit shaders'
  `shadowedVisibility`, the fog volume (`RhiFog`, one tap a map), the
  light shafts (the far map), the exhaust's light, the water (through
  `shadowedVisibility`). The sky enclosure and the lamps' maps have their
  own and stay. The OpenGL renderer keeps one set (no caches there).

### Step 4: check and tune

The same rides as in Measuring: pass times, the view's time, the frame;
images against step 1's.

## Expected gains (estimates, to measure)

| Step | Saves a frame while riding |
| --- | --- |
| 1.1 Vegetation from the caches | 1.5 to 2.4 ms (more at 4096) |
| 1.2 Moving casters where read | most of the middle and far maps' 6 to 10 ms |
| 3 Static and dynamic apart | the near copy (0.7 to 2.8 ms), the moving casters at half size (about 7.6 to 3 ms at 4096), standing trains out of the frame (Prudnik's stations) |

Against: the second lookup (step 2), the cache's redraws when trains stop
and start.

## Cons and risks (discussed 2026-11-16)

- The second lookup's cost: estimated 0.5 to 1 ms without an early-out,
  0.1 to 0.3 ms with one and fewer taps on the coarser map; measured in
  step 2 before building on it.
- Moving trains' own shadows coarser at half size (7.8 cm texels at a
  2048 dynamic near map): the setting's "same size" for strong GPUs (the
  user: not an issue).
- Trains turning static or dynamic: their shadow's sharpness changes and
  the cache is drawn again (a spike); rare (the user: not an issue).
- Two occlusions added over-darken a little where both are partly in
  shadow at an edge; negligible.
- Every reader of the maps changes (above).
- Unchanged: the static caches' redraws themselves (every 16 m at speed,
  task 37).

## Measuring

- bbb's circle in the ET22's cab: `file1.act`, 07:01:30, train 0 (the cab
  map's ride, 20 frames 0.1 s apart).
- Prudnik's stations with standing trains: PSK_2026, train 154 from
  10:58:30 through Zawiercie's yard (start beyond the object range).
- CMK's station.
- `TSRE_GPU_PASS_TIMES=1` medians per pass, the view's pass, the frame's
  time. Set the user's sizes in captures (`--set
  core.rendering.shadow.primaryMapSize=4096`): the capture script copies
  the main profile (2048).
- Steps 1.1 and 1.2 must give the same images (the pixel diff within 1).

## Acceptance

- Step 0: the baseline recorded here.
- Step 1: images the same on the three rides; the time saved measured.
- Step 3: at "same size" the images as before; at "half" moving trains'
  shadows softer, no new artifacts (the cab map's rides, a train passing a
  platform); the frame faster on bbb's ride and at Prudnik by the measured
  amount; the new setting registered, translated (Polish), the settings
  count test updated; all targets, ctest, the GPU suites.

## Decisions (the user, 2026-11-16)

1. The setting: "Dynamic shadows" (shorter than "Moving objects'
   shadows"), half size by default or the same as the static maps; under
   Graphics, Shadows.
2. Standing trains: the lamps' 2 s.
3. Step 1 not merged on its own: the whole task on one branch.
