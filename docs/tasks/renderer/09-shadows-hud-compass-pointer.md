# Task 09 - Shadows, HUD, Compass, Pointer

## Objective
Restore remaining frame passes and UI elements for gather mode parity.

## Scope
- Shadow map generation and use in gather path.
- HUD rendering.
- Compass rendering.
- 3D pointer and remote client pointer visuals.

## Suggested Touch Points
- `src/routeEditor/RouteEditorGLWidget.cpp`
- `src/tsre/hud/*.cpp`
- shadow-related renderer/shader setup files

## Requirements
- Keep pass order stable.
- Do not break selection/picking by UI pass changes.

## Acceptance Criteria
- Gather mode includes shadows (if enabled), HUD, compass, and pointer behavior matching legacy mode.
- No obvious UI z-order or depth artifacts.

## Out Of Scope
- Automated parity infrastructure and final go/no-go gate.
