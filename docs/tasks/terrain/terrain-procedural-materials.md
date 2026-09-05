# Terrain procedural materials from a painted ID map

Status: design/research only; implementation is NOT authorized.
The user's ?? review comments are incorporated below. Stage 1 is a **minimal
performance tech demo**, not a production material-system specification.
Detailed specifications follow measurement. No TSRE code changes until the
user explicitly approves implementation.

## Objective and selected first-stage scope

Store a compressed shader-ID bitmap instead of accumulating unique painted
patch ACE files. Generate ordinary patch textures from existing terrain shader
sources on load and after painting. Measure hashing, generation, compression,
upload and memory before expanding the design.

| Item | Tech-demo decision |
|---|---|
| Enable | Create a 2048 x 2048 uint8 map filled with shader ID **0** |
| Disable | Restore the existing static patch materials; **no baking** |
| Shader references | Direct existing tile-local terrain shader IDs, **0..255** |
| Shader-table changes | None from procedural conversion/painting; generated materials never enter it |
| Original patch shader IDs | Preserve them for return to static rendering |
| Custom source UVs | Ignore scale, rotation and offset; use default coordinates |
| Output | 256 x 256 opaque texture per patch; prefer **DXT1**, not DXT5 |
| Material cache | One **hash -> procedural material** table per terrain tile |
| Global sharing | **TexLib** shares identical output textures by content identity |
| Editing | Changed patch material is private until successful tile save |

No extra palette, general recipe language, cross-tile material cache,
shader-definition canonicalization, static bake/export or automatic recovery
of old paint layers in this stage. Small duplicated material records per tile
are acceptable when the underlying texture storage is shared.

## F2 tools and mode switching

In [TerrainTools.cpp](../../../src/routeEditor/TerrainTools.cpp), add section
**Procedural Materials** and two checkable tool buttons:

- **Make tile use procedural material**
- **Make tile use static textures**

Reuse buttonTools / enableTool and current-tool feedback. The button activates
a tool; a subsequent terrain click applies it to the containing terrain tile,
not one patch or World file. Do not repeatedly convert on mouse movement.
Keep controls in the toolbox, not inline in the GL key handler.

First enable validates shader 0, then fills the ID map with zero. It does not
attempt to preserve the previous appearance in the map. An already enabled
tile is a no-op, not a map reset. If shader 0 has no usable source, report the
problem rather than generating from a missing texture or creating a new shader.

Disable removes the active procedural reference and restores the static path.
The shader arrays and original patch shader IDs have not been changed. If a
static patch has a missing shader, try shader 0 as requested; if that is also
unusable, use missing-texture feedback rather than an invalid dereference.
This fallback is not permission to rewrite the whole shader table.

Keep original ACE assets. No automatic baking or asset deletion. The demo
need not delete an old sidecar when disabled; a new enable starts at zero.
Clearly state that disabling does not preserve procedural paint as static
appearance. Follow ordinary route-save behavior, not immediate disk writes
when a tool is clicked.

Respect route/app write-disable, tile editability and texture locks. Start
with local editing; today's static texture-paint buttons are already hidden
in multiplayer. A new networking protocol is outside the tech demo.

## Direct shader IDs and brush selection

ID bytes index the tile's normal TFile::materials table directly. They are
not TexLib runtime texture IDs, auxiliary shader-table offsets or generated
output handles. **255 is a valid ID**; white brush pixels are not a reserved
map value. Shaders above 255 are not selectable/applicable for procedural
painting. The tile may still contain such entries for its static materials.

A picked/selected texture must resolve to a **valid existing terrain shader**.
Procedural pick reads the ID under the pointer and selects that source shader,
never the generated patch composite. Static pick uses the original patch's
shader. Carry the source tile/shader identity where needed; Brush::texId alone
does not establish valid shader identity.

IDs are tile-local. When painting another tile, find an existing applicable
shader there for the selected source. Do not blindly copy its numeric ID or
create/import shader definitions in the demo. If no valid match exists in
0..255, report that the source cannot be applied. Prepare source shaders with
the existing static tools before enabling procedural mode. Better import and
source-selection workflows are later work.

Generated procedural material objects are runtime records with texture handles.
**They never enter materials/amaterials and never replace the original patch
shader-index fields in the descriptor.**

Painting writes IDs directly:

- grayscale brush-mask value 255: no change;
- every nonwhite value: full ID replacement, no opacity blending;
- white source-texture pixels remain valid colors;
- an unchanged stamp causes no clone, generation or dirty marking.

Read masks with correct image row stride. Reuse the current brush-size feel
initially, converting its physical rectangle once into intersecting tile/map
rectangles. Do not use heightmap N as ID-map resolution. Collect dirty patches
and regenerate each at most once per event, including large brushes.

Keep existing behavior on static tiles. Gate unsupported Color, Put/rotate/
crop and modifier-assisted operations on procedural tiles rather than letting
them modify preserved static materials accidentally. TFile::removeMat()
renumbers shader entries; block such table mutations while procedural mode is
active in the demo. Full map-ID remapping is a future feature.

## Default UVs and the first generator

Ignore custom scale, rotation and offset. Reset the **effective procedural
coordinates** to TSRE's existing default once-per-patch mapping. Use actual
R=N/P, not a fixed-16 domain. No new GLSL program or general per-pixel shader
parameter system is needed.

Interpret reset as runtime default coordinates in the procedural path while
leaving stored original static UV fields intact. This preserves the previous
static appearance on disable, including after save/reload. The old vertex-UV
builder and paged UV-parameter builder can choose the default without changing
GLSL. Do not bake the source transform and apply it again to the final texture.

The initial CPU generator selects an existing shader for each output pixel
and samples its primary texture at default patch-normalized coordinates.
Picked custom UV transformations are intentionally ignored. Document the
supported shader subset: complete secondary-texture/material effects are not
being implemented implicitly. Avoid applying a detail layer twice. Output is
opaque; water, gaps, lighting and shadows are not baked into its color.

Let T=N*S be physical tile width, M=2048 the ID-map side, and O=256 output side.
Each patch has K=M/P ID pixels per side. Read IDs with nearest-neighbor
categorical sampling at output pixel centers, never interpolation between IDs.
Source color filtering is separate from categorical ID painting.

| P | ID pixels per patch side | Generated output side |
|---:|---:|---:|
| 4 | 512 | 256 |
| 8 | 256 | 256 |
| 16 | 128 | 256 |
| 32 | 64 | 256 |

The ID-map spacing is 1 m for a 2048 m tile and 2 m for a 4096 m tile,
independent of terrain heightmap resolution. World files remain independent
2048 m cells. These are pixels, not vertices: no N+1 border row is persisted.
Require M divisible by P for demo enable without narrowing static tile support.

P4 can lose small features when downsampling; P16/P32 upsample categorical
areas. Border filtering, mipmaps and consistent physical source scale across
different patch sizes are observations to record, not a new halo/world-phase
subsystem required before the first benchmark.

## Per-tile material cache, global TexLib texture sharing

Each terrain owns its own hash table of lightweight procedural materials.
A patch references an immutable local material or a private edited material.
Duplicate material objects in different tiles are acceptable; only the
underlying textures must be globally shared.

With a stable local shader table and fixed sampling settings, the local key
can simply hash the patch's ID rectangle. Include dimensions/settings if
variable, or clear the local cache when they change. Clear/rebuild on source
texture or season changes too; the same bitmap must not reuse stale output.
No per-pixel revision scheme or global recipe canonicalization is needed.

Before generation, query the local table. On a hit, reuse its material.
On a miss, generate output, then use its content identity for TexLib sharing:

1. Local ID-map hash -> tile-local procedural material.
2. Generated pixel hash plus dimensions/format -> global TexLib texture.

The glTF precedent is real: in
[GltfShape.cpp](../../../src/tsre/shape/GltfShape.cpp), registerEmbeddedImage
uses QCryptographicHash::Sha256 over encoded image bytes, builds a namespaced
identity and registers a Texture through TexLib::addTex(). This is a reusable
pattern, not an existing general-purpose generated-pixel hashing helper.

For procedural output hash canonical RGB bytes plus dimensions/representation.
Use a non-file namespace such as proc:... and query TexLib before encoding/
upload where possible. Another tile's identical output then reuses its texture.
Different local IDs that produce the same pixels share globally; identical
ID bytes which mean different sources in different tiles do not share by
mistake. Existing TexLib identity searches are linear: measure before redesign.

This intentionally permits a first duplicate **CPU generation between tiles**:
its output identity is known after synthesis. Repeated input patterns inside
one tile generate once. Global output sharing avoids duplicate stored/uploaded
textures, not necessarily all cross-tile computation. Keep that distinction
explicit in benchmark results.

Follow texture reference ownership and clear local handles safely on route/
TexLib reset. Generated handles must never be treated as writable ACE paths.

### Copy-on-write and save

First actual ID change detaches the patch into a private material, retained
through subsequent strokes until tile save. The clone can be logical:
regenerate from the changed IDs rather than blindly call TexLib::cloneTex(),
whose current Texture copy constructor can read pixels back from the GPU.

On successful save, check the local ID hash, reuse/add its immutable material,
deduplicate output through TexLib and release the private reference. Failed
save retains dirty/private state. Other patches/tiles sharing old output must
not see in-place changes. No-op stamps must not allocate a private material.

## Minimal descriptor and compressed bitmap

One optional reference under terrain_samples enables procedural mode;
absence means static. Conceptual name: TsreProceduralMaterialBuffer.
Exact token/suffix are provisional until implementation checks
[TS.h](../../../src/tsre/fileFunctions/TS.h) and
[TS.cpp](../../../src/tsre/fileFunctions/TS.cpp). Do not repurpose E/N/F/AS/US.

Sidecar: small versioned dimensions/encoding header and compressed row-major
uint8 IDs. **No palette or recipe metadata.** Define Z orientation and pixel
centers independently of heightmap N. Default is 2048 x 2048.

Use the familiar zlib family: qCompress/qUncompress or an MSTS-style wrapper
where convenient. [ReadFile.cpp](../../../src/tsre/fileFunctions/ReadFile.cpp)
already adapts MSTS files for Qt; do not pass a differently framed sidecar
blindly through that parser. Qt's length prefix is only a hint, not a hard
decompression limit; enforce expected size and bounded decoding.
See [Qt compression documentation](https://doc.qt.io/qt-6/qbytearray.html#qCompress).

Retain basic file checks even in a demo: known version, valid dimensions,
correct output length and usable referenced IDs. A missing/corrupt referenced
sidecar is not permission to replace it silently with zeros. Shader-0 fallback
during disable is separate from corrupt-file recovery. Keep file references
within the intended route location.

Ordinary procedural save writes the ID bitmap and descriptor reference, never
generated patch ACEs. Prepare data before publishing a new reference; preserve
a recoverable previous pair and retain dirty state on failure. No generic
transaction framework is required for the demo. Verify unchanged static shader
definitions, patch references and UVs through save/reload.

Mark as TSRE experimental data. Other engines/editors are not assumed to
understand or preserve the token. Use backed-up test routes. A stable public
format and conversion/export compatibility follow measurements.

## DXT1 and measurements

No alpha is required: select opaque **DXT1/BC1 RGB**, not DXT5. Current texture
code can decode/upload DXT1, but an encoder for newly generated pixels still
needs selection and timing. Decoding support is not proof of fast encoding.
Retain uncompressed generation as a reference and hardware/performance fallback.

DXT1 uses 8 bytes per 4 x 4 block, so a 256 x 256 base level is **32 KiB**.
See the [Khronos S3TC specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_compression_s3tc.txt).

| All outputs unique, base levels only | P16 tile | P32 tile |
|---|---:|---:|
| RGB8 nominal output payload | 48 MiB | 192 MiB |
| RGBA8 staging/reference | 64 MiB | 256 MiB |
| DXT1 output | 8 MiB | 32 MiB |
| Uncompressed ID plane | 4 MiB | 4 MiB |

Mipmaps, source images, private edits and staging add memory beyond this table.
Large uniform ID areas should compress strongly; measure real/random maps too.
Compress the map on save, not during painting. Shared textures save storage,
but each tile may still do some duplicate synthesis.

Measure local hashing/lookup, generation, output hashing/TexLib lookup, DXT1
encoding, upload and save/dedup separately. If DXT1 encoding is too slow for
painting, compare uncompressed private edits with compression at save. Report
repeated-run mean, median, P95 and peak memory, not just FPS.

## Small implementation plan, only after approval

1. Add reference/bitmap round-trip and runtime tile-local state.
2. Implement CPU generation using direct shader IDs and default UVs; verify
   local cache hits and TexLib content sharing.
3. Wire F2 toggle, pick/paint, copy-on-write and save-time dedup using existing
   renderers/texture handles, without new GLSL.
4. Benchmark P16/P32, repeated/unique patches, multiple tiles, one-patch and
   large-brush edits, uncompressed versus DXT1 output.
5. Discuss results before specifying a production format or more features.

Basic checks: enable fills zero; disable restores static state; missing static
shader falls back to zero; invalid/above-255 selection is rejected; white/no-op
stamps do no work; cross-tile IDs are resolved correctly; generated materials
never enter shader tables; shared output is immutable; ID bytes round-trip;
painting does not rebuild height/normal meshes or save ACEs; default UVs remain
correct across R values in both mesh backends. Do not claim custom-source-UV
parity when the demo intentionally ignores those transforms.

## Deferred limitations, not demo implementation requirements

- Extra palette/wider IDs, source import, shader deletion/remapping and general
  per-source UVs. The earlier palette proposal is not selected for stage 1.
- Preserve/bake current appearance on conversion. First enable fills zero;
  disable restores static sources. Existing painted ACEs are not reconstructed
  into old layers or automatically deleted.
- Complete shader/detail effects, source alpha, physical scale consistency,
  filtering halos/mipmap seams and sophisticated season/hot-reload policies.
- Global dedup before cross-tile synthesis. The selected design uses per-tile
  material caches and global TexLib output sharing instead.
- Production undo: authoritative state is IDs/toggle state, not generated RGB.
  Do not let existing RGB-only undo silently alter procedural output; if undo
  is not integrated into the demo, explicitly disable it for these operations.
- Production safe-save/export/cleanup, route merge/B replacement and networking.
  Until handled, gate incompatible actions rather than leave stale references.
- Worker queues/global cache or memory-manager redesign only if measurements
  show a need. No mandatory extra architecture before the performance trial.

Related: [terrain task index](README.md),
[heightmap/UV conventions](terrain-heightmap-resolution.md),
[paged terrain/shared maps](terrain-paged-mesh-and-shared-map.md),
[height-brush batching](terrain-height-brush-performance.md).
