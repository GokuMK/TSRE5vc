# Task 06 - Players: several driven trains, each with its crew

Status 2026-11-20: design, on the user's "proceed with your 6"; nothing
built until the user's go. On `feature/server` (worktree
`../TSRE5vc-server`), from main 5fe41104 (tasks 01-05 merged). Roadmap:
[00-task-roadmap.md](00-task-roadmap.md), "Players". Built on task 05
([05-simulation-on-the-server.md](05-simulation-on-the-server.md)).

## Goal
A player is a logged-in user. On a server's run:
- several players drive, each a train of their own, at once;
- **a driven train has a crew:** the players in its cab. Two may share one
  cab; each crew member's commands go to the same cab, and a lever one
  moves is where the other sees it in the next frame (the cab's values come
  from the server). Two players moving one lever at once: the later
  command wins, as two hands on one lever;
- a player joins a driven train's cab, or takes a train nobody drives;
- when the last of a crew leaves or disconnects, the AI takes the train
  back (as Leave does today);
- each player's keys, loco panel and cab are bound to its own train;
- alone, the editor is one player on the local link: the same code.

These are the roadmap's, the user's (2026-11-17).

Not in this task:
- the characters (people the players move: the characters set, local, then
  task 07 over the network); a crew member here is a player in the cab's
  view, as Drive is today;
- run control from a client (the console only, as now);
- interest (every client still gets every driven train's values; section
  4);
- permissions beyond everyone may drive (the one check stays the place).

## Step 0: the flaky check (the user, 2026-11-20)
simulation-session's "a run to a time just ahead while running" fails now
and then, on main's d590a8aa too (task 04's). From the code:
`SimulationHost::runTo` (`SimulationHost.cpp:313`), when the host is
already at the target step, pauses there without setting `target`; the
frame keeps the target from before and its text differs from the run-to's,
though every train is the same. Nothing in the app reads a frame's target.
First: loop the suite until it fails, diff the two frames to confirm, then
set `target` in that branch too. Its own commit, before the task's steps.

## Now (feature/server 174a3d29)
- **The core** keeps a driving state per train (`Runtime::drivenState`:
  its vehicles' models, physics, stops, messages), but chooses one train:
  `SimulationCore::driven` (an int). `drive(train)` leaves the train driven
  before (`SimulationDriving.cpp:192`); `leave(train)`; `command(train,
  vehicle, message)` already names its train. `drivenPending` holds one
  train's commands until it is on the track. The 0.01 s step while a
  train is driven (`SimulationDriving.cpp:431`). A driven train's
  passengers count by its doors (`SimulationCore.cpp:575, 586, 1046`);
  shadows are not made for it (`SimulationDriving.cpp:1006`).
- **The host and the session:** `drive(train, seq)`, `leave(seq)` and
  `command(vehicle, message, seq)` take the one driven train
  (`SimulationHost.cpp:221-254`). The frame: `driven` (int) and
  `drivenValues` (one `DrivenFrame`, with its cab's controls, task
  driving 08) (`SimulationHost.cpp:132-143, 422-433`).
- **The input:** `driven` (Drive beside Play: the player's train driven
  from its start).
- **The link:** `drive(train)`, `leave()`, `command(vehicle, message)`;
  `drivenHere(frame)` (task 05's review): the frame's train locally, the
  one the server says this client drives remotely (`Wire::Kind::Driving`).
- **The view** (`SimulationController`): one `drivenTrain`, its keys, the
  camera following it into its cab, its messages, its values for the
  HUD, the 3D cab and sounds (`drivenValues()`, `SimulationController.cpp`
  48, 343, 1199, 1279). StationPeople's doors by `frame->driven`
  (`StationPeople.cpp:1909`). Activity Tools' and the Timetable window's
  Drive/Leave by `driven()`.
- **The wire:** the frame's `driven` and one `drivenValues`, its changes
  by name (task 05).
- **The server:** one `driver` (`SimulationServer.cpp`): a second Drive
  refused ("driven by"), Leave and a disconnect give the train back, the
  driven train's messages to the driver, commands only from the driver.

## Design

### 1. The core: a set of driven trains
- `driven` (int) becomes the driven trains, in train order
  (`std::vector<int>`, few). `drive(train)` adds one and no longer leaves
  another: who drives what is the session's and the server's;
  `leave(train)` gives that one back; `isDriven(train)`, `drivenTrains()`.
- `drivenPending` by train.
- The 0.01 s step while any train is driven; passengers, shadows and the
  authority keeping (`keepsToAuthority`) by `isDriven(train)`.
- A driven train's state, physics and messages are already its own: two
  driven trains run as one does, each with its vehicles' models.
- Locally nothing changes: the editor drives one train at a time (section
  3).

### 2. The host, the session, the link: commands name their train
- `drive(train)`, `leave(train)`, `command(train, vehicle, message)`, each
  with its sequence number as now.
- The frame: `driven` becomes the driven trains (sorted) and `drivenValues`
  one `DrivenFrame` a driven train (its train in it, as now), the cab's
  controls in each. A frame with one driven train holds what it holds
  today.
- `SimulationLink::drivenHere(frame)` stays: the one train this view
  drives, or -1. The local link now keeps its own as the remote one does
  (the train its Drive took, -1 after Leave or the run's end), instead of
  the frame's.

### 3. The view: its own train
- `drivenTrain` is `drivenHere`'s, as since task 05's review: keys, the
  cab, the camera following, Drive/Leave, the HUD and the driven messages
  are this player's train's.
- `drivenValues()` returns this player's train's `DrivenFrame`.
- Other driven trains are drawn and heard as now an AI train with a shadow
  is: their vehicles' values from their `DrivenFrame` (they are complete);
  their doors open by their drivers (StationPeople by `isDriven`).
- **Locally one player:** Drive on another train leaves the one driven
  (the controller sends Leave, then Drive), as the editor does today.

### 4. The wire
- The frame's driven trains as a list; each driven train's values, whole
  or by the names that changed, as the one train's are now; the cab's
  controls in each.
- Every client gets every driven train's values (one encoding for all, as
  in task 05). An estimate, to measure in step 4: a driven train's changes
  are tens of values a frame, a few hundred bytes compressed, about 0.1
  Mbit/s a driven train a client at 30 frames a second. Four driven trains
  bring PRUDNIK's busiest from about 0.6 to about 1 Mbit/s a client, the
  user's budget for a first version; interest (later) sends only the
  trains a client sees.
- `Wire::Kind::Driving` stays: the train this client drives, now as a
  member of its crew.
- **New: `Wire::Kind::Crews`**, the server's to every client when a crew
  changes: each driven train and its players' names, for the UI (who
  drives a train, Join).

### 5. The server: crews
- `driver` becomes the crews: each driven train's players, in the order
  they came; a player is in at most one crew.
- **Drive** on a train nobody drives: the core drives it, the player its
  crew. On a driven train: the player joins its crew (question 1). A
  player in another crew leaves that first (its train given back when it
  was the last).
- **Leave:** the player leaves its crew; the last gives the train back to
  the AI. A disconnect leaves as Leave does.
- **Commands** carry their train; taken from a player of that train's
  crew, refused otherwise ("not in its cab"). Two players' commands to one
  lever in the order they came: the later wins (the core applies them in
  order).
- **Messages** of a driven train to every member of its crew (and to the
  clients that hear it, as now).
- The console: `trains` and `status` show each driven train's crew.
- A reset, a run back to a time, a new run: every crew dissolved (the
  players told by `Driving`, -1), as the driver is today.

### 6. The UI
- **Drive** on a train nobody drives: as now. On a train others drive:
  **Join** (the same button, its text), into its cab, sharing it.
  **Leave** leaves the crew.
- Who drives: Activity Tools' and the Timetable window's train lines say
  "driven by jan, ola" (from `Crews`); locally nothing (no names).
- The map's train labels later, with the characters' names perhaps.

### 7. Local runs
- One player: the same code on the local link; the frames of a local run
  with one train driven read the same as before (its driven list of one).
- Drive beside Play (the input's `driven`) as now.

## Steps
0. The flaky check (above): confirm, fix, its own commit.
1. The core: the driven set (`drive`, `leave`, `isDriven`, pending by
   train, the step, passengers, shadows); the simulation suites with two
   trains driven at once (both move by their own commands, each stops
   and opens its doors, one left while the other drives on).
2. The host, the session, the links: commands with their train; the
   frame's driven list and values; the local link's own train; the view's
   train and values (`drivenHere`), other driven trains drawn and heard;
   local frames as before with one driven (the task 04 baseline's text but
   for the driven field's form).
3. The wire: the list and its values, whole and by changes; the
   simulation-wire suite with two driven trains (decoded the same,
   skipped frames, a late joiner); sizes.
4. The server: crews, Drive/Join/Leave/disconnect, commands from a crew,
   messages to a crew, `Crews`, the console; the server-simulation suite:
   two clients drive two trains, two in one cab (both commands taken, the
   later wins), the first leaves (still driven), the last (the AI's), a
   disconnect, a watcher sees both drawn; the bytes a second with several
   driven trains.
5. The UI: Join, "driven by"; the capture harness's two-client cases (two
   clients driving on PRUDNIK, a third watching); docs, results.

## Checks
- Every simulation, driving, sound, timetable and session suite as before;
  ctest; the route suites (PRUDNIK's simulation-route as on main).
- The task 04 and 05 baselines: one driven train's frames the same but for
  the driven field's form.
- server-simulation's new checks above, each failing with its step's code
  switched off.
- Live: two clients driving on PRUDNIK and a third watching, as task 05's
  live check (lavapipe: frames and states compared, not motion).

## Questions
1. **Joining a cab:** may any player join any driven train's crew (as
   everyone may drive until permissions come), or must the crew agree?
   Proposed: any player.
2. **Crew size:** a limit? Proposed: none for now (the roadmap's "two may
   share one"; the server's setting later if needed).
3. **One train a player:** Drive on another train leaves the first, as
   locally. Proposed: yes.
4. **Every driven train's values to every client** for now (about 0.1
   Mbit/s each, section 4), interest later. Proposed: yes.
5. **"Join" and "driven by jan, ola"** in Activity Tools and the Timetable
   window; nothing on the map yet. Proposed: yes.
