# Task 40 - The Processor's Cost of a Frame

Status: part 1 (the queue's draw order and culling) on `feature/cpu-sort`,
part 2 (recording draws, a first round) on `feature/material-table`
(2026-10-30), part 3 (the world's gather reviewed, the arenas' uploads)
on `feature/world-gather` (2026-10-31); part 4 open (the user,
2026-10-30).

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
- The PBR material's values at members found once, not by name; the
  item's members found once a draw, not once for each of its 15 values
  (the material's values 2.8 to 1.5 ms).
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
| material values | 2.5 | 1.5 |
| QRhi's command loop | 4.8 | 4.8 |
| recording, all parts (counters) | 21.8 | 19.3 |
| the processor's frame (no timers) | 33.5 (6 runs) | 31.3 (8 runs) |
| the frame (median) | 104-106 | 100-102 |

The still view is equal pixel for pixel. Comparing keys costs about what
the copies did (the records).

Tried and taken out: each MSTS packet's textures kept for its next draws
(its base and night textures and its own slots, checked against the
packet's texture fields and each texture still its handle's, made only
when complete, all dropped when an existing texture changed). 9,000 of
12,000 draws a frame took an entry, none went stale, yet the texture key
went 3.5 to 3.2 ms only: looking the base texture up costs about 75 ns a
draw (TexLib's table stays in the cache), and finding and checking an
entry in a table of its own costs nearly as much. Not worth the risk of a
stale texture (the user, 2026-10-30: only if the benefit outweighs it).

Left, for more:

- Vertex pools by vertex size, slices at whole vertices: every mesh drawn
  from its buffer's start (3,950 of the 5,900 vertex inputs are one buffer
  at another offset, 1,800 another 8 MB chunk). With part 4.
- QRhi's own cost per call (a new resource set tracks all its 22 textures)
  leaves fewer draws (part 4: instancing across packets, which a material
  table on the graphics card would then serve) as the larger lever.

## Part 3: the world's gather (reviewed) and the arenas' uploads (done)

The question (the user): "We probably don't cache good enough static
objects. Or there is an issue hidden".

The gather by object type (cycle counters, ms a frame, 6.6 in all): walking
the 8,600 objects of 17 tiles 1.8 (two hash lookups each, the shadow
caches' signature, the distance), 2,632 track objects 2.0 (MSTS track
shapes; procedural track profiles are about 1 % of Prudnik's), 2,476
static objects 1.5, 250
signals 0.5 (2 us each), the rest 0.4. 5,600 objects are in range and
submit 59,000 packets, about 10 each. No single issue hides there: the time
goes in reaching each object's and each packet's memory. Small changes
(0.3 ms together): one pass over a tile's objects (transfers still first,
in index order), an object's packets share its captured matrix and its
scale, a shape's static packets found without QHash::operator[] (two insert
checks an object), the field of view's cosine once instead of for every
far object.

A sampling profile of the whole frame found the hidden cost elsewhere: the
arenas' uploads. QRhi's updateDynamicBuffer copied each range of the
uniform and instance arenas into an allocation of its own, kept it for
every frame in flight's copy of the buffer and copied it again into those
at their next use: the uploads, the allocations and the freeing of
multi-megabyte blocks took 3-4 ms a frame (they showed under the ambient
occlusion and the clouds, whose passes flush the arenas). On Vulkan the
arenas are now written into the frame's copy directly
(QRhiBuffer::beginFullDynamicBufferUpdateForCurrentFrame): all a frame
draws with is written within it, so the other copies need nothing. A
timer started for every draw to time mesh uploads (1 ms a frame:
QElapsedTimer goes through steady_clock and clock_gettime on MinGW) runs
only when an upload is due.

| (the freight station) | before | after |
|---|---|---|
| the processor's frame (no timers, 4 runs) | 30.7 | 26.0 |
| the frame (median) | 97-100 | 93-95 |
| the scene's paint (profile, 15 s) | 4.95 s | 4.34 s |

The still view is equal pixel for pixel.

The profile after it, within the scene's paint (26 ms): the main view's
passes 33 %, the world's gather 22 %, the water reflection 18 %, an
environment face 13 %, shadow maps 4 %, the terrain 7 %. By function
(own time): QRhi's command recording 9 %, planning (culling) 7 %, the
draws' recording 6 %, queueInstance 6 %, hasMesh 4 % (a packet's memory
first reached), memcmp 4 %. The gather and the queue are bound by memory
latency; the larger lever is fewer submissions (part 4: static objects'
packets kept from frame to frame, instancing across packets).

Profiling: a Release build with -g1 (`build-prof`, `-O3 -DNDEBUG -g1`),
run as `build\TSRE5vc-prof.exe` (a copy: the executable's folder must end
in `build` for appdata), and Very Sleepy attached to the capture
(`sleepy.exe /a:PID /mt /t:15 /o:file.sleepy /q`). The .sleepy file is a
zip with Symbols.txt and Callstacks.txt (time, then the addresses from leaf
to root); Qt's DLLs resolve to the nearest export only, so their names
mislead (QRhi's Vulkan internals showed as QEdidParser and
QRhiVulkanInitParams), while the path through them is right.

## The forest corridor (the graphics card's, measured 2026-10-31)

The user: a simple scene with a lower frame rate than the freight station
(`core.startup.camera=-5420,14869,916.94,163.98,-712.21,207.08,-5.92`,
`tests/renderer/forest-corridor-prudnik.json`): the track between trees and
bushes. At 1280x720 its frame is 73 ms with 11-13 ms of the processor's:
the graphics card's time. Measured by removal (a temporary switch leaving
a pass's draws out of every view, or out of the shadow maps; the settings
for the rest), the frame's median (ms; the user's profile):

| left out | frame | saved |
|---|---|---|
| nothing (twice) | 73.4 | |
| alpha-tested draws | 37.9 | 35.5 |
| alpha-tested shadow casters | 73.0 | 0.4 |
| the water reflection | 60.5 | 12.9 |
| the environment face | 66.2 | 7.2 |
| opaque draws and casters | 68.3 | 5.1 |
| distant terrain | 68.6 | 4.8 |
| terrain draws and casters | 68.8 | 4.6 |
| clouds | 69.6 | 3.8 |
| ambient occlusion | 71.5 | 1.9 |
| bloom | 71.5 | 1.9 |
| blended draws and casters | 72.4 | 1.0 |
| mist, shadows | 73.1-73.3 | 0.1-0.3 |

The alpha-tested pass, the foliage here, is half the frame: 35 ms in the
view, the water reflection and the environment face together. Its shadow
casters cost nothing (the cached maps, task 37); the reflection and the
face are dear mostly for drawing the foliage again. The cost grows with
the pixels, so at the user's 250 % screen it rules the frame: hence a
frame rate below the freight station's.

Drawing the alpha-tested pass front to back (a test): 73.6 to 70.0 ms, and
the processor's frame 3 ms more (instancing lost). The graphics card
rejects little of the leaves behind leaves: the shader's discard leaves
the depth test until after shading, so every layer runs the whole lit
shader (shadow maps, environment, light grid, fog).

The alpha-tested pass in two, on `feature/alpha-prepass` (2026-10-31): the
main program's whole-texel draws (MSTS shapes, not moving, not decals)
first with a pre-pass program, the main vertex shader as it is (the same
depth) and the fragment shader's alpha test only (StandardFog.fs,
TSRE_DEPTH_PREPASS), writing depth only; then their colour with the
depth test equal and no depth written, so the graphics card tests depth
before shading and runs the lit shader once a pixel. Other alpha-tested
draws (PBR, moving, two-part blended) draw once as before; the depth test
keeps both kinds in order. All lit views take it (the view, the water
reflection, environment faces); TSRE_ALPHA_PREPASS=0 turns it off.

| (frame median, ms) | without | with | the processor's frame |
|---|---|---|---|
| the forest corridor | 73.3 | 55.3 | +0.6 |
| the freight station | 90.4 | 84.0 | +0.5 to 1 |

Not for opaque surfaces (tried: the opaque pass's grouped draws in two as
well): the freight station 84.5 to 88.8 ms and its processor's frame 4 ms
more, the forest corridor unchanged. The graphics card already rejects
the opaque layers behind others, and the second geometry pass costs more.
The station's DB track shapes are opaque (leaving the alpha-tested pass
out changes 0.4 % of the near tracks' pixels), so the pre-pass leaves them
as they were; its gain there is the trees'. The opaque pass's 15 ms at the
station (leaving it out: 82.4 to 65.4 ms) is the shading of what is seen;
the ordered opaque draws (track profiles, procedural parts) are 1.8 ms of
it.

The images match but for single pixels where two surfaces meet at the
same depth (the colour pass now lets the last drawn win, the first did
before): 57 pixels in the forest corridor, 1,137 (0.12 %) at the freight
station, nearly all in distant foliage near the horizon. Suites pass with
it.

Still to do:

- distant terrain after the scene's opaque and alpha-tested passes:
  measured with a temporary reorder of the main view (sky, the scene's
  terrain to alpha-tested passes, the distant band, the scene's blended
  pass on; the depth slices keep the distant band behind), 2026-10-31:
  the forest corridor 55.7 to 54.9 ms, the freight station 83.5 to 82.4
  ms, within the runs' noise; images equal (9 pixels on the station's
  horizon). The 4.8 ms of leaving distant terrain out of every view is
  then mostly elsewhere: what the reflection and the environment face
  draw of it, or shading the depth test cannot skip (the terrain shader
  discards for its gaps). Not worth splitting the scene's passes for
  now. (Nothing blends before the blended pass: opaque MSTS materials
  write alpha 1, so the order changes no colour.)
- the reflection and the environment face: foliage drawn cheaper there.

## Part 4: fewer submissions (reviewed, for later)

Noted for later (the user, 2026-10-31: review and note now, implement and
test later). After parts 1 to 3 the frame's cost is many small ones for
each object, packet and draw, bound by reaching memory, and QRhi's own
cost for each call: the lever left is doing them fewer times.

### Static objects' packets kept between frames

Every frame each object in range picks its level of detail (ShapeLod),
finds its shape's packets and submits each one (queueInstance: its matrix
captured, its sphere, its pass, its flags): 59,000 submissions at the
freight station, the gather's 6 ms, then the queue's ordering and culling
of them. Kept between frames, a static object's submissions (packet,
matrix, sphere, flags) would stay in a list for each tile, made once and
changed only when something changes.

World matrices. Instances are in submission space: the camera's tile at
the origin, each tile translated by 2,048 m times its offset from it
(Route::pushRenderItems), then the object's own matrix. They hold while
the camera stays in its tile; when it crosses into another, every tile's
offset changes by the same 2,048 m step. So: keep each tile's instances
with the object's matrix alone (tile space) and add the tile's offset to
the translation (three additions an instance) when the camera changes
tile, rewriting the kept matrices then (rare, 59,000 instances), or as a
frame takes them. Precision stays as today (the same submission space). A
per-draw offset added on the graphics card would need instance data of
its own; not needed.

What changes a kept list: an object edited, added or removed (the tiles'
objects; WorldObj::changedAt and the shadow caches' signatures already
follow edits), its shape loading or its packets made again (SFileLegacy's
requiresUpdate, night textures taken or dropped at dusk), its level of
detail as the camera moves (checked again only after the camera moved a
step, with ShapeLod's distances), the far objects behind the camera that
StaticObj and TrackObj skip today (kept lists keep them; culling leaves
them out). Animated objects, trains and the selection pass stay submitted
every frame.

The queue clears its lists every frame; kept lists need a part of
QueueRenderer that lasts: instances with their spheres and matrices,
their draw order made when they change (orderByTexture), merged with the
frame's own submissions by order. Each frame then only culls and plans.
Expected: most of the gather (6 to about 1 ms) and of queueInstance and
ordering. To test: moving, deleting and adding objects, crossing tiles,
shapes loading, levels of detail while flying, night textures at dusk,
picking, the shadow caches.

### Instancing across packets

An instanced draw today is one packet (one mesh range, one material) at
many places. Different packets drawn with the same program and textures
(track pieces of different lengths, parts of buildings) are draws of their
own, though they need the same pipeline and resource set: 12,000 draws a
frame at the freight station, and QRhi's cost for each call (4.8 ms) and
the draws' recording follow that count. "Across packets" means one call
for several packets:

- their meshes in one buffer at whole vertices (the shared chunks, with
  vertex pools by vertex size: see part 2);
- what differs between them for each instance: the material's values (the
  material table on the graphics card, indexed per instance) and the
  shape part's matrix (folded into the instance matrix);
- each instance's own mesh range: QRhi (Qt 6.10) has no indirect or
  multi-draw calls (draw and drawIndexed only), so static batches: a
  cell's static packets of one material put together into one mesh, made
  again when one of them changes (more memory; with kept lists above).

In that order: vertex pools by vertex size, the material table, static
batches.
