# Task simulation 08: Open Rails timetables

Status: designed 2026-10-21, waiting for the user's decisions (end of this
document).

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

**Part C, the editor.** Timetables listed with the route's activities (in
the activity panel), a timetable chosen like an activity; its trains
listed in the Timetable window with their stops and times; Simulate runs
it (all trains AI, or one chosen as the player's, then on the autopilot);
the map and 3D draw it as they do an activity's trains.

**Part D, verified.** PRUDNIK's timetable in TSRE against Open Rails
(`-timetable`, the comparison scripts of tasks 02 and 03): the trains'
arrivals and departures at their stations, the formed and static trains.

## Not in this task (proposal)

- Editing or writing timetables (the Timetable window's Simulate writing
  calculated times back is task 04's for activities).
- The shunting commands (`$attach`, `$detach`, `$pickup`, `$transfer`,
  `$forms` with run-rounds), pools and turntables, weather files,
  `$callon`, request stops: read and reported, not run; later, from
  content that uses them.

## Phases (proposal)

1. **Reading and the check** (part A), with fixtures; PRUDNIK read whole,
   its check report.
2. **The trains** (part B without the commands): paths, consists, start
   times, stops and times, static trains; PRUDNIK's trains run.
3. **The commands** PRUDNIK uses (part B), with fixtures for each.
4. **The editor** (part C).
5. **Verified** (part D), Open Rails on PRUDNIK.

## Questions for the user

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
