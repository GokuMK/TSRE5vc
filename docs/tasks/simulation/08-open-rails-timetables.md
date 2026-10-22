# Task simulation 08: Open Rails timetables

Status: designed 2026-10-21 with the user's ideas (part C), the
decisions at the end of this document; phases 1 to 7 done; reopened
2026-10-22 for phase 8 (the user: commands catalogued, everything not run
reported, editing commands).

## Progress

**Phase 1, reading and the check (done 2026-10-21).**
- `OrTimetable` (`src/tsre/trains/OrTimetable`) reads a timetable as Open
  Rails does (its `TimetableReader` and `ProcessTimetable`): the encoding
  from the byte order mark (else UTF-8), the separator the first character
  (the first cell must be empty), every cell trimmed; the column kinds
  (trains, `#comment`, blank continuations, `$static` trains named after
  their column) and row kinds (the special rows, `#path /binary`, a second
  speed row skipped, continuation rows of a station); the description
  where the first comment row and column meet. Each train: its path,
  consist parts (`+`, `$reverse`, `<…>`), start time (with seconds) and
  start commands, notes, dispose, speed row (`$max`, the `$` optional as
  in Open Rails, per m/s, km/h or mph), restart delays, briefing (`<br>`
  breaks), stops: the timing (arrival-departure, one time both, `P`
  passing, `*` early, `x` request) and commands, a continuation column's
  cells added to its train's. Commands as Open Rails' `TTTrainCommands`
  parses them (lower case, values at `+`, qualifiers after `/`), every
  cell's text kept. `OrTimetableGroup` reads `.timetablelist-or`.
  Folders by their fixed names (`ACTIVITIES/OPENRAILS`, `PATHS`,
  `TRAINS/CONSISTS`; at first found in any case, changed 2026-10-22: no
  runtime search in another letter case, the user).
- `CheckTimetable`: paths and consists not there, stations no platform of
  the route has, commands naming a train that is not in the timetable (a
  name with `:` is another timetable's), `/ahead` the same, commands TSRE
  does not run yet (all but PRUDNIK's), and a command with text after it
  (a timing after the commands, which Open Rails does not read either);
  the reading's problems first. Texts translated.
- **PRUDNIK** (`timetable-route`): 308 trains (42 static, 66 with a top
  speed), 100 stations, 1684 stops, read in 5 ms with no problems. The
  check finds 12, all in the content: the path name with a trailing dot
  (since phase 3 read as Open Rails reads it, so 11),
  station "KK Lokomotywownia" that no platform has, 9 `$follow` naming
  trains the timetable does not have, and two cells with their timing
  after a command (`$extendplatformtosignal 08:30-08:34`). No command
  outside PRUDNIK's set.
- **Fixtures:** `timetable-read` (20): UTF-16 with `;`, UTF-8 with `,`,
  a tab after a UTF-8 byte order mark, a first cell not empty; the
  description, a static train, consists, start commands with qualifiers,
  notes, dispose, the speed row, station commands, every timing, the
  continuation column and row, an unknown row and a bad timing reported;
  a group; the check. `timetable-route` reads and
  checks a route's timetables (`--game-root`, `--route`; a route without
  any passes).

**Phase 2, the activity in code (done 2026-10-21).**
- `Activity::Kind`: an MSTS activity or an Open Rails timetable set
  (`loadTimetableSet`: a timetable, or a group naming timetables beside
  it); a set has no activity file, its timetables (`Activity::timetables`),
  the player's train chosen for a run (`playerTrain`, none: every train
  AI), its name the group's or the timetable's description, its start the
  first running train's. Not saved yet (phase 6), never unsaved.
- Sets are kept apart from the MSTS activities
  (`ActLib::TimetableSets`, `Route::timetableSetId`): code going through
  `ActLib::Act` and `Route::activityId` knows .act files. A route loads its
  sets from `ACTIVITIES/OpenRails` (`Route::loadTimetableSets`, the
  folder's case not minded).
- The source of trains: `ActivityTrains::List` gives a set's trains as it
  gives an activity's (the rows the Timetable window and the simulation
  read): each AI or the player's, static or not, its path and consist,
  first and last stops by their times, the check's problems;
  `RouteDirectory`, `StartPosition` for both kinds.
- Runs with no player's train: the core already looks at the player's
  train only where there is one.
- **Fixtures:** `timetable-read` (24): a set as an activity (name, start,
  its trains in start order, the chosen one the player's, the static one
  marked, problems from the check), a group's two timetables;
  `simulation-coupling` (24): a run with no player's train to its end, an
  event looking at the player's train not fired. Every suite unchanged.

**Phase 3, the trains (done 2026-10-22).**
- `TimetableTrains` (`SimulationInput`): each train of a set's
  timetables a `TrainPlan` as an activity's service gets one, through
  the helpers taken out of `MakeInput` (vehicles, route, stop placing,
  finishing; activities unchanged): its consist's parts in order
  (`$reverse` turning one), its path (`OrTimetable::Train::pathName`, as
  Open Rails finds the file: ".pat" added where the cell has no
  extension, a trailing '.' none), standing with its rear at the path's
  start; its start time; the `#speed` row a lower top speed. A static
  train is a standing consist of the core from its path's start (one of
  no length, or off its track, warned and left out).
- Stops: each timetable stop at the path's first pass by a platform of
  its station, in the path's order; standing the booked dwell, or the
  platform's least waiting time without one, no longer than that least
  time when late (`TTTrain.ComputeTrainBoardingTime`). A station the
  path does not pass whose platform is behind its start or beyond its
  end, in the track section there (no signal between: Open Rails' route
  holds the whole section, `Signals.InsertNode` splitting only at
  signals), is on its route as in Open Rails: behind the start the
  train is at that station from its start
  (`TTTrain.CheckStationPosition`) and stands until it departs; beyond
  the end it runs on to the platform and stops there. Other stations
  off the path: warned and left out (Open Rails: "Station … not found").
- No exit signal held (`StopPlan::holdsExit`, true for activities):
  Open Rails' timetable mode holds one only with `$hold` or `$forcehold`
  (`ProcessTimetable`, a station's `HoldState` NoHold by default); the
  stop is still made short of its platform's end signal.
- `MakeInput` of a set: its trains, the run from its start (the chosen
  player's train's start where there is one); no events.
- Path: a junction node resolves to a junction within 2.5 m (was 1 m),
  the distance Open Rails' TrackViewer calls a node broken beyond (its
  AI paths take the tile's nearest junction at any distance). PRUDNIK's
  paths have junction nodes 1.5 m off; at 1 m their routes broke (116
  stops beyond the route).
- Content lookups as the content-case policy has them (the user,
  2026-10-22: no runtime search in another letter case; exceptions marked
  to be fixed): timetables in `ACTIVITIES/OPENRAILS`, consists by their
  file names as spelled (`OrTimetable::ConsistFile`), paths by the
  route's path names (`ActLib::GetPathByName`, logical names as the
  policy allows; the check finds them the same way). Two older lookups of
  mine made exact as well: the signal script files (`TrackNetwork`) and
  the Open Rails activity overlay (`ActivityOverlay`, the activity's file
  name in `ACTIVITIES/OPENRAILS`).
- Include files a UTF-8 byte order mark opens (25 of PSK's engine
  parameter files) broke the engine block: the mark, widened byte by
  byte, read as a token with no block, and skipping it ran to the end of
  the block (MaxForce, MaxPower and the includes after lost).
  `FileBuffer::toUtf16` now decodes such a file as UTF-8 without the
  mark; others byte by byte as before (`content-path` checks it). A
  stop-gap: encodings and the wider parsing belong to the ENG refactor
  task (the user, 2026-10-22).
- **PRUDNIK**, on a first conversion by the user with `--contentcase`
  (2026-10-22, not final: the numbers below are to be measured again;
  the converter does not read timetables, but all 200 consist and 215
  path names the timetable gives match the converted files). The
  timetable is still in `ACTIVITIES/Openrails`, so it ran from a scratch
  game root linking OR_PSK with a copy of it in `ACTIVITIES/OPENRAILS`.
  Release, 24 h simulated in 6 s: 266 trains, 239 finish, 26 stuck, no
  deadlocks, 1511 station stops; against the timetable (1276 timed
  stops) a median of 11 s early; 40 of 42 static trains placed (S286 and
  S288 have broken paths, "Static 1/3 Zdzieszowice"). Driven simply: 5
  (one with wagons whose folder `PL-PKPC_Sggrss_MMP` OR_PSK does not
  have, three "Obserwator" trains of a WM15 without force or power,
  ET22-2033 whose Engine block is inside its Wagon block). 31 stops at
  Gliwice and Katowice not made (wrongly thought then not on the paths;
  phase 7: the paths reverse inside those platforms and Open Rails stops
  there, as TSRE now does).
- The stuck trains: all queue behind one, 1027_EC341, at Jasiona's exit
  signal 150 (an entry signal type, USER1, that shows proceed only when
  the block signal 149 0.8 m beyond does; 149 shows RESTRICTING until a
  train's clearing reaches it, and 150 clears no signal ahead, having no
  `SignalNumClearAhead`). The train follows another closely; with 150's
  block taken when its clearing passed signal 315, nothing clears 149
  later. Open Rails' code reads the same (a train passing a signal asks
  the next one with no signals to clear ahead; a NORMAL signal's next is
  the one found at the start, 149); to see in phase 7 against an Open
  Rails run. Before the vehicles were read (driven simply, other
  timings) the same happened to 0552_R46405, and before the exit
  signals were let go (above) to the first train on that line,
  0446_R46401, held at the station. (Phase 7 found the cause, TSRE's:
  149 asked while the train ahead was in its block, cleared with
  BLOCK_OCCUPIED, was not asked again once that train left; fixed, all
  266 trains finish.)
- **Fixtures:** `timetable-trains` (9): three lines with platforms,
  paths, a consist, a timetable of three trains and a static one: the
  path cell naming the file and one ending in '.', the run from the
  first start, a station behind the path's start (stood at from the
  start, left at its booked time), a booked dwell and the least waiting
  time, no exit signal held, a station beyond the end (run on to and
  stopped at), one behind a signal (not on the route, warned), the
  static train placed. `simulation-route` counts a set's static trains
  among the consists to place. Every MSTS route's results and events
  unchanged.

**Phase 4, the commands (done 2026-10-22).** As Open Rails runs them
(its manual and `TTTrain`, `ProcessTimetable`):
- `$create[=time]`: the train is put on its track then (before the first
  train's start without a time) and stands, holding only the track it
  stands on and asking for none ahead, until its start
  (`TrainPlan::createTime`, the core's `StopKind::Created`).
- `#dispose`: the last of its forming commands counts (so `$static
  $triggers=X` triggers). `$static`: the train stays at its path's end,
  holding its track (`staysAtEnd`). `$triggers=X`: X is put on its track
  when this train's run ends and stands until its start time, starting
  at once when that has passed (`formedBy`); X with its own consist, at
  its path's start (Open Rails puts it where the other stood: the paths
  meet there).
- `$follow=X` (in `#note` from the path's start, in a station's cell
  from that station): the first section of each stretch where the routes
  join again, run the same way, after one they do not share
  (`TTTrain.ProcessWaitRequest`; none where the first shared section is
  run the other way); a junction is run the same way when both come from
  its branches or both from its input. The train does not take such a
  section while the other train is on the track and its rear has not left
  it; a point it need not wait at is let go for good, and so is one where
  it stands on track the other's way passes before it (Open Rails'
  check against a deadlock). Without `/maxdelay` it waits whatever the
  delays (the manual says 0; the code: none). The core's `followWait`,
  `WaitFor::Follow` ("following X").
- Stops: the exit signal found as Open Rails' timetable mode finds it
  (beyond the platform on its track node at any distance;
  `StopPlan::Exit`); held only with `$hold` (`$nohold`; `$forcehold`
  the first signal beyond the platform wherever it is); the train leaves
  only once its exit signal, the next ahead, shows more than STOP
  (`waitsForSignal`, asking for it meanwhile; not with `$nowaitsignal`,
  again with `$waitsignal`; `$forcewait` the first signal beyond);
  `$closeupsignal` stops 7 m short of the signal (Open Rails'
  `keepDistanceCloseupSignalM`), `$extendplatformtosignal` puts the
  platform's end there. The station's row first, then the cell.
- `$acc`, `$dec`: factors on the train's rates (`accelerationFactor`,
  `decelerationFactor`, kept when the consist changes).
- Midnight: a time before 08:00 of a train starting after 16:00 is the
  next day's, `$next` starts a train the next day (TTTrain's rule).
- Found on the way and fixed: a signal's script read its own aspect as
  the next signal's where its heads are a few centimetres apart (the
  next-signal search skipped only 5 cm from its start;
  `TrackNetwork::nextSignalWhere` now skips the signal's own heads).
  PRUDNIK's signal 1009 at Raclawice never cleared. And the runtime
  searches in another letter case left: the route's `OPENRAILS/sigcfg.dat`
  and the Timetable window's consist check now by their exact names.
- **PRUDNIK** (Release, 24 h in 5 s): 266 trains, 262 finish, 3 stuck,
  no deadlocks, 1643 station stops, a median of 50 s early (1404 timed
  stops). The stuck ones queue behind 1839_TOW at Jasiona's signals
  150/149, the block beyond 149 occupied (to compare with Open Rails,
  phase 7). 8 `$follow` name no train of the timetable (the check's).
- **Fixtures:** `timetable-commands` (15): `$hold` and the default, the
  signal wait and `$nowaitsignal`, `$create`, `#dispose $static`,
  `$triggers`, `$follow` on a passing loop, `$acc`/`$dec`, midnight and
  `$next`. Every MSTS route's results and events unchanged.

**Phase 5, the editor (done 2026-10-22).**
- The activity panel (F4) asks for the kind first (Kind: MSTS activities
  or Open Rails timetables, `ActivityNotifier::timetables()`); the list,
  the panel and the windows follow it. For a timetable set the MSTS
  activity's parts are hidden (new activity, player service, traffic
  pattern, loose consists, speed zones, failed signals, events, settings,
  the overlay); shown: the player's train (None, every train AI, as Open
  Rails can start; or any running train: a choice for the run, not saved)
  and what the set holds (timetables, trains, problems).
- Traffic window: the route's timetables (each file once), their trains
  and problems; the chosen one's summary and problems in place of the
  traffic editor (New and Delete hidden).
- Timetable window: a set's trains as an activity's; for the chosen one an
  Open Rails train's panel (`OrTrainProperties`) in place of the service
  timetable: its start time, path, consist, commands, comment, its stops
  with their times and commands (the station's and its own), its
  problems; MSTS-only parts hidden (Info, Simulate, Add/Remove, Make Own
  Copy, Show All Trains).
- Check Activity: a set's timetables' check (a command TSRE does not run
  a warning; a path not there jumps to the path list).
- The simulation draws a timetable's train of several consists, or turned,
  from its vehicles (no single consist file).
- Map and 3D: a set has no objects of its own; its trains are drawn from
  the simulation's frames (2D map checked on PRUDNIK by the user's choice
  of a 2D test: the 3D view is too slow on this machine).

**Phase 6, saving (done 2026-10-22).**
- `OrTimetable::toText` writes the model in TSRE's own layout (the user:
  not the read file's cells kept): ';' between cells, UTF-8 with a byte
  order mark; a #comment column after the stations' with the description
  where it meets the #comment row; #comment, #path, #consist, #start, and
  #note, #dispose, the speed row (its unit as read), #restartdelay,
  #briefing where a train has them; the stations in their order, their
  commands after their names; each train's cells as read (or made again
  where edited). Kept for that: each train's #comment cell, its path cell
  with its commands, its speed cell. Not kept: a `#direction` row (Open
  Rails reads it; PRUDNIK has none) and continuation columns and rows
  (merged into their cells, as read).
- Edits: a train's start time and its stops' times in the Timetable
  window, each an undo step (a set's snapshot is its timetables' texts);
  the set then unsaved ("[T]" in Save Activities). Saving writes the
  changed timetables; the first time over a file TSRE did not write, the
  original is kept beside it as `<file>.original` (the user: originals
  preserved).
- **Fixtures:** `timetable-read` (27): the fixture written and read again
  the same, edits written, saving keeping the original once;
  `timetable-route` (PRUDNIK: written in TSRE's layout, 97 KiB, read again
  the same; `TSRE_TIMETABLE_WRITE=<folder>` writes it there for Open
  Rails); `timetable-ui` (13): the kind chosen, the set, the player's
  train, the Traffic window's timetables, the Timetable window's train
  panel, a stop's time edited, undone, edited again with the start time,
  saved (the original kept) and read back, the check.
- **Open Rails reads TSRE's file** (`scripts/orts-timetable-run.sh`, the
  copy beside the route's, not in it): PRUDNIK's timetable as TSRE writes
  it loads in Open Rails as the original does (71 s each), with the same
  messages (the two `$extendplatformtosignal` cells TSRE's check finds
  too); run from the player's train's start (04:47) to 05:30, the same 77
  trains run, their 59 stops within a sample of the original's (median
  +4 s; a sample is 17 game seconds). One difference: Open Rails names a
  `$static` train with no name after its column (`S273`), and TSRE's
  layout has fewer columns (continuation columns merged), so those names
  change (`S265`): a command naming one would need its new name
  (PRUDNIK names none).
- Open Rails' Linux build lower-cases a timetable's path names before it
  opens them (`ExtractPathString`), so on a case-sensitive disk it finds
  no path with capitals: the script's view of the route has each path
  also under its lower-case name (the content left as it is).

**Phase 7, against Open Rails (done 2026-10-22).**
- `scripts/orts-timetable-run.sh` runs a timetable in Open Rails (its
  Linux build, as tasks 02 and 03 do for activities): the pre-run up to
  the player's train's start, then the player's train on the autopilot
  at 17 times real time with the dispatcher page sampled;
  `scripts/orts-compare-stops.py <events> <samples> <player>` pairs the
  trains (Open Rails numbers them in the timetable's column order, as
  TSRE's traffic) and their stops. Open Rails on this machine ran no whole
  day: it failed at 08:06 (`InvalidOperationException` in
  `TrackCircuitSection.TestTrainAhead`, its timetable update) and twice
  its watchdog stopped it (`Thread 'Loader Process' has hung`); three
  windows were compared, 04:47-08:06, 08:19-09:03 and 18:30-20:32 (the
  player's trains 0446_R46401, 0835_R46623, 1832_R46631).
- **Found and fixed in TSRE:**
  - 31 stops at Gliwice and Katowice, platforms the paths reverse inside
    (one item before the reversal, the other beyond): Open Rails' route
    holds the platform's section and the train stops there before
    reversing (`CalculateStationStopPosition`, the path not through the
    station); TSRE now stops there too, centred on the platform up to
    the reversal (MSTS paths' platform passes unchanged).
  - The Jasiona queue (above): a signal cleared for a train with its
    block occupied, beyond those the train may clear later on, was not
    asked again when the block emptied; it is now (Open Rails keeps an
    enabled signal's request until the train passes it). All 266 running
    trains finish; Open Rails runs the first of that queue,
    1839_TOW_Toszek, through Jasiona as TSRE now does.
- **The comparison:** 116 AI trains, 589 stops paired: TSRE minus Open
  Rails a median of -22 s (a sample is 17 game seconds), 81 % within a
  minute, 92 % within two, 14 over five minutes. The player's trains are
  left out: on the autopilot Open Rails drives them with its full train
  physics (`AI_PLAYERHOSTING`), not as AI, and they run faster; TSRE runs
  every train as AI. Open Rails' names of `$static` trains with no name
  (`S273`) have no TSRE train (TSRE writes no events for standing
  consists).
- **Differences left (open items):**
  - Open Rails passes some of PRUDNIK's block signals (`PKP_BLOK_NORMAL`,
    whose script shows CLEAR_2 only when enabled for a train and
    RESTRICTING when not) without enabling them, so they and the signal
    before them (`PKP_SEM_5k_BC`, RESTRICTING after a RESTRICTING one)
    show Ms2, 25 km/h; TSRE's clearing ahead (the same counts,
    `SignalNumClearAhead` 4) enables them and shows CLEAR_2 (e.g.
    0503_TLK48511 gains 4 minutes on Open Rails between Katowice and
    Zabrze). Open Rails shows RESTRICTING in about 3 % of a train's signal
    approaches, TSRE 0.3 %. Elsewhere Open Rails enables the same type
    5 km ahead (Jasiona's 149); why not there is not found (Open Rails'
    propagation reads the same as TSRE's; it needs Open Rails traced).
  - Which train takes a junction first where two lines join differs at
    times: 0624_IC_4520 passes 0540_KS_S1 at Sosnowiec in Open Rails
    (both leave at 06:41:31, the KS then held at the next signal) and
    follows it to Zawiercie in TSRE (11 and 24 minutes late); at
    Zdzieszowice 1839_TOW_Toszek goes before 1910_R46437 in Open Rails
    and after it in TSRE (10 minutes).
  - Open Rails' own faults in these runs: 0500_R99520 removed at its
    path's end (`OUT_OF_PATH`, overrunning at 38 km/h), the failure and
    hangs above.
- **Fixtures:** `timetable-trains` (11): T5 on a fourth line, its path
  reversing inside platform Zeta: its stop on the part before the
  reversal, centred on the platform up to it, left at its booked time.
  `simulation-core` (88): a signal 1 m past one that clears nothing
  beyond it, asked into a block a standing train holds: asked again
  when that train leaves, the train behind goes on (without the fix it
  stands for good). Every MSTS route's results and events unchanged.

**Phase 8, the commands (reopened 2026-10-22).**
- The catalogue: "Open Rails' commands in TSRE" below, from the manual
  (`unstable`) and the code: 49 commands, 16 run (four with qualifiers
  not run), 33 reported; the rows and cell notations.
- Check Activity reports everything TSRE reads and does not run, as
  warnings: besides the commands, a command's qualifiers TSRE does not run
  ("$create runs, its /ahead is not run by TSRE yet"; none run yet,
  `OrTimetableQualifierRun`), a path cell's commands
  (`$endatlastsignal`), request stops (x), leaving early (*) and passing
  times (P), a line a kind and train with its stations, and
  `#restartdelay`. A station's first cell cut at a '/' is a reading
  problem (Open Rails reads it only up to there).
- Fixed: a request stop or one it may leave early lost its mark when its
  time was edited; it keeps it and its one time (`13x25`, `17*35`, the
  only forms Open Rails reads), and the Timetable window shows the mark
  beside the station ("(request stop)", "(may leave early)"), either time
  column setting the one time.
- Fixed: the stations' commands were written in their first cell, where
  Open Rails (and TSRE) cut a qualifier at its '/'
  (`$keepclear /rear=50`); they are written in a station column after the
  names (a blank head), which Open Rails reads whole. PRUDNIK's copy has
  it (its stations' `$hold`) and loads in Open Rails with the original's
  messages.
- Commands edited (item 4 of the user's list): the Timetable window's
  train panel has fields for the train's start commands (after its time;
  none for a train static by its column's head), note, dispose and speed
  row, and its stops list columns for the stop's own commands and its
  station's (every train's there). As typed: a first command without its
  '$' takes one, the separator, tabs and line breaks become spaces; read
  as Open Rails reads them (`OrTimetable::setTrainCommands`,
  `setStopCommands`, `setStationCommands`) and kept whether TSRE runs
  them or not; what TSRE does not run (commands, qualifiers) shown in
  orange with a tooltip naming it, and in the train's problems below.
  Each edit an undo step; the set then unsaved, Save Activities writes
  it. The read-only "Start:", "Note:", "Dispose:", "Speed:" lines are
  gone (the fields hold them).
- **Fixtures:** `timetable-read` (30): a timetable using each of those
  reported (ten reports); a station's qualified command written in its
  column and read back; request and early stops edited keeping their
  marks; commands edited (start, note, dispose emptied, speed row, a
  stop's, a station's) written and read back the same. `timetable-ui`
  (16): T1's note edited with `$connect` (kept, orange, named in the
  tooltip), a stop's and its station's commands in the stops list, the
  station's undone and set again, all saved and read back.
- **The cheap commands run** (the user, 2026-10-22: those that can be
  checked working), as Open Rails' code runs them
  (`TTTrain`, `ProcessTimetable`, `StationStop`, `RequestStop`):
  - Stops: `$stoptime=n` (the least time standing; the station's row's
    wins, Open Rails adds them after the cell's); `$terminal` (rear 15 m
    inside the platform's start where the route has no junction before
    it, else 2.5 m short of the end where none after it, else 15 m short);
    `$keepclear /front /rear /force` (the value the first qualifier's, as
    Open Rails reads it); `$restrictplatformtosignal` (the platform's end
    7 m short of the last signal inside it where none at its end);
    `x` request stops with `$req /pu /pd` (25 % each by default: needed
    when a pick-up or a set-down is drawn, from the train's and station's
    names so a run repeats; one not needed given up 25 m before it, as
    Open Rails' AI brakes until then); `*` (leaves once it has stood its
    boarding time; `x` or `*` without a time a stop too). Positions in
    `placeStops` (MakeInput); the core's `departsEarly`, `RequestSight`.
  - Trains: `$cruise` and `$maxdelay` (its top speed the cruise speed
    while not later than `$maxdelay`; `topSpeedOf`); a train's delay as
    Open Rails keeps one (on leaving a stop with times, how late; once past
    its next booked arrival, at least that, kept while it stands there;
    `delayOf`); `$activated` and `$activate` (its start, arriving at a
    stop, `/depart` leaving it, `#dispose` its end: the activated train
    starts then, or at its start time if later; one formed of another or
    put on its track before, `$create`, stands there until then);
    `$forms` (as `$triggers`, of the forming train's vehicles, turned
    where it starts the other way); `$create /ahead=` (2.5 m ahead of that
    train's front in its section: the two as task 07's parted trains,
    sharing it, the one behind short of the other); `$wait` (one wait
    where the routes join, run the same way or not) and `$follow`'s and
    `$wait`'s `/maxdelay`, `/owndelay`, `/notstarted`.
  - Changed for Open Rails' rule: a `$follow` wait for a train that has
    not started or runs an earlier part of its route holds only with
    `/notstarted` (before, TSRE waited for one on an earlier part).
  - As Open Rails, nothing to run: `$creep` (read; its AI's creep speed is
    fixed), `$doo` (a player's departure sound), `$endstop` (its AI and
    autopilot trains run on past it: an Open Rails run showed it).
  - Not run (reported): `$wait /atstart` (it would wait in the section the
    train stands in, which it takes when placed; not checkable here), the
    experimental `/trigger`, `/endtrigger`.
- **Against Open Rails** (a PRUDNIK copy with the commands, the route's
  untouched; Open Rails 04:47-06:10): `$stoptime=150` stood 150 s in
  TSRE, about 142 s in Open Rails (a sample is 17 s); the request stop
  with `/pu=0 /pd=0` passed in both (the next stop 05:18:47 TSRE,
  05:19:25 Open Rails); `$cruise=50` the same in both (Katowice 05:37:26
  TSRE, 05:39:04 Open Rails, as close as without it); `$activated` by a
  train's `/depart` from Ruda Slaska started 2 min later in both;
  `$endstop` ran on in Open Rails (now in TSRE too). `*` could not be
  checked there: at Kedzierzyn-Kozle both waited for the exit signal
  (TSRE to 05:53, Open Rails to 05:55), and a formed train's `*` left at
  its start in Open Rails, which gives a formed train the forming one's
  arrival there and places it at that platform (TSRE at its path's
  start, phase 4's approximation); the fixture checks it.
- **Fixtures:** `timetable-commands-more` (25): each command above on a
  line of its own, the plans and a run; every MSTS route's results and
  events unchanged, PRUDNIK's original timetable runs as before.

## Why

Open Rails' timetable mode is the other way, beside activities, to define
a route's trains: one spreadsheet of trains and stations, every train run
by the AI unless the player picks it. Large modern routes come with
timetables and no activities: PRUDNIK (OR_PSK, 7673 vector nodes) has a
timetable of 308 trains and not a single activity, so none of its trains
can run in TSRE's simulation today. The user (2026-10-21): after task 07,
before the player's driving, look at Open Rails' timetable activities.

## Current state

- TSRE reads no timetable file: nothing in the editor or the simulation
  knows `.timetable-or`.
- The simulation's input is made from an activity (`MakeInput`): its
  player service and traffic, each service's path (`ActLib::GetPathByName`)
  and consist (`ConLib`), its stops at platforms with their booked times,
  its waiting points, its loose consists and events.
- The core already has most of what a timetable train needs: paths with
  reversals and passing paths, station stops with booked times and Open
  Rails' boarding times, signals and their scripts, station exit-signal
  holds (task 02, as in Open Rails' activities), waiting points, standing
  trains, coupling and cuts (task 07), AI driving with force and mass
  (task 03).

## The local content (2026-10-21)

One timetable: PRUDNIK's `PSK_2026.timetable_or` (in the route's
`ACTIVITIES/Openrails`, where Open Rails reads it; an older copy differs in
`OPENRAILS`), UTF-16, separated by `;`.

- 308 trains (columns), 100 stations (rows), 9 comment columns.
- Special rows: `#comment`, `#path`, `#consist`, `#start`, `#note`,
  `#dispose`, `#speedkph`, `#briefing`.
- 215 paths, all in the route's PATHS but one, a name typo
  (`TOW_Jaworzno_JSB_Juliusz.`); 200 consists, all in
  `OR_PSK/Trains/Consists`.
- The commands it uses, and how often:

  | Command | Uses | Where | What |
  | --- | --- | --- | --- |
  | `$acc`, `$dec` | 107 each | #note | factors on the AI's acceleration and deceleration |
  | `$follow=<train>` | 86 | #note, cells | at each place the two paths join again, wait until that train has passed |
  | `$nowaitsignal` | 60 | station rows, cells | depart without waiting for the next signal |
  | `$create[=time]` | 56 | #start | placed (standing) at that time, starting at its start time |
  | `$hold`, `$forcehold`, `$nohold` | 42, 3, 2 | station rows, cells | hold the exit signal until two minutes before departure (not by default in timetable mode) |
  | `$static` | 39 | #start, #dispose | a static train; becomes static at its end |
  | `$triggers=<train>` | 27 | #dispose | at its end it forms that train (with that train's consist) |
  | `$closeupsignal` | 8 | cells | closer to the signal ahead |
  | `$extendplatformtosignal` | 6 | station rows | the platform's end at its signal |
  | `$forcewait` | 1 | | wait for the next signal even if not the exit signal |

- Not used there: the shunting commands (`$attach`, `$detach`, `$pickup`,
  `$transfer`, `$forms`, run-rounds), `$wait`, `$connect`, `$callon`,
  request stops, pools, turntables, weather files.

## Reference: Open Rails' timetable mode

The manual's chapter (`Source/Documentation/Manual/timetable.rst`, 2521
lines) is the specification; the code is `Simulation/Timetables`
(`ProcessTimetable.cs` 4264 lines, `TTTrain.cs` 15826, pools and
turntables 4700).

- **Files:** `.timetable-or` or `.timetable_or` in the route's
  `Activities/OpenRails`; `.timetablelist-or` groups several timetables;
  `.pool-or`, `.turntable-or` pools; `.weather-or` in `WeatherFiles`.
  Separated by `,`, `;` or tab, in Unicode.
- **Layout:** the first row names the trains, the first column the
  stations (a platform's station name, unique, matched exactly); a blank
  first cell continues the row or column before it; `#comment` rows and
  columns are skipped, the first of each crossing gives the timetable's
  description.
- **Cells:** `HH:mm` a departure (the arrival at the last place),
  `HH:mm-HH:mm` arrival and departure, `x` a request stop, `*` may leave
  early, `P` a passing time; plus timing commands.
- **Trains:** every train is the same kind (`TTTrain`); any running train
  can be chosen as the player's, the others run as AI; a static train
  stands from the timetable's start (its rear at its path's start). No
  automatic coupling: trains join only by the shunting commands.
- **Start and end:** `#start` the start time (with `$create`, `$next`,
  `$pool`, `$activated`); `#dispose` what happens at the path's end
  (removed by default; `$forms`, `$triggers`, `$static`, `$stable`,
  `$pool`, `$attach`); a train formed out of another starts when that one
  has arrived, even if late.
- **Signals:** exit signals are held only on `$hold`; Open Rails' AI
  normally waits at a station for the next signal (`$nowaitsignal` not);
  call-on only with `$callon`.
- **Midnight:** trains before midnight start at the day's end and run on
  past it; after midnight at the day's start unless `$next`.
- **Running it:** `RunActivity -start -timetable <file> <train> <day>
  <season> <weather>`.

## Open Rails' commands in TSRE (catalogue, phase 8)

Every row, cell notation, command and qualifier of Open Rails' timetable
chapter (the manual, `unstable`, 2026-10-22, and the code where it has
more), and what TSRE does with it. **Runs**: the simulation does what Open
Rails does. **Partly**: runs, the qualifiers named not (reported).
**Reported**: read, kept when saved, editable in the Timetable window
(shown in orange), Check Activity warns that TSRE does not run it yet; the
simulation goes on without it. **As Open Rails**: nothing to run (Open
Rails' AI and autopilot trains ignore it too, or it is Open Rails' own
cache). Each command run is checked in a fixture (`timetable-commands`,
`timetable-commands-more`); those marked * also against Open Rails on a
PRUDNIK copy (phase 8's notes).

**Rows, columns, cells**

| Item | TSRE | Later |
| --- | --- | --- |
| `#comment` rows and columns, the description | As Open Rails (read, shown, written) | |
| Blank first cell or head (continuation rows and columns) | Runs (merged into their cells; a station's continuation column for its commands) | |
| `#path` | Runs | |
| `#path /binary` | As Open Rails (its binary path cache; TSRE reads the `.pat`) | |
| Path cell `$endatlastsignal [/reverse]` (code only) | Reported | signals |
| `#consist` with `+`, `$reverse`, `<…>` | Runs | |
| `#start` time | Runs | |
| `#note`, `#dispose`, `#speed`/`#speedkph`/`#speedmph` | Runs (their commands below) | |
| `#restartdelay` and its delay commands | Reported (no random restart delays) | delays |
| `#briefing` | Read, shown, written | |
| `#direction` | As Open Rails ("no longer used") | |
| `HH:mm`, `HH:mm-HH:mm`, with seconds | Runs | |
| `x` request stop (`13x21`, `x`) | Runs*: stops only when someone gets on or off (`$req /pu /pd`, 25 % each by default), drawn from the train's and station's names (Open Rails draws at random), else given up 25 m before it | |
| `*` may leave early (`17*23`, `*`) | Runs: leaves once it has stood its boarding time | |
| `P` passing time (`14P53`) | No stop, as Open Rails; the time (Open Rails' delay there) reported | delays |

**Start commands** (`#start`)

| Command | TSRE | Later |
| --- | --- | --- |
| `$create[=time]` | Runs | |
| `$create /ahead=<train>` | Runs: 2.5 m ahead of that train's front, in its section (the two as task 07's parted trains) | |
| `$next` | Runs | |
| `$static` | Runs | |
| `$static /ahead=<train> /pool=<pool>` | Partly: qualifiers reported | pools |
| `$pool=<pool> /direction /set_consist_name` | Reported | pools |
| `$activated` | Runs* | |

**Note commands** (`#note`; timing commands there too)

| Command | TSRE | Later |
| --- | --- | --- |
| `$acc`, `$dec` | Runs | |
| `$doo` | As Open Rails (only a player's departure sound) | driving |
| `$forcereversal` | Reported | |

**Speed and train commands** (the speed row)

| Command | TSRE | Later |
| --- | --- | --- |
| `$max` | Runs | |
| `$cruise`, `$maxdelay` | Runs* (`$cruise` its top speed until it is later than `$maxdelay`) | |
| `$creep` | As Open Rails (read; its AI's creep speed is fixed) | |
| `$attach`, `$detach`, `$movingtable` (speeds) | Reported | shunting, turntables |
| `$gradient /perc /speed` | Reported | driving |

**Station commands** (a station's row, or a train's cell there)

| Command | TSRE | Later |
| --- | --- | --- |
| `$hold`, `$nohold`, `$forcehold` | Runs | |
| `$forcewait`, `$nowaitsignal`, `$waitsignal` | Runs | |
| `$closeupsignal`, `$extendplatformtosignal` | Runs | |
| `$stoptime=n` | Runs* (the station's row's winning) | |
| `$terminal`, `$restrictplatformtosignal` | Runs (where it stands) | |
| `$keepclear /rear /front /force` | Runs (where it stands; the value the first qualifier's, as Open Rails) | |
| `$endstop` | As Open Rails* (its AI and autopilot trains run on past it) | driving |
| `$closeup` | Reported (with `$callon`) | |

**Timing commands** (a train's cell, or `#note`)

| Command | TSRE | Later |
| --- | --- | --- |
| `$follow=<train>` | Runs | |
| `$follow /maxdelay /notstarted /owndelay` | Runs | |
| `$follow` and `$wait` `/trigger /endtrigger` | Reported (experimental in Open Rails) | |
| `$wait=<train>` with `/maxdelay /notstarted /owndelay` | Runs (one wait where the routes join, either way) | |
| `$wait /atstart` | Reported (waiting in the section it stands in) | |
| `$connect=<train> /maxdelay /hold` | Reported | |
| `$waitany=<path> /both /opposite` | Reported | |
| `$callon`, `$noclaim` | Reported | |
| `$attach=<train> /firstin /setback` | Reported | shunting |
| `$detach` (`/power`, `/leadingpower`, `/allleadingpower`, `/trailingpower`, `/alltrailingpower`, `/nonpower`, `/units`, `/consist`; `/forms`, `/static`) | Reported | shunting |
| `$pickup=<train> /static` | Reported | shunting |
| `$transfer=<train> /static` (`/give`, `/take`, `/keep`, `/leave`; `/onepower`, `/allpower`, `/nonpower`, `/units`, `/consist`) | Reported | shunting |
| `$activate=<train> [/depart]` | Runs* (arriving; `/depart` leaving) | |
| `$req /pu /pd` | Runs* | |
| `$req /laststop /approach /message /sound` | Reported (notices for a player) | driving |

**Dispose commands** (`#dispose`)

| Command | TSRE | Later |
| --- | --- | --- |
| `$triggers=<train>` | Runs | |
| `$triggers` with `$forms`' qualifiers | Partly: qualifiers reported | shunting |
| `$forms=<train>` | Runs (of this train's vehicles, turned where it starts the other way) | |
| `$forms /runround /rrtime /setstop /atstation /closeup /speed` | Partly: qualifiers reported | shunting |
| `$static` | Runs | |
| `$static /closeup` | Partly: `/closeup` reported | |
| `$stable /out_path /out_time /in_path /in_time /closeup /callon /runround /rrtime /rrpos /speed /name /static /forms /triggers` | Reported (the train ends) | shunting |
| `$pool=<pool> /direction` | Reported (the train ends) | pools |
| `$activate` | Runs (when its run ends) | |
| `$attach`, `$detach`, `$pickup`, `$transfer` | Reported | shunting |

**Other files**

| File | TSRE | Later |
| --- | --- | --- |
| `.timetablelist-or` (a group) | Runs | |
| `.pool-or`, `.turntable-or` | Not read | pools, turntables |
| `.weather-or` (chosen when Open Rails starts) | Not read | weather |

In all: 49 commands (48 in the manual, `$endatlastsignal` in the code);
TSRE runs 27 (`$static` and `$triggers` with qualifiers it reports), three
need nothing to run (as Open Rails), 19 are reported. Of the cell
notations, `x` and `*` run; `P`'s time is reported.

## Design (proposal)

**Part A, reading.** A parser for timetables and timetable groups
(TSRE's own, Open Rails as the reference): rows and columns, the special
rows, the cells' timings and every command with its qualifiers, kept as
written. Unknown or unsupported commands are kept and reported, not
dropped. A check like Check Activity's: paths and consists missing,
stations not on a train's path or not in the route, train references to
no train, commands not supported yet (PRUDNIK: the path typo).

**Part B, the simulation.** An input made from a timetable
(`MakeTimetableInput`, beside `MakeInput`): every train with its path,
consist (the `+` and `$reverse` combinations), start time and stops at its
stations' platforms on its path, with the booked times; then the commands
mapped onto the core:

- `$create`: the train placed as a standing train at its create time,
  starting at its start time; `$static`: a standing train for the whole
  run, or the train standing at its end (task 07's standing trains).
- `$triggers`: at its end the train is gone and the named train starts
  there with its own consist, at its start time or when this one arrives,
  whichever is later (Open Rails' formed trains).
- `$follow`: a new kind of wait in the core: where the two paths join again
  after running apart, the train waits until the other has passed.
- `$hold`, `$forcehold`, `$nohold`, `$nowaitsignal`, `$forcewait`,
  `$closeupsignal`, `$extendplatformtosignal`: per stop, the core's
  existing exit-signal holds and stop positions given these switches
  (timetable defaults: no hold; wait for the exit signal).
- `$acc`, `$dec`, `#speed…`: factors on the train's rates and its top
  speed.
- Midnight and `$next` as Open Rails does them.

**Part C, the activity in code, and the editor** (the user's ideas,
2026-10-21; file formats and Open Rails compatibility not decided).

- **An activity in code is either an MSTS activity or an Open Rails
  timetable set** (a timetable or a timetable group, `.timetablelist-or`).
  Open Rails has no activity file in timetable mode: the set is what is
  played, the player's train chosen at the start. Treating the set as an
  activity from the start leaves room for later: activity events and the
  activity's own objects with timetables (a TSRE format, not decided).
  For now the two kinds are separate, no mixing.
- **No player's train needed:** an activity runs with none (every train
  AI), in both kinds; traffic runs without an activity, from the first
  train's departure. Later (driving) events will look at the train the
  player controls at the moment, as the player may change trains.
- **Classes:** `OrTimetable` (TSRE's own reader of `.timetable-or` and
  its groups) beside `Traffic` (`.trf`), with a common interface for a
  source of trains: each train's name, start, path, consist, stops and
  times, problems; whether it can be edited; its check. `Traffic` gets an
  adapter, `ActivityTrains::List` becomes one implementation. Not one
  class: the formats share almost nothing (`.trf` lists service instances
  and start times pointing at `.srv` files; a timetable has each train's
  path, consist, stops and commands in one grid).
- **The simulation:** `MakeInput` for an MSTS activity as now, a
  timetable input for a set (its player's train or none), the same
  `SimulationInput`; the simulation is started with an activity of either
  kind and the player's choice.
- **Saving:** TSRE writes a timetable file of its own layout from its
  model (the user: the easier way; who edits timetables in a spreadsheet
  need not use TSRE's tools), not the read file's cells kept.

The editor:

1. **Activity panel (F4):** the kind chosen first (MSTS activity or Open
   Rails timetable, as Open Rails' menu does), then the list of that kind;
   the kind is the mode of every activity window (below).
   For a timetable set: its timetables, and the player's train (None by
   default, or any running train); the MSTS-only fields hidden (the
   traffic pattern, events, loose consists, difficulty).
2. **Traffic window:** the chosen kind's traffic (MSTS `.trf` patterns, or
   Open Rails timetables); a timetable's properties a summary (trains,
   stations, problems) and its check. The Service window is MSTS's only
   (a timetable has no `.srv` files, its trains are in the timetable):
   off for timetables, its controls in the panel hidden.
3. **Timetable window:** both kinds in one consistent interface (the
   trains, their stops and times, Simulate, Follow, the states); a
   timetable's trains with more (their commands, start and dispose, the
   problems).
4. **Check window:** a timetable's problems (paths or consists missing,
   stations not on a train's path, references to no train, commands not
   supported).
5. **Map and 3D:** as for an activity's trains (from the frames).

**Part D, verified.** PRUDNIK's timetable in TSRE against Open Rails
(`-timetable`, the comparison scripts of tasks 02 and 03): the trains'
arrivals and departures at their stations, the formed and static trains.

## Not in this task

- The shunting commands (`$attach`, `$detach`, `$pickup`, `$transfer`,
  `$forms` with run-rounds), pools and turntables, Open Rails' weather
  files, `$callon`, request stops: read and reported, not run; later
  tasks.
- Activity events and objects with timetables; events looking at the
  train the player controls (the driving tasks).

## Phases

1. **Reading and the check** (part A), with fixtures; PRUDNIK read whole,
   its check report.
2. **The activity in code** (part C's model): an activity of either kind,
   the source of trains and its two implementations, runs with no
   player's train; every suite unchanged.
3. **The trains** (part B without the commands): paths, consists, start
   times, stops and times, static trains; PRUDNIK's trains run.
4. **The commands** PRUDNIK uses (part B), with fixtures for each.
5. **The editor** (part C's windows).
6. **Saving** a timetable in TSRE's own layout, and what the Timetable
   window edits for one.
7. **Verified** (part D), Open Rails on PRUDNIK.
8. **The commands catalogued** (reopened 2026-10-22, the user): every
   Open Rails command, qualifier and notation with what TSRE does with
   it; Check Activity reporting everything TSRE does not run; stops'
   marks kept when edited; then editing a train's, a stop's and a
   station's commands.

## Decisions (user, 2026-10-21)

- The commands: those PRUDNIK uses run; the rest read and reported. Pools,
  turntables and weather files: later tasks.
- The player's train: all trains AI by default, any running train
  choosable as the player's (on the autopilot), as in Open Rails.
- A timetable is an advanced traffic file; an Open Rails timetable set is
  an activity in code from the start (part C); the two kinds separate for
  now; the kind chosen first in the activity panel, and the activity
  windows (Traffic, Service, Timetable, Check) following it.
- Separate classes with a common interface (part C).
- The Timetable window shows both kinds in one consistent interface.
- Saving: TSRE's own file layout, written from the model.
- For now a review of this design, no code.
- 2026-10-22: no runtime search for content in another letter case (the
  content-case policy; exceptions marked to be fixed); the user converts
  the content (`--contentcase`), which does not read Open Rails
  timetables, so tests use a copy with its names matching.
- 2026-10-22, after phase 7: reopened. A catalogue of every command and
  what TSRE does with it; everything read but not run reported (the
  qualifiers and cell notations too); a request or early stop's mark kept
  when edited; then the commands editable (unsupported ones kept and
  marked). Which further commands to run: the user, from the catalogue.

## Questions for the user (as asked)

1. Scope: run timetables (reading, the simulation, the editor) as above,
   with editing timetables a later task?
2. The commands: those PRUDNIK uses now, the rest read and reported; or
   the whole command set from the manual (the shunting commands, pools,
   turntables) in this task?
3. The player's train: all trains as AI by default with a train choosable
   as the player's (as in Open Rails), or only all-AI runs in this task?
4. Where timetables appear in the editor: in the activity panel's list
   beside the activities, or a window of their own?
5. Verification against Open Rails on PRUDNIK: whether Open Rails runs it
   at all on this machine is not known yet (a 19 times bigger network than
   USA2's; frames on llvmpipe).
