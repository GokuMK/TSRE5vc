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
  light is about a tenth of what falls below it. Wide or important lamps
  get two maps tilted apart (about 100 degrees each, along the arm or the
  track). Not a paraboloid map: it bends straight edges, and MSTS
  casters' long, coarse triangles would warp their shadows near the lamp.
- **An atlas with a budget:** one depth texture, a tile a lamp (its place
  and size in the light's two free values); 512 x 512 near, 128 to 256 for
  farther lamps. Every lamp shadowed would be about 500 MB and many
  samples a pixel, so the lamps near the camera, by distance and
  brightness.
- **Drawn once, kept while it fits:** a lamp's static map is drawn the
  first time it needs one, in idle time (task 43), and kept until the
  atlas runs short; then the least recently used far lamps go. A map is
  drawn again only after it went or when its static casters change (a
  signature a lamp, of the objects within its reach, as task 37's); a lamp
  without its map yet lights unshadowed and fades in. Later perhaps a disk
  cache keyed by the object and its content: drawn once until the route
  changes.
- Moving trains keep A's boxes; perhaps later drawn exactly into the two
  to four most important lamps' maps each frame.
- The maps in the lamp's own frame (its tile and place), not the camera
  tile's submission space, so they stay valid as the camera's tile
  changes.

### C. Cabs and interiors

A cab's or coach's lamps and its own geometry move together: their shadows
are fixed in the car's space, drawn once a cab and kept. Screen-space
contact shadows for the small details near the camera (the user: useful in
cabs and coaches).

## Steps

0. **Measure** at the user's camera at 0:30: the lamps reaching the train
   (a car), the train's draws and triangles, lamps a pixel's column, the
   frame time; the static casters within a lamp's reach (B's cost a lamp).
1. A: trains as boxes.
2. B: the atlas, its cache and budget, one-map lamps.
3. B: two-map lamps, smaller maps for far lamps.
4. A: designer-marked world boxes.
5. C: cabs and interiors.

Later: the disk cache, exact train maps for the top lamps, the fog's lamps
shadowed (halos), contact shadows.

## Questions

- How soft the box shadows are; whether the fog's halos take them.
- The atlas's size on the Deck (4096 x 4096 at 16 bits is 32 MB: 64 tiles
  of 512).
- Which lamps enter B's budget: distance, brightness, size on screen.
