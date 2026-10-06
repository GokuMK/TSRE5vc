# Task 20 - QRhi Renderer

## Objective

A second renderer on Qt's QRhi (Vulkan, Metal, Direct3D 11/12 and OpenGL
backends) behind the same `Renderer` interface as `OpenGL3Renderer`, chosen at
startup. First parity with the OpenGL renderer on every capture set, then new
features (emissive light, many lights). Branch `feature/qrhi`, not merged into
main until the user decides.

## Toolchain (checked 2026-10-06)

- Qt 6.11.2: `rhi/qrhi.h` needs `Qt6::GuiPrivate`, `rhi/qshaderbaker.h`
  `Qt6::ShaderToolsPrivate`.
- llvmpipe drives both QRhi backends here: OpenGL (Mesa 4.6 compatibility)
  and Vulkan (lavapipe, `vulkan-swrast`, with `vulkan-validation-layers`).
  Both support wide lines, non-fill polygon modes, R32UI textures,
  instancing, base vertex, texel fetch and texture arrays.
- QRhi has no occlusion queries and no clip-distance feature flag.

## Architecture

### One QRhi, a swapchain per view

The OpenGL renderer shares buffers and textures between windows through a
context share group. QRhi resources belong to one `QRhi`, so the QRhi
renderer uses a single process-wide `QRhi` (`RhiContext`) and gives each view
its own `QWindow` with a `QRhiSwapChain`, embedded in the editor widget with
`QWidget::createWindowContainer`. `QRhiWidget` would give each top-level
window its own `QRhi`, so the route editor and a shape viewer window could
not share meshes or textures. Captures render offscreen frames from the same
`QRhi`.

### Host

`RouteEditorGLWidget` and `ShapeViewerGLWidget` become plain widgets that
host a render surface: a `QOpenGLWidget` child for the OpenGL renderer, or
the QRhi window container. The surface calls back the widget to initialise,
resize and paint, and forwards input to it. The widgets keep their names,
signals and event handlers.

### Shared queue

The backend-neutral part of `OpenGL3Renderer` (submission, routing to
passes, bounds, culling, sorting, instancing plans, shadow caster selection)
moves to a base class both renderers derive from.

### Targets and programs

The widgets bind targets and choose programs through renderer calls
(`bindTarget`, `useProgram`, `beginSelection` / `readSelection`), the
framebuffer ownership step of task 19. The environment map and the water
reflection keep their scheduling and geometry and take their textures from a
storage the renderer creates (`createEnvironmentStorage`,
`createReflectionStorage`): OpenGL objects, or QRhi targets.

### Frames on QRhi

- QRhi cannot change uniforms or clear depth inside a render pass. The QRhi
  renderer records draws for the current target and executes them when the
  target ends: uploads first, one uniform buffer with per-draw dynamic
  offsets, then one pass.
- The sky / distant / scene bands clear depth between bands in OpenGL. On
  QRhi each band gets its own slice of the depth range (viewport min/max
  depth), farther bands behind nearer ones, so one pass draws all three.
- The view renders into an offscreen colour and depth target, then to the
  swapchain when the surface ends the frame, with the overlay the widget
  painted (FPS counter) composed over it; the transmission copy, grabs and
  readbacks use that target.
- Each target keeps a render target per combination of clears at the start
  of its pass (`Attachments`); pipelines are made against the one that
  keeps both.
- Textures sampled by direction or projection (shadow maps, cube faces) keep
  OpenGL's row order: on y-down framebuffers their projection is flipped
  again and the winding turned, so the lit shaders sample them unchanged.
  Screen-position lookups (water reflection, transmission copy) use the
  backend's order on both sides.
- Matrices and lights still come from `GLUU`; the QRhi renderer copies them
  into each program's uniform block when draws are recorded.
- The water reflection clips under its plane in the fragment shader (QRhi
  enables no clip distances on OpenGL).
- Selection, pointer depth and colour reads wait for the GPU within the
  frame (`QRhi::finish`). Depth is read by a probe pass that samples the
  view's depth texture into a 1 x 1 float target.
- Water visibility (OpenGL occlusion query) needs another source on QRhi;
  until then the reflection is drawn whenever water is in view.

### Shaders

`RhiShaderSource` converts the `shaders330` programs when they are first
used: it resolves the variant `#if`s, gathers the loose uniforms of both
stages into one std140 block (binding 20), keeps the OpenGL texture units as
sampler bindings, assigns attribute and varying locations and turns
fragment inputs no vertex stage writes into private zeros. `QShaderBaker`
bakes the result for the backend (SPIR-V, GLSL 330; HLSL and MSL on Windows
and macOS). Uniform offsets come from the shader reflection; arrays take
their std140 stride from their size. Editing a shader still needs no
rebuild. Code-held shaders (environment prefilter) go through the same
conversion.

### Meshes and textures

The mesh store keeps uploading lazily; on QRhi it creates `QRhiBuffer`s in
the single `QRhi`. TexLib textures become renderer-owned handles that both
renderers resolve; the QRhi renderer keeps the CPU pixels until upload.

## Milestones

1. Shared queue base, target and program calls, render surface host
   (OpenGL renderer pixel-identical).
2. QRhi context, surface, shader baking, settings
   (`core.rendering.backend`, `core.rendering.rhiApi`).
3. Shape Viewer on QRhi: lit, textured, fog, alpha test and blending,
   compared with the OpenGL captures.
4. Route Editor on QRhi: bands, terrain (all mesh modes and materials),
   objects, overlays, UI, water.
5. Shadows, environment map, water reflection, PBR, transmission,
   instancing.
6. Selection, pointer depth, screenshots.
7. New: emissive light and many lights.

## Status (2026-10-06)

Milestones 1 to 6 are done on `feature/qrhi`. Parity sweep, QRhi on Vulkan
(lavapipe) against the OpenGL renderer of the same build, every view of
`parity-views` and `parity-views-shadows` on EUROPE1, JAPAN1, USA1 and
BNSF_SCENIC:

- Images: RMSE 1.2 to 7.1 (0.00 to 0.16 % of pixels off by more than 16);
  the remaining differences are thin-line rasterization (track lines).
- Picking: 144 of 144 probes equal in every view but USA1 aerial (143, a
  probe on a track line).
- Pointer depth: within 0.2 m.
- QRhi on OpenGL: RMSE 0.85 (EUROPE1) and 1.28 (BNSF), picking equal.
- glTF samples (32, Shape Viewer with the warehouse cube): RMSE up to 1.4.
- PROCEDURAL (direct GPU material arrays): RMSE 1.45.
- Environment map in the Route Editor (water without planar reflection):
  RMSE 2.4 against OpenGL, where the cube changes the image by 10.3.

Open:

- Water visibility without occlusion queries (the reflection is drawn
  whenever water is in view).
- Direct3D and Metal are not created yet (their init parameters are
  missing; `auto` falls back to Vulkan or OpenGL). Full-screen passes flip
  y where clip space and the framebuffer disagree about it (those two), by
  reasoning rather than a test.

## Hardware test (2026-10-06)

First run on a GPU: terrain missing, QRhi OpenGL at 50 % and Vulkan at 70 %
of the OpenGL renderer's frame rate, twice the time to the first frame.
Found and changed:

- Vsync: the OpenGL renderer runs with swap interval 0, QRhi swapchains
  had vsync, so the frame rate stopped at the display's refresh. Now
  `NoVSync`, frames paced by the editor's timer as before.
- Uniform blocks on OpenGL: QRhi's OpenGL backend has no uniform buffers;
  it sets every member with glUniform whenever shader resources are set.
  The 256 patch records of a terrain page (512 vec4) went again with each
  of about 700 terrain draws a frame. They now come from a one-row
  RGBA32F texture (`TerrainPatch.glsl`, binding 19); the OpenGL renderer
  keeps its block.
- Redundant state: every draw set its viewport, shader resources and
  vertex input. Draws now skip state equal to the draw before, reuse an
  equal uniform block, and pass instance and index offsets as
  firstInstance (`QRhi::BaseInstance`) and firstIndex. EUROPE1, one view:
  862 draws, 183 resource and 140 vertex input changes (862 each before),
  uniforms 0.47 MB a frame (1.09 MB).
- The 3D pointer read its depth with a readback that waited for the GPU
  mid-frame (`QRhi::finish`) 20 times a second; it now takes the last
  completed read (`readDepthLatest`).
- Textures: DXT textures were decoded to RGBA8 on the CPU at upload (all
  levels); DXT1 without alpha, DXT3 and DXT5 now go up as BC1/BC2/BC3 as
  in OpenGL. DXT1 with alpha has no QRhi format and is still decoded.
- Startup: the driver's pipelines are kept between runs (QRhi pipeline
  cache, `rhi-pipelines-<api>.bin` in the cache directory). Baking the
  shaders (glslang, SPIRV-Cross) takes about 10 ms a stage and is not
  cached.
- Diagnostics: the FPS display shows the GPU time of the last frame
  (QRhi timestamps); `TSRE_RHI_TRACE=1` logs draws, state changes,
  pipelines and uniform bytes per frame; `TSRE_RHI_API=null` runs without
  a GPU driver (renderer CPU cost: 1.7 ms a frame on EUROPE1 in a
  RelWithDebInfo build).

The terrain failure did not reproduce on lavapipe or llvmpipe; the patch
records it read moved from a uniform block to a texture since.

Retest on the Steam Deck (Windows, AMD driver), task 25: paged terrain was
discarded by the gap flag decode, frames waited on the overlay upload and
on `QWindow::requestUpdate()`, and Vulkan kept host copies of every mesh
buffer. All fixed; QRhi OpenGL remains CPU-bound (about 50 against 64
frames a second).

## Verification

Captures with `--set=core.rendering.backend=qrhi` compared with OpenGL
captures of the same build (RMSE and picking), on OpenGL and Vulkan QRhi
backends, plus the suites. OpenGL captures must stay pixel-identical to the
pre-task baseline (`q0`, taken after the 2026-10-06 system update).
