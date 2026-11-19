# Task 05 - The simulation on the server

Status 2026-11-19: design (the user: "Ok, review and design"); not a go
to build. On `feature/server` (worktree `../TSRE5vc-server`).
Roadmap: [00-task-roadmap.md](00-task-roadmap.md), "The simulation on the
server" and "The server's console". Built on task 04
([04-simulation-session.md](04-simulation-session.md)): the session, the
view and the link, in main since d590a8aa.

## Goal
A run on the server, shown on every client:
- the server runs it, started from its console;
- each client shows it as a local run is shown today: the trains in 3D and
  on the map, the Timetable window, the signals, the crossings, the clock;
- a client may drive a train; the train still runs on the server, and the
  client only sends controls.

A local run (no server) stays as it is.

Not in this task:
- several drivers and crews (task 06);
- the players' people (task 07);
- run control from a client: for now runs are started and controlled from
  the server's console only (the user, 2026-11-17). The commands get a
  place in the permission check, so clients can be given them later;
- editing activities over the server.

## Now (feature/server 04a2d6dd)
- **The session (task 04):** owns the input, the host and the run. It runs
  without a window. The view (`SimulationController`) reaches it through a
  `SimulationLink`; only `LocalSimulationLink` exists.
- **The input's digest:** made by both sides from the same files, it must
  be equal. It is the same in another game root, with another EngLib's
  numbers and in a new process; the local link never compares it.
- **The server:**
  - a headless TSRE that loads the route;
  - a console with users, save, status, kick, help and quit;
  - numbered requests with replies, and the binary records of task 02
    (`'B'` and a token);
  - each client's camera tile and place, every 100 ms (the pointer
    message).
- **A client:**
  - gets the route's data by kind (task 03);
  - loads no activities, services, traffic, paths or timetables;
  - their loaders read the disk (`QFile`), not `RouteFiles`;
  - its activity tools are off.
- **A frame**, for each train:
  - its state (`TrainState`, about 120 bytes);
  - each vehicle's two ends (`TrackPose`, 48 bytes each);
  - its vehicles when they change.

  The shared parts carry serials: aspects, section uses, crossings, event
  states, standing trains. The host publishes a frame every 10 ms.
- **Placing the cars:**
  - a vehicle's ends are its train's route distance on its route part, as
    the core has it;
  - the route part changes during a run: a passing path spliced in, a
    coupling. So a client cannot place the cars from its own copy of the
    plan's route; the frame has to say where they are;
  - the view places each car from its node, distance and direction, then
    moves it on to its tick (`aheadAt`, at most 50 ms).

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
  its trains, and the driver.

A server run plays in real time unless told otherwise. The server keeps
one run.

### 2. The run's data and the client's input
When a run starts, and to every client that logs in while it exists, the
server sends **the run's announcement**:
- its id, its activity or timetable set and the run's `RunOptions`;
- the input's digest, and one digest per train (`SimulationInput::
  trainDigests`, new: each train's part of the digest);
- **the run's data by kind:** the activity's file, its services, its
  traffic and the paths they use; or the timetable set's files and their
  paths.
  - The files are sent as the server read them. While nothing edits
    activities on a server (now), its objects are the files. Saving them
    again would not be the same: a timetable saved by TSRE names its
    unnamed `$static` trains, so a client's input would differ.
  - Once activities are edited over the server, an edited one is sent as
    its saved text.
  - Consists and vehicles are the client's own (`TRAINS`).

The client:
1. puts the files in its served route files (`RouteFiles`; they are a
   route's, so its own are never read);
2. loads the activity with the editor's loaders. Those of activities,
   services, traffic, paths and timetables read through `RouteFiles` from
   now on, as task 03's loaders do;
3. makes its own input with the run's options. `MakeRunInput`, the
   session's `makeInput` as a free function, is used by both sides;
4. compares the digests:
   - **equal:** the client says it is ready, and the run is shown;
   - **different:** the run is not shown on that client. The Errors and
     Messages window says which trains differ (their per-train digests),
     with the vehicle and consist files the client lacks where that is
     why. The server is told, and its console says so (question 2).

### 3. Frames on the wire
The server takes the host's latest frame **20 times a second** (a server
setting, `core.server.simulationFrames`). It sends each ready client what
changed since what it sent that client:
- **Each train:**
  - **its motion:** front, speed, acceleration, travelled, authority and
    the drawn travelled distance (40 bytes);
  - **its vehicles' ends:** each end's node, distance and two flags (its
    way along the node, its roll turned), 18 bytes a vehicle. The client
    makes the whole `TrackPose` from its own `TrackGeometry` (the same
    TDB), as the core's `pose` does;
  - **the rest of its state:** status, stops, waits, holder, signal,
    people and so on, when it changed;
  - **its vehicles** (by file, flip, id, number, tags, lines; task 04)
    when their version changed;
  - **its route part's nodes** when its part changed.
- **Interest**, from the client's camera (the pointer message):
  - trains with an end within 3 km of the camera, and the train the client
    drives: every frame;
  - the others twice a second, enough for the map and the Timetable
    window.
- **The shared parts**, when their serial changed:
  - aspects and draw states, section uses, crossings and event states:
    the changed entries only (index and value);
  - standing trains: whole (rare).
- **Events:** every client gets all of them, in order. One that logs in
  later gets the run's events so far.
- **The driven train's values:** to its driver, all of them, as changes
  against what it has. The driver holds every value every frame (the
  user's "all values" rule). Others get what drawing and sound need, as for
  heard trains.
- **Heard trains' values:** to the clients that hear them, as changes.
- **The frame's own fields:** time, asked time, step, steps, mode, speed,
  rate, target, the activity's end, the collision and the commands taken.

The server keeps, for each client, what it last sent:
- the shared parts' last pointers, so it can diff them;
- each train's vehicles' version and status, and the train's last full
  rate.

A client whose socket still holds more than 256 KB unsent gets no frame
that tick. Each frame is a change against what the client has, so skipping
one is safe.

**What it costs, estimated** (measured in step 6). A near 10-car train is
about 220 bytes a frame, 4.4 KB/s.
- PRUDNIK at its busiest: about 30 trains running, 5 of them near a
  camera. That is about 35 KB/s for a client, about 280 kbit/s.
- On login, the shared parts in full: tens of KB compressed.
- Heard and driven trains' changes come on top.

### 4. The remote link (the client)
`RemoteSimulationLink`, the client's `SimulationLink`:
- **Frames:**
  - it makes immutable `SimulationFrame`s from what arrives, sharing what
    did not change, as `MakeFrame` does on the host;
  - a vehicle's `definition` comes from the client's own EngLib, by the
    vehicle's file (`Sim::EngOfFile`);
  - the view gets them as it gets local frames, and compares them by run
    and tick.
- **Times:** a frame carries the server's send time. The link maps it onto
  its own clock with the smallest delay it has seen (an estimate of the
  clocks' offset). That is the frame's `publishedNs`, so a late frame is
  not drawn as new.
- **Trains drawn ahead:**
  - a train sent twice a second is drawn from older state than its frame.
    Each train gets its own state time (`TrainFrame::stateTime`, new; a
    local frame's is its frame's time, so local frames stay the same);
  - `aheadAt` moves it on from there;
  - the longest age moved on for becomes three frame intervals for a
    remote frame. It stays 50 ms for a local one.
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
One driver at a time in this task (question 1):
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
  shadows run on the server.
- The host's messages go to the clients they are for: a heard train's
  sound triggers to those that hear it; the driven train's messages and
  triggers to its driver.

### 7. Level crossings
The server tells the session the crossings' values from its own world
objects; clients send none. At a run's start it loads the world files of
the tiles that hold a level crossing item, and again when a client's edit
changes a crossing (`LevelCrossings::version`).

### 8. Joining, leaving, reconnecting
- **A client that logs in during a run:** the announcement, then a first
  frame in full (every train, every shared part) and the events so far.
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

Permissions then change nothing elsewhere.

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
   - the run's data by kind, and its loaders reading through `RouteFiles`;
   - `trainDigests`.
2. **The wire:**
   - `TrainFrame::stateTime`;
   - `SimulationWire` encoding (per client, with interest and changes)
     and decoding into frames;
   - suite `simulation-wire`.

   The task 04 baseline must stay byte for byte.
3. **The server:**
   - its session and console commands;
   - the announcement and the data;
   - the clients' states, frames at 20 a second, events and messages;
   - the heard union, the crossings;
   - Drive, Leave and commands with one driver; the permission check.
4. **The client:**
   - `RemoteSimulationLink` and `useLink`;
   - its input made and checked, a refusal shown;
   - run control off;
   - the clock's offset, the longer age for trains drawn ahead.
5. **Joining, leaving, reconnecting, the run stopped.**
6. **Measurements and live checks;** every suite, ctest.

## Checks
- **Suite `simulation-wire`** (no sockets):
  - a local run's frames, encoded for a client and decoded, are the same
    as the originals: the task 04 frame text, number for number. This holds
    with interest (far trains every tenth frame), with frames skipped and
    with a client joining late;
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
  - bytes a second measured for each client while it plays.

## Questions
1. **Driving in this task:** one driver at a time (a second Drive refused,
   "driven by <user>"; Leave or a disconnect gives the train back), with
   several drivers and crews in task 06. Is that right?
2. **A client whose input differs** (a vehicle or consist it lacks or has
   another version of): I'd refuse the run on that client for now, naming
   the trains that differ and the files it lacks. The other way is to show
   the run without those trains (the roadmap's "watching with missing
   trains", to decide later).
3. **The run's data:** the files as the server read them, rather than its
   objects saved again (a timetable saved by TSRE names its unnamed
   `$static` trains, so the input would differ), while nothing edits
   activities on a server. Edited ones are sent as saved text once
   activities are edited over the server. This refines the answer of
   2026-11-18 ("taken from what it holds in memory"). Is that right?
4. **The rates:** 20 frames a second (a server setting); trains within
   3 km of a client's camera every frame, the others twice a second. Are
   these right, or would you choose other numbers?
