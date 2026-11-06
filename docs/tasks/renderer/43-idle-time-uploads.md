# Task 43 - Uploads off the frame and in idle time

## Objective

When a new area streams in, its textures and meshes are made inside the
frames that first draw them, so those frames run long and uneven; under a
frame-rate limit the main thread then sits idle until the next tick. Make
them after the frame is shown instead, within a budget that counts what
they really cost, and, under a limit, in the idle time before the next
tick, so that a limited session loads at full speed with even frames.

## Background (2026-11-05)

- A tester's frame log (build.11, 30 fps limit, 60 Hz): the trains were
  drawn exactly (task simulation 01); the limit's timer ran on Windows'
  15.6 ms ticks (frames 31.25 and 46.9 ms apart). The view's tick now runs
  at an exact cadence on a precise timer (`RouteEditorGLWidget::
  startViewTick`, `nextViewTick`, `nextViewTickNs`; branch
  `fix/fps-limit-timer`). While he drove through loaded scenery his paints
  took 11.3 ms (median), 15.6 ms (p99); 13 of 3,208 paints made textures or
  loaded tiles. New areas are the open case (user).
- Where uploads happen now (QRhi): lazily, as items are drawn.
  `RhiRenderer::packetTexture` makes a world item's texture
  (`Texture::GLTextures`, then `RhiTextures::create` for RGBA8 or
  `createCompressed` for BC1/BC2/BC3), `Meshes::prepareRhi` its buffers
  into the frame's batch. An item whose texture or mesh is not there yet
  waits (not drawn half made).
- The budget (`core.rendering.objectLoading.uploadBudgetMs`, 4 ms;
  `RhiRenderer::mayUpload`) counts creation only (`creation.textureNs`,
  `creation.meshNs`): the first item is always made, terrain and overlays
  never wait, a whole view (camera jump, first view) is unlimited. The
  data goes into `RhiTextures`' pending batch, recorded into the frame's
  command buffer (`RhiTextures::takeUpdates` in `RhiRenderer`): the
  staging copy and the generated mipmaps run in the frame, outside the
  budget.
- Sizes: DXT sources (ACE, DDS) are uploaded as BC1/BC2/BC3, uncompressed
  ones as RGBA8 (a 4096 x 4096 texture with mipmaps about 85 MB, as BC3
  21 MB). Prudnik's bench view (PSK content): RGBA8 460 of 905 textures,
  852 of 1,057 MB; CMK in a Czech 3D cab: 129 of 358, 152 of 348 MB.
  Compressing the uncompressed sources is `../textures/04-texture-memory.md`
  (deferred 2026-10-25).
- Reading, decoding and mipmap preparation run on worker threads (the
  shape loader's pool, `TexLib`'s loader pool, the vehicles read ahead),
  which do not wait for frames. With a limit the main thread idles from
  the end of a paint to the next tick (`nextViewTickNs`).

## Measured (2026-11-05, step 0)

A ride on the Deck (1280 x 800, clouds, mist, light shafts and ambient
occlusion off, shadows on): PSK_2026 to 12:02:40, train 154 (1051_KS_S1)
leaving Katowice at 12:03 ridden in its cab, 150 s with the view's own
timer and a 30 fps limit (`runLive`, `onScreen`; frame log columns
`texture_kb`, `mesh_kb`, `copy_ms`, `idle_ms`, `idle_made`).

- The limit was out of reach: paints 30.0 ms (median; gather 9.0, scene
  8.7, finish 4.7), the GPU 31.7 ms, frames 34.6 ms apart (28.1 fps); no
  idle time. A 20 fps limit keeps the scene and leaves about 20 ms.
- Uploads were small: 271 textures (170 MB) and 40 MB of meshes made in
  150 s, 448 ms of the 126 s of painting (0.4%): creation at most 4.7 ms
  (textures) and 4.3 ms (meshes) in a frame, the textures' batch at most
  6.8 ms more than its 0.62 ms of every frame (11 MB in one frame), 8.4 ms
  of uploads in the worst frame. The budget deferred 8 items.
- The long frames came from elsewhere: 12 of 4,213 paints over 45 ms,
  gathering 29 to 66 ms in the run's first seconds, a 58 ms finish, sky
  enclosure maps up to 14 ms and environment map faces up to 15 ms; 3 of
  the 12 adopted a shape.
- A tester's drive (build.11, an RTX machine, loaded scenery): 13 of
  3,208 paints made textures or loaded tiles.

So in these runs uploads made frames longer by a few milliseconds at most;
the design below would remove that, and a scheduler for idle time could
take other work that can wait too.

## Measured: from the scenery's edge through Zawiercie (2026-11-05)

The user: the Katowice ride started with the city loaded and brought no new
train or world tile; start at the edge of the scenery and ride through a
town. PSK_2026 to 10:52:00, train 154 entering at 10:51:30, ridden in its
cab past Myszkow Mrzyglod through Zawiercie, 600 s (the capture's limit of
a run) at a 20 fps limit (1280 x 800, clouds, mist, light shafts and
ambient occlusion off); the frames from 10:52:00 on (`ride-live.py`).

- Before: frames 50.01 ms (median), p99 55.5, p99.9 101, longest 417; 75
  over 60 ms, 16 of them with more than 5 ms of uploads (the longest,
  300 to 417 ms, outside the paint and without uploads: world tiles,
  likely). Paints 22 ms (p99 48), so most frames leave 12 to 28 ms. 1,236
  textures (1.7 GB) and 96 MB of meshes made in 10 minutes; the budget
  held back 933 items; uploads in a frame 2.9 ms (p90), 13.2 (p99), 30
  (p99.9), 88.7 at most. Uploads matter here (user).
- Idle uploads as designed below (steps 2 and 3 together: under the limit
  the budgeted world items made after the frame, their copies in an
  offscreen frame): worse. 798 frames over 60 ms (p99 91 ms). An idle
  phase took 45 ms (median) even for one item (648 of 837 made at most
  one), and 731 of the late frames followed one: `QRhi::endOffscreenFrame`
  waits for the GPU to finish all it was given, the frame just shown
  included, and the GPU took 38 ms a frame. On a GPU this busy the copy
  cannot run in idle time through QRhi's offscreen frames; on a GPU with
  slack (a fast desktop one) the wait would be short.

## Measured: with free time on the GPU too (2026-11-05)

The user: test where the frame rate holds and the GPU has free time, and
use idle time only while both the CPU and the GPU have it. Idle uploads now
get the time to the next tick less the last frame's GPU time and a margin
(`surfaceFrameShown`: the offscreen frame waits about that long), none
under 4 ms, and pause for 10 frames after an idle phase passes its tick;
`TSRE_IDLE_UPLOADS=0` turns them off. The same ride at 960 x 600 without
shadows (paints 12.4 ms, the GPU 18.6 ms: about 16 ms free on both in a
50 ms frame), off and on:

| | Off | On |
|---|---|---|
| Frames over 60 ms | 37 (9 with uploads) | 36 (8 with uploads) |
| Uploads in frames | 3,849 ms | 3,788 ms |
| Textures made in frames | 1,233 | 1,149 |
| Items made in idle time | 0 | 664 |
| Uploads in a frame, p99 / most | 9.5 / 65 ms | 10.0 / 69 ms |

Idle phases ran 22 ms (median), at most 30, and no frame after one came
late; but they came before 323 of 11,969 frames, and none of the 257
frames with more than 5 ms of uploads (1.2 GB) followed one. Uploads come
in bursts with the new area's heavy frames (paints to 36 ms and more),
which leave no free time, so those items are made in their frame as
before. And the simulation's trains are gathered with the overlays
(`pushRenderOverlays`, `CategoryOverlay`): their textures are never
budgeted, never queued, always made in the frame.

Next (to decide): world items' uploads waiting across frames for the next
idle time (made in a frame after waiting too long), trains under the
budget too, a large texture uploaded in parts over several frames or idle
phases; a frame log column telling budgeted from other uploads.

## Measured: where the time goes, and a submission that does not wait (2026-11-05)

The user: an idle phase of 22 ms for one item is suspicious; measure first,
textures only (meshes are made in the frame as before), compare the
captures with a real session, use CMK where the GPU has time.

- Captures and a real session: at CMK's station hall a real session's view
  was 842 x 755 (in the editor's window), 53.6 fps, paint 9.3 ms, GPU
  18.0 ms; the capture's live view at 1280 x 800 35.8 fps, paint 8.5,
  GPU 22.8: the pixels, not the capture. Captures now run at 842 x 755;
  `"screen": "secondary"` shows the window on the Deck's own display,
  which nothing covers (a covered window showed no change at a 20 fps
  limit, but the comparison mixed settings).
- Each texture (`TSRE_UPLOAD_TRACE`, CMK file0: the ED160 on AI drive,
  300 s at 20 fps): 481 of 488 prepared on the loaders (the render thread's
  part 0.01 to 0.06 ms); the time is the allocation (`QRhiTexture::create`):
  4096 x 4096 RGBA with mipmaps 31 to 40 ms, 2048 x 2048 10 to 17 ms, now
  and then 35 to 52 ms for a small one (the driver's new memory block);
  the copy (earlier) 39 to 54 ms for the 85 MB.
- The idle phase with an offscreen frame: 31.8 ms, of which the copy 0.0
  and the wait 31.7 (the GPU's frame 32.6 ms): `endOffscreenFrame` waits
  for all the GPU was given, the frame just shown included, made for
  readbacks. With nothing to copy it still cost a GPU frame.
- Now the copies go in a frame on the swap chain that presents nothing
  (`RhiRenderSurface::submitBetweenFrames`, `QRhi::SkipPresent`), which
  does not wait (the old way's switch removed 2026-11-06): the idle
  phase 2.1 ms (median; at most 8.6 and 19.6 in two runs), the wait
  0.3 ms, the copy 0.2; no validation messages; frames as without idle
  uploads (p99 51.8 ms, 10 and 12 over 60 ms, 13 without). In one run all
  34 textures that came during the ride were made in the idle time. The
  frame after an idle phase waits 5 ms (median, at most 14) in
  `beginFrame` for its frame slot (the GPU finishing the frame shown and
  the copies), within the frame's spare time here; so idle uploads need
  spare GPU time too: the window is the CPU's time to the next tick, at
  most the GPU's spare time in a frame.

## Trains and large textures in parts (2026-11-05)

- Trains' draws are their own category now (`RenderStats::CategoryTrains`:
  the simulation's trains and the loose and static consists, gathered with
  the overlays), budgeted as the world, so their textures wait for the
  budget or the idle time.
- A texture over 8 MB goes in parts (`Texture::beginRhiParts`,
  `uploadRhiParts`, `RhiTextures::createForParts`, `uploadPart`,
  `finishParts`): its GPU memory made in one idle phase (the CPU's time:
  the allocation cannot be split), its levels copied over the next ones,
  8 MB at most a part (row bands of level 0; block rows for blocks), its
  draws waiting until the last; in a frame without idle time its parts go
  on within the frame's budget. Idle uploads get two budgets: the CPU's
  time to the next tick (allocations) and the GPU's spare time in a frame
  (copies).
- The Zawiercie ride at 20 fps on the Deck's own display (842 x 755,
  without shadows, clouds, mist, light shafts and ambient occlusion: the
  GPU 20 ms a frame), off, whole textures in idle time, and parts twice:

  | | Off | Whole | Parts | Parts again |
  |---|---|---|---|---|
  | Uploads in frames | 3,444 ms | 2,175 ms | 1,915 ms | 2,068 ms |
  | Textures made in frames | 1,247 | 416 | 408 | 374 |
  | Uploads in a frame, p99.9 | 23.6 ms | 32.1 ms | 11.9 ms | 23.2 ms |
  | Frames over 60 ms | 59 | 86 | 55 | 90 |
  | of them with uploads over 5 ms | 16 | 13 | 9 | 16 |

  The same run twice differs by 35 late frames: most late frames (46 and
  74) have a paint of the usual length and no uploads, the time spent
  outside the paint (300 to 600 ms; world tiles, likely), so their count
  does not judge uploads. Uploads in frames went down by about 40% and two
  thirds of the textures were made in the idle time; idle phases 2.4 to
  2.8 ms (median). The final frames of the off and parts runs are the same
  image (at most 1 level of 255); no validation messages.
- Of the 29 textures over 8 MB, about 6 went in parts; the others (the
  EN62's cab, loaded with the first view; trains' 85 MB textures) were
  made whole in frames without idle time (the free-time rule off in a
  burst's heavy frames) or in whole views. Next: a large texture waits for
  idle time instead of a frame without it (made in a frame after waiting
  too long); then the stalls outside the paint.

## Large textures wait for the idle time; 20 and 30 fps (2026-11-05)

After main's merge (the texture budget by the GPU memory left), one helper
decides for both texture paths (`RhiRenderer::textureReady`): under a
frame-rate limit a texture over 8 MB waits for the idle time even in a
frame without it, at most 500 ms, then goes in parts from the frames'
budget (as without a limit). `TSRE_IDLE_UPLOADS=0`: as before this task.
The Zawiercie ride on the Deck's display (842 x 755, lighter effects, the
GPU 20 ms a frame), the old way against the new:

| | 20 fps old | 20 fps new | again | 30 fps old | 30 fps new |
|---|---|---|---|---|---|
| Uploads in frames | 3,219 ms | 1,779 ms | 1,594 ms | 3,242 ms | 2,925 ms |
| Textures made in frames | 1,246 | 356 | 346 | 1,245 | 1,027 |
| Over 8 MB made whole | 29 | 3 | 3 | 29 | 3 |
| Uploads in a frame, p99.9 | 34.7 ms | 11.5 ms | 13.1 ms | 16.2 ms | 12.1 ms |
| The most in a frame | 85.8 ms | 18.8 ms | 15.3 ms | 70.9 ms | 66.0 ms |
| Frames over 60 ms | 44 | 52 | 48 | 18 | 20 |
| A frame late (30 fps: over 40 ms) | | | | 228 | 262 |
| Idle phases | 0 | 501 | 505 | 0 | 144 |

- At 20 fps the large uploads are gone from the frames (the most 85.8 to
  15 to 19 ms), but frames were paced as well before: a frame has about
  30 ms to spare after an 18 ms paint, so even 30 ms of uploads fit.
- At 30 fps the Deck rarely has free time on both sides (paints 19 ms,
  30 at p90; the GPU 20 ms): 144 idle phases in 10 minutes; large
  textures wait 500 ms and then go in 8 MB parts in frames with about 14 ms
  to spare. No gain there (262 against 228 late frames, within the spread
  of identical runs).
- What is left in frames: the three large textures made whole (not in
  parts: cutouts with coverage mipmaps, or loads again after a release),
  85 MB in one frame (24 ms making, 43 ms copying); small textures whose
  allocation opened a new GPU memory block (16 to 24 ms for 0.2 to 1.3 MB);
  meshes; and the stalls outside the paint.
- The case this is for, a fast GPU with free time at 30 fps (the tester's
  RTX 3060: paints 11 ms), cannot be measured on the Deck.

## Parts by the time left; 320 x 240 at 30 fps; GPU memory (2026-11-05)

- Parts are no longer 8 MB at most: an idle phase copies as much as half
  the GPU's spare time takes, a frame what is left of its upload budget
  (`RhiRenderer::frameRoomBytes`), at the measured copy speed.
- The GPU with time to spare at 30 fps, as on a fast GPU: the Zawiercie
  ride at 320 x 240, objects to 1500 m (the GPU 10.7 ms a frame, paints
  17 to 18 ms), the old way against the new:

  | | Old | New |
  |---|---|---|
  | A frame late (over 40 ms) | 499 | 336 |
  | Over 50 ms / over 60 ms | 63 / 28 | 46 / 21 |
  | Intervals p99 / p99.9 / the most | 44.0 / 72.7 / 876 ms | 42.6 / 63.9 / 237 ms |
  | Uploads in frames | 3,165 ms | 2,742 ms |
  | Textures made in frames | 1,241 | 891 (358 in idle time) |
  | Uploads in a frame, p99.9 | 36.9 ms | 15.2 ms |

  Idle phases: 210, 1.3 ms median, 55 ms the most, waiting 0.5 ms at most.
- The most in a frame (83.8 ms) is an AI train's 4096 x 4096 texture
  (ET22-641_2.ace, 85 MB) loaded again after the texture memory budget
  released it: such reloads keep their handle and are made whole, not in
  parts (34 ms allocating, 50 ms copying); the old way made it whole twice.
- GPU memory (`TSRE_UPLOAD_TRACE`: "blocks +N", QRhi's memory blocks an
  allocation opened): every allocation over 1.3 ms opened one; those that
  did not took 0.1 ms (p99). On the Deck 40% of the textures open one
  (822 to 848 of about 2,050 a ride; 2 ms median, 3.7 ms p90, 11 to 12 ms
  p99; about 12 small textures over 10 ms a ride): its GPU memory is
  1 GB (973 MB allowed) while TSRE uses 2.1 to 2.4 GB, so QRhi's allocator
  (VMA) cannot open its 256 MB blocks over the budget and gives a texture
  its own memory whenever no gap fits (64 x 64 never, 128 x 128 30%,
  512 x 512 50%, 1024 x 1024 67%, 2048 x 2048 90%, 4096 x 4096 always;
  QRhi's blocks: 17 at the start, 325 after 1 minute, 679 after 8). With
  free GPU memory (a discrete GPU) a 256 MB block opens once in about
  256 MB of textures. Opening blocks ahead in the idle time cannot help on
  the Deck (none open over the budget) and QRhi has no call for it: not
  done.
- In the first seconds (the start, memory short: 14.1 of 15.2 GB in use,
  550 MB of TSRE's memory paged out) one frame spent 940 ms and two
  140 ms in parts with 11 MB copied, while five large cab and train
  textures (361 MB) waited in parts; likely their waiting levels were
  paged out (not measured: the user, the build runs bloat this machine).
  The old way made them whole at once and stalled 1.66 s outside the
  paint there. The ride loads below make them whole in the first frame.

## Reloads in parts (2026-11-05)

The most in a frame at 320 x 240 (83.8 ms, above) was a texture loaded
again after the texture memory budget released it: an AI freight's
locomotive texture (ET22-641_2.ace, 4096 x 4096, 85 MB), made in parts when
first seen (22:26:32), released by the budget after its train had gone and
made again at 22:29:35. Such a texture comes back under its GPU handle (code
holding the handle, not the library id, draws it again), which parts did
not do: it was made whole. Now its parts' texture takes that handle back
when the last part is in (`RhiTextures::moveToEvicted`), its sampling kept,
its mipmaps too (as `Texture::uploadForRhi`). Suite texture-readback: a
texture in parts makes its pixels; one loaded again comes back in parts
under its handle.

## What the camera starts riding loads at once (2026-11-05)

The user: a train's start should be handled as a jump is, its loads forced
before the frame is drawn; frames with a half-drawn cab are of no use, the
player cannot use it. So when the camera starts riding a train (the start,
another train, the cab view or an outside one), what it rides is loaded
before that frame (`RouteEditorGLWidget::loadWholeRide`): the whole train,
with its 3D cab where it rides one. It is gathered alone until its shapes
have loaded
(`Consist::shapesLoaded`, without the loading limits) and the texture
files its draws use (`Renderer::queuedTextures`) have: one still queued on
the loaders loads at once on the render thread (`TexLib::loadQueuedNow`;
the queued one then does nothing), one loading there is waited for. The
frame is a whole view's: no upload budget, no idle waiting, parts begun
finished at once. At most 10 s (the jump's wait is 2 s); a vehicle or a
shape that failed is not waited for.

- First only the cab was loaded from the cab, the rest of the train
  streaming in after. That rest stalled the first seconds: train 154's
  three 4096 x 2048 cars' textures (42.7 MB) and its corridor's
  4096 x 4096 (85 MB) need 14 to 38 ms each to allocate, which cannot be
  split; two were made in frames (43 and 54 ms of uploads in one), one in
  an idle phase. The user: half a second or a second more for a clean
  start is reasonable; the train is loaded whole from its cab too. Again:
  "Ride (cab) loaded in 779 ms, 51 textures", none of the train's made
  after (23, 234 MB, before); the loading frame as long (3.3 s, 971 MB).
- The Zawiercie start (train 154 from its cab, 320 x 240): "Ride (cab)
  loaded in 871 ms, 26 textures", the cab's large textures (five
  4096 x 4096, 85 MB each, four 2048 x 2048) made whole in the frame after
  the jump, where before they went in parts over the next 2 s (with the
  940 ms frame above). That frame: 3.3 s, 244 textures, 737 MB (the
  jump's world and the cab). From behind the train: "Ride (train) loaded in
  291 ms, 30 textures", its four cars' textures in that frame.

## Without a frame-rate limit (2026-11-06)

Without a limit nothing waits for idle time, but trains' textures are
budgeted, large textures go in parts from the frames' budget, and ride
loads run. `TSRE_IDLE_UPLOADS=0` now turns all of that off (as main). The
Zawiercie ride on the Deck's display (842 x 755, lighter effects, about
50 fps), from 20 s into the ride (after the start's loading frames):

| | Old | New |
|---|---|---|
| Frames per second | 49.7 | 51.0 |
| Intervals p99 / p99.9 | 47.2 / 72.7 ms | 38.5 / 50.0 ms |
| The longest interval | 1,527 ms | 241 ms |
| Over 50 / 60 / 100 ms | 229 / 79 / 19 | 30 / 19 / 6 |
| Uploads in frames | 2,715 ms (1,233 textures, 1.7 GB) | 2,499 ms (1,238, 1.7 GB) |
| Uploads in a frame p99.9 / the most | 31.8 / 79.1 ms | 15.2 / 32.5 ms |

Not worse; the large uploads are split. Of the old run's 79 frames over
60 ms only 3 had uploads, so most of that difference is the spread
between runs (the time outside the paint).

## Design (as built, 2026-11-06)

Textures only: meshes are made in the frame as before (the user: the large
textures are what costs).

- **One decision for both texture paths** (`RhiRenderer::textureReady`,
  from `packetTexture` and `libraryTexture`). A world item's or a train's
  texture (`uploadBudgeted`: `CategoryWorld`, `CategoryTrains`, not
  terrain) not yet on the GPU:
  - while the frames leave idle time (`setIdleUploads`, on): queued for
    the idle time (`queueIdleTexture`), its item waiting;
  - under a frame-rate limit in a frame without idle time (`possible`): a
    large one (over `IdlePartBytes`, 8 MB, and `canUploadRhiParts`) still
    waits, at most `MaxIdleWaitMs` (500 ms);
  - otherwise within the frame's budget (`mayUpload`), as before: a large
    one begun in parts, each frame going on with what is left of its
    budget (`frameRoomBytes`, at the measured copy speed); others whole;
  - in a whole view (first view, camera jump, ride load:
    `uploadsUnlimited`) everything at once, parts begun finished.
- **Idle time** (`RouteEditorGLWidget::surfaceFrameShown`, after the frame
  is presented), only with a frame-rate limit (`viewTickPeriodNs`): the
  smaller of the CPU's time to the next tick and the GPU's spare time in a
  frame (the period less the last frame's GPU time), each less a 2 ms
  margin; none under 4 ms (`MinIdleNs`), and a 10 frames' pause after an
  idle phase passing its tick. `RhiRenderer::idleUploads` goes on with the
  parts begun (their copies within half the GPU's spare time), then the
  queue: a large texture's allocation within half the CPU's time
  (estimated at 0.4 ns a byte; the first always) and its first part,
  others whole while their copies fit. The copies are submitted in a frame
  on the swap chain that presents nothing (`RhiRenderSurface::
  submitBetweenFrames`, `QRhi::SkipPresent`), which does not wait for the
  GPU; an offscreen frame waited for the frame just shown (32 ms a phase
  on the Deck; removed 2026-11-06).
- **Parts** (`Texture::beginRhiParts`, `uploadRhiParts`;
  `RhiTextures::createForParts`, `uploadPart`, `finishParts`,
  `moveToEvicted`): the GPU memory at once (an allocation cannot be
  split), then row bands of each level (block rows for blocks); RGBA
  without the file's mipmaps gets the GPU's with the last part; the draws
  wait for the last. A texture loaded again after a release takes its old
  handle back. Not in parts: cutouts with coverage mipmaps (made from
  level 0 at the upload).
- **Ride loads** (`RouteEditorGLWidget::loadWholeRide`): the train the
  camera starts riding, with its 3D cab, loaded before that frame (above).
- **Switches and tools.** `TSRE_IDLE_UPLOADS=0`: as before this task (no
  idle uploads, parts, trains' budget or ride loads), for comparisons.
  `TSRE_UPLOAD_TRACE`: a line a texture made (its way, the render
  thread's preparing and allocating, the GPU memory blocks opened) and a
  line a ride load. Frame log columns `texture_kb`, `mesh_kb`, `copy_ms`,
  `idle_ms`, `idle_made`, `idle_copy_ms`, `idle_wait_ms`, `view_w`,
  `view_h`. Capture views: `runLive` (the view's own timer), `onScreen`,
  `screen` (`"secondary"`: the Deck's own display).

Not done from the first plan (2026-11-05): the budget counting the copy
(parts use the measured copy speed instead), and items made after the
frame without idle time (they are made in the frame within its budget,
as before).

## Status (2026-11-06)

Done as above, on the Deck:

- 20 fps (842 x 755, the GPU 20 ms a frame): uploads in frames 3.2 s to
  1.6 to 1.8 s in 10 minutes, the most in a frame 86 to 15 to 19 ms;
  pacing as before (frames had the time).
- 30 fps with GPU time to spare (320 x 240, the GPU 10.7 ms, as a fast
  GPU): frames late 499 to 336, intervals p99.9 72.7 to 63.9 ms, uploads in
  a frame p99.9 36.9 to 15.2 ms. At 842 x 755 the Deck has no time to spare
  at 30 fps: no gain.
- The start: the ridden train and its cab whole in the first frame shown.

Left:

- A fast GPU at 30 fps (a tester's machine): the case this is for.
- Small textures whose allocation opens GPU memory (2 ms median, up to
  about 40 ms): on the Deck its 1 GB is over its budget, so QRhi's
  allocator gives them memory of their own; QRhi has no call to open
  memory ahead.
- Stalls outside the paint: tile crossings (`../world/tile-streaming.md`),
  and a cluster approaching Zawiercie, not yet looked into.
- Meshes, if they ever show up (they were not large here).

## Questions and risks

- The offscreen frame's wait: 32 ms a phase on the Deck, the GPU's frame
  included; replaced by the frame that presents nothing (above).
- Textures loaded again after a release: in parts under their handle; a
  3D cab: loaded whole with its train (ride loads).
- Without a limit nothing waits for idle time, but trains' textures are
  budgeted, large textures go in parts and ride loads run: see
  Verification.
- On the Deck paging (TSRE's working set far under its private memory)
  slowed some part copies and allocations in the first seconds; the user:
  the build runs bloat this machine, so not what players see.

## Verification

- Suites (2026-11-06): on the GPU rhi-shaders (166), texture-readback
  (30, parts and a reload in parts), displays (37), mesh-store (24),
  consist-preview-gl (15), environment-map-gl (19), water-gl (31),
  gltf-pbr-gl (19); offscreen simulation-ui (80), timetable-ui (50),
  activity-ui (41), editor-tools (38), map-view (48), cab3d (28),
  texture-residency (41), driving (68), simulation-core (89),
  simulation-host (20). All pass.
- The rides above (20 fps, 30 fps at 842 x 755 and 320 x 240, without a
  limit), and the ride starts from the cab and from behind the train.
- Renderer captures (`parity-views.json`, `TSRE_IDLE_UPLOADS=0` against
  the new way), EUROPE1 and Prudnik, 6 views each: the same images (RMSE
  at most 0.05, no pixel over the tolerance), the same draws, no pick
  mismatches (no view settled by the harness's rule in either run).
