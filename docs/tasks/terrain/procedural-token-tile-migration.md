# Procedural terrain token tile migration

Status: proposed post-merge asset-maintenance task. Do not run it as part of
the native-token merge or without the user's separate approval after merge
validation.

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
