# Task 11 - Shader Pass Buckets And Custom Shaders

## Objective
Split gather rendering into shader-specific passes so we do not run one large shader for all object types.

## Scope
- Add a shader pass key/tag to gathered render items.
- Group and render queue items by shader pass (not only by texture).
- Use `StandardFog` (or equivalent default lit textured pass) for main VNTA/world shapes.
- Add a dedicated terrain pass/shader variant for terrain tiles.
- Add a dedicated lines/helpers pass/shader for overlays, editor lines, and simple markers.
- Keep shadow pass integration compatible with Task 10.

## Suggested Touch Points
- `src/tsre/renderer/RenderItem.h`
- `src/tsre/renderer/OpenGL3Renderer.cpp`
- `src/routeEditor/RouteEditorGLWidget.cpp`
- Gather producers in world/terrain/overlay paths that should set shader pass hints
- `appdata/0.697/shaders/*` (new shader files or minimal variants)

## Requirements
- Preserve runtime switch safety between legacy and gather.
- Keep selection/picking behavior unchanged.
- Do not introduce frame-to-frame stale state for dynamic/animated objects.
- If any queue caching is introduced, it must have clear invalidation for shape state changes, transform updates, late texture/material readiness, and tile load/unload.
- Keep a safe fallback: unknown/unassigned pass must render through default shader.

## Acceptance Criteria
- Gather mode renders by pass/shader without visual regressions in core world rendering.
- Terrain, lines/helpers, and VNTA/world shapes use intended passes.
- No regressions when switching renderer mode at runtime.
- No persistent stale visuals after async resource updates (for example pink/missing texture sticking).
- Debug output (or counters) can show per-pass draw/item counts when enabled.

## Testing Notes
- Validate at least one dense scenery area.
- Validate one area with many overlays/track DB lines.
- Validate one route section with terrain/water horizon in view.
- Validate one session with tile streaming (move across multiple tiles).
- Animation-specific validation is required once reliable animated test assets are identified.

## Out Of Scope
- Full material graph/PBR redesign.
- Deferred renderer implementation (this task should remain forward-compatible).
- Legacy pipeline removal.
- Final parity/performance gate (Task 12).
