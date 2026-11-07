# Task 46 - Shadows from Local Lights

Status: B with per-lamp redraws, 150 degree maps and the Lamp shadows setting (2026-11-07), on `feature/lamp-shadows`.

## Objective

Lamps' light stopped by what stands in it. First the trains: on Prudnik's
yard at night a train without its shadow under the lamps does not fit the
scene (the user;
`core.startup.camera=-5373,14872,-750.32,336.73,-601.77,-401.70,-12.51`).
Then the static world, which gives the biggest visual impact, and cab and
coach interiors.

## Step 0: measured (2026-11-07)

Prudnik's yard with a train, the user's camera, 0:30, the user's profile
with the frame-rate limit off, 842 x 755 on the Deck's display
(`TSRE_RHI_TRACE`'s `lamp-shadow trace` lines, `QueueRenderer::
traceLampCasters`):

| | |
|---|---|
| Lamps | 148 (30 m reach) |
| Static casters queued | 35,724; moving 776 (88 within 300 m) |
| A near lamp's static casters | median 252 (51k triangles), at most 783 (93k) |
| The nearest 8 lamps' together | 1,507 casters, 293k triangles (19 to 36 m away) |
| The nearest 16 | 3,401 casters, 624k triangles (to 62 m) |
| The nearest 32 | 8,599 casters, 1.54M triangles (to 124 m) |
| Lamps reaching a moving part | median 0, at most 2 |
| Lamps a column | at most 12, 4.5 on average, 8 at the camera |
| Frame (live) | paint 43.7 ms, GPU 41.6 ms (median), gather 28 ms; the sun's shadows 2.7 ms |

So one lamp's map is cheap on the GPU (about 50k triangles); its CPU cost
is choosing its casters among the 35,724 (a pass over them all a lamp).
At one or two maps a frame the nearest 32 lamps are drawn in about a
second, then only sampled; sampling (up to 12 lamps a pixel's column, the
GPU already at 42 ms here) decides how many lamps can be shadowed. B
first, the user decided, then measure how many lamps it carries before
deciding on A's boxes for world objects.

## B, first version: measured (2026-11-07)

`RhiRenderer::prepareLampShadows` (after the sun's maps, whatever the
sun does): steady spot lamps (not glowing surfaces, signals or what moves)
nearest the camera get a map, as many as `TSRE_LAMP_SHADOWS` (0, off, by
default for now), `TSRE_LAMP_SHADOW_SIZE` texels (512), drawn
`TSRE_LAMP_SHADOWS_PER_FRAME` (2) a frame, kept by the lamp's place in the
world (its tile's metres added) and drawn again when the static casters
change (the shadow signature, one for all: per lamp later). A map is a
layer of an R32F array (QRhi draws into no layer of a depth texture)
written by the shadow program's `TSRE_LAMP_SHADOW` variant, one shared
D32F depth for the test; the lamp's view as wide as its cone to 120
degrees, to its reach, its casters chosen around the lamp
(`QueueRenderer::planLampCasters`). A light's data is eight texels, its
layer and matrix added (`LightGrid::Light::shadowLayer`, `shadowMatrix`);
`prepareLights` gives each light the map drawn for it. `LocalLights.glsl`
blends 2 x 2 compares (texel fetches) at the point moved 5 cm and 0.4 %
toward the lamp, fading to none toward the map's edge. Spot lamps only.

Prudnik's yard (step 0's camera, the user's profile, 842 x 755 on the
Deck's display, 20 s live, the frames after every map was drawn; 8 maps a
frame):

| Lamps shadowed | GPU median / p90 | Against none | Memory |
|---|---|---|---|
| 0 | 41.9 / 45.3 ms | | |
| 16 | 44.0 / 44.6 ms | +2.1 ms | 16 MB |
| 48 | 45.2 / 47.2 ms | +3.3 ms | 48 MB |
| 148 (all) | 45.3 / 48.8 ms | +3.4 ms | 148 MB |

- Every lamp of the place can be shadowed on the Deck: the cost stops
  growing past about 48, as a pixel takes only its column's lamps (at
  most 12) and far lamps cover few pixels.
- Drawing: 0.5 to 1.8 ms of CPU a map; 148 maps in 19 frames, then none
  while the camera stood (the signature unchanged). The shadow phase's
  CPU grew from 2.7 to 3.5 to 3.8 ms a frame: the lights gathered a second
  time and the lamps chosen (to trim).
- The parked wagons now cast their shadow across the ground away from the
  lamps; no acne seen. The static world is shadowed by B: designer-marked
  boxes for world objects (A) are not needed for it; moving trains are
  not in the static maps (A's boxes or a dynamic layer).

Next: per-lamp signatures (one object loading redraws every map now),
the memory (16-bit maps, smaller far maps, the system memory tier), the
second gather, a setting, then the moving trains.

## B: 16-bit maps, two sizes, one gather (2026-11-07)

- Maps hold 1 - depth in R16F (floats are finest near 0: about 1 cm at
  20 m); `TSRE_LAMP_SHADOW_BITS=32` for R32F.
- Two sets: the nearest `TSRE_LAMP_SHADOWS_NEAR` (16) lamps in fine maps
  (`TSRE_LAMP_SHADOW_SIZE`, 512), the others in coarse ones
  (`TSRE_LAMP_SHADOW_COARSE_SIZE`, 256); a lamp changing sets keeps its old
  map until the new one is drawn. The light's data says which set (the
  fourth texel's w).
- The lamps chosen from the lights the scene took last frame, moved into
  this frame's tile: no gather of their own (a frame late is no matter).
- By day with the lamps dimmed (Lamps by day: Dimmed, no sky enclosure, no
  auto exposure) nothing is chosen or drawn: their shadows cannot be seen;
  the maps stay for the evening. Under cover by day (enclosure, full
  strength, auto exposure): a cover test a lamp, later.

All 148 lamps of Prudnik's yard (as above):

| | Memory | GPU median / p90 | Shadow phase (CPU) | Image against a |
|---|---|---|---|---|
| None | | 41.9 / 45.3 ms | 2.7 ms | |
| a: 32-bit, all 512 | 149 MB | 45.8 / 48.4 ms | 4.0 ms | |
| b: 16-bit, all 512 | 75 MB | 45.0 / 48.7 ms | 3.3 ms | near 0.01 (most 75), far 0.01 |
| c: 16-bit, 16 x 512 + 256 | 25 MB | 45.9 / 46.7 ms | 3.3 ms | near 0.01, far 0.16 (most 59) |
| d: 16-bit, 16 x 512 + 128 | 13 MB | 45.5 / 46.8 ms | 3.2 ms | near 0.01, far 0.26 (most 96) |

(Mean differences of 255 in a crop by the wagons and a far one; the far
ones on thin pole shadows.) c is the default: every lamp in 25 MB, the
image as a's, the GPU's time the same; a system memory tier is not needed
at this size. The shadow phase lost the second gather (about 0.7 ms).

## B: per-lamp redraws (2026-11-07)

- Each object the cached maps hold gives its place as well as its hash
  (`RenderQueue::addShadowObject`: world tiles' objects and standing
  consists, with their bounding spheres). When the shadow signature
  changes, the objects are compared with last time's by hash; each one
  added or gone makes the lamps it reaches (their reach and its radius)
  stale. A stale lamp keeps its map until drawn again. Changed objects
  outside a box around all the lamps, widened by the longest reach, are
  dropped first (a row of tiles loading: thousands of objects kilometres
  away).
- A map drawn while static casters within its reach (and 50 m) were still
  loading is drawn again once they have come
  (`QueueRenderer::lampShapesWaiting`).
- `TSRE_LAMP_SHADOW_GLOBAL=1`: the first version's rule (every map on any
  change), for comparisons.
- Found on the way: `WorldObj::size` was never set for platforms, sound
  sources and regions, car spawners, groups and watermarks (garbage, up to
  1e37). Their spheres reached every lamp, so each tile edge made every
  map stale. It is -1 by default now (unknown: 50 m for the lamps, no size
  cull, as before for the objects that set it).

A ride: PSK_2026, train 154 (1051 Czestochowa - Gliwice) in its cab from
its Zawiercie Borowe Pole stop at 10:58:30 (about 5 km out, beyond the
3000 m object range, so the yard's lamps do not all come at load),
through Zawiercie (11:02 - 11:03) and the yard at 33 m/s, 540 s; lit as
at 0:30, 64 lamps; the user's profile, no frame limit, clouds, mist,
shafts, ambient occlusion and the sun's shadows off; 842 x 755.

| Rule | Maps drawn | Frames with stale maps | Most stale at once | Maps' CPU in all |
|---|---|---|---|---|
| Global (first version) | 1085 | 124 | 64 | 643 ms |
| Per lamp, sizes unset | 954 | 56 | 64 | 574 ms |
| Per lamp | 862 | 8 | 3 | 526 ms |
| Per lamp, the box first | 864 | 13 | 3 | 516 ms |

- The maps left are first draws: lamps entering the nearest 64 as the
  train runs, and lamps moving from a coarse to a fine map (about two maps
  a lamp passed). A drawing frame: 1.1 ms median, 1.6 ms p90, 3.9 ms at
  most.
- Testing the lamps against a tile edge's changes (7,360 objects) took
  3.2 ms in that frame without the box; with it the edge frames are as
  any other.
- The pacing is the same for every rule: 32 - 33 ms a frame to Zawiercie,
  53 ms in the yard (GPU 33 ms there); the shadow phase 0.11 ms median,
  2.2 ms p99 in the yard.
- With lamp shadows off (the same ride): the pacing the same (the yard
  53.0 against 53.3 ms median, p99 80 ms both), the GPU 0.3 - 1.5 ms less
  (the yard 32.5 against 33.4 ms). The few frames of 12 - 13 ms in the
  shadow phase in every rule's ride are the ride's own spikes, as many
  with lamp shadows off (305 frames 10 ms over their neighbours against
  245), there in the gather, environment or scene phase.

## B: wider maps, the hand-over, a setting (2026-11-07)

**Wider maps.** A map was as wide as its lamp's cone to 120 degrees, its
shadows fading over the outer fifth. Street lamps light 75 degree cones:
for a lamp 9 m up the shadows faded out 12.5 - 15.6 m from its foot while
its light (30 m reach) went on to about 28 m, a ring lit through
everything (the user, from above the yard:
`-5373,14872,-803.13,364.84,-599.63,767.30,-37.72`). Maps are now as wide
as the cone to 150 degrees: a 75 degree cone whole, out to the light's
reach for lamps 8 m up or higher (an 85 degree cone keeps an unshadowed
edge where its light grazes the ground). `TSRE_LAMP_SHADOW_FOV`: the
widest view in degrees, for tests. The same texels cover a disc 2.15 times
wider: near shadows softer, the bogie still showing (enough for now; to
see with other shapes later).

All lamps of the yard (as in the 16-bit table), the nearest 16 in fine
maps:

| Maps | Memory | GPU, yard camera | GPU, from above |
|---|---|---|---|
| 120 degrees, 256 + 256 | 19 MB | 43.8 ms | |
| 120 degrees, 512 + 256 | 25 MB | 42.8 ms | 22.6 ms |
| 120 degrees, 1024 + 256 | 52 MB | 43.2 ms | |
| 150 degrees, 512 + 256 | 25 MB | 43.6 ms | 25.1 ms |
| 150 degrees, 1024 + 512 | 103 MB | 44.2 ms | |

At 120 degrees 256 made the wagon's shadow a blob, 1024 stair-stepped
(texels finer than the 2 x 2 blend softens). 150 degrees costs GPU time
where more ground is shadowed (its pixels skipped the lookup before).

**The hand-over.** A lamp moving between the sets lost its old map at
once: no longer wanted in that set, its layer was the first one free for
the next lamp, while its new map waited to be drawn (two maps a frame),
its light unshadowed meanwhile: a flash of light as a lamp came near or
went (the user). A lamp now keeps its old set's map until its new one is
drawn; each set has two spare layers so a full set still has room for the
lamp coming (`TSRE_LAMP_SHADOW_NO_KEEP=1`: the old rule, for comparisons).
And a lamp with a map ranks nearer than it is (a fine map 0.8 of its
distance squared, a coarse one 0.9), so lamps about as far keep their
places instead of trading them as the camera moves.

A quick ride: train 154 from 11:05:00, about 0.9 km before the yard
(loaded at start), past the yard camera, 90 s, all 148 lamps (the log's
counts: maps taken from a lamp in view while its new one waited; maps
taken from a lamp just past the budget; lamps in view without a map that
had one the frame before):

| Rule | Handed over | Dropped | Lost | Maps drawn |
|---|---|---|---|---|
| Old | 10 | 102 | 4 | 463 |
| Old map kept | 0 | 102 | 4 | 460 |
| Old map kept, ranks kept | 0 | 101 | 2 | 463 |

The two lost are lamps coming back into the budget after losing their map
(a few hundred metres away); the dropped ones are the ride's turnover, 3 km
of lamps leaving the budget.

**The setting.** `core.rendering.lampShadows` (Rendering, Lights): Off,
Low, Medium (default), High; the 128 lamps nearest the camera:

| Quality | Maps | Memory |
|---|---|---|
| Low | one set of 256 (each lamp drawn once) | about 17 MB |
| Medium | the nearest 16 at 512, the rest at 256 | about 25 MB |
| High | the nearest 16 at 1024, the rest at 512 | about 100 MB |

Cached by `RouteEditorGLWidget` (as Lamps by day) and handed to
`prepareLampShadows` each frame; a change releases the maps (Off frees
them) and the resource sets made with them (`forgetResourceSets`). The
graphics presets: Basic off (no local lights), Extended and Advanced
Medium. `TSRE_LAMP_SHADOWS` and the other variables still override it.
Polish names at the merge.

## The release plan and the 100 m cap (2026-11-07)

- The first release: B (static maps) with the Lamp shadows setting. The
  next one: moving trains (A, or a dynamic layer) and cabs (C). Point
  lights: maybe one day, no use seen (the user).
- Maps end 100 m from their lamp (`RhiRenderer::LampShadowFar`,
  `LocalLightShadowFar` in `LocalLights.glsl`): a lamp asking for a longer
  reach (its object's light range, up to 250 m) lights on past it
  unshadowed, its shadow fading out from 80 m. Bounds a map's casters (100
  m is 11 times the area of 30 m) and its texels.
- A cone up to 75 degrees is in its map whole, at any height and reach;
  only wider cones (a lantern's 85) light a band without shadow (assets
  task 05, Shadows).
- By day with Auto exposure (also Full strength or the sky enclosure) the
  maps are chosen, drawn and looked up as at night: in the sun a lamp's
  light, and so its shadow, is too faint to see.
- **For this release: lamps switch on at night** (the user). A world
  object's lights (`.sd`, glTF) are on only at night by default, as night
  textures: first their angle (on with the sun below 20 degrees, off above
  23; `NightTextures::wantedForSun`), better too early than too late;
  captures against a switch near sunset (on below about 3, off above 5)
  to choose. A light schedule: `Night` (the default) or `Always` (lamps
  under roofs and in tunnels), hour ranges later; for a shape's lights
  together (per light only once a shape mixes them):
  - the shape's default: `ESD_TSRELightSchedule ( Always )` in the `.sd`,
    `extras.TSRE_lightSchedule: "Always"` in a glTF scene (beside
    `TSRE_distanceLevelExtension`);
  - the placed object's override: `TSRELightSchedule ( Always )` in the
    `.w` by `TSRELightRange`, in the static object's properties;
  - the object's over the shape's over Night; Polish names, a round-trip
    test. A lamp off has no map and
  no lookups, so by day only Always lamps cost; the per-pixel skip (no
  lookup where the lamp's light is too faint to see at the exposure) is
  kept as a note for them.

## Today

- Local lights (task 21) light through 512 x 512 columns of 16 m around
  the camera (task 45) without shadows: a lamp reaches through trains,
  poles and roofs. Each light is four texels of data; the fourth has two
  free values.
- A scene holds over a thousand lights (bbb's lamps 1,164), 7 to 32 in a
  column. Street lamps are spots pointing down with cones up to 85
  degrees (assets task 05).
- The sun already caches: each cascade's static casters drawn into a cache
  when they change, the moving casters (trains) on top each frame, the
  redraws decided by a signature of the loaded objects
  (`RenderQueue::addShadowSignature`, task 37 part 2). Static consists are
  in the cache.

## What games do

- A shadow budget: only some lights cast shadows (4 to 32), picked by
  distance, size on screen and brightness; the others light unshadowed.
- A shadow atlas: one depth texture, a tile a light, sized by importance;
  point lights as six faces (or four), spots as one.
- Cached static shadows plus a dynamic layer: static casters drawn once a
  light, moving objects drawn again each frame into their own layer or on
  top (Unreal's cached shadows, Unity HDRP's mixed cached shadow maps,
  idTech's cached atlas).
- Analytic shadows for moving objects: capsules or boxes tested in the
  shader, soft and cheap from many lights (Unreal's capsule shadows,
  The Last of Us).
- Screen-space contact shadows: short marches through the depth buffer
  toward a light, for small details near the camera.
- Not for TSRE: baked shadow maps (MSTS shapes have no lightmap UVs and
  routes are edited), virtual shadow maps and ray-traced shadows (too heavy
  for the Steam Deck; QRhi has no ray tracing).

Static shadows are far cheaper than moving ones: drawn once, then only
sampled. A moving train would be drawn again every frame into every lamp's
map it falls in (10 to 20 at a station).

## Design

### A. Trains as boxes, shadowed by every lamp (first)

- Each car a body box and two bogie boxes (so the gap under the body
  shows), from its placement every frame, binned into the 16 m columns
  (a second list a column), uploaded with the light grid.
- The lit shaders test each lamp a pixel takes against its column's boxes
  (the segment from the pixel to the lamp, a slab test a box); a soft edge
  from the lamp's radius.
- Every lamp shadows the train at any cone, with no extra pass; the
  outlines are boxes (no pantograph, no wheels) and the train is not
  shadowed by roofs or poles. About 1 to 2 ms on the Deck: to measure.
- Later: route designers mark crucial world objects as boxes (the user):
  one box for a shape or one a matrix or part, in the `.sd`, animated parts
  following their matrices.

### B. Static world shadow maps for the lamps near the camera

The biggest visual impact (the user).

- **One map a lamp, two for wide ones; no cubes.** Lamps shine down with
  cones up to 85 degrees. One perspective map of about 110 to 130 degrees
  fitted to where the light still matters, the shadow fading to none at
  its edge: 60 degrees from straight down, 15 m from an 8.9 m lamp, the
  light is about a tenth of what falls below it. The light's reach bounds
  the map: 30 m unless its object asks for more (`LightGrid::DefaultReach`;
  an `.sd` or glTF `Range`, up to 250 m; assets task 05's lamps 40 m), so
  at 8.9 m at most about 73 degrees from straight down, its depth range
  and its casters (the objects within the reach) too. Wide or important lamps
  get two maps tilted apart (about 100 degrees each, along the arm or the
  track). Not a paraboloid map: it bends straight edges, and MSTS
  casters' long, coarse triangles would warp their shadows near the lamp.
- **An atlas with a budget:** one depth texture, a tile a lamp (its place
  and size in the light's two free values); 512 x 512 near, 128 to 256 for
  farther lamps. The budget follows the GPU memory left, as the textures'
  does (task 42): on a 12 or 16 GB GPU every lamp fits (about 0.5 GB for a
  thousand at 512 x 512 and 16 bits); on the Deck the lamps near the
  camera, by distance and brightness. Sampling stays a pixel's lamps (7 to
  32 a column); on the Deck perhaps only the brightest few of a column
  shadowed.
- **Drawn once, kept while it fits, in tiers** (the user, 2026-11-07):
  1. the atlas on the GPU;
  2. maps the atlas let go kept in system memory, compressed (depth maps
     compress well), and uploaded again when wanted: a 512 x 512 map at
     16 bits is 512 KB, about 0.3 to 0.6 ms to upload on the Deck, cheaper
     and steadier than drawing again every static caster within its
     reach (on the Deck the GPU's memory is system memory too, so this
     tier's budget is modest there);
  3. drawn again only for a lamp never drawn or whose static casters
     changed (a signature a lamp, of the objects within its reach, as task
     37's), in idle time (task 43); a lamp without its map yet lights
     unshadowed and fades in.
  Later perhaps a disk cache keyed by the object and its content: drawn
  once until the route changes.
- Moving trains keep A's boxes outside (see C for the camera's own car).
- The maps in the lamp's own frame (its tile and place), not the camera
  tile's submission space, so they stay valid as the camera's tile
  changes.

### C. Cabs and interiors

- A cab's or coach's own lamps and its geometry move together: their
  shadows are fixed in the car's space, drawn once a cab and kept.
- Outside lamps shining in (the user, 2026-11-07): as the train passes the
  station's lamps, the window frames' shadows sweep across the inside. A's
  boxes cannot give that (a car's box would shut out all of their light or
  none of it), so with the camera inside a train its own car is drawn
  each frame into the maps of the lamps within its reach (two to six), on
  top of their cached static maps (a dynamic layer, as the sun's moving
  casters on its cache); the other cars keep their boxes.
- Screen-space contact shadows for the small details near the camera (the
  user: useful in cabs and coaches).

## Steps

0. **Measure** at the user's camera at 0:30: the lamps reaching the train
   (a car), the train's draws and triangles, lamps a pixel's column, the
   frame time; the static casters within a lamp's reach (B's cost a lamp).
1. A: trains as boxes.
2. B: the atlas, its cache and budget, one-map lamps.
3. B: two-map lamps, smaller maps for far lamps.
4. A: designer-marked world boxes.
5. C: the camera's own car in the nearby lamps' maps each frame (window
   frames), cabs' own lamps.

Later: the disk cache, the fog's lamps shadowed (halos), contact
shadows.

## Questions

- How soft the box shadows are; whether the fog's halos take them.
- The atlas's size on the Deck (4096 x 4096 at 16 bits is 32 MB: 64 tiles
  of 512).
- Which lamps enter B's budget: distance, brightness, size on screen.
