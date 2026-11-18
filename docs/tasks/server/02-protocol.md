# Task 02 - The protocol and the ID ranges

Status 2026-11-18: built on `feature/server` (worktree `../TSRE5vc-server`),
the user's go ("continue", 2026-11-18); not merged: the tasks stay on the
branch until the feature is complete, then merge (the user). Suite
`server` (124 checks) passes; live check on MINI and EUROPE1 done. What
was built and where it differs: "Built" below. Question 1 still open.
Roadmap: [00-task-roadmap.md](00-task-roadmap.md) (Messages, New IDs,
Security). Task 01: [01-restore.md](01-restore.md). Findings (C, S, P
numbers): [reports/server-client-review.md](reports/server-client-review.md).

## Goal
- One frame layout for every message, a version check and a login whose
  password never crosses the wire, a reply to every request, errors back
  to the sender, the server's changes numbered, size limits, compression.
- Two users editing at once never take the same new ID (C1, C2 and the
  route tsection's dynamic track records).
- A client that loses the server keeps its route and refreshes what it
  has loaded when the server is back: no restart (task 01's stop gap).
- `wss://` addresses on the client; one permission check on the server.

Not in this task: route files from the server (03); versions per record
and one transaction per edit (the editor set: two users moving the same
object still means the last one wins); TLS inside the server; permissions
beyond "everyone may do everything"; loading or switching a route in the
running editor (a task of its own, the user 2026-11-18).

## Steps

### 1. Frames
Every WebSocket message is one binary frame:

| bytes | field |
|---|---|
| 4 | mark `TSRN` |
| 2 | protocol version (1) |
| 1 | kind: 1 text records, 2 a binary record |
| 1 | flags: bit 0 compressed |
| 4 | request number (the sender's; a reply carries the request's) |
| 4 | change number (the server's, on a change; else 0) |
| 4 | payload length |

The payload is today's: UTF-16 text with its byte order mark, of ParserX
records (kind 1), or a `NetworkToken` record as it is, `B` first (kind 2).
Little endian, as the binary records are. Request number 0 asks for no
reply (the pointer's position).
- **Compressed:** payloads over 16 KB, zlib (`qCompress`). Its stated
  size is checked against the limit before it is uncompressed.
- **Limits:** a message from a client at most 8 MB (an edit record or a
  terrain tile); from the server 256 MB uncompressed (PRUDNIK's TDB is
  20 MB of UTF-16). `QWebSocket::setMaxAllowedIncomingMessageSize` on both
  sides; a larger message closes the socket with a log line (S4).
- **A frame without the mark** (today's TSRE) closes the socket with
  "protocol: update TSRE", before anything is read.
- One place builds and reads frames (`NetworkMessage`); the server, the
  client and the scripted clients of the suite all use it.

### 2. Hello, challenge, welcome
1. client: `hello ( protocol ( 1 ) tsre ( "<version>" ) user ( "<name>" ) )`;
2. server: `challenge ( session ( "<id>" ) nonce ( "<32 random bytes, hex>" ) )`,
   or `refused ( "protocol 1 here, 2 there" )` and closes. TSRE versions
   may differ (shown in `users` and the server's log); the protocol must
   match;
3. client: `login ( answer ( "<hex>" ) )`, HMAC-SHA256 keyed by the
   password over the nonce and the user name (`QMessageAuthenticationCode`);
4. server: `welcome ( user ( "<name>" ) session ( "<id>" ) route (
   "<name>" ) tsre ( "<version>" ) change ( <last> ) allowed ( view edit ) )`;
   the client then loads the route as before (the welcome replaces
   `set_username` and `load_route`); or `auth_fail` and closes.

- With authentication off (the default) the answer is not checked.
- `users.txt` stays `user=password` in plain text: the server needs the
  password to check the answer. The password is no longer sent (S3).
- Anything before the welcome other than `login` closes the socket;
  task 01's 10 s timeout stays.
- The session id is new at every server start.

### 3. Replies, errors, numbered changes
- **Every request has a number** and gets a reply carrying it: the data
  asked for (`requested_tile`, ...), `done ( first last )` for edits
  (the change numbers they got), or `error ( "<why>" )`: an unknown
  record, a refused record (task 01's reasons), not allowed, too large.
- **The server numbers every record it accepts** (from 1 in a session) and
  forwards it to every other logged-in client with its number. **The
  sender gets the numbers, not its record back** (the roadmap said the
  record): it applied the edit already, and applying its own record again
  would replace the object it may still be dragging.
- **A refused edit is put right:** the server's reply carries its own
  version of what the record touched: its object (or the sender's object
  removed, when the server has none), its TDB node or item (or a
  removal), its terrain file as an edit. The client applies it as any
  received edit and ends as the server has it; no second round trip.
  Task 01 left it different, with a notice; the notice stays. Track
  sections and shapes are not put right: only global ones are refused,
  and both sides read those from the same file.
- **Nothing points at a replaced object:** when a received record,
  a put-right or a refresh replaces an object, the editor drops what
  holds the old one: the selection (also a group holding it) and with it
  the properties windows and a tool's half-done action; on a refresh
  also error messages' links and fixes. (The camera follows trains only,
  never a world object.) Today
  a received edit of the selected object leaves the selection on the old
  copy, and the next drag sends that copy. The old objects are kept, not
  freed, as received edits keep them today (`Tile::replaceWorldObj`);
  freeing them belongs to the route-switch task.

### 4. One permission check
`RouteEditorServer::allowed(client, action)`, action view or edit for now
(save, run control, drive, watch later). Every request goes through it;
everyone logged in may do both. The welcome lists what the user may do;
a client without `edit` keeps the editing off (as while disconnected).

### 5. ID ranges
**Getters first** (local behaviour unchanged):
- the route tsection's records for dynamic track: `TSectionDAT` takes
  two section IDs and one shape ID through getters
  (`TDB::fillDynTrack` takes `routeMaxIdx` and `routeShapes` inline);
- world object UiDs: `Tile::newUiD(soundItem)` for the two inline places
  (`Tile.cpp`, `++maxUiD`, `++maxUiDWS`). Deleting the highest UiD steps
  `maxUiD` back only locally; on a client the UiD belongs to its block.

**Blocks** (the roadmap's design):
- kinds: TDB nodes, TDB items, RDB nodes, RDB items, route tsection
  sections, route tsection shapes, and UiDs per tile (objects and sound
  items apart, as the two counters are).
- `id_block ( <kind> <first> <count> [<tile x> <tile z>] )`: the TDB and
  tsection kinds after the welcome, a tile's UiDs with the tile's answer;
  only to a client that may edit. The client asks for the next block
  (`request_ids ( <kind> )`) when half of one is used and holds at most
  two per kind.
- sizes: 256 for nodes, sections, shapes and UiDs, whose unused IDs cost
  nothing (nodes are closed up at save, task 01; the route tsection has
  holes today: every straight piece takes two section IDs and uses one;
  UiD gaps are harmless); 32 for items.
- the server hands out free item slots first (the `emptyitem`s, items
  removed since, a disconnected user's unused ones), then new IDs at the
  end. Nodes, sections, shapes and UiDs: new IDs at the end only; their
  unused ones are not given again (their gaps cost nothing).
- the server's maximums (`iTRnodes`, `iTRitems`, `routeMaxIdx`,
  `routeShapes`, a tile's `maxUiD`) move only when an ID is used, so the
  save ends the item table at the last used item. Unused IDs between
  used ones are written as `emptyitem`s (task 01 writes every slot).
- **a new ID outside the sender's blocks is refused** (an update of an
  existing record is not new). A client without ranges cannot collide.
- a disconnected client's unused item IDs go back to the free pool.
- a tile's UiD block comes with the tile once, while the user holds some
  of it; `request_ids ( <kind> [<x> <z>] )` gives the next block as its
  reply.
- `increase_tdb_itrnode` and the other `increase_*` messages go.
- **running out mid-edit** (rare: a paste of many signals before the next
  block arrives): the getter waits for the block, up to 10 s, with user
  input held and the view neither ticked nor drawn; messages that come
  meanwhile are applied after the edit, in order. If no block comes, the
  getter takes a local ID, the server refuses the edit and puts it right,
  and the user is told. Question 1.

### 6. Refresh after a reconnect
Replaces task 01's restart:
- the client logs in again (hello, challenge, welcome);
- it drops what points into the route (step 3's list) and stops a
  simulation running in the editor;
- it asks again for the route tsection, TDB, RDB, every loaded W tile and
  every loaded terrain tile (T, Y, F), and replaces them in place. The
  TDB is a new object as at load (`Route::loadTdbData`); a path resolved
  on the old one resolves again (`Path::resolvedOn`);
- new ID blocks.

Always a full refresh of what is loaded, not a replay of missed changes:
one path, and after a server restart the server's numbers differ anyway
(its saved TDB has its nodes closed up).

### 7. `wss://` on the client
- `core.network.clientLogin` takes `user:password@wss://host:port` (and
  `ws://`; no scheme stays `ws://`).
- Qt's TLS backend missing (`QSslSocket::supportsSsl()`): a clear error.
  OpenSSL on Linux; on Windows the MinGW build's backend is checked by the
  user (Schannel or OpenSSL).
- On the internet: the server plain `ws://` behind a TLS proxy (Caddy or
  nginx); the setting's description says so.
- `TSRE_SERVER_TLS_CA` (developer switch): a certificate file trusted
  besides the system's (the suite's self-made one, a private proxy's).

## Built (2026-11-18)
Commits on `feature/server`: 6c5bab34 (this design), 30114a3f (steps 1-4),
21d7c42e (steps 3 and 6), fc6b1b8e (step 5), 428f19c9 (step 7), d88a0608
(the live check's fixes).

Where it differs from the steps above (each written into its step):
- a refused edit is put right by the server's reply, not by the client
  asking again (step 3);
- only item IDs are given again; the other kinds' unused IDs cost
  nothing (step 5);
- with no block after 10 s the edit takes a local ID and the server
  refuses it and puts it right (step 5);
- a client whose route had not finished loading still starts itself
  again on a reconnect; one with its route loaded refreshes in place
  (step 6);
- a live flex, ruler or telepole whose object received data replaced is
  abandoned, not cancelled: a cancel acts on the route by UiD and would
  reach the replacement (found in the live check);
- the client sends nothing but its login before the welcome (found in
  the live check).

Found in the live check, fixed:
- a client refused another user's new node above its own count ("node
  260 is not between 1 and 6"): the `increase_*` messages that raised the
  counts are gone, so a client now takes the server's new records in;
- on a reconnect the 3D view's pointer was sent before the login; the
  server closed the socket, for ever;
- dropping a selection cancelled the user's live flex, and the cancel
  removed the other user's replacement locally (8 objects against 7);
- older: a cancelled placed piece (the live flex's next piece) was removed
  only locally; the server and the others kept it. Its removal is sent;
- the suite's server process outlived a suite killed by a timeout (eight
  idle servers from task 01's runs); it now dies with the suite (Linux).

Not this task's, seen again: a dynamic track read from a file or the
server draws magenta until its texture is loaded by a placement (task
01 saw it in a local load too).

Developer switches: `TSRE_SERVER_TLS_CA` (new), with task 01's
`TSRE_SERVER_LOGIN_TIMEOUT_MS` and `TSRE_SERVER_SUITE_KEEP`.

## Checks
Suite `server`, extended (its 60 checks move to the new frames):
- version: a frame without the mark, and protocol 2, are refused before
  any route data;
- login: the password is in no byte the client sends; a wrong answer and
  a replayed answer (another session's challenge) are refused; with
  authentication off any answer is taken;
- every request gets a reply with its number; an unknown record and a
  refused record give `error` to the sender only;
- changes: numbered without gaps, the others get the records with their
  numbers, the sender gets `done` with the same numbers and not its
  record;
- a refused edit is put right: the client's tile and TDB equal the
  server's after it;
- limits: a 9 MB client message closes the socket; a compressed payload
  stating a size over the limit is refused unread; the TDB arrives
  compressed and equal;
- ranges: two clients add track, items, dynamic track and objects on one
  tile at once: no ID taken twice, the server's save equals a local save
  of the same edits; free `emptyitem` slots handed out first; a
  disconnected client's IDs handed out again; the item table ends at the
  last used item; a new ID outside the sender's blocks refused;
- refresh: a client connected while the server restarts from its save
  (another client's saved change kept, an unsaved one lost): afterwards
  its TDB, tiles and terrain equal a fresh client's, in the same process;
  the selection dropped;
- `wss://`: a client reaches a secure test server (certificate made with
  `openssl` when on the PATH; else skipped, said in the log).
- local editing unchanged: tdb-editing, tdb-fields, new-route,
  track-cant, editor-tools and the other related suites.

A live check at the end, as in task 01: MINI and EUROPE1, two clients
laying track and placing signals at once on one tile; the server killed
and started again; both clients refresh and agree with a fresh one.

- `all` built before each commit on the branch; ctest and every app
  suite before the feature's merge.

Results 2026-11-18:
- suite `server` 124/124, all of the above but two: the ranges' save is
  not compared with a local save (the restarted test server has no
  console), and item ranges are checked with scripted clients only;
- related suites as in task 01 (tdb-load, tdb-ordering, tdb-editing,
  tdb-fields, track-cant, the terrain ones, new-route, settings 303,
  editor-tools, map-view, simulation-ui, activity-edit, tokens,
  token-world, selection-id); route-load on MINI, simulation-route on
  EUROPE1; ctest 11/11;
- live check: MINI and EUROPE1, two clients under Xvfb placing a track
  shape and laying flex track at the same moment on the same tiles: UiDs,
  nodes, sections and shapes from separate blocks, nothing refused, both
  showing the same objects. Client two moved client one's live flex
  piece: client one's selection and flex dropped, its objects as the
  server's. The server saved, an unsaved move followed, the server was
  killed (`kill -9`) and started again: both clients logged in again in
  the same process, refreshed, the unsaved move gone, and agreed with a
  fresh client (7 objects on MINI's tile, 1237 on EUROPE1's view, the
  same pieces). The clients' route folders unchanged. No signals were
  placed live; items are the suite's.

## Questions
1. Running out of IDs in the middle of an edit (a paste of many signals
   faster than the next block arrives): the edit waits for the block, up
   to 10 s, with user input held (proposed), or the edit is refused?
   Built as proposed; after 10 s the edit is refused and put right.
