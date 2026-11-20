# Server Task Roadmap

TSRE's server and client mode: restore it, finish it, then run the
simulation on the server for every player, with the people the players
move.

Status 2026-11-20: tasks 01 to 05 and editor task 10 in main
(`feature/server` merged, 5fe41104, the user's go after the review; not
pushed). The user had first set the merge after task 06; with the review
done and no saved file of a normal route changing, it came before. Task 06
goes on on `feature/server` (worktree `../TSRE5vc-server`). The review
and its fixes: [reports/review-2026-11-20.md](reports/review-2026-11-20.md).
The first review: [reports/server-client-review.md](reports/server-client-review.md).
It replaces the deferred notes in
[../core/server-client-rework.md](../core/server-client-rework.md).

## The user's goal (2026-11-17)
1. Review the architecture and the messages. Any issues? An earlier one,
   a client's message looping between server and clients, should be fixed.
2. Fix what is broken.
3. Finish the mode as designed: **the server gives every text file; a
   client keeps only the heavy route folders** (`SHAPES`, `TEXTURES`,
   `TERRTEX`, other heavy files?). Trains and shared assets are the
   client's to have. A route must load well without the other files.
4. **The simulation on the server.** Is the boundary between the editor and
   the simulation good enough for that? Clients send messages to the
   remote simulation and get its results; nothing runs locally.
5. **People the players move**, kept by the simulation like trains. Their
   physics is on the render side; the simulation only checks simply that a
   person is where it is allowed to be.
6. When the simulation works, the next set: the editor over the server:
   fixes, editing actions it lacks, TDB editing collisions.

## The user's answers (2026-11-17, after the review)
Answers, not a go: the set stays in design.
- **Terrain comes from the server** (`TILES`, `LO_TILES`, `TD`).
- **Building the run's input in the editor is fine:** the server is TSRE
  (the editor without its window). A server with only the simulation would
  be lighter to host, but it is not needed.
- **Trains on the client, to decide later.** Editing needs none. Watching
  a run may be allowed with missing trains. Driving needs the whole train
  the player drives, at least.
- **New IDs:** the user asked for the proposal. The easier way is a range
  of IDs for each user to work in. Asking the server for each new ID is
  more work, and needs a review of whether the TDB takes every new ID
  through its getters first. Reviewed the same day: see "New IDs" below
  and the review's section 6.
- **The server is for the internet too.** Whether TLS is worth doing now is
  open (see "Security" below).
- **Permissions:** later, users get permissions (who may pause or reset a
  run, and so on). Not built now, but the design keeps a place for them.

Second round, the same day:
- **The route's `SOUND` and `ENVFILES/TEXTURES` stay on the client**, for
  now.
- **`GLOBAL/tsection.dat` is sent by the server**, compressed. Measured:
  MSTS's is 159 KB (8.8 KB compressed), OR_PSK_NEW's 3.7 MB (174 KB).
- **ID ranges go in task 02.**
- **Trains run on the server; a driver only sends controls.** Two players
  may board one cab, which works only if both just send controls. So no
  client supplies its loco's force.
- **The route file reader is accepted**, on one rule, written down below:
  a file that becomes editable on a client moves from the reader to
  messages. (Revised 2026-11-18, the user's answers to task 03: no reader
  of paths; the route's data by kind in one bundle, see "Route files from
  the server".)
- **Runs start only from the server's console, for now.** The console is
  important: the server has no window. (It works: tested the same day.)

Third round, the same day:
- **The route's root images (`load.ace`, `details.ace`, `graphic.ace`) come
  from the server**: a good test of sending image files.
- **Heavy folders compared by name and size** only; syncing them is a
  future task.
- **The terrain's first load: the server's live tile**, read by the local
  loader.
- **TLS not now**, as proposed under "Security".
- **Unused item IDs** (the user): `emptyitem` records do not close gaps;
  item IDs cannot have holes, which is why `emptyitem` exists. Closing
  them would need renumbering items at save and rewriting the W files.
  The "New IDs" section now says what ranges cost for items and how to
  keep it small.

## What the review found (short)
Details and file:line in the review.
- **It works end to end.** On `MINI` a server and two clients loaded the
  route over the socket in about 1 s.
- **The old loop is fixed by a convention:** code that applies a received
  edit must not call the methods that send (`setModified`, `TDBClient`'s
  overrides). It holds today; nothing enforces it.
- **Broken:** the camera starts under the terrain on a client; a server
  without a console burns two cores; objects on a tile the server has not
  got are lost on the server but kept by everyone else; the server's save
  renumbers the TDB and the clients keep the old numbers; terrain flags
  never cross; a client quits on any disconnect; received records are not
  checked; one leak per message.
- **Collisions:** clients choose new TDB node and item IDs and world
  object UiDs themselves, so two users adding at once overwrite each
  other. No versions, no transactions, no replies.
- **Security:** sockets that never logged in get every edit and every
  user's position; no TLS.
- **Route files:** only the TRK, route tsection, TDB/RDB, terrain, W files
  and `.ref` come from the server. A client reads about twenty other route
  files from its own disk (`speedpost.dat`, `sigcfg.dat`, the env,
  `ssource.dat`, `forests.dat`, distant terrain...), loads no activities or
  paths, and can write new tiles and env files into its own route folder.
- **The simulation:** the core is ready (an immutable input of plain
  values, value commands, immutable frames with serials, ordered queues).
  What surrounds it is tied to one process: building the input needs the
  editor and a dialog; vehicles are shared editor objects with
  process-local numbers; the main side reads the core's own input object,
  compares frames by pointer and draws on the live TDB; one driven train;
  no player.

## The design

### Who keeps what
- **The server:** a headless TSRE that owns the route. It reads and saves
  every route file except the heavy folders, and later runs the
  simulation. To simulate it also needs `TRAINS`, `GLOBAL` and TSRE's
  scripts (`appdata`). It needs no shapes, textures or sounds.
- **A client:** the editor (or a player) with the heavy folders and the
  shared assets. It reads every other route file from the server and
  writes nothing into the route folder.

The heavy folders (the user, 2026-11-17):
- the route's `SHAPES` (with `.sd`, glTF, e3d), `TEXTURES`, `TERRTEX`,
  `SOUND`, `ENVFILES/TEXTURES`;
- the game root's `GLOBAL`, `TRAINS`, `SOUND`, `TEXTURES` (MaSzyna).

Everything else comes from the server:
- terrain (`TILES`, `LO_TILES`, `TD`): it is edited, and the editor reads
  only 130 MB of it even on PRUDNIK, tile by tile as today. `_e.raw` and
  `_n.raw` (half of `TILES`) are never sent; TSRE does not read them;
- the images in the route's root (`load.ace`, `details.ace`,
  `graphic.ace`), in the route's bundle: its first binary files.

### Messages
Today's records stay: an object, a TDB node or item, a W file, a terrain
tile are sent in the same text and binary forms the route's files and the
current messages use. What changes is around them (task 02):
- **one frame layout** for every message: a mark, the protocol version, the
  kind, a request number, the payload's length;
- **hello and welcome** first: protocol and TSRE versions, user, password;
  a mismatch is refused before any route data;
- **every request gets a reply** with its number, and errors go back to
  the sender;
- **the server numbers every change it accepts** and sends it to every
  logged-in client, the sender included (its confirmation);
- **limits:** a largest message; large payloads compressed;
- **never a loop:** a received change is applied by one function per kind
  that never sends; only the user's own edit sends.

### Route files from the server
Two kinds of route data, sent two ways (task 03).

**Data TSRE edits: the server's objects, as today.** The TRK, tsection,
TDB/RDB, W files, terrain. Today's messages send the server's *live*
objects (`saveToStream`), with everyone's edits since the last save, and
edits travel as records. That stays.

**The rest of the route's data: by kind, from the server (task 03,
revised 2026-11-18 on the user's answers).** A client never names a path
and lists no folder (the user: the server may hold data only in memory,
and listing folders is a security risk). After its login it asks for the
route's files as one kind of data, and the server sends one compressed
bundle of what it chose by kind:
- what TSRE edits and saves at once or on Save (the environment files and
  their weather maps, `terrainmaterials.dat`): as the server's files are at
  the client's login;
- what TSRE never writes once the route is made (`GLOBAL/tsection.dat`
  and its includes, the `.dat` lists, telepoles, track profiles,
  `PROCEDURAL`, `DISPLAYS`, `humans.dat`, the route's images): as the
  server read them at its start;
- the heavy folders' file names and sizes, compared with the client's.

The client holds the bundle in memory (`RouteFiles`) and its loaders read
it with the local loaders' own code; it writes none of it. Map overlays
(`TERRAIN_MAPS`) are the client's own, made, read and saved in its own
folder, never sent: the geo services (OSM, imagery, elevation) take each
user's own credentials and quotas, so they run on the client, never
through the server (the user, 2026-11-18); what they make that is the
route's goes to the server as an edit.
Markers, activities, services, traffic, paths and timetables are not sent
(not loaded on a client).

**Should the object classes load as a local load does?** Not their live
state or edits. But their **first load** could use the local loaders: the
server answers with its live object's text (what `requested_trk` and the
others already send) and the client parses it with the same code a local
load uses. That is worth it where the client's loading duplicates the
local one and misses parts: the terrain (`TerrainClient` skips patch
flags, procedural material maps and terrain flags, and distant tiles go
around it). The TRK, TDB, tsection and W files already parse with the
local classes' own code. **The terrain does it** (the user, 2026-11-17):
the server sends its live tile and the client reads it with the local
loader. Other classes as task 03 finds them.

**The rule (accepted by the user, 2026-11-17): a file that becomes
editable on a client moves from the bundle to messages in the same task.**
The bundle is a snapshot at the client's login, so an edited file sent in
it would show other clients old content. The small files TSRE edits or may
edit later (the env, `terrainmaterials.dat`, and any `.dat` in the bundle;
markers, activities, paths, services, timetables are not sent yet) come
in the bundle while clients may not change them. When a task lets clients
edit one, it gets records (as objects) or a whole-file replace: the client
sends the new file, the server saves it and tells everyone, and the
loaders read it again.

**`GLOBAL/tsection.dat` comes from the server** (the user, 2026-11-17),
with its include files and the route's `OPENRAILS/tsection.dat`: the
route's section numbers start after the global counts, so both sides must
use the same file. Compressed it is 9 KB (MSTS) to 174 KB (OR_PSK_NEW).
Held in memory; the client's own `GLOBAL` is not changed.

The heavy folders are compared with the server's list (names and sizes;
question 2): missing or different files are listed in the Errors and
Messages window.

### The simulation on the server
The answer to point 4: **yes, the core is good enough; the controller
around it is split first.** `SimulationController` today does two jobs.
- **The session** (owner): builds the input, owns the host and the core,
  takes commands, publishes frames, events and messages. On a server it
  lives in the server; alone it lives in the editor as today.
- **The view** (presenter): everything the main thread does with the
  results: placing and drawing the trains, sounds, keys, the cab, the
  Timetable window, the map.
- **A link between them:** local (in one process, as today, no cost) or
  remote (over the server's socket).

First in one process (task 04), so every simulation suite proves nothing
changed, and with what ties it to one process removed:
- vehicles named by their file (the path), not by EngLib's number;
- the input stays built by the editor's code, on the server too (the
  server is TSRE; the user, 2026-11-17); only no progress dialog when
  there is no window;
- frames compared by their serials, not by pointer; times relative to the
  frame, not one machine's clock;
- the view's copy of the input: **the client builds its own from the same
  files and checks it against the server's hash** (reusing `MakeInput`
  rather than writing a serializer for every plan); a mismatch refuses
  the run.

Then over the network (task 05):
- **fewer frames:** the server sends 20 a second (a setting); the client
  draws between the last two (it already extrapolates 50 ms ahead);
- **trains placed from their track position:** a frame carries each
  train's front on the track, its speed and its cars' gaps; the client
  places the cars on its own copy of the TDB, as `placeTrains` does today
  through `Ruch`;
- **interest:** each client's camera (the pointer message already sends
  its tile every 100 ms) decides which trains it gets in full (near) and
  which as a position only (for the map);
- **parts with serials** (aspects, section uses, standing trains, events)
  only when their serial changes;
- **the driven train's values** in full to its driver, every frame (the
  user's "all values" rule, 2026-10-29), and to others only what drawing
  and sound need;
- **heard trains:** the union of every client's heard set; their shadows
  run on the server; sound triggers go to the clients that hear them;
- **the clock:** the server's run is every client's clock;
- **run control:** a server run plays in real time; pause, speed, run to
  and reset from the server's console or a user with that permission
  (permissions come later; see "Permissions");
- **missing trains** (the user, 2026-11-17, to decide later): watching
  may be allowed with trains a client lacks; driving needs the whole train
  driven.

The user's decision of 2026-10-24 holds: the server is authoritative,
clients do not simulate other trains or signals. **Every train, driven or
not, runs on the server; a driver only sends controls** (the user,
2026-11-17): two players may share a cab, which works only that way. The
"force provider" of 2026-10-24 is not for players; an external app
driving a whole train stays a separate link (driving roadmap).

### Players
Task 06: a player is a logged-in user. The core drives several trains,
and **a driven train has a crew**: the players in its cab (two may share
one; the user, 2026-11-17).
- `driven` becomes a set of driven trains, each with its crew; every
  driving command carries its train and its player; a `DrivenFrame` per
  driven train, sent to each of its crew; the 0.01 s step while any train
  is driven;
- every crew member's commands go to the same cab: a lever moved by one is
  where the other sees it in the next frame (the cab's values come from
  the server). Two players moving the same lever at once: the later
  command wins, as two hands on one lever;
- a player joins a driven train's cab or takes a train nobody drives; when
  the last of its crew leaves or disconnects, the AI takes it back;
- keys, the loco panel and the cab bind to the player's train;
- alone, the editor is one player on a local link: the same code.

### People the players move
Task 07, the user's point 5. **The order (the user, 2026-11-20): the
player controller first, then task 07.** After task 06 and the merge, the
humans set's "people the player moves" is built locally: the controller
(controls, camera, collisions, walking, inverse kinematics) with the
person kept by the simulation and its checks, on the local link, reusing
the humans set's walk areas, boarding and aisle routes. Task 07 then puts
it over the network, as task 05 did for trains: no stand-in controller.

The simulation keeps one **person** per player, as it keeps trains:
- its player, its place (on the ground, or in car C of train T at a place
  in the car, or in a cab driving), heading, speed, what it is doing;
- the client moves its own person (controls, camera, collisions, walking,
  inverse kinematics: the humans set "people the player moves") and sends
  its pose 10 to 20 times a second, and what it wants: board a car by a
  door, step off, enter a cab, take the train, leave the cab;
- **the simulation checks only simple things:** not faster than a person
  can move; a door open and the train standing to board or step off; a
  person in the cab to join its crew. A refusal
  comes back with its reason and the client puts the person back where
  the server has it;
- a person in a car is placed relative to the car, so it rides with the
  train smoothly;
- other clients get the persons near their camera in the frames and draw
  them with the crowd's drawing (`src/tsre/humans`), the animation chosen
  from speed and state;
- alone, the same path on the local link.

Passengers already work this way: the simulation counts them
(`SimulationCore::passengersArrive`), the main side walks and draws them.

### The server's console
The server's only interface while it has no window (the user,
2026-11-17). Tested the same day through a pipe: `users` lists the users,
`save` writes the route, `quit` stops the server. Gaps:
- it spins at the end of input (B2), so a server under systemd or `nohup`
  cannot run;
- `save` says nothing back, and writes the TDB, RDB, TIT, RIT and route
  tsection every time, changed or not (as the editor's save does);
- no `help`, no `status`.

Planned:
- task 01: the end-of-input fix; `help`; `status` (route, users,
  unsaved changes); `save` answers with what it wrote; `kick <user>`;
- task 05: the run's commands; **runs start only from the console for
  now** (the user): `run <activity or timetable>`, `play`, `pause`,
  `speed`, `runto`, `stop`, `reset`, `trains`;
- later: the same commands from a client, for a user with that
  permission, so a server without a terminal (a service) can be run.

### New IDs
In task 02 (the user, 2026-11-17). The review (section 6) found:
- **The TDB is ready.** Every editing path takes a new node from
  `getNextItrNode()` and a new item from `getNewTRitemId()` (11 callers
  each, both virtual, both already overridden by `TDBClient`). Only the
  route merge tool, the loaders and undo's copy set them directly; undo is
  off on a client and the merge tool runs locally.
- **Two other allocators are inline and must become getters first:**
  - the route tsection's new sections and shapes for dynamic track
    (`TDB::fillDynTrack`: `routeMaxIdx`, `routeShapes`); these collide
    between users today as nodes do;
  - world object UiDs per tile (`Tile.cpp`: `++maxUiD`, `++maxUiDWS` in two
    places, and the step back when the highest is deleted).

**Proposed: ranges** (the user's easier way):
- The server hands each editing client a **block** of free IDs of each
  kind: TDB nodes, TDB items, RDB nodes and items, route tsection sections
  and shapes, and UiDs per tile.
- The client's getters take IDs from its blocks with no message. When half
  a block is used, the client asks for the next one ahead of need, so an
  edit never waits for the network.
- **UiDs:** a tile's block comes with the tile's answer, only for a client
  that may edit. Below 1,000,000 (TSRE treats higher UiDs apart).
- The server keeps every block it gave out. A disconnected client's unused
  IDs go back to the free pool. The server's own maximums move only when
  an ID is used.
- **Gaps: nodes and items differ.**
  - **Nodes** are closed up at save: TSRE already renumbers nodes on every
    save (`deleteNulls`) without touching other files, because W files and
    paths refer to places and UiDs, not node IDs. On the server only in
    the saved file, through task 01's map (B4): its memory and the
    clients keep their numbers, nothing is resent.
  - **Items keep their IDs for good.** W files (`TrItemId`), services and
    activities (`PlatformStartID`, `SidingItem`) refer to items by ID, so
    items are never renumbered: closing a gap would mean rewriting all of
    those files (the user, 2026-11-17). That is why a deleted item stays as
    an `emptyitem` record, and new items fill those slots first
    (`useTdbEmptyItems`, on by default).
  - **So a range's unused item IDs cannot be closed.** In the file they are
    `emptyitem`s, like deleted items. Ranges cost that, and it is kept
    small:
    - the server hands out free slots first (existing `emptyitem`s),
      then new IDs at the end;
    - item blocks are small (items are rare: a signal, a platform);
    - unused IDs go back to the server when their client disconnects and
      are handed out again;
    - the save ends the table at the highest used item, so unused IDs
      above it are not written at all. Only unused IDs between used ones
      become `emptyitem`s, and the next new items fill them.

    After a session the table looks as it does after deleting a few items
    today.
  - **The save must write those `emptyitem`s, not skip the slots.** Today
    it writes the count and then skips empty slots (`TDB.cpp:3920-3926`).
    Open Rails fills its item table in the order of the records, not by
    the IDs in them (`Orts.Formats.Msts/TrackDatabaseFile.cs:123-139`), so
    one skipped slot moves every later item down by one. Normal editing
    never leaves an empty slot today; ranges would.
  - Rejected: the server numbering each item when it arrives and the
    client renumbering its references afterwards (no gaps, but every
    object holding a `TrItemId` would need fixing on the client).
  - **UiDs:** gaps are harmless.
- Asking the server for every new ID would make every creation path wait
  for a round trip in the middle of building nodes (a flex track takes
  several IDs one after another). That is not small, as the user said.
  Ranges keep the editor's code as it is.

### Security
- **TLS: not now** (agreed by the user, 2026-11-17), with a cheap path to
  it:
  - the client accepts `wss://` addresses as well as `ws://`. Qt's
    websockets use Qt's TLS plugins: OpenSSL on Linux; on Windows also
    Schannel, still to check for the MinGW build;
  - on the internet the server stays plain `ws://` behind a TLS proxy
    (Caddy or nginx with a certificate);
  - TLS inside the server comes later if a proxy is not enough.
- **The password never crosses in clear**, TLS or not (task 02): the
  server sends a random challenge and the client answers with a keyed hash
  of it (HMAC with the password).
- **Permissions:** every request the server takes goes through one check,
  user against action: edit, save, run control (pause, speed, run to,
  reset), drive, watch. Everyone logged in may do everything until
  permissions are designed. Permissions then need no change elsewhere.
- **The users** (the user, 2026-11-20): `users.json` in the folder of the
  profile the server runs with (`--profile`: a profile is one server
  session, its game root, route, settings and users, moved as one folder
  and never shipped with a route); users by name, room for each user's
  permissions; plain passwords (the admin hands them out). `--users
  <file>` overrides.

### Afterwards: the editor over the server
The user's point 6, a set of its own after task 07:
- the collisions left after task 02's ID ranges: versions per record, one
  transaction per edit (a flex track is many records);
- undo on a client; activities, paths, services and timetables edited over
  the server; terrain painting and procedural materials on a client (the
  painted `TERRTEX` files are the client's: how others get them);
- editing actions that are missing or not sent today (found in the
  review: join/split item moves, terrain flags); making a tile from a
  client, as an action of its own (the user, 2026-11-17; task 01 refuses
  objects on tiles the server has not got).

## Tasks
- [x] [`01-restore.md`](01-restore.md): today's mode working as meant, on
  today's messages: the broken items above, nothing written into a
  client's route folder, a test suite of a server and clients. Built
  2026-11-18 on `feature/server`; suite `server`; live check on MINI and
  EUROPE1.
- [x] [`02-protocol.md`](02-protocol.md) (built 2026-11-18 on
  `feature/server`; suite `server` 127 checks; live check on MINI and
  EUROPE1): frames,
  hello and welcome with a challenge login,
  replies and errors, numbered changes to everyone, limits, compression,
  `wss://` on the client, the one permission check (everyone allowed);
  ID ranges (getters for dynamic track's tsection records and UiDs first;
  free item slots handed out first; the save ends the table at the last
  used item; task 01 already writes every slot, an empty one as an
  `emptyitem`); the refresh of a client's route after a reconnect
  (replacing task 01's restart).
- [x] [`03-route-files.md`](03-route-files.md) (built 2026-11-18 on the
  revised design, the user's go; not merged): the route's data by
  kind in one bundle (no paths, no listings), held in memory and read by
  the local loaders; `GLOBAL/tsection.dat` from the server; the terrain's
  first load through the local loader, distant terrain and map mode
  included; map overlays the client's own; the heavy folders compared by
  name and size; a client with only the heavy folders loads as a full
  local load does (captures compared).
- [x] [`04-simulation-session.md`](04-simulation-session.md) (built
  2026-11-19 on `feature/sim-session` from main, the user's go; merged into
  main d590a8aa, then main into `feature/server`): session, view and link
  in one process, nothing changed for the user (frames byte for byte as
  before), every simulation suite passing; vehicles by file, frames by run
  and tick, the input's digest ready for task 05.
- [x] [`05-simulation-on-the-server.md`](05-simulation-on-the-server.md)
  (design 2026-11-19, its questions answered; built the same day on
  `feature/server`): the remote link, the frames as they are at 30 a
  second without repeating unchanged trains (measured live: about
  0.6 Mbit/s a client on PRUDNIK at 07:05, 20 trains running), the run's
  data written by Save's writers and checked by the digest, heard trains,
  run control from the console, the clock, one driver. A client's maps at
  run-to times equal a local run's; motion at 30 frames a second against
  100 still to be seen on a machine with a GPU.
- [x] [`06-players.md`](06-players.md) (built 2026-11-20 on
  `feature/server`, the user's go; not merged): several driven trains, each
  with its crew of players (a cab shared by its first player's choice,
  Join), a driven train's values only to its crew and its hearers, who
  drives shown in the windows and on the map; the flaky
  simulation-session check fixed first.
- [ ] Later (the user, 2026-11-19): **interest.** A client gets in detail
  only the trains it can see, as shadow trains run only near a camera
  today; trains further away less often. Task 05 sends every client every
  train that changed (about 1 Mbit/s is fine for a first version).
- (Done 2026-11-20, the user: saves keep `$static` and paths 6 digits as
  before task 05; an unnamed static train is named after its column in
  TSRE's own layout, stable through saves and the network; only a run's
  data sent to a client has 9-digit paths. Nothing left to test with Open
  Rails from task 05.)
- [ ] The player controller (the humans set's "people the player moves",
  its own set), local, after task 06 and the merge (the user,
  2026-11-20): the person in the simulation and its checks on the local
  link, riding, the controller.
- [ ] `07-player-people.md`: the person over the network (its pose and
  wishes to the server, the server's refusals put right, other players'
  people in the frames and drawn), after the controller.

Each task's doc is written when it starts, with its design and checks.

## Questions for the user
All answered on 2026-11-17: the roadmap's above, task 01's in
[01-restore.md](01-restore.md). Task 01's question (2026-11-18): the
client starting itself again after a reconnect is a stop gap; loading or
switching a route in the running editor is a task of its own. The user's
go for task 02 (2026-11-18: "continue"); the tasks stay on
`feature/server` until the feature is complete, then merge. Task 02's
question answered (running out of IDs mid-edit: wait 3 s, then refuse);
a server back with another route: the client loads it in place
([editor task 10](../editor/10-route-switch.md), built 2026-11-18).
