# Task 06 - Activity Events

Status: phases 1 and 2 done 2026-10-20 (branch `feature/simulation`).
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
- **"Stop here" location events (decided 2026-10-20).** With
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
  when they are active in time (section 3).
- A triggering train's event before that train starts: as Open Rails, the
  player's train looked at until then; the event list says when the
  player's train fired it, and Check Activity warns of it.
- No pausing on messages and no message boxes for now: the log is enough
  for the simulation and the autopilot; the game's interface is not
  designed yet. On the map an event's message shows beside the train that
  fired it, hiding after some time.
- The player's work orders (task 07): TSRE's full autopilot.
