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

## Temporary cached-item lifetime protection

`RenderItem::cacheOwner` and `OpenGL3Renderer::retainedPackets` are an interim
compatibility mechanism introduced with SFileLegacy cache cleanup, not the final
renderer ownership design.

The existing submission paths have different ownership rules:

- `pushItem` takes ownership of non-shared items. For shared items it creates a
  frame-owned copy. Frame cleanup deletes the queued objects.
- `pushItemsVNTA` borrows shape-cached items for batching. Clearing its frame queue
  does not delete those cached objects.

SFileLegacy now owns its cached items and releases them on invalidation. Some may
already be queued for the current frame. The optional weak `cacheOwner` lets the
renderer retain a strong reference in `retainedPackets` until frame cleanup, avoiding
use-after-free when the shape releases its cache. This retains the RenderItem only:
its referenced matrices, VAO and VBO still require their owners to remain valid
through drawing. Producers without `cacheOwner` receive no additional protection.

GltfShape currently clears raw-pointer caches without deleting their RenderItems;
its borrowed VNTA submission does not transfer ownership to the renderer. That cache
leak is an outstanding issue, not an alternative lifetime solution.

### Ownership follow-up

User direction: keep the `pushItemsVNTA` model of borrowing persistent, reusable
shape-owned items as the target, to preserve batching and avoid per-frame item
allocation/copying. The renderer task must define safe update, invalidation and
retirement boundaries around that model. This redesign is outside the current
shape task; keep the interim protection until the renderer work replaces it safely.

- [ ] Define a consistent ownership and invalidation contract for cached and
  frame-owned submissions, including referenced matrix and GPU-resource lifetimes.
- [ ] Migrate shape producers, including glTF, so cache replacement and destruction
  neither leak items nor invalidate queued work. Preserve the preferred borrowed,
  reusable-item model; determine its cache handles or frame-boundary retirement
  rules within the renderer task.
- [ ] Replace the temporary `cacheOwner` / `retainedPackets` mechanism once the new
  contract provides equivalent protection; do not simply remove it while queues
  still borrow items that can be deleted mid-frame.
- [ ] Verify invalidation after submission, multiple instances/states, reload and
  destruction boundaries, and steady-state allocation overhead.

Related: [SFileLegacy matrix-reuse and ownership follow-up](../shapes/05-sfile-legacy-load-gl.md).
Animation matrices should use persistent per-state storage updated before submission;
that allocation improvement is separate from cached RenderItem ownership.
