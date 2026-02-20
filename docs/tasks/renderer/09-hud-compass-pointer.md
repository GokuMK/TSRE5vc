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
