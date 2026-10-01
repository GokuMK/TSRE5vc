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

## Packet ownership contract (implemented)

The renderer separates persistent packets from per-frame draw instances.

- **Packets** are `RenderItem`s owned by a producer and reused across frames.
  `Renderer::pushPackets(packets, selectionId)` queues one instance per packet
  with a copy of the current `mvMatrix` and the selection ID. The renderer never
  modifies a packet, so one packet can be queued any number of times per frame,
  including by the selection pass.
- **Retirement**: producers release packets with `Renderer::retirePacket()`,
  never `delete`. A packet is deleted at once when no renderer has queued
  packets, otherwise after the next flush. This replaces the earlier
  `cacheOwner` / `retainedPackets` protection.
- **Single packets and order**: `pushPacket(packet, selectionId, order)`
  queues one packet. `SUBMIT_GROUPED` packets are batched by texture;
  `SUBMIT_ORDERED` packets keep submission order together with frame-owned
  items, for overlays, decals and helper geometry. A null `msMatrix` means
  identity.
- **Refreshing packets**: a producer may refresh a packet's fields when it
  submits it (for example a texture that finished loading), but must not
  change them between submissions in one frame. `Renderer::frameNumber()`
  increments at every `resetFrame()` so producers can tell when a new frame
  starts.
- **Frame-owned items**: `pushItem()` remains for producers that still build
  items every frame. The renderer takes ownership (a shared item is copied),
  draws them in submission order before packets, and deletes them after the
  flush. `pushItemsVNTA()` is a wrapper for `pushPackets()`.
- **Matrices**: `mvPushMatrix()`/`mvPopMatrix()` copy into a reused stack, and
  `mvMatrix` is a stable pointer. Submitted model-view matrices are copied into
  a per-frame arena. A packet's `msMatrix` pointer must stay valid while the
  packet lives. Frame storage is cleared, not freed, between frames.
- **Order**: instances are drawn grouped by the first submission of their
  texture, then of their packet, then in submission order. The order no longer
  depends on pointer values or hash iteration. `OpenGL3Renderer::groupByTexture
  = false` draws in submission order.
- **Reset**: `Renderer::resetFrame()` drops queued work and rebalances the
  matrix stack. The gather frame calls it first; a pipeline switch also uses it.

A packet's VAO, VBO and textures are still owned by the producer. Retirement
protects the `RenderItem` only; releasing GPU resources while instances are
queued is still unsafe.

### Status

- [x] Ownership and invalidation contract for borrowed packets and frame-owned
  items, including matrix lifetimes.
- [x] Shape producers (`SFileLegacy`, `SFile`, `GltfShape`) submit packets with
  `pushPackets()`, retire them on cache rebuild and destruction, and select
  through the instance ID instead of copying packets. This removes the `SFile`
  and glTF cache leaks.
- [x] `cacheOwner` / `retainedPackets` removed.
- [x] `OglObj` keeps a small packet pool per object. Each frame a submission
  reuses a packet already queued with the same material or takes the next
  pool entry, so one object can still be drawn with several materials in a
  frame. Packets are submitted ordered, as before.
- [x] Terrain keeps one packet per patch for the surface, wireframe grid and
  map layers, refilled every frame and submitted ordered.
- [x] `SFileLegacy` animated shapes reuse per-state packets and matrix storage;
  a state submitted twice in one frame falls back to frame-owned items.
- [x] `SFileComplex` submits its persistent packets instead of copies, with
  selection on the instance, and retires them through its packet deleter.
- [ ] Animated shapes of the opt-in `old` backend (`SFile`) still build
  frame-owned items.
- [x] Packets can reference a TexLib texture resolved at draw time
  (`textureId`); `SFileLegacy` uses it, so texture streaming no longer
  rebuilds shape caches. See task 11 for passes and surfaces.

Related: [SFileLegacy matrix-reuse and ownership follow-up](../shapes/05-sfile-legacy-load-gl.md).
