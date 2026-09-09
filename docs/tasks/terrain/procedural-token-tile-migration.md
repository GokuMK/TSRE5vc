# Procedural terrain token tile migration

Status: completed on 2026-09-09. Automated validation and visual Route Editor
acceptance passed.

## Scope

The prototype procedural terrain SIMIS token numbers changed to allocated
native TSRE IDs. Existing experimental tiles that contain the prototype IDs
are intentionally not recognized by the new reader and therefore need a
one-time update after the code merge.

| Prototype token | Prototype ID | Native token | Native ID |
| --- | --- | --- | --- |
| `TSRE_Terrain_Material_Buffer` | `100009` / `0x000186A9` | `TSRETerrainMaterialBuffer` | `0x00061000` |
| `TSRE_Terrain_Baked_Material` | `100010` / `0x000186AA` | `TSRETerrainBakedMaterial` | `0x00061001` |
| `TSRE_Terrain_Material_Map` | `100011` / `0x000186AB` | `TSRETerrainMaterialMap` | `0x00061002` |

The migration applies only to:

- repository test fixtures or expectations, if any embed prototype IDs; and
- the user's local procedural test route tiles.

Do not scan or rewrite unrelated route terrain. Discover the actual affected
files first instead of hard-coding the historical count, because the test route
may have changed.

## Procedure after approval

1. Inventory candidate `.t` files and identify those containing any of the
   three prototype IDs. Keep the resulting path list outside the repository
   when it names local or copyrighted route assets.
2. Back up every affected descriptor and required procedural sidecar before
   changing it. Record hashes or another simple before/after identity record.
3. Recreate or resave each affected procedural tile through the current TSRE
   workflow so the normal writer emits the native IDs. Do not add old-ID reader
   aliases, conversion-on-load, dual writes, or a permanent converter.
4. If tests synthesize old-ID input to verify rejection, retain those negative
   tests. Update only fixtures or positive expectations intended to represent
   current valid procedural terrain.
5. Verify that each updated descriptor contains the required native IDs and no
   prototype procedural IDs, then test load, paint, bake, save and reopen.
6. Visually verify the local procedural route and confirm its material mapping,
   `.pmap`, baked fallback and route material catalogue remain correct.

The file inventory and asset rewrite are deliberately separate from merging
the parser code. Present the discovered targets and exact proposed operation to
the user before modifying route data.

## Migration result (2026-09-09)

The repository contains no tracked `.t` or `.pmap` fixture requiring an asset
rewrite. The synthetic old-ID rejection case in `TokenIdTestSuite.cpp` remains
intentionally unchanged.

The approved local procedural route contained three affected descriptors:

- `tiles/-11dbfb9c.t`
- `tiles/-11dbfba0.t`
- `tiles/-11dbfbac.t`

Each descriptor contained all three prototype blocks as valid direct children
of `terrain_samples`. Only their four-byte token fields were changed; block
lengths, labels and payloads were retained byte-for-byte. The descriptors,
their `.pmap` files, current baked `_procedural.ace` files and
`terrainmaterials.dat` were backed up first. The backup directory contains a
before-hash manifest and per-descriptor before/after hashes.

Validation completed after the rewrite:

- structured scan: native IDs present and prototype IDs absent in all three
  descriptors;
- binary comparison: descriptor sizes unchanged and differences limited to
  the nine expected token-byte positions per file;
- all backed-up `.pmap`, baked ACE and catalogue hashes match the live files;
- local procedural-route terrain corpus: 3/3 descriptors accepted, loaded and
  editable, with no payload failure;
- actual descriptor save/reopen on disposable copies: 3/3 retained their map
  reference, baked marker and valid UiD map;
- token suite: 1533 passed, 0 failed;
- procedural-material suite: 503 passed, 0 failed, including paint, bake,
  save and reopen coverage.

The user subsequently opened the local route in Route Editor and confirmed the
migrated terrain looked correct. The migration backup remains available for
manual removal when it is no longer wanted.
