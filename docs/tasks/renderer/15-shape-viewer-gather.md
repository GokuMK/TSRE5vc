# Task 15 - Shape Viewer And Consist Editor On The Renderer

> **Legacy pipeline removed (2026-10-03).** The gather renderer is the only
> pipeline and `core.rendering.pipeline` no longer exists, so commands below
> that select a pipeline do not apply. To check a renderer change, capture a
> baseline and compare against it; see `00-task-roadmap.md`.

## Objective

Draw the Shape Viewer and Consist Editor (`ShapeViewerGLWidget`) through the
gather renderer, so the legacy `render()` paths of shapes, engines, consists
and `OglObj` are no longer needed outside the legacy pipeline. This is the
prerequisite for removing legacy rendering.

## Implementation

Status: implemented; legacy remains selectable for comparison.

- `ShapeViewerGLWidget` owns an `OpenGL3Renderer` and installs it as
  `Game::currentRenderer` only while it draws, restoring the previous one, so
  it can coexist with a route editor window. With
  `core.rendering.pipeline=gather` the frame submits the current item and
  draws it with `renderFrame()`; otherwise the legacy body runs.
- `Eng::pushRenderItems()` and `Eng::pushDrawBorder()` mirror `render()` and
  `drawBorder()`. `Eng::resolveShapeIds()` replaces three copies of the
  shape-id lookup.
- `Consist::pushRenderItems()` mirrors the viewer layout of `render()`:
  wagons, the selected-wagon border and the name, number and wagon-type
  labels. Picking keeps the Shape Viewer's own IDs (wagon i is
  `selectionId + i`). The labels are created by shared helpers used by both
  paths.
- The consist-preview test shape has a gather path, so `consist-preview-gl`
  covers both pipelines.

## Verification

- `consist-preview-gl` passes with `--set core.rendering.pipeline=legacy` and
  `--set core.rendering.pipeline=gather`, including wagon picking after a
  resize.
- `shape-viewer-capture` / `shape-viewer-compare` render the items in
  `tests/renderer/shape-viewer-views.json` (two route shapes, two engines, a
  wagon with freight animation, a consist with labels) in a hidden viewer,
  one process per pipeline, and diff the images:

  ```bash
  for p in legacy gather; do
      TSRE5vc --game-root <root> --test --test-suite=shape-viewer-capture \
          --test-cases tests/renderer/shape-viewer-views.json \
          --set core.rendering.pipeline=$p
  done
  TSRE5vc --test --test-suite=shape-viewer-compare \
      --test-cases tests/renderer/shape-viewer-views.json
  ```

  On llvmpipe (MSTS stock content) shapes, engines and the freight wagon
  match exactly; the consist differs by 0.01% of pixels.

## Manual checks on Windows

With `--set core.rendering.pipeline=gather`:

1. Shape Viewer: open MSTS and glTF shapes; rotate; texture and hierarchy
   panels still work; screenshot (image capture) works.
2. Consist Editor: open consists; wagon names, numbers and type letters
   show; clicking selects the wagon and shows the red border; flip, move,
   delete, copy and paste update the view.
3. Engine preview in the Consist Editor, including wagons with freight
   animations.
4. A route editor window open at the same time keeps rendering correctly.

## Review: drawing only legacy did (2026-10-03)

Prompted by the missing selection border on activity consists, every legacy
draw function was compared with its gather counterpart: the draw calls each
makes, the `Game::view*`/`show*` toggles each tests, and any legacy drawing or
GLUU draw state called from gather functions. Fixed:

- Activity consists on track: the selected wagon's border. Legacy draws it
  after `Eng::renderOnTrack()` returns, relying on the wagon matrix left in
  the shader; `Eng::pushRenderItemOnTrack()` now submits it inside the wagon
  transform.
- World-object pivot points (`showWorldObjPivotPoints`) for static and track
  objects; the gather calls had been commented out.
- Snapable endpoints (`viewSnapable`) for static objects.

The remaining differences the comparison reports are naming only
(`pushRenderBox()` for `drawBox()`, shapes pushed by the base
`WorldObj::pushRenderItems()`, terrain patches configured instead of drawn).
No gather function calls legacy drawing. The capture cases accept
`pivotPoints` and `snapable` to turn those toggles on; with both on, EUROPE1
legacy and gather differ by 0.06% of pixels with no picking mismatches.
