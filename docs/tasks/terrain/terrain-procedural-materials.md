# Terrain procedural materials from a painted ID map

Stage B update: new global-material tiles use the
[route material library](../../features/terrain-material-library.md), not copied
local shader definitions. Earlier local-palette descriptions below document the
original demo and the retained fallback for tiles without a UiD table.

Status: stage-1 tech demo implemented; automated verification is recorded below,
and interactive acceptance is pending. Implementation was authorized after the
reviewed design was committed as `ea0c02d`.
The user's ?? review comments are incorporated below. This remains a **minimal
performance tech demo**, not a production material-system specification.
Detailed specifications and further features follow measurement.

Stage A verification update: the ACE integration passed 405 procedural CPU checks
and the OpenGL suite, plus 66 terrain-grid checks. The DXT1-bake follow-up adds
further checks; see the baked-fallback task for current
save/migration/near-far behavior and remaining interactive acceptance. Earlier
counts below describe the preceding demo milestones, not the latest total.

Load-time bake input hashing is now behind the internal
`TerrainMaterialMap::ValidateBakeOnLoad` setting, default false. Keep it for a
future debug/restore control in procedural terrain settings. Even when validation
reports stale inputs, the existing decodable bake remains a usable fallback.
Full save-time signatures are also opt-in. Normal saves use tracked dirty patches
and compact source/settings metadata; unchecked saved bakes never retain stale
validation signatures. Interactive edit invalidation remains enabled.

Generated materials now retain reduced CPU bake miniatures. Background workers
produce them before full-size CPU data is discarded; synchronous painting uses
a separate latest-request-per-patch queue for shrinking and output hashing.
Busy workers no longer cause the final edit's request to be dropped. Size comes
from the current baked tile size divided by
patch count, and save checks dimensions/recipe validity before reuse. Save-time
assembly and any missing-image generation/reduction run in the existing worker
pool, while saving still waits for completion without processing editing input.
Save drains pending edit miniatures and promotes/deduplicates existing textures
without regenerating visible patch output. No GPU readback is used.
Incremental saving updates dirty regions of the existing bake, including after
near-texture eviction. Recipe keys survive GPU eviction and only edited/halo
patches invalidate them. CPU bake images are released with tile residency and
can be read again by the save worker. Whole ID-map compression remains, at zlib
level 1 for faster saves without changing the file format. See the baked-fallback
task for checked/unchecked markers, migration, rollback and measurements.

Stage A follow-up: [baked tile fallback; B — material catalogue](terrain-procedural-baked-fallback.md).
A now adds a checked 1024-square opaque DXT1 ACE bake on save and distant
texture selection. Saved bakes are prefetched at tile load, with bounded retries
and begin-frame GPU uploads independent of patch visibility. Pending near patches
use the baked image until their generated textures are ready. A brand-new/unbaked
tile or immediate visibility before async prefetch completes can still show an
initial placeholder. B remains a design. The historical no-bake behavior is
superseded: disable retains the current successful bake; first enable reserves
material 0 and shifts source shaders/IDs to 1..255.

## Objective and selected first-stage scope

Store a compressed shader-ID bitmap instead of accumulating unique painted
patch ACE files. Generate ordinary patch textures from existing terrain shader
sources when a patch is requested for drawing. Interactive painting regenerates
affected outputs synchronously. Measure hashing, generation, compression,
upload and memory before expanding the design.

| Item | Tech-demo decision |
|---|---|
| Enable | Reserve material 0 for the bake; create a 4096 x 4096 uint8 map filled with source ID **1** (formerly 0) |
| Disable | Keep the current saved bake; unsaved procedural paint must be saved first |
| Shader references | Direct tile-local source IDs, **1..255**; picked sources can be imported; 0 is not a source |
| Shader-table changes | Painting may append a picked source shader pair; never renumber existing entries or register generated outputs |
| Saved patch shader IDs | All use baked material 0 and the corresponding tile-wide UV region; retain source palette |
| Custom source UVs | Ignore scale, rotation and offset; use default coordinates |
| Output | 512 x 512 opaque texture per patch (original baseline: 256); prefer **DXT1**, not DXT5 |
| Material cache | One **hash -> procedural material** table per terrain tile |
| Global sharing | **TexLib** shares identical output textures by content identity |
| Editing | Changed patch material is private until successful tile save |

No extra palette, general recipe language, cross-tile material cache,
shader-definition canonicalization or automatic recovery
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

First enable validates the first source, reserves material zero for the bake,
shifts the existing normal/auxiliary source pairs by one and fills the map with
ID 1. It does not reconstruct previous painted layers. An already enabled tile
is a no-op. Re-enable recognizes the saved bake marker and does not insert another
slot. Existing demo maps migrate IDs and palette together in memory before save.

Disable requires a current successful bake, clears the active map reference and
keeps the baked patch assignments/UVs. Save persists the mode change. Sources,
old ACEs and the unreferenced map remain; there is no automatic asset deletion.

Respect route/app write-disable, tile editability and texture locks. Start
with local editing; today's static texture-paint buttons are already hidden
in multiplayer. A new networking protocol is outside the tech demo.

## Direct shader IDs and brush selection

ID bytes index the tile's normal TFile::materials table directly. They are
not TexLib runtime texture IDs, auxiliary shader-table offsets or generated
output handles. **255 is a valid ID**; white brush pixels are not a reserved
map value. The procedural destination source ID must fit 1..255. A static palette tile can use a
higher source shader ID: import assigns a new local ID within the byte range.

A picked/selected texture must resolve to a **valid existing terrain shader**.
Procedural pick reads the ID under the pointer and selects that source shader,
never the generated patch composite. Static pick uses the original patch's
shader. Carry the source tile/shader identity where needed; Brush::texId alone
does not establish valid shader identity.

IDs are tile-local. When painting another tile, reuse an identical shader pair
in its first 256 entries; otherwise import the picked pair at the next free ID.
This replaces the original no-import limitation after the user's palette-tile
trial. Pick stores a value snapshot of the normal and auxiliary definitions,
including all texture references and integer arguments, so the source tile may
subsequently unload or change. No source-file copying is needed between tiles
of the same route: their shader references use the shared terrain-texture directory.
The target diffuse source is validated before appending either shader entry.
No existing IDs, patch shader references or UVs are changed; existing bitmap
IDs and cached outputs remain valid. Imported entries persist through save/reload
and remain in the palette when switching back to static rendering.

Only an actual nonwhite, unlocked paint stamp imports a missing source. Repeated
strokes reuse the imported pair. A full 256-entry destination refuses import
without replacing anything; matching existing entries can still be painted.
A texture loaded directly from a file is not a complete shader definition:
pick it from a static palette tile first if no matching target shader exists.

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

Interpret reset as runtime default coordinates in the procedural path. Stage A
saves separate tile-wide baked UVs. The old vertex-UV builder and paged parameter
builder both retain normalized near coordinates, with a small remap uniform in
the existing vertex shader selecting a baked patch region when needed. No mesh
rebuild/program switch occurs during near/far selection or worker completion.
Do not bake a source transform and apply it again to the final texture.

The initial CPU generator selects an existing shader for each output pixel
and samples its primary texture at default patch-normalized coordinates.
Picked custom UV transformations are intentionally ignored. Document the
supported shader subset: complete secondary-texture/material effects are not
being implemented implicitly. Both rendering paths bind fixed `microtex.ace`
as the second texture, using the standard `DetailTerrain` tiling factor **32**.
`TFile::newMat()` stores that float as integer bits `1107296256` (`0x42000000`).
The existing fragment shader applies this detail texture to patch-normalized
UVs; it is not baked into generated pixels. It uses the ordinary route/season
texture lookup, repeating coordinates and mipmaps, with one shared TexLib
reference per procedural tile. Missing/pending detail disables the layer until
available. Original static material descriptors remain unchanged.
Avoid applying a detail layer twice. Output is
opaque; water, gaps, lighting and shadows are not baked into its color.

Original baseline: T=N*S is physical tile width, M=2048 the ID-map side, and
O=256 output side. Each patch has K=M/P ID pixels per side. The initial demo
read IDs with nearest-neighbour sampling; the selectable generation methods
below now extend this. Never interpolate the numerical shader IDs themselves.
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

With a stable local shader table, the local key hashes the patch's ID rectangle,
sampling mode, source patch side and output side. Modes 1/2/4 additionally include
the clamped one-texel halo used by their filter; mode 3 needs no halo.
Clear/rebuild on source
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
centers independently of heightmap N. Default is 512 x 512 (reduced from 2048 for
upload-latency testing). Existing 2048 bakes are not used or resampled; save
once to regenerate them at 512 and restore normal near/far selection.

Use the familiar zlib family: qCompress/qUncompress or an MSTS-style wrapper
where convenient. [ReadFile.cpp](../../../src/tsre/fileFunctions/ReadFile.cpp)
already adapts MSTS files for Qt; do not pass a differently framed sidecar
blindly through that parser. Qt's length prefix is only a hint, not a hard
decompression limit; enforce expected size and bounded decoding.
See [Qt compression documentation](https://doc.qt.io/qt-6/qbytearray.html#qCompress).

Retain basic file checks even in a demo: known version, valid dimensions,
correct output length and usable referenced IDs. A missing/corrupt referenced
sidecar is not permission to replace it silently with zeros. The saved bake
is a render fallback, not corrupt-file recovery. Keep file references
within the intended route location.

Ordinary procedural save writes the ID bitmap, one whole-tile opaque DXT1 ACE and the
descriptor, never individual generated patch ACEs. Keep one backup per sidecar
and commit the descriptor last, restoring overwritten data on reported failure.
Preserve source definitions and unrelated fields; baked patch assignments/UVs
are intentional changes. See stage A for transaction and format details.

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

## Stage-1 implementation scope

1. Add reference/bitmap round-trip and runtime tile-local state.
2. Implement CPU generation using direct shader IDs and default UVs; verify
   local cache hits and TexLib content sharing.
3. Wire F2 toggle, pick/paint, copy-on-write and save-time dedup using existing
   renderers/texture handles, without new GLSL.
4. Benchmark P16/P32, repeated/unique patches, multiple tiles, one-patch and
   large-brush edits, uncompressed versus DXT1 output.
5. Discuss results before specifying a production format or more features.

Basic checks: enable fills source ID 1; disable requires and keeps a current bake;
reserved-zero/above-255 source selection is rejected; white/no-op
stamps do no work; cross-tile IDs are resolved correctly; generated materials
never enter shader tables; shared output is immutable; ID bytes round-trip;
painting does not rebuild height/normal meshes or save ACEs; default UVs remain
correct across R values in both mesh backends. Do not claim custom-source-UV
parity when the demo intentionally ignores those transforms.

## Deferred limitations, not demo implementation requirements

- Extra palette/wider IDs, shader deletion/remapping and general
  per-source UVs. The earlier palette proposal is not selected for stage 1.
- Reconstruct old painted layers on conversion. First enable fills source ID 1;
  stage A bakes subsequent procedural edits on save, but existing painted ACEs
  are not reconstructed into layers or automatically deleted.
- Complete shader/detail effects, source alpha, physical scale consistency,
  filtering halos/mipmap seams and sophisticated season/hot-reload policies.
- Global dedup before cross-tile synthesis. The selected design uses per-tile
  material caches and global TexLib output sharing instead.
- General redo remains separate (the global Undo API has no redo). Procedural
  undo is implemented below using IDs/toggle/palette state, not generated RGB.
- Production safe-save/export/cleanup, route merge/B replacement and networking.
  Until handled, gate incompatible actions rather than leave stale references.
- Worker queues/global cache or memory-manager redesign only if measurements
  show a need. No mandatory extra architecture before the performance trial.

Related: [terrain task index](README.md),
[heightmap/UV conventions](terrain-heightmap-resolution.md),
[paged terrain/shared maps](terrain-paged-mesh-and-shared-map.md),
[height-brush batching](terrain-height-brush-performance.md).

## Implemented demo: entry points and trial instructions

### Procedural undo follow-up

Implemented: the first real material edit to each tile in an open Undo action
deep-copies its full authoritative byte-ID plane (16 MiB). Further stamps in
that action reuse the snapshot. Painting, patch fill, flood fill, shader import,
and procedural/static toggles participate. Snapshot metadata includes complete
normal/auxiliary shader definitions, the material reference/bake marker and patch
shader IDs/UV transforms. Height/bounds/flags and generated textures are not
captured. A static-before-enable snapshot needs no ID plane.

The existing **two-second action boundary is intentional**: a long drag splits
into short undoable segments, and mouse release closes the final segment. The
procedural mouse-move handler opens a new action after the timer closes one.
One action can capture several tiles. The existing 50-action history limit stays.

At action completion, `UndoBuffer` queues zlib level-1 compression on a dedicated
**single-worker** pool. There is at most one submitted compression job; waiting
entries are weak references to history-owned snapshots, not extra map copies.
The worker owns only immutable bytes, never Terrain or the Undo stack. The
existing Undo timer publishes completed results on the main thread and releases
raw storage only if compression succeeded and shrank the payload. Until then,
Ctrl+Z reads the raw snapshot without waiting. Afterwards it decompresses it.
Clear/cancel/history eviction destroy snapshot ownership; expired queue entries
are skipped, and an already-running job may finish but cannot resurrect history.
`Undo::Clear()` also deletes the previously leaked open action.

Restore cancels old procedural generation/miniature requests, restores palette
and IDs together, preserves unaffected resident patches, and regenerates changed
patches including sampling halos synchronously. Mode/palette changes can require
all patch outputs again. The tile becomes modified and its bake is marked for
rebuild: saving since capture must not make the restored state appear clean.
Undo never writes files or rolls back a previously saved ACE immediately.
Missing/deleted/reloaded/replaced tiles, changed layouts/routes, write-protected
sessions, and outstanding save-recovery state are refused rather than applying
the snapshot to the wrong target. Source files must still be available unless
their decoded images remain cached. GPU/cache eviction alone does not expire
undo. General redo remains outside this implementation.

Alternative retained for future comparison: fixed chunks or contiguous region
deltas can lower peak capture memory (a 64x64 ID chunk is 4 KiB). They complicate
overlapping stamps/fills and were not selected for the first implementation.
Raw snapshots waiting for compression still consume 16 MiB each; compression
is not a global memory budget. Noisy maps can compress poorly.

Pre-implementation measurements on the current
`C:/trainsim/routes/procedural/tiles/-11dbfba0_materials.pmap`, Release Qt code,
two warmups and 11 measured repetitions per level, disk I/O excluded:

| zlib level | Deep clone | Compression | Clone + compression | Decompression | Compressed bytes |
|---|---:|---:|---:|---:|---:|
| 1 | 4.558 ms | 46.036 ms | 50.594 ms | 17.536 ms | 174,193 (~170 KiB) |
| 6 | 3.763 ms | 124.377 ms | 128.140 ms | 40.919 ms | 87,322 (~85 KiB) |

The raw plane is 16 MiB. These are actual deep copies, not cheap implicitly shared
`QByteArray` handles. Every decompression was checked byte-for-byte. The map was
only read; log: `build/procedural-undo-map-benchmark.log`. Results depend strongly
on map content and system load; noisy/scattered maps may compress worse. Patch
texture regeneration is excluded and is acceptable as part of Ctrl+Z according
to the user; no actual procedural undo implementation was timed.

Level-1 snapshots would use about 8.3 MiB for 50 maps of this particular content,
versus 800 MiB raw (excluding metadata and any pending raw snapshots). Synchronous
compression could cause a ~51 ms stroke-start pause; the implemented worker
avoids putting that compression on the painting thread. The explicit deep copy
still occurs once per tile/action, not per mouse-move stamp.

Implementation: `src/tsre/Undo.{h,cpp}`, `src/tsre/UndoBuffer.{h,cpp}`,
`Terrain::captureProceduralUndo()` / `TerrainMaterialUndo::restore()` in
`src/tsre/world/TerrainProceduralMaterial.cpp`. CPU and GL regression coverage
belongs to `--test --test-suite terrain-material` and `terrain-material-gl`.

Verification (2026-09-08): Release build passed; `terrain-material` passed
454 checks in each of the default BC1 and `TSRE_TERRAIN_MATERIAL_RGB=1` near-output
modes, and `terrain-material-gl` reported
zero failures. Coverage includes deep-copy isolation, raw restore while a worker
is pending, compressed restore, weak queue ownership/cleanup, no-op/locked stamps,
multi-tile overlapping strokes, two-second action boundaries, separate stroke
segments, shader import, enable/disable, save/reload, write protection, tile
reload/deletion, cache eviction, restored GPU colours and shared texture lifetime.
Logs: `build/terrain-material-undo-{cpu,rgb,gl}.log`.
Tests use temporary route fixtures and preserve the user's application log.

- [TerrainMaterialMap](../../../src/tsre/world/TerrainMaterialMap.h): CPU-only
  ID-plane codec, categorical brush, patch hashing, texture synthesis and fast
  opaque BC1 encoder. Independent of terrain height samples and OpenGL.
- [TerrainProceduralMaterial.cpp](../../../src/tsre/world/TerrainProceduralMaterial.cpp):
  per-tile state/source images, immutable cache, private edited outputs, TexLib
  registration, source picking/resolution, toggles and save finalization.
- [TerrainMaterialSource](../../../src/tsre/world/TerrainMaterialSource.h):
  owned pick snapshots, complete shader-pair identity and append-only import.
- [Terrain.cpp](../../../src/tsre/world/Terrain.cpp) and
  [TerrainMeshBackend.cpp](../../../src/tsre/world/TerrainMeshBackend.cpp):
  generated textures/default runtime UVs in both precomputed and paged meshes.
  Painting updates textures only, without height/normal/mesh invalidation.
- [TerrainMaterialTestSuite.cpp](../../../src/tsre/tests/TerrainMaterialTestSuite.cpp):
  isolated fixtures, codec/painting/cache/save tests and opt-in CPU/GPU trials.

Testing in the editor:

1. Use a disposable test route. A separate static palette tile may hold your
   source shaders: put the desired source textures on its patches and save.
   The procedural target does not need those shader entries beforehand.
2. Open F2, select **Make tile use procedural material**, then click the tile.
   It starts entirely with source ID 1 (former shader 0), not a reconstruction of static paint.
3. Pick an existing terrain source, select **Texture** in the **Procedural
   Materials** section and paint (not Texture in the Static textures section).
   Picking a procedural patch selects its source at the cursor, not the composite.
   Painting reuses or automatically imports the picked shader on each target;
   static neighbours are left untouched. Brush radius follows the existing
   texture brush convention (`size * startingPatchWorldSize / 512` metres).
4. Save and reload to test ID persistence, the whole-tile ACE bake and deduplication.
   Select **Make tile use static textures** and click to keep the current saved
   baked appearance. Unsaved procedural changes must be saved first.
5. Increase terrain visibility beyond `TerrainMaterialMap::DetailDistanceMeters`
   (default 2048 m) to test the baked far texture. The cutoff is per patch, using
   horizontal distance to its center, independent of objectlod and geometry LOD.
   Returning near reuses cached output or resumes per-visible-patch generation.
   Geometry LOD remains independent; unsaved tiles stay procedural.

The **Static textures** section contains Color, Texture and Put; these silently
do nothing on procedural tiles. **Procedural Materials** has its own Texture,
Fill Patch and Fill tools, which do nothing on static tiles. **Shared texture
tools** contains Pick, Lock and Load; the one Lock tool applies to both modes.

- Procedural **Texture** uses the brush mask/size and can reach other procedural
  tiles, retaining a single physical stamp radius. Static neighbours are ignored.
- **Fill Patch** replaces every stored ID in the clicked unlocked patch, whatever
  its existing IDs. Brush size, intensity and mask are ignored.
- **Fill** replaces the four-connected region with the same stored shader ID as
  the clicked pixel. It stops at other IDs, tile borders and locked patches;
  diagonal-only contact does not connect regions. It follows the stored ID map,
  not the scattered/generated image. Filling with the current ID is a no-op.
- Both fills execute once per click; only Texture paints while dragging. Picked
  shaders can be imported by either fill, through the same append-only path as
  procedural painting. Global write protection and editability remain enforced.

`TerrainMaterialMap::fill()` is the CPU implementation. Flood fill uses an
iterative scanline stack, with no recursion or full-size visited bitmap; IDs are
replaced as spans are scheduled. Changed patches include the sampler's halo.
Fills reuse identical private output recipes within the action, so a uniform
tile fill does not regenerate the same texture for every patch. Subsequent
painting replaces a patch's material rather than mutating that shared output.
Final global/local deduplication still happens on successful save.

Verification after tool separation/fills: **141 procedural CPU checks**, **66
terrain-grid checks**, and the GPU/UI smoke test pass. Coverage includes four-way
connectivity, randomized scanline-vs-reference comparisons, tile boundaries,
locked barriers, patch overwrite/no-op behavior, write protection, shader import
with a white brush mask, private-recipe sharing and fill save/reload. The UI test
checks all three new buttons' tool/brush signals and the single shared Lock;
the updated F2 screenshot was visually inspected. A whole 4096-square ID-plane
flood took approximately 192 ms in one CPU-only test (excluding synthesis/save);
this is not an interactive frame-time measurement.

The buttons describe procedural Undo support; painting uses two-second actions.
UV manipulation/map-to-texture tools and material removal remain refused on
procedural tiles. Height editing, water and gaps remain independent.
Do not use this prototype as an interchange/export format. Route merge is
refused before TDB/world mutations if either route contains `.pmap` sidecars or
the current route has an enabled loaded tile. This conservative demo guard also
blocks retained/unreferenced sidecars: use a separate static-only copy to merge.
B overwrite creates a fresh descriptor without the extension; old sidecars are
retained, not reused. Full shader effects, source alpha, per-source custom detail
layers (beyond the fixed runtime `microtex.ace` layer) and live source-image
reload are outside this demo; source images are refreshed by reloading terrain.

### Prototype storage and safety

`TSRE_Terrain_Material_Buffer = 100009` is registered in `TS.h`/`TS.cpp` and
serialized as an optional filename block inside `terrain_samples`. Empty/absent
means static mode; there is no second serialized enable flag. Existing AS/US and
E/N/Y/F buffers are untouched by this extension.

The `.pmap` file is `TSREPMAP` (8 bytes), little-endian uint32 version `1`,
uint32 width and height equal to `TerrainMaterialMap::Side`, then a zlib stream
of exactly `Side * Side` ID bytes. The current experimental setting is **4096**
(16,777,216 bytes), increased by the user from the original 2048 setting.
The demo still requires file dimensions to match that compile-time setting;
automatic resizing/loading of the older 2048 maps is not implemented here.
This is **not** an ordinary Y RAW file or a SIMISA wrapper.
Compression uses Qt's zlib-compatible compressor; the decoder uses existing
miniz with a fixed output allocation and requires complete, exact stream length.
Wrong version/dimensions, truncation, surplus output/trailing data, invalid IDs,
missing sources and unsafe sidecar names are refused. A missing/bad reference
stays present, disables procedural painting/saving, and displays the preserved
static fallback; it never creates replacement zero data automatically. Stage A's
normal disable tool requires a valid current bake, so corrupt-map recovery is
not silently performed by that tool.

Saved sidecars now use stable `<tile>_materials.pmap` names and a single
`<tile>_materials.pmap.bk` backup. When replacing a map, remove the previous
backup, move the current map to `.bk`, and write the new map using `QSaveFile`.
The `.t` replacement also uses `QSaveFile`. If either map writing or descriptor
saving fails, copy the backup back to the previous map filename (retain `.bk`),
restore the in-memory reference and leave edits dirty/private. With no previous
map, descriptor failure removes the newly written unreferenced map. A backup
rotation failure leaves the current map alone; a failed rollback blocks another
rotation until recovery succeeds. This handles reported I/O failures, not an
atomic two-file transaction across a process/power failure: `.bk` is retained
for manual recovery in that case. Unchanged stable maps do not rotate backups.

A currently referenced old hash-named map migrates to the stable name on save;
the old file becomes the one `.bk`. Other unreferenced historical map files are
not automatically swept from the route directory. The removed whole-map hash
was only a filename/version identifier, not an integrity check or cache key.
The independent patch-recipe and generated-output hashes below remain in use.
No per-patch generated ACEs are saved; Stage A saves one tile-wide fallback.
This does not make the older Y/F/ACE save paths a
whole-tile transaction.

As of 2026-09-07, load/enable prepares no patch output. The colour render paths
request individual visible patches after visibility/hidden-patch checks (selection
does not generate textures). Source validation/decoding still happens on load.
Painting regenerates changed patch slots plus the sampling halo synchronously;
automatic first-visibility generation remains background work. Saving never generates unseen
patches merely to finalize their material references.
The tile cache prevents repeated synthesis of identical local ID regions.
Global `terrain-proc:v1:<dimensions>:<encoding>:sha256:<RGB hash>` TexLib keys
prevent duplicate upload/storage across tiles; background jobs may repeat CPU
synthesis and encoding across tiles before global output registration. Edited
outputs remain private until successful save.
Obsolete private GPU textures are deleted in their owning GL share group,
deferred until a compatible context is current when necessary.

Whole-tile residency exit clears patch slots, recipe-cache ownership and decoded
source images, including for dirty/selected Terrain objects that must retain
editable data. The ID map, source IDs, pending private-edit markers and save state
survive, so returning to the tile reconstructs the same appearance. Release is
connected to the legacy and Gather tile-region traversal, the distant terrain
pass, and the deprecated Simple library's existing retention policy. It is not
per-patch frustum eviction. Gather still retains its Terrain/mesh objects; this
change does not re-enable its commented-out terrain-object deletion loop.

Generated Material ownership balances one TexLib reference per Material, not per
patch/cache shared_ptr. Final-owner cleanup frees CPU output and deletes/queues
GPU storage. Other tiles sharing that output retain their references. Ordinary
microtex references are returned through the existing TexLib path; this change
does not redesign global TexLib asynchronous-source lifetime/cleanup.

### Bounded background generation (2026-09-07)

Both colour render paths now request background generation. A dedicated
QThreadPool runs **at most four workers**, with **four outstanding background jobs globally**
(running, queued or awaiting collection). Repeated pending recipes within a tile
coalesce into one job. Workers generate RGB, hash output and encode BC1; they
never access Terrain/TFile objects, TexLib, OpenGL or mutable editing state.

The worker limit was increased from two to four for interactive comparison.
The outstanding-job cap, recipe/upload budgets and synchronous painting behavior
are unchanged. The earlier two-worker benchmark below remains a baseline, not a
measurement of the four-worker configuration.
The user reports that the four-worker version works well. Verification passed
169 procedural CPU checks and the OpenGL suite, which observed four active
workers and confirmed synchronous painting bypasses streaming upload limits.
Logs: `build/terrain-material-four-workers-{cpu,gl}.log`.

Jobs capture immutable, implicitly shared ID-map/source-image snapshots. Painting
detaches a shared ID map, which can temporarily cost another 16 MiB per distinct
outstanding map version at the current 4096 setting; the four-job cap also bounds
snapshot retention. Jobs do not pin Terrain objects. Painting, successful save,
release and destruction cancel pending work. Workers check cancellation between
generation stages; running work is not force-terminated midway through a loop.
Cancelled results cannot publish into caches, including after tile re-entry.

`Terrain::beginProceduralFrame()` runs once at the editor frame entry, before
legacy/Gather/validation selection. It collects completed CPU results on the UI
thread, discards expired state registrations, and resets global frame budgets.
Previously unkeyed patches are limited to **16 recipe hashes / approximately
2 ms per frame**. Generated-texture uploads are limited to **two uploads /
approximately 2 ms per frame**. The time bounds stop subsequent operations;
they cannot pre-empt a single slow driver/hash call. Reusing already uploaded
shared textures consumes no additional upload slot.

**Interactive painting is synchronous**, following the user's regression report.
Texture painting, Fill Patch and Fill cancel older pending background tile jobs and generate
all changed patch outputs during the edit, including sampling-halo dependants.
Identical changed recipes still share private outputs. No-op stamps neither
generate output nor cancel work. Painted textures upload immediately if the
tool owns a current GL context; otherwise their next draw bypasses the automatic
streaming upload budget. Painting must not wait for worker jobs or progressively
reveal its result under the two-upload cap. First-time pending patches use the
existing static-texture fallback (now the stage-A bake after a successful save;
before the first bake it remains a temporary source preview).
Geometry/picking do not wait for completion. Source validation/decoding still
happens on load or first use after source-cache release; file I/O, geometry,
microtex and ordinary texture uploads are not covered by these budgets. The
synchronous generation path serves painting and tests; save-time finalization
now promotes/deduplicates existing outputs instead of regenerating them. The
normal automatic colour-render generation uses workers. No worker-count/settings UI is added; the small limits
are named constants in TerrainProceduralMaterial.cpp.

Background verification: 168 procedural CPU checks passed in both BC1 and RGB
modes, 66 terrain-grid checks passed, and the OpenGL suite
passed, observing a peak of two workers and multiple upload frames with no more
than two uploads each. Tests cover cancelled/in-flight edits, save/unload/tile
destruction, shared ownership and identical synchronous vs
background output. Logs: `build/terrain-material-background-{cpu,gl}.log`.

Synchronous-paint correction verification: 169 procedural CPU checks and the
OpenGL suite pass. The new regression tests cover immediate paint generation,
cancelled workers not overwriting it, and eight distinct painted outputs all
uploading in one draw frame while automatic streaming remains capped. Logs:
`build/terrain-material-synchronous-paint-{cpu,gl}.log`.

Read-only benchmark on route `procedural`, tile `-11dbfba0`, P16: 256 patches,
67 distinct recipes and 63 output textures (186 patches share one recipe).
The synchronous request-all baseline averaged 1.70 s including GPU upload over
five runs. The background harness requests all patches each simulated frame;
per-frame request/collection/upload work averaged 3.44–3.72 ms, with P95
4.71–5.08 ms. Four run maxima were 5.34–7.42 ms; one run had a 36.46 ms outlier.
All output became ready progressively in 2.15–2.28 s with the harness's sleep
pacing. This measures texture work plus glFinish, not full-editor FPS, source
loading or actual frame presentation; it is not a guarantee of zero stutter.
Logs: `build/terrain-visibility-background-bench.log` and the synchronous
`build/terrain-visibility-bench.log`. The subsequent four-worker version has
positive interactive feedback; these timings are still the earlier two-worker
diagnostic, not a full-editor four-worker FPS measurement.

### Nearest visible patches first within each tile

Implemented following the user's selection of the simpler **per-tile** approach.
Before the ordinary colour patch loop, both `Terrain::render()` and
`Terrain::pushRenderItem()` call `prepareVisibleProceduralTextures()`. It collects
visible patches missing generated output or GPU upload, sorts their requests by
distance, then uses the existing bounded request/upload path in that order.
The actual row/page draw loop is unchanged, preserving VAO/UBO binding locality.

Distance uses cached patch centres and `PatchVisibility` camera-local X/Z in
metres: `(centerX-cameraX)^2 + (-centerZ-cameraZ)^2`, matching `isPatchVisible()`'s
coordinate convention. Equal distances use patch ID as a deterministic tie-break.
Physical-grid centres provide a fallback if bounds are unavailable. There are no
square roots, heightmap copies or additional terrain lookups for prioritization.

Hidden/do-not-draw and culled patches are excluded. The pass is skipped for
selection and when ordinary terrain colour is not being drawn. Already uploaded
materials need no request. Texture bindings/active unit are restored after the
prepass so renderer binding caches remain valid. Ready uploads are prioritized
alongside first-time hashes and job admission; four workers/four outstanding
jobs and the existing hash/upload budgets are unchanged.

The request list is temporary and rebuilt using the current camera position.
Active jobs finish normally; moving the camera does not cancel/restart them.
Identical recipes still share a job/output, and synchronous painting bypasses
this automatic scheduling exactly as before.

Priority is **not globally exact across tiles**. Existing camera-centred spiral
tile traversal is retained; an earlier tile can claim a slot before a closer
patch in another tile. The earlier global candidate-list proposal was not
selected: it could improve that boundary case but adds cross-tile coordination.
QThreadPool priorities alone would not fix admission order with four workers
and only four outstanding slots. No persistent priority queue was introduced.

Verification: 181 procedural CPU tests and the OpenGL suite pass. New tests
cover camera movement/tie order, P16/P32 and larger physical tiles, hidden flags,
frustum/distance culling, recipe sharing, nearest-first readiness/uploads and
restoration of texture-unit bindings. P32 candidate filtering/sorting averaged
0.042 ms over 200 calls with all 1024 patch outputs missing (CPU-only fixture);
this is not a full-frame rendering benchmark. Logs:
`build/terrain-material-nearest-{cpu,gl}.log`. Interactive confirmation remains
to be done for this ordering change.

Earlier lazy-generation verification (2026-09-07): Release build succeeded; 158 procedural CPU checks,
66 terrain-grid checks and the OpenGL suite passed (AMD Custom GPU 0932).
Lifecycle coverage includes zero generated slots on load/enable, one-slot first
request, paint invalidation without generation, shared-reference counts, final
CPU-only/GPU cleanup, deferred GL-context deletion, five release/re-entry cycles,
and dirty release/regeneration/save/reload without pixel changes. Logs are in
`build/terrain-material-lazy-{cpu,gl,grid}.log`. Runtime camera-turn smoothness and
actual long-route traversal still need interactive verification; these unit/GL
checks do not constitute that performance test.

Generated textures use clamp-to-edge and no mipmaps in this trial. RGB output is
opaque. BC1 uses a fast actual-color endpoint fit, not a production-quality
offline compressor. Compare its appearance against the RGB reference mode.

The current local output setting is **512 x 512**, from
`TerrainMaterialMap::OutputSide`; the initial 256-sized design/memory tables and
recorded benchmark results above/below describe the earlier baseline. Tests
derive map coordinates and output midpoints from the current dimensions.

### Selectable categorical sampling experiment

Change **`TerrainMaterialMap::SamplingMode`** in `TerrainMaterialMap.h` to
**1, 2, 3 or 4**, rebuild and restart/reload the route. The selected default is
now **2 (random scattering)** following interactive comparison. All four
methods generate RGB by choosing exactly one source material per output pixel.
They do not blend source colours or change the stored IDs/brush mask.

| Mode | Selection rule |
|---|---|
| 1: strongest | Read the four surrounding ID texels, sum their bilinear distance weights by ID, and choose the largest `support * (ID + 1)`. Higher ID wins equal scores. |
| 2: scattering | Use those summed distance weights as probabilities. Select one ID using deterministic integer-hash noise; importance does not bias this mode. |
| 3: original | Nearest-neighbour selection, matching the previous generator. |
| 4: strongest support | Choose the largest summed distance support, without an importance multiplier. Higher ID wins only when support is exactly equal. |

For mode 1, `(ID + 1)` is deliberately a provisional, active strength rule,
not only a tie-breaker. High IDs can noticeably expand into low-ID regions;
zero-support IDs can never win. `materialImportance()` in `TerrainMaterialMap.cpp`
isolates this policy for future shader properties. No `mixing_type` or importance
fields are serialized yet. Thin features/corners may change shape; these modes
are visual experiments, not reconstruction of lost subpixel geometry.

Mode 2 hashes patch-local output pixel coordinates with fixed constants. It is
independent of time, generation order and mutable RNG state, and repeats its
pattern on identical patch recipes so deduplication remains valid. Save/reload
does not change the pattern. Cache keys distinguish all four modes.

Modes 1/2/4 read neighbouring patches' ID texels within the same tile. Their cache
keys include a one-texel halo, and painting invalidates every dependent edge or
corner patch. Texture locks still protect stored IDs, but a locked neighbour's
generated edge may refresh when an unlocked adjacent texel changes. At the
outer tile boundary sampling clamps to that tile: cross-tile palette-aware
filtering is not implemented. Mode 3 keeps the original patch-only dependencies.

The initial three-mode verification passed **99 CPU checks**, covering those selection rules,
probability distribution/determinism, no invented IDs or blended source pixels,
halo invalidation/cache identity, and the existing persistence/import tests.
The GPU smoke test also passed with the default strongest mode. The CPU suite
writes diagonal-boundary previews to `build/terrain-material-sampling-1.png`,
`-2.png`, `-3.png`, and now `-4.png` for visual comparison.

The mode-4 follow-up passes **108 CPU checks** and the GPU smoke test with mode
4 selected. Added tests confirm summed support, higher-ID selection on an exact
tie only, no priority on near-ties, and no influence from ID magnitude. Halo,
cache-key, deterministic unblended generation and boundary tests cover mode 4
as well as the original three modes.

A five-warm-run, synthesis-only comparison on one P32 diagonal patch with a
4096 ID map and 512 output measured means of 9.59 ms (strongest), 9.79 ms
(scattering), and 6.36 ms (nearest). These desktop timings exclude hashing,
compression, upload and editor input handling; do not treat them as frame times.

The no-blending rule applies to CPU synthesis. Existing BC1 compression and
GPU texture filtering can still interpolate colours; use the RGB reference
option when distinguishing compression artefacts from the sampling method.

### Reproducible tests and profiling

Run from the repository root with the matching Qt runtime/plugins available:

```powershell
& .\build\TSRE5vc.exe --test --test-suite terrain-material --test-verbose
& .\build\TSRE5vc.exe --test --test-suite terrain-material-benchmark
& .\build\TSRE5vc.exe --test --test-suite terrain-material-gl
```

The normal suite needs no OpenGL context. The opt-in GL suite uses a hidden
offscreen surface and reports the actual renderer, upload/readback/sharing checks
and F2 layout checks; it does not measure editor FPS or a full terrain render.
The CPU benchmark reports 100 measured patch runs after five warm-ups, mean,
median/P95, repeated recipes and worst-case unique P16/P32 maps, including a
second concurrently loaded tile sharing the first tile's outputs.

```powershell
$env:TSRE_TERRAIN_MATERIAL_PROFILE = '1' # generation/hash/encoding/upload log
& .\build\TSRE5vc.exe
Remove-Item Env:TSRE_TERRAIN_MATERIAL_PROFILE
$env:TSRE_TERRAIN_MATERIAL_RGB = '1'     # compare uncompressed RGB in a new process
& .\build\TSRE5vc.exe --test --test-suite terrain-material-benchmark
Remove-Item Env:TSRE_TERRAIN_MATERIAL_RGB
```

BC1 is the default; unavailable GPU S3TC support falls back to CPU decoding and
ordinary RGB upload through the existing texture loader. Generated GPU payload
is 32 KiB per unique BC1 patch versus nominal 192 KiB RGB; driver RGB allocations
can be padded. Benchmark payload sums are not peak process/driver memory.

Interactive brush feel, detailed visual comparison on real source textures and
peak process/driver memory still need the user's trial before production design.

### Initial verification and measurements (2026-09-05)

Follow-up (2026-09-06): automatic picked-shader import is implemented and the
expanded CPU suite passes **65 checks**. These cover shader snapshots surviving
palette mutation/unload, normal/auxiliary pair import, source IDs above 255
remapped into destination byte IDs, repeated-stroke reuse, save/reload, slot 255,
full-palette refusal, and no import for white, locked or write-disabled strokes.
The GPU smoke test and all **66 terrain-grid checks** passed again. Pick from
the palette tile, then paint directly on a procedural tile; no static-mode
round trip or pre-population of the destination palette is needed.

Release/O3 MinGW build succeeded. The final CPU suite passed **49 checks**,
including full static-descriptor byte preservation and different shader IDs in
source/target tiles. Existing terrain-grid **66**, brush **360**, edge **52** and
normal **132** checks passed (the last compares 16,899,906 vertex normals).
The hidden-surface GPU smoke test passed on **AMD Custom GPU 0932**, confirmed
compressed 32,768-byte outputs, readback color, sharing/private replacement,
texture release and F2 button fit. The panel image was inspected as well.
Neither this test nor CPU profiling constitutes an interactive terrain-render
FPS/visual acceptance test; those remain pending.

2026-09-06 fixed-detail follow-up: the GPU smoke test also verifies the standard
`microtex.ace`/32 defaults, pending-source fallback, cross-tile detail sharing,
mipmapped/repeating upload, preserved primary binding, stable reference counts
across repeated patch requests, release on disable, and gather-item detail-state
copying. It passes on AMD Custom GPU 0932; the procedural CPU suite still passes
all 65 checks. Interactive detail appearance remains a manual acceptance check.

The subsequent single-backup revision passes **75 procedural CPU checks** with
`OutputSide=512`, including stable naming, bounded file count across repeated
saves, replacement of the one backup, map-write and descriptor-write rollback,
blocked-backup refusal, retry, and legacy hash-name migration. The GPU smoke
test also passes at 512 (131,072-byte BC1 base level). This supersedes the earlier
65-check count, not the historical performance measurements below.

CPU patch operations, 100 measured runs after five warm-ups, milliseconds:

| Layout / operation | Mean | Median | P95 |
|---|---:|---:|---:|
| P16 generation | 1.482 | 1.282 | 2.260 |
| P16 RGB SHA256 | 1.613 | 1.428 | 2.263 |
| P16 BC1 encoding | 0.918 | 0.796 | 1.384 |
| P32 generation | 2.431 | 1.751 | 3.976 |
| P32 RGB SHA256 | 1.965 | 1.668 | 5.741 |
| P32 BC1 encoding | 1.221 | 1.025 | 2.253 |

These are ordinary-desktop wall-clock samples, not isolated CPU measurements.
The separate RGB-reference process measured P32 generation/hash/BC1 means of
1.311/1.468/0.796 ms, showing substantial run-to-run background-load variance.
Do not attribute that variation to output encoding: the microbenchmark executes
all three CPU operations in either process, while tile preparation uses the
selected encoding.

Production preparation of all-unique categorical-noise patches (one recorded
tile pair per mode/layout, no GPU upload), including map read/source decode:

| Output / P | First tile | Concurrent identical second tile | Total generated payload, shared by both |
|---|---:|---:|---:|
| BC1 / 16 | 1.096 s | 0.795 s | 8 MiB |
| BC1 / 32 | 4.175 s | 3.695 s | 32 MiB |
| RGB / 16 | 0.798 s | 0.766 s | 48 MiB |
| RGB / 32 | 3.166 s | 2.891 s | 192 MiB |

Add a 4 MiB ID plane per tile and source images/heightmaps/cache records to those
payloads. Uniform tiles prepared in roughly 36–44 ms, generating one texture;
a 77-recipe pattern took 134–249 ms for local hash/generation-only preparation.
The worst-case multi-second load is a real demo limitation. Global sharing
eliminates duplicate texture storage/encoding, not the second tile's generation
and RGB hashing. Interactive dirty-patch responsiveness is the next acceptance
check; do not add worker/cache architecture solely from these noisy load timings.
