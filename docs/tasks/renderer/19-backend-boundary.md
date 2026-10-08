# Task 19 - Backend Boundary Review

## Objective

Before a second renderer (QRhi, Vulkan, a deferred OpenGL renderer) is
started, find every place outside `src/tsre/renderer` that still talks to
OpenGL directly, and decide how each moves behind the `Renderer` interface.
Textures and framebuffers stay where they are until the second renderer
exists (decided earlier); this review lists them so that step is sized.

## Inventory (2026-10-06)

Raw `gl*` calls outside `src/tsre/renderer`, `src/tsre/ogl/GLUU*`,
`Shader` and the test suites. Commented-out legacy code (`Terrain::oglInit`,
`ForestObj` VAO setup, `TSection` Java leftovers) is not counted.

| Where | Calls | What | Group |
| --- | --- | --- | --- |
| `RouteEditorGLWidget::initializeGL`, `restoreDefaultGlState` | 15 | depth test, culling, blending, colour mask, scissor defaults | State |
| `RouteEditorGLWidget::paintScene` | 10 | bind default framebuffer, clear, viewport, blend off for selection, depth clears between sky, distant and scene | Frame and views |
| `RouteEditorGLWidget::renderShadowMaps` | 12 | bind each shadow framebuffer, clear, viewport | Targets |
| `RouteEditorGLWidget::renderEnvironmentMap` | 2 | depth clears between face bands | Frame and views |
| `RouteEditorGLWidget::renderWaterReflection` | 8 | depth clears, front face flip, clip distance, query result | Frame and views |
| `RouteEditorGLWidget::renderWaterPass`, `cleanup` | 4 | samples query around the water pass | Measurement |
| `RouteEditorGLWidget::readPointerPosition` | 2 | viewport, depth read under the mouse | Readback |
| `ShapeViewerGLWidget` | 11 | state defaults, clear colour, clear, depth read | State, Readback |
| `Texture.cpp` | 49 | TexLib texture upload and parameters | Textures (parked) |
| `TerrainProceduralMaterial.cpp` | 90 | procedural terrain texture arrays and maps | Textures (parked) |
| `Terrain.cpp` `uploadTerrainBaseTexture` | 2 | clamp baked terrain textures | Textures (parked) |

Not raw GL but still GL-shaped, through `GLUU`: the widgets set
`pMatrix`/`fMatrix`/shadow matrices and bind programs by name
(`gluu->shaders["Shadows"]`), and `GLUU` owns the shadow framebuffers.

## Findings

1. **The sky / distant / scene sequence is written three times**: the main
   view, each environment cube face and the water reflection all set three
   projections, draw sky, clear depth, draw distant terrain, clear depth and
   draw the scene. A second renderer would have to see the same sequence
   three times too. This is the main thing to move.
2. **State defaults** are set by the widgets (`restoreDefaultGlState`,
   `initializeGL`). They belong to the backend.
3. **Mirrored views** (front faces turned, user clip plane) and **pass
   measurement** (samples query) are OpenGL features used from the widget;
   both have equivalents in other APIs and fit a renderer call.
4. **Readback** of depth under the mouse (pointer placement) and of the
   Shape Viewer depth is direct `glReadPixels`.
5. **Targets**: the default framebuffer, shadow maps, cube faces and the
   reflection are bound by the widget. Moving them is the framebuffer
   ownership step; it waits for the second renderer, but a renderer call
   that takes "the target to draw into" is the shape it will need.
6. **Textures**: TexLib and the procedural terrain upload their own
   textures; renderer-owned textures are the parked step. About 140 calls.

No blocker was found: nothing in the widgets depends on OpenGL behaviour
that another backend could not provide.

## Done

All verified pixel-identical: the four routes with and without shadows,
BNSF with the environment map on (cube faces and water reflection), the MSTS
Shape Viewer set and the glTF set, plus every suite.

| Renderer call | Replaces |
| --- | --- |
| `resetState()` | `restoreDefaultGlState()` and the state set in both widgets' `initializeGL`; it also sets the default line width (the Shape Viewer used OpenGL's 1) |
| `setBlending()` | blending off and back on around the selection pass |
| `clear()`, `setViewport()`, `viewport()` | frame, shadow map and Shape Viewer clears and viewports |
| `LayeredView`, `beginViewBand()`, `endView()`, `renderLayeredView()` | the sky / distant / scene sequence: cube faces and the water reflection call `renderLayeredView()`; the main view calls the bands itself around its stats phases and pointer drawing |
| `LayeredView::mirrorPlane` | front-face flip and clip distance of the water reflection |
| `renderPassesMeasured()`, `measuredSamples()` | the widget's water samples query |
| `readDepth()`, `readColor()` | the pointer's depth read and the Shape Viewer screenshot |

What the widgets still do with OpenGL directly: bind render targets (the
default framebuffer, the three shadow map framebuffers) and select the
active texture unit next to them. That is the framebuffer ownership step.

## Left For The Second Renderer

- Render targets as renderer-owned handles: default framebuffer, shadow
  maps (now created by `GLUU::makeShadowFramebuffer`), environment cube
  faces, water reflection.
- Textures: TexLib uploads (`Texture.cpp`), procedural terrain arrays and
  maps (`TerrainProceduralMaterial.cpp`), baked terrain clamping.
- Programs by name (`gluu->shaders["Shadows"]`, `"Selection"`, the main
  program) and the matrix fields the widgets fill in `GLUU` (`pMatrix`,
  `fMatrix`, shadow matrices): pass or view descriptions the renderer turns
  into its own uniforms.
- `EnvironmentMap`, `PlanarReflection` and `WaterNormalMap` are OpenGL
  classes inside `src/tsre/renderer`; a second renderer needs its own.

## Status

- [x] Inventory and plan.
- [x] Renderer calls above, verified pixel-identical.
- [x] Render targets and programs (with the QRhi renderer, task 20, on
  `feature/qrhi`): `bindTarget`, `useProgram`, selection, and the storage
  of the environment map and the water reflection.
- [ ] Textures: producers still upload them with OpenGL calls on the
  OpenGL renderer (`Texture.cpp`, `TerrainProceduralMaterial.cpp`,
  `uploadTerrainBaseTexture` in `Terrain.cpp`) beside `RhiTextures` calls
  on QRhi.
- [ ] Matrices and lights: the widgets fill `GLUU` fields that both
  renderers read.
