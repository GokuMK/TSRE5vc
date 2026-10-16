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
- [ ] 05: `sigscr.dat` read and run in place of the built-in aspect rule
  (the rule is a stand-in: each route's scripts define its own aspect
  sequences, plates and features).
- [ ] Later: the Open Rails additions that need world data (stored in the
  TDB, `tdb-signal-extension.md`), approach control, location-linked
  passing paths, curve resistance, activity events, the player driving,
  the Play mode and sounds on the simulation; Open Rails' slower AI
  pre-run as a compatibility setting.
