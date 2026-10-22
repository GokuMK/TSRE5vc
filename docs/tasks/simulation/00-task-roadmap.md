# Simulation Task Roadmap

This folder holds the tasks for simulating activities in TSRE: AI and player
trains running through an activity on their paths and timetables, in the
Route Editor first (a preview, as MSAE's) and later in the game.

The analysis behind it is in `msts-analysis/reports/signalling/` (2026-10-14
and 2026-10-15): MSTS, MSAE and Open Rails signalling and AI, a basic block
model as the first operating policy, and threading.

## Decisions (user)
- 2026-10-15 (task 02): all signal data but scripts; Open Rails additions
  needing world files wait (their data to be stored in the TDB by the
  editor); single-track stretches claimed by one train, deadlocks reported
  to the user; path-based passing paths; clearing ahead in Open Rails'
  legacy mode, then Open Rails mode; speed posts and zones; the player's
  train as an AI train.
- 2026-10-15: TSRE's own C++ simulation core, Open Rails as a behavioural
  reference (from the analysis).
- 2026-10-15: the simulation runs on its own thread on an immutable copy of
  the data it needs (track and activity data). Route and world editing are
  not frozen while it runs: the simulation never reads them, and messages
  from it to an editor object that has gone are not delivered. (The
  analysis proposed an editing pause instead; the copy is small, so it is
  the simpler and safer choice.)
- 2026-10-15: activity editing is off while a simulation exists (the
  activity choice locked, activity undo refused; route and world editing
  stay on); the player's train runs its timetable in task 01; Play runs the
  simulation in real time with steps of at most 0.1 s; stops last Open
  Rails' boarding time; a run goes on until Stop; the world's time is set to
  the activity's start time at start and then follows the simulation's
  (paused with it, at its speed; runs as fast as possible leave it until
  they pause); the controls are in the activity panel below Play's
  new place, the trains' states and Follow in the Timetable window.
- 2026-10-17: the simulation steps at one fixed step a run, its size by
  what drives the trains: 0.1 s when only AI trains run (Timetable
  Simulate, previews: ten times faster), 0.01 s when a train is driven by
  the player, a script or an external app. Today's steps vary (a tick's
  remainder); a fixed step carries the remainder to the next tick. Why
  fixed: runs repeat with the same inputs, stiff parts (couplers, wheel
  slip, brake pipes) stay stable without sub-steps (Open Rails' axle
  model sub-steps up to 100 times a frame, `Axle.cs`), and scripts see
  one `dt`. Cost estimated from the suites' per-step measurements: in
  real time nothing (steps are already about 16 ms); runs as fast as
  possible about ten times slower at 0.01 s.
- 2026-10-17: the simulation never waits for an external app or a script
  that supplies values: it uses the last value received (an external
  train's speed, a loco's force). More in `docs/tasks/driving/`.
- 2026-10-17: next come activity events (06), then coupling and trains
  changing (07); the player's driving is a task set of its own
  (`docs/tasks/driving/00-task-roadmap.md`).
- 2026-10-20: the player's work orders are done by TSRE's own full
  autopilot (task 07), so an activity can be checked end to end; Open
  Rails' autopilot only asks the player to shunt.

## Execution Order
- [x] `01-simulation-thread-and-train-motion.md`: the simulation thread and
  its messages, the immutable input, a route-based train traveller instead
  of `Ruch` for the simulation, trains running on their paths and
  timetables (stops, waits, reversals) with no signals: trains meet and
  pass through each other. Done 2026-10-15 (input, driver, thread and
  editor, measured).
- [x] `02-signals-and-track-authority.md`: everything the TDB and
  `sigcfg.dat` say about signals, no scripts: sections, occupation and
  reservations, signals clearing ahead (Open Rails' legacy and Open Rails
  modes), junctions set and locked, aspects from a built-in rule with
  their speeds, speed posts and speed zones, single-track claims and
  deadlocks reported to the user, path-based passing paths. Designed
  2026-10-15, decisions taken; done 2026-10-16 (phases 1 to 7: the core,
  the editor, the scaling workloads, Open Rails comparisons on every
  local route: our trains run ahead of Open Rails' where stops are close,
  its AI accelerating through its physics).
- [x] `03-train-performance.md`: AI driving with force and mass
  (tractive effort from the engines' force and power, resistance,
  gradient, Open Rails' acceleration floor, braking per train), chosen per
  train in code against task 01's simple driving. Designed 2026-10-16
  from the Open Rails comparisons, decisions taken; done 2026-10-16
  (phases 1 to 4: every local route's stops within a minute or so of Open
  Rails' where they differed by up to 20 minutes, but for trains in Open
  Rails' slower pre-run). The player's own driving is a later, separate
  system.
- [x] `04-timetable-from-simulation.md`: a Simulate button in the
  Timetable window runs the activity as fast as possible and writes the
  timetables (departures rounded to 30 s by default); the user sets later
  departures and simulates again, the window showing what changed; with a
  shared traffic pattern only the player's times are written. Designed and
  done 2026-10-16 (phases 1 to 4; Open Rails runs a calculated timetable,
  its trains leaving on time but where it stands them its platforms'
  minimum waiting time).
- [x] `05-signal-scripts.md`: `sigscr.dat` read and run in place of the
  built-in aspect rule (the rule is a stand-in: each route's scripts
  define its own aspect sequences, plates and features). Designed and
  done 2026-10-16 (phases 1 to 4: every local script read and run; the
  aspects trains see agree with Open Rails' at 62 to 100 % of samples,
  the rest where signals are cleared, task 02). Open Rails' script
  additions later.
- [x] `06-activity-events.md` (designed 2026-10-20, done 2026-10-21; open
  for investigation: location events on the other track of a double line,
  MSTS's rule against Open Rails'; decided 2026-10-21: no stopping at
  stop-here events, trains placed at their start as Open Rails places
  them): the activity's
  events run in the simulation as Open Rails checks them (`Activity.cs`):
  time, location (the point within 2.5 m of the track, the front within
  the radius; Open Rails' triggering AI train), AllStops, ReachSpeed and
  the wagon events on fixed car lists; outcomes (levels, the activity's
  end, restarting a waiting train); the fixed step of 2026-10-17. Shown:
  firing times in the Event window, the Timetable window and on the map
  (messages beside the train that fired them, no message boxes), why an
  event did not fire in Check Activity; Simulate records them too. Wagon
  events that need shunting wait for 07.
- [x] `07-coupling-and-train-changes.md` (designed 2026-10-21, done
  2026-10-21, phase 6 verified against task 06 and Open Rails; the
  user's decisions at its end; part A done (phases 1 to 3): each train its
  own cars, standing trains, attaching, coupling and cuts, signals into
  occupied blocks, living trains, 60001 and 60002, the editor drawing the
  changed trains and the standing ones; B0 done: 42 of 48 goals planned,
  20 of 22 activities whole, sorting and run-arounds beyond it; run-arounds
  and B1 to B5 done: the autopilot runs its plans, back to the path or to
  the place ending the activity, 41 of the 48 wagon events fire and 19 of
  the 22 work-order activities end in success; sorting done: 47 of 48
  wagon events fire, 20 of the 22 end in success (homestre and winterun
  never end, not for their work); phase 6: couplings close in as Open
  Rails' AI does, a train taking cars off its path removed as there): part A, trains that change as Open Rails' AI
  does (each train its own cars, loose consists as standing trains,
  attaching and coupling at contact, cuts at waiting points); part B, the
  autopilot's work orders, planned off the player's path (MSTS paths do
  not lead through the work: 20 of the 22 local activities with wagon
  events), with a feasibility phase first. Notes from 2026-10-17 kept
  below.
  - Open Rails' AI shunts only where the author coded it in the path
    (`AITrain.cs`): waiting point 4NNSS / 5NNSS uncouples keeping the
    first / last NN cars and waits SS s (the cars left become a standing
    consist), 60001 attaches to the next train, 60002 asks permission to
    pass a signal at red, 3HHMM waits until HH:MM. An AI train meeting a
    standing train at the end of a path part (a reversal or its end)
    couples: a loose consist is picked up and taken along
    (`CoupleAIToStatic`), another train has cars taken from it or left
    with it, at the path's end it merges into it. Activities written for
    Open Rails then run unchanged.
  - Today: a train's vehicles are fixed in the input (`TrainPlan`:
    `vehicles`, `length`, summed `performance`), its state is a rigid
    line (`TrainState`: front on its route, speed, reversed), a loose
    consist only holds its sections for the whole run (`StaticConsist`),
    and the core works in whole sections: a train stops a section short
    of another train or a loose consist (it never passes through, it
    never meets either).
  - The player's work orders: TSRE's full autopilot carries them out
    (user, 2026-10-20), its moves planned from the wagon events. AI
    trains could carry out work-order events too (user, 2026-10-17).
  - Needed: one table of the activity's vehicles (trains' and loose
    consists') with stable ids, so events' wagon lists name them (Open
    Rails' CarID from the UiD; the ids come in 06, the lists changing
    here); each train's own vehicle list in its
    state, its performance summed again when it changes; loose consists
    as standing trains of the simulation; frames carry each train's
    vehicles and the controller makes its consist copy again when they
    change; the activity's own loose consist objects hidden while a
    simulation draws them. Places inside a section: a train enters an
    occupied section at restricted speed when it is to couple (Open
    Rails: the other train stands within about 50 m, at a path part's end
    or after 60001), stops at contact and couples. Unplanned contact
    stops both and is reported (no damage).
  - The editor's `Consist` has no split or join (an editing list:
    append, delete, reverse, move, flip); the simulation does not need
    them (its copies are made from frames). The player's driving with
    real consists will.
- [ ] `08-open-rails-timetables.md` (designed 2026-10-21 with the user's
  ideas; phases 1 to 6 done 2026-10-22): Open Rails' timetable mode read,
  simulated, shown and saved (the user, 2026-10-21: before the player's
  driving); an Open Rails timetable set is an activity in code. PRUDNIK's
  timetable, 308 trains and no activity, is the local content: on
  OR_PSK converted with `--contentcase` its trains run with the commands
  it uses (262 of 266 finish, a median of 50 s early; the rest wait
  behind one at a signal pair, see the task). The activity panel shows
  a set's timetables, trains and problems; a train's start and stop
  times are edited and saved in TSRE's layout (the original kept as
  `.original`), which Open Rails reads and runs as the original.
- [ ] Later: the Open Rails additions that need world data (stored in the
  TDB, `tdb-signal-extension.md`), approach control, location-linked
  passing paths, curve resistance; Open Rails' slower AI pre-run as a
  compatibility setting. The player's driving: `docs/tasks/driving/`.

## Open items
- Aspects: Open Rails shows the signal after next uncleared where ours
  clears it (JAPAN1 APP3/CLR2, task 05: propagation only while the block
  is reserved or in the train's route, to study).
- Stopping at a red signal: Open Rails' AI stands about 30 m short with 10
  cars or more but 15 to 20 m with fewer (it aims 5 m short); ours 30 m
  for all.
- Open Rails' script additions to `sigscr.dat` (task 05, later).
- Offered, not decided: Jump To going to where a simulated train is; the
  efficiency after a train's last stop.
- Kept for later (user, 2026-10-21, after task 07):
  - whitefishydsw's 13-car drop-off (Whitefish Siding 529) is skipped as
    impractical: a block would have to come to the cars gathered there
    the other way round (a run-round for a delivery to cars gathered).
  - Location events on another track than the train's: Open Rails fires
    them only within 2.5 m of the train's track, MSTS by a 2D distance from
    the player's train (task 06); the user wants both modes, their
    definition open.
  - Coupling: after a reversal Open Rails' AI creeps for some 50 s before
    its authority reaches the cars it closes on (local_cutbank, 91 s
    later than ours); ours follows the approach rule at once.
  - Single-track arbitration (task 02's decision 2: later):
    BNSF_SCENIC 3_CanadianImports still deadlocks.
  - AI trains doing work orders themselves (the user's idea of
    2026-10-17), with the same planner.
  - Two Open Rails crashes found, not reported upstream: an AI path
    waiting point "until 08:30" on EUROPE1 lclsrvce (2026-10-14), and
    JAPAN1 itr from 12:00 at about 12:23 ("Collection was modified" in
    `TrackCircuitSection.TestTrainAhead`).
- Separate tasks: Open Rails' timetable mode (task 08); the flagged
  refactors below.

## Flagged refactors (user, 2026-10-17)
- `Sim::TrackGeometry` repeats `TDB::getDrawPositionOnTrNode`'s section
  walk on its own copy (the geometry maths reused, `TSection`). A TDB copy
  would need no new class, but the full TDB class would make the
  simulation less portable: a common interface both use, or a TDB
  snapshot taken when a simulation starts (the editor edits the TDB).
- `Sim::Vehicle` copies a few `Eng` fields (length, type, the summed
  performance). Open Rails-style loco physics needs the whole ENG file:
  replaced by the shared vehicle definitions of driving task 01.
- Accepted as they are: `RouteTrack` beside `Ruch` (task 01's decision),
  `ParserX::GetToken` beside `GetString`.
