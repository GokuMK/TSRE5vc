# TSRE Renderer Pipeline Review and Modernization Design

## Scope
- Project: TSRE5vc (route editor/game engine renderer path)
- Request: design review only, no runtime code changes
- Goal: document current old/new pipelines, renderable types, and propose a realistic modernization path

## Current Status (2026-10-03)

Sections 1-3 describe the pipeline when this review was written; much of it
is out of date. Current state:

- The legacy pipeline is removed (stage 2a): the gather renderer is the only
  pipeline, the pipeline settings are gone, and producers no longer have a
  legacy `render()` twin.
- Stage 4 step 1: producers receive a `RenderQueue &` in every
  `pushRenderItems()`; the global `Game::currentRenderer` is gone. Weakness 2
  below is resolved.
- Stage 4 step 2: `RenderItem` is grouped into a mesh (buffers, layout,
  backend-neutral primitive and index types, range), a material (surface,
  lighting, texture handle, colour, detail texture, decal, line width,
  wireframe), a terrain block and model bounds, plus the draw parameters.
  GL constants no longer appear in packets; the mesh still holds the
  producer's GL buffer objects.
- Stage 4 step 3: terrain packets draw with `<name>Terrain` shader variants;
  object programs carry no terrain code.
- Stage 4 step 4: procedural terrain patches draw in one pass from per-tile
  material and detail texture arrays.
- Stage 4 step 5: the renderer culls instances against the view-projection
  of the scene passes (`Renderer::setCullView`) and of each shadow map.
  Packets carry bounds where exact: static `SFileLegacy` parts and
  `OglObj` meshes; others are never culled. On EUROPE1 and BNSF_SCENIC this
  removes 6-80% of scene draws and 20-50% of shadow draws with identical
  images.
- Stage 4 step 6: grouped passes draw repeated packets instanced, reading
  matrices from a buffer texture. `SFileLegacy` shares static packets
  between object states with the same distance level and enabled
  sub-objects, so repeated objects submit one packet set; together this
  removes a further 19-82% of scene draws on EUROPE1 and BNSF_SCENIC.

- The gather frame draws everything through the renderer: sky, distant
  terrain and water, high-res terrain, world objects, overlays, water, pointer,
  compass and HUD, and both shadow maps from the gathered queue (task 10).
- Producers own persistent packets; the renderer queues per-frame instances
  and routes them into explicit passes (task 02, task 11).
- Picking uses the integer selection target and 32-bit IDs (task 13).
- Renderer changes are checked by comparing a capture with a baseline
  capture made before the change (task 12).
- Windows hardware validation passed manually (task 14).

## Stage 1 Review (2026-10-02)

### Verdict

The gather approach is the right structure for TSRE. Producers own persistent
packets, the renderer queues cheap per-frame instances, and explicit passes
order them. On llvmpipe, with shadows on and off, gather matches legacy within
about 1% of pixels on four routes with no picking mismatches. Gather frames
allocate no render items or matrices, both shadow maps come from one scene
traversal instead of three, and draw order is deterministic. Every step stayed
behind the pipeline setting, and the parity captures check each change.

Stage 1 settled the queue and passes, not the data model. The weaknesses
below matter for stage 2 and the new renderer.

### Weaknesses

1. **`RenderItem` is still OpenGL-shaped.** It carries VAO/VBO pointers, GL
   enums, raw texture addresses and terrain-only fields. A deferred or non-GL
   backend cannot consume it as is. It should become a mesh handle, a material
   (textures, surface class, alpha mode) and draw parameters. Surface classes
   and texture handles (task 11) are the first part of this.
2. **Producers still contain render logic.** Each producer has a legacy
   `render()` and a gather twin (`render()`/`pushRenderItems()`,
   `renderWater()`/`pushRenderItemWater()`, and so on). Producers use the
   global `Game::currentRenderer`, push and pop its matrix stack, and choose
   ordered or grouped submission. Removing legacy deletes the `render()`
   half, but removing renderer references from data code also needs a
   narrower submission interface than `Renderer`.
3. **The frame is orchestrated in the widget.** `paintGLGather()` sets each
   phase's projection, clears depth, sets layers and decides where passes are
   drawn. The renderer should own pass views (projection, target, clear) as a
   small render graph before a deferred backend is added; otherwise that
   backend needs a second copy of this orchestration.
4. **Packets have no bounds.** Culling still happens per tile and per object
   in producers; shadow maps cull casters by origin distance only; blended
   packets sort by origin. Bounding spheres on packets would allow
   renderer-side frustum culling, per-shadow-map caster culling (45-65% fewer
   near-map casters measured in task 10) and better sorting.
5. **Every instance is one draw call.** Batching reduced state changes, not
   draws. With packets separate from instances, GPU instancing (one draw per
   packet with a matrix buffer) is a small change and likely the largest
   remaining CPU saving.
6. **GL state goes through the `GLUU` singleton.** The current shader and its
   uniforms live there, and selection swaps shaders. Acceptable inside the GL3
   backend, but a new renderer should not depend on it.

Measurement lesson: the first harness compared both pipelines in one process
by switching at runtime and reported false defects. Captures now run one
process per pipeline. Frame times from llvmpipe remain meaningless; task 14
measures them on hardware.

### Next stages

1. Hardware validation (task 14).
2. Remove legacy: delete the `render()` twins and narrow producers to a
   submission interface without GL types.
3. Packet rework: mesh, material, bounds and draw parameters; pass views and
   culling owned by the renderer.
4. Instanced drawing in the GL3 backend.
5. A deferred renderer next to GL3 on the same packets and passes: opaque and
   alpha-test into the G-buffer, blended forward. The three-map shadow split
   from task 10 fits here.

Steps 2 and 3 are stage 2. The stage 1 queue and passes carry over unchanged.

## Evidence Base (Code Anchors)
- Old and new frame entry points: `src/routeEditor/RouteEditorGLWidget.cpp:328`, `src/routeEditor/RouteEditorGLWidget.cpp:453`
- Shadow path: `src/routeEditor/RouteEditorGLWidget.cpp:565`
- Route traversal (new/old): `src/tsre/world/Route.cpp:966`, `src/tsre/world/Route.cpp:1029`
- Tile traversal (new/old): `src/tsre/world/Tile.cpp:886`, `src/tsre/world/Tile.cpp:923`
- Terrain (new gather and old draw): `src/tsre/world/Terrain.cpp:1523`, `src/tsre/world/Terrain.cpp:1815`
- Terrain library gather/draw: `src/tsre/world/TerrainLibQt.cpp:1228`, `src/tsre/world/TerrainLibQt.cpp:1288`
- Renderer core: `src/tsre/renderer/OpenGL3Renderer.cpp:45`, `src/tsre/renderer/OpenGL3Renderer.cpp:86`
- Render item contract: `src/tsre/renderer/RenderItem.h:22`
- Shape gather path: `src/tsre/shape/SFile.cpp:934`, `src/tsre/shape/SFile.cpp:1044`
- OglObj gather path: `src/tsre/ogl/OglObj.cpp:135`
- Selection decode path: `src/routeEditor/RouteEditorGLWidget.cpp:618`

---

## 1. Old Render Pipeline Review (`paintGL2`)

## 1.1 Frame Sequence
`paintGL2` is a direct draw pipeline that traverses world data and issues GL work immediately:
1. Optional shadow prepass (`renderShadowMaps`) when shadows enabled.
2. Main scene setup, shader bind, framebuffer bind, clear, render mode switch (`default` or `selection`).
3. Skydome draw (`route->skydome->render`).
4. Distant terrain draw (`TerrainLib::renderLo`) and distant water (`renderWaterLo`).
5. High-res terrain draw (`TerrainLib::render`).
6. World draw (`route->render` => tiles => world objects).
7. Water layers draw (`TerrainLib::renderWater`).
8. Editor overlays (pointer, compass, HUD).
9. Selection readback/resolve (`handleSelection`) and UI info emit.

## 1.2 World Traversal
Call graph (old path):
- `RouteEditorGLWidget::paintGL2`
- `Route::render`
- `Tile::render`
- `WorldObj::render` (virtual override by each object type)

Selection and LOD behavior:
- Tile window is `[-tileLod, +tileLod]` around camera tile, reduced to 3x3 in selection mode.
- Selection colors are packed per tile/object and decoded in `handleSelection`.

## 1.3 Non-World Systems Included in Old Path
Old path includes systems beyond tile objects:
- Track DB/road DB overlays and items (`renderAll`, `renderLines`, `renderItems`)
- Activity/event/service/path overlays
- Route markers
- Skydome
- Terrain low/high + water low/high
- HUD/compass/pointer
- Selection resolve

These are wired in `Route::render` and `paintGL2`, not only in object classes.

## 1.4 Old Pipeline Strengths and Weaknesses
Strengths:
- Feature complete for editor workflows.
- Behavior parity across object types and overlays.
- Selection and shadow flows are integrated and known-good.

Weaknesses:
- Strong coupling of traversal and draw submission.
- High draw/state churn, limited batching.
- Hard to optimize globally because each object decides render state in-place.
- Maintaining parallel behavior (default/selection/shadow) is duplicated across many render functions.

---

## 2. Renderable Object Types Inventory

## 2.1 World Object Type Inventory (from `WorldObj::TypeID` and creation map)

| Type ID | Runtime class | Old path status | New gather status | Notes |
|---|---|---|---|---|
| `sstatic` | `StaticObj` | Rendered | `pushRenderItems` implemented | Uses `SFile` shape path |
| `signal` | `SignalObj` | Rendered | Not migrated | Uses shape + helper markers |
| `speedpost` | `SpeedpostObj` | Rendered | Not migrated | Shape + lines/helpers |
| `trackobj` | `TrackObj` | Rendered | `pushRenderItems` implemented | Shape or procedural shape |
| `gantry` | `StaticObj` | Rendered | Via `StaticObj` | |
| `collideobject` | `StaticObj` | Rendered | Via `StaticObj` | |
| `dyntrack` | `DynTrackObj` | Rendered | Not migrated | Procedural geometry |
| `forest` | `ForestObj` | Rendered | Not migrated | Procedural billboard mesh |
| `transfer` | `TransferObj` | Rendered | Not migrated | Procedural quad mesh |
| `platform` | `PlatformObj` | Rendered | Not migrated | Shape + lines/helpers |
| `siding` | `PlatformObj` | Rendered | Not migrated | |
| `carspawner` | `CarSpawnerObj` | Rendered | Not migrated | Shape + line helpers |
| `levelcr` | `LevelCrObj` | Rendered | Not migrated | Shape + helper markers |
| `pickup` | `PickupObj` | Rendered | Not migrated | Shape + helper markers |
| `hazard` | `HazardObj` | Rendered | Not migrated | Shape + helper markers |
| `soundsource` | `SoundSourceObj` | Rendered | Not migrated | Helper marker |
| `soundregion` | `SoundRegionObj` | Rendered | Not migrated | Line helpers |
| `groupobject` | `GroupObj` | N/A placeholder | Not migrated | Group edit container |
| `ruler` | `RulerObj` | Rendered | Not migrated | Procedural helpers |

Additional object class present: `TrWatermarkObj` (render path exists, no gather path).

## 2.2 Terrain/Environment/UI Renderables

| Category | Geometry source | Old path | New path |
|---|---|---|---|
| Terrain high-res | `Terrain::render` (`VNT`, patch draws) | Yes | Collect exists (`pushRenderItem`) but not displayed |
| Terrain low-res distant | `TerrainLibQt::renderLo` | Yes | Not migrated |
| Water high/low | `renderWater` / `renderWaterLo` | Yes | Not migrated |
| Skydome | `Skydome::render` (`SFile`) | Yes | Disabled in new frame path |
| TrackDB/RoadDB overlays | `TDB::renderAll/renderLines/renderItems` | Yes | Disabled in new route gather path |
| Activity/path/service overlays | `Activity::render`, `Path::render` | Yes | Disabled in new route gather path |
| Pointer/compass/HUD/text | `OglObj`/`TextObj` direct render | Yes | Disabled or gather path incomplete |
| Selection buffer decode | `handleSelection` | Yes | Disabled in new frame path |

## 2.3 Vertex/Primitive Usage in Existing Assets
Current code uses mixed attribute/primitive types:
- `V` lines (grid, guides, overlays, TrackDB, path lines)
- `VT` textured quads (HUD/compass/text)
- `VNT` terrain and some meshes
- `VNTA` shape and alpha-aware meshes
- Primitives: `GL_TRIANGLES`, `GL_LINES`, and custom line widths

This matters because new renderer currently only submits a narrow subset.

---

## 3. New Pipeline Review (`paintGL`)

## 3.1 What Is Good About Current Direction
The direction is correct:
- Frame is split into data collection (`pushRenderItems`) then renderer submission (`renderFrame`).
- Shape gather path reuses cached `RenderItem` lists in `SFile` and groups by texture in renderer.
- Separation between scene traversal and low-level draw is started.

This is the right architecture direction for TSRE.

## 3.2 Current Functional Gaps (Critical)

1. `OpenGL3Renderer::pushItem` is empty (`src/tsre/renderer/OpenGL3Renderer.cpp:45`).
- Terrain gather (`Terrain::pushRenderItem`) sends work to `pushItem`, so terrain does not render in new path.

2. `OglObj::pushRenderItem` never enqueues (`src/tsre/ogl/OglObj.cpp:175` is commented).
- Anything based on `OglObj` gather path cannot appear.
- This also makes migration of many overlays/object helpers impossible until fixed.

3. Only two world classes override gather.
- `StaticObj::pushRenderItems` and `TrackObj::pushRenderItems` exist.
- Most world objects fall back to empty `WorldObj::pushRenderItems`.

4. New `Route::pushRenderItems` has major systems commented out.
- Track DB/road DB lines and items
- markers
- activity and selected path overlays

5. New `paintGL` has many passes commented out.
- shadows, skydome, low terrain/water, high water, compass, HUD, selection handling

6. Selection flow is currently incomplete.
- `handleSelection()` call is commented in new path.
- `selection` is set on click in tool logic; decode/reset path is not run.

7. `OpenGL3Renderer::renderFrame` currently draws only VNTA-style grouped triangles.
- `items` queue is not rendered (only cleared/deleted).
- line width, polygon mode, primitive type, texture disable/color paths are not honored for generic items.

8. New gather still relies on old global GLUU state patterns.
- Not wrong for incremental migration, but should be intentionally managed.

## 3.3 Why You Currently See Only VNTA Shapes
Because only this chain is effectively active:
- `StaticObj` / `TrackObj` gather
- `SFile::pushRenderItem` builds cached `RenderItem` list with VNTA-oriented geometry
- `OpenGL3Renderer::pushItemsVNTA` groups by texture and submits triangles

Most other content categories are either not collected or collected into paths the renderer does not submit.

---

## 4. Assessment of Direction

Short answer: **yes, this is a good direction**.

Reasoning:
- Gather-then-render is the correct structural move for performance and maintainability.
- You can migrate incrementally without rewriting all world systems at once.
- Existing VAO/VBO assets and SFile infrastructure can be preserved.

Main correction needed:
- Move from a VNTA-only gather path to a **generic queue contract** first.
- Then migrate render producers in batches (terrain, world objects, overlays, picking, shadows).

Without that correction, migration stalls after static/track shapes.

---

## 5. Proposed Realistic Modern Renderer (No Full Redesign)

## 5.1 Design Goals
- Keep OpenGL + Qt stack.
- Keep existing asset formats and object model.
- Preserve editor-specific features (picking, overlays, helper visuals).
- Reduce draw calls/state churn via batching/sorting.
- Allow old and new paths to coexist behind a runtime toggle until parity is reached.
- Design gather packets so a deferred renderer can be added later without rewriting producer systems.

## 5.2 Non-Goals
- No engine-wide ECS rewrite.
- No deferred renderer/PBR overhaul requirement.
- No asset format rewrite.

## 5.3 Target Frame Architecture

Recommended pass graph:
1. Shadow near
2. Shadow far
3. Sky
4. Terrain far
5. Opaque
6. Alpha-tested
7. Transparent (back-to-front)
8. 3D overlays (track db lines/items, markers, helper gizmos)
9. HUD/UI
10. Picking pass (on-demand or when selection flag is set)

Use one central `RenderFrameContext` per frame:
- camera matrices
- render mode (default/selection/shadow)
- pass-local queue containers
- per-frame memory arena for matrices/items

## 5.4 Queue Contract (Minimal Additions)
Extend current `RenderItem` usage with enough metadata to submit correctly:
- pass id
- primitive type (`GL_TRIANGLES`, `GL_LINES`)
- vertex attr (`V`, `VT`, `VNT`, `VNTA`)
- blend/depth/cull flags
- shader/material key
- texture binding info or constant color
- transform pointer/index (frame-local lifetime)
- optional pick color/object id

You do not need a brand new scene graph; only a stricter packet format.

### 5.4.1 Deferred-Friendly Gather Contract (Future-Proofing)
- Keep gather stage lighting-model agnostic: producers emit geometry/material facts, not forward-specific lighting decisions.
- Add material semantic fields now (used incrementally): base color/albedo, normal map (optional), emissive (optional), alpha mode, two-sided.
- Reserve placeholders for roughness/metalness (or equivalent) even if current shaders ignore them.
- Add a surface class tag (`opaque`, `alpha_test`, `transparent`, `decal`, `ui`) so future deferred/forward-hybrid routing is renderer-owned.
- Ensure packet data keeps world-space normal/tangent availability flags for future G-buffer usage.
- Keep stable `objectId` and `materialId` in packets for picking/debug views and parity diagnostics.
- Avoid producer-side hardcoding of backend passes; renderer derives pass routing from packet + mode.
- Keep API neutral enough that backend can move from forward-only to deferred+forward hybrid with minimal producer edits.

## 5.5 Backend Submission Strategy
- Keep current texture-grouped VNTA fast path, but under generic queue infrastructure.
- Add generic submission path for `items` queue (currently missing).
- Sort per pass by a compact sort key (shader/material/texture/VAO) for state locality.
- Preserve existing VAO ownership by producer classes (SFile, Terrain, OglObj) for now.

## 5.6 Picking Design (Editor-Critical)
- Keep existing 24-bit packed ID style to avoid breaking selection decode logic.
- Use dedicated pick shader outputting encoded color, no texture sampling.
- Centralize packing helpers so world/terrain/activity/track item IDs are consistent.
- Keep current decode categories (`ww` domain in `handleSelection`) for compatibility.

## 5.7 Migration Strategy by Producer Type

### Phase A: Renderer Foundation (must happen first)
- Implement generic `pushItem` and draw path.
- Wire `OglObj::pushRenderItem` to queue submission.
- Ensure primitive type, texture/color, normals, line width, and matrix handling work.
- Restore selection handling in new frame path.

### Phase B: Feature Parity (core content)
- Terrain high/low + water gather and draw parity.
- Skydome pass integration.
- Port remaining world object classes to gather path (or route through shared helper wrappers).
- Re-enable track DB overlays, markers, activities/path.

### Phase C: Performance and Cleanup
- Sort keys and batch policy tuning.
- Reduce per-frame heap churn (arena/pool for transforms and temporary items).
- Remove debug spam in hot loops.
- Validate parity and retire old path when safe.

## 5.8 Practical Class Responsibilities
- Producers (`Route`, `Tile`, `Terrain`, `WorldObj` subclasses, `TDB`, activity/path systems): collect render packets only.
- `Renderer`: owns per-pass queues and matrix lifetime for the frame.
- `OpenGL3Renderer`: submits passes with state sorting.
- `GLUU`: shader registry and uniform helper, not traversal owner.

This preserves most of today’s code layout while decoupling submission.

## 5.9 Clean Switching Between Old and New `paintGL`
Instead of commenting/uncommenting `paintGL2`, use a runtime pipeline switch:
- Introduce a small interface, e.g. `IRenderPipeline::renderFrame(RenderFrameContext&)`.
- Implement two concrete pipelines:
  - `LegacyImmediatePipeline` (current `paintGL2` behavior).
  - `GatherPipeline` (current new `paintGL` behavior).
- Select with a config flag (`Game::rendererPipeline = legacy|gather|hybrid`) and optional debug hotkey.
- Add safe fallback: if gather pipeline fails init, log and fall back to legacy automatically.
- Keep shared frame setup (camera/projection/common uniforms) in one helper to avoid drift between implementations.
- Optional dev-only `hybrid` mode: render both (or render one and diff offscreen captures) for parity debugging.

---

## 6. Immediate Risks and Mitigations

Risk: selection regressions block editor usability.
- Mitigation: re-enable and test selection pass early (before full migration).

Risk: partial migration leaves invisible object categories.
- Mitigation: keep old path toggle and parity checklist per category.

Risk: matrix lifetime/pointer aliasing bugs.
- Mitigation: store transforms in frame-owned memory, avoid transient stack pointers.

Risk: OglObj gather path leaks if enqueue stays disabled.
- Mitigation: once queueing is enabled, ensure ownership rules (`shared` vs frame-owned) are explicit.

## 6.1 Testing Strategy for AI-Assisted Pipeline Migration
Use both visual and semantic checks. Visual parity alone is not enough for editor correctness.

1. Deterministic A/B frame capture
- Fix camera path, weather/season/time, and random seeds where possible.
- Render old and new pipelines from identical camera frames.
- Save color + depth + picking outputs for each frame.

2. Image-diff regression with tolerance
- Compare old/new color buffers using absolute diff and an aggregate metric (RMSE/SSIM).
- Allow small tolerances for known non-determinism (alpha edges, floating precision).
- Flag large differences and attach heatmaps for review.

3. Picking/selection parity tests
- For a screen grid of sample points, run selection in both pipelines.
- Compare decoded category/id payloads (world object, terrain patch, tr item, activity item).
- Treat mismatches as high-severity regressions for editor workflows.

4. Render coverage counters
- Log per-frame counts by category: terrain patches, world objects by type, overlays, water, UI packets.
- Compare old/new counts to detect missing producers even when screenshots look acceptable.

5. Pass and performance sanity
- Track draw calls, triangles, texture binds, and frame time by pass.
- Use thresholds to catch accidental state explosion or missing batching.

6. Basic “is anything valid” smoke checks
- Non-black/non-empty frame check.
- Depth buffer has valid range (not all 0 or all 1).
- Shader/material fallback counters are below threshold.

7. AI-agent workflow guardrails
- Require every renderer PR from agents to run a scripted camera-route capture set.
- Require artifact upload: old/new images, diffs, counters, and selection parity report.
- Keep a small curated route set (dense urban, forest-heavy, yard with many interactives) for representative coverage.

---

## 7. Parity Checklist for “New Renderer Ready”

Required before removing old path:
- All world object classes visible and selectable.
- Terrain high/low and water high/low parity.
- Track DB/road DB lines and items parity.
- Activity/path/marker overlays parity.
- Compass/HUD/pointer parity.
- Shadow map parity.
- Selection decoding parity for all `ww` categories currently used in `handleSelection`.
- No major perf regression vs old path on representative routes.

---

## 8. Recommendation Summary
- Keep this direction: gather then render is the right architecture move.
- First unblock renderer generality (`pushItem` + `OglObj` queue path + selection pass).
- Then migrate systems in vertical slices (terrain, world objects, overlays) while keeping an old-path fallback.
- Avoid broad redesign; focus on queue contract, pass separation, and parity-driven migration.

This path is realistic for TSRE and can be implemented incrementally with controlled risk.
