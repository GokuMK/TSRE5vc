# Task 46 - Shadows from Local Lights

Status: B merged from `feature/lamp-shadows` (2026-11-07), with the Lamp shadows setting and the light schedule. A (moving trains, trains' head lamps) and the cab part of C merged from `feature/train-lamp-shadows` (2026-11-09); moving trains tested live by the user, the cab not yet. The fog's lamps through the maps merged from `feature/fog-lamp-shadows` (2026-11-10), tested live by the user.

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
graphics presets: Basic off (no local lights), Extended (lamps lighting
the scene, as Open Rails' unstable versions) and Advanced Medium. `TSRE_LAMP_SHADOWS` and the other variables still override it.
Polish names at the merge.

## B: soft edges (2026-11-07)

The 2 x 2 blend showed every texel as a step along a shadow's edge (the
user: a lattice pole's shadow by Zawiercie's lamps,
`-5373,14873,834.27,341.11,926.10,333.13,-58.21`). Now a box of
`LocalLightShadowWidth` texels (3) around the point, sliding with it
through its texel, is compared: (width + 1) squared texels (16), blended;
the point moved toward the lamp (width + 1) / 2 times as much (0.1 m and
0.8 % of its distance), as the compares reach farther. Compared at the
user's camera (mean change of the picture against 2 x 2, the user's
choice):

| Kernel | Compares | Change | Look |
|---|---|---|---|
| 2 x 2 | 4 | | every step |
| B-spline, 4 x 4 | 16 | 1.8 | about as before |
| Box of 2 | 9 | 3.0 | corners of steps left |
| Box of 3 (chosen) | 16 | 4.6 | smooth; a pole's thin shadow a light band |

The cost at the yard (148 lamps; each lookup repeated 8 times, as the
GPU times scatter by 1 - 3 ms; the lookups count in about 40 % of the
frames, perhaps those drawing an environment map face over the yard):
the lookups about 1.1 ms in those frames with 2 x 2, 1.9 with a box of 2,
2.2 with a box of 3; over all frames a box of 3 about 0.5 ms more than
2 x 2, 0.25 more than a box of 2. Later, if wanted: the box by the
setting (2 at Low).

## The release plan and the 100 m cap (2026-11-07)

- The first release: B (static maps) with the Lamp shadows setting. The
  next one: moving trains (A, or a dynamic layer) and cabs (C). Point
  lights: maybe one day, no use seen (the user).
- Right after the moving trains: the fog's lamps shadowed (the user,
  2026-11-07; the trains' shadow columns in lamp-lit fog give it most of
  its impact); done 2026-11-09, in every slice (below, "Lamps in fog").
  The plan was: The fog's lamp pass (`RhiFog.cpp`, cells of 8 x 8 or 16 x 16
  pixels, 64 slices) takes the lamps' map arrays; each lamp's light in a
  cell scaled by one lookup of its map at the cell's segment (its middle,
  or two points), no blend. Near slices only: far ones are metres deep
  and the lamps' shadows small within them (maps end 100 m from their
  lamp), so they skip the lookups. Measured as the soft edges were (each
  lookup repeated), at Prudnik's yard in thick fog by night; today the
  fog ignores the maps, its lamps' light filling the air behind walls and
  wagons.
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
    test.

  Done (2026-11-07): `ShapeLights::Schedule` and `ShapeLights::lit`; a
  static object's lights off by its schedule are not submitted at all
  (`RenderQueue::setLightsOn`: no light, glow or disc; glowing surfaces'
  lights skipped in the gather). Static objects only: signals, level
  crossings, car spawner cars and trains keep their lights. The view sets
  night each frame (`ShapeLights::setNight`): as night textures load, every
  lamp on with time of day off; `TSRE_LIGHT_SCHEDULE_SUN` (degrees, off 2
  above) to compare switches. The yard at dusk on the profile's 11 July
  (Auto exposure): by the night textures' angle the lamps are on before
  18:00 (faint pools) and plain at 19:00 under a bright sky; by a 3 degree
  one off at 19:00, on by 20:00 (both alike from there).

  A lamp off has no map and
  no lookups, so by day only Always lamps cost; the per-pixel skip (no
  lookup where the lamp's light is too faint to see at the exposure) is
  kept as a note for them.

## A: moving trains (2026-11-08 to 09)

**Standing trains** go into the cached maps: a car whose placement has not
changed for 2 s (`Eng::StandingMs`; `TSRE_TRAIN_STANDING_MS`), not seen
from its cab, is a static caster of the lamps' maps alone (not the sun's
cache), out again as it moves off (`RenderQueue::addLampShadowObject`).

**Per-pixel boxes, dropped.** Each car as a body box and two bogie boxes,
tested in the lit shaders along each pixel's way to each lamp (the first
design above): +2 ms at Zawiercie's platform by night, +7 ms at the
standing EMU at Prudnik (3 cars, 9 lamps). A skip where a lamp's light is
faint leaked light through the last IC (many lamps' faint light adding
up; the user: "serious regression"). The boxes also darkened more than the
real train does (the tracks beyond it: the platform lamps' light passes
over the coaches).

**Tiles over the maps** (the user's design, 2026-11-08): a lamp reaching a
moving car takes a tile, a layer of its set's array after the maps' (from
`LampShadowSet::tileBase`; 16 fine, the rest of 64 coarse;
`TSRE_LAMP_TILES`), its map copied in each frame (through a staging array:
QRhi copies between two textures, not within one) and the cars drawn over
it, a pass a tile. The nearest 16 every frame, the others every other
frame (about 1 ms saved at 64 tiles). The lights read a tile as any layer.
- The cars as boxes, their far faces stored (drawn as mirrored): what lies
  inside a box (its car's roof and walls) stays lit, what lies beyond is
  shadowed. A flat box top had darkened the coaches' curved roofs.
- The driven cab and the cab ridden (`Eng::lampDetail`) draw their own
  shapes: from inside the cab its light only through its windows (glass
  under the 0.3 alpha cut, as the sun's shadows). The level drawn:
  `ComplexShape::shadowLevel()` (its coarsest, or a glTF shape's floor;
  a car pushes it as a stand-in only where it is not the view's).
- Real shapes against boxes at the standing EMU: +0.12 ms of the GPU and
  +0.13 ms of the CPU a lamp (9 lamps, 1,553 parts). The cars' last levels
  would not help this content: the EMUs and the IC's ED160 have one level,
  as about two thirds of the route's car shapes; leaving out a car's small
  parts neither (parts over 1 m kept 1,206 of 1,553). Shapes task 17
  (levels made at load) is the way.
- Bugs met: the tiles' viewport on y-down framebuffers (QRhi's viewports
  count from the bottom); a set's copies queued in a batch the set before
  had already submitted (made after the boxes: no train shadow at
  Zawiercie, the user).

**The tiles' cost** (GPU pass times, `TSRE_GPU_PASS_TIMES=1`, the standing
EMU at Prudnik, 9 tiled lamps): first an atlas a set with a branch of its
own in `localLightShadow`: the frame +2 to 2.5 ms against no tiles, the
view alone +1.0 to 1.4 ms. The branch was the cost (lamps taking different
branches; the same code in it cost 1.3 ms; not the atlas, the copies, the
compression or the CPU's share of the power: all tried). As layers, one
code path: the copies 0.5 ms, the tiles' passes 0.15 ms, the view +0.45 to
0.9 ms, the frame +1.3 ms. The copy could be the next saving.

**Trains' head lamps** (the user, 2026-11-09): the nearest moving spot
lamps' maps drawn each frame (as any lamp's, keyed by their car and order,
from this frame's lamps: last frame's would put a map a metre behind its
lamp at 100 km/h), with the other moving cars as boxes but their own car.
A loco end's lamps (one car's within 30 degrees) share one map, from their
middle with their widest cone; groups reaching under 10 m (tail lamps)
take none. The setting `core.rendering.movingLampShadows`: Off, Low (the 2
nearest loco ends: a train passing on the next track), Medium 3, High 6;
default Low as Extended, Basic off, Advanced Medium (the user).
- Cost at the user's Prudnik view by the yard (a heavy view: 34 ms of the
  CPU for the scene): a map a lamp, 9 maps: 2.84 ms of the GPU, about 4 ms
  of the CPU. Three lamps 1.53 ms and 3.1 ms; three loco ends (one map
  each) 0.96 ms and 1.9 ms: a loco end about 0.3 to 0.5 ms of the GPU and
  0.65 to 1 ms of the CPU. Three loco ends, the Deck's target (the user):
  about 1 to 1.5 ms and 2 to 3 ms there.
- A map's CPU: about 0.19 ms finding its casters (every queued instance
  tested), 0.17 ms recording them, 0.29 ms the pass. A grid of the casters
  (32 m squares, made at a frame's first map) cost 6 to 9 ms to make:
  left out.
- The shared map moves the sleepers' small shadows a little against a map
  a lamp (`TSRE_LAMP_MOVING_PER_LIGHT=1` to compare).
- Checked: the standing train's head lamps through this path give the
  cached maps' image exactly. A moving train at night: to test live.

**Review before the merge** (2026-11-09, an independent review of the
branch, its findings checked against the code, then fixed):
- A moving lamp's map and the tiles are used only in the frame they were
  drawn (`lampShadowsSerial`): a map kept from an earlier frame (its lamp
  past the setting's count) held the trains where they had been.
- A standing train's lamps on the cached maps again, as a static
  consist's (`light.moving` only for a train not standing): they had gone
  through the moving path, a map drawn each frame. The user's Prudnik
  view: maps drawn in 39 frames against 613, the image the same, the CPU's
  scene 28 ms against 34.
- Standing cars (`CarBox::standing`) are left out of the moving lamps'
  boxes (their shapes are in those maps), and out of a tile only while
  standing: a car moving off has its boxes at once (its lamp's map holds
  it where it stood until drawn again).
- A lamp takes only the cars within its view (its frustum), not all in
  its reach.
- One staging layer a set (each tile's copy through it in turn): Low about
  25 MB of maps, Medium 40, High 160 (main without part A: 16, 24, 98).
- A car's boxes are all added (sizes under 1 cm taken as 1 cm): a box
  dropped would have shifted the boxes within onto the next car.
- The tiles past the nearest 16 drawn every other frame by their tile's
  turn: by their rank, which changes as the camera moves, a lamp could miss
  its turn for good.
- Without moving cars no tiles and no gather of the moving lamps; a
  group's cone covers its members' (each one's way off the middle added);
  the cab's stand-in only with local lights on.
- Checked against main's build: bbb's 924 lamps without trains the same
  image (within 2 levels of 255), the CPU the same (the scene 3.5 against
  3.6 ms), the GPU the same (16.8 ms); the standing EMU's tiles (moving
  and standing) and Zawiercie's IC at its fixed times the images of before
  the fixes (moving frames within the train's run-to-run placement). All
  targets built, ctest and the app's suites passed.

**Tools.** `TSRE_GPU_PASS_TIMES=1`: each target's GPU time in real frames
(Vulkan timestamps), stable to about 0.05 ms within a run, the view up to
0.4 ms between runs; the timer itself costs about 1.6 ms of the CPU a
frame (bbb's lamps), so frame times are taken without it. RenderDoc's
replay is no use for such comparisons on the Deck (the same passes 40 %
slower in one capture than in another).

## Lamps in fog (2026-11-09)

The fog's lamp light (`RhiFog.cpp`, the injection: each lamp's light along
a cell's slice at 4 points, equiangular as seen from the lamp) passes
through the lamps' maps: each point scaled by its lamp's map there, one
texel, no blend (a cell of 8 or 16 pixels is wider than a map's texel),
none toward the map's edge and from 80 to 100 m from the lamp, as the lit
shaders fade theirs. The maps are bound to the injection (bindings 25 and
26); a lamp's layer, set and matrix come from the light data the fog
already reads, so the moving trains' tiles and the head lamps' maps come
with them (every lamp read from a tile, `TSRE_LAMP_TILE_TEST=1`: the same
image). The set chosen a lamp, outside the points' loop (as the lit
shaders'). `TSRE_FOG_LAMP_SHADOWS=0`: not (to compare).

- At Prudnik's yard (part B's scene, 148 lamps, by night in valley fog,
  Mist High): a lamp shining across a row of wagons no longer lights the
  air beyond them; the fog there dark, its edge along the wagons' tops.
  7 % of the pixels changed; without the lookups the image is main's.
- Cost (the fog volume's own GPU time, now its own entry in
  `TSRE_GPU_PASS_TIMES`, "fog volume"): 4.1 to 4.3 ms without, +0.22 to
  0.25 ms with (three runs).
- Near slices only (the plan) did not pay: to 100 m (32 slices) the same
  time (+0.18 to 0.24 ms), to 50 m (27) up to 0.12 ms less, and a
  shadow stopped at that distance, the fog beyond the train's far end lit
  (a visible step). Every slice reads them.
- Prudnik's other views and Zawiercie had no fog to test: the fog's floor
  had left their detailed tiles out as wrong against the blank distant
  terrain about them (15 tiles around the user's view by the yard, 26 at
  Zawiercie). Distant terrain no longer sets where the fog lies, the fog
  laid over it (task 33, the user): thick fog at the user's view.

## Cabs: the live test and the next step (2026-11-10)

The user, from the driven cab by night: the lamps' shadows inside it look
poor; better where the cab is part of the loco's shape, much worse in the
FLIRT's (its loco shape not drawn from the cab), as if the maps were 256
texels at High. Hard to repeat by hand (drive mode). Found in the code:

- **Inward-facing walls cast nothing.** Shapes go into the lamps' maps with
  back faces culled (`RhiRenderer::recordDraw`, `key.cullBack` for
  every single-sided material; only the sky enclosure's maps draw both
  sides). A 3D cab made as an inside shell has its walls facing inward:
  from a lamp outside they are back faces, so its light comes in through
  the walls; only the desk, the seats and the far wall cast. Where the cab
  is inside the loco's shape, the body's outward faces cast.
- **Tinted windows block it all.** A caster's texel casts from alpha 0.3
  up (`GLUU::alphaTest`, `Shadows.fs`), and glass in an opaque material
  whatever its alpha: tinted cab glass shadows the whole cab.
- **The resolution is High's, the look softer.** The nearest 16 lamps'
  maps are 1024 texels, the others 512, over the lamp's cone to 150
  degrees: a texel about 7 cm across at 10 m, 15 cm at 20 m (twice that in
  the others), and the soft edge's box of 3 texels widens an edge to 30 to
  60 cm: window pillars and frames wash out.

Next, in this order, once the ENG and cab rework on its own branch has
landed here (the user):

1. The driven and the ridden cab's shape double-sided in the lamps' maps
   (their tiles and the moving lamps' maps): one shape, so little cost
   (unculled, the whole scene cost the sun's maps 5 to 10 ms at CMK).
2. Blended (translucent) parts as alpha-tested in the lamps' maps, at a
   cut high enough that tinted glass lets the light through; for the cabs,
   and likely for all the lamps' maps (light through more of what is
   translucent; to check on stations and trees).
3. If those are not enough: a narrower soft edge while the camera is in a
   cab.
4. Later, with better cabs to test: a map aimed at the cab alone for the
   lamps lighting it (a few metres across instead of the whole cone).

A capture for it: the harness rides a train's cab (`trainView` "cab", or
`trainVehicle` with an eye and a point in the vehicle's frame) at fixed
simulation times; a FLIRT at a lit platform by night would repeat the
user's view.

Open: the cab's next step (above); the copies' cost; the task's guide and
the release notes.

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
