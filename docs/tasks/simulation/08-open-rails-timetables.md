# Task simulation 08: Open Rails timetables

Status: designed 2026-10-21 with the user's ideas (part C), the
decisions at the end of this document; phases 1 to 3 done.

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
  Gliwice and Katowice, stations the paths do not reach (Open Rails
  skips them too).
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
  0446_R46401, held at the station.
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
