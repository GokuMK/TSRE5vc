# Task 40 - The Processor's Cost of a Frame

Status: part 1 (the queue's draw order and culling) on `feature/cpu-sort`
(2026-10-30); parts 2 to 4 open, in this order (the user, 2026-10-30).

Related: [task 25](25-qrhi-hardware-investigation.md) (the Deck, per-draw
recording), [task 37](37-vegetation-shadows.md) (the cached shadow maps),
[task 20](20-qrhi-renderer.md).

## Goal

Less of the processor's time a frame on large routes, for faster graphics
cards than the Deck's and for driving. The measured view: Prudnik's freight
station (the user,
`core.startup.camera=-5417,14865,-627.16,191.99,-241.71,-155.93,-20.54`),
12,000 draws a frame, about 45 ms of the Deck's processor a frame at
1280x720 (its graphics card about 110 ms there).

## Measuring

The `renderer-capture` harness at that view, still (for the images) and
moving, with temporary stage timers (a patch kept out of the tree: the
gather's parts, the queue's order, planning, recording and flushes,
logged each frame) and means over 279 frames. The means include frames
while shapes and textures still load: compare runs of one spec only.

The profile before (2026-10-29), ms a frame: gather 8.3 (the world 6.0),
the queue's sort 10.4 (59,000 instances queued, 39,000 of them culled
after it), planning 4.8, recording draws 14.1, flushes 8.4 (their command
loop 4.6). 8,114 draws bound 2,271 resource sets, 174 pipelines and 3,705
vertex inputs.

## Part 1: the queue's draw order and culling (done)

Before, each grouped list sorted itself in place three times (by packet,
by texture through the packet, then by both ranks), culled instances
included, and again for every view drawn from it (environment cube faces,
the reflection, the view). Each view then worked out every instance's
bounding sphere again (its matrix times the packet's bounds, a square root)
to cull it, and the QRhi renderer copied each instanced matrix into the
OpenGL renderer's instance buffer, which it never reads.

Now (`QueueRenderer`, both renderers):

- The lists stay in submission order. A pass's draw order, indices into
  its list, is made once while the list stays as it is (`groupedOrder`)
  and the views share it: each packet a slot by its first submission (a
  table by address, open addressing, kept from list to list), the textures
  by their first packet (a sort of the distinct packets only), the
  instances placed by the counts. The order is the one before: the images
  are equal pixel for pixel. Blended and transmissive lists: back to front
  by the distance at submission, as before.
- Each instance's bounding sphere is worked out at submission
  (`DrawInstance::sphere`); the views and the shadow maps' planning cull
  by it.
- Planning keeps the visible instances (`plannedInstances`) instead of a
  flag for each; the QRhi renderer copies no matrices
  (`instanceRowsCopied`).

Results (the freight station, two runs of each build, ms a frame):

| | before | after |
|---|---|---|
| order (sort) | 9.6-9.9 | 0.65-0.7 |
| planning (culling) | 4.5-4.6 | 2.3-2.5 |
| recording draws | 14.7-14.9 | 15.5-15.8 |
| the world's gather | 5.5-5.9 | 6.0-6.2 |
| the processor's frame | 44.6-46.5 | 36.1-38.7 |

Recording gains a millisecond: culling no longer reads the instances'
matrices, so they come into the cache when the draws copy them. The
gather's 0.3 ms are the spheres. Two runs of the new build counted one
flush more each frame, a third of the same code (with a trace of each
flush) not: the run's state (a cached shadow map redrawn while a caster
was still loading, most likely), not the change.

Tests: `shape-complex-gl` (grouped against submission order),
`environment-map-gl`, `water-gl`, `transfer-depth-gl`,
`terrain-material-gl`, `terrain-mesh-gl`, `token-shape-gl`,
`gltf-pbr-gl`, `rhi-shaders` pass.

## Part 2: a material table (next)

Recording draws (15 ms) and flushes (8 ms) are now the largest part. Each
draw sends its material's values in its own uniform block, and its
textures make its resource set. The idea: materials in an indexed buffer
uploaded when they change, each instance carrying its material's index
(as the alpha type once went into the vertex data instead of a uniform),
so a draw's uniforms and resource sets depend on fewer things and more
draws share them. QRhi has no bindless textures, so the textures stay in
resource sets. The user hopes for a real gain here, not another
millisecond: measure what the draws' uniforms and sets cost first.

## Part 3: the world's gather (a review)

6 ms a frame at the freight station. Whether static objects are cached as well as they could be (the
user: "We probably don't cache good enough static objects. Or there is an
issue hidden").

## Part 4: shared mesh buffers (later)

Small track shapes that share textures could share vertex buffers (3,705
vertex inputs bound for 8,114 draws). To review, study and rethink first.
