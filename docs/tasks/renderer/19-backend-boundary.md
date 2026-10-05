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

## Plan

Done in this task (no change in pixels):

- `Renderer::resetState()`: the default state, used by both widgets.
- `Renderer::clear(colour, depth, clearColour)` and `setViewport()`.
- `Renderer::LayeredView` and `renderLayeredView()`: one call draws sky,
  distant terrain and the scene passes for a view matrix and a projection
  function, optionally mirrored about a clip plane, with view limits; the
  cube faces and the water reflection use it, and the main view uses its
  band helpers around the pointer drawing and stats phases.
- `Renderer::setMirror(plane)`: front faces and the clip plane of a
  mirrored view.
- `Renderer::renderPassesMeasured()` and `measuredSamples()`: the water
  query.
- `Renderer::readDepth(x, y)`: depth under the mouse.

Left for the framebuffer and texture ownership step (with the second
renderer): binding targets (default framebuffer, shadow maps, cube faces,
reflection) through renderer-owned target handles, TexLib and procedural
terrain textures, and moving shader selection by name (`GLUU::shaders`)
behind pass or material kinds.

## Status

- [x] Inventory and plan.
- [ ] Renderer calls above, verified pixel-identical (routes with shadows,
  with the environment map, the Shape Viewer and the glTF set).
