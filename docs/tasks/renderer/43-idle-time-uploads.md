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

## Design

0. **Measure first.** Frame log columns for the bytes uploaded a frame
   (textures, meshes) and the pending batch's recording time; a run into
   a new area on the Deck, with and without a 30 fps limit (a capture
   view's `runFor` with `"runPaints"` moving the camera, and a real
   session with `TSRE_FRAME_LOG`). Shows how much of a long frame is the
   copy against the creation.
1. **The budget counts the copy.** An item is made while the frame's
   creation time and its uploaded bytes both stay under the budget (bytes
   from the measured copy speed), the first item always: a large texture
   gets a frame to itself, small ones share one.
2. **Made after the frame is shown.** During the frame an item still to
   be made is queued (once) and waits as now. After the frame is presented
   the queue is made within the budget: textures and buffers created
   (`create()` needs no frame) and their uploads put into a batch kept for
   the next frame, separate from the pending batch other users fill
   (displays, water waves, terrain), merged at its start. New objects show
   a frame later. The copy itself stays at the next frame's start (QRhi
   records copies into a frame's command buffer).
3. **Idle time under a limit.** After step 2, while the next tick is more
   than a margin away, more of the queue is made and its copies recorded
   in an offscreen frame (`QRhi::beginOffscreenFrame`, `endOffscreenFrame`,
   which waits for the GPU, the frame just presented included), stopping
   before the tick. The wait's length decides the margin, measured on the
   Deck; a wait too long falls back to step 2. Without a limit nothing
   changes (no idle time).

Not in this task: the OpenGL renderer's path; terrain and overlays (never
budgeted); whole views (unlimited as now); compression (textures task 04).

## Questions and risks

- The offscreen frame's wait, the GPU's frame included: how long on the
  Deck, and whether it can overrun the tick.
- Released and evicted textures loading again (`TexLib::reloadReleased`,
  `reloadEvicted`), the selection pass, a 3D cab's parts made at once.
- The frame log's marks for the new phases (after the present, idle).

## Verification

- The Deck, a run into a new area at 30 fps: frame intervals and paint
  times (p99, longest) before and after; textures and meshes made a second
  while loading; the time until the view has everything.
- Without a limit: frame times unchanged within noise.
- `rhi-shaders`, `texture-residency`; renderer captures (EUROPE1, Prudnik)
  the same images once settled.
