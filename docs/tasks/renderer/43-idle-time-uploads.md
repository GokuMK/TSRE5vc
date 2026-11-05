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
