# Task 04 - Timetable from the Simulation

Status: done (phases 1 to 4, 2026-10-16, branch `feature/simulation`).
Decisions taken are at the end.

Sources:
- tasks 01 to 03 (the simulation core, signals, driving: every local
  route's stops within about a minute of Open Rails');
- the Timetable window and its properties (`ActivityTimetableWindow`,
  `ActivityTimetableProperties`, editor task 07), `ActivityTrains`
  (trains, their timetables, shared patterns, own copies), `ActivityEdit`
  (undo);
- Open Rails (a behavioural reference): `StationStop.CalculateDepartTime`.

## Progress

**Phase 1, core and input (done 2026-10-16).**
- `SimulationInput::departureStep`: a station's departure (and a train's
  first station's) rounded up to it, the train waiting for it
  (`SimulationCore::departureAt`); `StopPlan::entry`, its timetable row.
- `SimulationCore::settled()`: nothing more will happen (every train
  finished, at its path's end, in a deadlock or held by a standing
  consist); the route suite runs to it too.
- `TimetableCalculation` (`src/tsre/sim`): `Prepare` makes the input on the
  main thread (which departures hold, 2), `run` simulates to the end on any
  thread, `write` writes the times and keeps the run for the session;
  `Last`/`LastTrain` for the window. The trains' state texts moved to
  `SimulationTexts` (the controller's and Simulate's).
- Fixtures: rounding (30 s and 15 s steps, a booked departure off the
  step).

**Phase 2, Simulate in the Timetable window (done 2026-10-16).** Under
the trains: Simulate, Round to (15 s, 30 s, 1 min, none), Times for (all
trains, the player's train; the player's alone and disabled when the
pattern is shared or missing, with why), From scratch. The run on a worker
thread behind a progress dialog (Cancel); the write is one undo step;
Simulate is off while the panel's simulation exists. The old Calculate
(one train's estimate) and `ActivityServiceDefinition::calculateTimetable`
removed.

**Phase 3, the changes shown (done 2026-10-16).** The stops' table has a
Change column: the arrival's change against the run before ("+1:30"),
and the departure's where it differs ("0:00 / +2:00"); "new" for times a
row had not; "not reached" (greyed, the train's state as tooltip); "–" for
a stop whose platform is not on the path. Departures that are the user's
in bold: kept by the run, or set since (shown once the edit ends). A line
under the trains: the time simulated, the step, whose times were written
(the pattern's other users when shared), how many trains changed and the
largest change, each train that did not finish and why ("deadlocked:
waiting for ..."). Polish texts with the English.

**Phase 4, verified (done 2026-10-16).**
- simulation-ui: Simulate writes both trains (the file's departures hold
  them, 08:04:20 rounded to 08:04:30); again: nothing changed; From
  scratch: departures after boarding; one undo brings the times back; the
  file's departures hold again; a departure set in the table is bold,
  kept, its change shown; a shared pattern writes the player's alone; a
  deadlock names the trains that did not finish.
- simulation-route, every local route: each activity simulated twice, the
  second run changes nothing (JAPAN1 8 activities, 1117 stops; USA2 16;
  EUROPE1 10, 417 stops); the times restored after. A saved calculated
  timetable, loaded again in a new session, simulates to the same times.
- Open Rails on a calculated timetable: JAPAN1's `itr_e1_s1_w1_t1` from
  10:30 (a scratch copy of the route: links, a copy of the activity and of
  its traffic pattern), its 566 stops simulated and saved by TSRE
  (`TSRE_SIM_TIMETABLE_SAVE`, refused outside a copy marked
  `TSRE_SCRATCH` or through a link), then run in Open Rails, which loads
  the files TSRE wrote. Its trains never leave before the timetable; at
  56 % of 322 stops they leave on it, at 30 within 30 s; arrivals against
  ours median −6 s (−21 s on the untimed timetable before). The late
  departures all begin at Yurigaoka and Chitose Funabashi, where Open Rails
  stands its trains 182 s (their platforms' minimum waiting time, 183 s,
  which it uses in an edge case of its boarding computation; elsewhere on
  the same route it stands 18 to 55 s, by passengers), and carry on from
  there.

## Left

- Open Rails' 183 s stands at Yurigaoka and Chitose Funabashi (above):
  where and why its boarding computation takes the platform's minimum
  waiting time, to stand the same there (the timetables we calculate are
  late from there in Open Rails).
- Times past midnight are written wrapped; a booked departure after
  midnight is not unwrapped for a train still running from the day before
  (task 01's limit).

## Why

The user (2026-10-16): the Timetable should have a button that runs the
simulation fast and updates the timetables. Working on an activity is then
an incremental process: simulate; where a train ends in a deadlock or
should wait (say at a large station), set a later departure; simulate
again; the Timetable shows what changed against the run before.

## Current state

- The simulation runs as fast as possible (the activity panel's speed
  "As fast as possible", "Run to"); a whole activity takes about a second
  (JAPAN1's 8 activities, 50 trains, 30.8 simulated hours in 4.2 s).
- Nothing writes its results back. The timetable properties' "Calculate"
  is an older estimate for one train alone: the consist's constant
  acceleration, no stops' dwell, no signals or other trains; it takes the
  times there as minimums, so it only ever moves times later.
- What a booked time means, in Open Rails and in our simulation alike: a
  train leaves a stop at the later of its booked departure and its arrival
  plus its boarding time; the booked arrival constrains nothing (Open
  Rails only checks it for a schedule more than 4 hours off).

## Design

### 1. Simulate

A "Simulate" button in the Timetable window runs the chosen activity from
its start to its end as fast as possible: all its trains, since they meet,
follow and wait for each other. It runs on its own core and thread, apart
from the panel's simulation (it is off while that one exists: activity
editing is off then), and ends when every train has finished, or every one
left stands deadlocked, at its path's end or held by a standing consist,
or 24 hours after the start.

Then, for the trains whose times it writes, every stop's arrival and
departure as simulated, rounded to the step (3), past midnight wrapped; one
undo step for the whole write. Where trains got stuck, the stops they
reached are written, the rest left as they were and marked, with where
the train waits and for what (as the Timetable shows waits now).

### 2. Which departures hold a train

Nothing is stored beyond the activity's own files. A departure in the
timetable is a "not before" (as Open Rails takes it), but a timetable the
simulation wrote must not hold its own trains on the next run, else a train
held up by a conflict the user has since removed would keep its slower
times. So the session keeps, per timetable row, what the runs wrote (they are
needed for the comparison too):
- a departure a run calculated (one that held nothing) is calculated
  again: it may come earlier or later;
- any other is the user's (typed, the file's, or one undo brought back)
  and is kept: the train leaves no earlier;
- where a kept departure could not be met (the train came later, or it
  was off the step) the run writes the later one and remembers the user's
  under it: the next run keeps the user's while the row still shows that
  later one;
- after a reload every booked departure is the user's; "From scratch"
  calculates them all again.

AI trains whose times are not written (a shared pattern, 4) keep all
their booked departures.

### 3. Rounding

Departures are rounded up to the step during the run: the train waits for
its rounded departure, so every later time follows from it, as Open Rails
will run the timetable. Arrivals are rounded down (they constrain
nothing). The step: 30 s by default (common in real timetables); 15 s,
1 min or none to choose. Its cost is about half a step a stop: 30 s adds
about 8 minutes to a JAPAN1 stopper's 33 stops in 1.5 hours.

### 4. Whose times are written

- The player's times are in the activity: always written.
- The AI trains' are in the traffic pattern. When the activity has its own
  pattern, they are written too (or the player's alone, chosen).
- When the pattern is shared with other activities, Simulate still runs,
  writing the player's times alone: a player timetable that fits the
  existing traffic, the AI trains keeping theirs. To rewrite the traffic's
  times, the window's "Make Own Copy..." first.

### 5. What changed

Per stop, the change against the last run (or, on the first, against the
times before it): "+1:30", "−0:30"; departures the user set since the last
run marked as such; stops not reached marked. A line under the trains: how
many trains changed, the largest change, trains stuck and where.

## Phases

1. **Core and input:** the departure step at stops (and a train's first
   station); which departures hold (2); each stop's timetable row kept in
   the input (rows the route cannot place are skipped now); a run to the
   end shared with the route suite. Fixtures: rounding, a calculated
   departure freed, a set one kept.
2. **Simulate in the Timetable window:** the run on its thread, writing
   (player, all trains), shared patterns, undo, stuck trains reported.
3. **The changes shown:** per stop, marks, the summary line.
4. **Verified:** on every local route, each activity simulated twice: the
   second run changes nothing; the user's flow scripted (a later
   departure, simulate again); Open Rails run on a calculated timetable
   (a scratch copy of the route): its trains leave on time.

## Decisions (user, 2026-10-16)

- This task before `sigscr.dat` (now 05).
- Nothing stored for the calculation: simulate, set a later departure,
  simulate again; the Timetable shows what changed against the run before.
- Rounding: 30 s by default; 15 s possible.
- Shared traffic: Simulate available; only the player's timetable is
  written then, fitting the existing traffic; an own copy to rewrite the
  traffic's times.
- "Yes to all": the old Calculate replaced by Simulate; stuck trains: the
  stops reached written, the rest marked; an own pattern: all trains by
  default, the player's alone to choose; departures rounded up, arrivals
  down.
