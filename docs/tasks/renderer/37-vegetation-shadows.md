# Task 37 - Vegetation shadows and cached shadow maps

Status: the TREES forest measured (2026-10-21). Part 1, the forest's own
costs (steps 1-3), done on `feature/forest-performance`. Part 2: snapped
maps and the static cache (step 4), the vegetation map set (step 5, QRhi)
done there; the depth gap and impostor shadows facing the sun open.
Updated 2026-10-21.

Related: [task 10](10-shadows-gather-pass.md) (the three shadow maps),
[task 27](27-sky-enclosure-and-tunnel-lighting.md) (the sky enclosure),
[task 36](36-foliage-backlighting.md) (crowns dark from below),
[vegetation objects](../world/vegetation-objects.md) (forests of shapes,
impostors), [glTF levels of detail](../shapes/10-gltf-native-lod.md).

## The forest's costs (2026-10-21)

Test view: route TREES,
`core.startup.camera=96,-118,142.12,8.32,320.64,-626.51,7.15`, inside a
Forest of 2000 `norway-spruce-v2-edge.glb` (levels of 54,040, 22,308 and
1,580 triangles, then the impostor; coverages 0.444, 0.167 and 0.060), with
493 black locust statics in the next tile. Cases:
`tests/renderer/trees-forest-perf.json`. The user's profile (QRhi Vulkan,
shadow maps 2048 and 1024, environment map, sky enclosure, AO, Ultra clouds,
volumetric shafts) with the clock stopped, on the Steam Deck under Windows.

The harness's frame time is the CPU's recording and then the GPU's work
(`grabFramebuffer` waits for both), so GPU = frame - cpu. Turning off the
glTF mipmap loop (step 1) took 18.1 ms off both the frame and the cpu, with
the GPU time unchanged.

| GPU time | 1280x800 | 2123x1948 (the user's editor) |
|---|---|---|
| Whole frame | 104 ms | 197 ms |
| Without the forest | 21 ms | 44 ms |
| Forest: main view | 46 ms | 107 ms |
| Forest: shadow maps | 36 ms | 40 ms |
| Forest: environment-map face | 6 ms | 11 ms |
| Whole frame, every tree an impostor (LOD bias -100) | 26 ms | 60 ms |
| Whole frame, LOD bias -50 | 67 ms | 158 ms |

CPU: 24 ms a frame, 18 ms of it the glTF texture sync (step 1). Without it
the whole scene takes 5 ms, and choosing the levels of the 2000 trees 0.3 ms.

Triangles a frame, counted per render target (temporary counters in
`RhiRenderer::recordDraw`, by `currentTarget`, world items only):

| Target | Triangles | Instances |
|---|---|---|
| Main view | 1.43 M | 1,149 |
| Near shadow map | 2.50 M | 172 |
| Mid shadow map | 4.44 M | 1,661 |
| Far shadow map | 4.54 M | 2,847 |
| One environment-map face | up to 1.44 M | about 300 |
| Enclosure maps, redrawn once a second | 5.2 M more | |

Findings:

- The shadow maps are centred on the camera and take every caster within
  250, 600 and 1000 m all round it, behind the camera too, at the level the
  main view chose: 8 times the main view's triangles. The far map's texels
  are 1.4 m wide.
- About 26 ms of the main view (at 1280x800) does not grow with the
  resolution. Triangles of a pixel or less are shaded as 2x2 blocks with the
  full foliage shader. Level 1 is still drawn at a coverage of 0.167: a tree
  about 325 px tall on a 1948 px view, about one triangle per pixel of the
  tree. The fixture is deliberately heavy (the user), a test of the limits.
- The sky enclosure changes nothing in this view (images identical with it
  on and off) and costs the forest 4.8 ms at 1280x800 (no shadows, no
  environment map).
- Not the cost: draw calls (every pass together draws the 2,500 trees in 73
  instanced draws), mipmaps (`TSRE_GLTF_MIPMAPS=0`: the same GPU time),
  local lights (no change), visible overdraw (OpenGL occlusion queries: 1.5
  samples a pixel pass the depth test). Grouped instancing of forest trees
  is not needed for now.
- RenderDoc's per-draw GPU durations are unusable on this AMD driver
  (2-triangle draws report 94 ms). On QRhi, `RenderStats` counts indices as
  primitives (3 times the triangles).

Variants used: `--set core.rendering.shadows.enabled=false`,
`core.rendering.environmentMap.enabled=false`,
`core.rendering.skyEnclosure=false`, `core.rendering.lodBias=-100`, `-50`,
`100`; `TSRE_SHAPE_LEVEL=n`; `TSRE_GLTF_MIPMAPS=0`. The forest's cost is
the difference with the Forest skipped (a temporary switch in
`ForestObj::pushRenderItems`). Run as in
[`scripts/hardware`](../../../scripts/hardware/README.md), with
`QT_ENABLE_HIGHDPI_SCALING=0`, `TSRE_CLOUDS_STILL=1` and
`--set core.rendering.timeOfDay.running=false`; the user's size by
`"width": 2123, "height": 1948` in the cases.

## Part 1: the forest's own costs

### Step 1: glTF texture sync once a frame

`GltfShape::pushRenderItem` synced the shape's textures on every push. For
shapes with an impostor, the mipmap loop added on 2026-10-20 (`20f2d90`,
glTF levels of detail) walks the 14 texture slots of all 30 primitives (24
of them impostor frames): 420 hash lookups per tree and push, long after
every texture is uploaded. MSTS shapes sync on every push too, but with a
few lookups per image. Fix: once per shape and frame
(`Renderer::frameNumber()`).

Done (2026-10-21): `GltfShape` syncs and hashes its textures on the first
push of a frame, and again after a reload or invalidation. TREES forest
view at 1280x800: cpu 23.7 to 4.4-4.9 ms, frame 128 to 109 ms, GPU
unchanged (104 ms), the image identical. Suites `gltf-lod` (42, with
`--game-root C:/trainsim` for TREES_EXT) and `forest-objects` (7) pass.

### Step 2: vegetation, outside the sky enclosure

What counts as vegetation, per packet, so that a station with a hedge sends
only the hedge:

- glTF shapes with an impostor.
- MSTS primitives with light material -9 (cruciform) or -10. In the stock
  routes 63 shapes use -9 and 47 use -10, and every one is vegetation:
  trees, bushes, hedgerows, tree lines, clumps, billboard trees, the cherry
  blossom; ordinary objects use -5. Open Rails maps both to its vegetation
  shader, which receives no shadows (its trees never shadow themselves).
- Every Forest, of crossed quads or of shapes.

Vegetation materials skip the enclosure lookup (open sky) and are not drawn
into the enclosure maps (removes the 5.2 M triangle redraw once a second).

Done (2026-10-21): `RenderItem::Material::vegetation`, set by `GltfShape`
(an impostor), both MSTS backends (`SFileComplexGL`, `SFileLegacy`: light
material -9 or -10) and the Forest's crossed quads (`OglObj::setVegetation`).
A glTF shape without an impostor in a Forest of shapes is not vegetation:
the flag is the packet's, shared with the shape's statics. The QRhi
renderer passes it as `materialVegetation`; `skyEnclosed()`
(`ShadowSampling.glsl`) returns open sky for it, so `enclosureSun`,
`skyAmbient` and `lampScale` see no cover. `planShadowCasters` leaves it out
of the enclosure maps. OpenGL has no enclosure.

- TREES forest view, no shadows or environment map: GPU 62.6 to 58.1 ms.
  The enclosure redraws draw no world triangles there now (5.2 M before,
  11.5 M on the first frame with all four maps).
- EUROPE1 enclosure views (`tests/renderer/enclosure-views.json`: tunnel
  portal, stone bridge, night), with main's build as the reference:
  pixel-identical.
- Tests: `shape-complex-gl` (light materials -5, -9, -10 in both backends;
  131 on the real platform), `gltf-lod` (packets of shapes with an
  impostor).

### Step 3: vegetation casts a coarser level

A tree pushes its view level without shadow casting and a coarser level as
shadow-only packets. Packets stay shared per level, so the shadow maps draw
them instanced. View passes skip shadow-only packets.

The shadow's level (the user asked: not always the last; not a cruciform):
the level the view would choose 3 times farther away
(`ShapeLod::ShadowDistanceScale`), down to the shape's
`shadowFloorLevel()`: the coarsest level fit to cast. For glTF trees that is
the last mesh level before the impostor in both bindings: with Append the
last mesh (level 2's clusters), with ReplaceLast the one before the
cruciform the impostor stands in for (never drawn). Never the impostor,
which faces the camera; a tree at the impostor in the view casts it as
before (the frame facing the sun: later). Shapes without an impostor cast
their view's level. MSTS shapes have no marker for a billboard level (-9 is
a lighting model, used by 3D foliage too); stock trees are cruciform at
every level, so nothing to choose; a check of a few quads can come with
multi-level MSTS trees (tree-gen's planned `.s` export). Not done for the
LOD bias's ends, `TSRE_SHAPE_LEVEL` or picking.

Done (2026-10-21): `RenderQueue::setShadowOnly()`; the queue keeps such
packets in `shadowOnlyInstances`, read by `planShadowCasters` only (both
renderers). `ShapeLod::shadowLevel()`, set by `ShapeLod::apply()` through
`ComplexShape::setShadowDistanceLevel()`. `GltfShape::pushRenderItem` takes
the packets per level (`levelPackets()`).

- TREES forest view, 1280x800: shadow map triangles near 2.50 to 0.42 M,
  mid 4.44 to 1.53 M, far 4.54 to 1.63 M (11.5 to 3.6 M). With step 2:
  frame 109 to 81 ms, GPU 104 to 76 ms, cpu 5.0 ms. At the user's
  2123x1948: GPU 197 to 159 ms (frame 220 to 164 ms).
- The image: brightness unchanged (the trees +0.5, the ground -0.2 of 255);
  10% of pixels change by a little, the self-shadow speckle on the leaves,
  cast by the coarser levels.
- Tests: `gltf-lod` (50): the floor in both bindings and without levels,
  the level at 20 m (0 casts 1), 50 m (1 casts 2), the view's own at 5 m,
  at the last mesh level and at the impostor, none without an impostor;
  the push (view without casting, shadow-only set, the queue's state
  restored), none when picking or not casting.

## Part 2: cached and vegetation shadow maps

A dedicated renderer step after part 1.

### Map sets

- **Main, static and dynamic:** a static map per cascade, cached; each
  frame it is copied into the frame's map (or written by a full-screen
  depth pass, if QRhi refuses depth copies) and only moving things are drawn
  on top. Receivers keep one main lookup.
- **Vegetation:** its own maps, cached the same way; vegetation never moves
  (no wind yet). Sampled separately, because trees read it differently.
- Memory: the maps are 32-bit depth; today's set is 36 MB (2048, 2048,
  1024). The static cache adds 36 MB, the vegetation set 36 MB at the same
  sizes or 12 MB at 1024 (tree shadows are soft anyway).

### Snapped centres (step 4a)

Each map's centre lies on a grid of its own texels in the light's frame
(`ShadowSnapTexels`), the depth too; the shaders choose the map by the
point's place in it, unchanged. The map's texels keep their place on the
ground while the camera moves: with the centre moved 1.5 cm and the camera
still, 5.8% of the TREES view's pixels changed before (the crawl of shadow
edges), none now. A still view differs from before only by where the texel
grid falls. First done with a grid of 32 texels, which also decided the
redraws (2.5 m for the near map: 11 to 19 redraws a second at 100 km/h);
now one texel, the redraws by distance (below).

Each map's centre lies `ShadowLead` ahead of the camera in the view's
direction on the ground (16, 60 and 140 m: a tenth of its width). The half
of a map behind the camera mostly shades points out of view; shifted, a
cached map still reaches its half width ahead when it is drawn again.

### The static cache and its redraws (step 4b)

Each map has a cache of its static casters, drawn for the place and sun it
was drawn for; every frame the map starts as a copy of it (a full-screen
pass writing each texel's depth, as the water's depth copy) and the moving
casters are drawn on top. Receivers keep one lookup.

A cache is drawn again at once when what it holds changes, and in turn
(one a frame, the one drawn longest ago, so a fast camera still moves the
middle and far maps along) when it was drawn for another place, view, sun
or time:

- At once: the static objects' signature, textures uploaded
  (`Texture::uploads`), shapes loaded (`ShapeLoader::progress()`), the map's
  size, or the camera's tile (positions are the tile's, so a cached matrix
  is wrong in another). The signature (`RenderQueue::addShadowSignature`,
  from `Tile::pushRenderItems`) sums a hash of each loaded object that
  casts and is not being edited: its identity, tile, place, rotation and
  last change, cached on the object (`WorldObj::signatureHash`) and
  computed again only after an edit. It counts every loaded object, in
  range or not, drawn or not, so the camera moving or turning does not
  change it: counting only objects in range that drew (the first version)
  changed it nearly every frame on a moving camera, as objects crossed the
  3 km object range and the 150 m line behind the camera where `StaticObj`
  stops drawing. Levels of detail switching do not change it either
  (hashing the queued packets, before that, did).
- In turn: the map's centre would have moved more than its `ShadowLead`,
  the view turned 30 degrees (distant `StaticObj` culling themselves by the
  view come and go), the sun moved more than 0.1 degrees, or 4 seconds since
  its last draw (a safety net, as the enclosure maps' redraw once a
  second). At 100 km/h (28 m/s): the near map about 1.7 times a second, the
  middle one 0.5, the far one every 5 seconds.

The LOD of static objects is kept as it was at the redraw while the camera
moves; harmless. `TSRE_SHADOW_CACHE=0` turns the caches off (the maps drawn
as before), `TSRE_SHADOW_CACHE_LOG=1` logs each redraw and its reason.
OpenGL has no caches (`Renderer::beginShadowCache` false).

Done (2026-10-21, QRhi):

- TREES forest view at 1280x800: frame 82.5 to 70.4 ms (GPU 77 to 66 ms),
  the image identical with the caches on and off; the shadows now cost
  about 3.5 ms (13 to 15 before; no shadows at all: 63 ms). At the user's
  2123x1948: GPU 159 to 148 ms (197 at the start of the task).
- Moving 0.5 m a frame for 40 frames: mean frame 93 to 82 ms; the near
  cache drawn again about every 5 frames, the middle one twice. 9 frames
  differ from the caches off by more than 0.5% of pixels (5% at worst):
  while a cache waits its turn, its map lies up to a cell away, so trees at
  the near map's edge fall in the middle one (coarser texels); nothing is
  missing.
- Editing (`tests/renderer/trees-shadow-cache-edit.json`, run with the
  TREES camera as `core.startup.camera`): a gantry placed, moved 5 m and
  captured the next frame, then settled; each frame identical with the
  caches off, no shadow left behind. The harness's `"moveObjects": [x, y,
  z]` moves the placed objects before a view (`WorldObj::translate`, as
  the editor).
- EUROPE1 (`parity-views-shadows.json`, six views turned and from above):
  identical with the caches on and off. Before objects counted only when
  drawn, the view turned 90 degrees differed (0.5% of pixels): the caches
  lacked the distant objects that culled themselves when they were drawn.
- Redraws by distance (2026-10-21, the user: 2.5 m is very little):
  EUROPE1 from its start, 2 m a frame for 80 m: the near cache drawn again
  4 times for its place, the others only by the 4 second refresh, none for
  the objects; the enclosure maps by their own cells only. TREES forest
  view and the edited gantry still identical with the caches off.

The sky enclosure's maps (task 27) had the same problem: drawn again only
when the camera changed cell or every 60 frames, so a roof dragged in the
editor left its cover behind (the user, 2026-10-21). They now also redraw
when the queue's scene signature changes (`RenderQueue::addSceneSignature`:
every loaded object, being edited too, with shapes loaded): the near map
every frame while an object is dragged, the far map the frame after. The
camera moving or turning does not change it. `TSRE_SHADOW_CACHE_LOG=1`
logs these redraws too.

### What moves

Drawn on top every frame (`RenderQueue::setShadowDynamic`,
`WorldObj::castsMovingShadow`): objects with the Animate flag (MSTS and glTF
shapes animate only with it), car spawners, level crossings, signals (arms
in the simulation), overlay and frame items (trains, markers), and objects
being edited.

Editing (the user's question, 2026-10-21): an object is dynamic while it is
changed and for a second after its last change (`WorldObj::changedAt`,
stamped in `setModified`, which every edit calls), then settles into the
static map. Marking it until save would grow over a session: one forest
re-defined, 2000 trees cast every frame. An edit costs two redraws of the
caches: when the object turns dynamic (or its old shadow stays behind) and
when it settles. Deleting or adding an object changes the signature too.

### Vegetation's maps (step 5)

Done (2026-10-22, QRhi): three maps of vegetation's own
(`TARGET_VEGETATION_*`), the main maps' sizes and matrices, cached with
them (caches 3 to 5) and drawn every frame the main ones are. Vegetation
(`RenderItem::Material::vegetation`: glTF shapes with an impostor and their
shadow stand-ins, MSTS light materials -9 and -10, Forests) casts into them
only (`Renderer::SET_VEGETATION`), everything else into the main maps
(`SET_MAIN`). The OpenGL renderer keeps one set (`vegetationShadows()`
false).

- The lit shaders (`shadowedVisibility`, `ShadowSampling.glsl`) read both at
  the same coordinates: the main maps' 16 taps (2 far), vegetation's 4 (every
  fourth; 2 far). The shaded share combines as two occluders, `main + s veg
  - main s veg`, with `s` 1 for ordinary surfaces and 0.5 for vegetation
  (`vegetationSelfShadow`; 0.3 first, raised by the user, 2026-10-22: the
  shaded parts hard to see; `TSRE_VEGETATION_SELF_SHADOW` tries others). Without vegetation's shadow the result is the
  old one exactly (the switch off: pixel-identical).
- The fog volume (`RhiFog`, one tap per map) and the light shafts (the far
  map) take both at full strength, so trees still cut the shafts.
- `TSRE_VEGETATION_SHADOWS=0` casts vegetation into the main maps as before,
  its own maps cleared (the fog, shafts and shaders read them as lit;
  the dummy depth texture is not initialized, so it must not stand in).
- Cost, TREES forest view: 1.8 ms at 1280x800, 2.2 ms at 2123x1948 (8
  vegetation taps cost 6 and 14 ms; 4 differ from 8 at shadow edges on
  0.6% of pixels). Memory: 36 MB, and 36 MB of caches. Prudnik's station
  view (shapes task 11's camera, 2026-10-22): below the noise, GPU 59.6
  against 57.8 ms at 1280x800 and 117.9 against 117.2 ms at 2123x1948
  (means of two runs each; runs of one setting differ by 2-5 ms).
- Look: the spruces' crowns lit evenly, their own shade light (fixed
  exposure: the big tree 7 to 9 levels brighter, the trees behind 8 to 11);
  the ground under them unchanged. With the profile's auto exposure the
  brighter trees darken the rest of the view a little. EUROPE1's crossed-quad
  trees lose the hard dark wedge of their own quads; their shadows on the
  ground stay.

### Receivers

- Ordinary surfaces: main and vegetation.
- Vegetation: main, and the vegetation map at half its strength. Bark
  could take it at full strength (dappled trunks). Level-2 casters on a
  level-0 crown would draw the shadows of cards that are not there at full
  strength; half softens it. This fixes the crowns dark from below
  (task 36) and the stripes of MSTS crossed-quad trees.
- Later: the strength by the depth gap between the receiver and the
  vegetation caster: light within the tree's own crown, darker in a
  neighbour's shadow a few metres away. Needs a raw depth read (no
  comparison sampler).

### Choosing the map per object

The world file's shadow type overrides the detection (`StaticFlags` 0x2000
round, 0x4000 rect, 0x8000 treeline, 0x10000 dynamic; TSRE casts every
object whatever its type). Existing content gives trees ordinary types
(in USA1's text world files, 153 of 158 vegetation statics are Round), so
the type cannot detect vegetation, only override:

- Treeline Shadow: the vegetation map (the closest MSTS meaning; Open Rails
  still sees a shadow bit and casts).
- The main map, even with an impostor: a new value. Dynamic is already set
  on content with its MSTS meaning.

### Open

- The depth gap (light within a tree's own crown, darker in a neighbour's
  shadow); bark at full strength.
- The vegetation set at smaller sizes (1024) if memory matters.
- Impostor shadows from the frame facing the sun.
- A moving camera on heavy routes still gives bumpy frame times (the user,
  2026-10-22): another task.
- Wind: vegetation would move.
