# Task 16 - Renderer-Owned Meshes

## Objective

Move vertex and index buffers from the producers (shapes, terrain, editor
helpers) to the renderer. Producers describe geometry with CPU data and keep
a handle; the renderer creates, uploads, binds and frees the GL objects.
This removes GL calls and the "current context" requirement from producer
code, and is the prerequisite for a renderer that is not OpenGL 3.3.

## API

`src/tsre/renderer/Mesh.h`:

```cpp
MeshData data;
data.layout = RenderItem::VNTA;          // V, VT or VNTA; floats per vertex
data.vertices.assign(points, points + count);
// Optional: data.indices (raw u16 or u32), data.indexType.

MeshHandle mesh = Meshes::create(std::move(data));   // any thread
Meshes::update(mesh, std::move(newData));            // replaces the data
packet->mesh.handle = mesh;                          // draw it
Meshes::release(mesh);                               // when done
```

- `create` and `update` take the data and need no GL context. The renderer
  uploads pending data on the GL thread the first time a packet draws the
  mesh, then drops the CPU copy.
- `release` frees the mesh and resets the handle. Buffers are deleted at the
  next frame on the GL thread; a stale handle never matches a reused slot,
  so a packet still queued with it simply draws nothing.
- `RenderItem::Mesh` keeps `first`, `count`, `indexed`, `indexOffset` and
  `baseVertex`, so one mesh can serve several packets.

## Design

- One global store (`Meshes`, mutex-protected) owns the buffers. TSRE shares
  GL contexts (`Qt::AA_ShareOpenGLContexts`), so the buffers are valid in
  every widget.
- Vertex arrays cannot be shared between contexts, so each
  `OpenGL3Renderer` keeps its own vertex array per mesh, rebuilt when the
  mesh's layout, buffers or index presence change (the store's stamp), and
  dropped when the mesh is released.
- Index uploads use the copy-write target, so they never change the index
  binding of whichever vertex array is bound.
- Packets without a handle still draw from the producer's own
  `QOpenGLVertexArrayObject`, so producers move one at a time.

## Migration

Each step is checked against baseline captures (EUROPE1, USA1, BNSF_SCENIC,
shadows off and on, Shape Viewer set) and the test suites, including
`mesh-store`.

- [x] `OglObj` (and with it its 26 users: editor helpers, procedural track,
  rulers, markers, HUD, compass). `init()` no longer needs a GL context;
  `mapBuffer`/`unmapBuffer` are gone, the compass updates its own vertices.
  Pixel-identical.
- [ ] `GltfShape`
- [ ] `SFileComplex`
- [ ] `SFileLegacy`
- [ ] `ForestObj`, `TransferObj`
- [ ] Terrain (`TerrainMeshBackend`, paged patch mesh and its uniform block)
- [ ] Remove `RenderItem::Mesh::vao`/`vbo` once no producer sets them.

Textures and framebuffers (TexLib, procedural material arrays, shadow maps)
are a separate, later step.
