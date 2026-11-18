# Task 03 - Route files from the server

Status 2026-11-18: design, written on the user's "Ok" to writing it; not
built (a go to build is separate). On `feature/server` (worktree
`../TSRE5vc-server`). Three questions below.
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

## What a client still reads from its own disk
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

### 1. One route file reader
`RouteFiles` (new, `src/tsre/fileFunctions`), in front of `ReadFile`:
- `read(path)`: the file's contents as a `FileBuffer`, or none;
  `bytes(path)`, `exists(path)`, `entries(folder, filters)` (folder
  listings: activities, paths, profiles... are found by listing);
- local: the disk, as today; a client: the server's copy (step 2);
- for a route's files and `GLOBAL`'s text files; paths as `ContentPath`
  makes them, no case search (TSRE's rule).

Every reader of those files moves to it, with no change for a local load
(each reader's own suite, and the app suites). The heavy folders and the
object data keep their own way.

### 2. The server's list and bundle
- At its start the server lists the route's files outside the heavy
  folders and the object data, and `GLOBAL/tsection.dat` with its
  includes (and the route's `OPENRAILS/tsection.dat`): path, size, hash.
- After the welcome a client gets the list and, compressed, the files
  themselves (question 2: all of them, or only those TSRE reads). Held in
  memory for the route; a route switch (editor task 10) gets the next
  route's.
- The server serves only files on its list, nothing outside the route
  and those `GLOBAL` files.
- The rule (the user, 2026-11-17): a file a client may edit moves to
  messages in the task that lets it. Today a client edits none of them
  (task 01 refuses the environment's save, the activity tools are off).

### 3. A client's load is the local load
- `Route::load` split: the object data (TRK, tsection, TDB, RDB, the
  terrain's quad trees, the REF) from the disk or from the server's
  messages, as today; then the same steps for both through the reader:
  environment, speed posts and signals, sound lists, forests, car
  spawners, weather transitions, markers and places, services, traffic,
  paths, activities, timetables (question 1), telepoles, track profiles.
- `RouteClient::load`'s own copy of those steps goes.
- `GLOBAL/tsection.dat` read through the reader: both sides number the
  route's sections alike.

### 4. Terrain through the local loader (the user, 2026-11-17)
- A client asks for a tile's files (`request_terrain_files ( x z low )`:
  detailed or distant); the server answers with every file the local
  loader reads, as its live tile would save them (the `.t`, heights,
  flags, patch flags, procedural material map).
- The client hands them to the reader for that tile and runs the local
  loader (`Terrain::load`), then drops them. Patch flags and material
  maps arrive as in a local load.
- Distant tiles (`LO_TILES`) and map mode's descriptors the same way,
  asynchronously: the map draws a tile when it has arrived.
- Edits stay messages (tasks 01, 02). `TerrainClient`'s load path goes;
  its edit hooks stay.

### 5. The route's root images through the reader
`load.ace`, `details.ace`, `graphic.ace` (the TRK's route ID folder):
read by the texture loaders from the reader's bytes. The first binary
files the reader serves (the user, 2026-11-17: a test of image sharing).

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
  markers, activities and paths equal a local load's of the same route;
- its terrain tile through the local loader equals a local load's (heights,
  flags, patch flags), a distant tile too;
- a file not on the list, and a path outside the route (`../`), are refused;
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

## Questions
1. Markers, places, activities, services, traffic, paths and timetables on
   a client: loaded read-only now, through the reader (a client then shows
   what a local load shows; its activity tools stay off), or left out
   until clients may edit them (the editor set)? Proposed: loaded now.
2. The bundle: every listed file (PRUDNIK 14.5 MB, 6.7 MB compressed;
   EUROPE1 1.8 MB compressed; MINI's unused 14.8 MB folders too), files
   over 16 MB left out and named; or only the files TSRE reads (a second
   list to keep in step with the readers)? Proposed: every file.
3. Heavy folders compared now: the route's and the root's `GLOBAL`;
   `TRAINS` and the root's `SOUND` (tens of thousands of files on OR_PSK)
   with the simulation tasks, where the server runs the trains. Proposed
   as written.
