# Task 10 - Shadows Gather Pass

## Objective
Add shadow map generation/use to gather mode without mixing unstable legacy post-passes.

## Scope
- Shadow map generation for gather path.
- Shadow map usage in gather scene pass.
- Keep shadow quality controls (`shadowMapSize`, `shadowLowMapSize`, biases) functional.

## Suggested Touch Points
- `src/routeEditor/RouteEditorGLWidget.cpp`
- `src/tsre/renderer/*.cpp`
- Shadow-related shader setup files

## Requirements
- Keep gather and legacy runtime-switch-safe.
- No regressions in world transforms, texture binding, or overlay visibility.
- Selection/picking must remain unaffected by shadow pass changes.

## Acceptance Criteria
- Gather mode renders shadows when enabled and keeps behavior close to legacy.
- No broken matrices, missing objects, or texture corruption after runtime mode switches.
- Shadow toggle/settings still work in both pipelines.

## Out Of Scope
- HUD/compass/pointer (Task 09).
- Parity automation and performance gate (Task 11).
