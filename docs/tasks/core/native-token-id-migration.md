# TSRE local work plan: native 32-bit SIMIS token IDs and extension allocation

Status, 2026-09-09: the native-ID implementation is in commit `8c85bb1` on
`feature/native-token-ids`. That commit also contains binary-parser refactoring;
its whole-file rejection/rollback behavior is **not accepted as TSRE's
recovery-first loading policy**. The plan below is retained as design history,
not a claim that the entire parser integration is complete.
See the [consumer inventory and follow-up TODOs](../../features/file-buffer.md#111-status-and-follow-up-todos).
The later QuadTree correction is limited to two named writer IDs; its reader
is intentionally unchanged.
See [implementation results](native-token-ids-and-binary-parser-implementation.md) and
[the current API/allocation](../../features/native-token-ids.md). The sections below
retain the reviewed design and describe the pre-migration baseline where noted.
Reworked 2026-09-08 against current TSRE `main`, commit
`9f389f08450411614256d9f3e018225ddc62584a` (merged ACE converter and route-wide
procedural material work). Implementation and fixture verification were recorded
in `8c85bb1`; the subsequent review identified parser scope and recovery-policy
issues. Use the combined report and current TODOs for follow-up, rather than
treating this historical plan as another implementation handoff.
This TSRE task now belongs in `docs/tasks/core`, not the MSTS research category.
The separate research workspace's `reports/tsre-full-token-id-migration-task.md`
is an earlier copy; this repository document and the linked follow-up checklist
are the current references. Paths in the source inventory refer to TSRE.

Decisions incorporated from the latest review:

- Replace numeric SIMIS token IDs at TSRE use sites with enum names, including
  unchanged Core IDs in terrain and shape code, not only offset-related world IDs.
- No runtime backward migration for the prototype terrain token numbers. Do not
  add old-ID reader aliases or a converter. A separate, user-approved post-merge
  asset task may update the small number of experimental tiles in place.
- Keep underscored network/app message names; use PascalCase with the `TSRE`
  prefix for the three renamed terrain file blocks, so the roles remain
  visually distinct. Update their enum names and textual name-table entries.
- Current `main` adds a third terrain token, `TSRE_Terrain_Material_Map`; include
  it in the allocation and tests. This plan applies the same prototype-only
  treatment to that current terrain-token set, explicitly for the user's review.
- Preserve the merged main-tree functionality. The document-only review gate is now passed;
  implementation does not authorize test-tile rewrites, branch merges or publishing.

## 1. Outcome and scope

Make TSRE use the complete native 32-bit token ID consistently in its enum,
name lookup, binary readers/writers and network dispatch. Remove the world-file
offset adaptation. Allocate existing TSRE extensions in namespace 6, with the
world/terrain ranges below. Replace hard-coded token numbers with enum names
where TSRE reads, compares, stores as IDs or writes them. Make a clean switch
to the new prototype terrain IDs without a backward-compatibility path.

Ordinary MSTS binary files must keep their original bytes and interpretation.
This changes TSRE's internal numbering, not the MSTS file format. Existing
native MSTS Unicode spelling, payload layouts, coordinate conventions, flags, terrain maps,
route-wide material UiDs, typed selection history and ACE behavior are not to
be redesigned by this task.

This is the prerequisite for clean binary extension support, not authorization
to build a universal Unicode-to-binary converter or a complete binary `.w`
writer. Registering a token does not implement its payload reader/writer; track
those capabilities separately. Do not turn allocation of world IDs into a
silent claim that every current Unicode world extension can round-trip in binary.

**Windows-host filesystem, registry, process and application access always
requires explicit user approval.** Use synthetic fixtures and Linux-local
sources/data first. Do not invoke Windows executables or mounted host tools to
run Git, compilers, converters or captures without that approval. No Windows
access is needed for the core acceptance tests. Do not submit changes to ORTS
or start Wine/MSRE captures as an implicit part of this task.

## 2. What already exists, and what does not

The ORTS side is a tested, local review draft, **not an upstream-merged feature**:

- Package in the MSTS workspace: `share/orts-token-namespaces/`.
- Exact ORTS base: `unstable` at `e869e9b7006d76a97071163748fc426f5a665617`.
- Draft removes ORTS's world-dependent 300 offset and upper-word-as-flags
  interpretation, updates its enum and world dispatch, and adds tests.
- Its documented headless verification passed 209 tests in Debug and Release,
  including 24 namespace tests, against actual parser/format sources. This was
  not a full simulator build or real-route certification.
- Its audit matched all 1,434 shared enum/executable names against both clean
  MSTS 1.4 and Bin 1.8. The executable ordering, not the uncorrected UTILS header,
  is the numbering reference.
- It did not modify TSRE or implement procedural terrain in ORTS.

The inspected pre-migration TSRE `main` had `Static = 303`, `Tr_Worldfile = 375`, a signed
`int` token map, and two `setTokenOffset(261844)` calls. That baseline had no
namespace migration. It wrote three procedural terrain IDs:

- `TSRE_Terrain_Material_Buffer` (100009): map filename.
- `TSRE_Terrain_Baked_Material` (100010): bake marker string.
- `TSRE_Terrain_Material_Map` (100011): local material ID to route-library UiD map.

The old handoff predated the last item. All three are children of
`terrain_samples` (139). The user explicitly waived a compatibility migration
for the experimental terrain files; their mere existence is not a reason to
add legacy support. Existing ordinary MSTS binary compatibility remains required.

Research references, all relative to the separate MSTS workspace:

- `share/orts-token-namespaces/README.md`: allocation rationale and corrections.
- `share/orts-token-namespaces/VERIFICATION.md`: observed tests and limits.
- `share/orts-token-namespaces/orts-full-token-ids.patch`: ORTS-only patch.
- `share/orts-token-namespaces/audit_ids.py`: reproducible enum/native-ID audit.
- `analysis/openrails-token-namespace-work/Source/Orts.Parsers.Msts/TokenID.cs`:
  corrected reference enum; `Docs/SIMIS Token IDs.md` in that same worktree is
  the format proposal.
- `reports/msts-binary-token-namespaces.md`: MSTS executable evidence and the
  distinct ORTS/TSRE adaptation paths.

These research files are available to us locally. Use the reference enum and
audit to check our implementation; do not apply the C# patch to TSRE. Generated
TSRE tests must still work in a separate clone without `/root/msts`, proprietary
files or the ORTS worktree. Give the later testing agent the required portable
fixtures and commands, not a dependency on our research directory.

## 3. Why the offset must disappear

The binary block header is:

```text
uint32 little-endian complete token ID
uint32 little-endian body length
body: uint8 label-character count, UTF-16LE label, then payload/children
```

The body length includes the label framing but excludes the 8-byte ID/length
header. Not every payload is a child-token sequence; many contain positional
integers, floats or arrays. Do not reinterpret those payload values as IDs.

```cpp
// Complete ID = (namespace << 16) | localId.
// Each component is an unsigned 16-bit quantity.
// TSRE namespace = 6.
```

For example, native `Static` is `0x00040003`. Old ORTS represents it as
`3 + 300 = 303`. TSRE currently subtracts `261844 = 262144 - 300` from the
complete native world token, producing the same flattened 303. That arithmetic
only fits the assumed Train namespace; it corrupts IDs from another namespace
inside the same file. The high word is not a block flags field.

After migration, `Static` stays `0x00040003` from input through dispatch. No
reader adds 300, subtracts 261844, masks away the namespace, or infers the
namespace from `.w` versus `.t`. Real payload fields such as `StaticFlags`
remain unchanged. A world file can contain Core, Train and extension IDs.

The 2048 extension boundary is an allocation policy for old readers, not a
runtime offset: the reviewed legacy ORTS enum ends at 1564. Local IDs >=2048
avoid its known collisions both directly and after the world-file +300 step.
This does not override strict-container behavior or promise compatibility with
every future fork. MSTS itself compares complete IDs in the reviewed paths.

## 4. Agreed namespace and TSRE allocation

### Namespace ownership

| Namespace | Complete range | Policy |
| --- | --- | --- |
| 0 | `0x00000000–0x0000FFFF` | Kuju Core; preserve existing terrain/shape/environment IDs |
| 1–3 | `0x00010000–0x0003FFFF` | Existing Kuju assignments; do not allocate TSRE features here |
| 4 | `0x00040000–0x0004FFFF` | MSTS Train, including world objects and shared application fields; native IDs |
| 5 | `0x00050000–0x0005FFFF` | Proposed ORTS namespace; file extensions start at local 2048 |
| 6 | `0x00060000–0x0006FFFF` | Proposed TSRE namespace; allocation below |

Namespaces 5 and 6 remain our interoperability proposal, not upstream approval.
Unlisted namespaces must still survive generic token reading unchanged. A
registry of known names must not become a whitelist for reading block headers.

### Namespace 6 subranges

| Purpose | Local IDs | Complete IDs | Capacity |
| --- | --- | --- | ---: |
| Internal/network | `0–2047` | `0x00060000–0x000607FF` | 2,048 |
| World objects and parameters | `2048–4095` | `0x00060800–0x00060FFF` | 2,048 |
| Terrain extensions | `4096–6143` | `0x00061000–0x000617FF` | 2,048 |
| Unallocated | `6144–65535` | `0x00061800–0x0006FFFF` | 59,392 |

This is the latest agreed split. Earlier conversational terrain proposals at
`0x00060802/03` or `0x00060900/01` are superseded; they are not evidence that
files using those IDs exist. Do not add speculative read aliases for them.

Ranges organize allocation only. Do not infer a token's layout from its range,
renumber published IDs when a range fills, or allocate the same semantic token
again just because another file type uses it. Preserve existing Core/Train IDs
when reusing their actual semantics. Newly owned world parameters get the next
free world ID; inventory our current source rather than assuming there are
only `Ruler` and `ShapeTemplate`.

### Naming convention

Keep the intentional distinction between network/app messages and new file
blocks:

- Network/app messages retain their existing underscored names, for example
  `TSRE_Requested_Terrain_tFile` and `TSRE_Terrain_RawFile`. Their numeric IDs
  change as planned, but their symbolic names do not.
- The three terrain file blocks become `TSRETerrainMaterialBuffer`,
  `TSRETerrainBakedMaterial` and `TSRETerrainMaterialMap`, consistently in the
  enum and the textual name registry. Use this PascalCase/`TSRE`-prefix style
  for newly named TSRE file extensions.

The prefix identifies ownership in Unicode, which has no numeric namespace
field. The naming difference is for readers of the code, not a runtime
dispatch rule: binary identity still comes from the complete uint32 ID.
Renaming a symbol alone does not alter its assigned binary representation.

Keep existing MSTS and ORTS spellings, including their historical case and
underscore differences. Preserve the established `Ruler`/`ShapeTemplate`
names and unrelated existing catalogue keywords; this is not a general
rename of all TSRE text formats. Old underscored terrain names below describe
the source snapshot, not the desired canonical names after implementation.

### Initial TSRE assignments

```cpp
// Network-only. Zero is left unused by this allocation, not forbidden.
TSRE_Requested_Terrain_tFile   = 0x00060001,
TSRE_Requested_Terrain_RawFile = 0x00060002,
TSRE_Requested_Terrain_FtFile  = 0x00060003,
TSRE_Terrain_tFile            = 0x00060004,
TSRE_Terrain_RawFile          = 0x00060005,
TSRE_Terrain_FtFile           = 0x00060006,
TSRE_Requested_TD_File        = 0x00060007,
TSRE_Requested_TD_Lo_File     = 0x00060008,

// World: preserve the two existing assignments in the ORTS draft.
Ruler                        = 0x00060800,
ShapeTemplate                = 0x00060801,
// Additional world objects/parameters: allocate from 0x00060802.

// Terrain: the agreed new range.
TSRETerrainMaterialBuffer    = 0x00061000,
TSRETerrainBakedMaterial     = 0x00061001,
TSRETerrainMaterialMap       = 0x00061002,
// Additional terrain tokens: allocate from 0x00061003.
```

Use explicit stable values and a uint32-capable type. These are the full enum
and serialized values: do not add a namespace or 2048 again at write time.
Internal message IDs must not be written as route-file extension blocks.

If importing ORTS extension names, use the draft's assignments, not namespace 6:
`ORTSListName = 0x00050800`, followed at successive explicitly recorded IDs by
`ORTSSoundFileName`, `ORTSPantographToggle3`, `ORTSPantographToggle4`,
`ORTSCraneSound`, `ORTSMaxStackedContainers`, `ORTSStackLocations`,
`ORTSStackLocationsLength`, `ORTSPickingSurfaceYOffset`,
`ORTSPickingSurfaceRelativeTopStartPosition`, `ORTSGrabberArmsParts`,
`StackLocation`, `MaxStackedContainers`, `Length`, and `Flipped = 0x0005080E`.
Adding their names does not require implementing new ORTS features in TSRE.

## 5. Exact migration of the inspected MSTS enum

Do not just set the first Train enum entry to a new base. The inspected `TS.h`
has explicit assignments throughout, sparse object IDs, arbitrary tail IDs and
one historical ordering error. Generate/review the mapping by symbolic name.
The following old numbers were rechecked against the current `main` snapshot.
If main moves again before implementation, compare by name and recheck the diff;
do not silently discard newer entries or return to the old ACE feature branch.

### Core namespace

Preserve the existing Core assignments, including `error` (0), `terrain` (136),
`terrain_samples` (139), `terrain_nsamples` (140), and
`terrain_sample_asbuffer` (281). Add the recovered enum/name entry
`terrain_sample_usbuffer` (282) so its current numeric dispatch, opaque-buffer
order storage and writer can use that name. Its ID and opaque payload behavior
do not change. This migration does not insert a 2048 gap into MSTS's own
namespaces. Unchanged values are not an exception to the enum-name cleanup.

### Train object/form IDs

| Canonical name | Old TSRE value | Complete native value | Old helper alias to retire/canonicalize |
| --- | ---: | --- | --- |
| `Static` | 303 | `0x00040003` | — |
| `TrackObj` | 305 | `0x00040005` | — |
| `Dyntrack` | 1542 | `0x00040006` | `DynTrack2` (306) |
| `Forest` | 308 | `0x00040008` | — |
| `CollideObject` | 311 | `0x0004000B` | — |
| `Wagon` | 1546 | `0x0004000D` | — |
| `Engine` | 1547 | `0x0004000E` | — |
| `Signal` | 317 | `0x00040011` | — |
| `Gantry` | 1544 | `0x00040038` | `Gantry2` (356) |
| `CarSpawner` | 1540 | `0x00040039` | `CarSpawner2` (357) |
| `Pickup` | 1545 | `0x0004003B` | `Pickup2` (359) |
| `Platform` | 360 | `0x0004003C` | — |
| `Siding` | 1541 | `0x0004003D` | `Siding2` (361) |
| `LevelCr` | 362 | `0x0004003E` | — |
| `Transfer` | 1543 | `0x0004003F` | `Transfer2` (363) |
| `Speedpost` | 364 | `0x00040040` | — |
| `Hazard` | 365 | `0x00040041` | — |

Remove redundant binary dispatch alternatives. Prefer one canonical enum/name
entry per binary identity. If source-level aliases are retained temporarily,
keep them out of the canonical ID-to-name registry and do not add duplicate
`switch` cases. Preserve accepted real Unicode spellings and case handling;
helper names ending in `2` must not become newly emitted file token names.

### Train load-string run and the brake correction

For names from `Tr_Worldfile` (old 375) through `DEMPath` (old 1539), the usual
mapping is `(4u << 16) | (oldValue - 300u)`, **except**:

1. `EngineBrakesControllerGraduatedSelfLapLimitedHoldingStart` (old 793)
   becomes `0x0004020E`.
2. The 33 names with old values **794 through 826 inclusive** become
   `(4u << 16) | (oldValue - 301u)`.

Both MSTS 1.4 and Bin 1.8 executable tables confirm this ordering. The supplied
UTILS `loadstr.hdr` and the copied enum misplaced that one engine-brake token;
blindly adding a base perpetuates 34 incorrect name/ID assignments. Use these
formulae only as a migration generator, not as runtime decoder arithmetic.

Required spot checks:

| Name | Complete native ID |
| --- | --- |
| `Tr_Worldfile` | `0x0004004B` |
| `Tr_Worldsoundfile` | `0x00040058` |
| `FileName` | `0x0004005F` |
| `Position` | `0x00040061` |
| `StaticFlags` | `0x00040068` |
| `UiD` | `0x0004006C` |
| `TrainBrakesControllerGraduatedSelfLapLimitedKeepPsiStart` | `0x000401ED` |
| `EngineBrakesControllerGraduatedSelfLapLimitedStart` | `0x0004020D` |
| `EngineBrakesControllerGraduatedSelfLapLimitedHoldingStart` | `0x0004020E` |
| `EngineBrakesControllerGraduatedSelfLapLimitedKeepPsiStart` | `0x0004020F` |
| `DEMPath` | `0x000404D7` |

## 6. Source inventory and implementation work

Line numbers will drift; use the symbols and searches, not mechanical edits by
line number. Preserve the current route-wide material catalogue, local-ID/UiD
mapping, save/cache/undo behavior, ACE library and converter integration.

| Area | Observed state | Required work |
| --- | --- | --- |
| [TS.h](../../../src/tsre/fileFunctions/TS.h), [TS.cpp](../../../src/tsre/fileFunctions/TS.cpp) | Explicit flattened enum; `unordered_map<int, const char*>`; stale “not used” comments | Apply native mapping, allocate extensions, document ranges, remove stale comments and canonicalize aliases; use unsigned 32-bit token keys |
| [FileBuffer.h](../../../src/tsre/fileFunctions/FileBuffer.h), [FileBuffer.cpp](../../../src/tsre/fileFunctions/FileBuffer.cpp) | `getToken()` reads an int and subtracts `tokenOffset`; `findToken(int)` uses `getInt()` | Remove offset state/setter and subtraction; return the complete little-endian uint32, preserve it in searches, and bound touched header/skip reads |
| [Tile.cpp](../../../src/tsre/world/Tile.cpp) | Both `load()` and `loadWS()` set 261844 and test literal 375; nested view-database parsing uses tokens | Remove both offset calls, use named full IDs, review the distinct `.w`/`.ws` root checks, update local token variables and length-safe unknown skipping |
| [WorldObj.cpp](../../../src/tsre/world/objects/WorldObj.cpp) and subclasses | `createObj(int)` has canonical/helper alternatives; virtual `set(int, FileBuffer*)` methods compare TS names | Propagate token types through declarations/overrides/callers, remove duplicate binary identities, preserve object behavior and canonical textual names |
| [SignalObj.cpp](../../../src/tsre/world/objects/SignalObj.cpp) | Nested signal-unit token headers use `getToken()` | Verify nested alignment and full IDs; do not mistake nested IDs or lengths for payload flags |
| [TFile.cpp](../../../src/tsre/world/TFile.cpp), [TFile.h](../../../src/tsre/world/TFile.h) | Direct `getInt()` token reads; Core numeric switches/writes; numeric AS/US order entries; three TSRE children in `get139()` and writer | Replace token literals with names, keep native Core values, use full-ID types, switch all three extension IDs; no prototype aliases; preserve mapping validation and invalid-state behavior |
| [SFile.cpp](../../../src/tsre/shape/SFile.cpp), [SFileC.cpp](../../../src/tsre/shape/SFileC.cpp) | Numeric shape-root/section/animation cases, `findToken(53)` and `temp == 56`; some reads bypass `getToken()` | Replace all proven token literals with names, verify unchanged Core semantics and locate token narrowing/file-dependent arithmetic |
| [RouteEditorClient.cpp](../../../src/routeEditor/RouteEditorClient.cpp), [RouteEditorServer.cpp](../../../src/routeEditor/RouteEditorServer.cpp) | Binary messages start with `B`, then a four-byte ID; old 100001–100008; nested terrain payloads | Upgrade both ends and wire tests; preserve envelope, sizes and payloads; decide explicit old-peer policy |
| [TerrainMaterialTestSuite.cpp](../../../src/tsre/tests/TerrainMaterialTestSuite.cpp), [terrain-material-library.md](../../features/terrain-material-library.md), other docs/fixtures | Current route-wide material tests and documentation include the third token and typed selection behavior | Add new-ID and enum-name regressions, preserve existing catalogue/undo tests, update current ID documentation; do not add a historical terrain-file migration suite |

### Mandatory replacement of numeric token IDs

The user's requirement covers **all TSRE SIMIS-token use sites**, including
Core namespace values that do not otherwise change in this migration. It is
not limited to the two world-root checks or the new terrain tokens.

| Current use | Required named form |
| --- | --- |
| `findToken(136)` in terrain loading | `findToken(TS::terrain)` |
| `case 139:` / `case 140:` in terrain dispatch | `case TS::terrain_samples:` / `case TS::terrain_nsamples:` |
| `write << (qint32)136` when emitting a terrain block ID | Explicit 32-bit write of `TS::terrain` |
| `contains(281)`, `push_back(282)` and token comparisons in opaque-buffer order | `TS::terrain_sample_asbuffer` / `TS::terrain_sample_usbuffer` |
| `data->getInt() == 71` in shape-root detection | Read a token and compare with `TS::shape` |
| `case 70:` / `case 72:` in shape sections | `TS::shape_header` / `TS::shader_names` |
| `findToken(53)` in shape primitives | `findToken(TS::primitives)` |
| `temp == 56` for a shape primitive-state token | `temp == TS::prim_state_idx` |
| Literal 375 in world-root detection | Named root appropriate to the path: `TS::Tr_Worldfile` or verified `TS::Tr_Worldsoundfile` |

Also cover numeric animation/control token dispatch, token-valued collections,
lookup keys, skipped-header validation, and any production fixture-building
helpers. Add missing recovered enum entries with verified names/IDs; do not
invent names or globally rewrite every integer that happens to match an ID.

Numeric values remain appropriate in the enum's explicit assignments, the
allocation specification and independent golden-wire tests. Golden tests should
retain literal expected IDs/bytes so they can catch a wrongly numbered enum.
Other test construction should use names where it represents ordinary token use.
Document any genuinely unidentified token value rather than silently claiming
the symbolic-name requirement is complete.

Do **not** replace payload counts, offsets, dimensions, flags, material IDs,
route UiDs, GPU selection IDs or ACE/DXT format numbers with SIMIS enum entries.
For example, the map payload's local IDs 0–255 are material selectors, not the
low word of a SIMIS token. No numeric helper-method renaming is required merely
because a method such as `get139()` contains digits; the ID use sites are the target.

### Token type and parser boundaries

- Use a common `std::uint32_t`/`quint32` token type, including enum underlying
  type, map keys, dispatch arguments and relevant locals. Do not route full IDs
  through 16-bit values or signed sentinel logic. Exercise high-bit namespaces
  even though current assigned TSRE values fit in a signed int.
- The new token read must be explicit little-endian and safe for unaligned
  bytes; the old pointer-cast `getInt()` implementation is not an appropriate
  portable specification. Change token-related reads in scope, not every numeric
  payload reader as an unrelated refactor.
- Report truncated headers/invalid lengths; do not invent a special token value
  for EOF, because all 32-bit bit patterns can be IDs. Keep parser failure state
  or a checked-return mechanism separate from token data.
- Validate child ends against the parent and input bounds, including length
  overflow and label bytes. Preserve counted collections and positional payloads.
  Unknown IDs must not produce an infinite skip loop or an out-of-bounds seek.
- `.ws` currently also compares against literal 375 (`Tr_Worldfile`), although
  its text branch handles `Tr_Worldsoundfile`. Check native fixtures/specification
  and use the correct root, normally `0x00040058`; do not blindly convert both
  literals to `TS::Tr_Worldfile` and call the path verified. Existing binary
  sound-object factory limitations must be reported, not confused with IDs.
- `TS::IdName[unknownId]` currently inserts a null mapping. Use non-mutating
  lookup with a defined unknown-ID diagnostic, preferably showing full hex ID,
  namespace and local ID. Unknown IDs must not crash logging or change the registry.
- Do not globally replace every literal 300, 375 or 2048: some are ordinary
  data, including terrain dimensions and coordinates. Replace proven token
  comparisons with names; leave unrelated math and actual flag masks intact.

### World extension inventory

Inventory the actual Unicode object/parameter readers and save methods,
including additions not listed in `TS.h`. Record each extension's owner, stable
ID, textual spelling, valid parent, positional/child-block layout, and current
binary-read/write status. Allocate TSRE-owned names in the world range and use
the existing namespace-5 IDs for ORTS-owned names.

Examples requiring care: `WorldObj` has a textual `ShapeTemplate` handler;
`RulerObj` has a textual `Points`/`Point` structure and `ShapeTemplate`, but no
corresponding specialized binary reader in this snapshot. Spelling similarity
to Core `points`/`point` is not enough to assume interchangeable payload layouts.
Reuse native IDs only where semantics/layout agree; document context-specific
serialization explicitly. Adding names to `TS.h` alone must not silently discard
such content during binary conversion. Full binary codecs may be a separately
listed follow-up; no binary `.w` export should be enabled on unsupported objects.

Do not repurpose internal `WorldObj::typeID`, selection colors, renderer IDs or
other unrelated application enums as SIMIS IDs. Preserve `Tr_Watermark`
ordering/control-record behavior; its object-model cleanup is a separate task.

## 7. Prototype terrain tokens: clean switch, no migration

The user has only a small number of testing tiles and does not need runtime
backward migration. Remove the former requirement for old-ID reader aliases,
conversion on save, mixed-old/new conflict handling, dual writes, or a
compatibility conversion tool. Updating those assets after merge is tracked as
a separately approved
[procedural token tile migration](../terrain/procedural-token-tile-migration.md).

Current main writes these three children of `terrain_samples` (139):

| Current prototype name | Current prototype ID | New canonical name | New ID |
| --- | --- | --- | --- |
| `TSRE_Terrain_Material_Buffer` | `100009` / `0x000186A9` | `TSRETerrainMaterialBuffer` | `0x00061000` |
| `TSRE_Terrain_Baked_Material` | `100010` / `0x000186AA` | `TSRETerrainBakedMaterial` | `0x00061001` |
| `TSRE_Terrain_Material_Map` | `100011` / `0x000186AB` | `TSRETerrainMaterialMap` | `0x00061002` |

The third token is new since the earlier plan. Its proposed treatment is the
same as the two originally discussed prototype tokens: use only the new ID,
with no support for its old prototype number. This does not change the agreed
network/world/terrain ranges; it consumes the next terrain slot.

Implementation requirements:

1. Change the enum, canonical name table and all three reader/writer paths together. Recognize and
   emit only the new assignments for these TSRE extensions. Old prototype IDs
   are no longer recognized as procedural terrain tokens; no global namespace-1
   remapper, low-word matching or special alias cases.
2. Do not rely on old tiles as new-reader round-trip fixtures. Use freshly
   generated fixtures for implementation verification. Any later rewrite of
   existing test or local-route assets is a separate post-merge task.
3. Preserve validation, invalid-presence handling, atomic saves, sidecar backup
   behavior and dirty-state rules for the **supported new-ID** records. Do not
   remove these existing safeguards under the heading of removing migration.
4. Keep payloads unchanged: the first token references the compressed material
   buffer by string; the second holds the existing bake-marker string; the third
   has a uint32 count followed by `(uint32 localMaterialId, uint32 routeUiD)`
   pairs. Preserve the existing maximum of 256 pairs, local IDs 0–255, nonzero
   UiDs, duplicate checks and exact-size validation.
5. Do not renumber route-library UiDs, rewrite bitmap material bytes, redesign
   the catalogue, or remove existing bake-marker/local-material compatibility
   logic. That existing semantic behavior is separate from the specifically
   waived **SIMIS token-number** migration.

Newly numbered tokens do not make an old prototype tile forward-compatible.
The accepted tradeoff is that those testing tiles are not preserved by a
production legacy-ID path. Document this clearly in the eventual release/test
notes. Do not publish claims that old 100009/100010/100011 files are migrated.

### Placement is a separate compatibility issue

The inspected legacy ORTS `terrain_samples` reader throws on unknown children;
the prepared full-ID ORTS patch intentionally leaves that policy unchanged.
Therefore it can still reject these terrain children regardless of whether
their IDs are 100009 or `0x00061000`. Reserving high local IDs only avoids
misidentification; it does not make these particular files load in old ORTS.

For this ID migration, preserve current placement and explicitly document that
limitation. If legacy ORTS fallback is also required, obtain a separate schema
decision: an agreed skippable parent/export profile or an ORTS parser change.
Do not silently relocate blocks while renumbering. MSTS's reviewed unknown-child
skip behavior is distinct from ORTS's strict-container behavior.

No changes are required here to `terrainmaterials.dat`, its positive material
UiDs or `NextUiD`, the `.pmap` magic/version, compressed local-ID plane, shader
IDs, baked ACE contents, heightmap buffers, AS/US payloads, or positional
`terrain_patchset_patch` (164) fields. Replace their enclosing numeric SIMIS IDs
with enum names where applicable, not their data. Do not insert child tokens
into the fixed positional patch record as part of this work.

## 8. Network protocol migration

Update client and server together. The user reports no independent third-party
server clients, so preserving old peer interoperability indefinitely is not a
requirement. The namespace-6 low range is available for this internal protocol.

- Emit/read the complete four-byte ID; keep the existing `B` framing and
  payload encoding. Make QDataStream integer width and byte order explicit.
- A transmitted `.t` blob is a file payload with file-token IDs, not a set of
  namespace-6 network IDs; do not translate every DWORD in a message.
- Choose and document a clear peer-version policy. Prefer a small explicit
  protocol/capability check before terrain exchange where the existing handshake
  allows it; recognize known legacy message IDs to give a clear incompatibility
  diagnostic rather than silently ignoring requests.
- Supporting old peers is optional, but any compatibility adapter must live at
  the network boundary, not inside generic `FileBuffer::getToken()`. Do not
  claim an old peer can edit a new-ID terrain blob merely because its outer
  request token was translated. Plain same-version deployment is acceptable
  when documented and tested; do not add a large negotiation subsystem.
- Preserve authentication behavior and avoid logging passwords or entire
  authentication messages when adding diagnostics.

## 9. Our implementation sequence, after plan approval

1. **User review first (completed):** the user approved implementation after
   reviewing this document and the network/file naming distinction.
2. **Current-main baseline:** after approval, start a dedicated implementation
   branch from the then-current consolidated main tree. Recheck changes since
   `9f389f0`, establish current test results, and enumerate token use sites and
   world extensions. Do not merge the parked old documentation branch as code.
3. **Registry and native mapping:** implement the token type, explicit complete
   IDs, canonical name/helper-alias policy, allocation comments and mapping tests.
4. **Coordinated parser and name cleanup:** remove offsets, replace numeric
   token use sites with enum names, and update affected dispatch/root checks
   together, including native Core terrain/shape paths. Do not ship half of
   that conversion with the old enum or old subtracting reader.
5. **Terrain and network switch:** use all three new terrain IDs without legacy
   migration, preserve their current payload semantics, update both network ends,
   and document peer/schema limitations. Generate fresh testing fixtures.
6. **Our verification:** build and run the namespace tests plus current terrain,
   route-material, shape/world and ACE tests as applicable. Investigate regressions
   here and record actual results before calling the implementation ready.
7. **Later independent testing:** only after our implementation works, prepare
   a separate focused request for the other agent: exact branch/commit, test
   commands, expected results and outstanding test cases. They are not being
   asked to implement this plan. Commit/push follows the user's authorization.

Suggested discovery commands from the TSRE repository root:

```sh
rg -n 'setTokenOffset|tokenOffset|261844|getToken\(|findToken\(' src
rg -n 'TS::IdName|TS::coreids|TSRE_Requested_|TSRE_Terrain_' src
rg -n 'DynTrack2|Gantry2|CarSpawner2|Pickup2|Siding2|Transfer2' src
rg -n '375|100009|100010|100011' src docs
rg -n 'case [0-9]+:|findToken\([0-9]+\)' src
```

Also inspect direct integer/short reads, comparisons and writes at block
boundaries, including `SFile.cpp`/`SFileC.cpp` and the AS/US token-order containers.
Classify every candidate as an ID or ordinary data; these searches produce
false positives and are a starting inventory, not proof of complete coverage.

## 10. Required tests and acceptance criteria

Use deterministic generated fixtures. No stock assets, MSTS executable, Windows
registry, running route server or remote network access should be required for
the core tests. Exercise the actual TSRE readers/dispatch, not only a replacement
test parser. Existing CMake/Qt test infrastructure may be reused or a small
CPU-focused target added; avoid making a whole GUI route load the only test.

| Area | Required evidence |
| --- | --- |
| Allocation/lookup | Exact IDs in sections 4–5; all current native Core names unchanged; all 17 canonical forms; all 34 brake corrections; unique canonical IDs/names; helper alias policy; no accidental registry insertion on unknown lookup |
| Full-width identity | Distinguish `0x00000003`, `0x00040003`, `0x00050003`, `0x00060003`; preserve `0x0006FFFF`, `0x80000800`, `0xFFFF0800`, and `0xFFFFFFFF`; no signed sentinel collisions |
| Byte order/framing | `Static` header starts with bytes `03 00 04 00`; material reference with `00 10 06 00`, bake marker with `01 10 06 00`, UID map with `02 10 06 00`; file type does not alter IDs; exercise plain and compressed SIMISA envelopes |
| Parser robustness | Nonempty labels, unknown child before a valid sibling, mixed namespaces, zero-length invalid body, truncated headers/labels, oversized/overflowing lengths and nested parent bounds; deterministic failure or correct skip |
| Ordinary files | Actual `.w` dispatch for Static, TrackObj and canonicalized sparse forms; nested properties and view-database records; distinct `.ws` root; Core terrain/shape paths unchanged; equivalent existing Unicode behavior |
| Alias/name handling | Canonical names do not depend on unordered-map insertion order; real textual spellings retain their accepted case behavior; helper aliases are not emitted as binary-converter names |
| Numeric-token cleanup | Reviewed production token searches/comparisons/switches/writes and token-valued collections use enum names, including unchanged Core values; independent literal golden IDs/bytes remain in tests; unrelated payload numbers remain unchanged |
| New terrain IDs | Fresh fixtures round-trip all three new IDs and their payloads; preserve missing/invalid-presence behavior, material UID map validation, catalogue references, `.pmap` and bake state; no old-ID read aliases, conversion-on-save or dual writes |
| Unknown/legacy boundaries | Wrong-namespace low-word matches do not become known objects; old prototype IDs are not canonical TSRE extensions; document expected strict ORTS `terrain_samples` failure instead of asserting universal compatibility |
| Network | Serialize/dispatch all eight assigned messages through real client/server encoding helpers; stable framing/payloads and nested file blobs; clear documented old-peer behavior without accidental cross-namespace dispatch |
| Existing integration | Clean full TSRE build; current terrain-material/route-library/typed-selection tests and ACE tests remain passing; current world/shape and ACE-converter suites where available; new tests discoverable and repeatable from a separate checkout |

For world extensions lacking binary codecs, test safe refusal/skipping as
appropriate and explicitly mark round-trip support absent. If a codec is added
within a small bounded follow-up, compare its binary and Unicode object state,
not just the numeric ID. Do not claim universal unknown-block round-trip
preservation when TSRE only skips unknown data on read.

We can use the locally available ORTS draft tests/audit as an independent
second-reader check. Its 209 passing tests are prior ORTS evidence, not a
substitute for our TSRE tests. Optional real-file validation must use copies;
any Windows validation needs a prepared request and explicit approval.

Acceptance checklist:

- [x] Production TSRE no longer uses `tokenOffset`, `setTokenOffset`, or 261844
  to interpret IDs; retained mentions are explanatory docs/tests only.
- [x] Complete IDs flow through token paths without narrowing, file-dependent
  offsets or upper-word-as-flags logic; real data flags are untouched.
- [x] Native mappings and the agreed extension allocations are verified, with
  no unintentional duplicate identity or unstable implicit renumbering.
- [x] Network/app symbolic names retain their underscore convention; the three
  terrain file blocks use the agreed `TSRETerrain...` spelling in both enum
  and textual name registry. Existing unrelated token names are unchanged.
- [x] Production SIMIS token use sites use enum names, including unchanged
  Core terrain/shape IDs and AS/US order entries; reviewed exceptions are only
  definitions, specifications, independent golden tests or documented unknown IDs.
- [x] All three terrain extension readers/writers and both network ends use the
  new assigned IDs. No prototype terrain-ID aliases, conversion on save, dual
  writes or migration tool have been added.
- [ ] Re-establish recovery-first behavior and verify existing native binary
  consumers against representative files. The initial fixture results did not
  establish equivalence of recovery/error handling; codec gaps remain listed.
- [x] Our tests cover real TSRE parsing and the stated negative cases; the full
  build and relevant current-main suites pass or have explicitly diagnosed blockers.
- [x] Current docs include the final assignment list, prototype/old-peer policy,
  strict-container limitation, exact test commands/results and remaining scope.

## 11. Deliverables and review gates

**Review gate completed:** the user approved implementation. The parked
`docs/tsre-token-migration-plan` branch remains an older document, not code.
Implementation is on the new `feature/native-token-ids` branch. The user has
approved a local commit and will publish the branch themselves. Existing test
tiles must not be rewritten without a separate request.

**After approval:** we produce code, generated-fixture tests, the explicit
allocation/name registry and an implementation report with base/final revisions,
affected files, actual results and remaining binary-reader/writer gaps. Update
current procedural/library docs that mention 100009/100010/100011; retain those
values where documenting the deliberate prototype compatibility break. Preserve
newer main-tree changes and do not reapply the ACE work or reset to an old branch.

**After our verification:** create a short testing-only handoff for the other
agent. It should identify the finished branch/commit and ask for independent
tests, not another interpretation of the implementation design. Commit/push or
upstream submission still requires normal user authorization. None of these
steps authorizes ORTS publication, bundling proprietary files or Windows access.

Related existing TSRE work:

- [Procedural terrain storage](../terrain/terrain-procedural-materials.md)
  and [baked fallback](../terrain/terrain-procedural-baked-fallback.md).
- [Route-wide terrain material library](../../features/terrain-material-library.md):
  current catalogue, local-ID/UiD map and prototype semantics to preserve.
- [World-file control records](../world/world-file-control-records.md):
  preserve current `Tr_Watermark` behavior; do not fold that redesign into this task.
- [ACE integration](../../features/ace-library.md): completed, separate file format;
  ACE payloads do not become SIMIS token trees because token IDs are cleaned up.
- [ACE converter](../../features/ace-converter.md): now present on main;
  preserve it without confusing ACE surface-format numbers with SIMIS tokens.
