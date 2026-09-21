# Complete terrain T-file structure, parser and caller migration

Status: **binary codec and engine/editor migration implemented, 2026-09-21**.
`TFile` now owns the typed native model directly. Normal load/save, both mesh
paths, material edits, procedural snapshots/undo/baking and network parsing use
it; there is no float-array/material-pointer adapter. No user route files were
rewritten by verification. The [runtime report](reports/terrain-tfile-runtime-integration.md)
records tests, benchmarks and remaining interactive/performance qualifications.
The earlier [binary-core checkpoint](reports/terrain-tfile-binary-core.md) remains
historical evidence, not the current integration status.

### Implementation checkpoint

- [x] Review shape-parser lessons: bounded inflation/read scopes, contiguous
  numeric arrays and sparse cold preservation, not a scalar scene DOM.
- [x] CPU-only legacy/typed parse-and-write benchmark and synthetic fixtures.
- [x] Typed native binary records, dynamic shader slots/UV calculations,
  all patch sets, C/D/patch-F references, embedded AS/US, transfers and shapes.
- [x] Content-based shader classification; explicit stale auxiliary-reference
  repair, separate from byte-preserving parsing. No editor auxiliary assignment.
- [x] Transactional reads, bounded decompression, checked atomic single-file
  writes, labels/order/unknown blocks and optional-field preservation.
- [x] Dedicated `terrain-tfile` suite and read-only corpus scanner.
- [x] Complete engine/editor caller migration and remove the legacy runtime
  representation. This includes ownership, exact integer patch indices, material
  snapshots, procedural undo/rollback, effective patch-F handling and save preflight.
- [x] Merge known TSRE extension editing with cold preservation, including
  bake-container labels, order and unknown children.
- [x] Runtime parse/write and compressed file-I/O benchmarks; CPU/GPU regression
  suites and read-only real-route corpus checks.
- [ ] Exhaustive live/peak allocation profiling and interactive full-route FPS,
  editing, shadows/Gather and external-editor acceptance after integration.
- [ ] Text codec, if accepted as the separate fixture-led milestone below.

The old implementation is frozen under `tests/tokens/TerrainFileLegacy*` solely
for benchmark comparisons; it is not linked into the application. All local
builds use at most two compiler jobs (`-j 2`), as requested.

### Runtime integration decisions

- `TerrainFile::Data` is the native value model; `TFile` adds editor/procedural
  policy and explicit external patch-flag loading. Patches stay contiguous,
  60 bytes each, with exact `quint32` shader indices and named UV/bounds fields.
- Primary/auxiliary accessors address one flat vector. Paired detection uses
  shader names/order, never tile location. Stale auxiliary references are
  repaired once during adoption; new direct auxiliary assignments are refused.
- Palette insertion/removal/reordering updates all sets. Unknown records that
  could contain indices block renumbering, not unrelated bounded edits.
- Height changes accumulate a cheap sample rectangle. Save updates overlapping
  inactive-set bounds/error bias; UV-only saves leave those bounds untouched.
  Inactive sets not interpretable on the shared sample grid make editing unsafe
  and leave the tile read-only rather than dropping the sets.
- External patch-F bytes override runtime flags. Missing/invalid sidecars leave
  the tile viewable/read-only. Unchanged bytes and inline words are preserved;
  edited bytes use `Flags & 0xcb`. Descriptor/sidecar staging rolls sidecars back
  if descriptor commit fails. This is not a transaction for every RAW/ACE file.
- Save preflight precedes normal terrain resource writes; parsing/layout checks
  do not serialize the descriptor during loading. Layout-only probing skips
  shader and patch arrays. Disk-side flag conflicts are detected on save.
- Network parsing performs no local resource lookup: external patch-F references
  without transferred sidecars remain read-only. No new network protocol is added.
- Texture cropping receives a typed UV transform plus actual R; no pointer into
  a numeric patch array or fixed-16 crop domain remains. Flat one-texture shaders
  disable the previous patch's detail-texture state in the legacy draw path.

## Goal and scope

Replace the incomplete, pointer-heavy `TFile` model with a complete representation
of the documented terrain descriptor. Read and write all documented records,
preserve unsupported extensions, and migrate the engine/editor callers without
large loading, painting, rendering or memory regressions.

"Complete" means complete **file representation and round-trip support**, not
implementing every MSTS terrain algorithm. This task does not introduce E/AS
adaptive triangulation, a new LOD policy, C/D interpretation, terrain-owned shape
collision, or rendering of terrain-owned transfer records. Retain those records
and report unsupported runtime semantics rather than pretending to implement them.
W-file `TransferObj` is a different feature and must remain working.

Keep current procedural formats, seasons, material-library UiDs, terrain profiles,
patch picking, paged/legacy backends and dirty-patch updates. No automatic route
migration, new shader IDs, new file tokens or changes to World tile coordinates.
World files remain 2048 m cells; terrain footprints remain independent.

### Evidence and related work

- [T-file field usage and implementation schema](../../msts/tsre-msts-terrain-tfile-field-usage.md).
- [MSTS shader pairing and procedural fallback](../../msts/msts-terrain-shader-pairing-and-procedural-fallback.md).
- [Multiple patch sets and water](../../msts/msts-orts-multiple-patchsets-and-water.md).
- [Native token IDs](../../features/native-token-ids.md) and
  [FileBuffer ownership, framing and recovery](../../features/file-buffer.md).
- [Heightmap layouts](terrain-heightmap-resolution.md),
  [patch count](terrain-patch-count.md),
  [paged mesh / distant-terrain issue](terrain-paged-mesh-and-shared-map.md),
  [discrete LOD](terrain-basic-discrete-lod.md),
  [procedural materials](terrain-procedural-materials.md),
  [bakes and catalogue](terrain-procedural-baked-fallback.md),
  [seasons](terrain-procedural-seasons.md).

The MSTS reports contain dated TSRE source observations and historical procedural
design proposals. Use their recovered binary layouts, but the current source for
TSRE behavior. In particular, the implemented route material catalogue is not to
be replaced by the report's older shared local-shader palette proposal. Statements
about ORTS refer to the research's pinned versions, not a fresh upstream audit.

## 1. Current implementation audit

Principal files: [TFile.h](../../../src/tsre/world/TFile.h),
[TFile.cpp](../../../src/tsre/world/TFile.cpp),
[TFileBakeMetadata.cpp](../../../src/tsre/world/TFileBakeMetadata.cpp).

Names such as `get151()` below identify existing code only. New parser/writer
functions use meaningful schema names (`readShaders()`, `readPatchSets()`,
`readTextureSlots()`, etc.) and symbolic `TS::terrain_*` enums, never numeric
function names or magic token literals.

| Area | Current source behavior | Required change |
|---|---|---|
| Binary framing | `FileBuffer::readBlock`, `ScopedLimit`, checked scalars and complete 32-bit token IDs are already used | Retain this foundation; do not invent another token namespace or rewrite every SIMIS consumer |
| Shader table | `get151()` always divides count by two into `materials` / `amaterials`; `get163()` folds upper indices; save emits `materialsCount * 2` | Preserve the complete shader table; detect pairing before repairing stale auxiliary references; never fold flat-list indices |
| Slot/UV lists | Fixed two-entry arrays; current checked parser **rejects** counts greater than two | Dynamic lists, bounded by the input; the old research's unchecked-overrun description is no longer current |
| UV calculations | Four integers, with float bits in the fourth; callers reinterpret/memcpy `itex[1][3]` | Three signed integers and one float, serialized with the correct bits |
| Patch sets | Last parsed set replaces arrays; earlier allocations are deleted, data is lost; writer emits one set | Retain every set in original order; explicitly select an active runtime set |
| Patch fields | `float[13]`, separate flags/error arrays; shader integer converted to float; `PatchField` only names offsets | Named typed records; no integer-through-float conversion |
| Distance | `patchsetDistance` is an integer carrying float bits | Float32 distance; do not numerically cast the old integer representation |
| Unsupported native fields | C, D, patch-set F, tile-owned transfers/shapes skipped | Typed representation and serialization of their documented payloads |
| Unknowns/labels | Most unknown children and ordinary labels/tails are dropped; AS/US alone retain labels/payloads | Preservation at every schema container, including extensions and nested shaders |
| AS/US | Separate variable-length opaque payloads, not interpreted | Preserve, validate length against N after parsing; do not merge AS with US |
| Water | One-float and four-float input already accepted; writer always emits four | Preserve original form on an unchanged save; expand only when needed |
| Ownership/reload | Empty copy constructor/destructor; heap scalar/string pointers; partial reset on load | Value/RAII ownership, explicit copy/move semantics, transactional parse/replace |
| Saving | `QSaveFile` checks descriptor commit; hand-calculated block sizes and singleton/paired assumptions remain | Checked composable serialization; retain stream output used by networking |
| Header/text | Full loader assumes binary after a fixed 32-byte skip | Explicit format dispatch; text support is a separate milestone below |

`loaded=true` currently means that the top-level parse finished, not that all
required renderable fields or references were validated. Count framing has
improved, but `nextTerrainBlock()` searches past unknown siblings without retaining
them. Reusing a `TFile` can retain old optional members not reset by `load()`.

The dated claim that patch bounds are unused is also obsolete:
`Terrain::refreshPatchBounds()` calculates bounds and updates all six descriptor
fields, and current culling uses cached runtime bounds. Preserve that behavior.

### Distant terrain is a correctness priority

Stock `Lo_tiles` use flat `TexDiff` lists, including one-entry and odd-count lists.
The current split/fold/write path cannot round-trip these correctly. This is
separate from large tile dimensions: distant generation still has its own profile.

The paged-mesh task records a BNSF Scenic distant-terrain driver crash. Correct
shader representation and consumers are prerequisites to fixing/testing that
path, but this source review does not independently prove the entire crash cause.
Do not close that issue without reproduction and retest; do not mask it by forcing
distant terrain to the legacy backend or disabling QuadTree.

## 2. Proposed data model

Use value-owned strings and contiguous vectors. Prefer `std::vector` for mutable
hot patch storage so writes cannot unexpectedly detach a shared Qt container.
Small optional scalar fields need a value plus presence (`std::optional` is fine),
not separate heap allocations. Absent, present-empty, zero and malformed-present
must remain distinguishable where the format requires it.

Conceptual model; names can be adjusted during implementation:

```cpp
struct TextureSlot { QString filename; qint32 stateArg0, stateArg1; };
struct UvCalc { qint32 arg0, arg1, arg2; float scale; };
struct Shader {
    QString name;
    std::vector<TextureSlot> textures;
    std::vector<UvCalc> uvCalcs;
};
struct PatchBounds {
    float centerX, averageY, centerZ;
    float sphereRadius;          // file FactorY
    float verticalHalfExtent;    // file RangeY
    float horizontalHalfExtent;  // file RadiusM
};
struct PatchUv { float x, y, w, b, c, h; };
struct Patch {
    quint32 flags;
    PatchBounds bounds;
    quint32 shaderIndex;         // flat index; primary-half index in paired mode
    PatchUv uv;
    float errorBias;
};
struct PatchSet {
    // Presence/labels/order metadata omitted from this sketch.
    float distance;
    quint32 patchesPerSide;
    std::optional<QString> flagsBuffer;
    std::vector<Patch> patches;
};
```

The root also owns sample metadata; Y/F/E/N/C/D references; separate AS/US byte
arrays; optional water corners; `vector<Shader>`; `vector<PatchSet>`;
tile-owned transfers/shapes; and the current typed TSRE procedural metadata.
It also retains the optional tile-level `errthresholdScale` and
`alwaysselectMaxdist` floats independently of patch `errorBias` and route settings.

### Target schema inventory

This hierarchy defines ownership, not a requirement to reorder loaded blocks.
Parentheses contain native token IDs; all IDs retain their complete namespace.
Each binary block has the normal label framing in addition to the payload shown.

```text
terrain (136)
  terrain_errthreshold_scale (137): float32
  terrain_alwaysselect_maxdist (138): float32
  terrain_water_height_offset (251): one or four float32 values
  terrain_samples (139)
    terrain_nsamples (140): uint32
    terrain_sample_rotation (141): float32
    terrain_sample_floor (142): float32
    terrain_sample_scale (143): float32
    terrain_sample_size (144): float32
    terrain_sample_fbuffer/ybuffer/ebuffer/nbuffer/cbuffer/dbuffer
      (145/146/147/148/149/150): UTF-16 filename each
    terrain_sample_asbuffer/usbuffer (281/282): separate raw byte payloads
    TSRETerrainMaterialBuffer: UTF-16 .pmap reference
    TSRETerrainMaterialMap: counted (uint32 byte-ID, uint32 UiD) pairs
    TSRETerrainBakedMaterials: version + content revision + seasonal records
    TSRETerrainBakedMaterial: legacy string form only in this parent
  terrain_shaders (151): count + Shader[]
    terrain_shader (152): name string + slot/UV child lists
      terrain_texslots (153): count + TextureSlot[]
        terrain_texslot (154): filename + two int32 values
      terrain_uvcalcs (155): count + UvCalc[]
        terrain_uvcalc (156): three int32 values + float32
  terrain_patches (157)
    terrain_patchsets (158): count + PatchSet[]
      terrain_patchset (159)
        terrain_patchset_distance (160): float32
        terrain_patchset_npatches (161): uint32 P
        terrain_patchset_fbuffer (162): optional UTF-16 filename
        terrain_patchset_patches (163): P*P records, no extra count
          terrain_patchset_patch (164): 60-byte numeric record
  terrain_transfers (165): count + TerrainTransfer[]
    terrain_transfer (166): nested Shader, then x0,z0,x1,z1 float32
  terrain_shapes (167): count + TerrainShape[]
    terrain_shape (168): filename + four int32 bounds + three float32 rotations
```

Current extension IDs and the two different context-dependent
`TSRETerrainBakedMaterial` payloads are specified in the native-token feature
document: inside the plural container it is a seasonal record, not the legacy
string. Preserve those layouts and dispatch by parent context as well as token.

Keep uncertain fields honestly named (`stateArg0/1`, `arg0/1/2`), with the research
mapping in comments. Do not invent texture sampler semantics or shape rotation
axis names. Terrain-owned transfers contain a **nested Shader**, not an index;
their four floats are `x0,z0,x1,z1`, not necessarily ordered minima/maxima.
Terrain-owned shapes contain filename, four signed sample-grid bounds, and three
float rotations in radians in file order.

The ordinary patch has the same 60 bytes of numeric content as today's
`13*float + flags + errorBias`: 15 KiB for P16, 60 KiB for P32. This is a locality
and memory-cost target, not a permanent 60-byte C++ ABI requirement. Verify actual
size/alignment; serialize fields explicitly because the native struct layout is
not the disk format. Keep variable-size preservation metadata out of hot patches.

### Shader identity and pairing

1. The serialized flat list is authoritative, for detailed and distant terrain.
   Preserve shader names, order, counts and valid patch indices on load/save.
   Stale auxiliary-half references in confirmed paired tables are an explicit
   repair exception to exact patch-record preservation, described below.
2. Resolve a small cached material view from **shader array contents**, at
   load/edit time, for both detailed and distant terrain. Recognize the conventional even
   `[DetailTerrain..., AlphaTerrain...]` layout by names, case-insensitively.
   Do not require matching texture names: stock corresponding pairs can differ.
   TSRE supports paired as well as flat distant tiles. `LO_TILES` location must
   not force flat interpretation; `TILES` location must not force pair detection.
   Even count alone is insufficient: an even-sized `TexDiff` list remains flat.
3. For a recognized pair, editor material operations maintain both shader records,
   but patches reference only the primary half. Direct auxiliary-half assignment
   is not a supported authoring feature: no UI, public editing operation or new
   TSRE file output should create it. Repair legacy occurrences on load.
4. Unknown/custom arrangements remain flat and preserved. TSRE may display a
   supported texture approximation with a diagnostic; it must not claim arbitrary
   shaders or flat detailed lists are MSTS-compatible. Native MSTS chooses paired
   mode from its terrain manager, **not** by this TSRE editor classification.
5. Shader creation/cloning/deletion on an existing tile follows its detected
   mode: a pair operation for paired data, a single-entry operation for flat data,
   including both modes in distant terrain. Keep current brand-new tile defaults
   unless their change is explicitly agreed; the parser refactor must not impose
   a new distant creation mode. Do not silently convert existing layouts or
   invent a mixed-mode table where arbitrary entries are paired independently.
6. Insertion/deletion/remapping must update references across **all patch sets**.
   After load-time repair, appending a pair leaves primary patch indices unchanged;
   deleting/compacting materials still requires updating affected references.
   Keep auxiliary shader records correctly paired when table positions move.
   Make required remapping one edit-time operation, never per draw.
   Opaque content that may reference indices must not be blindly renumbered;
   refuse the affected destructive operation or require explicit conversion.

Research found seven upper-half references with flags `0x00000300` in stock USA2
`-01a18944.t`. The traced far-material path adds half the shader count and sets
`0x300`; the near path reverses this, and the traced save path normalizes auxiliary
references. This explains the state represented by those records, **not why that
particular shipped file retained it**. A saved transient material-selection state
is plausible, but its authoring tool/save path was not established by the research.
The agreed **TSRE policy treats this as a stale-state authoring/save bug to repair**,
not a feature or a mixed shader mode. This policy does not claim that the research
identified the original faulty writer.

For a validated paired table of `2*M` shaders, normalize each patch once after
parsing and mode detection: an index in `[M,2*M)` becomes `index-M`. When the
auxiliary-state flag `0x200` is present, clear it and set `0x100`, preserving all
unrelated flag bits. Do not subtract twice if the index is already in the primary
half. Check bounds before access; an index outside the complete table is not
repaired by arbitrary subtraction/modulo. Report the repair once per tile and
retain a diagnostic of the original values if useful. Ordinary explicit save
writes the corrected record; loading alone never writes the file.

Apply this to every parsed patch set and reconcile any effective patch-F flags
without letting a stale override reintroduce the auxiliary state. Flat tables
must never receive this repair, even if their indices are in the upper half or
they carry similar flag bits. Valid auxiliary shader definitions remain in the
table for native MSTS's own near/far switching; only direct patch assignment is
normalized. Repair must not crash TSRE or disable an otherwise usable tile.

Slot count and shader-name support are runtime capabilities, not parse limits.
Keep extra slots/UV records even if TSRE currently renders only the supported
primary/detail combination. Guard short/empty lists instead of accessing slot 1
because slot 0 exists.

### Patch-set selection and editing

Keep the **last set** active for ordinary TSRE terrain rendering, water, selection,
mesh generation and editing. This preserves current TSRE behavior and matches
the researched MSTS normal draw/query path. ORTS's first-set behavior is a
compatibility warning, not a reason to reorder or duplicate the saved collection.
`distance` is retained, not connected to TSRE's discrete LOD bands.

Store the active index and validated layout once. Hot code uses a reference/span
to that set; it must not search a tree each frame. Runtime P/N/R restrictions
remain in `TerrainGridLayout`, separate from syntactic file acceptance. A safe,
well-framed unsupported set can be retained without allocating GPU geometry.

UV/material/patch-flag edits target the active set. Heights and water corner
heights are tile-wide: height-derived bounds and affected ErrorBias values need
updating for corresponding patches in every interpretable set before saving.
Do that for changed regions, not during every draw. Do not copy UVs or patch flags
between grids. If an unsupported set prevents a safe shared-height update, allow
viewing and explain the specific editing restriction; do not silently discard it.
No new patch-set-selection UI is required for this task.

## 3. Parsing and preservation

### Binary parser

- Reuse `FileBuffer` framing and full `TS::TokenId`. Parse into a fresh local
  descriptor, validate, then move it into the target. Failed reads cannot leave
  a half-updated live descriptor, old AS bytes, stale shaders or leaked arrays.
- Validate SIMISA/JINX format before binary traversal; retain compressed binary
  loading. Check file/decompressed sizes before allocation. Existing `ReadFile`
  decompression happens before TFile payload checks, so its allocation boundary
  needs a bounded path too; changing unrelated parsers is not the goal.
- Traverse actual sibling blocks in order; never search arbitrary payload bytes
  for token-looking integers. Counts apply to the schema's matching records;
  unknown siblings are preserved, not mistaken for texture slots or patches.
- Check counts/products/offsets using wide arithmetic before allocation and
  narrow casts. Reserve arrays once. `P*P`, string byte counts, cumulative record
  counts and nesting all need bounds tied to available bytes and explicit codec
  limits. A rendering limit of P32 must not become the binary format's definition.
- Parse order-independent metadata where possible. For patch records occurring
  before `npatches`, remember the bounded span and decode after metadata; resolve
  shader references and AS/US sizes after the containing descriptor is parsed.
- Fixed patch payload: 60 numeric bytes after the label. Preserve framed extra
  tail bytes without treating them as child tokens. Reject short records.
- Preserve float bit patterns when unchanged, including signed zero. Parser
  preservation and finite-value checks for safe rendering are separate concerns.
  Use bit-preserving scalar helpers, never int-to-float numeric conversion for
  UV scale/distance migrations or shader-index storage.
- Diagnostics identify file, token, byte offset, set/patch where applicable.
  Avoid per-patch logging on valid data. Keep one summary/warning per issue.

### Cold preservation data, not a general-purpose scene DOM

Keep labels, child order, unknown blocks and nonstandard record tails in a cold
sidecar owned by the descriptor. Ordinary zero-label fixed patches need no
individual heap object. Small ordered entries identify typed children or retained
opaque bytes. Attach unknowns to their actual container/record, not one flat bag.
Use either compact owned byte slices or one owned backing buffer with spans;
do not keep both a complete DOM and duplicate payload copies.

Preserve unknown full token IDs and opaque bodies byte-for-byte, including labels,
at root, samples, shaders, slot/UV lists, patch sets, patches and TSRE metadata.
Keep original order and optional-field presence. Recompute affected parent sizes
when writing. Unchanged binary records should retain their payload bits; the
compressed wrapper need not be byte-identical after recompression. A sparse
preservation sidecar is preferable to requiring a dirty flag at every scalar
assignment just to avoid duplicating ordinary known records.

Duplicate singleton fields are not silently merged into one. Retain their bounded
source form and issue an ambiguity diagnostic; do not permit rewriting that field
until its ambiguity is resolved. A known malformed optional extension may be kept
opaque and unavailable, whereas invalid framing or missing core height/grid data
can prevent tile loading. Use validated block ends for recovery, never heuristic
byte scanning. Unknown does not automatically mean corrupted or uneditable.

Presence of malformed/unsupported procedural metadata must not become absence:
retain the existing policy of static fallback plus refusal of unsafe procedural
editing/saving. Preserve future-version containers opaquely. The current
`readBakeMetadata()` skips unknown children; it must join the preservation path.

### Text descriptors: separate explicit milestone

The current TFile codec is binary-only; accepting UTF-16 text is not accomplished
by changing the in-memory types. Proposed full target includes BOM text input and
output through the same typed schema, with existing token-name lookup and bounded
text parsing. Keep this off the binary hot path.

Before implementing that milestone, establish fixtures for native text grammar,
especially embedded AS/US and mixed positional/nested transfer records. Do not
invent byte-array syntax from binary layouts. Preserve unknown text blocks as
balanced source spans in text-to-text saves. An unknown textual token with no
numeric ID cannot be losslessly converted to binary; refuse such conversion
instead of dropping it. Binary-to-text has the corresponding opaque-data issue.
Binary completeness can land first but must not be described as universal
binary/text conversion. This text milestone is a proposal for scope review.

## 4. External buffers and save semantics

| Data | Required handling in this task |
|---|---|
| Y/F sample buffers | Keep existing terrain editing/loading; dimensions belong to samples, not patch count |
| E/N references | Preserve optional names and external files without loading them for ordinary TSRE rendering. Retain new-tile named E/N resources even when no generated file exists; MSRE compatibility depends on the references |
| C/D references | Parse/preserve filenames; no eager ACE decode or new C/D grids without a runtime consumer |
| AS/US embedded data | Validate each against `ceil((N+1)^2/8)` after N is known; preserve all bits and labels, including padding. An anomalous bounded payload can remain opaque with a warning, not be truncated |
| Patch-set F | Distinct from sample F: optional external `P*P` bytes overriding record flags, with MSTS writer mask `0xcb` |
| Tile-owned transfers/shapes | Parse and save complete records; runtime interpretation remains explicitly unsupported |
| Procedural metadata | Keep current token IDs, ID-to-UiD map, seasonal bake revisions, validity and legacy-reader behavior; do not regenerate/hash `.pmap` as a parser side effect |

Resolve external paths with existing content-path rules and parent directory,
including batch/distant loads. `TFile::load(FileBuffer*)` stays usable without a
filesystem: parse references first, load runtime dependencies in the terrain
resource layer. Do not make every descriptor inspection read all RAW/ACE files.

For patch-set F, retain original inline flags and sidecar bytes separately;
derive effective flags once on load. Rendering, picking, holes and water must
use that effective view. If flags are edited, update the runtime view and stage
the associated byte (`effectiveFlags & 0xcb`) plus inline record consistently.
Unedited sidecars remain untouched. Do not mask the entire inline flags word:
high bits have independent meanings. A missing/invalid referenced sidecar requires
a diagnostic and explicit degraded behavior, not pretending inline flags are an
equivalent source. Inline fallback may support viewing; flag-changing edits must
not silently overwrite the missing authoritative data.

Normal edits must not delete E/N/C/D files or rename buffers. Explicit B overwrite
remains a separate replacement operation with its existing cleanup and write
guards. Changing sample dimensions requires an explicit decision about AS/US and
dependent buffers; never preserve a stale bitset as though it still matches N.
This refactor is not an in-place terrain-resampling implementation.

Serialize using a measured-size pass or scoped block writer, replacing the
hand-expanded length formulas. Check length overflow, stream status and commit
failure. Keep `QSaveFile` for `.t` and equivalent in-memory/network serialization.
Do not buffer/copy each small patch multiple times to calculate its length.

Preflight save compatibility before writing RAWs/textures. `Terrain::save()`
currently writes Y/F before committing `.t`; new validation failures must not be
discovered only after those writes. Adding patch-set F writes requires staging
and rollback of the newly coupled outputs; do not claim a single `QSaveFile`
makes the complete multi-file terrain save atomic. A wholesale route transaction
framework is outside this task.

## 5. Caller migration (required, not optional follow-up)

| Caller | Migration work |
|---|---|
| `Terrain.cpp`: load, network load, save, new tile | Content-based shader mode independent of detailed/distant context; selected-set layout; optional sample fields; safe resource paths; preserve named E/N; save preflight |
| `Terrain.cpp`: render/pushRender/water | Cached shader/material view, exact integer indices, effective flags, typed UV/bounds; no per-frame pairing detection; validate absent slots |
| `TerrainMeshBackend.cpp` and legacy `oglInit()` | Named UV input and dirty UBO refresh; replace whole `tdata` copies for procedural UV override with a local `PatchUv` selection; keep existing GPU vertex/UBO layout |
| Texture tools and `Texture::advancedCrop()` | Replace `&tdata[p*13+6]` (shader-as-float followed by UVs) with explicit UV parameters; no reinterpret-cast adapter to the new struct |
| `Terrain` UV/reset/map/flag/ErrorBias/bounds tools | Named field edits with unchanged invalidation scope; integer flags; preserve bits outside the edited mask |
| `TerrainMaterialSource.{h,cpp}` | Value snapshots with dynamic slots/UVs and explicit optional pairing; remove fixed arrays and manual pointer deduplication/deletion; preserve whole supported shader records on clone/import |
| `TerrainProceduralMaterial.cpp` | Shader keys, source extraction, palette insertion/remapping, bake shader reservation, rollback and undo currently copy raw arrays/pairs; migrate all, including map restoration after MSRE token loss |
| `TFileBakeMetadata.cpp`, `TerrainBakeCommand.cpp` | Typed/preserved metadata; no incidental large hashes; batch `.t` save must preserve every set and unsupported block |
| `ScopedBakeTFile.h` | Remove its manual ownership workaround once TFile owns values; leaving it would double-free/refer to deleted members |
| `TerrainClient.cpp`, stream/network callers | Same validation/selection/material resolution as local terrain; payload remains normal `.t`, not dumped C++ records; external patch-F transport needs an explicit supported/degraded policy |
| `TerrainTileCreationDialog.cpp` / `readLayoutInfo()` | Lightweight dimensions probe must use the same active-set choice and framing policy, without full shader/mesh allocation |
| `ContentCaseDocument.cpp` | Its separate terrain path scanner needs C/D, patch-F and nested transfer/shape paths; strings such as shader names, hashes and UiDs must not be treated as resource paths |
| Existing tests and fixture builders | Replace direct `tdata`, pointer scalars, paired maps and `itex` assignments; retain independent byte-level assertions so tests do not merely agree with the new writer |

The texture crop audit found an additional real assumption:
`Texture::advancedCrop()` currently multiplies UV steps by **16**. Its new typed
interface must accept the actual patch sample span `R=N/P`; test R8/R16/R32/R64.
Do not preserve the hard-coded factor in a compatibility adapter or "fix" stored
UVs by scaling them during parse. UVs consume raw patch-local sample coordinates;
default texture steps are `1/R`, whole-tile map increments `1/N`.

Resolve RAII ownership together with palette operations. Existing `Mat` values
can alias QString pointers after map moves; simply adding deletes to today's
destructor is unsafe. Prefer full value copies for edit snapshots and move for
parsed descriptors. Worker jobs keep immutable source snapshots, never pointers
into resizable shader vectors or the live `TFile`. Reallocation invalidates
references: caches use indices/generation and are rebuilt only on relevant edits.

## 6. Performance requirements and measurement

The design should improve allocation locality, not replace flat arrays with a
dynamic token/property lookup in rendering or sample loops.

- Direct indexed patch/UV access and cached shader resolution remain O(1).
  No string comparisons, hash building, DOM traversal, locks, reference-counted
  per-patch objects or parser validation inside per-vertex/per-sample loops.
- One contiguous patch array per set; one shader vector and small slot vectors.
  Optional/preservation data is cold and sparse. Do not build height, texture or
  geometry data for inactive sets. More sets necessarily cost their own record
  memory, but ordinary one-set files should not pay a second runtime model copy.
- Dirty tracking stays per affected patch/region. Clearing ErrorBias through the
  new API must not reintroduce per-sample expensive invalidation or full-tile
  rebuilds. Preserve the 8-byte GPU vertex layout and current draw/page batching.
- AS/US memory is proportional to actual payload, not a fixed maximum allocation.
  No E/N/C/D I/O merely to parse a descriptor. No `.pmap` hashing on load/render.
- Use a direct switch/typed schema, not a generalized reflective framework.
  Binary and text readers may share the model/writer rules without sharing an
  allocation-heavy generic syntax tree.

Before implementation, record repeatable baselines in a benchmark mode, outside
OpenGL: decompression separately; in-memory parse; write to memory; full descriptor
file load/save; repeated load/destroy to find leaks; live/peak allocation bytes.
Use warm-ups and repeated batches, reporting median and p95 plus corpus/commit/
compiler. Include P4/P8/P16/P32, detailed pairs, flat odd distant tables, AS/US,
and N256/N512/N1024/N2048. Descriptor cost normally follows **patch count and
payloads**, not N squared except embedded masks; do not conflate it with RAW or
mesh generation.

Then compare visible-frame CPU submission, UV/texture edits, height brushes,
procedural undo/save and tile streaming on the same routes/settings. Parser
timings do not prove renderer performance. Proposed investigation thresholds:
repeatable >10% regression in warmed ordinary-descriptor parse/write, >5% in
frame/edit CPU cost, or unexplained ordinary-descriptor memory growth. These are
review triggers, not excuses to reject correctness or trust noisy one-off FPS.
Record absolute milliseconds/bytes too; fix large regressions before rollout.

## 7. Implementation stages and verification gates

1. **Baseline and fixtures.** Inventory and benchmark the current implementation;
   create synthetic complete files and a read-only stock corpus manifest. Keep
   this separate from migration so results can be compared.
2. **Value model and binary codec.** Implement complete known schema, labels/order/
   opaque preservation, typed patch/shader arrays, checks and writer. Exercise it
   in tests before changing live callers. Temporary old/new readers are acceptable
   in the test harness, not two authoritative mutable models in a loaded tile.
3. **Engine/editor integration.** Migrate the complete caller table, material
   pairing/flat views, effective patch-F, ownership, undo and batch/network paths.
   Keep ordinary existing tiles editable. Do not declare this stage done merely
   because TFile round trips in isolation.
4. **Compatibility and performance gate.** Run CPU and GL suites, inspect output
   independently, test stock distant/ordinary terrain and current procedural
   routes. Resolve paged distant handling without hiding it behind legacy mode.
5. **Text codec milestone, if accepted in scope.** Fixture-led text parsing/writing
   and supported conversion; separate tests and explicit unknown-data limitations.

Required regression fixtures/checks:

- Flat `TexDiff` tables of 1/3/9 shaders with references to first/middle/last;
  detailed pairs with unequal paired texture names, upper-half references and
  flags; unknown shader names; >2 slots and UV records; safe zero-slot handling.
- Both paired and flat distant tiles; even-sized flat tables; identical shader
  arrays classified identically in `TILES` and `LO_TILES`. Test add/clone/delete
  and stale auxiliary-reference repair in paired mode in both locations, without
  conflating TSRE support with native MSTS compatibility.
- Paired upper-half references with/without `0x200`, already-primary references
  carrying that flag, and truly out-of-range indices. Repair is idempotent,
  preserves unrelated bits, and explicit save/reload retains primary references;
  flat upper-half references remain unchanged. No editor tool can create a direct
  auxiliary assignment. These repair cases intentionally differ from byte-exact
  no-op preservation tests.
- Multiple patch sets with different P, noninteger float distances and distinct
  water/UV/flags; same selected last set after reload; earlier sets unchanged by
  active-only UV editing; all references remapped correctly on palette edits.
- Every native token described by the research, including nested transfer shader,
  signed shape bounds and rotation bits; no normalization of endpoint order.
- Unknown full IDs at every nesting level, labels, fixed-record tails, optional
  absence/empty values, reordered children, duplicate fields and unknown extension
  versions. Preservation assertions compare bytes/tree, not just render output.
- Truncated/overflowing lengths/counts, invalid UTF-16 framing, invalid header,
  decompression limits, absent required fields, missing resources, repeated load
  and failed reload. No crash, partial overwrite, stale state or unbounded alloc.
- AS/US N-dependent lengths and tail bits, malformed-but-bounded retention;
  patch-F precedence, masking, read-only fallback, save failure/rollback; E/N
  references survive new-tile creation and overwrite as previously tested in MSRE.
- One/four-float water forms; patch integer indices above float's exact-integer
  range remain exact in the codec even when runtime validation rejects them;
  bit-exact float round trips without treating an invalid value as renderable.
- Static UV reset/rotate/mirror/crop/texture cloning, map UVs, hidden patches,
  gaps, water, bounds, selection and both rendering backends. Include R8 and R64.
- Procedural conversion, paint/fill/pick, catalogue import, restore existing pmap,
  two-second undo segmentation, async jobs, seasonal and incremental bake saves;
  no flicker/cache thrashing or new large input hashes caused by representation.
- Batch descriptor-only baking and network stream round trips preserve unrelated
  data. UI layout probe agrees with the full parser's active set.

Use temporary copies of all locally available stock MSTS route Tiles/Lo_tiles,
CMK and the custom profile/procedural fixtures. Include BNSF Scenic for the recorded
paged crash. Never save over original research or user routes during automated
tests. Real proprietary fixtures stay out of git; synthetic fixtures can be tracked.

Keep the existing `tokens`, `terrain-grid`, `terrain-material`, relevant brush/
edge/transfer and GPU suites passing; add a dedicated `terrain-tfile` suite and
parser benchmark. The suites now exercise both the independent preservation
codec and production `TFile`; see the runtime report for commands/results.
Do not infer MSTS/ORTS visual compatibility solely from TSRE reload.

## 8. Agreed direction

- Adopt contiguous typed records plus cold preservation metadata, not a runtime DOM.
- Preserve all shader/patch-set records; last active set; content-based flat/paired
  detection and editing, including both shader modes in distant terrain.
- Complete binary round-trip support and migrate callers before adding new runtime
  features. Rare native transfers/shapes and E/AS algorithms remain separate work.
- Keep text completeness as an explicit later milestone, with grammar fixtures
  required before implementation rather than guessing unsupported syntax.
- Preserve safely framed unsupported data for viewing/round-trip. Restrict only
  operations whose integrity cannot be guaranteed; no blanket rejection of
  unfamiliar but well-framed terrain, and no silent dropping of data on save.

Implementation was authorized on 2026-09-21. The checklist at the top separates
implemented binary/runtime work from remaining interactive acceptance and the
explicitly separate text-format milestone.
