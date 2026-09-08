# Procedural terrain: A — baked tile fallback; B — material catalogue

Status: **stage A implementation authorized and implemented; verification below**.
Stage B remains a design only. The user approved a small checked RGB ACE writer
addition for A; the future replacement ACE class is not a prerequisite.

2026-09-08 ACE integration update: the replacement is now implemented.
Baking now uses the new ACE document API with opaque DXT1 output (follow-up below);
CPU source/baked-file loading uses `AceLib::load` without mip staging.

Compatibility finding: TSRE v0.7.620 rejects these valid no-mipmap DXT1 bakes
because its legacy reader assumes a full mip-offset table. Older unchecked
readers may display plausible but shifted block data. This is documented as a
[legacy reader bug](../../features/ace-library.md#legacy-tsre-bug-dxt1-ace-without-mipmaps),
including reproduction details and the measured cost of the optional full-mip
workaround. Updating the reader/build is the proper fix; bakes remain mip-free.
The temporary `saveRgbChecked` exists only in `AceLibLegacy`. See the
[ACE library API](../../features/ace-library.md). Earlier verification and
interactive-lag observations below remain historical evidence, not new timing
claims for the replacement.

Related: [current procedural demo](terrain-procedural-materials.md),
[patch count](terrain-patch-count.md), [heightmap resolution](terrain-heightmap-resolution.md),
[paged renderer](terrain-paged-mesh-and-shared-map.md),
[discrete geometry LOD](terrain-basic-discrete-lod.md).

## A: requested outcome

Pre-DXT1 detail-distance milestone: **398 CPU checks, 0 failures**, and the
offscreen OpenGL suite passes. Six loaded tiles exercise saturation/retry beyond
the four-job cap without rendering; baked GPU uploads occur before visible-patch
requests, preserve texture binding state, share identical outputs and release on
final ownership loss. Tests cover validation off/on, mismatch detection and
continued use of a stale bake without forced distant-patch generation.

**Historical tile-entry issue / ACE handoff:** the user observed comparable,
though smaller, tile-entry lag with the 512 bake. Removing baked tile textures
from disk eliminates the lag while staged procedural patch generation remains
visible. Disabling bake mipmaps did not solve it; they remain disabled. Isolated
offscreen upload calls measured 30–53 ms at 2048 and 0.42–0.46 ms at 512, but this
does **not** explain the full interactive delay or establish its remaining cause.
Follow-up diagnosis isolated synchronous bake validation: SHA-256 of the complete
16 MiB ID map took roughly 120–150 ms per actual route tile. Missing bake files
skip that check; smaller bake images do not shrink the ID map. Normal tile load
now trusts the saved bake. `TerrainMaterialMap::ValidateBakeOnLoad` is an internal
debug/restore setting, default **false**, retained for a future editable procedural
terrain settings section (no GUI/settings.txt entry yet). Enabling it restores
the full input/signature check and marks mismatches for rebaking on save, but a
stale, decodable bake remains available for pending patches and distant terrain.
Unsaved interactive painting still uses procedural output. Normal saves now use
tracked patch edits and small source/settings metadata, not full-map hashing.
Explicit validation/repair mode hashes inputs and establishes checked signatures.
Missing/invalid-file checks remain
enabled regardless of this setting. The user confirmed that disabling load-time
validation resolved the lag. The original checked RGB writer and original reader
are retained in `AceLibLegacy`; production now uses the new API described above,
including CPU-only loading for incremental-save bake reads. No background
resizing of old bakes.

On saving a procedural terrain tile, generate one opaque **1024 x 1024 DXT1 ACE**
texture covering the complete physical terrain tile. Every patch uses the same
ordinary terrain material and a different part of that image, as with Make
Texture From Map. This provides a standard-texture fallback for legacy readers
and a precomputed texture that TSRE can use farther from the camera.

Disabling procedural materials would retain these patch assignments and their
baked texture. It would **not restore the pre-procedural static appearance**.
This supersedes the initial demo's restore-original-static disable behavior.
Keep original source assets; A does not authorize deleting a route's old ACEs.

The texture covers a terrain tile, not a 2048 m World-file cell. Larger terrain
tiles still receive one image; W-file ownership and coordinates remain unchanged.
Keep the existing 4096-square ID map and 512-square near-patch output settings
independent from the now 1024-square baked output setting. The bake was initially
2048-square; it was reduced to 512 after isolated non-mipmapped uploads took
30–53 ms, then increased to 1024 after DXT1 baking was implemented.
Old 512/2048 bakes are not resampled or used: the loader requires the current size.
The loader rejects old-size bakes: save the tile once to generate
the new 1024 ACE and restore normal near/far selection. No route files are changed
merely by loading them. The runtime upload is 3 MiB RGB;
near patch quality and the stored ID map are unchanged. The coarse fallback and
legacy-reader appearance lose detail; verify this tradeoff interactively.

## Can A precede B?

**Yes, with one distinction: one draw material is not one total shader entry.**
Today's bitmap bytes directly index the tile's `TFile::materials` source palette.
Deleting that palette would destroy procedural regeneration and picking. For A,
retain those source definitions and reserve normal material **0**, with its
matching auxiliary material, for the dedicated baked shader pair;
all patch ShaderIndex fields point to that draw material when serialized.

Never offer/import the baked composite
as a procedural source. Keep source resolution behind the existing source-loader
boundary so B can change where definitions come from without changing baking.
B's global catalogue is not required for this.

The selected conversion happens when making a tile procedural: insert the bake
at material 0 and shift the old source palette by one. Keep normal/auxiliary
pairs and every affected shader index consistent. Procedural source IDs are now
1..255; initializing the map uses source 1 (the former source 0), not the baked
composite. Import, picking and source validation must exclude 0. Do not insert
another bake entry on subsequent saves or re-enabling a converted tile.

The current import limit uses `materialsCount`, not a separate source count.
Reject conversion if the retained palette cannot fit alongside the reserved
entry, before any mutation, with an explicit message. Do not silently evict
sources or wrap byte IDs. The maximum is 255 procedural source entries for A.

Existing demo procedural tiles also need a one-time conversion: shift their
source definitions and **all existing bitmap IDs** together. Preserve appearance
and leave original disk data intact until a successful save. Define a reliable
way to recognize converted tiles (including disabled/re-enabled tiles) before
implementation; do not assume every existing material 0 is already a bake.
Implemented recognition: optional sample token `TSRE_Terrain_Baked_Material`
(100010), a UTF-16 string. It persists when procedural
mode is disabled. This explicitly reserves material zero; its expected primary
filename and complete pair are also checked on conversion/load. Old demo maps
without the marker migrate in memory (palette and byte IDs together), becoming
dirty without writing route files until save. A full old 256-entry palette is
refused, not truncated. The marker is separate from the existing map reference.
This migration is separate from B's possible future global-ID mapping.

Current marker forms (all retain v1 palette/material-zero semantics):

- `v1:pending`: no completed bake yet.
- `v1:unchecked:<settings SHA256>:<UUID>`: ordinary save. The small settings hash
  covers source definitions/file existence/size/timestamps, patch count, bitmap
  and output sizes, and sampling mode. The UUID distinguishes saved bake versions;
  it is not an input-validation hash.
- `v1:checked:<settings SHA256>:<full-input SHA256>`: explicit validation/repair
  mode computed a whole-map input signature.
- Old `v1:<SHA256>` markers still load. They lack separate source/settings
  metadata, so their first actual save rebuilds once to establish the new baseline.
  Merely loading an old tile in normal mode does not force validation or migration
  writes. With validation enabled, unchecked/mismatched input signatures schedule
  a full repair; a decodable old bake remains usable meanwhile.

## Existing code to reuse, and traps

- [Terrain::makeTextureFromMap](../../../src/tsre/world/Terrain.cpp) already assigns
  one normal/auxiliary material pair and tile-wide UVs to every patch. Reuse its
  coordinate convention, not its direct file-writing/control flow: it depends on
  MapWindow, currently rejects procedural mode and immediately mutates state.
- [TerrainMaterialMap](../../../src/tsre/world/TerrainMaterialMap.cpp) provides
  categorical selection, deterministic scattering, CPU synthesis and BC1 blocks.
  Use these without taking screenshots, invoking MapWindow or reading GPU pixels.
- [TerrainProceduralMaterial](../../../src/tsre/world/TerrainProceduralMaterial.cpp)
  resolves source shaders, manages generated textures, rotates the map's single
  `.bk` and finalizes private materials after successful save.
- [AceLib::save](../../../src/tsre/texture/AceLib.cpp) now accepts explicit writer
  options. Stage A deliberately selects RGB format 14, no mip chain, atomic
  QSaveFile commit and payload-relative row offsets. The new library additionally
  supports DXT output and authored mip payloads, but that does not change this
  bake recipe. The original writer/reader remain in `AceLibLegacy`. No DDS
  substitution is introduced.
- `TFile` contains owning/raw pointers. Do not assume a default C++ copy is a safe
  transactional descriptor snapshot. Back up affected fields/material records
  explicitly or serialize a properly prepared descriptor projection.

## Tile-wide mapping and microtex

Let N be terrain samples per side, P patches per side, R=N/P, and (px,pz) the
patch column/row in the existing terrain-data orientation. Patch-local sample
coordinates span 0..R. The baked transform is:

```text
X = px / P         Y = pz / P
W = 1 / N         H = 1 / N
B = 0             C = 0

U = X + localX * W + localZ * B
V = Y + localX * C + localZ * H
```

Verify row orientation against existing map texture output, rather than deriving
it from W-file Z conventions. Preserve flags, holes, water, bounds, ErrorBias and
unrelated descriptor data. Bake colour everywhere, including hidden patches and
holes; their drawing remains controlled by existing mesh/flag rules.

Do **not bake microtex, lighting, shadows or map overlays** into the base image.
The normal baked material retains `microtex.ace`. Because primary UVs now span
only 1/P per patch, its detail multiplier must be **32 * P** to preserve the
current 32 repeats per patch. Near procedural output continues using 32.
The existing Make Texture From Map code uses fixed `32 * 16`; copying that value
would be wrong for P4/P8/P32. A does not require changing that older tool here.

## Image generation, filtering and memory

Current implementation retains a small CPU miniature with each generated
material. Automatic generation shrinks the original RGB image in its worker,
before BC1 encoding/upload. Synchronous painting uses a **separate coalescing
queue**, retaining only the latest request per patch. It does not lose that
request when workers are busy. The frame pump retries waiting work; a subsequent
edit replaces only that patch's request, not other patches' work. Both queues
share the same four-worker pool. Background loading retains its four-outstanding
job limit; at most four edit jobs are submitted in addition, with the rest kept
in the per-patch queue. GPU handoff never shrinks images. Cancelled/replaced
materials cannot receive stale jobs' results.

Queued edit work temporarily retains the already-generated RGB image (implicitly
shared for identical materials), until reduction and output hashing complete.
Backlog memory is bounded by the latest edited patch versions plus at most four
superseded submitted jobs, not by stroke count. Tile eviction/destruction cancels
this queue and releases pending images; workers cannot resurrect the tile.

The size is `TerrainMaterialMap::BakedSide / patchesPerSide`, not a fixed 32.
Save validates both recipe and current required dimensions, and clears cached
results when source-image comparison detects a change. Miniatures are released
with generated materials (no permanent full-size CPU images or GPU readback).
At the current bake size, distinct miniatures for all patches total at most
0.75 MiB of RGB pixels per tile, excluding container/alignment overhead.

Save first drains this tile's edit queue, then takes the ready miniatures and
updates only dirty regions of the existing baked image in the same worker pool.
The dirty set includes sampling-halo neighbours. Unvisited unchanged patches do
not need near images or miniatures. Only missing/mismatched dirty recipes
generate/reduce a scratch near image. The
save operation still waits for completion. It queues at most one save assembly
job in addition to bounded automatic work, without increasing worker concurrency.
The UI thread blocks without pumping editing events: this is not asynchronous
route saving. Whole-map input hashing is opt-in, not a normal save cost. The ID
map still compresses as a whole, now using zlib level 1 instead of 6; the format
and bounded decoder are unchanged.

The CPU bake image is retained after asynchronous loading or successful save
(0.75 MiB of RGB pixels at 512 square), and released with GPU residency. If it is
absent at save, the worker loads the saved ACE. Missing/invalid/wrong-size base,
new conversion, changed source/settings, or explicit repair require a full bake.
Recipe hashes are cached independently of GPU residency and invalidated only for
affected patches. Source changes invalidate output images, not ID-only recipes.
Small CPU images/metadata are committed only after descriptor save succeeds;
rollback retains the previous baseline and the dirty patch set for retry.

Successful save now promotes existing private output using the content key
computed by the edit worker. Unique textures keep their existing GPU object;
identical textures acquire the existing shared TexLib reference. A deduplicated
texture not yet uploaded bypasses the automatic upload budget on first draw.
No patch synthesis or BC1 encoding is performed by save-time finalization.
This avoids discarding visible edited textures and showing a temporary fallback.

Do not assemble the full-resolution tile at
P*512 pixels per side and shrink it afterwards. P32 would otherwise require a
16384-square intermediate. Repeated patch recipes may reuse reduced images.

Regeneration must use the same source selection and deterministic scatter as the
near image. Direct synthesis at a different output size can change scatter seeds
and sample coverage, so it is not automatically equivalent to reducing the
existing patch output. Avoid decompressing/recompressing BC1 when CPU sources
can generate the image directly.

Use ordinary area/colour reduction to represent the minified near image rather
than taking one ID per baked pixel. This is image minification, not a new
procedural material-blending model. Do not change the painting modes.

The first writer saves only the RGB base level. GPU mipmap generation for the
bake is temporarily disabled for a tile-entry hitch A/B test, both while
procedural mode is active and after disabling it. Base-level linear filtering
and clamp-to-edge remain; microtex mipmaps and near outputs are unchanged.
Expect possible distant shimmer/aliasing; this is not a measured permanent
optimization. On-disk mipmaps/BC1 belong to the future writer.
Check internal patch borders and outer-tile clamping. A lower-resolution image
cannot preserve all near texture detail or scatter pixels:

| P | Baked pixels per patch side at 512 output |
|---:|---:|
| 4 | 128 |
| 8 | 64 |
| 16 | 32 |
| 32 | 16 |

Base-level costs at 512: RGB staging/pixel payload 0.75 MiB (788,696 bytes for the
whole current ACE); future BC1 payload 0.125 MiB. A full BC1 mip chain would be
approximately 0.167 MiB, plus small container/level overhead. These are per-tile
texture costs, not total terrain memory. No new global memory-management task.

## Save and disable behavior

Use a stable, tile-specific asset name distinct from `_map.ace`, for example
`<tile>_procedural.ace` (or `<tile>_lo_procedural.ace` for Lo_tiles), with one `.bk`, not hash-versioned filenames. Resolve the
normal route terrain-texture directory through existing conventions; confirm
hi/lo tile naming does not collide. Initial proposal is base-season baking only,
matching the existing map-conversion restriction: report unsupported seasonal
editing rather than silently overwriting a different season's asset.

Successful save must leave all three resources consistent: ID map + source
palette, baked ACE, and the descriptor's patch assignments. Recommended sequence:

1. Validate palette capacity, sources, destination paths and supported layout.
2. Generate the bake before changing active descriptor assignments.
3. Prepare bounded backups and write the ID map and baked texture with checked
   results. Preserve the existing single-backup policy for each resource.
4. Commit the `.t` with the baked draw shader/UVs and the procedural reference
   retained. Commit the descriptor last.
5. Only after success, install the new baked texture state, invalidate relevant
   texture/UV caches, and finalize procedural edits.

On any reported failure, restore overwritten map/ACE data and changed descriptor
fields, retain backups and leave edits dirty. This is not an assertion of atomic
three-file recovery across power loss, or a redesign of existing Y/F save paths.
No manifest pointing at incomplete output; no silent success from void ACE-save
functions. Reusing a stable texture name must invalidate old TexLib/GPU content.

Rebuild the bake on first save, relevant material/ID changes, missing baked asset,
or changed bake settings. An unchanged valid bake can be retained on later saves;
height-only changes do not require regenerating an unlit colour texture.

Selected minimal disable rule: require a successful save of current procedural
edits before disabling. If the bake is absent/stale, ask the user to save first;
do not silently revert to the last bake and lose visible unsaved paint. Once
current, clear the procedural reference and retain baked shader/UV assignments.
The ordinary route save persists that mode change. Keep unreferenced ID data for
recovery; no automatic source/sidecar deletion. Disable no longer restores the
original patch textures. Re-enabling initializes from the first procedural source
(now ID 1), not the reserved bake at ID 0; it does not reconstruct IDs from the
baked composite. If no valid source remains, report this instead of using the bake.

## Prerequisite: lazy patch generation and tile-level release

Implement and test this before A. Both demand-driven generation and actual
resource release are required, not optional later memory optimizations.

Implementation update, 2026-09-07: the user separately authorized lazy generation
and texture management, followed by bounded background generation. Per-request
generation, paint invalidation and tile-region release are implemented. A now builds on these; B remains design-only.
See the [current demo](terrain-procedural-materials.md) for lifecycle details and
verification. Four-worker scheduling and controlled uploads are now implemented. This
does not fix the general TexLib lifetime of asynchronously loaded file textures.

- Allocate empty patch slots on load. Generate/cache only patches requested for
  rendering, rather than generating the whole tile at the first request.
- Painting synchronously regenerates affected slots, including the existing
  sampling halo, and cancels superseded pending jobs. Its uploads bypass the
  automatic streaming budget. Only automatic first-visibility generation is
  asynchronous; preserve private-edit/deduplication semantics. Save must tolerate
  empty, never-requested slots.
- Release generated resources at whole-tile residency exit, alongside terrain
  mesh GPU-data release, not whenever an individual patch leaves the frustum.
  Returning to that tile regenerates its needed patches.
- Clear both patch references and the tile's recipe-cache references; clearing
  only one leaves materials alive. Release tile-owned detail/source image caches
  where appropriate and regenerate/reload them on demand. Retain the authoritative
  ID map, palette and dirty editing state if the Terrain object must remain alive.
  Generated output is disposable even when a tile has unsaved edits.
- Shared textures survive until the last owner releases them. Each Material owns
  one TexLib reference; multiple patch/cache shared_ptrs own that Material, not
  additional TexLib references. Distinct Materials sharing a TexLib texture must
  each acquire/release their own reference. Account for queued draw use before
  deleting GPU resources, especially in Gather.

Current lifecycle caveats verified in code:

- `Terrain::~Terrain()` resets the procedural state. Legacy `TerrainLibQt::render`
  deletes unused clean, unselected tiles, but the corresponding cleanup loop in
  `pushRenderItems` (Gather) is commented out. Do not assume both paths already
  provide a working residency-exit hook. Dirty/selected tiles also survive the
  legacy deletion gate; their generated caches need a release path independent
  of deleting their editable data. Avoid broad terrain-ownership redesign.
- `TexLib::getTex`/`addTex` acquire references. However, current `delRef()` does
  not fully destroy resources at zero: it only erases a GL-loaded entry after
  `Texture::delVBO()`, which currently resets flags without issuing GL deletion.
  CPU-only entries are retained. Therefore reusing those functions blindly is
  insufficient. The procedural Material destructor already handles final-owner
  CPU deletion and context-aware queued GL deletion. Reuse/verify that ownership
  path or provide a narrowly scoped common release helper; do not introduce a
  second competing owner or claim all TexLib lifetime issues are solved by A.

Test two tiles sharing output: release either one, render the other, then release
the last owner and verify CPU/TexLib/GPU resources are reclaimed. Include private
edits, CPU-only outputs, repeated leave/re-enter cycles, dirty retained tiles,
route close, and legacy/Gather. Measure that generated-cache usage does not grow
with every previously visited tile once those tiles leave residency.

### Generation scheduling

Per-patch requests reduce the work needed before newly encountered terrain can
be drawn; they do not inherently require doing CPU generation on the render
thread. Procedural synthesis/BC1 encoding now uses its own bounded four-worker
pool, separate from the optional threaded ACE/DDS loaders. Reusing TexLib alone
did not automatically put procedural generation on those loader threads.

Prefer worker-side CPU synthesis/encoding with publication and GL upload on the
owning/render thread. Use immutable input snapshots, avoid worker mutation of
shared TexLib/cache maps, coalesce duplicate requests, and bound queued work.
Results need tile-lifetime and edit-generation checks so edits/unload cannot
publish stale textures or resurrect released caches. A worker must not pin whole
tiles indefinitely. Existing texture-loading machinery may be reused where its
lifetime guarantees fit; do not launch unbounded threads per patch.

Until a requested output is ready, use the current baked fallback once A exists;
before A, use an explicit temporary static fallback. Never block awaiting all
patches. A synchronous prototype can measure generation costs, but any initial
render-thread stalls are a scheduling tradeoff, not an unavoidable disadvantage
of lazy generation. Test movement into new tiles and camera turns separately.

## Far rendering: a separate milestone within A

A1 is save-time baking, standard fallback loading and keep-bake-on-disable.
A2 consumes that same asset for far rendering; B is not a dependency of either.

Keep stored patch transforms usable for the static bake. Procedural rendering
uses fixed runtime defaults: X=Y=B=C=0, W=H=1/R, and microtex factor 32.
These are independent from the saved baked transforms; no per-frame rewriting
of `.t` values. The present procedural renderer already overrides UVs in this
way. The far/static path instead uses the saved tile-wide UVs and `32*P` detail
factor. Binding the bake alone without selecting those parameters is insufficient.
Check legacy and Gather, precomputed and paged meshes, without affecting geometry
LOD or map overlays.

The initial 3 x 3 per-tile switch is now replaced by **per-patch texture distance**.
`TerrainMaterialMap::DetailDistanceMeters` defaults to **2048 m**, alongside the
other internal procedural settings (no settings.txt/TRK/GUI control yet).
Measure horizontal camera-to-patch-center distance in physical metres, including
mixed tile sizes and patch counts. Within the radius, use/request detailed output;
outside, use the saved tile bake, even if that patch's detailed texture is already
resident. The limit filters both nearest-first generation requests and actual
draw selection. It is independent of objectlod, tilelod and geometry LOD, but
cannot make already-culled geometry visible. There is no fading or hysteresis.

Unbaked tiles and tiles with unsaved procedural changes retain their previous
detailed-rendering override so old baked pixels cannot hide edits. Cached detailed
textures are not evicted per patch on crossing this boundary: existing whole-tile
residency release remains responsible for reclaiming them. Returning inside the
radius can therefore reuse existing output without regeneration.

**Original eager behavior, replaced by the prerequisite:** `loadProceduralMaterial()` called
`proceduralTexture(0)`, whose first invocation loops over every patch and prepares
its material (with recipe sharing). That whole-tile preparation has been removed;
requested visible patches now generate individually. A2 must preserve lazy
behavior by not requesting near output for tiles using the bake.

A2 builds on the mandatory lazy-generation/tile-release prerequisite above and
adds the separate procedural-distance limit analogous to objectLod. Its far
draws must not request near patch outputs. Whole-tile residency exit releases
generated caches; merely switching to the bake need not evict them immediately.
Do not claim generation-time or memory savings without measuring them. Keep this
work separate from the bake save transaction, not a general streaming framework.
If a required bake is missing, fall back safely to procedural rendering rather
than displaying an invalid texture. A dirty near tile must not silently display
an outdated far bake; an initial policy can keep dirty tiles procedural until save.

## B: options to analyze later

| Option | Effect | Relation to A |
|---|---|---|
| Global selection UI, copy definitions locally | Pick from one catalogue, append/reuse complete definitions in each tile's current palette. Bitmap bytes remain local IDs. | Smallest extension; A's source resolution and persistence remain usable. |
| Local byte-ID to global material-ID mapping | Tile stores a compact mapping; definitions and future mixing_type/importance live in a catalogue. | Removes duplicate source definitions, but requires mapping persistence, stable identities, catalogue ownership, migration and missing-entry handling. |

Prefer analyzing the first option as the migration-friendly baseline, not
preselecting it permanently. **Global means per route**, not per TSRE installation:
each route designer defines that route's material set. Shared definitions must
remain portable when distributing a route.
Do not use catalogue list positions or TexLib runtime IDs as persistent IDs.
For either option, the baked draw material remains a normal standard material.
Only after sources no longer depend on `TFile::materials` can B reduce that table
to the baked draw entry/pair alone. Legacy fallback must not depend on the catalogue.

## Historical acceptance and decisions before implementing A

- Confirmed design choices: material 0 reserved at procedural conversion, sources
  shifted by one; current-save prerequisite for disable; image reduction; A1 and
  per-tile A2 with the default 3x3 procedural range. Authorized for implementation
  with checked uncompressed RGB ACE output first.
- Complete the prerequisite's lazy generation, tile-level release and shared
  ownership tests before A. Verify generation scheduling and pending-job teardown;
  release is not deferred until a later optimization stage.
- Test RGB ACE structure/row offsets with an independent reader and actual legacy
  applications. The current reader alone can share a writer bug. In particular,
  verify 2048 texture limits and whether each reader tolerates the procedural
  extension token; standard baked fields do not prove the whole `.t` is accepted.
  If necessary, a legacy export that omits the extension is a separate choice.
  The future DXT1 ACE class/specification is not a dependency of this interim A.
- Round-trip P4/P8/P16/P32 and varying N/physical tile sizes. Check one draw
  material, exact UV coverage, orientation, microtex density, hidden patches,
  holes/water and preservation of unrelated file fields.
- Test full source palettes, repeated saves, unchanged saves, failed generation,
  ACE/map/descriptor writes, missing sources/bakes, disable before/after saving,
  and stable-name texture reload without stale GPU output.
- Compare near output against the baked image and mip levels, including scatter,
  patch boundaries and tile edges. Measure CPU bake/encode/save time and peak
  staging memory. Do not present lower-resolution fallback as identical quality.
- Update the demo task's current-behavior sections and UI text.

## Stage A implementation notes

- `TerrainMaterialMap::bake()` updates dirty regions of a correctly sized saved
  bake, using cached recipe hashes and valid worker-generated miniatures. Dirty
  cache misses generate the same near output and minify it into a `BakedSide/P` region.
  A cache retains
  only reduced recipes, bounded by one tile image; no all-patch full-resolution
  intermediate, GPU readback or near-output residency is required.
- `Terrain::saveProceduralBake()` prepares the ACE before the map/descriptor save.
  The ACE keeps one `.bk`; a reported map/descriptor failure restores the old ACE,
  ID map, bake marker and changed patch fields. The `.t` commits last. Existing
  Y/F write behavior and power-loss recovery are not redesigned.
- Full input signatures are debug/repair-only. Small source/settings metadata
  and tracked edits drive normal saves. Unchanged/height-only saves keep
  a valid bake; missing files or changed source/settings cause a full rebuild.
  Ordinary painting replaces only dirty bake regions. A rebake decodes
  current used source files and invalidates near outputs if their source pixels
  changed, so it cannot save new file stamps alongside old cached pixels.
  Continuous source-file watching outside save/reload is not implemented.
- Both precomputed and paged meshes retain normalized near UVs while procedural
  mode is active. A cached `terrainTextureRemap` vec3 in the existing vertex shader
  maps them to the baked patch region when drawing a fallback. Zero is identity;
  it is reset before other draws. No geometry rebuild or shader-program switch
  occurs as workers finish or the tile crosses the texture-distance boundary.
  Both legacy submission and Gather carry the remap and 32*P baked detail scale.
- Normal and pending-near fallback draws use the same saved bake. Patches outside
  `DetailDistanceMeters` do not request or draw detailed procedural output.
  Physical patch centers, not World-cell indices, determine the distance.
  Dirty/unbaked tiles remain procedural. Geometry LOD is unchanged.
- Successful rebakes invalidate ordinary TexLib lookup aliases for the stable
  filename and release this tile's static texture references. Loader-owned
  paths/pixel buffers are not changed under in-flight readers. New requests load
  the committed ACE; this does not repair general ordinary TexLib reclamation.
- While procedural mode is active, bake-file decoding uses the same bounded
  four-worker/four-outstanding-job pool and controlled upload budget as near
  generation. Loading a procedural tile requests its saved bake immediately,
  without waiting for visible patches or requiring a GL context. Begin-frame
  processing retries requests when the pool was full and uploads decoded bakes
  before visible-patch generation can consume the upload budget. Pending near
  patches draw their region of this bake, not an empty/loading patch. Prefetch
  preserves active texture/binding state and generates no near-patch images.
  If the tile becomes visible before asynchronous file loading/upload finishes,
  or has never been baked, an initial placeholder remains possible; this is not
  a synchronous guarantee against all first-frame pop-in. Tile residency release
  cancels prefetch/retries too, so the frame pump cannot resurrect evicted caches.
  RGB outputs use shared Material ownership and release at tile residency exit;
  the file job captures no Terrain/TFile/GL pointers. Bake GPU output currently
  has base-level linear filtering and clamp-to-edge, with a separate identity from near
  output. No synchronous ordinary ACE loader is started as a second fallback.
  Once procedural mode is disabled, ordinary static texture ownership applies.
- Disable requires a current successful bake and keeps its assignments. Re-enable
  keeps the reserved slot/source palette, initializes IDs to 1 and does not infer
  paint layers from the baked RGB. Old assets/sidecars are not swept/deleted.
- External MSTS/MSRE/Open Rails visual acceptance remains a user test; automated
  TSRE round trips cannot establish how those readers treat extension tokens.

## Verification, 2026-09-07

Release build succeeded (Qt 6.10.1 / MinGW). Commands:

```text
build/TSRE5vc.exe --test --test-suite terrain-material
build/TSRE5vc.exe --test --test-suite terrain-material-gl
build/TSRE5vc.exe --test --test-suite terrain-grid
```

- **288 CPU checks passed**, zero failures. Includes independent RGB ACE row-table
  and planar-payload inspection plus legacy-reader round trip, conversion and
  one-time old-map migration, full-palette refusal, N128/P4 through N2048/P32,
  baked UV/detail scale, flags/ErrorBias, minification/orientation, save/re-enable,
  unchanged saves, source-file refresh, missing bake, and ACE/map/descriptor
  failure recovery. Stable-file alias invalidation leaves loader-owned state
  unchanged. Existing painting/import/worker/lifecycle tests still pass.
- **OpenGL suite passed**, zero failures on AMD Custom GPU 0932. All six standard
  vertex/fragment pairs compile/link. Offscreen rendering checks UV remap and
  identity reset; baked-file jobs obey budgets, create clamped textures,
  share identical output across tiles, and release GL storage at the final owner.
  Synchronous painting and nearest-first automatic uploads still pass.
- **66 terrain-grid checks passed**, including ordinary T-file round trips and
  overwrite output. No existing route was written; tests use temporary fixtures.
- The final simple uniform-source fixture saves took approximately 0.46–0.58 s
  per tile. These include ID-map/descriptor/Y saving, use different N/P layouts,
  and ran under a background workload. They are not a benchmark of unique painted
  route content or frame latency. A 2048-square RGB ACE is 12,591,320 bytes here;
  one prior `.bk` may occupy the same amount after a later changed save.
- Local logs: `build/terrain-material-baked-{cpu,gl,grid}.log`. The user's root
  `log.txt` was restored byte-for-byte. Interactive route and external-editor
  acceptance are still required, preferably on a copied test route.

## Miniature-cache verification, 2026-09-08

- Release build succeeded. `terrain-material` passed **340 CPU checks**, zero
  failures; `terrain-material-gl` also passed with zero failures. New checks cover
  worker miniatures at P4/P8/P16/P32, cached versus regenerated bake equality,
  changed recipes, wrong-size rejection/regeneration, synchronous painting's
  queued reduction, and cache release. OpenGL checks confirm that miniatures
  survive release of full-size CPU pixels after upload, including different
  miniature dimensions for P16/P32 sharing the same GPU output.
- On temporary copies of `procedural/tiles/-11dbfba0` (256 patches, 71 distinct
  recipes), five forced rebake/map-write saves per case averaged **1742 ms without
  miniatures** (1666–1862 ms), versus **665 ms with ready miniatures** (566–855 ms).
  Bake time including signature/source checks and ACE writing averaged 1586 ms
  versus 484 ms. All 71 recipes were reused in every cached run.
- These runs had substantial background memory pressure. Compare the paired
  cases, not their absolute timings against earlier measurements. Prewarming was
  excluded from save timing and used CPU-only generation; the separate OpenGL
  test verifies that upload does not invalidate that cache. This fixture did not
  include private edited-material finalization, so it is not a complete benchmark
  of every interactive save. No GPU-readback benchmark was performed.
- ID-map compression and full bake-signature hashing remain possible
  optimizations. The subsequent edit-queue fix removes finalization regeneration.
  Source images still
  undergo the existing save-time freshness check before miniature reuse.
- Logs: `build/terrain-material-miniatures-{cpu,gl}.log` and
  `build/terrain-save-miniatures-{uncached,cached}.log`. Existing route files were
  only read; tests wrote temporary copies. The user's `log.txt` was restored.

### Edit-queue and save-flicker correction (2026-09-08)

The earlier skip-on-full miniature requests were insufficient: later painting
cancelled other patches' unfinished work, and successful save regenerated private
materials as shared ones, replacing textures that were already visible. This is
now replaced by the separate coalescing edit queue and in-place promotion/content
deduplication described above. Background-loading request limits are unchanged.

Release build and **345 CPU checks in both BC1 and RGB modes** pass, with zero failures; the offscreen
OpenGL suite also passes. New tests edit 16 distinct patch recipes twice without
explicit inter-stroke waits and compare all final miniatures before save. They
also save immediately after another edit and verify texture identity retention.
The OpenGL test verifies that the unique edited texture's GPU object survives
save, an identical edited texture in another tile deduplicates to it, and final
reference release deletes the GPU object only after both users release it.
Existing source-change, failed-save, cancellation and tile-lifecycle checks pass.
Logs: `build/terrain-material-edit-queue-{cpu,gl,rgb}.log`.
These are automated/offscreen checks, not interactive acceptance of the reported
flicker. The earlier cached-save timings predate this correction.

### Incremental save implementation and verification (2026-09-08)

Implemented all three follow-ups: dirty-region bake updates, persistent native
recipe-key caching, and removing full ID-map hashes from normal saving. Zlib level
1 replaces level 6 without changing the `.pmap` format. An old-style bake rebuilds
once on its first actual save to establish source/settings metadata; subsequent
painting saves use the incremental path. New conversions, source/settings changes,
missing/invalid base images and explicit validation/repair retain full rebuilding.

Release build succeeded. **377 CPU checks passed in both BC1 and RGB modes**, the
offscreen OpenGL suite passed, and **66 terrain-grid checks passed**. Coverage
includes P4/P8/P16/P32 incremental/full image equality, sampling halos, cached
keys, missing miniatures, wrong-size bases, eviction and worker-side base loading,
checked/unchecked T-file metadata round trips, explicit repair, and prior save
failure/rollback, source-refresh and shared-texture lifetime checks.

Five one-click/one-patch save runs on temporary copies of the current
`procedural/-11dbfba0` tile (N256/P16, 123 original unique recipes):

| Case | Earlier review average | Incremental average |
|---|---:|---:|
| All near recipes warmed | 543 ms | 105 ms |
| No other near recipes resident, CPU bake retained | 2234 ms | 94 ms |
| CPU bake and generated textures explicitly evicted before painting | not measured | 120 ms |

The evicted case included worker-side ACE reading; four runs were 96–105 ms and
one was 190 ms under background load. Every measured edit used incremental mode
with exactly one dirty patch. Baseline preparation/migration and painting are
excluded from save timing. These are terrain-save measurements, not whole-route
save or interactive frame-latency claims. Original route files were only read.

Isolated compression of the route's 16 MiB map averaged about 41 ms at level 1
versus 109 ms at level 6. File size increases from 87,338 to 174,209 bytes (about
85 to 170 KiB); the bounded decoder accepts both. Whole-map compression and the
existing Y/raw save remain non-incremental, but no longer dominate a multi-second
unchanged-patch regeneration pass.

Logs: `build/terrain-material-incremental-{cpu,rgb,gl,grid}.log`,
`build/terrain-single-save-incremental-{warm,cold,evicted}.log`, and
`build/terrain-pmap-compression-levels.log`. The user's `log.txt` was restored.

### Independent detailed-texture distance (2026-09-08)

`TerrainMaterialMap::DetailDistanceMeters = 2048.0f` now controls per-patch
generation and draw selection, replacing the fixed 3x3 whole-tile switch.
Build, **398 CPU checks** and the offscreen OpenGL suite passed. New tests cover
P16/P32, exact and just-outside boundaries, independence from objectlod, cached
far textures switching to the bake, camera movement back to resident output,
and physical distances on larger tiles. Logs:
`build/terrain-material-detail-distance-{cpu,gl}.log`. Existing unsaved/unbaked
overrides and tile-level resource eviction are preserved.

### ACE v2 integration merge verification (2026-09-08)

The complete ACE v2 branch is merged without removing the incremental-save,
coalesced-miniature, shared-texture promotion or detailed-distance improvements
above. Procedural source reads and the shared bake reader (including an evicted
CPU bake reloaded for incremental saving) explicitly use full-resolution CPU
pixels with no staged mipmaps. RGB bake writing uses the new QImage API.

The Windows Release build, **405 CPU checks in both BC1 and RGB modes**, the
procedural OpenGL suite and **66 terrain-grid checks** pass. The additional
coverage checks authored-mip ACE sources and baked fallbacks while rendering
texture quality is reduced. See the [ACE library verification](../../features/ace-library.md#windows-merged-main-integration-2026-09-08)
for commands, logs and the separate standalone DXT3 GPU-readback discrepancy
on this Windows/AMD host. These results do not remeasure the historical save
timings above or repeat the MSRE experiments.

### Opaque DXT1 baked fallback (2026-09-08)

At the initial DXT1 milestone, 512x512 tile bakes used `AceEncoding::Dxt1`, with no alpha, authored
mipmaps or SIMISA zlib envelope. The BC1 image payload is **131,072 bytes
(128 KiB)** instead of 786,432 bytes (768 KiB) for RGB, plus a small ACE header.
This is lossy texture compression, not lossless packing of the previous RGB.
The standard shader, tile-wide UVs, microtex, near/far distance and disable
behavior are unchanged. MSRE compatibility tests from the ACE library remain
separate evidence; this change does not claim a fresh interactive MSRE test.

Encoding is part of the small bake-settings key. Previously saved RGB bakes
remain loadable as fallbacks and are upgraded on the next actual terrain save
that checks/rebuilds the bake; route load does not modify files or mark every
old tile for unsolicited saving. Unchanged saves of a current DXT1 bake do not
rewrite it or rotate backups. The decoder continues to require the current
bake dimensions and still does not resample old 2048 images.

Assembly and DXT1 encoding run in the existing worker pool. Save waits for that
work, then synchronously performs the checked ACE write and existing `.bk` /
map / descriptor transaction: this is not background route saving. Initial
encoding uses the new ACE document API. Incremental saves preserve old BC1
blocks outside the dirty patches and their existing sampling halos, rather than
re-encoding those blocks from a previously decompressed base. All current
P4/P8/P16/P32 miniature edges align to 4x4 blocks. A full rebake, old RGB bake
or incompatible previous document follows the full-encoding path. This avoids
cumulative colour drift in unchanged regions after eviction/reload, while
retaining the one-patch save optimization.

This follow-up changes the **saved encoding**, not the owned procedural bake
prefetch/cache representation: that path still decodes on its worker, keeps an
RGB CPU base for incremental assembly and uploads RGB without mipmaps. No extra
VRAM reduction is claimed for that active procedural fallback cache. Ordinary
static texture loading can use the ACE loader's compressed GPU upload path.

New checks inspect opaque DXT1 headers, dimensions, single-level block payload
and file size at P4/P8/P16/P32. Existing tests cover legacy RGB bases, shared
fallback loading, source refresh, no-op saves, disabling, migration and failed
save rollback. A valid noncanonical compressed block in an untouched patch is
verified byte-for-byte across three edit/save/reload cycles to detect accidental
lossy recompression. The old source-refresh fixture now explicitly CPU-decodes
the loaded bake, because a rendering ACE load correctly retains DXT1 blocks.

Verification: Windows Release build, **417 CPU checks in both BC1 and RGB near
output modes**, and the procedural OpenGL suite pass. RGB near-output mode does
not change the new on-disk DXT1 bake policy. Logs:
`build/terrain-material-dxt1-bake-{cpu,rgb,gl}.log`. The user's app log was restored;
tests operate on temporary routes, not original route data.

### Default bake increased to 1024 (2026-09-08)

`TerrainMaterialMap::BakedSide` is now **1024**; ID maps stay 4096-square and near
patch output stays 512-square. Opaque DXT1 payload becomes **524,288 bytes
(512 KiB)** plus the ACE header. The active procedural fallback still uses the
existing decoded RGB cache/upload path, now 3 MiB rather than 0.75 MiB per unique
bake; no compressed-GPU-cache optimization or mipmaps are added by this change.
Miniatures automatically derive their side from `BakedSide / P`: P16 now uses
64-square miniatures and P32 uses 32-square miniatures.

The existing exact-size loading policy is unchanged: old 512 and 2048 bakes are
not resized or used by the procedural fallback loader. A successful terrain save
regenerates them at 1024. Until then detailed procedural generation is the
fallback, so there may be no baked placeholder during loading. Merely loading a
route does not rewrite old files. Tests cover both old sizes and regeneration.

Verification at 1024: Release build, **421 CPU checks in both BC1 and RGB near
output modes**, and the procedural OpenGL suite pass. The incremental-edit
fixture now derives its probe pixel from bake size instead of assuming 512.
Logs: `build/terrain-material-1024-bake-{cpu,rgb,gl}.log`; original app log restored.
