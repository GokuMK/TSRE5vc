# Task 05 - The simulation on the server

Status 2026-11-19: built (steps 1-6), on the user's go ("Ok, proceed",
2026-11-19); the design's questions answered (below); as built and
results at the end. On `feature/server` (worktree `../TSRE5vc-server`).
Roadmap: [00-task-roadmap.md](00-task-roadmap.md), "The simulation on the
server" and "The server's console". Built on task 04
([04-simulation-session.md](04-simulation-session.md)): the session, the
view and the link, in main since d590a8aa.

## Goal
A run on the server, shown on every client:
- the server runs it, started from its console;
- each client shows it as a local run is shown today: the trains in 3D and
  on the map, the Timetable window, the signals, the crossings, the clock;
- one client at a time may drive a train. It only sends controls: the
  train runs on the server.

A local run (no server) stays as it is.

Not in this task:
- several drivers and crews (task 06);
- the players' people (task 07);
- run control from a client: for now runs are started and controlled from
  the server's console only (the user, 2026-11-17). The commands get a
  place in the permission check, so clients can be given them later;
- editing activities over the server;
- interest: only the trains a client can see in full, those further away
  less often, as shadow trains run only near a camera today. A later task
  (the user, 2026-11-19; see the roadmap); this one stays at about
  1 Mbit/s for a client (section 3).

## Now (feature/server 04a2d6dd)
- **The session (task 04):** owns the input, the host and the run, and runs
  without a window. The view (`SimulationController`) reaches it through a
  `SimulationLink`; only `LocalSimulationLink` exists.
- **The input's digest:** made by both sides from the same files, it must
  be equal. It is the same in another game root, with another EngLib's
  numbers and in a new process; the local link never compares it.
- **A frame** already holds what a view needs to draw without the
  simulation. For each train:
  - its state;
  - each vehicle's two ends (`TrackPose`: the node, the distance along
    it and the way, with the place and the angles worked out);
  - its route part's nodes, so the view takes the right junctions while it
    moves the cars on between frames (`aheadAt`, at most 50 ms);
  - its vehicles when they change.

  The shared parts carry serials and are shared from frame to frame while
  unchanged: aspects and draw states, section uses, crossings, event
  states, standing trains. The host publishes a frame every 10 ms. (It
  published 20 a second before; users saw trains stutter in rare cases.)
- **The server:**
  - a headless TSRE that loads the route;
  - a console with users, save, status, kick, help and quit;
  - numbered requests with replies, and binary records (task 02).
- **A client:**
  - gets the route's data by kind (task 03);
  - loads no activities, services, traffic, paths or timetables;
  - their loaders read the disk, not `RouteFiles`;
  - its activity tools are off.
- **Writing the activity's data:** the activity, service, traffic, path
  and timetable classes each have `toText()`, the writer the editor's Save
  uses.
- **Locally the view gets every train in every frame** and draws those
  within its reach. Only the heard trains (within 1.5 km of the camera)
  run their models (shadows) and carry their values.

## Design

### 1. A run started from the console
Console commands:
- **`run <activity or timetable set>`**: the file name, as the capture
  harness takes it (`lclsrvce.act`, `PSK_2026.timetable_or`).
  - The server's session makes the input with the server's options: its
    world date and its `core.simulation.boardingHolds`.
  - The run starts paused at its start. `run` again replaces the run.
- **`play`, `pause`, `speed <x>`** (0 as fast as possible), **`runto
  <HH:MM[:SS]>`**, **`reset`**, **`stop`**.
- **`trains`:** each train's number, name and state text, as the Timetable
  window gives them.
- **`status`** also shows the run: its activity, its time, mode and speed,
  its trains and the driver.

A server run plays in real time unless told otherwise. The server keeps
one run.

### 2. The run's data and the client's input
When a run starts, and to every client that logs in while it exists, the
server sends **the run's announcement**:
- its id, its activity or timetable set and the run's `RunOptions`;
- the input's digest, and one digest per train (`SimulationInput::
  trainDigests`, new: each train's part of the digest);
- **the run's data by kind:**
  - the activity, its services, its traffic and the paths they use; or
    the timetable set and its paths;
  - each written by its own `toText()`, the writer Save uses (the user,
    2026-11-19: one shared writer, maintained anyway; nothing new to keep
    in step);
  - consists and vehicles are the client's own (`TRAINS`).

**The writers must read back to the same run**, and the digest checks
that they do. Known case:
- TSRE reads a `$static` timetable train without a name as `S<column>`;
- its layout merges continuation columns, so after a save the name
  changes (task 08: `S273` became `S265`);
- PRUDNIK's `PSK_2026` has 42 such trains, in columns 302 to 343, with 8
  continuation columns before them. Written by `toText()` and read back,
  `S302` to `S343` would become `S294` to `S335`. Every client's input
  would differ from the server's, and every PRUDNIK run would be refused;
- **fix:** `OrTimetable::toText` writes the name it read (`S302 $static`),
  as a cloned static train already gets one. A timetable saved by TSRE
  then also keeps Open Rails' names (question 5). Corrected on 2026-11-20
  (the user): only in a run's data sent to a client (`toText(true)`); a
  saved timetable keeps `$static` as it was read, as before.

Found while building (step 1):
- `Path::toText` wrote positions with 6 significant digits, about 1 cm
  at a tile's edges. A path saved and read back moved by up to 5 mm, and
  PRUDNIK's stops by under a millimetre: 76 of its trains differed.
- **Fix:** 9 digits, so any float reads back the same. Corrected on
  2026-11-20 (the user): only in a run's data sent to a client
  (`Path::toText(9)`); a saved path keeps 6 digits, the most a decimal
  keeps through a float and back, so loading and saving again never
  changes the file (with 9, values recomputed between saves changed their
  last digits). `path-roundtrip` (1 cm tolerance) passes on EUROPE1 and
  PRUDNIK.
- With both fixes, every run's data tried reads back to the same input:
  the built-in route, EUROPE1's sctutorial, rsrescue, winterun and
  lclsrvce, and PRUDNIK's `PSK_2026` (237 files, 296 trains). Without the
  `$static` fix, PRUDNIK's standing trains' names differ.

The client:
1. puts the texts in its served route files (`RouteFiles`; they are a
   route's, so its own are never read);
2. loads the activity with the editor's loaders. Those of activities,
   services, traffic, paths and timetables read through `RouteFiles` from
   now on, as task 03's loaders do;
3. makes its own input with the run's options. `MakeRunInput`, the
   session's `makeInput` as a free function, is used by both sides;
4. compares the digests:
   - **equal:** the client says it is ready, and the run is shown;
   - **different:** the run is not shown on that client (the user,
     2026-11-19). The Errors and Messages window names the trains that
     differ (their per-train digests), with the vehicle and consist files
     the client lacks where that is why. The server's console says so too.

### 3. Frames on the wire: the frame as it is, without repeats
The server takes the host's latest frame **30 times a second**. That is a
server setting (`core.network.serverSimulationFrames`), to change if trains
stutter or a connection is too slow (the user, 2026-11-19). Every ready
client gets the same frame, encoded once:
- **the trains that changed since the frame before:**
  - their state;
  - each vehicle's ends as their node, distance and two flags (the way
    along the node, the roll turned), 9 bytes an end. These are the
    `TrackPose` fields the view places a car by. The client works out the
    rest (place, angles, cant) from its own `TrackGeometry`, from the same
    TDB, as the core's `pose` does;
  - the drawn travelled distance and whether it keeps to its authority;
  - their vehicles (by file, flip, id, number, tags, lines; task 04) when
    their version changed;
  - their route part's nodes when their part changed.

  A standing, waiting or finished train does not change from one frame to
  the next, so it is not repeated.
- **the shared parts** (aspects and draw states, section uses, crossings,
  event states, standing trains), when their serial changed: the entries
  that changed;
- **the driven train's values and the heard trains' values**, as in a
  local frame (those that changed);
- **the frame's own fields:** time, asked time, step, steps, mode, speed,
  rate, target, the activity's end, the collision and the commands taken.

Each frame names the frame it follows on from:
- a client applies it to the frame it has, and ignores one that does not
  follow on, or that is older than the frame it has (the user,
  2026-11-19);
- a client without the frame before gets a **whole frame** instead: every
  train, every shared part, every train's vehicles. That is a client that
  logged in, reconnected or skipped frames.

The server keeps for each client only the last frame it sent it. A client
whose socket still holds more than 256 KB unsent skips frames until it
drains (`QWebSocket::bytesToWrite`); it then gets a whole frame. Frames
are always compressed, whatever their size (task 02 compresses messages
above 16 KB only).

Alongside the frames:
- **events:** all of them, in order;
- **the host's messages,** to the clients they are for (section 6).

**What it costs**, measured on the task 04 baseline's PRUDNIK frames:
their numbers packed as above, compressed with zlib as task 02's messages
are.

| PRUDNIK frame | Trains moving | A frame without repeats | A whole frame |
|---|---|---|---|
| 07:51: 296 trains, 37 on the track, 434 vehicles | 14, 198 vehicles | 2.0 KB | 7.8 KB, 13 KB with its shared parts |
| 05:51: 26 on the track, 210 vehicles | 8, 107 vehicles | 1.1 KB | 4.5 KB |
| 04:51: 11 vehicles on the track | 4, 11 vehicles | 0.4 KB | 2.8 KB |

The shared parts' changed entries add little: a few sections and heads a
frame. Whole, they compress well (section uses 75 KB to 1.0 KB, aspects
and draw states 67 KB to 4.4 KB); a client gets them whole only when it
lacks them.
- At PRUDNIK's busiest, 30 frames a second without repeats are about
  60 KB/s (0.5 Mbit/s) for each client; EUROPE1 is a few KB/s.
- Whole frames every time would be 1.9 to 3.2 Mbit/s, over the budget of
  about 1 Mbit/s for this first version (the user).
- The driven and heard trains' values come on top (step 6 measures them).
- A server's upload is that times its clients.

**Later (a task of its own, the user, 2026-11-19):** a client gets in
detail only the trains it can see, as shadow trains run only near a
camera today; trains further away less often. It changes only what the
server sends a client, not what the view gets.

Found while building (step 4), on PRUDNIK's client (its input differed
from the server's in 286 trains, then in 46):
- **Numbers in the network's text messages:** a `QTextStream` writes 6
  significant digits, so the timetable files and paths a client received
  were rounded again. Those streams now write 9 (`NetworkMessage::
  exactNumbers`), the same as the files.
- **The signals' files on a client:** the simulation looked for the
  route's `OPENRAILS/sigcfg.dat` and the signal scripts on the disk, and
  task 03's route bundle did not send them. A client's network then had
  the MSTS `sigcfg.dat`'s signal types, cut its tracks at other signals and
  numbered its sections otherwise. The bundle now sends both `sigcfg.dat`
  files with the scripts each lists; the network reads them, and the
  scripts, through `RouteFiles`.
- **The network numbers its sections, heads and crossings in node id
  order.** It went through the database's hash map, whose order depends on
  how the map was filled: a server whose track a client edited, and a
  client that loaded the same database fresh, would number them
  differently. The track geometry and the paths' tile index also go by id.
  Runs behave the same: the task 04 baseline's frames are equal but for
  those numbers (the per-head aspect lists, the sections' uses, a train's
  blocking section), on the built-in route, EUROPE1 and PRUDNIK. The new
  frames are kept as the baseline from here on (`~/sim-session-04/
  server05-*-sorted.txt`).
- `TSRE_SIM_INPUT_DUMP=<file>` (a developer switch) writes each made
  input's digest text and its vehicles' definitions to `<file>.<pid>`, to
  compare a server's and a client's line by line.

### 4. The remote link (the client)
`RemoteSimulationLink`, the client's `SimulationLink`:
- **Frames:**
  - it makes immutable `SimulationFrame`s from what arrives, sharing the
    parts that did not change, as `MakeFrame` does on the host;
  - a vehicle's `definition` comes from the client's own EngLib, by the
    vehicle's file (`Sim::EngOfFile`);
  - the view gets them as it gets local frames, and compares them by run
    and tick.
- **Times:** a frame carries the server's send time. The link maps it onto
  its own clock with the smallest delay it has seen (an estimate of the
  clocks' offset). That is the frame's `publishedNs`, so a late frame is
  not drawn as new.
- **Between frames:** the view moves the cars on from the last frame as it
  does now (`aheadAt`). For a remote frame the longest age moved on for is
  two frame intervals plus the delay's spread, so 30 frames a second do not
  stop the trains between frames. It stays 50 ms for a local frame.
- **Commands:**
  - hearing, Drive, Leave and the driven train's commands go to the
    server, each as a request; the reply gives its sequence number in the
    session;
  - run control (start, play, pause, speed, run to, reset, stop) answers
    "The server's console runs the run".
- **The view on a client:**
  - the controller holds the remote link while the client is connected,
    and its local link otherwise (`SimulationController::useLink`, new;
    it stops any run first);
  - the panel's Play, Pause, Run to, Reset and Stop and the Environment
    window's time are off while a server run exists;
  - Follow and Drive stay;
  - the world's clock is the run's, as today.

### 5. Driving from a client
One driver at a time in this task (the user, 2026-11-19):
- a client's Drive takes a train nobody drives;
- a second Drive is refused, "driven by <user>";
- Leave, or the driver's disconnect, gives the train back to the AI;
- the driver's keys, its loco panel, its cab and its sounds work as
  locally, from the frames' driven values.

Several drivers and crews are task 06.

### 6. Hearing and sounds
- Each client sends its heard set (the trains within 1.5 km of its
  camera, as `updateTrainsNear` finds them today).
- The session hears the union of every ready client's set; the trains'
  shadows run on the server, and their values are in every frame.
- The host's messages go to the clients they are for: a heard train's
  sound triggers to those that hear it; the driven train's messages and
  triggers to its driver.

### 7. Level crossings
The server tells the session the crossings' values from its own world
objects; clients send none. At a run's start it loads the world files of
the tiles that hold a level crossing item, and again when a client's edit
changes a crossing (`LevelCrossings::version`).

### 8. Joining, leaving, reconnecting
- **A client that logs in during a run:** the announcement, a whole frame,
  then frames without repeats, and the events so far.
- **A client that disconnects:** its heard set leaves the union. If it was
  driving, the train goes back to the AI.
- **Reconnecting (task 02's refresh in place):** the announcement again.
  The same run with the same digest keeps the input it made.
- **The run stopped (`stop`, a new `run`, the server switching routes):**
  clients are told, and their views stop showing it.

### 9. Permissions
The one check (task 02) gets the actions watch, drive and run control:
- watch and drive are allowed to everyone logged in;
- run control is refused to every client (console only).

### 10. Messages
New binary records (tokens in the network range; the token suite's count
goes up):
- **from the server:** `sim_run` (the announcement and data), `sim_frame`,
  `sim_events`, `sim_messages`, `sim_end`;
- **from a client:** `sim_ready` (or refused, with why), `sim_hear`,
  `sim_drive`, `sim_leave`, `sim_command`.

They use task 02's frame (request numbers, replies, compression above
16 KB, limits). The encoding is its own little-endian binary
(`SimulationWire`), pure functions that need no socket.

## Steps
1. **The run's input on both sides:**
   - `MakeRunInput`, a free function;
   - the run's data by kind, written by `toText()`, and the loaders reading
     through `RouteFiles`;
   - the unnamed `$static` name written;
   - `trainDigests`.
2. **The wire:** `SimulationWire`, encoding a frame (whole, or without
   what the frame before had) and decoding it into a frame; suite
   `simulation-wire`. The task 04 baseline must stay byte for byte.
3. **The server:**
   - its session and console commands;
   - the announcement and the data;
   - frames at 30 a second, events and messages;
   - each client's last frame, whole frames for those without it, a
     lagging client's skipped frames;
   - the heard union, the crossings;
   - Drive, Leave and commands with one driver; the permission check.
4. **The client:**
   - `RemoteSimulationLink` and `useLink`;
   - its input made and checked, a refusal shown;
   - run control off;
   - the clocks' offset, the longer age for trains moved on.
5. **Joining, leaving, reconnecting, the run stopped.**
6. **Measurements and live checks;** every suite, ctest.

## Checks
- **Suite `simulation-wire`** (no sockets):
  - a local run's frames, encoded and decoded, are the same as the
    originals: the task 04 frame text, number for number. This holds when
    a client joins late and after skipped frames;
  - a frame that does not follow on from the client's, or is older, is
    ignored;
  - each run's data written by `toText()` and read back gives the same
    digest (the built-in route; with a route, its activities and timetable
    sets);
  - the bytes per frame are reported.
- **Suite `server`** (a server child process and real clients, as now):
  - a console `run` of a fixture activity: a client's frames at a run's
    paused times equal those of a local run of the same activity;
  - a client with another vehicle file is refused, the train named;
  - Drive, a command and its values, Leave; a second Drive refused; a
    driver's disconnect gives the train back;
  - two clients hearing different trains: the union heard, each client's
    sound triggers its own;
  - a client logging in during a run gets the same frames;
  - reconnecting;
  - `stop`, then a new `run`;
  - the console commands' answers.
- **Every simulation suite** as in task 04, the task 04 baseline byte for
  byte, every app suite, ctest.
- **Live** (copies of EUROPE1 and PRUDNIK, a server and two clients under
  Xvfb):
  - a client's view at a run-to time captured, equal to a local run's at
    that time;
  - Drive and Leave from a client;
  - bytes a second for each client while it plays, the driven and heard
    values included;
  - the trains' motion between frames at 30 frames a second (the frame
    log), against 100.

## As built (2026-11-19)
Where the build differs from the design above, or adds to it:
- **The setting** is `core.network.serverSimulationFrames` (30).
- **One network token** (`TSRE_Simulation`, 0x0006000B) carries every
  record; its first byte is the kind: from the server Run, Frame, Events,
  Messages, End, Taken; from a client Ready, Hear, Drive, Leave, Command.
  Every record is compressed above 64 bytes.
- **A remote frame is moved on for longer:** two frame intervals plus the
  delays' spread (at least 50 ms, at most 1 s), from the decoder.
- **Found on PRUDNIK's client** (section 2): 9-digit numbers in the
  network's text messages; the signals' files in the route bundle and
  read through `RouteFiles`; the network, the track geometry and the
  paths' tile index in node id order.
- **A client joining a paused run** gets its frame at once. The server sent
  frames only when the run made a new one, so a client that joined while
  the run was paused waited until it moved (found live).
- **The server's status** gives each client's bytes a second as sent
  (compressed).
- **Activity Tools on a client** list the run's activity or timetable set
  and choose it, switching to its kind, as if the user had chosen it there:
  the map, the Timetable window and Drive follow the activity chosen. The
  run's controls are off; Drive and Leave stay. The activity stays listed
  after the run.
- **The run's end on a client:** the view lets the run go (its clock, the
  signals' and crossings' sources, the editing lock) before the link drops
  it.
- **Checks:** the suite is `server-simulation` (it needs `--game-root`,
  `--route` and `TSRE_SIM_BASELINE_ACTIVITY`), not part of `server`.
  `TSRE_SIM_INPUT_DUMP=<file>` writes each input's digest text to
  `<file>.<pid>`. The capture harness has a client mode (`"server": {"url",
  "user", "password"}`): the route from the server, `"simulateTo"` waits
  for the server's run paused at that time, `"runFor"` for the console's
  play. A map view's `"terrain": false` leaves the ground out (a client's
  tiles may still be coming).

## Results (2026-11-19)
- **Suite `server-simulation`, 31 checks,** passes on EUROPE1 `lclsrvce`
  and PRUDNIK `PSK_2026` (296 trains, 237 files of run data):
  - the client makes its own input (PRUDNIK 1.8 s, EUROPE1 23 ms) with the
    server's digest, and shows the run;
  - its Run to is refused; Activity Tools choose the run's activity, its
    run controls off and Drive on;
  - its frames at +60, +300 and +900 s (run to from the console) equal a
    local run's, number for number;
  - a second client joining a paused run gets the frame, then changes;
  - a client whose input differs refuses the run, naming the train, and
    the console says why;
  - Drive, a second Drive refused, a command, Leave; a heard train's
    shadow; a driver's disconnect gives the train back;
  - reconnecting keeps the input; `stop` lets the view go (its input, the
    clock, the editing lock), a new `run` is shown; the console's
    commands.
- **Bytes a second for a client, as sent:**
  - suite, EUROPE1 at 09:00: 22 KB/s (52 KB/s uncompressed);
  - suite, PRUDNIK at 03:51 (night): 13.5 KB/s (28 KB/s);
  - live, PRUDNIK at 07:05-07:06, 18 to 20 trains running, three clients
    watching: 73-77 KB/s each, about 0.6 Mbit/s (195-210 KB/s
    uncompressed).
- **Live** (a server on a copy of PRUDNIK, harness clients with only the
  heavy folders, two GUI clients; Xvfb, QRhi on lavapipe; cases, script
  and images in `~/server-05/live`):
  - maps at 04:51:30 (a client joining at the start) and at 05:00 (one
    joining later): the same pixels as the local run's, but for the
    editor's pointer mark (the client draws it at the map's corner, 97
    pixels);
  - Drive from a client (train 59 at 04:51:30), throttle and brakes, the
    console on to 04:53; Leave, on to 04:55: the same train states as the
    local run's (speeds and times in the logs). The local images of the
    driven views differ: the local harness sends the commands and starts
    its Run to at once, and draws the driven consist away from the view's
    centre (task 04's captures do too); at 04:55 its map had not drawn the
    new place;
  - the GUI clients show the run, watching, at 13 frames a second here.
  - **Motion at 30 frames a second against 100 could not be measured
    here:** paints come 330 ms apart on lavapipe, locally as on the
    client, and the two frame logs look alike. It needs a machine with a
    GPU.
- **Other suites:** every content-free app suite (120) and ctest (11/11)
  pass; the route suites on EUROPE1 (route-load, simulation-route,
  timetable-route, path-roundtrip, activity-check-route) and on PRUDNIK
  pass, but for simulation-route on PRUDNIK, whose 3 failing checks fail
  the same on main d590a8aa (20 heads linked to junctions further than it
  looks, 2 loose consists off the track, no MSTS activities). The task 04
  baseline's frames are equal but for the head and section numbers
  (section 2).

## Questions
Answered by the user on 2026-11-19:
1. **Driving:** one driver at a time, as section 5; crews in task 06.
2. **A client whose input differs:** the run is refused there, naming the
   trains and files.
3. **The run's data:** one shared writer, the one Save uses (`toText()`),
   not the files' bytes.
4. **Frames:** the frames as they are, less often: 30 a second by default
   (a setting); a frame older than the client's is ignored. About 1 Mbit/s
   for a client is fine for this first version. A later task sends a
   client in detail only the trains it can see, as shadow trains run only
   near a camera; those further away less often. Whole frames at 30 a
   second would be up to 3.2 Mbit/s on PRUDNIK, so the frames leave out
   the trains that did not change (about 0.5 Mbit/s).

5. **The unnamed `$static` fix:** a `$static` train without a name is
   written with the name it was read with (`S302 $static`). Accepted, but
   it needs testing later with the other tools that read timetables: Open
   Rails, and other timetable editors (the user, 2026-11-19). That test is
   not part of this task; it is written in the roadmap's later items.
