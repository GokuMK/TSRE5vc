# Task 01 - Restore the server and client mode

Status 2026-11-18: built on `feature/server` (worktree `../TSRE5vc-server`),
not merged. Suite `server` (60 checks) passes; the live check on MINI and
EUROPE1 done. One question for the user (below). What was built and where
it differs from the steps: "Built" below.
Roadmap: [00-task-roadmap.md](00-task-roadmap.md). Findings (B, C, S
numbers) and file:line: [reports/server-client-review.md](reports/server-client-review.md).

## Goal
Today's mode works as it was meant to, **on today's messages**:
- a server runs for days with or without a console;
- clients load and edit, see each other's edits, and lose nothing;
- nothing is written into a client's route folder;
- a test suite of a server and clients proves it, and keeps proving it.

Not in this task: the new message layout (02), route files from the
server (03), the collisions between users' new IDs (task 02's ID ranges;
what ranges leave, the editor set).

## Steps

### 1. A test suite first
Suite `server` (`--test --test-suite server`), content-free where it can
be, so it runs with the others before every commit:
- **a server in the test process** on a tiny route made for the test (a
  few tiles, a TDB with one junction, one terrain tile), written into a
  scratch folder;
- **scripted clients:** `QWebSocket`s that send today's messages and
  record what comes back. They test the server's rules without a window:
  login, refusals, what is forwarded to whom, unknown tiles, malformed
  records;
- **a real client** for the load: `RouteEditorClient` + `RouteClient`
  under `QT_QPA_PLATFORM=offscreen` against that server, checking the TRK,
  TDB, tiles and terrain arrived, and the camera's height (B1).

A live check besides the suite, at the end: a server on `MINI` and on
`EUROPE1` (scratch copies), two clients under Xvfb, each adding, moving
and deleting objects, laying a flex track, raising terrain; both clients
and the server agree; the server saves; a fresh load equals what the
clients showed.

### 2. Broken items
- **B1 The camera under the terrain.** When the start tile's terrain
  arrives (`TerrainLib::terrainAvailabilityChanged`), set the camera 20 m
  above it, as a local load does, unless the user moved the camera
  first. Also for a `core.startup.camera` position.
- **B2 The console.** It works today (tested 2026-11-17: `users`, `save`,
  `quit`) and is the server's only interface (the user). Fixes and
  additions:
  - stop reading at the end of input (stdin closed: no console, the
    server runs on); no `processEvents` in the reading thread;
  - `help`; `status` (route, users, unsaved changes); `kick <user>`;
  - `save` answers with what it wrote, or what failed;
  - the commands stay: `quit`, `save`, `users`.
- **B3 Tiles the server has not got.** Making a tile is an action of its
  own, for a later editor task (the user, 2026-11-17). In this task:
  - the server forwards an object only if it applied it; an object on a
    tile with no W file is refused, not kept and not forwarded;
  - a request for a tile the server has not got gets an answer (`no tile`);
    the client marks the tile missing and refuses placing objects on it,
    with a message (tiles cannot be made from a client yet);
  - look-ups use `value()`/`contains()`, not `operator[]`, so no NULL
    entries are made.
- **B4 The save's renumbering: only in the saved file** (the user,
  2026-11-17). The server's save writes the TDB's and RDB's nodes
  numbered without holes **through a map applied while writing** (nodes,
  pins, signal junction IDs: what `deleteNulls` changes today) and keeps
  its numbers in memory.
  - Nothing is sent to the clients: their numbers are the server's memory
    numbers, which do not change.
  - The next server start reads the compacted file; clients connecting then
    download its TDB with those numbers.
  - The next save makes its map again from memory, so saving twice gives
    the same file.
  - The local editor saves as today (renumbering in memory).
  - Rejected: renumbering in memory and making every client reload its
    TDB and RDB after each save (a client's selection and open windows
    would point at old records).
- **B5 Terrain flags.** The server sends the F file
  (`Terrain::saveFfileToStream` exists; its call is commented out) and
  forwards F edits it applied.
- **B6 Disconnecting.** A client does not quit. It shows the disconnection
  (status bar and the Errors and Messages window), stops sending and
  refuses edits, keeps the view, and **reconnects on its own** (the user,
  2026-11-17: automatic if it fits): after 2 s, then longer waits up to
  30 s. On reconnecting it loads the route again from the server, since
  others may have edited meanwhile. A failed login says so and does not
  retry.
- **B7 Received records.** IDs checked (in range, existing where they must
  exist) before anything changes. A terrain T file is parsed into a new
  `TFile` and swapped in only when it is whole; a failed one is not marked
  modified or forwarded. Replaced records freed only where nothing keeps a
  pointer to them (checked one kind at a time; where unsure, kept).
  A received item `remove` makes the item an `emptyitem`, as a local
  deletion does, never an empty slot: the save would skip it and Open
  Rails would then shift every later item (review, section 6).
- **B8 Buffers.** A `FileBuffer` that reads the socket's bytes without
  owning them (a borrowed view), freed after each message.
  `TerrainClient::loadTFile` frees the descriptor it replaces.
- **B9** Each edit forwarded once, on its own.
- **B10** `users_info` sent only when something in it changed; the pointer
  only when it moved; the old `Ref` freed.

### 3. Security that cannot wait
- **S1** Only logged-in sockets get edits and `users_info`; a socket that
  does not log in within 10 s is closed.
- **S2** The authentication setting read one way (empty = anyone, `file` =
  `users.txt`); `users.txt` beside the route or given by `--users`, not the
  working directory.
- Usernames cleaned before the duplicate check; a duplicate gets a number
  added, not a random name.
TLS is not now (roadmap, "Security"); the challenge login comes in task 02.

### 4. A client writes nothing into its route folder
In client mode these are refused, with a message, until task 03 or the
editor set sends them to the server:
- `Route::newTile` and auto new tiles (today a client's placeholder tile
  makes it write `TILES` and `WORLD` files locally);
- saving the environment (`saveEnvironment`), quad tile creation and
  deletion, terrain overlays.

One check for all of them: a client's route folder has the same files,
with the same times, before and after a session (the live check and the
suite).

### 5. The receive rule, written down
Received changes are applied by functions that never send, and only the
user's own edit sends. In this task:
- the rule as a comment where the client applies messages;
- `Tile::wczytajObiekty` on a client does not send what `loadingFixes`
  changed (it records it, as the server's own load does);
- the suite: a client that receives every kind of edit sends nothing back.

## Built (2026-11-18)
Everything in the steps, with these differences and findings.

**Differences from the steps**
- **The suite's server is a process**, this program with `--server`, as a
  user starts it (step 1 said in the test process). One process holds one
  route (`Game::trackDB`, `Game::terrainLib`...), and the real client needs
  them. Scripted clients (`QWebSocket`) and the real client
  (`RouteEditorClient` + `RouteClient`) run in the test process. The route
  is RouteCreator's (needs `assets/templateRoute_0.6`, as a new route does)
  with a small `GLOBAL/tsection.dat` and a TDB written by the test.
- **B1 is not in the suite** (it is the 3D view's); seen in the live check.
  A `core.startup.camera` position has its own height, as in a local load,
  so it is kept.
- **B6, loading the route again:** when the server is back, the client
  starts itself again (same arguments, the camera as
  `core.startup.camera`) and loads the route from the server. The editor
  has no way to replace a loaded route in the running process (windows,
  the renderer and the selection keep pointers into it). While
  disconnected, the central area is disabled: the view stays, nothing can
  be edited, the status bar says so. Question 1 below.
- **B8:** each message is copied into a buffer the parser owns, freed
  after it (not a borrowed view: `FileBuffer` frees its data, and
  `toUtf16` may replace it).
- **B7:** each record is parsed from its own copy, so a parser cannot read
  into the next record (a malformed track section did).

**Found on the way, fixed**
- `quit` never ended the server: the console thread waits on stdin with
  the C library's lock held, and the exit, flushing every stream, waits
  for that lock. The console now reads its descriptor.
- Terrain flags never arrived over the network: `Terrain::readF` read from
  the start of the buffer, not its place (the message's envelope came
  first). Gap edits were not sent at all; now they are (`updateFFile`).
- A client's TDB from the server lacked the signals' directions a local
  load completes from the `.tit` (`checkTrSignalRDirs`); added.
- Placing an object sent records with no object in them (a track object
  writes nothing before its place is set); the server now refuses those,
  so the client no longer sends them.
- An empty item slot is written as an `EmptyItem` (TDB and `.tit`).

**Developer switches** (environment, read once): `TSRE_SERVER_LOGIN_TIMEOUT_MS`
(the suite: 1.5 s), `TSRE_SERVER_SUITE_KEEP` (the suite keeps its route and
the servers' output).

**Live check (2026-11-18)**, MINI and EUROPE1 copies, a server and two
clients under Xvfb, a third client and a local load to compare:
- start: terrain shown, camera above it (B1); the other user's pointer;
- added, moved (keys), deleted objects, raised terrain, a flex piece: the
  other client showed each; refused records shown in the status bar;
- `status`, `save` (says what it wrote), `kick` (the client says so and
  stays out), `quit`; a server with no console;
- a fresh client and a local load of the saved route showed the same
  scene; the server restarted: all three clients came back by themselves
  at their cameras;
- the clients' route folders: the same files, sizes and times after.
- Seen, not of this task: a flex piece left half made (its first click)
  is drawn textured in the sessions that saw it made and magenta after a
  reload, locally too.

## Checks
- Suite `server`: login and refusal; edits forwarded once and only to
  logged-in clients; an object on a tile the server has not got refused
  and not forwarded; `no tile` answered; a malformed record refused without change; terrain T, heights
  and flags round trip; a save leaves the server's node numbers unchanged
  in memory and writes a file without holes that loads to the same
  track; a receiving client sends nothing; the client's camera above the
  terrain; a client reconnects after the server restarts.
- A server with stdin closed: under 1 % CPU when idle.
- The live check above, on `MINI` and `EUROPE1`.
- `all` built, ctest, the app suites (settings, simulation-ui...) as for
  every commit.

Results 2026-11-18: suite `server` 60/60, all but the camera (live check);
the idle server with its input closed: 0 to 1 clock ticks in 3 s; the live
check above. ctest and every app suite: before the merge.

## Questions
Answered by the user on 2026-11-17: B4 renumbering only in the saved
file; B3 no tile making here (a separate action, later editor task); B6
reconnecting on its own.

Open (2026-11-18):
1. After reconnecting, the client starts itself again where its camera
   was, to load the route from the server. Good enough, or should the
   route be replaced in the running editor (a larger change: every window
   and cache holding route objects)?
