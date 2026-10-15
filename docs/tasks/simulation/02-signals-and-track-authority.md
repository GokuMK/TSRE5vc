# Task 02 - Signals and Track Authority (No Scripts)

Status: design (2026-10-15), for review. Decisions taken so far are at the
end; nothing is implemented yet.

Sources:
- `msts-analysis/reports/signalling/` (2026-10-14, -15): `overview.md`,
  `msts-signalling.md`, `orts-signalling.md`, `basic-timing-preview.md`,
  `preview-design.md`, `ai-driving.md`, `signal-scripting.md`,
  `tdb-signal-extension.md`.
- Open Rails (`openrails-linux-spike`), a behavioural reference only:
  `Signals.CreateTrackCircuits`, `TrackCircuitSection` (reserve, occupy,
  release), `SignalObject.requestClearSignal`, `SIGSCRfile.SH_update_basic`,
  `Train.UpdateNodeMode` (Auto Node), `DeadlockInfo`, `AITrain`; the manual
  (`operation.rst`: track access rules, `SignalNumClearAhead`, deadlocks and
  passing paths).
- TSRE on `feature/simulation` after task 01: `src/tsre/sim/` (the core,
  routes, the thread), `SigCfg`/`SignalType` (all of `sigcfg.dat` read),
  `TRitem` (signal, speed post and crossover items), `SignalObj` (signal
  lights drawn in their default aspect, task renderer 26).

## Objective

Trains wait for each other: the track they occupy and the track reserved
for them are theirs alone, signals give and withhold authority, junctions
are set for the train that has them, and speeds follow the line's limits.
Everything the TDB and `sigcfg.dat` say about signals is used; signal
scripts (`sigscr.dat`) are not (task 03). Where trains cannot pass each
other, the editor says so.

In scope:
- the track divided into sections at stop signals, junctions and diamond
  crossings; occupation by a train's whole length; reservations; release
  when the rear leaves;
- signals clearing ahead of a train (`SignalNumClearAhead`, Open Rails'
  legacy and Open Rails modes), stopping trains at signals not cleared;
  Auto Node authority on track without signals;
- junctions set and locked by the train that reserved them; `route_set`
  from the TDB's junction links;
- aspects and draw states from a built-in rule per signal function (what
  the stock scripts do), with the aspects' speeds;
- speed posts and the activity's restricted speed zones;
- single-track stretches claimed by one train at a time, deadlocks found
  and reported to the user;
- path-based passing paths (Open Rails' default rule);
- the editor: aspects on the signals in 3D, occupied and reserved track on
  the map, why each train waits.

Not in scope: `sigscr.dat` and other scripts (task 03); Open Rails
additions that need data from the world files (`HASHEAD`, exact signal
grouping: they wait for the TDB fields proposed in
`tdb-signal-extension.md`); location-linked passing paths; permissive
working, call-on and coupling; curve and gradient speeds; the player
driving.

## Current state (task 01)

- Each train has a route (`TrainRoute`): the path's legs in order, split at
  reversals, as intervals of TDB vector nodes with route distances. The
  driver brakes to targets (stops, waiting points, reversals, the path's
  end) on an exact constant-deceleration curve, up to the route's limit and
  the train's top speed × efficiency. Trains pass through each other.
- The input holds a copy of the track geometry (poses) but not its
  topology: no junctions, no items.
- `SigCfg` keeps every signal type: function (`NORMAL`, `DISTANT`,
  `REPEATER`, `SHUNTING`, `INFO`, `SPEED`, Open Rails functions), aspects
  with speeds and flags, draw states, lights, `SignalNumClearAhead` values
  in file order, approach control settings, script names. It reads only
  the route root's `sigcfg.dat`, not `OpenRails/sigcfg.dat`.
- TDB signal items: flags (bit 0 a junction link; bits 4 to 9 number
  plate, gradient plate, USER1 to USER4, read by `sig_feature`), the
  direction they face (0 or 1 along the node), the signal type's name and
  the junction links (`TrSignalDirs`: junction node, branch). Speed post
  items: limit, warning or resume, passenger and/or freight, the value,
  units. Crossover (diamond) items: the item on the crossing track.
- Signal lights are drawn in each head's default aspect; the world's
  signal objects know their heads' TDB items.

## Reference: what MSTS and Open Rails do

- **MSTS** (native code, `msts-signalling.md`): the engine scans the track
  for trains (`block_state`), checks a head's linked junction
  (`route_set`), owns heads and junctions for trains (the `enabled` flag,
  release when the train has passed) and builds a constraint list for the
  AI driver (signals, trains ahead, speed limits, curves, gradients,
  stops). Scripts only choose each head's aspect and draw state.
- **Open Rails**: track circuits derived from the TDB, split at NORMAL
  signals; occupation, reservation and claims per section and direction;
  a train clears signals ahead of it (`SignalNumClearAhead` of the first
  signal ahead), a signal clears when its route is reserved; Auto Node
  clears 5,000 m, or 2 minutes at the train's top speed if further, where
  there is no signal; deadlock traps on single track; passing paths;
  restrictive aspects slow AI trains; the aspects' speeds apply. Without a
  script, a head shows its least restrictive aspect when its block is
  clear, else its most restrictive. Failed signals in an activity are read
  but not used.
- **Local content** (2026-10-15): scripts are few and small (USA1 1,
  USA2 5, EUROPE1 2, JAPAN1 2); USA1's whole script: STOP when not
  enabled, block not clear or route not set; otherwise approach 1, 2, 3 or
  clear by the next signal; the default draw state. `SignalNumClearAhead`
  is one value per type everywhere (EUROPE 1, JAPAN 2 and 3, USA1 5, USA2
  3 and 4); BNSF_SCENIC has an `OpenRails/sigcfg.dat` (values 1 and 5).
  Of 223 local paths only BNSF_SCENIC's (12 of 17) have passing paths.

## Design

### 1. The track in the input

The input gains the track's topology, derived from the TDB when a
simulation starts and immutable after (as the geometry):
- **Sections:** intervals of vector nodes (node, from, to), split at every
  NORMAL head (by its type's function, in either direction), at the
  node's ends, and where a diamond crossing lies on it; each **junction**
  is a section of its own (its pins, its main route); each **diamond** is
  one resource the two crossing intervals share. Sections are numbered in
  the input and keep their node and distances, so the editor can show
  them.
- **Signal heads:** TDB item id, section boundary (node, distance), the
  direction they face, type (function, aspects with speeds, draw states,
  clear-ahead values), feature flags, junction links. **Signals** group
  heads at the same place: same node, within 0.1 m, facing the same way
  (MSTS's own tolerance; two signal objects placed at one spot would count
  as one, which nobody does).
- **Speed posts:** node, distance, the direction they face, limit or
  resume, passenger and freight, the speed.
- **Static consists:** the activity's loose consists, placed as the editor
  places them, occupying their sections for the whole run.
- **Speed zones:** the activity's restricted speed zones.
- Each train's route is **projected** onto sections: for each part, the
  sections in order with the direction it passes them and the route
  distance where it enters each; the signal heads facing it; the speed
  posts facing it.

### 2. Runtime state

Owned by the core, per section: the trains on it (a list, though one at a
time is the rule here) and the direction they pass it; the train it is
reserved for; per junction: its switch position and the train that locks
it; per signal head: cleared for which train, its aspect and draw state;
per single-track stretch: the train that claims it. Switches start in
their main position.

### 3. Authority

A train may move up to the end of its authority, the nearest of:
- **a signal not cleared** for it (it stops short of it; Open Rails' AI
  keeps 30 m to the stopping point, to be checked against an Open Rails
  run);
- **the end of the reserved track** where there is no signal (Auto Node):
  short of the first section it could not reserve;
- the targets of task 01 (stops, waits, reversals, the path's end).

The driver brakes for it as for a stop. Requests:
- **Signals:** a train approaching a NORMAL signal asks the signals ahead
  of it to clear: as many as the first signal ahead's
  `SignalNumClearAhead` says. In Open Rails' legacy mode (a single value,
  `sigcfg.dat` in the route's root) that counts NORMAL heads, those of the
  first signal included; in Open Rails mode (`OpenRails/sigcfg.dat`, or
  the first of two values) it counts signals, 0 meaning nothing beyond
  this one until the train passes it and -1 a signal not counted. A signal
  clears for the train when the sections from it to the next NORMAL
  signal on the train's route are free, the junctions there can be set
  its way, and no single-track claim stands against it: it reserves them,
  sets and locks the switches, and is cleared. Signals clear in order:
  one that cannot clear stops the count.
- **No signal ahead:** the train reserves along its route, section by
  section, up to 5,000 m or 2 minutes at its top speed, whichever is
  further, or to the next NORMAL signal facing it.
- **Release:** a section, its reservation and its junction's lock are
  freed when the train's rear leaves it; a head passed is no longer
  cleared. A train that stops for good (an AI train at its path's end) or
  reverses releases what lies beyond its new direction.
- **Start:** a train starts when its whole length's sections are free (it
  occupies them); otherwise it waits and the editor says why.

Requests are taken in a fixed order (train id) each step, and a grant is
all or nothing, so a run is repeatable: the same input gives the same
events. A train waiting on a resource is woken when that resource frees;
it does not rescan its route each step.

### 4. Single track and deadlocks

Two trains whose routes use the same sections in opposite directions meet
on a **single-track stretch**: each maximal run of shared sections between
the places where their routes part. A loop where both routes use the same
track is no passing place for them, so it lies inside the stretch. The
stretches are found per pair of trains when the input is made.

A train about to reserve into such a stretch, while the opposing train
still has it ahead on its route, **claims the whole stretch**; the
opposing train cannot reserve into it until the claimant's rear has left
it, so it waits where their routes part. This keeps trains out of
nose-to-nose meetings at some cost in capacity (Decision 1).

Deadlocks can remain (a waiting train longer than its loop, three trains
in a ring). The core keeps a **wait-for graph** (which train each waiting
train waits for) and reports a cycle at once: the trains, where they
stand, what each waits for. The editor tells the user (the activity
panel's status and a message, the Timetable window's states), who can
change the schedules or paths; the stuck trains stay stuck. Trains that
never finish are reported at the end too.

### 5. Passing paths (Open Rails' default rule)

A path's passing paths are alternative branches beside its main path.
Each becomes an alternative in the train's route between its start and end
nodes. When the train sets its route to the start of a passing path and
an opposing train claims the single track beyond its end, the train takes
the passing path if it is free (and long enough for it), leaving the main
path to the other; otherwise it waits short of the start. Where only one
of two meeting trains has a passing path there, that train always takes
it. Both trains' main paths must be the same through the area. Stops on
the main path keep their places (Open Rails' location-linked option moves
them to a platform on the passing track; not in this task).

### 6. Aspects without scripts

Each head's aspect comes from a built-in rule by its function, the rule of
the stock scripts:
- **NORMAL:** STOP when it is not cleared, the track to the next NORMAL
  signal is not clear, or its junction link is not set (`route_set`);
  otherwise from the next NORMAL head ahead (along the switches as set):
  next STOP, STOP_AND_PROCEED or RESTRICTING gives APPROACH_1, next
  APPROACH_1 gives APPROACH_2, next APPROACH_2 gives APPROACH_3, otherwise
  CLEAR_2; each limited to the aspects the type defines (the nearest more
  restrictive one it has).
- **DISTANT:** from the next NORMAL head: STOP gives its most restrictive
  aspect, else the clear progression as above. **REPEATER:** the next
  NORMAL head's aspect.
- **Others** (SHUNTING, INFO, SPEED, Open Rails functions): their default
  draw state.

The draw state is the aspect's draw state in the type. Each head notes
that the built-in rule chose it, so task 03 can report where a route's
script would decide differently. A head's aspect is evaluated again when
its reservation, its track, its junction or the next head's aspect
changes (a change runs back along the line, bounded).

Authority comes from reservations; aspects add speed: a train passing a
head takes its aspect's speed (from `sigcfg.dat`) until the next signal.
STOP_AND_PROCEED and RESTRICTING do not occur from the built-in rule
except as a type's only non-stop aspects.

### 7. Speeds

The driver's targets become constraints with a speed: stops (speed 0)
and speed reductions. Along each route part, the limit is the lowest of
the route's limit, the train's top speed × efficiency, the speed posts
facing it (passenger or freight limits by the train's type; a resume post
ends a limit; warning posts are signs only), the activity's speed zones,
and the speed of the last signal passed. The driver brakes to reach a
lower limit where it begins and speeds up again when the whole train has
passed the end of it. This also corrects the early arrivals measured in
task 01 (no speed posts).

### 8. In the editor

- **3D:** each head's lights in its simulated draw state (the frame
  carries draw states by TDB item; the world's signal objects find
  theirs).
- **Map:** occupied sections in red, reserved ones in the reserving
  train's band colour; signal markers with their aspects.
- **Timetable window:** why a train waits: "at signal 1234: track
  occupied by train X", "for the single track to Y: train Z", "to start:
  its track is occupied"; events: stopped at a signal, passed it,
  deadlock.
- **Activity panel:** a deadlock in the status line, with a message
  naming the trains; the run goes on.
- **Measured:** route size, active trains and contention at a busy
  junction, as the analysis proposes; section visits and reevaluations per
  simulated hour.

### 9. Later

Task 03: `sigscr.dat` read and run (the 14 native functions, `mr`/`lr`
aggregation, `sig_feature`, grouping on one signal), replacing the
built-in rule where a route has a script. Then the Open Rails additions
that need world data, stored in the TDB as `tdb-signal-extension.md`
proposes; approach control; location-linked passing paths; curves and
gradients; the player's train driven by the user.

## Phases

1. **Input:** sections, junctions, diamonds, heads and signals, speed
   posts, static consists, routes projected onto sections; `SigCfg` reads
   `OpenRails/sigcfg.dat` first. Tests on hand-made track and every local
   route (every head on a section boundary, every route projected, the
   sections covering each node once).
2. **Occupation and Auto Node:** occupation by whole trains, reservation
   and release, authority on track without signals, starts on free track,
   static consists; following trains, crossing routes at a junction.
3. **Signals:** clearing ahead (legacy and Open Rails modes), stopping at
   signals, junctions set and locked, `route_set`.
4. **Aspects and speeds:** the built-in aspect rule and draw states,
   aspect speeds, speed posts, speed zones, the driver's speed
   constraints.
5. **Single track:** stretches per pair of trains, claims, the wait-for
   graph and deadlock reports.
6. **Passing paths:** Open Rails' default rule.
7. **Editor and measures:** lights, map, Timetable reasons, deadlock
   message (parts of this come with the phases above); the three scaling
   workloads; the local activities run to their ends, waits and deadlocks
   listed; selected activities compared with Open Rails runs.

## Verification

- **Fixtures** (hand-made track): a following train waits for the rear of
  the one ahead and goes when it clears; two routes over one junction take
  turns, disjoint routes do not; a train starting on occupied track waits;
  a static consist's track is never entered; clearing ahead counts heads
  and signals (with 0 and -1); a head linked to a junction set the other
  way stays at STOP; two opposing trains on a single line with a loop
  pass when their routes part at the loop and are reported when both use
  its main track; a passing path is taken by the train that has it; a
  three-train ring is reported as a deadlock; the aspect progression on a
  line of signals; a speed post's limit reached at the post and left when
  the rear has passed; a large step never skips a stop at a signal; the
  same input gives the same events.
- **Content:** every local activity run to its end: trains finished, waits
  and their reasons, deadlocks; arrivals against calculated timetables
  before and after speed posts.
- **Open Rails:** selected activities (one per local route, BNSF_SCENIC
  with passing paths) run in the Linux fork; waits at signals and arrival
  times compared, differences explained.

## Questions for the MSTS analysis

- How MSTS decides how many signals to clear ahead: the route-wide maximum
  of `SignalNumClearAhead` and the further calculations the Open Rails
  manual calls "complex and strange" (around the signal dependency pass
  at `0x005c1715` and the head ownership in `0x005c4ecf`).
- What an MSTS AI train does at a failed signal (Open Rails reads failed
  signals but does not use them).
- Where an MSTS AI train stops short of a signal at danger.

## Decisions (user, 2026-10-15)

1. **All but scripts:** the TDB's and `sigcfg.dat`'s signal data are used;
   `sigscr.dat` waits for task 03. Open Rails additions that need world
   files wait: the simulation does not read the W files; the editor can
   store what they need in the TDB (`tdb-signal-extension.md`).
2. **Single track:** a train claims the whole single-track stretch; a
   deadlock is reported to the user, who fixes the schedules (we are in
   the editor). Better arbitration later.
3. **Passing paths:** Open Rails' default (path-based) rule, the last
   phase of this task.
4. **Clearing ahead:** Open Rails' legacy mode first (heads counted), then
   Open Rails mode (signals counted, heads at one place grouped as one
   signal). MSTS's own rule: asked of the MSTS analysis.
5. **Speed posts and speed zones** are in this task.
6. **The player's train** obeys signals as an AI train does.
