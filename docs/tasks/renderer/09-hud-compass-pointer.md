# Task 09 - HUD, Compass, Pointer

## Objective
Restore UI and pointer elements in gather mode parity without changing shadow handling.

## Scope
- HUD rendering.
- Compass rendering.
- 3D pointer and remote client pointer visuals.

## Suggested Touch Points
- `src/routeEditor/RouteEditorGLWidget.cpp`
- `src/tsre/hud/*.cpp`

## Requirements
- Keep pass order stable.
- Do not break selection/picking by UI pass changes.

## Acceptance Criteria
- Gather mode includes HUD, compass, and pointer behavior matching legacy mode.
- No obvious UI z-order or depth artifacts.

## Out Of Scope
- Shadow map generation/use in gather mode.
- Automated parity infrastructure and final go/no-go gate.

## Gather implementation

Status: implemented; the gather frame no longer draws these directly.

- Pointer: `RouteEditorGLWidget::updatePointerPosition()` reads the depth
  under the mouse and drives the live placement tools; `drawPointer()` (legacy)
  and `pushRenderPointer()` (gather) share it. The gather frame draws the
  passes the pointer depends on first (terrain only when the pointer sticks to
  terrain, otherwise the whole scene), then submits the pointer and remote
  client markers (`ClientInfo::pushRenderItem()`).
- Compass: `GuiGlCompass::pushRenderItem()` updates the heading strip and
  submits it in `LAYER_UI`; the frame draws `PASS_UI` with the compass
  projection.
- HUD: `Camera::pushRenderHud()` -> `GameObj::pushRenderHud()` ->
  `Consist::pushRenderHud()` -> `SimpleHud::pushRenderItems()`, drawn in
  `PASS_UI` with the HUD projection. It is only active when the camera follows
  an object (play mode), which the parity capture does not cover.
- The editor FPS label is still painted with `QPainter` over the GL frame.
