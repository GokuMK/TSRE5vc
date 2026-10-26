# Loading Tiles Ahead of the Camera

Status: open, planned (2026-10-26); put off by the user for another time.

World tiles (`Route::requestTile`, `new Tile(x, z)`) and terrain tiles
(`TerrainLibQt::getTerrainByXY`) load on the main thread in the frame whose
gather first needs them: the tile ring of the render radius
(`core.rendering.tileRadius`, `Game::tileLod`) around the camera's tile. A
tile crossing loads a whole new row in one frame: the stutter every 2 km.

## Measured (2026-10-26)

`tests/renderer/crossing-heavy.json` replays the crossing of the user's frame
log on Prudnik (287 ms frame in the editor) as single frames 2.5 m apart. The
crossing frame's gather, timed part by part:

| Part | Before | After the fixes below |
|---|---|---|
| Track database labels and lines | 179 to 240 ms | 10 ms |
| The 3 new world tiles loaded | 135 to 175 ms | 57 ms |
| Terrain | 40 to 110 ms | 46 ms |
| The new tiles' first gather | 24 to 30 ms | 23 ms |
| The frame's CPU (harness) | 547 ms | 257 to 270 ms |

Done (`docs/tasks/textures/04-texture-memory.md`, "Tile crossing"): track
node numbers from one digit texture instead of a painted texture each
(`TextObj::initDigits`), the network's lines kept around an origin tile and
section lines a tile each (`TDB::pushRenderAll`, `pushRenderLines`), and the
shape library's lookup by an index and cached path keys
(`ShapeLib::addShape`: 108 of 116 ms of a 2226-object tile's object making).

A world tile's load is now its file read and parsed (`Tile::load`:
`loadBinaryData` / `loadUtf16Data`, "read in", 12 to 29 ms for the heavy
tiles) and its objects made (`wczytajObiekty`, `WorldObj::load`, "made in",
about 25 ms for a 2226-object tile).

## Plan

1. **World tiles, a ring ahead.** Keep the ring one tile beyond the render
   radius loading in the background, nearest first, outside the render set
   (W tiles are small: thousands of objects, no shapes loaded until drawn).
   - The parse on a worker (the user: "the easy third"): it builds object
     state only (`loadBinaryData`'s own comment). To check first: no
     globals touched while parsing (error messages, TDB, settings).
   - The objects made on the main thread in slices of a few ms a frame:
     `WorldObj::load` adds shapes to the shared shape library and looks up
     the track database, which the main thread reads and edits each frame;
     on a worker it would need locks on every main-thread access, or a
     two-phase load (objects made unregistered, registered on the main
     thread).
   - A tile the gather needs before it is done is finished at once, as now.
   At 30 m/s a crossing comes every 68 s; a row ahead takes a few seconds of
   spare frame time.
2. **Terrain: the next row only.** Decision open (the user asked for the
   memory first, below). Proposal: only the row in the travel direction (3
   tiles, 5 moving diagonally), only its heights and flags, with the
   tiles behind released as now (`releasesUnusedTiles`: prefetched tiles
   must count as in use, or the release deletes them before their first
   draw). A full extra ring is out: 16 more tiles, gigabytes at 2048
   samples.
3. **Big tiles' paged mesh build** (`TerrainMeshPaged::ensureInitialized`):
   92 to 105 ms at 1024 samples on the main thread, about 400 ms expected at
   2048: split over several frames (by page) or done before the tile is
   drawn.
4. Left at the crossing: the new tiles' first gather (23 ms) and the upload
   of the new terrain meshes (about 58 ms in the harness).

## Terrain tile memory

Private bytes around a tile's load and its paged mesh build (Steam Deck;
GPU bytes as logged by `TerrainMeshPaged`):

| Samples | Load | Mesh build (CPU) | GPU mesh |
|---|---|---|---|
| 256 (Prudnik) | 0 to 0.3 MB | 0.57 MB | 0.67 MB |
| 256 (procedural) | 21 to 22 MB | 0.57 MB | 0.67 MB |
| 512 (procedural) | 23 MB | 3.1 MB | 2.5 MB |
| 1024 (procedural) | 25 MB | 36 MB | 9.7 MB |
| 2048 (expected) | about 17 MB heights | about 140 MB | about 39 MB |

- The heights are 4 bytes a sample (4 MB at 1024, 17 MB at 2048).
- A procedural tile holds about 21 MB whatever its samples (its material
  state): about 190 MB for 3x3 tiles. Worth a look of its own.
- How much of the mesh build's CPU memory stays after the upload is not
  separated.

## Related

- Vulkan memory blocks: a texture creation that makes QRhi's allocator add a
  block takes 24 to 29 ms (`docs/tasks/textures/04-texture-memory.md`).
- Frame log (`core.system.frameLog`): world and terrain tile loads a frame,
  with their time.
