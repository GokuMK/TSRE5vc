# Task 40 - The Processor's Cost of a Frame

Status: part 1 (the queue's draw order and culling) on `feature/cpu-sort`,
part 2 (recording draws, a first round) on `feature/material-table`
(2026-10-30); parts 3 and 4 open, in this order (the user, 2026-10-30).

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
while shapes and textures still load: compare runs of one spec only. The
timers cost about 4 ms a frame themselves (a QueryPerformanceCounter call is
25 ns on the Deck, three a draw): compare timed runs with timed runs, and
the frame log's `cpu` of runs without them (it varies by 2 ms from run to
run: take several). The moving view's image varies from run to run by the
frame time (thin shadows of the catenary, 0.25 % of the pixels, in every
build); the still view's is equal from run to run.

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

Without timers, part 1's processor frame is 32.2-35.6 ms (6 runs).

## Part 2: recording draws (a first round, done)

The plan was a material table: materials' values in an indexed buffer, each
instance carrying its material's index. Measured first (cycle counters for
each part of a draw, 12,000 draws a frame), the uniforms were not the cost:
10,000 of the draws already share the block of the draw before (2,000
blocks of 1.4 KB, 2.7 MB a frame), so such a table would save 2-3 ms at
most. The parts, ms a frame with the counters: finding the program and the
mesh 1.5-2 (and the timers' own), the material's values 2.4, the texture
key 4.6 (its loop over the program's 22 slots 2.5), the arenas 2.2, the
draw's records 1.7, the flush resolving sets and pipelines 1.4, QRhi's
command loop 4.8 (12,000 draws, 6,300 vertex inputs, 3,100 resource sets,
200 pipelines).

Not done: each packet's textures kept from frame to frame. Looking a
texture up stamps its use (the residency evicts the least used) and
reloads a released one, and a released texture's pointer would dangle.

Done (QRhi renderer):

- A run of draws with one resource-set key keeps one copy of the key
  (670 bytes); the flush makes a set for each key.
- A program's slots: the frame's copied from the batch's first draw whole,
  only the item's own found for each draw.
- The setters record whether they changed a program's block; an unchanged
  block takes the program's block of this frame again, without comparing
  1.4 KB a draw.
- The PBR material's values at members found once, not by name.
- A mesh at a whole number of vertices into a shared buffer is drawn from
  the buffer's start with a base vertex, so draws of one buffer keep one
  vertex input. Few qualify: the slices start every 256 bytes, which VNT's
  32-byte vertices divide, but not VNTA's 36 or PBR's 76 (1,200 of 8,300
  draws; vertex inputs 6,330 to 5,910).

Results (the freight station, ms a frame):

| | part 1 | part 2 |
|---|---|---|
| texture key (its slot loop) | 4.6 (2.5) | 3.5 (1.4) |
| arenas | 2.2 | 1.3 |
| flush resolving | 1.4 | 0.8 |
| records | 1.75 | 1.75 |
| material values | 2.5 | 2.8 |
| QRhi's command loop | 4.8 | 4.8 |
| recording, all parts (counters) | 21.8 | 19.3 |
| the processor's frame (no timers) | 33.5 (6 runs) | 31.3 (8 runs) |
| the frame (median) | 104-106 | 100-102 |

The still view is equal pixel for pixel. Comparing keys costs about what
the copies did (the records); comparing values before writing them cost
the material 0.3 ms, now less with a fast path for 4-byte members.

Left, for more:

- Vertex pools by vertex size, slices at whole vertices: every mesh drawn
  from its buffer's start (3,950 of the 5,900 vertex inputs are one buffer
  at another offset, 1,800 another 8 MB chunk). With part 4.
- Each packet's resolved state (textures, material values) kept for one
  frame: the views draw a packet 2 to 3 times a frame. 2-3 ms; the state
  depends on the pass (blended texels, the scene copy, selection).
- QRhi's own cost per call (a new resource set tracks all its 22 textures)
  leaves fewer draws (part 4: instancing across packets, which a material
  table on the graphics card would then serve) as the larger lever.

## Part 3: the world's gather (a review)

6 ms a frame at the freight station. Whether static objects are cached as well as they could be (the
user: "We probably don't cache good enough static objects. Or there is an
issue hidden").

## Part 4: shared mesh buffers (later)

Small track shapes that share textures could share vertex buffers (3,705
vertex inputs bound for 8,114 draws). To review, study and rethink first.
