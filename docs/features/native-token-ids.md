# Native SIMIS token IDs in TSRE

Implemented on `feature/native-token-ids`, based on consolidated `main`
`9f389f08450411614256d9f3e018225ddc62584a`. The namespace assignment for ORTS/TSRE
is an interoperability proposal, not an upstream Open Rails allocation.

## Identity and allocation

`TS::TokenId` is an unsigned 32-bit value, on disk and in memory:

```cpp
// Complete ID = (namespace << 16) | localId.
// TSRE namespace = 6.
TS::TokenId id = TS::Static; // 0x00040003, bytes 03 00 04 00 on disk
```

**Why start extension file tokens at local ID 2048?** Legacy Open Rails and
derived tools discard namespaces and use either the local ID or local ID +300.
The reserved gap avoids collision with their inspected existing enum. It is an
allocation rule, not an offset to apply at runtime and not a promise that an
old strict parser will accept unknown children.

| Namespace | Ownership | Rule |
| --- | --- | --- |
| 0 | Kuju Core | Native IDs unchanged, including terrain and shape tokens |
| 1–3 | Existing Kuju applications | Not available for TSRE allocation |
| 4 | MSTS Train | Native complete IDs, including world objects |
| 5 | Proposed ORTS | File extensions begin at `0x00050800` |
| 6 | Proposed TSRE | Subranges below |

| TSRE purpose | Local range | Complete range |
| --- | --- | --- |
| Network/app messages | 0–2047 | `0x00060000–0x000607FF` |
| World objects/parameters | 2048–4095 | `0x00060800–0x00060FFF` |
| Terrain file extensions | 4096–6143 | `0x00061000–0x000617FF` |
| Unallocated | 6144–65535 | `0x00061800–0x0006FFFF` |

IDs are explicit and stable: append without renumbering published assignments.
The generic reader accepts the entire uint32 range, including unassigned and
high-bit namespaces; this table is not a decoding whitelist.

| Name | Complete ID |
| --- | --- |
| `TSRE_Requested_Terrain_tFile` | `0x00060001` |
| `TSRE_Requested_Terrain_RawFile` | `0x00060002` |
| `TSRE_Requested_Terrain_FtFile` | `0x00060003` |
| `TSRE_Terrain_tFile` | `0x00060004` |
| `TSRE_Terrain_RawFile` | `0x00060005` |
| `TSRE_Terrain_FtFile` | `0x00060006` |
| `TSRE_Requested_TD_File` | `0x00060007` |
| `TSRE_Requested_TD_Lo_File` | `0x00060008` |
| `Ruler` | `0x00060800` |
| `ShapeTemplate` | `0x00060801` |
| `TSRETerrainMaterialBuffer` | `0x00061000` |
| `TSRETerrainBakedMaterial` | `0x00061001` |
| `TSRETerrainMaterialMap` | `0x00061002` |
| `TSRETerrainBakedMaterials` | `0x00061003` |

Network names intentionally retain underscores; new file blocks use the
`TSRETerrain...` spelling. Existing native names/case conventions are unchanged.
ORTS extension names use the draft's explicit `0x00050800–0x0005080E` assignments
in `TS.h`; they are not allocated in TSRE's range.

## Native numbering corrections

Both clean MSTS 1.4 and Bin 1.8 executable name tables confirm the corrected
Train ordering. The supplied UTILS `loadstr.hdr` misplaced
`EngineBrakesControllerGraduatedSelfLapLimitedHoldingStart`, causing 34 incorrect
name/ID pairs in the old TSRE copy. The native value is `0x0004020E`; the adjacent
33 entries are corrected too. This is not an arbitrary reordering.

Sparse forms such as `Dyntrack`, `Gantry`, `Pickup`, `Siding`, `CarSpawner` and
`Transfer` now have their native namespace-4 IDs. The invented `...2` helper
aliases are removed. There is one canonical ID/name lookup entry per identity.
`terrain_sample_usbuffer` (282) now has its recovered symbolic name; its opaque
payload behavior is unchanged.

## Reader/writer API

For the detailed API, ownership/cursor rules, old/new loading examples,
recovery-first integration and block-writing examples, see
[FileBuffer usage](file-buffer.md). Checked framing does not itself provide
recovery; some current consumers still abort whole-file loads on parse errors.

`FileBuffer::getToken()` reads an unaligned-safe little-endian uint32. There is
no `tokenOffset`, `setTokenOffset`, world-file subtraction, +300 adjustment or
namespace inference from filename. Use `TS::name(id)` for non-mutating lookup,
or `TS::describe(id)` for name, complete hex ID, namespace and local ID.
No token value is reserved as an EOF/error sentinel.

```cpp
const auto child = data->readBlock(); // validates ID, length and UTF-16LE label
FileBuffer::ScopedLimit scope(*data, child.end);
data->skipLabel();
switch (child.id) {
case TS::UiD:
    uid = data->getUint(); // positional value, not another token
    break;
default:
    break; // unknown payload is opaque
}
data->off = child.end;
```

The length includes `uint8 labelLength + UTF-16LE label + payload`, but excludes
the 8-byte ID/length header. `readBlock()` leaves the cursor at the label byte;
`payload` identifies the byte after the label. `readBlockEnd()` serves callers
that already consumed the ID. `ScopedLimit` bounds nested headers and legacy
positional getters against their current parent. Invalid input raises
`FileBuffer::ParseError`, separate from token data; production terrain, world
and binary shape entry points catch/report it.

`Simis::Block` combines expected-token sibling search, scoped payload bounds and
an automatic end seek. `findToken(TS::...)` retains its historical cursor
convention: on success the next field is the length, not the label. Use the
helpers only where the schema says children follow. Do not scan integers,
floats, texture indices or raw bytes as if they were tokens.

Write IDs as `quint32(TS::...)` with little-endian `QDataStream`, not a native C++
enum size or an added base. `TS::pack`, `nameSpace` and `localId` are utilities
for allocation/inspection; ordinary dispatch uses full enum names directly.

## Terrain compatibility

Current top-level TSRE terrain blocks are children of `terrain_samples` (139):

| Block | Payload after label | Purpose |
| --- | --- | --- |
| `TSRETerrainMaterialBuffer` | uint16 character count, UTF-16LE string | `.pmap` filename |
| `TSRETerrainBakedMaterials` | uint32 version (2), uint64 shared revision, child records | Per-season bake metadata |
| `TSRETerrainMaterialMap` | uint32 count, repeated `(uint32 localMaterialId, uint32 UiD)` | At most 256 pairs, local IDs 0–255, nonzero route-library UiDs |

Inside the plural version-2 container, `TSRETerrainBakedMaterial` entries contain
a UTF-16 variant, uint64 baked revision, uint32 resolution and three UTF-16
signature strings. Each string has a uint16 character count. The old root-level
string marker is no longer written; its existing reader is retained. See
[procedural seasons](terrain-procedural-seasons.md) for current semantics.

The bitmap's 8-bit material IDs, library UiDs, `.pmap`, bake logic, ACE textures,
patch records and AS/US bytes are not renumbered. Invalid-presence behavior is
retained. TSRE's existing shader half-list split, auxiliary-index folding and
last-patchset-wins behavior are also unchanged.

**Prototype compatibility intentionally ends here.** Old 100009/100010/100011
blocks are unknown and skipped, not converted. Recreate the three experimental
tiles. No old-ID aliases, dual writes or migration tool exist. Existing test
tiles were not modified by this implementation.

Legacy ORTS's strict `terrain_samples` reader can still reject these children.
The separate ORTS full-ID patch does not itself change that policy. MSTS's
reviewed unknown-child skipping is different. Namespace allocation avoids
misidentification; it does not implement procedural rendering or universal
legacy fallback.

## World extension inventory and codec limits

The current `set(QString, FileBuffer*)` readers and save methods were inspected;
UI/app setters such as `ref_filename`, `update_type`, `x` and `z` are not file
token declarations. There were no additional unallocated TSRE world-file names
in this source snapshot beyond the existing `Ruler`/`ShapeTemplate` entries.

| Name / owner | ID | Parent and data | Current binary status |
| --- | --- | --- | --- |
| `Ruler` / TSRE | `0x00060800` | `Tr_Worldfile`; ordinary object properties plus `Points`, optional `ShapeTemplate` | Unicode read/write only; binary factory safely skips the object |
| `ShapeTemplate` / TSRE | `0x00060801` | World object or Ruler; one template-name string | Unicode reader only; binary setter not implemented |
| `Points` / reused Core `points` | 7 | Ruler; count followed by `Point` children | Same count/child framing as Core points; Ruler-specific binary codec not implemented |
| `Point` / reused Core `point` | 2 | Ruler `Points`; three float32 coordinates | Same positional representation; Ruler's Z conversion belongs in its future codec |
| `ORTSListName` / ORTS | `0x00050800` | `CarSpawner`; one string | Unicode read/write; binary setter not implemented |
| `ORTSSoundFileName` / ORTS | `0x00050801` | `LevelCr`; one string | Unicode read/write; binary setter not implemented |

Registration is not binary round-trip support. There is still no universal
binary `.w` writer/converter. Unknown object/property payloads are skipped, not
retained for lossless rewriting. Do not enable binary export for the unsupported
extensions merely because their IDs are now registered.

Binary `.ws` uses `Tr_Worldsoundfile` (`0x00040058`), not `Tr_Worldfile`
(`0x0004004B`). The sound-source/region factories now connect to their existing
binary setters through the newly registered native `Soundsource` (`0x00040043`)
and `Soundregion` (`0x00040044`) forms. Both executable tables confirm these
names/IDs, absent from the old ORTS-derived enum. This does not complete
`SoundRegion`'s binary schema: its
rotation/track-type/item fields still lack specialized binary handling.

The shape reader still renders only its existing supported geometry/controller
types and one LOD-control tree. Unknown primitive/controller kinds are skipped;
token cleanup does not add their renderers. Block labels and lengths are now
honored instead of relying on fixed 9-byte header skips.

## Network policy

Update client and server together. `NetworkToken::write/read` is shared by their
actual binary paths: `B`, uint32 complete ID, then the unchanged reserved-size
DWORD, reserved byte, tile coordinates and payload. Existing Unicode requests
and authentication remain unchanged. File blobs keep their own file IDs.

A new peer diagnoses incoming legacy 100001–100008 IDs as incompatible instead
of silently translating them. Unknown namespaces are rejected without low-word
matching. There is no legacy transport adapter or negotiation subsystem, and
old binaries cannot acquire the new diagnostic automatically. Deploy matched
versions on both ends; a new outer header cannot make an old peer understand
new terrain contents.

## Verification

See [token-ID and parser implementation results](../tasks/core/native-token-ids-and-binary-parser-implementation.md)
for commands, measured results and remaining tests. The portable fixtures are
generated in temporary directories; they need no proprietary files, Windows
registry, Wine or running network server.
