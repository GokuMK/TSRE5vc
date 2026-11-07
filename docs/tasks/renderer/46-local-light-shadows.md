# Task 46 - Shadows from Local Lights

Status: planned 2026-11-07 with the user; step 0 (measure) first.

## Objective

Lamps' light stopped by what stands in it. First the trains: on Prudnik's
yard at night a train without its shadow under the lamps does not fit the
scene (the user;
`core.startup.camera=-5373,14872,-750.32,336.73,-601.77,-401.70,-12.51`).
Then the static world, which gives the biggest visual impact, and cab and
coach interiors.

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
