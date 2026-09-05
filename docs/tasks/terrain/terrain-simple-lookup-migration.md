# Replace TerrainLibSimple with synthetic QuadTree lookup

Status: reviewed design; lookup migration is not implemented.
Review date: 2026-09-05.

The preliminary cleanup is implemented: `setHeight256()` is no longer a
virtual terrain-library API or a `TerrainLibQt` method. Both overloads remain
private to `TerrainLibSimple`, whose track-bed path still uses them. The simple
backend is deprecated, not removed or disabled by this cleanup.

## Objective and scope

Use the existing `TerrainLibQt` editing, edge-cache, LOD and rendering paths
also when `core.advanced.useQuadTree=false`. In that mode, discover detailed
terrain by the deterministic 2048 m filename, without requiring a populated
entry in on-disk QuadTree data. Then remove the duplicated simple backend.

User decision: fake mode must perform **no QuadTree reads or writes**, including
explicit tile creation. Missing/invalid QuadTree metadata is route damage;
this mode permits access to recoverable terrain, not a new valid route format.
QuadTree regeneration may be offered separately as an explicit repair/migration.

This is a lookup/backend-consolidation task, not a renderer replacement.
Keep both precomputed and paged mesh backends. Do not implement adaptive E/AS
triangulation, arbitrary footprints or a global terrain-memory manager here.

## Existing simple rules

See [TerrainLibSimple.cpp](../../../src/tsre/world/TerrainLibSimple.cpp),
[Terrain.cpp](../../../src/tsre/world/Terrain.cpp) and
[TerrainInfo.h](../../../src/tsre/world/TerrainInfo.h).

- `load(x,z)` constructs `Terrain(x,z)`, which uses
  `Terrain::getTileName(x,-z)`. No populated-node test controls this lookup.
- `hasDetailedTerrain()` checks the corresponding `.t` file directly.
- Missing/corrupt payloads produce an unloaded terrain, not an automatically
  generated tile. Existing failed-load caching and explicit reload matter.
- `getTerrainByXY()` only checks its resident map, even when `load=true`;
  migration should use Qt's explicit load semantics, not preserve this defect.
- `simpleTerrainLayoutSupported()` currently restricts samples/spacing to
  256/8. Synthetic lookup need not retain that obsolete restriction: all valid
  detailed profiles with a **2048 m footprint** can use Qt's common machinery.
- The simple library does not implement the separate distant-terrain methods.
- Despite ignoring QuadTree for terrain discovery, `loadQuadTree()` loads a
  real tree, and `saveEmpty()`/`createNewRouteTerrain()` update real TD metadata.
  This is deliberately NOT preserved: the new fake mode must neither read
  nor write TD/index files. This is an intentional correction of old behavior.

Do not reuse the simple map key `x*10000+z`: it can collide. Do not depend on
World `.w` files to discover terrain. Their independent 2048 m grid remains
unchanged in both modes; normal QuadTree mode still supports larger terrain.

## QuadTree dependencies in TerrainLibQt

See [TerrainLibQt.cpp](../../../src/tsre/world/TerrainLibQt.cpp) and
[QuadTree.h](../../../src/tsre/world/QuadTree.h).

| Consumer | What it needs | Migration requirement |
|---|---|---|
| `getTerrainByXY`, `load`, `reload`, `isLoaded` | Name ID and `TerrainInfo` | Synthetic identity and level-1 placement, independent of population |
| `terrainQt` / `terrainQtLo` | Stable nonzero unsigned cache key | Keep identity stable for the route lifetime; isolate domains |
| `edgeTerrainAt`, `fillRaw`, sample notifications | Correct lookup plus domain/residency | Reuse edge vectors and invalidation, not a second seam solver |
| `prepareTerrainLod`, direct/Gather draws, picking, shadows | The same resident terrain lookup | Reuse existing preparation and render paths unchanged |
| `renderLo`, `renderWaterLo` | Direct calls to `quadTreeLo` and its cache | Define distant-mode policy explicitly; detailed-only substitution is insufficient |
| `saveEmpty`, B overwrite, route creation | Name and footprint | Create terrain payloads without TD access in fake mode |
| `loadQuadTree*`, current-domain setters | Tree/cache pair and replacement notifications | Preserve lookup mode through reconstruction and reload |
| `getQuadTree*`, `saveQt*ToStream` | Concrete tree and serialized data | Guard fake-mode consumers; neither access TD nor serialize imaginary population |

The Qt backend's core is already suitable. It does not traverse QuadTile nodes
in its ordinary editing or detailed rendering loops. Three lookup operations
are the main seam: `getMyNameId()`, `getMyName()`, `fillTerrainInfo()`.

Two existing initialization details deserve explicit tests during migration:
`isLoaded()` takes an ID from the detailed tree but accesses `currentQt`, and
`createNewRouteTerrain()` installs only `currentQuadTree`. Avoid leaving
tree/cache/domain pointers inconsistent when changing construction.

## Preferred small design

Add an explicit mode to `QuadTree`, for example `PopulatedTree` and
`Direct2km`. Default remains `PopulatedTree`. The latter is a synthetic
facade: implement the three lookups arithmetically and disable all real
tree loading, insertion and persistence entry points, including stream forms.

This gives the requested fake-tree behavior without a fabricated hierarchy,
a route-wide directory scan, or another terrain implementation. A tiny
`FakeQuadTree` subclass is also possible, but the lookup methods are currently
**nonvirtual**. Merely hiding them in a subclass will not work through Qt's
`QuadTree*`. If choosing inheritance, virtualize the actual lookup interface
and enforce no-I/O behavior on persistence methods too. Prefer the mode approach
unless implementation exposes a concrete reason for another abstraction.

For direct detailed lookup at QuadTree-space `(tileX,tileY)`:

```text
name  = Terrain::getTileName(tileX, tileY)
cx    = tileX
cy    = tileY
level = 1
low   = false
```

`Terrain(TerrainInfo*)` then sets `mojex=cx`, `mojez=-cy`, matching the simple
constructor. Do not invert Z a second time: Qt library lookups already pass
`(x,-z)` into QuadTree, whereas tile-creation arguments use terrain-space Z.

Use a deterministic, collision-free nonzero ID within the supported MSTS
coordinate domain. Check the zero case: the raw hexadecimal name payload can
be zero, while Qt treats ID zero as absent. A separately encoded coordinate
key is acceptable because runtime cache keys need not equal filename bits.
Do not change real-tree IDs as an incidental part of this task. Test the
coordinate limits and every sign combination before selecting the encoding.

Pure name/ID/placement calculation should do no disk I/O. Payload existence
and validity remain the terrain loader's responsibility; `load=false` never
creates a terrain. Cache failed loads so rendering empty space does not probe
the disk each frame. Explicit B creation/reload must replace that failed entry.
Do not turn empty lookups into automatic creation or an unbounded retry loop.

Validate that a direct-mode payload actually spans 2048 m before it becomes
renderable/editable. The common grid validator also accepts larger footprints,
but a level-1 synthetic locator cannot correctly claim those footprints.
Report that such a tile requires normal QuadTree lookup; do not silently
relocate it or reject it globally in normal mode.

## Persistence and distant terrain

Agreed no-QuadTree policy:

1. Loading, viewing, editing, saving, B replacement and new-tile creation in
   fake mode must not read, create or modify TD/index files. Existing metadata,
   valid or broken, remains untouched. Terrain `.t`/RAW operations still work.
2. `QuadTree::addTile()` and `createNew()` currently call `save()` internally.
   Bypass those real implementations in fake mode. Synthetic insertion can
   simply invalidate lookup/failed-load cache state without constructing TD.
   Guard `load`, both `save` forms, `loadTD`, both `saveTD` forms, and network
   entry points as well; an ordinary write-enabled route must not bypass this.
3. Show that QuadTree is ignored and route integrity is not established.
   Missing/invalid TD is damage; fake mode must not claim to repair or validate
   it without reading it. Do not silently switch to fake mode after a real-tree
   load error. Rebuild/repair requires an explicitly selected operation.
4. B overwrite remains permitted for a non-editable tile but must respect
   global write-disable. Retain E/N deletion and descriptor resource-name rules.
5. Initially preserve simple-mode behavior by disabling distant lookup/draw/
   creation with a clear capability check, keeping pointer accesses safe.
   Do not invent 2 km `Lo_tiles` or consult the real distant QuadTree: the
   no-read/no-write rule applies to both detailed and distant tree metadata.

Normal mode retains its real tree/persistence behavior. Fake mode provides a
safe object/capability boundary for callers, not invented TD population.

## Optional explicit QuadTree regeneration

This is a separate repair action, not an automatic side effect or a prerequisite
for the first fake-mode implementation. Offer it with a warning/preview and
explicit confirmation; respect route/app write-disable.

- Discover actual terrain descriptors by scanning filenames and validating
  placement/layout and required payloads, not by iterating World files.
- Initially limit regeneration to validated 2 km detailed tiles if necessary.
  Report larger footprints, overlapping layouts, corrupt payloads and unknown
  names as unresolved; do not silently discard them or report full repair.
- Do not equate every populated-without-payload entry with corruption: MSTS RGE
  can deliberately save that state. A reconstruction from payloads alone cannot
  recover all original designer population choices. State that limitation.
- Back up existing index/TD files before explicit replacement, build and verify
  the replacement separately, and install it with a recoverable write strategy.
  Do not rewrite terrain heights, textures or World files during regeneration.
- Use a separate real-tree builder for this explicit action. Fake-mode load/
  save remains no-I/O. After successful repair offer normal QuadTree mode on
  route reload; do not change discovery underneath resident tiles mid-session.
- Distant-tree recovery is separately scoped; do not claim it was repaired by
  reconstructing detailed terrain only.

## Construction, settings and network boundary

- Replace both selection sites in [Route.cpp](../../../src/tsre/world/Route.cpp)
  with `TerrainLibQt` configured from the route-construction setting.
  Preserve the setting key and its restart/route-load semantics.
- Update its description in
  [SettingsRegistration.cpp](../../../src/settings/SettingsRegistration.cpp):
  false means deterministic detailed lookup with no TD reads/writes, not a
  second renderer or a healthy alternative route format.
- Review [RouteClient.cpp](../../../src/tsre/world/RouteClient.cpp) separately:
  false currently selects the local simple library instead of
  `TerrainLibQtClient`. Do not replace a network client with a local loader.
- [TerrainLibQtClient.cpp](../../../src/tsre/world/TerrainLibQtClient.cpp)
  duplicates the ID/info lookup but constructs `TerrainClient`. Share the
  lookup policy while preserving asynchronous terrain requests/updates.
- [RouteEditorClient.cpp](../../../src/routeEditor/RouteEditorClient.cpp)
  calls `getQuadTree*()->loadTD()` directly. The server enumerates public
  `QuadTree::td` and sends index/TD streams in
  [RouteEditorServer.cpp](../../../src/routeEditor/RouteEditorServer.cpp).
  In normal mode these continue operating on real metadata. In fake mode skip
  TD exchange/application rather than writing or populating a fake tree from
  those messages. Verify how client and server agree on mode, and whether the
  existing protocol can request terrain without the TD handshake. Otherwise
  explicitly disable fake-mode networking until that contract is supported.
- Do not delete `TerrainLibSimple` until all local/client construction sites
  are migrated or an explicit unsupported-network-mode boundary is in place.
  Do not claim multiplayer parity based solely on offline tests.

## Implementation order and verification

1. Implement/test pure direct name, ID and info lookup. No renderer fork.
2. Wire local Qt construction, cache initialization, failed-load/reload handling
   and footprint validation. Keep the old class temporarily for comparison.
3. Enforce no TD reads/writes, even on creation; test absent, stale,
   populated-without-payload and unreadable TD/payload cases and write-disable.
4. Set distant/network policy, update all constructors and settings text.
5. Compare both modes on regular 256/8 routes, then synthetic mode on
   512/4, 1024/2 and 2048/1 profiles with valid patch counts. Test an unpopulated
   TD entry with an existing `.t`: direct mode must load it, tree mode must
   retain its current population rules. World files need not exist.
6. Test all coordinate signs, zero ID candidate, edge normalization, missing
   files, corrupt files, explicit reload and B replacement. Check hashes of
   TD files after all fake-mode operations, including write-enabled B creation/
   overwrite. Verify absence remains absence. Use file-access instrumentation
   to verify the no-read rule too; hashes prove only lack of writes. Verify
   World preservation separately where required by each operation.
7. Test brush/KEY_F, undo, save/reload, edge reset/stitching, mixed resolutions,
   LOD, map, water, picking and shadows. Cover both mesh backends and direct/
   Gather submission without claiming that unrelated Gather work is finished.
8. Benchmark warm lookups and empty-space traversal: no per-sample filesystem
   operations, tree insertion or heap allocation on the resident hot path.
9. Remove the deprecated class, private helpers, includes and construction
   branches only after the replacement passes the above checks.

Estimate: a small lookup adapter, but a medium integration task because
persistence isolation, domain switching and multiplayer are wider than three methods.
No new terrain geometry or duplicated editing algorithms are needed.

Related: [terrain task status](README.md),
[heightmap resolution](terrain-heightmap-resolution.md),
[adjacent edges](terrain-adjacent-edge-cache.md),
[basic LOD](terrain-basic-discrete-lod.md),
[height brushes](terrain-height-brush-performance.md).
