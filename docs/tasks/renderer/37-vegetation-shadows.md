# Task 37 - Vegetation shadows and cached shadow maps

Status: the TREES forest measured (2026-10-21). Part 1, the forest's own
costs (steps 1-3), in progress on `feature/forest-performance`. Part 2,
cached and vegetation shadow maps (steps 4-5), designed, not started.
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

The queue carries it per instance. Vegetation materials skip the enclosure
lookup (open sky) and are not drawn into the enclosure maps (removes the
5.2 M triangle redraw once a second).

### Step 3: vegetation casts its lowest mesh level

A tree pushes its view level without shadow casting and its lowest mesh
level as shadow-only packets; a tree already at that level, or at the
impostor, pushes nothing more. Packets stay shared per level, so the shadow
maps draw them instanced. View passes skip shadow-only packets.

Estimate: with every tree at level 2 the shadow maps cost 15 ms instead of
36 (1280x800). Impostors as casters need the frame facing the sun, not the
camera; later.

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

### Snapping and redraws

The cached maps are drawn from a centre snapped to a cell (as the enclosure
maps), with the map grown by a cell to cover the camera anywhere in it. This
also stops shadow edges crawling as the camera moves (today the maps are
centred exactly on the camera each frame). A cached map is drawn again when:

- the camera leaves its cell (the near map often, the far map rarely);
- the sun moves more than about a texel;
- the world is edited; shapes or cut-out textures finish loading; the
  season or the LOD bias changes.

The near, mid and far redraws go in different frames. The LOD of static
objects is kept as it was at the redraw while the camera moves inside a
cell; harmless.

### What moves

Trains, animated objects (the Animate flag, animated shape states) and
objects dragged in the editor. Anything that moves but is classed as static
leaves its shadow behind.

### Receivers

- Ordinary surfaces: main and vegetation.
- Vegetation: main, and the vegetation map at about 30%. Bark could take it
  at full strength (dappled trunks). Level-2 casters on a level-0 crown
  would draw the shadows of cards that are not there at full strength; 30%
  hides it. This fixes the crowns dark from below (task 36) and the stripes
  of MSTS crossed-quad trees.
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

- The vegetation set's cascades and sizes (near and mid at 1024? far trees
  as impostors in the main far map?).
- Impostor shadows from the frame facing the sun.
- Wind: vegetation would move.
