# Task 01 - Restore the server and client mode

Status 2026-11-17: design. Not started; starts on the user's go.
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

## Questions
Answered by the user on 2026-11-17: B4 renumbering only in the saved
file; B3 no tile making here (a separate action, later editor task); B6
reconnecting on its own. None open; the task waits for the user's go.
