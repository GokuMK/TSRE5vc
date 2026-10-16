# Task 04 - Timetable from the Simulation

Status: designed 2026-10-16; decisions taken and open at the end.

Sources:
- tasks 01 to 03 (the simulation core, signals, driving: every local
  route's stops within about a minute of Open Rails');
- the Timetable window and its properties (`ActivityTimetableWindow`,
  `ActivityTimetableProperties`, editor task 07), `ActivityTrains`
  (trains, their timetables, shared patterns, own copies), `ActivityEdit`
  (undo);
- Open Rails (a behavioural reference): `StationStop.CalculateDepartTime`.

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
left stands deadlocked, at its path's end or waiting for track, or 24
hours after the start.

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
times. So the last run's written times are kept in memory (they are needed
for the comparison too):
- a departure still as the last run wrote it is calculated again (it may
  come earlier or later);
- one the user changed since is kept: the train leaves no earlier;
- without a run before in this session (after a reload) every booked
  departure is kept; "From scratch" calculates them all again.

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

## Open decisions

1. **The old "Calculate"** (one train alone, times only later): replaced by
   Simulate? Recommended.
2. **Stuck trains:** write the stops reached and mark the rest (above), or
   write nothing until every train finishes? Recommended: write the
   stops reached (one undo step brings the times back).
3. **Own pattern:** write all trains by default, with "the player's train
   alone" to choose? Recommended.
4. **Rounding's direction:** departures up, arrivals down? Recommended
   (the written stand covers the real one).
