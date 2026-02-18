# Task 02 - Renderer Core Generic Queue

## Objective
Make the new renderer submit generic render items, not only VNTA grouped shape items.

## Scope
- Implement `OpenGL3Renderer::pushItem`.
- Implement generic draw submission for `items` queue in `renderFrame`.
- Wire `OglObj::pushRenderItem` to actually enqueue items.
- Keep existing VNTA grouped fast path working.

## Suggested Touch Points
- `src/tsre/renderer/OpenGL3Renderer.cpp`
- `src/tsre/renderer/Renderer.cpp`
- `src/tsre/ogl/OglObj.cpp`
- `src/tsre/renderer/RenderItem.h`

## Requirements
- Respect `itemType` (`GL_TRIANGLES`, `GL_LINES`), `vertexAttr`, texture/color state, and line width.
- Handle transform lifetime safely (no transient pointer aliasing bugs).
- Keep ownership rules explicit (`shared` vs frame-owned items).

## Acceptance Criteria
- Terrain items collected via `pushItem` are visible.
- OglObj items submitted through `pushRenderItem` become visible.
- No obvious memory leaks/crashes from render item lifetime.

## Out Of Scope
- Selection parity logic and pass restoration (next task).
