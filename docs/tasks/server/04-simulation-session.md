# Task 04 - The simulation session

Status 2026-11-18: design. The user agreed to build this task from main
("If you prefer this way, ok", 2026-11-18); that agreement is not a go to
build. On `feature/sim-session` (worktree `../TSRE5vc-sim-session`, from
main 398a2356); merged into main when done, since nothing changes for the
user, then main goes into `feature/server` for task 05.
Roadmap: [00-task-roadmap.md](00-task-roadmap.md), "The simulation on the
server" (main has the roadmap as of 2026-11-17; its later state is on
`feature/server`). Inventory: [reports/server-client-review.md](reports/server-client-review.md),
section 5.

## Goal
`SimulationController` does two jobs. It owns the run: it makes the input,
owns the host and its thread, sends the commands and keeps the run's
state. It also shows the run: it places and draws the trains, and handles
the sounds, the keys, the cab, the Timetable window, the map, the world's
clock, the signals' lights and the level crossings. This task splits the
two inside one process:
- a **session** that owns the run;
- the **view** that shows it;
- a **link** between them.

It also removes what makes the split work only inside one process.

**Nothing changes for the user.** Every simulation suite passes unchanged,
and a run's frames are the same as before, number for number.

Not in this task:
- the link over the network (task 05);
- several views and drivers (task 06);
- the players' people (task 07);
- the Timetable window's Simulate. It runs its own core on a worker, as a
  planning tool each editor runs on its own copy, and it stays as it is.

## Why from main (the user, 2026-11-18)
Nothing in this task needs the server work. `feature/server` touches the
simulation in one place only: `SimulationController::forgetRoute`, nine
lines for the route switch. The simulation is also where the humans,
driving and timetable work keeps landing on main, so a split kept on a
branch for weeks would conflict again and again. Once this task is in
main, main goes into `feature/server`, and task 05 builds the remote link
there.

## Now (main 398a2356)
- **`SimulationController`** (1,800 + 400 lines) is one instance on the
  main thread. 17 files use it: the 3D view, the map layers, the Timetable
  window, the activity tools, the properties, station people, displays,
  the loco controls, `Activity` and two suites.
  - Most of their calls read: `frame` (24), `input` (23), `consistOf`
    (18), `exists`/`shows` (38), `viewFrame` (14), `trainOf` (14),
    `stateText` (12).
  - The rest are commands: start, play, pause, run to, drive, leave,
    command, choose, follow.
- **`SimulationHost`** is a QObject on its own thread that owns
  `SimulationCore`. Commands arrive as queued calls with sequence numbers.
  It publishes immutable frames (their shared parts carry serials),
  events and messages, each in a mailbox.
- **The core already fits** (review, section 5): its input is plain
  values, its commands are values, and it is deterministic.

## Design
### The session (the owner)
A new class, `Sim::SimulationSession`. It lives on the main thread here,
and in the server process in task 05. It takes over from the controller:
- **Making the input** (`makeInput`): the wait for `VehiclePreload`,
  `computeTrackCant`, `MakeInput`, the outside air, `PlanWorkOrders`,
  `FillPassengers` and the player's train for Drive.
  - The outside values it reads (the world's date,
    `core.simulation.boardingHolds`, Drive) are gathered into one
    `RunOptions`, given with the start, so that a remote view can make the
    same input (task 05).
- **Running it:** the host and its thread, the sequence numbers, the mode
  asked and the speed; a run to a time, including its "already passed:
  from the start again"; reset and stop.
- **The activity's editing lock** (`ActivityNotifier`) while a run exists.
- **What the run produces:** the run's events (all of them, in order) and
  the host's messages (sound triggers, the driven train's messages),
  handed to each view.
- **What the view tells it:** the trains heard and the level crossings'
  parameters, kept as the view sent them. In task 05 the heard trains are
  the union of every view's.

The session knows no camera, no consist copy and no widget, and it runs
without a window. `PlanWorkOrders` shows its progress dialog only when
there is a window; a server has none.

### The view (the presenter)
`SimulationController` stays as the view, with its name and API
unchanged, so the 17 files that use it need no changes. It keeps:
- its activity and its input (locally, the session's);
- the frames it took (the latest and the pinned one);
- its own list of events, the messages shown on the map, and the
  `EventResultStore` records;
- the world's clock, and the states of the signals' lights and the
  crossings;
- the traffic's seconds, the chosen and followed trains, and the trains'
  places;
- the texts, the render items and the map features;
- the consist copies, the doors and the junctions;
- the trains heard near the camera, the keys, and the sound triggers
  played on the cars.

Its commands go through the link.

### The link
`Sim::SimulationLink` is an abstract class: the view's side of the
session.
- **Commands:** start (with its `RunOptions`), play, pause, speed, run to,
  reset, stop, drive, leave, command, hear, crossings.
- **Take:** the latest frame, the events and messages since the last
  take, the run's id and its mode.

`LocalSimulationLink` calls the session directly, on the main thread.
Frames, inputs and events pass as shared pointers; nothing is copied or
encoded, so it costs nothing, as the roadmap says. Task 05 adds
`RemoteSimulationLink`.

### What ties it to one process, removed
1. **Vehicles named by their file.** A `Vehicle` carries its file, path
   relative to `TRAINSET`, as the consist names it. The view's consist
   copies (`copyConsist`, `refit`) find their `Eng` by that file through
   their own `EngLib`, not by `Vehicle::eng`. `Vehicle::eng` is
   `EngLib`'s running number, so it differs in another process. This
   matters for the vehicles in frames (couplings, cuts, standing trains),
   which come from the session's process.
2. **The run's identity.** The view compares a frame with the run id the
   link gives it, which is the session's. Its own input's `runId` comes
   from `MakeInput`'s counter, which differs in another process.
3. **Frames and their parts compared by serial, not by pointer.** These
   comparisons change:
   - in the controller, `taken != latest` and `now == placedFrame`;
   - in `ActivityMapLayer`, the raw pointers `simulatedFrame`,
     `trackUses` and `trackAspects`.

   They become comparisons of the frame's (run id, tick) and of its
   parts' serials (`holdSerial`, `drawSerial`). This also closes an old
   hidden fault: a new frame allocated at a freed frame's address looks
   unchanged and is not redrawn.
4. **Times.** A frame's `publishedNs` becomes "when the view got the
   frame, on the view's clock". Locally that is the host's stamp, on the
   same clock. The remote link (task 05) stamps the arrival time. Drawing
   trains ahead (`aheadAt`, at most 50 ms) and the frame log need no
   change.
5. **The view's input checked.** `SimulationInput::digest()` is a hash of
   what the session and a view must agree on: the trains, their vehicles
   (by file and flip), paths and stops, times, events, standing consists,
   work orders and passengers. It never includes `EngLib` numbers,
   pointers or the run id.
   - The local link shares the session's input, so nothing is compared.
   - In task 05 the view makes its own input from the same files with the
     session's `RunOptions`, and refuses the run when the digests differ.
     This is the roadmap's choice: reuse `MakeInput`, with no serializer
     for every plan.

## Steps
1. **A baseline before any change.** Run fixture activities to fixed times
   and write down every frame's trains (state and vehicle ends), aspects,
   section uses and events. Fixtures:
   - simulation-host's own route;
   - with a route: PRUDNIK's timetable and EUROPE1's activity
     (simulation-route's).

   Two runs before the change must give the same numbers before the
   baseline is used. It is kept in the task's work folder,
   `~/sim-session-04`, outside the repo.
2. `RunOptions`, and `makeInput` moved into the session; the dialog shown
   only with a window.
3. The session, the link and the local link; the controller's commands
   and its poll go through the link.
4. Vehicles by file; the run's identity from the link; serials in place of
   pointers.
5. The digest and its checks.
6. Every suite, the baseline compared, and the live checks.

## Checks
- **A new suite, `simulation-session`:**
  - a run through the local link: its frames at the baseline's times
    equal the baseline, number for number;
  - commands acknowledged in order; a run to a time already passed starts
    from the start again, as before; reset; stop;
  - the session made and run without a view (offscreen, no window): the
    same frames;
  - the view's consist copies: each vehicle's `Eng` found by its file is
    the one found by its number; with `EngLib`'s numbers shifted (other
    consists loaded first), the copies are still right;
  - the digest:
    - the same activity made twice: equal;
    - with `EngLib`'s numbers shifted: equal;
    - a train's start time edited: different;
    - another date in `RunOptions`: different (the outside air);
  - the map layer redraws when a new frame comes at a freed frame's
    address (serials).
- **Every simulation suite passes unchanged:**
  - simulation-core, simulation-boarding, simulation-host, simulation-ui,
    simulation-scaling, simulation-events, simulation-coupling,
    simulation-work;
  - level-crossings, road-cars;
  - timetable-read, timetable-route, timetable-trains, timetable-commands,
    timetable-commands-more, timetable-ui, activity-ui;
  - driving, keys, cab3d, steam, heating, exhaust-particles, sound,
    station-people;
  - with a route: simulation-route (EUROPE1), timetable-route;
  - renderer-parity's simulation cases.

  Plus ctest, and every other app suite before the merge.
- **Live** (llvmpipe):
  - in the editor, Play, Run to, Follow, Drive and Leave on PRUDNIK and
    EUROPE1;
  - `--play` headless;
  - screenshots at the same run-to time, before and after the change,
    equal.

## Questions
1. **Task 05's client input.** A client's own input needs the run's
   activity or timetable, with its paths, services and traffic. Task 03
   left these off a client ("by messages when a task needs them").
   - **My recommendation:** when a run starts, the server sends them as
     the run's data by kind (the activity as the server holds it, as with
     the env files in task 03). The client holds them in memory and makes
     its input from them with the session's `RunOptions`, checked by the
     digest.
   - **The other way:** a serializer of `SimulationInput`, covering every
     plan and every field added later.

   Either way task 04's code stays the same. The answer decides whether
   the digest earns its place in this task.
