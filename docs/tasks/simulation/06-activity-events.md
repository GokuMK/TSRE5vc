# Task 06 - Activity Events

Status: done 2026-10-21 (branch `feature/simulation`); location events on
another track open for investigation (end of the doc).
Decisions taken are at the end.

Sources:
- tasks 01 to 05 (the simulation core: trains on their routes, signals,
  driving, Simulate, signal scripts) and the roadmap's agreement for this
  task (2026-10-17);
- TSRE's activity model: `ActivityEvent` (the event parser, outcomes,
  wagon lists), `Activity`, `ActivityCheck`, the Event window
  (`ActivityEventWindow`, `ActivityEventProperties`), the Timetable window,
  the map's Events layer (`ActivityMapLayer`);
- Open Rails (a behavioural reference, no code taken), checked in
  `openrails-linux-spike` (the same as `openrails` for these files):
  `Activity.cs` (`Activity.Update`, `EventWrapper.IsActivityEnded`,
  `EventCategoryActionWrapper`, `EventCategoryLocationWrapper`,
  `EventCategoryTimeWrapper`, `ActivityTaskPassengerStopAt`,
  `AssociateEvents`), `Traveller.DistanceTo` (`MaximumCenterlineOffset`),
  `Simulator.cs` and `AI.cs` (car ids), `AITrain.RestartWaitingTrain`,
  `ActivityWindow.cs` (messages, `ORTSContinue`), `ActivityEventsWebpage`
  (the `/activityevents` websocket);
- `msts-analysis/reports/msts-activities-tsre-orts-review.md`: MSTS's
  event fields are read and written natively, but their runtime meaning is
  not traced; Open Rails stays the reference;
- the local content: 68 activities in `~/ORTS` (census below).

## Progress

**Phase 1, core (done 2026-10-20).**
- The fixed step: `SimulationCore::step` takes whole steps of 0.1 s and
  keeps the rest for the next call; time is the origin plus steps × 0.1
  (an hour of steps ends on the second); the pre-run steps whole steps
  too. `onStep`: the host runs to the first whole step at or after a
  target, and the controller takes a run to a time as passed only beyond
  one step past it. Every simulation suite passes unchanged.
- The input: `Vehicle::id` (the player's cars consist 0, a loose consist's
  its object id × 65536 + UiD, AI trains' none), `StaticConsist::cars`,
  `EventPlan`s made from `Activity::event`: location points placed on the
  track within 2.5 m by x and z (`TDB::findNearestPositionsOnTDB` with
  `useHeight` false), sidings between their two items on one node,
  outcomes by the events' places in the list, triggering and restarted
  trains found by service and time (warnings when not).
- The core checks the events after each step as Open Rails does (section
  2): levels, reversible events disabled until their condition stops
  holding, outcomes once in Open Rails' order, chains in one step, a
  staying message holding the others, the activity's end. Location events
  look at where the front went during the step (`Runtime::swept`) and
  where it came to rest (`rests`, a reversal included), so radii smaller
  than a step's travel are not missed; places on a train's parts are
  found again when it takes a passing path. Records: `TrainEvent`
  `EventFired`, `ActivityEnded`, `Restarted`; frames carry the events'
  states, shared while unchanged.
- `simulation-events` suite (new, content-free): 38 checks then, among
  them a time event at its second, chains and levels, a 0.5 m radius at
  20 m/s, a stop-here event at a reversal, a reversible event out and
  back, Open Rails' stand-in for a triggering train, each wagon event,
  restarts standing and still coming, frames sharing states, a point
  2 m beside the track found and 3 m not.
- The route suite prints each activity's events fired by kind and its
  end.

**Phase 2, the player's train (done 2026-10-20).**
- It stops at the nearest active stop-here event ahead that it can still
  stop at with its deceleration (once braking for one it keeps it), stands
  until it fires (at most a minute), then goes on (`StopKind::Event`,
  `TrainState::eventStop`, "stopped for event ..."). Its next station stop
  moves to such an event along the platform.
- Checks added: it stops at the point and leaves once fired; activated
  too late, passed; along a platform the stop moves there (boarding
  there); before the platform a stop of its own first (41 checks).
- On every local route (the route suite's events): the tutorials' location
  events fired went up from 25 to 27 of 39 (sctutorial), 27 to 47 of 57
  (oetutorial), 18 to 28 of 34 (tokyohakonetutorial), 24 to 26 of 29
  (hisatsututorial), 18 to 21 of 36 (necorridortutorial), 12 to 15 of 19
  (freighttutorial); BNSF's MustComeDown now ends successfully. Of the 65
  activities on the seven routes, 35 end successfully, 3 fail (USA2
  freight activities whose failure events stand for work not done: their
  wagon events wait for 07), 27 have no end yet (phase 4 tells why).
- Cost, Release builds against main: a step on the measured activities
  unchanged (0.4 to 0.5 µs); whole routes JAPAN1 400 to 420 ms, USA1 268
  to 314 ms, EUROPE2 120 to 153 ms for 13.1 simulated hours (its
  oetutorial's 57 location events: about 70 ns a step).

**Phase 3, editor (done 2026-10-20).**
- `EventResultStore` (`src/tsre/sim/EventResults`): each activity's last
  run's event results for the session, from Play and Run to (each frame
  whose event states changed; the time alone otherwise) and from Simulate
  (`TimetableCalculation::write`); out of date after any `ActivityEdit` of
  the activity or an undo restoring activity data.
- Event window: a fired event's time before its name, events that did not
  fire grey, all in italics when out of date; the tooltip says when, how
  often and by which train (or the player's standing in); under the lists
  the run: how many fired, the activity's end, up to when.
- Timetable window: the activity's events (fired, the end) among the
  player's train's events, with their messages; an AI train's list keeps
  those it fired and its restarts.
- Map: events fired drawn green, those not active grey (palettes
  `eventFired`, `eventInactive`), the rest red as before; a fired
  location event's label has its time; the latest message of each train
  beside it for 10 s (cut at 80 characters), following the train.
- simulation-ui: 6 checks (Run to: results, the Event window's rows and
  run line, the Timetable list, the map's message, an edit putting them
  out of date, Simulate recording them again; 57 checks). A capture of
  EUROPE1's sctutorial run to 08:06 (scratch case, map by the player's
  train) shows the tutorial's chain fired along the route with times, the
  station-stop message beside the train, events passed but not fired red
  ("Tunnel", "F4": phase 4 tells why).

**Phase 4, why not fired and Check Activity (done 2026-10-21).**
- The core notes for each event not fired: when it was first active,
  when the train it looks at first came within its radius without firing
  it (and whether it was active then), ReachSpeed's best speed,
  AssembleTrainAtLocation's siding reached. The input keeps a location
  point's distance from the track when it has no places, and from the
  path of the train it looks at when that path does not pass them.
- `WhyNotFired` (SimulationTexts) gives the reason (section 7); the
  Event window's tooltips show it, Check Activity a "Last run" part: the
  run (to where, the activity's end), each event not fired and why, those
  fired by the player's train standing in, and at the end the texts MSTS
  shows (`TextToDisplayOnCompletionIfTriggered` or `IfNotTriggered`). The
  controller records the results again when a run pauses or stops, so the
  reasons read where the trains are then. The route suite prints them
  with `--test-verbose`.
- Checks: 2 in simulation-events (passed before active, never active,
  best speed, siding reached), 2 in simulation-ui (tooltips, Check
  Activity's last run).
- The local content (7 routes, 65 activities): 175 events not fired. 45
  wagon events (shunting, task 07); 30 location events on the other track
  of a double line, 4.4 to 10.3 m from the player's path (sctutorial 7,
  necorridortutorial 12, oetutorial 3, and in winterun, aftstrm,
  tokyohakonetutorial, tnrescue, evegrain): Open Rails looks for the
  point within 2.5 m of the track the train is on, so it never fires them;
  23 of them lie within their radius of the path (decision 4 below); 24
  never active; 20 time events after the run's end; 15 not reached (the
  player's train stuck or its run ended first); 11 PickUpPassengers; 9
  passed before they were active; 6 AllStops with the last stop not left;
  5 off the path; 4 ReachSpeed not reached (a tutorial asking 219 km/h of
  a 158 km/h train); 4 AllStops without stops; 2 passed, active too late
  to stop.

**Phase 5, verified (done 2026-10-21).**
- Every local activity simulated (the route suite on the seven routes):
  the table at the end of the doc, each run's events fired by kind and the
  activity's end. The route suite logs fired events with
  `TSRE_SIM_EVENTS` (kind "event", "activityEnd").
- Play against the core alone: the host suite's input has activity events
  (a time event activating a location event, location events triggered by
  each train); the thread's paced run fires them at the core's times
  exactly (the fixed step).
- Open Rails: `scripts/orts-activity-events.py` reads its activity events
  page (`/activityevents`; connected, Open Rails sends its messages there
  and does not pause), `orts-simulation-compare.sh` runs it with
  `ORTS_EVENTS=1`, `scripts/orts-compare-events.py` matches the two lists
  by name. EUROPE1 sctutorial, Open Rails' autopilot driving the player's
  train at 17 times real time to 08:11:30: all 32 events Open Rails fired
  TSRE fires too, in the same order; time events at the same second;
  location events 6 to 55 s later in TSRE (from the train's first move:
  the trains run differently, below), median 10 s. TSRE fires 3 more:
  "Long Crosby Garrett" (a stop-here event where TSRE's train stops, Open
  Rails' autopilot does not: decision 5), "Can't Move" (a time event at 2
  minutes cancelled by moving past a point 1 m ahead: TSRE's train starts
  later) and "Rolling backward" (a point behind Open Rails' train's front,
  ahead of TSRE's: decision 6). Open Rails fires none of the events on
  the other track of the double line ("Shift 1 View", "Coordinate
  Revereser and Regulator", active, with messages, passed): its 2.5 m
  rule confirmed (decision 4).
- Stopping at stop-here events (decision 1) measured: with it, 42 more
  events fire on the seven routes than without, nearly all detecting a
  player's mistake: oetutorial's "UnexpectedStop1" to "16", "Too Soon
  ...", "Too Far ...", "Long ...", "Short ...", "Stop Wrong Siding";
  a few instructions ("Yoyogi Uehara", "Umegaoka", "MoveSwitch",
  "Detective Now Aboard"). Stopping only along the platforms of station
  stops (the stop moved to the event) fires 3 more mistakes and loses 2
  good ones ("Good Gotokuji", "Perfuchs2"): the first such event along a
  platform is the "too early" one. `SimulationInput::eventStops` (none,
  platforms, all; `TSRE_SIM_EVENT_STOPS` in the route suite) keeps the
  choice (decision 5).

**After the user's decisions (2026-10-21).**
- The player's train no longer stops at stop-here events (None by
  default; checks: by default passed, not fired; the stopping cases with
  the option All).
- Trains start as Open Rails places them, the rear at the path's start
  (`MakeInput`, the route's lead-in the train's length; the editor's
  display of a service's train, `Consist::initOnTrack` with
  `rearAtStart`); loose consists unchanged. simulation-ui: the editor
  shows the player's train where the simulation starts it.
- On the seven routes against the placement before (both not stopping):
  the same trains finish, BNSF one more (13 of 16); the same three trains
  wait for loose consists (they need shunting, task 07), none more;
  arrivals a little earlier (JAPAN1 median 137 to 128 s late, BNSF 365 to
  303 s); the activities end alike (34 success, 3 failure, 28 none).
  Events: sctutorial's "Can't Move" and "Rolling backward" no longer fire
  (they came from the old start), nor two mistake events
  ("Depart Landeck Too Soon", "Departed Sanugbashi Too Early": the
  player's train had started short of its first station and run through
  them); tokyohakonetutorial's "Can't Move" fires now (its train starts at
  Sangubashi and leaves at its booked departure, 5 s before the 105 s
  timer, where a player would leave when told), CanadianImports' Location2
  too.
- Open Rails (sctutorial, the same run): all 32 events on both sides,
  none on one side only, median difference 1 s (was 10 s); the 18 to 32 s
  left around Crosby Garrett and Ormside are the trains braking for the
  stations differently.

## Why

The roadmap (2026-10-17): the activity's events run in the simulation,
so the designer sees when each event fires, which never fire and why, and
the messages at the time they would show in the game. With the player's
train run by TSRE (today as an AI train, in task 07 a full autopilot that
also does the work orders, decided 2026-10-20) an activity can be checked
from its start to its end without playing it.

## Current state

- `ActivityEvent` reads and writes every event field and outcome (task
  editor 07), and `ActivityCheck` checks what events name (stations,
  sidings, wagons, linked events). Nothing evaluates events.
- The simulation (`src/tsre/sim/`) runs the player's train as an AI train
  on its timetable, and the AI trains; loose consists only hold their
  track (`StaticConsist`). Its input has no events and no car ids:
  `Vehicle` has the length, type, Eng and passenger flag.
- The core steps at most 0.1 s, the last step of a tick shorter (the
  tick's remainder), so the same run can step differently in Play and in
  Simulate; the fixed step decided 2026-10-17 is not done yet.
- The Timetable window lists the chosen train's simulation events
  (`TrainEvent`: started, arrived, departed, waiting, reversed, blocked,
  deadlock, finished). The map's Events layer draws location events and
  their radius. Nothing shows a message box in the 3D view.

## Census of the local content (2026-10-20)

68 activities, 476 events: 271 location, 88 time, 117 action (50
AllStops, 24 DropOffWagonsAtLocation, 14 PickUpWagons, 11
PickUpPassengers, 8 ReachSpeed, 7 AssembleTrain, 3
AssembleTrainAtLocation).
- Location events: 70 have `TriggerOnStop ( 1 )` (the train must stand
  within the radius), 61 of them in the six tutorials; radii are small,
  189 of 271 at most 10 m. 250 start active, 21 inactive (level 0 or
  below).
- Time events: 0 to 10500 s after the start, median 67 s.
- Outcomes: 320 messages, 73 ActivitySuccess, 12 ActivityFail, 55
  DecActLevel, 54 IncActLevel, 1 ActivateEvent, 1
  StartIgnoringSpeedLimits. No Open Rails additions (triggering trains,
  restarts, sounds, weather, `ORTSContinue`): the content is MSTS's, so
  those are tested with written fixtures.
- Wagon lists name the player's cars (consist 0) or loose consists' (the
  object's id): AssembleTrain 4 lists of the player's cars only and 3
  mixed; AssembleTrainAtLocation 3 of the player's; DropOff 11 of the
  player's, 12 of loose consists', 1 mixed; PickUpWagons 14 of loose
  consists'. Five of the ten assemble events (three AssembleTrain, two
  AssembleTrainAtLocation) list exactly the player's train as it starts,
  so they can fire without any shunting.

## Reference: Open Rails' rules

The activity is checked each frame after the trains moved
(`Activity.Update`):
- An event is looked at while its activation level is above 0, and only
  until it has fired once, unless it is reversible.
- When its condition holds and it is not disabled, it fires: its count
  goes up; a reversible event is disabled (enabled again once its
  condition no longer holds), any other gets activation level 0. Its
  outcomes act only the first time (`IsActivityEnded`), in this order:
  activate (level 1), restore (to its level in the file), decrease,
  increase the listed events' levels; a sound or weather change noted; a
  failure ends the activity unsuccessfully, else success ends it
  successfully, else a waiting train is restarted. Then nothing else is
  checked until the event's message window is closed: a message pauses
  the game until the player closes it (`ORTSContinue` below 0, the
  default), or stays for `ORTSContinue` seconds while the game runs; an
  event without a message lets the next be checked on the next frame.
  After the activity ended no event is checked.

What each kind checks:

| Kind | Fires when |
| --- | --- |
| Time | seconds since the activity's start (whole seconds) reach its `Time` |
| Location | the player's train's front (just after a reversal, its old front) is closer than the radius along the track to the event's point, which must lie within 2.5 m of that track's centre line (`Traveller.DistanceTo`); with TriggerOnStop also its speed is at most 0.032 m/s. With `ORTSTriggeringTrain` the named AI train (service, and start time when given) instead, once it exists: until then the player's train is looked at |
| AllStops | the player's last station stop is done: boarding complete there, the train gone before it was, or the stop missed. Never for a player train without stops |
| AssembleTrain | any train (loose consists included) holds exactly the listed cars, in order or reversed |
| AssembleTrainAtLocation | the player's train has an end (front or rear) inside the siding, and some train holds exactly the listed cars |
| DropOffWagonsAtLocation | a standing consist (cars left uncoupled) holds all listed cars, in any order, with an end inside the siding |
| PickUpWagons | the player's train holds all listed cars (others may be in it) and its speed is at most 1 m/s |
| ReachSpeed | the player's engine's speed reaches `Speed` (m/s) |
| PickUpPassengers | never (an empty case) |

Car ids: the player's cars are "0 - UiD", a loose consist's "id - UiD"
(the wagon list's `UiD` is the consist's number × 65536 + the car's UiD);
AI trains' cars are "AI…" and cannot be named. Open Rails ignores
`TextToDisplayOnCompletionIfTriggered`/`IfNotTriggered`,
`TextToDisplayDescriptionOfTask` and the Start/StopIgnoringSpeedLimits
outcomes (in MSTS the latter stop and restart counting speeding in the
activity's evaluation, as far as their name tells).

`ORTSRestartWaitingTrain`: the named AI train, waiting at (or still
coming to) a waiting point whose value is `ORTSMatchingWPDelay`, waits
`ORTSDelayToRestart` seconds from now instead; an "until HH:MM" waiting
point gets that time rounded up to the minute.

## Design

### 1. Events in the input

`SimulationInput` gets the activity's events as plans, made on the main
thread from `Activity::event` (TSRE's parser) when the input is made:
- each event's id, kind, activation level, reversible flag,
  `ORTSContinue`, and whether it has a message;
- time events: the seconds after the start;
- location events: where the event's point lies on each train route that
  can trigger it (the player's, or the triggering AI train's): its route
  part and route distance, wherever a section of the part runs within
  2.5 m of the point (`TDB::findNearestPositionsOnTDB`); the radius and
  TriggerOnStop. A point no route passes stays without places (section
  7 explains it);
- ReachSpeed: the speed; wagon events: the listed car ids, and the
  siding as the track between its two items;
- outcomes as data: the event ids whose levels change and how, success,
  failure, the train to restart with its waiting point value and delay;
  messages, sounds and weather changes only as "this event has one" (the
  texts stay in the editor's `Activity`).

Cars get ids: each `Vehicle` of the player's train and of loose consists
its Open Rails id (consist × 65536 + UiD); `StaticConsist` its vehicles
in order. This is the first part of task 07's vehicle table: the lists do
not change in this task.

### 2. Checking events in the core

The core keeps each event's state (activation level, times fired,
disabled, the time it last fired) and the activity's end (none, success,
failure, when). After all trains have advanced in a step it checks the
events as Open Rails does (the reference above), with two choices for a
simulation without a player at the screen:
- **Chains in one step.** Where Open Rails fires one event a frame and
  then waits for its window, the core checks again from the first event
  after each one fires, until none fires (each event at most once a
  step): events that activate each other fire at the same time, as in a
  game paused by each message.
- **`ORTSContinue`.** An event with a message and `ORTSContinue` above 0
  holds the other events for that many simulated seconds (its window
  stays while the game runs); 0 and below hold nothing.

Each firing is recorded as a simulation event (`TrainEvent` gets the
kinds `EventFired` and `ActivityEnded`; the train the one that triggered
it, -1 for none): the Timetable window, Simulate and the tests read them
as they read the trains' events. Frames carry the events' states, shared
with the frame before while unchanged (as the aspects are), so the map
and the Event window follow a run. A restart changes the AI train's own
copy of its waiting points (`Runtime::waits`). The activity's end stops
the checking; the trains run on.

Cost: a step looks at the active events only; each check is a comparison
of route distances, a speed or a few short lists.

### 3. The player's train: wagon lists, stops, speed

The player's train is run by TSRE (task 01's decision), so what an event
asks of the player has to be done by it:
- **Station stops and AllStops.** The player's stops are done as the core
  does them now; AllStops fires when the train's last stop is done
  (boarding complete, Open Rails' completion of the last stop). Stops the
  input could not place are not stops (as Open Rails skips a stop whose
  platform the TDB lacks), and are named in the warnings.
- **Speed.** ReachSpeed fires if the train reaches the speed on its run;
  if it never does, the highest speed it reached is shown (section 7).
- **"Stop here" location events (decided 2026-10-20, revised 2026-10-21:
  not by default, an option).** With
  TriggerOnStop the player is asked to stop at the place. The player's
  train stops with its front at the event's point when the event is
  active as the train comes within its braking distance of it, and goes
  on once it has fired (signals permitting). Where the point lies along
  a platform the train stops at, the stop's place moves to the point (it
  stands there its boarding time). An event activated too late for the
  train to stop is passed, as a player not told in time would pass it.
  Without this, 70 local events (most of the tutorials' chains) never
  fire and nothing after them happens.
- **Wagon events.** Checked on the cars where they are: in this task the
  trains and loose consists never change, so a wagon event fires only if
  its condition holds as the activity starts (or once the player's train
  reaches the siding, for AssembleTrainAtLocation). The others wait for
  task 07, where the autopilot does the work orders; section 7 says what
  each would need.

### 4. In the editor

- **Event window:** each event's result from the last run of the shown
  activity: its firing time (and how often, for a reversible one), or
  "not fired" with why; its outcomes as they acted. Results are kept for
  the session per activity and marked out of date once the activity's
  events are edited.
- **Timetable window:** the activity's events among the player's train's
  events ("08:12:31 Event Approach Settle: message"), a triggering train's
  events also among its own, a restart among the restarted train's; the
  activity's end ("09:42:10 The activity ends: success").
- **Map, Events layer:** location events drawn by state: not active,
  active, fired (with its time beside the name), not fired at the run's
  end; the chosen wagon event's siding and listed cars marked. An event's
  message is shown beside the train that fired it (the player's train for
  events that look at no train) while the simulation runs, and hides
  after a while.
- **No message boxes and no pausing** (decided 2026-10-20): for the
  simulation and the autopilot the log (the Timetable window's list) is
  enough; how the game shows messages belongs to the game's interface,
  not designed yet.
- **Simulate** (Timetable window, task 04): records the event results too,
  so the Event window has firing times without Play.
- **Check Activity:** a "Last run" part: the events that never fired and
  why, an activity that never ends, and at its end the texts MSTS shows
  (`TextToDisplayOnCompletionIfTriggered` or `IfNotTriggered` per event).

### 5. Results after a run

A run ends as Simulate's does: every train finished or settled, or 24
hours. Then each event not fired gets its reason (section 7). Results of
a run stopped early are kept as "up to HH:MM".

### 6. Fixed step (decided 2026-10-17)

The core steps exactly 0.1 s; a tick's remainder waits for the next tick
(time kept as the start plus steps × 0.1, no drift). Play, Run to and
Simulate then step alike, so an event fires at the same time in each and
on every run. 0.01 s steps for driven trains come with the driving tasks.

### 7. Why an event did not fire

- never active (its level stayed at 0 or below), or active only from
  HH:MM;
- location: no route of a train that triggers it passes within 2.5 m of
  its point (the nearest track and distance named); the train passed but
  did not stand there (TriggerOnStop: active too late to stop); its train never
  came (not started, stuck, the triggering train not in the activity);
- time: after the run's end;
- AllStops: the player's train has no stops, or did not reach its last;
- ReachSpeed: the highest speed reached;
- wagon events: where each listed car is (the player's train, a loose
  consist and where, not found), and whether the player's train reached
  the siding: work for task 07's autopilot;
- PickUpPassengers: never fires (as in Open Rails);
- the activity ended before it.

### Not in this task

- Coupling, uncoupling and the autopilot's work orders, AI trains doing
  work orders: task 07.
- Carrying out sound and weather outcomes (the world's weather is TSRE's
  own keyframe model, task environment): listed with their times only.
- Start/StopIgnoringSpeedLimits: listed; no effect on TSRE's driving.
- Open Rails' evaluation (scores, penalties), saving and loading runs.

## Phases

1. **Core:** the fixed step; car ids; the events' plans in the input
   (location places on routes, sidings); their checking as Open Rails',
   chains and `ORTSContinue`; outcomes (levels, the end, restarts); the
   records and frames. Fixtures in simulation-core: each kind, chains,
   reversible events, levels below 0, a triggering train, a restart, a
   point off the track, AllStops without stops.
2. **The player's train:** stopping at "stop here" events,
   a stop's place moved to the point; fixtures: in time, too late, at a
   platform.
3. **Editor:** the Event window's results, the Timetable list, the map's
   states and times, messages beside the trains, Simulate's results,
   session results marked out of date. simulation-ui cases.
4. **Why not fired and Check Activity:** reasons, the Last run part, the
   completion texts.
5. **Verified:** every local activity simulated: per activity the events
   fired, those that did not and why, and the end, tabled here; each run
   twice gives the same times; Open Rails compared on a few activities
   (its autopilot driving the player's train, a client on its
   `/activityevents` websocket so messages do not pause it, `/API/TIME`
   for the times): the same events fire, at nearly the same times where
   the trains run alike; the cost per step measured on the largest
   activities.

## Decisions (user, 2026-10-20)

- The player's train stops at "stop here" location events (TriggerOnStop)
  when they are active in time (section 3). Revised 2026-10-21: it does
  not (below).
- A triggering train's event before that train starts: as Open Rails, the
  player's train looked at until then; the event list says when the
  player's train fired it, and Check Activity warns of it.
- No pausing on messages and no message boxes for now: the log is enough
  for the simulation and the autopilot; the game's interface is not
  designed yet. On the map an event's message shows beside the train that
  fired it, hiding after some time.
- The player's work orders (task 07): TSRE's full autopilot.

## Decisions (user, 2026-10-21)

- **Stop-here events: the player's train does not stop at them**
  (decision 1 revised after phase 5's measurements):
  `SimulationInput::eventStops` is None by default; Platforms and All stay
  as options (task 07's autopilot may know an event's intent from the work
  it asks for).
- **Trains stand at their start as Open Rails places them**: the rear at
  the path's start, the whole train ahead, in the simulation and in the
  editor's display of a service's train (`Consist::initOnTrack` with
  `rearAtStart`). Loose consists stay as they are: MSTS's files, Open
  Rails and TSRE all put the middle of the consist's first car at the
  object's place, the rest behind it (Open Rails' `Simulator.cs`, static
  consists: "the specified location represents the middle of the last
  car", its trains' cars being the file's backwards).

## Open: location events on another track (for investigation)

Open Rails fires a location event only where its point lies within 2.5 m
of the track the train runs on; 30 local events (MSTS's own tutorials
among them) lie 4.4 to 10.3 m away, on the adjacent track of a double line,
23 within their radius of the train's path. Open Rails confirmed it in
phase 5: they never fire there. What was found (2026-10-21):
- **Open Rails** has measured along the train's track since its first
  location events (`1a4ae26e5`, 2011, `TDBTraveller.DistanceTo`: the point
  within 1.5 m of a straight's centre line), the tolerance widened to
  2.5 m with the Traveller of 2012 (`580d035dc`,
  `MaximumCenterlineOffset`). No setting, no legacy mode, nothing in the
  history about MSTS's rule.
- **MSTS** (`train.exe`, `FUN_0059b146`, found from the location event's
  writer `FUN_0059bd21`, which writes x, z and radius from offsets 0x76,
  0x7a, 0x7e of the event; the ghidra project of `msts-analysis`): the
  straight distance in x and z from a place of the player's train (the
  object at offset 0x62 of the global at 0x007c2ac0, its x at 0x170 and z
  at 0x178; which point of the train not traced) to the event's point,
  squared, against the radius squared; with TriggerOnStop also the
  train's speed (offset 0x92 of that global) below 0.1 (m/s). No track: an
  event on the adjacent track fires within its radius.
- The 30 events are both kinds: tutorial instructions ("Tunnel", "Shift
  1 View", "Approaching New Carrollton", "Adjust Speed for NC") and
  catchers of a player's mistakes ("80 mph catcher", "Too fast for TSRZ",
  "Penalty Brakes", "Minor Damage", "Too soon Pians", "UnexpectedStop15").
  In MSTS both fire.

Plan (user): MSTS and Open Rails differ, so both modes are to be offered:
by the event's radius (MSTS) and at most 2.5 m from the train's path
(Open Rails); how to define them is still open. Until then TSRE keeps Open
Rails' rule and Check Activity says when a point is on another track.

## Local activities (2026-10-21)

Each activity's events fired by kind (fired/all) and its end, in the
route suite's run (not stopping at stop-here events, trains placed as
Open Rails places them).

| Route | Activity | Events fired | End |
| --- | --- | --- | --- |
| EUROPE1 | aftstorm | allstops 1/1 | success at 16:32:13 |
| EUROPE1 | homestre | assemble 1/1, location 1/2, passengers 0/1, time 2/2 | no end |
| EUROPE1 | itr_e1_s1_w1_t1 | allstops 0/1 | no end |
| EUROPE1 | lclsrvce | allstops 1/1 | success at 11:10:21 |
| EUROPE1 | lowwater | allstops 0/1 | no end |
| EUROPE1 | medemerg | allstops 1/2, location 1/1, time 0/1 | success at 10:55:27 |
| EUROPE1 | rsrescue | allstops 0/1, assemble 0/1, dropoff 0/1, location 1/1 | no end |
| EUROPE1 | sctutorial | allstops 1/1, location 24/39, passengers 0/1, time 10/11 | success at 08:15:35 |
| EUROPE1 | shrtpass | allstops 1/1 | success at 08:56:19 |
| EUROPE1 | winterun | assemble 1/1, location 0/2, passengers 0/2, time 1/1 | no end |
| EUROPE2 | barcarso | allstops 0/1, assemble 0/1, dropoff 0/1, time 0/1 | no end |
| EUROPE2 | bridegrm | allstops 1/1, time 0/1 | success at 15:09:25 |
| EUROPE2 | itr_e1_s1_w1_t1 | allstops 0/1 | no end |
| EUROPE2 | murder | assemble 0/1, dropoff 0/1, location 4/6 | no end |
| EUROPE2 | oetutorial | allstops 1/1, location 26/57, passengers 0/1, speed 0/1, time 13/14 | success at 12:19:37 |
| EUROPE2 | replloco | allstops 0/1, assemble 0/1, location 2/2, passengers 0/1, time 0/1 | no end |
| EUROPE2 | royalonb | allstops 1/1, time 0/1 | success at 20:08:56 |
| EUROPE2 | sainnwn | allstops 0/1, passengers 0/1, time 1/1 | no end |
| EUROPE2 | shrtpass | allstops 0/1, time 0/1 | no end |
| JAPAN1 | afterthestorm | allstops 1/1, location 2/2, time 0/1 | success at 10:17:07 |
| JAPAN1 | hvydelay | allstops 1/1, time 0/1 | success at 10:05:35 |
| JAPAN1 | itr_e1_s1_w1_t1 | allstops 0/1 | no end |
| JAPAN1 | pubhol | allstops 1/1, time 1/1 | success at 11:52:01 |
| JAPAN1 | shinhakmachida | allstops 1/1 | success at 17:23:05 |
| JAPAN1 | shnhaksu | allstops 1/1 | success at 09:38:21 |
| JAPAN1 | shrtpass | allstops 1/1 | success at 09:06:51 |
| JAPAN1 | tokyohakonetutorial | allstops 1/1, location 17/34, passengers 0/1, time 14/14 | success at 18:12:03 |
| JAPAN2 | hisatsututorial | allstops 1/1, location 24/29, passengers 0/2, time 6/7 | success at 10:10:46 |
| JAPAN2 | isaburo | allstops 1/1, time 1/1 | success at 14:38:45 |
| JAPAN2 | itr_e1_s1_w1_t1 | allstops 1/1 | success at 14:05:24 |
| JAPAN2 | lowfuel | allstops 1/1, time 0/1 | success at 16:18:18 |
| JAPAN2 | quaketrk | allstops 1/1, location 1/1 | success at 18:58:51 |
| JAPAN2 | shinpei | allstops 1/1, location 2/2 | success at 16:47:52 |
| JAPAN2 | shrtpass | allstops 1/1, time 0/1 | success at 18:42:38 |
| JAPAN2 | wst_rver | allstops 1/1, time 1/1 | success at 21:51:17 |
| USA1 | aftstrm | allstops 1/1, location 0/2, time 1/1 | success at 17:53:07 |
| USA1 | brdgstrk | allstops 1/1, location 1/1, time 0/1 | success at 18:01:45 |
| USA1 | itr_e1_s1_w1_t1 | allstops 1/1 | success at 13:53:31 |
| USA1 | morningmaryland | allstops 1/1, location 0/2 | success at 08:31:42 |
| USA1 | necorridortutorial | allstops 1/1, location 18/36, passengers 0/1, speed 1/7, time 5/6 | success at 10:20:08 |
| USA1 | rdngyllw | allstops 1/1, time 0/1 | success at 15:12:47 |
| USA1 | shortpr | allstops 1/1, time 1/1 | success at 11:17:03 |
| USA1 | smmrex | allstops 1/1, time 1/1 | success at 14:03:21 |
| USA1 | tnrescue | allstops 0/1, location 0/1, pickup 0/1 | no end |
| USA2 | autotrnsetout | dropoff 0/1, location 1/2 | failure at 10:08:18 |
| USA2 | dawnoil | assemble 1/1, location 4/5, time 0/1 | success at 07:22:56 |
| USA2 | evegrain | location 1/3 | no end |
| USA2 | freighttutorial | assemble-at 0/1, location 12/19, pickup 0/1, time 7/9 | failure at 10:01:53 |
| USA2 | hotbox | dropoff 0/1, location 4/6, time 0/2 | failure at 17:01:39 |
| USA2 | itr_e1_s1_w1_t1 | allstops 0/1 | no end |
| USA2 | local_cutbank | dropoff 0/2, location 0/4, pickup 0/1 | no end |
| USA2 | local_one | dropoff 0/1, location 1/2 | no end |
| USA2 | local_two | dropoff 0/1, location 0/1, pickup 0/1 | no end |
| USA2 | mainduty | dropoff 0/1, location 1/1 | no end |
| USA2 | mornimod | assemble-at 0/1 | no end |
| USA2 | nnmxdgds | dropoff 0/1, location 1/2 | no end |
| USA2 | whitefishydsw | assemble-at 0/1, dropoff 0/7, location 0/1 | no end |
| USA2 | whitpull | dropoff 0/1, time 1/1 | no end |
| USA2 | yard_one | dropoff 0/1, pickup 0/2 | no end |
| USA2 | yard_two | dropoff 0/1, pickup 0/3 | no end |
| BNSF_SCENIC | 1_WhatGoesUp | allstops 0/1, location 1/1 | success at 13:26:53 |
| BNSF_SCENIC | 2_MustComeDown | allstops 0/1, location 0/1 | no end |
| BNSF_SCENIC | 3_CanadianImports | allstops 0/1, location 1/2 | no end |
| BNSF_SCENIC | 4_lumberrun | dropoff 0/3, location 0/1, pickup 0/5, time 1/1 | no end |
| BNSF_SCENIC | 5_wb_empirebuilder | allstops 1/1 | success at 09:38:00 |
