# Task 25 - QRhi Renderer on Real Hardware (Steam Deck)

A brief for an agent working on the Steam Deck, where the problems show.
The QRhi renderer (task 20, branch `feature/qrhi`) was developed on a
headless machine with software rendering only (Mesa llvmpipe for OpenGL,
lavapipe for Vulkan). Every problem below is invisible there, so it has
to be found on the hardware.

## Ground rules

- Work on `feature/qrhi` (or a branch from it). Never merge into `main`.
  Commit in small steps; the user pushes.
- The OpenGL renderer (the default backend) must stay pixel-identical:
  shaders in `appdata/0.7/shaders330` serve both renderers, and QRhi-only
  code sits under `#ifdef TSRE_RHI`.
- Build: `cmake --build build -j8`. The full `all` target also builds the
  standalone tools in `tests/`.
- Some files have CRLF line endings (for example `Renderer.h` and
  `RenderItem.h`). Keep each file's endings.
- Write findings with numbers, and the commands that produced them, into
  the "Findings" section at the end of this file.

## Hardware and symptoms (user's report, 2026-10-06)

Steam Deck: AMD Van Gogh APU (RDNA2), Mesa RADV (Vulkan) and radeonsi
(OpenGL). Memory is shared between the CPU and GPU.

1. Paged terrain is not drawn by QRhi, on either API as far as we know.
   `core.rendering.terrainMesh=legacy` terrain is drawn. Both draw on
   lavapipe and llvmpipe.
2. Frame rate and load. "Native" is the OpenGL renderer (`backend=opengl`):

   | Backend | FPS vs native | CPU (render thread) | GPU |
   |---|---|---|---|
   | Native OpenGL | 100 % | one core saturated | 78 % |
   | QRhi Vulkan | about 90 % | about half a core | 50 % |
   | QRhi OpenGL | far lower | about 65 % of a core | 40 % |

   QRhi is limited by neither the CPU nor the GPU, so something makes them
   wait for each other or for something else.
3. Memory: 1100 MB for native OpenGL, 1800 MB for QRhi (process, with
   legacy terrain).
4. Earlier fixes already on the branch (see the "Hardware test" section of
   `20-qrhi-renderer.md`):
   - vsync off;
   - terrain patch records moved to a texture;
   - redundant draw state skipped;
   - asynchronous 3D pointer depth;
   - compressed DXT uploads;
   - pipeline cache on disk;
   - GPU time shown in the FPS display.

## Running

- Switches:
  - Renderer and API: `--set=core.rendering.backend=qrhi --set=core.rendering.rhiApi=vulkan|opengl`
  - Terrain mesh mode: `--set=core.rendering.terrainMesh=paged|legacy`
- Environment variables:
  - `TSRE_RHI_DEBUG=1`: Vulkan validation layer (`VK_LAYER_KHRONOS_validation`) and debug markers.
  - `TSRE_RHI_TRACE=1`: per frame, logs draw counts by program kind, mesh format and pass (`rhi-trace draw kind1 format1 ...` is paged terrain), state changes, pipelines, and uniform and instance bytes.
  - `TSRE_RHI_API=null`: runs the renderer without a GPU driver, to measure its own CPU cost.
- Repeatable views (no mouse needed):
  `TSRE5vc --game-root <root> --route <route> --test --test-suite=renderer-capture --test-cases tests/renderer/perf-views.json --test-label <label> [--set ...]`
  - `perf-views.json` has `timingFrames`, and `capture.json` holds the frame CPU time per view.
  - `parity-views.json` is the shorter set.
  - Use `--test-suite=renderer-compare` to compare two labels.
- The FPS display (editor settings) shows "GPU x ms" under QRhi. A GPU time
  close to the frame time means the GPU limits the frame rate.
- Useful tools on the Deck:
  - MangoHud: frame time, CPU and GPU load.
  - `perf record -g` and `perf report`.
  - `strace -f -c`.
  - RenderDoc: works with both Vulkan and OpenGL.
  - `heaptrack`.
  - `amdgpu_top`, or `/sys/class/drm/card*/device/mem_info_{vram,gtt}_used`, for GPU memory.

## 1. Paged terrain not drawn

Find out where the draws are lost.

1. Run with `TSRE_RHI_TRACE=1`. Look for `draw kind1 format1 pass2` lines:
   - No such lines: the draws are dropped before recording. Look at `RhiRenderer::recordDraw`, `Meshes::prepareRhi` (it returns false, or the format is `MeshData::Buffer`), and `hasMesh`.
   - Lines present: the GPU receives them; go to the next step.
2. Run with `TSRE_RHI_DEBUG=1` on Vulkan and read the validation errors.
3. Capture a frame with RenderDoc and pick a terrain draw:
   - Mesh viewer: vertex shader input and output. Are the positions sensible, or NaN, zero, or off-screen?
   - The `terrainPatchData` texture at binding 19: 512 x 1 RGBA32F, two texels per patch. The texels hold the patch origin in `.w` and the UV mapping in `.xyz`.
   - The vertex buffer: per vertex, a float height, then 4 bytes of normal and gap. QRhi converts the packed 2_10_10_10 normal to `UNormByte4` in `convertTerrainNormals` (`src/tsre/renderer/Mesh.cpp`).
   - The index buffer: the 16-bit shared index templates, read with `firstIndex`.
   - `baseVertex`: slot times vertices per patch.
   - The viewport depth range: the scene band is [0, 0.98].
4. Code to read:
   - Producer: `src/tsre/world/TerrainMeshBackend.cpp`, `TerrainMeshPaged::configureRenderItem` and the page upload.
   - Shaders: `appdata/0.7/shaders330/TerrainPatch.glsl`, and the paged branch of `StandardFog.vs` (`gl_VertexID / terrainVerticesPerPatch`).
   - Renderer: binding 19 in `RhiRenderer::recordDraw`.
   - Mesh store: `Meshes::dataTextureRhi` and `uploadDataTexels` in `Mesh.cpp`.
   - The vertex input layout for `MeshData::TerrainHeightNormal` in `RhiRenderer::pipeline`: stride 8, location 0 `Float`, location 2 `UNormByte4` at offset 4.
5. Software rasterizers tolerate some things GPUs do not: reads out of buffer bounds, uninitialised memory, missing barriers. Check that every buffer range a draw touches was uploaded before the draw.

## 2. Frame pacing and stalls

Lead to test first: QRhi frames are not drawn by the editor's timer.
`RouteEditorGLWidget::update()` calls `RhiRenderSurface::requestUpdate()`,
which calls `QWindow::requestUpdate()`. The frame is drawn when Qt delivers
`QEvent::UpdateRequest` (`RhiRenderSurface.cpp`, the `RhiWindow` event
handler, which calls `surface->render()`). On X11 with a compositor that
syncs frames (KWin, `_NET_WM_FRAME_DRAWN`) and on Wayland (frame
callbacks), the compositor paces that delivery, whatever the swap interval.
The OpenGL renderer's `QOpenGLWidget` does not wait this way.

- Experiment: draw directly from the timer (call `surface->render()`
  instead of `window->requestUpdate()`, or send the `UpdateRequest`
  synchronously). Compare FPS, CPU and GPU load against the current build.
- Note the session: desktop mode (X11 or Wayland) or game mode (gamescope).
  Try `QT_QPA_PLATFORM=xcb` against `wayland`.

If pacing is not the cause, find where the render thread waits:

- `perf record -g`, and `strace -f -c`.
- Waits in `vkAcquireNextImageKHR` or `vkQueuePresentKHR` point to presentation.
- Waits in `vkWaitForFences` / `QRhi::beginFrame` point to the GPU or frames in flight.
- Waits in `glXSwapBuffers` or `glFinish` point to OpenGL synchronisation.
- Remaining synchronous waits in our code, which should not run every frame:
  - `RhiRenderer::readNow` (`QRhi::finish`), used by `readDepth` (now only for tests), `readColor` and selection.
  - Check that nothing calls these each frame (for example water or the environment map).
- The overlay: the FPS display and other QPainter output are painted into a window-sized image (`RhiRenderSurface::overlayPaintDevice`). Check whether it is uploaded every frame, and measure with the FPS display off.

QRhi OpenGL is a separate, larger problem:

- QRhi's OpenGL backend has no uniform buffers. On every `setShaderResources` it sets each uniform member with glUniform (it caches values of up to 4 components; matrices are always sent) and binds every texture.
- The instance data is a QRhi `Dynamic` vertex buffer, updated with `glBufferSubData` several times a frame. radeonsi may stall on a busy buffer.
- Profile it with `perf` (symbols for `libQt6Gui`: install Qt debug symbols, or use `debuginfod`) and report the top costs.

## 3. Memory

1800 MB for QRhi against 1100 MB native, with legacy terrain.

- First split the difference into CPU heap (`heaptrack`, `smaps_rollup`) and driver/GPU memory (GTT and VRAM counters). On the APU, both come out of system RAM.
- Suspects:
  - Textures that are still decoded to RGBA8 with full mip chains: DXT1 with alpha (QRhi has no BC1 format with alpha), uncompressed ACE files, chains that stop early (`Texture::uploadForRhi`, `RhiTextures::create`).
  - Upload data held in `QRhiResourceUpdateBatch` objects until submitted.
  - Two copies (frames in flight) of the `Dynamic` uniform and instance arenas, which grow in 4 MB chunks.
  - Per-view attachments: colour, depth, ambient and glow; the scene copy for transmission; environment cube faces; the reflection target.
  - Descriptor pools and pipelines.
- Compare with the OpenGL renderer's equivalents. For example, its terrain detail textures and normal maps are uploaded compressed where possible.

## What to report

For each of the three problems:

- the cause, if found;
- the evidence (trace lines, RenderDoc observations, profiles, numbers);
- the fix and its commit, or the next experiment.

Keep the OpenGL renderer unchanged and say how that was checked.

## Findings

### Machine (2026-10-07)

The Deck runs Windows 10 (19045), not SteamOS: Steam Deck OLED
("Galileo", AMD Custom APU/GPU 0932), AMD's Windows driver (Vulkan
1.3.280, driver 24.10.02.03; OpenGL 4.6 compatibility, same driver), Qt
6.10.1 MinGW, Release build. Displays: 800x1280 panel and a 3840x2160
monitor at 250 %; the editor window is 3232x2088, its view 2160x1948
pixels. The Mesa and Linux leads above (RADV, radeonsi, compositor
frame callbacks, perf, MangoHud) did not apply. Tools used instead,
kept outside the repository and the system paths:

- RenderDoc 1.46 portable, driven by Python scripts in qrenderdoc
  (`--python`). Its Vulkan layer is enabled for the child process only
  (`VK_ADD_LAYER_PATH`, `VK_INSTANCE_LAYERS=VK_LAYER_RENDERDOC_Capture`).
- The Khronos validation layer from MSYS2's clang64 package, loaded with
  `VK_ADD_LAYER_PATH` and `TSRE_RHI_DEBUG=1`.
- PresentMon 2.6 (`--process_id <pid> --timed 15 --output_file x.csv`),
  which needs no administrator rights for members of Performance Log
  Users; GPU load and GPU memory from the `GPU Engine` and
  `GPU Process Memory` performance counters, CPU from process and
  thread times.

Route `bbb` (basic, fast to load), editor start view, 15 s after a 20 s
warm-up, FPS display on unless stated.

### 1. Paged terrain not drawn - fixed (`1d6723f`)

- Trace: the draws were recorded, `draw kind1 format1 pass2` 63-412
  a frame. Validation: nothing about terrain (only Qt's
  `VK_IMAGE_LAYOUT_PREINITIALIZED` with optimal tiling, and unused
  `ambientOut` / `glowOut` writes).
- RenderDoc, frame 712: 65 terrain draws, inputs all sensible (16-bit
  index template, `baseVertex` = slot x 289, heights, normal bytes
  `[128, 255, 128, 128]`, patch texture 512 x 1 RGBA32F with origins
  0/128/256/384 and UV step 1/16, viewport depth [0, 0.98]). The VS
  output projects on screen (NDC z 0.975-0.9999). Pixel history at
  (2015, 1753), event 6456: the fragment is rasterized and then
  `shaderDiscarded`. The colour target is plain sky after the last
  terrain draw.
- Cause: the gap flag comes as an unsigned byte, 128 for no gap, and
  `StandardFog.vs` / `Shadows.vs` decoded it as
  `(normal * 255.0 - 128.0) / 127.0`. On AMD (Vulkan and OpenGL)
  128/255 x 255 comes out a little over 128, so `vTerrainGap > 0.0`
  discarded every fragment; lavapipe and llvmpipe give exactly 128. The
  shaders now round to the byte first (QRhi branch only).
- Check: `parity-views` on bbb, QRhi against the OpenGL renderer, paged
  terrain: Vulkan RMSE 0.83-1.55 (at most 0.33 % of pixels), OpenGL RMSE
  0.83-1.48, picking 144/144 in every view.
- `StandardFogStoredCoords.vs` still reads `normal.w` without the
  decode; it is a reference shader and not used by the editor.
- Also (`f051eee`): the Vulkan instance leaves out portability drivers
  outside macOS. RenderDoc refuses instances that enable
  `VK_KHR_portability_enumeration`, so Vulkan could not be captured.

### 2. Frame pacing and load

| Renderer, legacy terrain | Frames/s | Median ms | Busiest thread | GPU 3D |
|---|---|---|---|---|
| OpenGL renderer | 63.8-65.7 | 12.2 | 71-77 % (+52 % driver thread) | 72 % |
| QRhi Vulkan, before | 39.6 | 25.1 | 60 % | 51 % |
| QRhi Vulkan, after `c372ad2` | 57.9-58.6 | 16.9 | 76 % | 62 % |
| QRhi Vulkan, after `cb3cca7` (paged terrain) | 66.2-66.7 | 14.9 | 87-95 % | - |
| QRhi OpenGL, before | 24.9 | 40.3 | 72 % | 33 % |
| QRhi OpenGL, after | 49.3-51.5 | 20.0 | 84 % (+38 %) | 58 % |

Two causes, both on the main thread:

- The overlay (`c372ad2`): the FPS display painted a window-sized image
  (2160x1948, 17 MB), cleared, uploaded and blended over the frame every
  frame, and flipped into a copy first on OpenGL. With the display off
  QRhi Vulkan ran at 66.4 against 39.7. Now the image and its texture
  persist, the painter passes its area, only the area painted last time
  is cleared, only the changed part is uploaded, and the overlay is
  drawn scissored to the painted area. A source rectangle was not
  enough (`cb3cca7`): QRhi's Vulkan backend copies only the rectangle
  but sizes the staging buffer for the whole image, so the changed part
  is copied first. QRhi Vulkan then runs at the editor timer's limit
  (15 ms) with the FPS display on, as the OpenGL renderer.
- `QWindow::requestUpdate()` (`48d3974`): on Windows Qt delivers the
  UpdateRequest after a 5 ms timer (`QT_QPA_UPDATE_IDLE_TIME=0` alone
  gave 39.6 -> 51). The surface now posts the UpdateRequest itself,
  coalesced, at low priority, as QOpenGLWidget repaints are posted.
  Alternating runs after the overlay fix: Vulkan 57.9-58.6 posted
  against 42.9-43.2 with `requestUpdate()`; OpenGL 49.3-51.5 against
  37.2/37.3/50.9. After `cb3cca7`, three alternating runs of each way
  to schedule the frame (paged terrain, FPS display on): posted at low
  priority 66.2-66.5, posted at normal priority 66.2-66.6, rendered
  from the timer 58.8/66.2/66.7, `requestUpdate()` 49.2-50.8. Loading
  did not differ: first frame 3.0-3.3 s after launch (one cold start
  4.3 s), last mesh upload 0.1 s later, in every variant.

Not causes: the editor timer (`core.system.fpsLimit=200`, 5 ms step,
changed nothing); waits on the GPU (`finish()` ran once in 1109 frames,
for the first pointer depth; `beginFrame` 0.2 ms, `endFrame` 1.3 ms,
time in Present 0.17 ms). QRhi Vulkan records the whole frame and
submits at the end, so the GPU starts when the CPU is done (PresentMon
GPU latency equals the frame time); that adds latency, not frame time.

Open:

- QRhi OpenGL is CPU-bound at about 20 ms a frame (main thread 84 %),
  against 12 ms for the OpenGL renderer; not profiled (no symbols for
  Qt or the driver on this build). Leads from the brief stand: glUniform
  per member on every `setShaderResources`, `glBufferSubData` of the
  instance and uniform arenas.
- QRhi OpenGL after `cb3cca7`, paged terrain: 45.8-46.9 frames a
  second, main thread 87 %, against 64.2-64.5 for the OpenGL renderer.
- Loading on a larger route: bbb loads within 0.1 s of the first frame,
  too fast to tell the ways of scheduling frames apart.

### 3. Memory - fixed (`ab8562a`)

| Renderer, legacy terrain | Private MB | GPU shared MB | GPU dedicated MB |
|---|---|---|---|
| OpenGL renderer | 1278-1286 | 429 | 209 |
| QRhi Vulkan, before | 1830-1843 | 1139 | 209 |
| QRhi Vulkan, after | 1253-1277 | 590 | 185 |
| QRhi OpenGL | 1288-1317 | 506-548 | 118-160 |

- The extra memory was Vulkan device memory only (QRhi OpenGL matched the
  OpenGL renderer). `TSRE_RHI_TRACE` (`ec5ae18`) logs QRhi's allocator:
  956 MB used and 179 MB unused in 14 blocks, against 279 MB of mesh
  buffers and 64 MB of library textures (194 RGBA8, 9 BC1). In the
  RenderDoc capture every vertex and index buffer had a twin of the same
  size with transfer usage only.
- Cause: QRhi's Vulkan backend (Qt 6.10.1 `qrhivulkan.cpp`, static
  upload) keeps a host staging buffer, as large as the buffer, for every
  `Static` buffer (one per frame slot it was uploaded in), and frees it
  only for `Immutable` buffers. The mesh store now creates Immutable
  buffers; a later upload goes through a temporary staging buffer.
  Steady state has no mesh uploads (`uploads new 0 again 0 ranges 0`).
- After: allocator 472 MB used, 87 MB unused, 1647 allocations (3045);
  frame rate equal over three alternating runs each (39.7-40.0 against
  39.2-39.7, before the overlay fix).

### OpenGL renderer unchanged

The QRhi shader change sits under `TSRE_RHI`; the other changes are in
QRhi code, except the overlay area argument, which the OpenGL surface
ignores. `parity-views` with the OpenGL renderer, bbb, paged terrain:
captures before the work (shaders of `20022a2`) and after all commits
compare at RMSE 0.00, 0.00 % pixels and equal primitives and samples
in every view. The `all` target builds. Suites `rhi-shaders`,
`terrain-mesh-gl` and `selection-id` pass; `terrain-material-gl` fails
13 checks when the profile selects the QRhi backend, before these
changes as well, and passes with `--set=core.rendering.backend=opengl`.
