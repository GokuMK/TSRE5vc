# Server and client mode: review

Review step for the server task set (roadmap: [../00-task-roadmap.md](../00-task-roadmap.md)).
Date 2026-11-17, `main` at 1b67228f. Nothing changed in the code.

How it was reviewed:
- the code: `src/routeEditor/RouteEditorServer.cpp`, `RouteEditorClient.cpp`,
  `src/tsre/world/RouteClient.cpp`, `TerrainClient.cpp`,
  `TerrainLibQtClient.cpp`, `src/tsre/tdb/TDBClient.cpp` and every
  `Game::serverClient` branch;
- the simulation's boundary (`src/tsre/sim`): what a run reads, sends and
  publishes;
- a live run: a server and two clients on MSTS `MINI`, in scratch copies
  (below).

The deferred notes from the native-token work
([../../core/server-client-rework.md](../../core/server-client-rework.md))
are confirmed and folded in here.

## 1. How it works today

### Starting
- **Server:** `TSRE5vc --server --game-root R --route X [--ip A] [--port P]`
  (port 65535 by default). Headless (`Game::gui = false`). It loads the whole
  route as the editor does, with every W file preloaded and the terrain
  QuadTree on (`main.cpp:166-171`), then listens on a `QWebSocketServer`
  without TLS (`RouteEditorServer.cpp:44-73`).
  - A console thread reads stdin: `quit`, `save`, `users`.
  - A 100 ms timer sends `users_info` (every user's name, tile, pointer and
    last action) to every socket.
- **Client:** the editor with `core.network.clientLogin` set to
  `user:password@host:port` (secret placeholders allowed). It sets
  `Game::ServerMode`, turns the QuadTree on, turns **undo off**
  (`main.cpp:131-139`) and connects. After `load_route` it checks that
  `ROUTES/<route>` exists locally (`Game::checkRemoteRoute`) and loads a
  `RouteClient`.

### Loading a route on a client
`RouteClient::load` is a staged load. Each answer from the server moves
`loadingProgress` on by one:

| Stage | Asks for | Gets | Notes |
|---|---|---|---|
| 0 | `request_trk` | `requested_trk` | the TRK |
| 1 | `request_tsection` | `requested_tsection` | the route's tsection part; the global `tsection.dat` is read locally |
| 2 | `request_tdb`, `request_rdb`, `request_terrain_qt` | `requested_tdb`, `requested_rdb`, `requested_td`, `requested_td_lo`, then each TD file as binary | |
| 6 | `request_addons` | `requested_addon` | the route's `.ref`; then reads `ssource.dat`, `ttype.dat`, `forests.dat`, `carspawn.dat` **from local disk** |

Then, on demand while viewing:
- `request_tile x z` returns the tile's W file as text;
- `request_terrain_tfile`, `_yfile`, `_ffile` return a terrain tile's T
  file, heights (floats) and flags.

Not loaded at all on a client: activities, services, paths, traffic,
markers, route places, Open Rails weather (`RouteClient.cpp:117-124`
commented out). The activity tools, the Route menu (save, reload) and
some Tools items are hidden (`RouteEditorWindow.cpp:226, 337, 683`).

### Messages
- Every message is a websocket **binary** frame:
  - **text messages**: UTF-16 with a BOM, ParserX syntax, for example
    `update_worldobj ( x ( .. ) z ( .. ) Static ( ... ) )`. The records are
    the same text the route's files hold: objects, TDB nodes and items,
    tsection shapes and sections, the TRK, the `.ref`.
  - **binary messages**: start with `B` and a native token
    (`NetworkToken`), then a size that is always written as 0, a byte, x and
    z, and a terrain payload (T file, float heights, flags, TD file).
- Edits a client sends, and the server forwards to every other socket:

| Message | What it carries | Sent from |
|---|---|---|
| `update_worldobj` | a whole world object, `remove ( )` if deleted | `WorldObj::setModified` (`WorldObj.cpp:67-68`), every time |
| `update_trnode_tdb`/`_rdb` | a whole track node, or `remove` | `TDBClient::updateTrNode` |
| `update_tritem_tdb`/`_rdb` | a whole track item, or `remove` | `TDBClient::updateTrItem`; also `setModified` for the object's items |
| `update_tracksection`, `update_trackshape` | a route tsection record | `TDBClient` |
| `increase_tdb_itrnode` and the three like it | nothing: "I used one more ID" | `TDBClient::getNextItrNode`, `getNewTRitemId` |
| `TSRE_Terrain_tFile`, `TSRE_Terrain_RawFile` | a whole terrain tile's T file or heights | `TerrainLibQtClient` |
| `update_pointer_position` | tile and 3D pointer, about every 100 ms | `RouteEditorGLWidget.cpp:404-408` |

- **Edits are optimistic:** the sender changes its own copy first and sends
  the whole record. The server applies it, marks the file modified, and
  forwards the message unchanged. Nobody confirms or refuses anything. The
  last record to arrive wins.
- **Saving:** only the server saves, from its console (`save`). A client's
  `RouteClient::save` is empty.

### The old ping-pong between server and clients
Fixed by a convention: **code that applies a received edit must not go
through the methods that send.**
- `Tile::replaceWorldObj` calls `setModified` only when there is no
  `serverClient` (`Tile.cpp:211-212`), so a client applying someone's
  object does not send it back.
- `TDB::updateTrNodeData` and `updateTrItemData` record the change for the
  superelevation but do not call the virtual `updateTrNode`/`updateTrItem`
  (`TDB.cpp:3767-3770, 3812-3815`).
- The client calls base methods explicitly: `t->Terrain::loadTFile(data)`
  (`RouteEditorClient.cpp:214`), `Game::trackDB->TDB::getNextItrNode()`
  (336).

It holds today. It comes back as soon as any code reached from a receive
path calls `setModified(true)` or one of `TDBClient`'s overrides. Examples
that already exist and are safe only by luck:
- `Tile::wczytajObiekty` runs every object's `loadingFixes()` on a tile that
  came from the server (`Tile.cpp:128`). Several `loadingFixes` call
  `setModified()` (Signal, Speedpost, Static, Track, LevelCr). The server
  ran the same fixes when it loaded the tile, so they find nothing to fix. A
  client with different fix rules (an older or newer TSRE) would send the
  whole tile's fixed objects back to everyone each time it loaded it.
- The receive path for `TSRE_Terrain_RawFile` calls the virtual
  `loadRAWFile`, which on a `TerrainClient` steps its loading stages again
  (`TerrainClient.cpp`, `loadRAWFile`). It does not send, but it relies on
  the stage counter being past its last case.

A design that cannot loop: received changes are applied by one function per
record kind that never sends, and sending happens in one place (the edit
the user made), never as a side effect of `setModified`.

## 2. The live run (2026-11-17)
Scratch copies under the session scratchpad: the server on a copy of
`MINI`; one client with a full copy of the route, one with only `SHAPES`,
`TEXTURES` and `TERRTEX`. `GLOBAL`, `TRAINS` and `SOUND` linked from
`~/ORTS/MSTS`. The Release build in `build/` (2026-11-17 19:23; the
network code is the same at 1b67228f), Xvfb, OpenGL.

- **The protocol works end to end.** Both clients logged in and loaded: TRK,
  tsection, TDB, RDB, QuadTree, `.ref`, nine W files, a terrain tile's T
  file and heights, all from the server in about 1 s. The users window
  listed the user and its last action.
- **The view looked empty** on both clients (sky and a horizon line) where
  the local editor shows the terrain. The terrain was there; the camera was
  under it (the user, 2026-11-17). The start height is set to 20 m above
  the terrain only if the terrain is loaded at that moment
  (`RouteEditorGLWidget.cpp:618-620`); a client's terrain arrives later over
  the socket.
- **The client with only the heavy folders also loaded**, with less:
  - `ENVFILES/editor.env` not read (error);
  - `speedpost.dat` and `sigcfg.dat` missing: empty configurations, so
    signals and speed posts cannot be drawn or placed as on the server;
  - `ssource.dat`, `ttype.dat`, `forests.dat`, `carspawn.dat` missing:
    empty lists, silently;
  - the route's `load.ace` and `details.ace` looked for in `ROUTES/mini/`
    (lower case, from the TRK's name) on both clients.
- **A server with stdin closed** (as under systemd, `nohup`, or `< /dev/null`)
  burns two cores: the console thread 99 % and the main thread 80 %
  (`top -H`, 15 s). `QTextStream::readLine` returns at once at end of input,
  and the thread emits an empty command each time
  (`RouteEditorServer.cpp:176-182`). The thread also calls
  `QCoreApplication::processEvents` from a non-GUI thread.

- **The console works** (a second run, commands through a pipe): `users`
  printed `Users: 0`; `save` wrote `mini.tdb`, `.rdb`, `.tit`, `.rit` and
  the route `tsection.dat` with nothing changed, and said nothing; `quit`
  stopped the server.

Editing was not tried live (no scripted input). The findings on edits below
are from the code.

## 3. Findings
Severity: **P1** breaks data or the mode; **P2** wrong but limited;
**P3** cleanup.

### Broken today
- **B1 (P1) The camera starts under the terrain on a client.** Above.
  Place it once when the start tile's terrain arrives
  (`TerrainLib::terrainAvailabilityChanged` is called then), unless the
  user moved it first.
- **B2 (P1) A server without a console spins.** Above. Stop reading at end
  of input; read stdin with a `QSocketNotifier` on the main thread (Linux)
  or keep the thread but block properly.
- **B3 (P1) Objects on a tile the server has not got are lost on the
  server and kept by everyone else.**
  - The server forwards `update_worldobj` whatever happened. If its
    `tile[x*10000+z]` is NULL (a tile with no W file yet), the object is
    dropped (`Route.cpp:913-915`) but the other clients get it
    (`RouteEditorServer.cpp:475-482`). It is never saved.
  - `QHash::operator[]` on `route->tile` also inserts a NULL entry for every
    tile looked up that way (server 328, `Route.cpp:913`).
  - A client asking for a tile the server has not got gets no answer and
    keeps an empty placeholder `Tile()` (`RouteClient.cpp:158-167`).
- **B4 (P1) The server's save renumbers the track database and nobody is
  told.** `TDB::save` runs `while(deleteNulls())`, which moves the highest
  node into each hole (`TDB.cpp:3696-3706`; task tracks 15 noticed it). The
  clients keep the old numbers; their next node or item update by ID
  writes over a different record.
- **B5 (P2) Terrain flags are never sent.** The server's answer to
  `request_terrain_ffile` has an empty payload (`saveFfileToStream` is
  commented out, `RouteEditorServer.cpp:468`). A received F edit is
  applied on the server but not forwarded (686-700). Clients cannot send
  one.
- **B6 (P2) A client quits on any disconnect.** `RouteEditorClient::close`
  calls `qApp->quit()` (108-111): no message, no reconnect, edits since the
  last server save are only on the server. A failed login says nothing
  (`auth_fail` is skipped, 248-251).
- **B7 (P2) Received records are trusted.** Node, item, section and shape
  IDs from a message index the server's tables with no check
  (`TDB.cpp:3738-3858`); `remove` sets NULL without deleting; replaced
  records leak. A received terrain update that fails half way leaves a
  partly changed tile, is still marked modified and forwarded
  (`TFile::load` mutates as it parses; core doc).
- **B8 (P2) Each message is wrapped in a `FileBuffer` that owns the
  socket's memory.** Never deleted, one leak per message; deleting it
  would free `QByteArray`'s storage (core doc). `TerrainClient::loadTFile`
  replaces its `tfile` without freeing the old one.
- **B9 (P3) A message holding several edits is forwarded once per edit,
  whole.** The server forwards the incoming message inside the per-token
  loop (`RouteEditorServer.cpp:481, 518, 525...`). Clients send one edit a
  message today, so it does not show.
- **B10 (P3)** the client's `requested_addon` makes a new `Ref` without
  freeing the old one (`RouteEditorClient.cpp:308`); `users_info` and pointer messages run every 100 ms
  whether anything changed.

### Edits that collide (the user's "TDB editing collisions")
- **C1 (P1) Track node and item IDs are chosen by each client.**
  `TDBClient::getNextItrNode` returns `++iTRnodes` locally and only tells
  the server it took one; `getNewTRitemId` also reuses the first local
  `emptyitem` slot (`TDBClient.cpp:53-79`). Two users adding track at the
  same time take the same ID; each overwrites the other's node on the
  server and on every other client.
- **C2 (P1) World object UiDs are chosen by each client per tile**
  (`++maxUiD`, `Tile.cpp:618-620, 674-676`), and deleting the highest UiD
  lowers `maxUiD` so it is used again (604-605). Received objects replace
  the one with the same UiD and type (`Tile.cpp:214-223`), so two users
  placing objects on one tile replace each other's.
- **C3 (P1) No versions, no transactions.** One flex track is many
  messages (nodes, items, objects, tsection shapes), each applied on its
  own; another user's edit can land between them. Join and split move
  items without `updateTrItem`, so the moved items are never sent (task
  tracks 15, section 3).
- **C4 (P2) Undo is off in client mode** (`main.cpp:137`); the activity
  tools, terrain painting, procedural materials and baking are off
  (`TerrainTools.cpp:78-218`, `TerrainMaterialLibrary.cpp:155-190`, ...).

### Security
- **S1 (P1) Sockets that never logged in get everything.** Edits and
  `users_info` go to every socket in `clients`, logged in or not
  (`RouteEditorServer.cpp:622-635`, 165).
- **S2 (P2) The authentication setting is read two ways:**
  `usersAuthInit` compares `" "`, `userAuth` compares `""` (75-83,
  123-135). With the default (empty) anyone logs in, which may be meant;
  `file` reads `users.txt` from the working directory, plain text.
- **S3 (P2) No TLS; the password crosses in clear text.** Usernames are
  checked for duplicates before they are cleaned, and renamed with `rand()`.
- **S4 (P3) No size limit on a message**, and the binary size field is
  always 0 (core doc).

### Protocol
- **P1 No version or capability check.** A mismatched client finds out by
  failing to parse (core doc).
- **P2 No request IDs, no replies to edits, no errors back to the sender.**
- **P3 Two framings** (BOM text and `B` binary) told apart by the first
  bytes, inside binary websocket frames.

## 4. Route files on a client
Every file a route load and an editing session read, and where a client
gets it. Readers with file:line are in the inventory made for this review
(code reading, 2026-11-17).

### From the server today
| File | Asked with | Edits sent back |
|---|---|---|
| TRK | `request_trk` | none (the TRK editor is hidden) |
| route `tsection.dat` | `request_tsection` | `update_tracksection`, `update_trackshape` |
| `.tdb`, `.rdb` | `request_tdb`, `request_rdb` | node, item and `increase_*` messages |
| `TD/` QuadTrees and `.td` files | `request_terrain_qt` | none |
| detailed `TILES/*.t`, `_y.raw` | `request_terrain_tfile`, `_yfile` | T file, heights |
| `_f.raw` | `request_terrain_ffile` | **empty answer** (B5) |
| `WORLD/*.w`, `*.ws` | `request_tile` (sound items included) | `update_worldobj` |
| `.ref` and `ADDONS/*.ref` | `request_addons` (merged) | none |

### Route files a client reads from its own disk
Each of these breaks the "server gives every text file" rule. The client
with only the heavy folders showed the first group failing.

Read at every load:
1. `GLOBAL/tsection.dat`, or the route's `OPENRAILS/tsection.dat` first
   (`TSectionDAT.cpp:82-121`). The route part from the server is numbered
   after the client's own global counts (`Route.cpp:931-933`): **a client
   with a different global tsection gets shifted section IDs.**
2. `speedpost.dat`, `sigcfg.dat` (`Route.cpp:984-985`).
3. `ENVFILES/editor.env` and its weather maps (`RouteClient.cpp:98`).
4. `ssource.dat`, `ttype.dat` (`RouteClient.cpp:124-126`).
5. `forests.dat`, `carspawn.dat`, `OPENRAILS/carspawn.dat`.
6. `load.ace`, `details.ace` from `ROUTES/<RouteID>/` (the TRK's ID, not
   the folder; `Trk.cpp:434-435`).

Read while editing or viewing:
7. `telepole.dat`; `TRACKPROFILES/*.stf`, `*.xml`; `PROCEDURAL/`;
   `terrainmaterials.dat`; `TERRAIN_MAPS/*.png`; `DISPLAYS/*.rml`,
   `*.rcss`; `humans.dat`.
8. **Distant terrain:** `LO_TILES/*.t`, `_y.raw` are read from disk even
   though their QuadTree came from the server: `TerrainLibQt` makes them
   with `new Terrain(...)` directly (`TerrainLibQt.cpp:1568, 1654`).
9. **Map mode:** tile descriptors from `TILES/` and `LO_TILES/`
   (`TerrainLibQt.cpp:137`, `TerrainMapLayer.cpp:157`); `reload` too
   (420).
10. Terrain requests carry x and z but not detailed or distant; the server
    answers from its current QuadTree, normally the detailed one
    (`RouteEditorServer.cpp:408, 432, 456`).

For the simulation and play only: `OPENRAILS/sigcfg.dat`, `sigscr*.dat`
(`TrackNetwork.cpp:303-321, 651-660`), the activity for `--activity`.

### Not loaded on a client at all
Activities, services, traffic, paths, OR timetables, markers and country
places, `weathertransitions.dat`, `.tit`/`.rit`, terrain flags, `.pmap`
procedural material maps, patch-flag files of network tiles.

### Written into the client's own route folder
Gated only by `Game::writeEnabled`, not by client mode:
- `saveEnvironment`: `ENVFILES/*.env` (`RouteEditorGLWidget.cpp:4289`);
  the Environment panel is shown on a client.
- **Auto new tiles:** `RouteClient::requestTile` hands back its placeholder
  (`loaded == -2`) on the second call, and `Route` then calls
  `Route::newTile`, which writes `TILES/*.t`, `_y.raw` and `WORLD/*.w`
  locally (`Route.cpp:1130-1140`, 2549-2592). The server never hears of
  the tile.
- Quad tile creation and deletion, terrain overlays (`RouteDataTools.cpp`).

### What is heavy
| | PRUDNIK (7.0 GB) | USA1 (250 MB) | BNSF_SCENIC (298 MB) |
|---|---|---|---|
| `TEXTURES` | 5.5 GB | 79 MB | 102 MB |
| `TERRTEX` | 399 MB | 13 MB | 110 MB |
| route `SOUND` | 464 MB | 11 MB | 3.5 MB |
| `SHAPES` (with `.sd`) | 159 MB | 3.4 MB | 8.4 MB |
| `TILES` | 356 MB, half of it `_e`/`_n.raw` TSRE never reads | 129 MB, 86 MB of it `_e`/`_n` | 62 MB |
| `ENVFILES` (mostly `.ace`) | 11 MB | 1.7 MB | 3.9 MB |
| `WORLD` | 104 MB (UTF-16 text) | 3.1 MB (compressed) | 2.8 MB |
| TDB + RDB + TIT + REF | 36 MB | 8.5 MB | 3 MB |
| everything else | under 10 MB | under 1 MB | under 1 MB |

`TEXTURES`, `TERRTEX`, `SOUND` and `SHAPES` are 92 % of PRUDNIK. All its
text files, `WORLD` included, are about 150 MB; USA1's about 12 MB. The
terrain the editor reads (`.t`, `_y.raw`, `_f.raw`) is 130 MB for
PRUDNIK and already comes from the server, tile by tile.

### How the readers read
Most route-file readers open a `QFile`, check that it exists, and call
`ReadFile::read` (SpeedPostDAT, SoundList, ForestObj, CarSpawnerObj,
TelepoleData, HumanSet, TSectionDAT, Trk, Activity, Path). `SigCfg`,
`Environment` and `TerrainMaterialLibrary` read through `QFile` or their
own code. So one reader in front of `ReadFile` ("give me this route
file") reaches most of them with a small change each.

## 5. The simulation's boundary
Question: can the simulation run on the server, with clients sending
commands and getting its results over the network?

**The core is ready; what surrounds it is not.**

What already fits:
- The core never touches editor objects while it runs. It works on an
  immutable `SimulationInput` (plain values: no pointers into Consist,
  Eng, TDB or world objects) and its own copies of the track
  (`TrackGeometry`, `TrackNetwork`).
- Commands are values: `setSpeed(double)`, `play`, `pause`,
  `runTo(time, thenRun, untilOnTrack)`, `drive(train)`, `leave`,
  `command(vehicle, Message)`, `hear(trains)`, `setCrossings(params)`, each
  numbered. `Message` already has a text form (`toText`/`fromText`).
- Frames are immutable and the parts that change rarely carry serials
  (aspects, section uses, standing trains, path nodes, events): a natural
  delta.
- Events and messages are ordered queues with bounds.
- Lua in the simulation is deterministic (no wall clock, seeded random).

What ties it to one process:
- **Building the input needs the editor:** `SimulationController::makeInput`
  runs on the main thread, changes the route (`computeTrackCant`), opens a
  `QProgressDialog` for work-order planning (`WorkPlanning.cpp:29`), reads
  the live activity, ConLib, EngLib, settings and WorldClock.
- **Vehicles are shared objects:** `Vehicle::definition` is the editor's
  `VehicleDefinition` (a parsed document, makers, caches, polymorphic cab
  controls). `Vehicle::eng` is an EngLib number given out by a running
  counter, so it differs between processes.
- **Indexes into the editor's data:** activity objects, events, traffic,
  services, TDB items and node IDs. Fine across processes only if both
  sides built them from the same files in the same order.
- **The main side reads the same input object the core runs** (`in`):
  the Timetable window, the map, the activity tools, station people,
  displays.
- **Frames are compared by pointer** (`now == placedFrame`,
  `sectionUses.get() != trackUses`...), timed by one machine's steady clock
  (`publishedNs`, `aheadAt` up to 50 ms), and the queues have one reader.
- **Drawing reads the live editor data:** cars are placed on the live TDB
  through `Ruch`; doors from loaded platform objects; ConLib and EngLib for
  the consist copies.
- **One driven train** (`int driven` in the core), one `DrivenFrame`, no
  train in `command()`, one set of keys; the heard set comes from one
  camera. Nothing identifies a player.
- **Simulate** (Timetable window) runs its own `SimulationCore` on a worker,
  outside the host.

What a frame costs on a wire: every train's state and every vehicle's
front and rear pose every frame (about 96 bytes a vehicle), plus every
value of every driven vehicle (the user's choice, 2026-10-29). The host
publishes every 10 ms. 100 trains of 20 vehicles is about 190 KB a frame,
19 MB/s at 100 frames a second: a network needs fewer frames, only the
trains near each player in full, and the shared parts sent only when
their serial changes.

The user's decision of 2026-10-24 (driving roadmap) already sets the
direction: **the server is authoritative and runs the simulation; clients
do not simulate other trains or signals but get render-ready results; a
remote train's loco may be a provider of force.**

## 6. Where new IDs come from (reviewed 2026-11-17, after the user's answers)
Question: does every new ID go through a function a client can override,
so the server can hand out ranges (or IDs)?

**TDB and RDB nodes and items: yes.**
- `getNextItrNode()` (virtual) has 11 callers in `TDB.cpp`: every new end,
  vector and junction node in the editing paths (lines 886-888, 1006,
  1229-1231, 1510, 1561).
- `getNewTRitemId()` (virtual) has 11 callers: pickups, sidings and
  platforms, signals, speed posts, level crossings, sound regions,
  hazards, crossovers (3071-3341).
- `TDBClient` already overrides both (`TDBClient.cpp:53-79`).
- Set directly only by: the loaders (`TDB.cpp:268-379`), the route merge
  tool `mergeTDB` (497-540, local), `deleteNulls` at save, and the deep
  copy undo uses (4027-4033; undo is off on a client).

**Route tsection sections and shapes: no.** `TDB::fillDynTrack` takes
`tsection->routeMaxIdx` (two sections at a time) and
`tsection->routeShapes` inline (`TDB.cpp:660-711`), after looking for an
equal existing record. Two users laying dynamic track at once take the
same numbers, as with nodes. A getter is needed first.

**World object UiDs: no.** Taken inline per tile: `++maxUiDWS` /
`++maxUiD` in two places (`Tile.cpp:618-620, 674-676`); deleting the
object with the highest UiD steps `maxUiD` back (604-605). UiDs of a
million and more are left out of the maximum (`Tile.cpp:121-124, 466,
490`).

**Gaps on save:**
- Nodes: holes are filled at save by moving the highest node in
  (`deleteNulls`; B4).
- Items are never renumbered, and cannot be: W files (`TrItemId`),
  services and activities (`PlatformStartID` in `Service.cpp:145`,
  `Activity.cpp:761`; `SidingItem` in `ActivityEvent.cpp:482`) refer to
  them by ID. A deleted item becomes an `emptyitem` record and keeps its
  ID (`TDB.cpp:3431`); `getNewTRitemId` reuses those slots when
  `useTdbEmptyItems` is on (the default). The save writes the count `iTRitems` and
  skips NULL slots (`TDB.cpp:3920-3926`, 3959-3964), so a NULL item would
  make the count wrong. Only a received `remove` makes one
  (`TDB.cpp:3793`), and no client sends it today: a local deletion makes
  an `emptyitem` and sends that. Unused IDs of a range would be NULL
  slots, so with ranges the save must write an `emptyitem` for each.
- Why it matters: Open Rails fills its item table in the order of the
  records and ignores the IDs written in them
  (`Orts.Formats.Msts/TrackDatabaseFile.cs:123-139`, `++idx` per record).
  One skipped record moves every later item down by one, so signals,
  speed posts and platforms would point at the wrong items. TSRE reads
  items by their IDs (`TDB.cpp:379`) and would not notice.
- The save loop also runs to `i <= iTRitems`, one past the last item;
  `operator[]` on the map adds an empty entry there. It is not written,
  so it is harmless.

## 7. Other existing pieces
- `TrainNetworkEng`: the old external-train link (TCP 7755, main thread,
  `Game::useNetworkEng`). Replaced by design with a link in the simulation
  thread (driving roadmap); only its wire format may carry over.
- `core.network.playerMode`: hides the editing UI and the 3D pointer. A
  start for a "player" client.
- Passengers already follow the pattern this set wants for people: the
  simulation counts them (`SimulationCore::passengersArrive`, boarding
  holds); the main side draws and walks them (`src/tsre/humans`, Detour).
