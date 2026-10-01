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
- `appdata/0.7/shaders/*` (new shader files or minimal variants)

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

## Pass buckets (implemented)

Status: explicit passes, surface classes, texture handles and per-pass
counters are implemented. All passes still use the main shader
(`StandardFog`); per-pass shaders are not started.

- `Renderer::RenderPass` defines the order: `PASS_TERRAIN`, `PASS_OPAQUE`,
  `PASS_ALPHA_TEST`, `PASS_BLENDED`, `PASS_OVERLAY`. Each pass draws its
  ordered work in submission order, then its grouped packets.
- The renderer routes submissions; producers do not name passes:
  - the overlay layer (`setLayer(LAYER_OVERLAY)` around
    `Route::pushRenderOverlays`) goes to `PASS_OVERLAY`;
  - `SURFACE_TERRAIN` packets go to `PASS_TERRAIN`;
  - other ordered work (helpers, decals, animated shapes) to `PASS_OPAQUE`;
  - grouped packets by `RenderItem::surface`.
- Grouped opaque, alpha-test and overlay packets are batched by texture with
  a deterministic order; blended packets are sorted back to front from the
  camera position (`setViewPosition`), measured at the packet origin.
- `SFileLegacy` sets the surface from the material, matching the per-vertex
  alpha mode it already writes: `texdiff` is opaque, alpha-test mode 1 is
  alpha test, other materials blend. glTF, `SFileComplex` and `SFile` packets
  are opaque until they classify their materials.
- `renderPasses(first, last)` replaces the mid-frame `renderFrame()` flushes.
  The gather frame draws `PASS_TERRAIN` before the terrain-attached pointer,
  the scene passes before the directly drawn water (legacy order; water was
  previously drawn before the world), and the overlay pass in `renderFrame()`.
- Packets can reference a TexLib texture (`textureId`), resolved and uploaded
  when drawn; a texture that is not ready draws in the missing-texture
  colour. `SFileLegacy` packets use it, so texture loading no longer rebuilds
  shape caches.
- `RenderStats` counts draws per pass (`passDraws`).

Remaining for this task: dedicated shader variants per pass (terrain,
lines/helpers), and surface classification for glTF and `SFileComplex`.
