# Task 48 - Shadow map cost

Status: done (2026-11-17, `feature/shadow-map-cost`): the shadow maps'
per-frame copies gone, what moves in maps of its own at half the static
maps' size (the "Dynamic shadows" setting), standing trains in the static
caches. Live on the Steam Deck against main, the same profile: bbb's ET22
cab 15.2 ms GPU against 27.8 (56 fps against 30.5), CMK 18.2 against 30.9,
Prudnik 38.8 against 46.7 (bound by the CPU there). Numbered 47 on the
branch (its commits say so) until the merge: 47 was the environment map
capture hang's brief already.

The user: "soon we are going to start shadow map optimisation task, so we
should gain more ms back" (after the cab's shadow map, task 10, cost about
1 ms more at 2048).

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
   caches' copies. The renderer binds the caches instead
   (`readVegetationCache`, `vegetationShadowDepth`: `frameSlot`, the fog
   volume, the light shafts) and the vegetation maps' per-frame clear and
   copy are skipped, except while something moving is in the vegetation
   set (`movingVegetationCasters`: an edit, an animated tree). Expected:
   the three copies (1.5 to 2.4 ms at these sizes, about 2.8 ms more for
   the near one at 4096); images identical.
2. **Moving casters only in the maps that light their shadows.** The lit
   shaders take the near map wherever a point lies within the unit ball
   of the near map's clip space (`ShadowSampling.glsl`, no blend zone),
   the middle map within its own ball, the far map past that. A moving
   caster whose shadow stays within the near map's ball shades only
   points that read the near map: the middle map leaves it out; the far
   map leaves out those whose shadow the middle map's ball holds
   (`planShadowCasters` with a `ShadowCover`: the instance's bounds as a
   box in the nearer map's clip space, swept along its shadow).
   - Not exact: a shadow runs on along the sun until something takes it,
     so its end is assumed. The sweep takes a shadow to fall at most 20 m
     below its caster (`MovingShadowDrop`: an embankment, a low bridge),
     20 m / sin(the sun's elevation) along the sun. Below about 6 degrees
     of sun the sweep leaves the ball and nothing is left out. A higher
     bridge at a low sun can lose the far end of its train's shadow where
     that end leaves the nearer map's ball.
   - Widened by 2 m and three of the farther map's texels: its filter,
     the receivers' normal offsets, the exhaust's taps.
   - The other readers: the exhaust's puffs take the maps by the same
     balls; the fog volume takes the nearer map over its whole square, a
     larger region, so it reads the farther map only where the lit
     shaders do. The volumetric light
     shafts (`Game::lightShafts` 2, the user's profile) marched the far
     map alone through the air around the camera, which would have kept
     every moving caster in it. They took the maps as the fog volume does
     for a while (near, middle, far; the user, 2026-11-17: "makes sense"),
     but that cost 25 ms a frame (see "The regression" below): they march
     the far maps alone again, and the far map keeps its moving casters
     while they are on (`farShadowInAir`).
   - `TSRE_SHADOW_COVER=0` keeps them all (A/B captures).

   Expected: most of the middle map's per-frame cost on a ride (3.7 to
   5.9 ms on bbb), the far map's too (2.0 to 3.8 ms). Images: the same within the pixel diff on the three rides
   (sun above 6 degrees, no high bridges).

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

As built (2026-11-17):

- The static maps are the caches themselves (`shadowCaches` 0 to 2, and 3
  to 5 vegetation's), bound where the main maps were (`shadow0..2`, the
  fog volume's and the shafts' bindings); the frame's maps
  (`TARGET_SHADOW_*`) are the dynamic ones, `staticSize / divisor`, drawn
  with the caches' matrices (one UV for both), cleared every frame and
  holding `CASTERS_DYNAMIC` of every set. The copy pass and vegetation's
  frame maps are gone (`TARGET_VEGETATION_*`, 84 MB at the user's sizes):
  moving vegetation (an edit) draws into the dynamic maps.
- The lit shaders multiply each filter tap's static and dynamic lookups
  (`dynamicShadow0..2`, bindings 42 to 44): a tap is lit where both are,
  as the one map's tap was. The dynamic lookup's depth bias is the static
  one times `dynamicShadowBias` (the dynamic filter, `max(texel, tap
  spread)`, against the static one: 1.22 near at 4096/2048, 2 middle and
  far at half). The fog volume, the shafts and the exhaust multiply a tap
  of each.
- Standing trains: Eng's StandingMs car (`setLampStatic`) is static for
  the sun's caches too (`dynamicShadow` false), added with
  `RenderQueue::addStandingCar`; each map's signature is the static
  objects' plus the standing cars' within its range
  (`standingSignature`), so a train stopping 900 m off redraws the far
  cache alone. A car's two-state parts (doors, pantographs, mirrors,
  windows) on their way, or its wipers on, keep it moving; where they
  rest goes into its place hash.
- The setting `core.rendering.shadow.dynamicMapSize`: 2 half size
  (default), 1 the same; read when the renderer initializes. The cab's map
  is the near map's dynamic size (`TSRE_CAB_SHADOW_SIZE` over it).
- Without a cache (the OpenGL renderer, a cache that could not be
  made) the frame's map takes every caster, as before.

### Step 4: check and tune

The same rides as in Measuring: pass times, the view's time, the frame;
images against step 1's.

Done (2026-11-17): the user's live check ("seems to be ok"); the totals
live against main with the frame log (Results, "The regression"); the GPU
suites on the real platform (rhi-shaders 166, gltf-pbr-gl 21,
environment-map-gl 19, water-gl 31, terrain-material-gl, terrain-mesh-gl,
shape-complex-gl 132, token-shape-gl, transfer-depth-gl 44, local-lights
17, exhaust-particles 18, night-textures 11) and offscreen (settings 297,
car-animation 18, eng-lights 47, e3d-file 71, shape-materials 30,
track-cant 54, sky 12, time-of-day 31), all passed; ctest 11/11; the
native OpenGL renderer (`core.rendering.backend` opengl) on bbb's four
views the same as main's (the moving cab view within its noise).

Open (later): a cache redraw for a standing train draws up to the three
caches in one frame (about 16 ms more GPU on Prudnik's ride, an eighth of
the redraws there); an early-out for the dynamic lookup (step 2) if the
second lookup shows; the finer shafts near the camera would want a cheaper
way than the near map along every ray.

## Results

### Steps 0 and 1 on bbb (2026-11-16)

The gather-renderer build with both step 1 switches off (A: the old frame,
code paths as main's) and on (B); near 4096, middle 2048, far 1024,
volumetric shafts (the user's profile), 842 x 755; `t47-bbb.json` (the
scratchpad's): the ET22's train moving 0.1 s a frame, 150 timed frames a
view. GPU time of all timed passes a frame, the mean of the views' 2 s
medians; three A runs and four B runs, alternated:

| View | A | B | Saved |
| --- | --- | --- | --- |
| Cab ahead | 32.8 ms | 28.0 ms | 4.8 ms |
| Cab looking back along the train | 36.5 ms | 31.5 ms | 5.0 ms |
| Aerial, still, the train's whole shadow | 27.6 ms | 23.2 ms | 4.4 ms |

- The vegetation maps' passes are gone: 3.0 + 0.75 + 0.2 ms at these
  sizes.
- The middle map draws 136 casters instead of 487 on the cab ride and
  costs 2.7 to 3.1 ms instead of 4.1 to 4.2: the rear coaches, beyond the
  near map's ball at this sun, are still drawn into it (correctly).
- The far map keeps its 553 casters (the volumetric shafts). With
  screen-space shafts (`lightShafts` 1) it drops to 66 casters, 2.1 to
  0.2 ms.
- Images: the still views identical (max 0); the moving cab views differ
  only where two A runs differ from each other as much (the motion-blurred
  track ahead, max 49: the blur follows the wall-clock frame time).

### Steps 0 and 1 on Prudnik and CMK (2026-11-16)

Live rides, one A and one B run each (`t47-prudnik.json`: train 154 from
10:58:30 through Zawiercie and past the yard, 420 s; `t47-cmk.json`: act 2
at 12:00, the ET22's train standing 15 s, then driven 60 s); all timed
passes a frame, mean of the 2 s medians:

| Ride | A | B | Saved |
| --- | --- | --- | --- |
| Prudnik, train 154 | 45.5 ms | 41.3 ms | 4.2 ms |
| CMK act 2, standing | 26.8 ms | 23.9 ms | 2.9 ms |
| CMK act 2, ride | 27.1 ms | 23.7 ms | 3.4 ms |

At CMK the vegetation maps are gone and the middle map costs 2.3 ms
instead of 3.0. At Prudnik the vegetation maps were still drawn in most
frames (1.9 + 0.5 + 0.1 ms). Traced: trains' parts with MSTS's cruciform
light materials (-9, -10, `MstsMaterials::applyLight`: the ET22's mirrors,
`ET22_lusterka.dds`, and EN57 parts) carry the vegetation flag; animated
(folding mirrors) or simply in a train, they counted as moving vegetation
whenever such a train was near.

The rule since (the user, 2026-11-17: only route objects should be
vegetation; a stop-gap until vegetation's shadows are redone as generic
transmission shadows with moving maps, when it goes): an instance is
vegetation only when it is route scenery that does not animate
(`QueueRenderer`'s `vegetationInstance`, `DrawInstance::vegetation`). The
flag decides the shadow set, the sky enclosure's skip in the lit shaders
(`materialVegetation`), vegetation's lighter share of its shadows, the
normal flip and the shadow alpha cut; all now follow the instance. Route
vegetation being edited stays vegetation (it draws on the vegetation maps'
copies while the edit settles). Prudnik again with the rule and the new
shafts (2026-11-17): the vegetation maps' passes gone in every window, the
far map 0.4 ms instead of 1.2, the middle 2.0 instead of 2.7; all timed
passes 41.3 ms against 44.5 (the mean of both A runs; the live rides'
view and near passes wander by about 1 ms between runs); the still start
view the same within 4 of 255.

### The volumetric shafts from the three maps (2026-11-17, reverted)

Measured with the pass times only, which leave the shafts out: see "The
regression". With the shafts marching the near, middle and far maps (as the fog volume
takes them), the far map leaves out covered moving casters too: on bbb's
cab ride 66 instead of 553 casters, 0.2 ms instead of 2.1. bbb B against A
(one pair, both with the new shafts): cab ahead 25.1 against 31.8 ms, cab
looking back 28.0 against 35.5, aerial 21.9 against 26.5; images the same
as before (the moving cab views' blur noise aside). The look at CMK's
start (a low sun over the stands' frames, `t47-shafts.json`, clouds still):
against the far map alone at most 50 of 255 (19:00, clear), 9 to 13
elsewhere: the frames near the camera shade the haze a little more.

### Step 2: a second lookup's cost (2026-11-17)

Two temporary `ShadowSampling.glsl` variants swapped in by file copy (the
scratchpad's `make_second.py`, `make_e4.py`, `t47step2.ps1`): a second
16-tap filter of the same map half a texel over (S, what a dynamic map
read like the static one costs), and a second check of the Poisson disk's
outer 4 taps (E, an early-out's test where a dynamic map is empty); 2 taps
in the far map either way. bbb, near 4096, 842 x 755, alternated O S E O S
E, the mean of two runs each (the start view left out: its windows hold
the loading):

| Pass | O | S | E |
| --- | --- | --- | --- |
| View, cab ahead | 5.86 ms | 6.32 (+0.46) | 5.86 (+0.00) |
| View, cab looking back | 9.06 ms | 9.58 (+0.52) | 9.11 (+0.05) |
| View, aerial | 6.28 ms | 6.85 (+0.57) | 6.24 (-0.04) |
| Environment cube, cab views | 3.0 ms | +0.13 to +0.25 | +0 to +0.13 |

A second full lookup costs about 0.6 to 0.8 ms a frame (view and cube); a
4-tap check costs nothing measurable. Against step 3's savings (the near
map's copy, 2.8 ms at 4096, and the moving casters at half size) either
pays. A 4-tap early-out could miss a thin moving shadow between its taps
(a pantograph, a wire): step 3 starts with the full filter in the dynamic
maps, an early-out (taps, or a coarse map of the dynamic maps' nearest
depths) only if the full one shows in the measurements.

### Step 3: static and dynamic maps apart (2026-11-17)

bbb (`t47step3.ps1`), near 4096, half (H) and same size (S) alternated
H S H S, all timed passes a frame, two runs each; step 1 is the step 2
session's original-shader runs (same build otherwise, same spec):

| View | Step 1 | H | S |
| --- | --- | --- | --- |
| Cab ahead | 25.8 ms | 13.5 | 19.4 to 22.4 |
| Cab looking back | 29.1 ms | 17.2 | 22.6 to 27.0 |
| Aerial, still | 21.8 ms | 9.9 | 10.3 |

- The near map's pass 11.4 to 1.2 ms on the cab ride (no 64 MB copy, the
  train at 2048), the middle 3 to 0.4, the far 0.2 to 0.01; the view
  +0.4 ms and the environment cube +0.4 (the second lookup, step 2).
- At the same size the cab's map follows the near one to 4096: 7 to 11
  ms on the Deck (a strong GPU's option).
- Images against step 1: the still views identical in both (the train
  stands through the settle frames: in the static caches); the cab views
  within the moving runs' blur noise at H; at S the cab ahead differs a
  little more (its cab map at 4096).

Live rides at H against step 1's B runs:

| Ride | Step 1 | Step 3 |
| --- | --- | --- |
| Prudnik, train 154 | 41.3 ms | 33.5 ms |
| CMK act 2, standing | 23.9 ms | 17.7 ms |
| CMK act 2, ride | 23.7 ms | 18.1 ms |

Prudnik's near map 9.3 to 0.9 ms, the view +1.6 ms, the cube +1.0; CMK's
step 1 runs predate the shafts' change, so their view (+3.2 ms) mixes both.
The caches drawn again for standing trains ("casters") 108 times on
Prudnik's ride (counted per map, about one change in 10 to 15 s), beside
602 redraws for the camera's place, 160 refreshes and 11 for content that
came. A redraw shows in the frame log (`TSRE_FRAME_LOG`, `framespikes.py`):
the frames recording the most shadow work (the top 5 %, the redraws) are
followed by GPU frames of about 61 ms against the ride's median of 45 (p99
84). The standing trains add about an eighth to those redraws, each up to
the three caches in one frame (a car within all three ranges): watch for
it in live rides; staggering them would leave a stopped train without a
shadow until its cache is drawn.

The first runs (all surfaces in shadow) found the dynamic samplers taken
as ordinary 2D ones: `RhiProgram` knows comparison samplers by name; the
`dynamicShadow` names were added.

### The regression: the shafts from the near map (2026-11-17)

The user, after step 3: the frame rate on the branch half of main's at
bbb's default start view, the settings alike. Live on screen (`TSRE_FRAME_LOG`,
`t47-start-live.json`, 20 s, the branch's profile): the branch 20 fps, its
GPU 39 ms a frame, main 46 fps, 19 ms; the branch with the shafts off 63
fps, screen-space 60 (GPU 13.6 ms). The volumetric shafts marching the
near map (the 4096 static cache, its dynamic and vegetation maps) along
every ray cost about 25 ms a frame more on the Deck. `RhiPassTimes` does
not time the shafts (they run at present, outside the flushed targets):
every comparison of steps 1 and 3 left them out, and the capture harness's
pass times showed the branch twice as fast as main at that view.

Fixed: the shafts march the far maps alone (static, moving, vegetation's)
and the far map keeps every moving caster while they are on
(`Renderer::farShadowInAir`). Totals are checked with the frame log's
`gpu_ms` from now on, live.

The totals since, live (`TSRE_FRAME_LOG`, medians after 8 s), both builds
with the branch's profile (near 4096, middle 2048, far 1024, volumetric
shafts, clouds 3, no fps limit), main at the branch's base (a2cad986):

| Live | Branch | Main |
| --- | --- | --- |
| bbb's start view | 13.9 ms GPU, 60.9 fps | 19.1 ms, 46.3 fps |
| bbb, the ET22's cab (`t47-bbb-live.json`) | 15.2 ms, 56.0 fps | 27.8 ms, 30.5 fps |
| CMK act 2 (`t47-cmk.json`) | 18.2 ms, 47.8 fps | 30.9 ms, 27.4 fps |
| Prudnik, train 154 (`t47-prudnik.json`) | 38.8 ms, 20.1 fps | 46.7 ms, 17.8 fps |

Prudnik is bound by the CPU (paint 38 to 40 ms in both). With main's
profile (fps limit 30) both hold 30 fps at the start view, the GPU 23.7 ms
against 29.3.

### Later: the second lookup at 1920 x 1080 (2026-11-24, task 50)

Steps 2 and 3 measured the second lookup at 842 x 755 only (+0.4 to 0.6
ms of the view, +0.4 of the cube; +1.6 and +1.0 on Prudnik's ride), and
the early-out was left until the full lookup showed. It shows at 1920 x
1080: the dynamic maps are read whatever they hold, and their taps cost
(every timed pass, a shader copy without them) 4.2 ms at Prudnik's forest
and 7.9 at its freight station, where they are empty, and 2.7 to 3.7 in
the ET22's cab, where they hold the cab's train. On Prudnik's ride step 3
saved the near map's 8.4 ms against +2.6 of lookups at 842 x 755; at 1920
x 1080 the lookups (about 3.3 times the pixels) take most of that back.
The drawing saved does not change with the resolution; the lookups grow
with it. Next: skip a map's dynamic taps when nothing was drawn into it,
and outside the light-space rectangles of what was (task 50, "The lookups
at high resolution").

### Measuring noise

The near map's pass runs at one of two levels, about 5 or about 12 ms at
4096 with the same 648 draws: steady through a view, switching between
runs or at a view's change (both A and B). The Deck has a 1 GB carve-out
of dedicated GPU memory, about 950 MB of it in use while TSRE runs, TSRE's
share about 315 MB, about 1 GB more of TSRE's in shared memory: where the
driver places the 64 MB near map and its cache may decide the level (not
confirmed: three runs sampled, all at 12 ms). Compare steps with alternated
runs, never one pair.

## Expected gains (estimates, to measure)

| Step | Saves a frame while riding |
| --- | --- |
| 1.1 Vegetation from the caches | 1.5 to 2.4 ms (more at 4096) |
| 1.2 Moving casters where read | most of the middle map's 3.7 to 5.9 ms and the far map's 2.0 to 3.8 ms |
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
- Step 1.1 must give the same images (the pixel diff within 1), step 1.2
  too with the sun above 6 degrees and no high bridges in view.

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
4. (2026-11-17) The volumetric shafts take the near and middle maps where
   they hold a point ("makes sense"), so the far map can leave out the
   trains the middle map holds. Reverted the same day: 25 ms a frame (the
   user's report); the far maps alone again.
5. (2026-11-17) Vegetation only on route scenery that does not animate: a
   stop-gap until vegetation's shadows become transmission shadows with
   moving maps.
