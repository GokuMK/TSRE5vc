# Task 03 - Route files from the server

Status 2026-11-18: built, the user's go ("Ok, now makes sense, proceed",
2026-11-18, on the revised design); results below. On `feature/server`
(worktree `../TSRE5vc-server`); not merged: it merges with the server set. Revised
after the user's answers (below): no file reader serving paths, route data
by kind from the server's memory.
Roadmap: [00-task-roadmap.md](00-task-roadmap.md) ("Route files from the
server", the user's answers of 2026-11-17). Inventory of 2026-11-17:
[reports/server-client-review.md](reports/server-client-review.md),
section 4.

## Goal
A client with only the heavy folders (the route's `SHAPES`, `TEXTURES`,
`TERRTEX`, `SOUND`, `ENVFILES/TEXTURES`; the root's `GLOBAL` shapes and
textures, `TRAINS`, `SOUND`) loads the route as a local load does: the
same data, the same view. Everything else comes from the server; the
client writes none of it to disk.

Not in this task: clients editing the files the reader serves (the rule
below: such a file moves to messages in the task that lets clients edit
it); `TRAINS` and the root's `SOUND` compared (with the simulation tasks);
updates of a served file changed on the server's disk while clients are
connected (a snapshot at login, as the roadmap says).

## What a client read from its own disk (before this task)
From the review (section 4), checked again on the branch:
- **every load:** `GLOBAL/tsection.dat` (or the route's
  `OPENRAILS/tsection.dat`) and their includes: the route's sections are
  numbered after the client's own global counts, so another global file
  shifts them; `speedpost.dat`, `sigcfg.dat`; `ENVFILES/editor.env` and
  its weather files; `ssource.dat`, `ttype.dat`; `forests.dat`,
  `carspawn.dat`, `OPENRAILS/carspawn.dat`; `load.ace`, `details.ace`,
  `graphic.ace`;
- **while editing or viewing:** `telepole.dat`, `TRACKPROFILES/`,
  `PROCEDURAL/`, `terrainmaterials.dat`, `TERRAIN_MAPS/*.png`,
  `DISPLAYS/`, `humans.dat`;
- **terrain beyond the server's tiles:** distant tiles (`LO_TILES`) and
  map mode's tile descriptors are read from disk; the client's own tile
  loader (`TerrainClient`) skips patch flags and procedural material maps.
- **not loaded at all on a client:** markers and country places,
  activities, services, traffic, paths, timetables, weather transitions.

Sizes (2026-11-18), the route's files outside the heavy folders and the
object data (TRK, tsection, TDB/RDB/TIT/RIT, REF, `WORLD`, `TILES`,
`LO_TILES`, `TD`): PRUDNIK 490 files, 14.5 MB (paths 3.8 MB, track
profiles 2.3 MB, `OPENRAILS` 1.9 MB, the root's files 6.1 MB); EUROPE1 113
files, 2.7 MB; MINI 23 files, 15 MB, of which 14.8 MB are two folders TSRE
never reads (`tiles_1024-16`, `-32`). Compressed (gzip -6, as `qCompress`):
PRUDNIK 6.7 MB, EUROPE1 1.8 MB.

## Steps

### 1. Route data by kind, from the server's memory
Revised (2026-11-18): the first design's file reader served the server's
disk by path, with folder listings. The user: the server may hold data
only in memory (an activity, a path, a painted terrain texture), so a file
read gives old or no data; remote file access fits only what TSRE does not
create; listing folders is a security risk. So: **no paths and no listings
from a client**; a client gets kinds of data, and the server sends what it
holds.
- **What TSRE creates or edits:** serialized from the server's live
  objects, as the TRK, TDB, W files and terrain are today: the
  environment files the route uses (`ENVFILES/*.env`),
  `terrainmaterials.dat`, the terrain overlays (`TERRAIN_MAPS`, a tile's
  when asked).
- **What TSRE neither creates nor edits** once the route is made
  (`speedpost.dat`, `sigcfg.dat`, `forests.dat`, `carspawn.dat` and the
  `OPENRAILS` one, `ssource.dat`, `ttype.dat`, `telepole.dat`,
  `GLOBAL/tsection.dat` with its includes and the route's
  `OPENRAILS/tsection.dat`, the track profiles, `PROCEDURAL/`,
  `DISPLAYS/`, `humans.dat`, the root images): the bytes the server read
  at its start; a fixed set of files named by kind, not a folder's
  listing.
- **Not now:** markers, activities, services, traffic, paths,
  timetables (question 1: the user is not sure; the activity tools stay
  off on a client; editing activities will be a permission of its own).
  They come as messages when a task needs them on a client.
- **Heavy assets** (shapes, textures, sounds, painted terrain textures)
  stay the client's, compared (step 6). A later sync task may send those
  TSRE does not create; painted terrain textures go by messages when
  painting over the server comes (the editor set).

### 2. The bundle
- All of step 1's kinds in one compressed message after the welcome (the
  user: the bundle idea is good), held in memory for the route; a route
  switch (editor task 10) gets the next route's.
- Only what TSRE loads, by kind: no scan of the route folder (MINI's
  unused folders are not sent), smaller than the 6.7 MB measured for
  every file of PRUDNIK (its paths, 3.8 MB, are not in it).

### 3. A client's load is the local load
- `Route::load` split: the object data (TRK, tsection, TDB, RDB, the
  terrain's quad trees, the REF) from the disk or from the server's
  messages, as today; then the same parse code for both: a local load
  reads each file and parses it, a client parses the bundle's copy
  (the TRK, tsection, TDB and REF already parse from a buffer for the
  network; the others are split into reading and parsing).
- `RouteClient::load`'s own copy of those steps goes.
- `GLOBAL/tsection.dat` from the bundle: both sides number the route's
  sections alike.

### 4. Terrain through the local loader (the user, 2026-11-17)
- A client asks for a tile's files (`request_terrain_files ( x z low )`:
  detailed or distant); the server answers with every file the local
  loader reads, as its live tile would save them (the `.t`, heights,
  flags, patch flags, procedural material map).
- The client runs the local loader's parse steps on them (`Terrain`'s
  loading split into reading the files and parsing them), then drops
  them. Patch flags and material maps arrive as in a local load.
- Distant tiles (`LO_TILES`) and map mode's descriptors the same way,
  asynchronously: the map draws a tile when it has arrived.
- Edits stay messages (tasks 01, 02). `TerrainClient`'s load path goes;
  its edit hooks stay.

### 5. The route's root images from the bundle
`load.ace`, `details.ace`, `graphic.ace` (the TRK's route ID folder):
read by the texture loaders from the bundle's bytes. The first binary
files sent this way (the user, 2026-11-17: a test of image sharing).

### 6. The heavy folders compared
The server also lists its heavy folders' files: name and size (the user,
2026-11-17). A client compares its own: missing files, other sizes, and
files the server has not got are listed in the Errors and Messages window
(not refused). The route's heavy folders and the root's `GLOBAL` here;
`TRAINS` and the root's `SOUND` with the simulation tasks (question 3).

## Checks
Suite `server`, extended: a client game root with only the heavy folders
(the fixture route's files removed from it):
- it loads; its TRK, tsection (global and route counts), TDB, the speed
  posts, signal config, environment, sound lists, forests, car spawners,
  telepoles, track profiles equal a local load's of the same route;
- a client asks only for kinds: no message names a path;
- its terrain tile through the local loader equals a local load's (heights,
  flags, patch flags), a distant tile too;
- a heavy file missing or of another size is listed in Errors and
  Messages;
- a route switch gets the next route's files;
- local loads unchanged: every reader's suite and the app suites (tdb-*,
  terrain-*, new-route, route-load, simulation-route, path-edit,
  activity-edit, editor-tools, map-view, settings, sound, route-switch).

Live check: MINI, EUROPE1 and PRUDNIK served to a client whose game root
has only the heavy folders; captures of the same views from the client and
from a local load compared; the bundle's size and the login time noted.

- `all` built before each commit; ctest and every app suite before the
  feature's merge.

## Built (2026-11-18)
On `feature/server`, the user's go on the revised design.

- **`RouteFiles`** (`src/tsre/fileFunctions`): the loaders' reading of the
  route's data. Local: the disk. A client: its server's files in memory for
  every path in a route's folder outside the heavy folders and in GLOBAL
  outside its shapes and textures; such a path the server did not send does
  not exist (the client's own copies are never read, of any route, so not
  between two routes either). Exists, size, read, SIMIS buffers, images, a
  folder's files (the server's, held), all thread safe (textures load on
  their threads). It keeps the client's game root itself and needs QtCore
  only (images in `RouteFilesImage.cpp`): the test programs that build the
  readers (`tests/ace`, `aceConverter`, `contentCase`, `shapes`, `tokens`)
  build it too.
- **Loaders moved to it, local loads unchanged:** the global tsection and
  its includes (`FileBuffer::insertFile`), speed posts, sigcfg, the env
  files and their weather maps, the env file list, sound sources and
  regions, forests, car spawners (and the OPENRAILS one), telepoles, track
  profiles (their folder's files), PROCEDURAL (OBJ files and textures),
  `terrainmaterials.dat`, `humans.dat`, ACE and image textures (the route's
  images, PROCEDURAL), display documents (RmlUi's file interface) and
  their image sizes, the terrain's tile files (`.t`, patch flags, material
  maps, flags).
- **The bundle** (`RouteBundle`, `src/tsre/world`): the server gathers what
  TSRE never writes at its start (as it read them: the tsection the load
  uses, OR's or GLOBAL's, with its includes; the `.dat` lists; the route's
  images; TRACKPROFILES, PROCEDURAL, DISPLAYS) and, at each login, the env
  files with their weather maps, `terrainmaterials.dat` and the heavy
  folders' names and sizes. One binary record
  (`TSRE_Requested_Route_Files`), compressed by the frame layer, the answer
  to `request_route_files ( )`, the client's first request. Files over
  32 MB, or past 192 MB in all, are left out and told on the server.
- **The client's load:** the files held (`RouteFiles::setServed`), the
  caches by route folder read again, then the TRK and the rest as before;
  `Route::loadRouteLists()` is the local load's code for the sound lists,
  forests and car spawners, both loads run it. The game root needs ROUTES
  and GLOBAL only (`Game::checkClientRoot`). A route switch drops the
  files and gets the next route's; a reconnect to the same route keeps them
  (a snapshot at login).
- **Heavy folders:** compared by name and size (case ignored); missing, of
  another size and not on the server: one message each kind in Errors and
  Messages, with its files.
- **Terrain:** every tile is made by `TerrainLibQt::makeTerrain`; a client's
  is a `TerrainClient` that asks `request_terrain_files ( x z low part )`
  (detailed or distant; all, or the tile file only for map mode; the rest
  asked when map mode's tile is completed). The server answers with its
  live tile's files (`Terrain::filesForClient`: the `.t` as it is now, its
  patch flags, its procedural material map, heights as floats, flags named
  as its save names them); none for a tile it has not got. The client puts
  them in `RouteFiles`, runs `loadDescriptor` and `completeLoad`, and drops
  them. Heights on a client are read as floats: exact, as its edits are.
  The old three-request load is gone; those requests stay for the
  refresh after a reconnect.
- **Map overlays** (`TERRAIN_MAPS`): `request_terrain_overlay ( x z )`, the
  server's image held, else its file (`TSRE_Requested_Terrain_Overlay`); a
  client saves none.
- The client keeps its last 500 requests (`RouteEditorClient::requests`,
  the suite's check).

Results 2026-11-18:
- `all` built; ctest 11/11; suite `server` 141/141 (new: a client root with only the heavy folders;
  its track sections, speed posts, signal config, sound lists, forests,
  car spawners, telepoles, environment and env files as a local load's; its
  terrain tile through the local loader with the server's edited heights
  and flags; requests of kinds only, no path or file name; the three heavy
  differences told; a tile's files answered as the tile file only, all of
  them, or none; the other route's files after a switch); `tokens`
  1640/1640 (the three new tokens pinned); `route-switch` 16/16.
- local loads unchanged: sound, weather, sky, environment-window, sigcfg,
  displays, rmlui, station-people, forest-objects, orts-profile,
  dyntrack-road, procedural-policy, content-path, tdb-load, tdb-ordering,
  tdb-editing, tdb-fields, track-cant, terrain-tfile, terrain-grid,
  terrain-edges, terrain-brush, terrain-normals, terrain-material,
  terrain-material-gl, terrain-mesh-gl, quadtree-recovery, new-route,
  settings, editor-tools, map-view, path-edit, simulation-ui,
  activity-edit, token-world, selection-id, local-lights, time-of-day all
  pass; terrain-files on MINI's tiles (1), PRUDNIK's (900) and its distant
  ones (6): all loaded; route-load on MINI and PRUDNIK, path-roundtrip on
  MINI and EUROPE1 (24 of 24 paths), simulation-route on EUROPE1 pass.
- live (llvmpipe; a server on copies of the routes, a client whose game
  root has only the heavy folders, symlinked, beside a local editor on the
  server's copy):
  - MINI: 13 files, 0.3 MB, and 682 heavy files listed; the 3D views equal
    (0 pixels differ);
  - EUROPE1: 14 files, 1.8 MB, 1565 heavy files; 1235 objects on both;
    427 of 629,000 pixels differ, in the trees: the local view differs
    from itself by 164 a few seconds later (wind);
  - PRUDNIK: 137 files, 6.4 MB, 69,027 heavy files listed (the record
    12.2 MB before compression), in 0.8 s after the load began; 2734
    objects on both, the 3D views equal (0 pixels); map mode equal, zoomed
    out on the whole route too, but for station labels (the local load has
    the route's markers, a client none yet); the client loaded all 6
    distant tiles and its 14 detailed ones from the server, none failed.
  - Differences that stay: the Navi window's markers and places (not on a
    client yet, question 1).

Left: markers, activities, paths and the like on a client (question 1, by
messages when a task needs them); the heavy folders' list is most of
PRUDNIK's record (69,027 names): fine at a login; `TRAINS` and the root's
`SOUND` compared with the simulation tasks.

## Questions
Answered by the user on 2026-11-18:
1. Markers, activities, services, traffic, paths, timetables on a client:
   not sure; activity editing will be a permission too; the activity tools
   stay off. So not now (step 1), unless a check needs them.
2. "Every listed file" meant every file a scan of the route folder finds
   outside the heavy folders and the object data. Gone with the revision:
   the bundle holds only what TSRE loads, by kind.
3. Heavy folders: the route's and `GLOBAL` now, `TRAINS` and the root's
   `SOUND` with the simulation tasks: yes.

The revision: accepted ("Ok, now makes sense, proceed", 2026-11-18).
