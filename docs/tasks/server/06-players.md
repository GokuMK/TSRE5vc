# Task 06 - Players: several driven trains, each with its crew

Status 2026-11-20: built (steps 0-5), on the user's go ("All ok", after
its questions were answered: a shared flag, off by default; a driven
train's values only where needed; the map too). As built and results at
the end. Reviewed before its merge (the user's go, 2026-11-20): the
findings and fixes in [the review's report](reports/review-2026-11-20.md),
"Task 06". Not merged, not pushed. On `feature/server` (worktree
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
- interest for AI trains (the heard trains' values still go to every
  client, as in task 05); a driven train's values go only where needed
  (section 4);
- permissions beyond everyone may drive (the one check stays the place).

## Step 0: the flaky check (the user, 2026-11-20) - done
simulation-session's "a run to a time just ahead while running" failed now
and then, on main's d590a8aa too (task 04's). The design's guess (a stale
`target`) was wrong: the comparison ignores it. Looped under load with the
first differing line printed: only the frame's `asked` time differed
(28860.0056 for 28860). Playing at 128x left a few milliseconds short of a
whole step in the core (`pending`); the run to +60 s landed on its step
with them still there, and `asked` is the stepped time plus that rest
(`SimulationHost.cpp:51`). Every train the same; the view reads `asked`
only while playing (trains drawn a fraction of a step ahead), so nobody
saw it. **Fix:** a run to a time drops that rest (`SimulationCore::
dropPending` in `SimulationHost::runTo`): it lands on its step as one
asked from paused does. 0 of 75 runs failed under the load that failed 3;
the comparison now prints the first line that differs.

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

### 4. The wire: a driven train's values only where needed
(The user, 2026-11-20: "only his own makes sense".)
- The shared frame (one encoding for all, task 05) keeps the driven trains
  as a list, and their positions and states as every train's: what drawing
  a train and the map need. A driven train's values (`DrivenFrame`: its
  vehicles' models' outputs, the cab's controls) leave the shared frame.
- **A new record, `Wire::Kind::DrivenValues`:** one driven train's values,
  whole or by the names that changed since its last, encoded and
  compressed once a tick for a train, sent to:
  - its crew (always: their cab, HUD, keys, sounds);
  - the clients that hear it (their heard set, near their camera): another
    player's train passing gets its sounds and what its values show (lamps,
    pantographs, doors), as a heard AI train's shadow does. Nobody else.
  A client lacking a train's last values gets them whole, as frames work.
- The client's link puts the latest values of each train it gets into its
  frames (`drivenValues`), so the view reads them as locally.
- Even with one driver (task 05 now) a watcher stops getting its values:
  about 0.1 Mbit/s less a watcher (an estimate: tens of values a frame, a
  few hundred bytes compressed, at 30 frames a second; measured in step 4).
- A local run: every driven train's values in its frames, as now.
- `Wire::Kind::Driving` stays: the train this client drives, now as a
  member of its crew.
- **New: `Wire::Kind::Crews`**, the server's to every client when a crew
  changes: each driven train, its players' names in order and whether
  its cab is shared; for the UI and the map.
- `Wire::Kind::Driving` stays: the train this client drives, now as a
  member of its crew.
- **New: `Wire::Kind::Crews`**, the server's to every client when a crew
  changes: each driven train and its players' names, for the UI (who
  drives a train, Join).

### 5. The server: crews
- `driver` becomes the crews: each driven train's players, in the order
  they came; a player is in at most one crew.
- **Drive** on a train nobody drives: the core drives it, the player its
  crew, its first player. On a driven train: the player joins its crew
  when its cab is shared, else refused ("its cab is not shared"). A player
  in another crew leaves that first (its train given back when it was the
  last).
- **Shared or not (the user, 2026-11-20):** the crew's first player sets
  it (`Wire::Kind::Share`, on or off); turning it off keeps those already
  in. Proposed default: not shared, so nobody steps into a cab
  unasked. When the first player leaves, the next in the crew sets it.
- **Leave:** the player leaves its crew; the last gives the train back to
  the AI. A disconnect leaves as Leave does.
- **Commands** carry their train; taken from a player of that train's
  crew, refused otherwise ("not in its cab"). Two players' commands to one
  lever in the order they came: the later wins (the core applies them in
  order).
- **Messages** of a driven train to every member of its crew (and to the
  clients that hear it, as now).
- **Its values** (section 4) to its crew and its hearers, each tick.
- The console: `trains` and `status` show each driven train's crew.
- A reset, a run back to a time, a new run: every crew dissolved (the
  players told by `Driving`, -1), as the driver is today.

### 6. The UI
- **Drive** on a train nobody drives: as now. On a train others drive in
  a shared cab: **Join** (the same button, its text), into its cab.
  Disabled with "its cab is not shared" otherwise. **Leave** leaves the
  crew.
- **Shared cab:** a check box beside Drive/Leave in Activity Tools, for
  the crew's first player (off by default, proposed).
- Who drives: Activity Tools' and the Timetable window's train lines say
  "driven by jan, ola" (from `Crews`), "(shared)" when it is.
- **The map too (the user, 2026-11-20):** the map's train labels (Train
  Names, `MapLabelLayer`) add the crew's names under a driven train's
  name. Locally nothing changes (no names).

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
3. The wire: the driven list in the frame; `DrivenValues` (whole and by
   changes, to a crew and hearers), `Crews`, `Share`; the link putting
   values into its frames; the simulation-wire suite with two driven
   trains (decoded the same, skipped frames, a late joiner, values only
   where sent); sizes.
4. The server: crews, the shared flag, Drive/Join/Leave/disconnect,
   commands from a crew, messages and values to a crew and hearers,
   `Crews`, the console; the server-simulation suite: two clients drive
   two trains, a third refused in a cab not shared, joins once shared,
   both commands taken (the later wins), the first leaves (still driven,
   the next sets sharing), the last (the AI's), a disconnect; a watcher
   sees both drawn and gets their values only while it hears them; the
   bytes a second with several driven trains.
5. The UI: Join, the shared check box, "driven by" in Activity Tools, the
   Timetable window and the map's labels; the capture harness's
   two-client cases (two clients driving on PRUDNIK, a third watching);
   docs, results.

## Checks
- Every simulation, driving, sound, timetable and session suite as before;
  ctest; the route suites (PRUDNIK's simulation-route as on main).
- The task 04 and 05 baselines: one driven train's frames the same but for
  the driven field's form.
- server-simulation's new checks above, each failing with its step's code
  switched off.
- Live: two clients driving on PRUDNIK and a third watching, as task 05's
  live check (lavapipe: frames and states compared, not motion).

## Questions (the user's answers, 2026-11-20)
1. **Joining a cab:** the crew's first player sets a shared flag (the
   user); off by default (the user: all ok).
2. **Crew size:** a limit? Proposed: none for now. (No answer: as
   proposed unless said.)
3. **One train a player:** Drive on another train leaves the first, as
   locally. Proposed: yes. (No answer: as proposed unless said.)
4. **A driven train's values:** only to its crew, and to those who hear
   it (the user: only one's own makes sense; hearing another's train
   nearby needs its values, as a heard AI train's shadow). Section 4.
5. **Who drives:** Activity Tools, the Timetable window and the map's
   train labels (the user: the map too).

## As built (2026-11-20)
- **Step 0** (3da092f6): the flaky check (above).
- **Step 1** (f3d64d9f): `SimulationCore::driven` a sorted set
  (`drivenTrains`, `isDriven`), `drive` adds, `leave(train)`, pending
  commands by train, the 0.01 s step while any is driven; passengers, a
  driven start's throttle, shadows, crossings and the authority kept by
  `isDriven`.
- **Step 2** (4fbd2417): `drive(train)`, `leave(train)`, `command(train,
  vehicle, message)` through the host, the session and both links; the
  frame's `driven` a list and `drivenValues` one a driven train
  (`isDriven`, `drivenOf`); the view's own train by `drivenHere` (a local
  run's: the first driven, one player); another Drive gives the train
  before back first; `askedTrain` takes commands between a Drive and its
  frames; other driven trains drawn and heard by their values. Frames
  byte for byte task 05's baseline (built-in route, EUROPE1 x3 with
  driving); PRUDNIK's but for its static trains' names (c97fe204).
- **Step 3** (75cfc4ec): `Wire::Kind::DrivenValues` (`EncodeDriven`: for
  a frame's tick, whole or by changes since the values the client got),
  `FrameEncoder::setDrivenValues(false)`, `FrameDecoder::decodeDriven`
  (kept for the frames after; changes without values before refused, Ready
  asks for them whole); `Crew`, `Wire::Kind::Crews`, `Wire::Kind::Share`;
  `SimulationController::crews`, `share`.
- **Step 4** (587fc766): the server's crews (`CrewState`: players in
  order, shared): Drive takes a free train or joins a shared cab ("its cab
  is not shared (driven by ...)" otherwise), one train a player; Leave and
  a disconnect leave, the last gives the train back, the next drives on;
  commands only from the train's crew; Share from its first player; the
  crews to every client and on the console (`status`, `trains`). Each
  driven train's values encoded once a frame, to its crew and its hearers,
  before their frame.
- **Step 5** (9dd769dd): the train's state "driven by jan, ola"
  ("(shared)"), the map's train labels "name (jan, ola)" (labels are one
  line: beside the name, not under it); Activity Tools: Join, Drive off
  with "Driven by ...; its cab is not shared", a Shared cab check box for
  the crew's first player on a client; the Timetable window's Drive as
  Join. Both .ts files.

## Results (2026-11-20)
- **Suites:** driving 80 (two trains driven at once, each by its own
  commands); simulation-wire 16 content-free, 54 EUROPE1, 9 PRUDNIK (the
  values apart: decoded the same where sent, none where not, changes
  without values before refused; the crews and sharing read back);
  server-simulation 53 on EUROPE1, 52 on PRUDNIK (two trains driven at
  once; a cab not shared refuses; shared, a third joins; both crew
  members' commands, the later winning; values only to a crew and its
  hearers; the first leaves, the train driven on, the next sets sharing;
  the last gives it back; a disconnect; the state and label name the
  drivers; Activity Tools' Join on EUROPE1, where there is a player's
  train). Each step 4 rule's check fails with the rule switched off.
- **A driven train's values:** 230-312 B a frame compressed on EUROPE1's
  locos, about 7-9 KB/s a recipient at 30 frames a second; a watcher no
  longer gets a driven train's values it does not hear.
- **Live** (`~/server-06/live`, PRUDNIK copy, capture-harness clients):
  jan drove 0403_R64620 and ola 0408_REG at once from 04:51:30 (throttle
  0.5 and 0.3), the console's status listing both crews; the console ran
  to 04:53; a third client's map labelled train 59 "0403_R64620_B-N-KK
  (jan)"; leaving, each train went back to the AI. (The clients' logs:
  `load.ace`/`details.ace` not sent. Not a content case gap: PRUDNIK's
  TRK names `Load.ace`, and TSRE read a fixed `load.ace`; fixed on main
  251f734d, merged into this branch.)
