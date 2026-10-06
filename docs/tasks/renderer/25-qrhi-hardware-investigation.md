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

(to be filled in)
