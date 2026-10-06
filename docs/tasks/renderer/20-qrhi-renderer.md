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

The widgets still bind framebuffers (screen, shadow maps) and choose
programs by name (`gluu->shaders["Shadows"]`). Both become renderer calls
(render target handles and program kinds), implemented by both renderers;
this is the framebuffer ownership step of task 19.

### Frames on QRhi

- QRhi cannot change uniforms or clear depth inside a render pass. The QRhi
  renderer records draws for the current target and executes them when the
  target ends: uploads first, one uniform buffer with per-draw dynamic
  offsets, then one pass.
- The sky / distant / scene bands clear depth between bands in OpenGL. On
  QRhi each band gets its own slice of the depth range (viewport min/max
  depth), farther bands behind nearer ones, so one pass draws all three.
- The view renders into an offscreen colour and depth target, then to the
  swapchain; the transmission copy, grabs and readbacks use that target.
- Matrices and lights still come from `GLUU`; the QRhi renderer copies them
  when passes are drawn.
- The water reflection clips under its plane in the fragment shader.
- Water visibility (OpenGL occlusion query) needs another source on QRhi.

### Shaders

GLSL 440 sources in `appdata/<version>/shadersrhi`, with the same `#include`
and define variants as `shaders330`, baked at startup with `QShaderBaker`
into SPIR-V and GLSL 330 (HLSL and MSL on Windows and macOS). Editing a
shader still needs no rebuild.

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

## Verification

Captures with `--set=core.rendering.backend=qrhi` compared with OpenGL
captures of the same build (RMSE and picking), on OpenGL and Vulkan QRhi
backends, plus the suites. OpenGL captures must stay pixel-identical to the
pre-task baseline (`q0`, taken after the 2026-10-06 system update).
