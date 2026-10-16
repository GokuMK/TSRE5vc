# Task 02 - Signals and Track Authority (No Scripts)

Status: done (phases 1 to 7, 2026-10-16, branch `feature/simulation`),
compared with Open Rails on every local route. Decisions taken are at the
end.

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

## Progress

**Phase 1, the track in the input (done 2026-10-16).** `TrackNetwork`
(`src/tsre/sim`), made with the input from the TDB and `sigcfg.dat`:
- signal types with their function (MSTS's `DISTANCE` is distant; Open
  Rails' functions by the MSTS function they declare), aspects with their
  draw states and speeds, clearing ahead (heads, or signals in Open Rails
  mode);
- heads: place, the way they face (TDB direction 0: trains going the
  node's way), type, feature flags, junction link (node, output pin);
  signals: heads within 0.1 m facing one way;
- speed posts (limits, warnings, resumes; passenger, freight; m/s), the
  way they face from their angle as Open Rails takes it (a post at the
  track's angle less a quarter turn faces trains going against the node:
  warnings come before their limits that way on USA1 45 to 3, USA2 47 to
  4, BNSF_SCENIC 10 to 1);
- sections: each vector node cut at its NORMAL heads (either way);
  junctions, each a section linked to the sections at its pins, its main
  route known; diamond crossings, each one point resource on its two
  tracks (they do not cut the tracks);
- each train's route parts projected: sections in order with junction
  pins, the heads and speed posts facing it, route distances; a train
  knows whether it is a passenger train;
- the activity's loose consists on the track (as the editor places them);
- `sigcfg.dat` from the route's `OpenRails` folder when there is one.

Tests: `simulation-core` (a passing loop with signals both ways, a
distant signal, speed posts, a crossing: sections, links, grouping,
facing, a route projected both ways), `simulation-route` (every node
covered once, every NORMAL head on a section boundary, every junction
link ahead of its head, speed posts along the track and facing their way,
every route part projected end to end, every loose consist placed). The
network takes 1 to 4 ms and 69 to 239 KiB on the local routes.

**Phase 2, occupation and Auto Node (done 2026-10-16).** In the core:
- every section has one holder (a train, a standing consist, or none); a
  train holds the sections under its whole length and those reserved
  ahead of it, takes them a track section at a time (with the junctions
  before it and the crossings on it), all or nothing, and gives each back
  when its rear has left it; an AI train at its path's end gives back all
  (the player's keeps standing there);
- authority as Open Rails' Auto Node: up to 5,000 m ahead, or two minutes
  at its top speed if further, as far as it can reserve; the driver brakes
  to stop 10 m short of its end and goes on when it is extended;
- a train starts when the track under it is free, else waits; loose
  consists hold their track for the whole run;
- at a reversal the train gives back its track and takes again what it
  stands on;
- the pre-run runs all trains together from the first one's start;
- events: a train waits for track (and for whom), a deadlock (a ring of
  trains each waiting for the next), each reported once; the Timetable
  window's states ("waiting for track held by …", "deadlocked: waiting for
  …", "to start: its track is held by …") and the panel's status line
  name them.

Tests (`simulation-core`): a following train starts when the first has
left its section and never reaches its rear, in 0.1 s and 5 s steps, the
same events each time; opposing trains on a single line longer than they
look ahead deadlock and both are reported (on a short line the first one
reserves it all and the other waits to start); a train on a loose
consist's track waits; two routes into one junction take turns while a
route clear of them runs on. `simulation-route` counts trains finished and
stuck: EUROPE1, EUROPE2 and JAPAN2 all finish; JAPAN1 49 of 50 (one AI
train's path runs through a loose consist); single-track USA1 (41 of 46),
USA2 (39 of 48) and BNSF_SCENIC (5 of 16) deadlock without signals and
stretch claims (phases 3 and 5). `simulation-ui`'s AI train has its own
line now.

**Phase 3, signals (done 2026-10-16).**
- Each projected route part lists the stop signals facing it (signals
  with NORMAL heads): where each stands, its NORMAL heads, its type, the
  first section of its block.
- Past the first stop signal ahead a train reserves block by block: a
  signal is cleared for it when its whole block (to the next stop signal
  on the route, or the part's end) is free and taken at once; it clears as
  many as the first signal ahead's type says: NORMAL heads, the first
  signal's own included (Open Rails' legacy mode), or signals (Open Rails
  mode), where a type of 0 clears none beyond it and one of -1 is not
  counted. Short of the first signal (or where there is none) it reserves
  as Auto Node does.
- A train stops 30 m short of a stop signal not cleared for it (10 m short
  of other ends of authority); a passed signal is no longer cleared.
- Junctions: a train taking a junction sets it its way (the output it
  leaves by, or the one it comes in by trailing) and holds it; switches
  start in their main position and stay where the last train left them.

Tests (`simulation-core`): clearing two ahead counts a signal's two heads
in legacy mode and two signals in Open Rails mode; 0 and -1; a train
behind one waiting in the next block stands 30 m short of the red signal
and goes on when the block is clear; a junction set and held for the
loop's side track, then set back for a train from the main track. The
local activities run with signals: EUROPE1, EUROPE2, JAPAN2 finish,
JAPAN1 as before; single-track USA1, USA2 and BNSF_SCENIC still deadlock
(phase 5).

**Phase 4, aspects and speeds (done 2026-10-16).**
- Every head's aspect and draw state from the built-in rule, evaluated
  again after a step that changed reservations, switches or cleared
  signals: NORMAL heads at stop unless cleared and their junction link set
  (`route_set`), else the caution/clear progression from the next stop
  signal along the switches as set (its least restrictive head); distant
  heads at their most restrictive before a stop signal at stop, else the
  progression; repeaters as the next stop signal; others their default
  draw state. A type's aspect for the one wanted: its most restrictive for
  stop, else its least restrictive proceed aspect no less restrictive than
  wanted (a two-aspect home signal clears).
- Speeds: each route part has its limits (speed posts facing it for its
  kind of train, from a limit post to the next limit or resume post; the
  activity's speed zones at the route's TempRestrictedSpeed, 25 km/h
  without one, both ways); a train keeps a limit until its rear has left
  it and brakes on the exact curve to be down to a lower one where it
  begins; passing a stop signal it takes its aspect's speed until the next
  one, and brakes for the next signal's aspect speed.

Tests (`simulation-core`): cleared signals at caution before one at stop,
a distant before it at caution; a train passing a signal at caution at
its speed; a speed post's limit met at the post, kept until the rear is
past the resume post; a speed zone over a limit; route heads at a
junction (the set route's proceeds, the other stops). On the local routes
arrivals against MSAE's calculated timetables: JAPAN1 median 5 s early,
EUROPE2 58 s early, EUROPE1 142 s early (few posts); USA1's 15 timed
stops 326 s late (its approach aspects are 30 and 45 mph and it has 377
speed posts, which MSAE's calculation may leave out; the Open Rails
comparison in phase 7 will tell). USA1's trains all finish now; USA2 and
BNSF_SCENIC still deadlock.

**Phase 5, single track (done 2026-10-16).**
- When a simulation starts, each pair of trains' route parts are compared:
  every maximal run of track sections they pass in opposite directions
  (with the junctions and crossings both pass) is a single-track stretch.
- A train going on into a stretch (or standing in one at its start)
  claims it, unless the other train is in it or has claimed it: then it
  waits where their routes part. The claim lapses when its rear has left
  the stretch; a stretch the other train has passed (or the other train
  gone) is no matter. A train entering several at once claims all or
  none.
- A train reserves a track section with the junctions before it and the
  crossings on it; the junction at its far end goes with the next one (it
  took that junction too, so a train could not reach a loop's track while
  the single line beyond was claimed, and stood in the stretch behind).
- Deadlocks that remain are found as before (rings of waiting trains).

Tests (`simulation-core`): opposing trains on a single line between two
loops, one through the loops' main tracks and one through their others:
three stretches, one train waits at a loop, both finish; both through the
main tracks: one stretch end to end, a deadlock for both. On the local
routes no train is in a deadlock any more except two in BNSF_SCENIC's
CanadianImports (its paths have passing paths: phase 6); the trains still
stuck wait behind loose consists on their paths (USA2's Cutbank_Grain is a
player pick-up: no coupling yet), behind the player's train standing at
its path's end, or behind those.

**Phase 6, passing paths (done 2026-10-16).**
- The input carries each path's passing paths (`Path::passingPaths`): the
  part they lie on, the route distances where they leave and rejoin the
  main path, and their own track. Each train runs on its own copy of its
  route parts, projections, limits, stops and waits.
- Open Rails' path-based rule: reserving the unit that holds the junction
  where a passing path leaves, the train takes the passing path when the
  main route through the loop is claimed for an opposing train or held (a
  train, a standing consist: Open Rails takes it when the main block is
  not reservable), and the passing path is free; when it is not, the
  train waits short of the loop. The train's route part is spliced; its
  stops, waits and later passing paths on that part move by the
  difference in length (those along the loop to the same share of the
  passing track); its speed limits are made again from the speed posts
  (the activity's speed zones on that part are dropped); its single-track
  stretches are found again, keeping the claims on the same track.
- Stretches: where two trains' routes part between two runs of shared
  track is a place to pass only when one of them fits there clear of both
  runs (its length and the 30 m signal margin) and no standing consist is
  on either one's track there; otherwise the runs are one stretch. This
  was the last deadlock on the local routes (CanadianImports' 1.2 and
  1.3 km trains met at a loop neither fits) and what made
  5_wb_empirebuilder's intermodal wait for the player in a loop whose
  other track a loose consist holds.
- Starting: the trains due in a step take their places first, all of them
  before any reserves ahead (trains the activity puts on the track at the
  same time are there together; on one stretch they deadlock, as before);
  a train whose start lies in a stretch claimed earlier for another train
  (or with the other in it) waits to start.
- The player's train takes its passing paths as an AI train would (it is
  driven as one). Not done: the train's length against the passing track
  (Open Rails' path-based rule does not check it either; where it does not
  fit, the stretches join across it).

Tests (`simulation-core`): the westbound train with passing paths takes
the east loop's other track and both finish; 300 m trains on a line
between a short loop and a long one: two stretches (three for 40 m trains),
they pass at the long loop; a consist on the long loop's main track: the
eastbound train takes its passing path round it and both finish. Local
routes: no deadlocks. BNSF_SCENIC 15 of 16 trains finish (8 before), the
other waits behind the player's train at its path's end; EUROPE1 48/48,
EUROPE2 21/21, JAPAN1 49/50, JAPAN2 16/16, USA1 46/46, USA2 46/48 (the
rest behind loose consists).

**Phase 7, the editor and measures (done 2026-10-16).**
- 3D: each frame carries the heads' aspects and draw states (shared with
  the frame before until they change); the signals' lights
  (`SignalLights`) ask the simulation for each head's draw state by its
  TDB item, by name (the simulation may read `OpenRails/sigcfg.dat`, the
  lights the route's), and show its lights; without a simulation, the
  default. Flashing lights are drawn steady; semaphore arms do not move.
- Map: the track a train (or a consist) stands on in red, whole sections;
  track reserved ahead in amber; each signal as an arrowhead pointing the
  way the trains it faces go, red at stop, yellow at caution, green clear
  (the palette's `occupied`, `reserved`, `aspectStop`, `aspectCaution`,
  `aspectClear`). The frame shares the sections' use until a holder
  changes or a train's front passes into the next section.
- Timetable window: why a train waits ("at signal 597: the track beyond
  is held by ...", "the single track beyond is claimed by ...", "the
  passing track is held by ...", "to start: the single track is claimed
  by ..."); its Blocked events say the same.
- Activity panel: a deadlock opens a message (not modal) naming the
  trains, to change their times or paths; the status line names them
  too; the run goes on.

Tests: `simulation-core` (the frame's draw states by name; a station
stop's exit signal held until two minutes before the train leaves, the
signal before it at caution, the train leaving on time; three trains in a
ring reported as a deadlock), `simulation-ui`
(a deadlock at a junction: the message, the status line and each train's
state name the trains; Stop closes the message); capture case
`tests/renderer/simulation-signals.json` (JAPAN1's shrtpass at 09:03: a
head at caution ahead of the AI train shows yellow, one ahead of the
player's train green, both red without a simulation; the two maps).

**The scaling workloads** (`simulation-scaling`, hand-made track: 100 m
pieces, home signals clearing two ahead; llvmpipe box). Measured first,
three costs grew where they should not: every aspect change evaluated
every head (a whole-route scan), the single-track stretches were sought
for every pair of trains, and a train waiting for track looked at it
again every 0.1 s step. Now:
- aspects are evaluated again only where something changed: the heads of
  signals cleared or given back, of signals whose next signal is found
  through a switch that was set, of heads linked to that switch, and,
  when a head's aspect changes, of the signals reading it; each signal's
  next signal is kept until a switch on its way changes;
- stretches are sought only for trains' parts that pass a section in
  common;
- a train waiting for track tries again when the section it waits for
  changes holder (any section, for the single track), when a claim
  changes, and at least every 5 s.

The local routes give the same results as before each change (trains
finished, waits, stops, arrivals).

| Workload | Trains | Sections, heads | Track built | Input | Wall time per simulated hour | Slowest simulated second | Per simulated hour | A frame |
|---|---|---|---|---|---|---|---|---|
| Double track, 20 km | 8 | 40, 38 | 0.3 ms | 0.2 ms | 149 ms | 0.11 ms | 800 head visits, 205 section checks | 23 µs |
| + 100 unused 10 km lines | 8 | 1,040, 1,838 | 5 ms | 1.0 ms | 150 ms | 0.10 ms | the same | 57 µs |
| + 1,000 unused lines | 8 | 10,040, 18,038 | 55 ms, 3.2 MiB | 7.9 ms | 170 ms | 0.38 ms | the same | 340 µs |
| 10 double tracks | 80 | 400, 380 | 1.1 ms | 1.5 ms | 1,328 ms | 1.06 ms | 8,003 head visits | 176 µs |
| 100 double tracks | 800 | 4,000, 3,800 | 12 ms | 15 ms | 13,786 ms | 11.9 ms | 80,030 head visits | 1.8 ms |
| 2 branches onto a trunk | 8 | 17, 13 | 0.1 ms | 0.2 ms | 164 ms | 0.08 ms | 157,040 section checks, 21 waits | 21 µs |
| 4 branches | 16 | 27, 17 | 0.1 ms | 1.0 ms | 286 ms | 0.15 ms | 261,264 section checks, 59 waits | 30 µs |
| 8 branches | 32 | 47, 25 | 0.1 ms | 5.1 ms | 493 ms | 0.40 ms | 371,051 section checks, 183 waits | 48 µs |
| 16 branches | 64 | 87, 41 | 0.1 ms | 31 ms | 851 ms | 0.79 ms | 467,440 section checks, 623 waits | 75 µs |

Before the changes: 1,000 unused lines took 756 ms per simulated hour
(4.95 million head visits), 800 trains' input 2.2 s, 16 branches 5.2
million section checks. The work that grows with the route alone is now
the track's build and a frame: a frame copies the heads' aspects and the
sections' use whenever one changed (about 20 ns a head or section), a
third of a millisecond on a route of 10,000 sections; frames sharing
unchanged blocks of them would remove it if country-scale routes need it.
Every running train still asks for authority each 0.1 s step (cheap: it
stops at once where its reservation already reaches its lookahead), so
wall time follows the trains on the track, about 17 ms per train and
simulated hour here.

**Compared with Open Rails** (`scripts/orts-simulation-compare.sh`,
`scripts/orts-dispatcher-stops.py`): the activity runs in the Linux fork on
a display of its own (with a window manager: the game takes keys only in a
focused window, held across its frames), the player's train on autopilot,
the game sped up; every second or so Open Rails' time and its dispatcher
page (each train's state, speed, limit and next signal) are read from its
web server, and each train's stops and stands are found in those samples.
Open Rails has no mode without drawing, and a frame over 250 ms simulates
nothing; with less drawn (no vertical sync, distant mountains, wire or
extended LOD, scenery of detail level 0, no sound) its frames on the
llvmpipe box take 25 to 45 ms against 55 to 80, and CanadianImports runs
at the 17 times real time asked (an hour of the activity in under four
minutes), its stops and waits within a sample of a run at 5 times; at 38
times they come up to a minute later.
Open Rails numbers its AI trains in traffic order after the loose
consists, so Traffic k is train k + 6 on USA1.
- **USA1 smmrex, 12:00 to 12:40.** Open Rails' Summer Express trains
  slowed for miles at 45 and 30 mph behind approach aspects that led to
  the exit signal of the station they were to stop at: Open Rails holds a
  station stop's exit signal at danger until two minutes before the train
  leaves (forced red at station stops, its default). Our core did not, so
  our trains reached their stops 12 to 15 minutes earlier; with the hold
  (section 3) they reach them 1.5 to 4.5 minutes earlier (Traffic 02's
  first stop 12:34:36 against 12:36:05, Traffic 04's 12:23:04 against
  12:27:29), and Traffic 01, running since 10:30, 8 to 10 minutes earlier
  (about 9 % of its run). What is left is driving: Open Rails' AI drives
  with its physics and holds the aspects' speeds until the next signal as
  we do. Both skip the stop at Baltimore 3, which is not on the trains'
  paths. On the local routes the hold makes USA1's timed stops 501 s late
  on the median against MSAE's timetables (326 s before); the other
  routes change by a few seconds.
- **BNSF_SCENIC 3_CanadianImports, 06:00 to 07:30.** Our player's train
  and the eastbound intermodal waited at the start of the single line
  from 06:12 until 10:43, while one westbound train after another claimed
  it: the line was one stretch end to end, its loops inside it, though the
  trains' paths have passing paths round them. Open Rails let them on,
  and they met the westbound trains at those loops. A loop where one of
  two trains has a passing path it fits on (and the other does not use its
  track) now ends the single track for them (section 4), so they meet
  there: the intermodal waits from 06:26 at 19.6 mi (Open Rails 06:28 at
  18.4 mi, 11.5 minutes), the player behind it from 06:30 at 15.4 mi (Open
  Rails 06:33 at 13.8 mi), the westbound intermodal from 06:47 (Open
  Rails 06:42). BNSF_SCENIC's sixteen trains all finish now, in 19.2
  simulated hours (39.9 before); its two timed stops are 179 s early and
  179 s late.
- **The other routes** (`scripts/orts-compare-stops.py` pairs each Open
  Rails train's stops and stands with the same train's in TSRE by the
  distance travelled, from TSRE's events, `TSRE_SIM_EVENTS`; Open Rails
  numbers its trains: the player's 0, loose consists from 1, then the AI
  trains in traffic order). Each route's itr_e1_s1_w1_t1 from 12:00
  (EUROPE1's aftstorm from 15:00), at 17 times real time, TSRE minus Open
  Rails at the stops paired:

  | Route | Stops paired | Median | Range |
  |---|---|---|---|
  | EUROPE1 | 56 | -396 s | -898 to -60 s |
  | EUROPE2 | 51 | -200 s | -1309 to +261 s |
  | JAPAN1 | 249 | -1154 s | -2033 to -47 s |
  | JAPAN2 | 16 | -24 s | -39 to +264 s |
  | USA2 | 6 | -500 s | -588 to -367 s |

  USA2's first run had our trains 10 to 24 minutes late: its BNSF
  three-light signals have no APPROACH_3, so behind a red every signal
  showed APPROACH_2 (40 mph) mile after mile; Open Rails' script clears
  them. The built-in rule now does too (section 6); USA2's two timed stops
  went from an hour late to 31 minutes.

  What is left is driving. Open Rails' AI drives through its locomotive's
  physics, raising the throttle in steps: its passenger trains accelerate
  at about 0.4 m/s² (EUROPE2's 17, 47, 77, 95 km/h at 18 s intervals) and
  brake at 0.2 to 0.5, where ours take 1.0 m/s² both ways (Open Rails' AI
  targets, which only its pre-run reaches, scaled by the efficiency). Ours
  gain about 30 s on each run between stops (JAPAN1: 79 s against 109 s on
  the median) and stand a little shorter, so they pull ahead where stops
  are close: JAPAN1's Odakyu stoppers by 10 to 30 minutes over an hour or
  two, and trains started long before the activity (the pre-run) furthest.
  On JAPAN2's long mountain runs the player stays within 25 s of Open
  Rails at nearly every stop for two hours, and EUROPE1's player within 60
  to 96 s at most of its ten (its AI stoppers gain 70 s a stop). Open Rails' trains run behind
  these activities' calculated timetables, ours near them (JAPAN1 median 4
  s early). Matching Open Rails needs acceleration limited by the
  locomotive (its force and power, the train's mass and resistance) and
  its gentler braking: task 01 left physics out; task 03 does it.

  BNSF_SCENIC's CanadianImports meets are the same with both; ours wait
  about seven minutes longer at the first (the opposing westbound,
  running since 03:18, comes five minutes later).

## Objective

Trains wait for each other: the track they occupy and the track reserved
for them are theirs alone, signals give and withhold authority, junctions
are set for the train that has them, and speeds follow the line's limits.
Everything the TDB and `sigcfg.dat` say about signals is used; signal
scripts (`sigscr.dat`) are not (task 05). Where trains cannot pass each
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

Not in scope: `sigscr.dat` and other scripts (task 05); Open Rails
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
  NORMAL head (by its type's function, in either direction) and at the
  node's ends; each **junction** is a section of its own (its pins, its
  main route); each **diamond** is one point resource on the two crossing
  tracks. Sections are numbered in
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
- **Station stops (Open Rails' forced red, its default):** a stop's exit
  signal, the first stop signal beyond the platform within 150 m (further
  on the platform's node when its block holds a facing junction), is not
  cleared for the train until it stands at the stop with its departure
  two minutes off; a stop too near it moves back to 31 m short of it.

Requests are taken in a fixed order (train id) each step, and a grant is
all or nothing, so a run is repeatable: the same input gives the same
events. A train waiting on a resource is woken when that resource frees;
it does not rescan its route each step.

### 4. Single track and deadlocks

Two trains whose routes use the same sections in opposite directions meet
on a **single-track stretch**: each maximal run of shared sections between
the places where their routes part. A loop where both routes use the same
track is no passing place for them, so it lies inside the stretch, unless
one of them has a passing path round it that it fits on and the other
does not use (Open Rails' path-based rule: the trains meet there). A place
where they part that neither train fits in, or where a standing consist is
on one's track, lies inside the stretch too. The stretches are found per pair of trains when
the input is made, and again for a train that takes a passing path.

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
nodes, and its loop a place where single track ends (section 4). When the
train sets its route to the start of a passing path and the main route
through to its end, or the single track beyond, is claimed for an opposing
train or held (a train, a standing consist), the train takes the passing
path if it is free, leaving the main path to the other; otherwise it waits
short of the start. Stops along the loop's main track move to the same share of
the passing track (Open Rails' location-linked option moves them to a
platform on the passing track; not in this task).

### 6. Aspects without scripts

Each head's aspect comes from a built-in rule by its function, the rule of
the stock scripts:
- **NORMAL:** STOP when it is not cleared, the track to the next NORMAL
  signal is not clear, or its junction link is not set (`route_set`);
  otherwise from the next NORMAL head ahead (along the switches as set):
  next STOP, STOP_AND_PROCEED or RESTRICTING gives APPROACH_1, next
  APPROACH_1 gives APPROACH_2, next APPROACH_2 gives APPROACH_3, otherwise
  CLEAR_2; each limited to the aspects the type defines (the nearest more
  restrictive one it has), except that a caution beyond the type's last
  caution is its clearest aspect: a three-aspect signal before one at
  APPROACH_1 (or a BNSF three-light before one at APPROACH_2) clears, as
  their scripts have it, and cautions do not repeat signal after signal.
- **DISTANT:** from the next NORMAL head: STOP gives its most restrictive
  aspect, else the clear progression as above. **REPEATER:** the next
  NORMAL head's aspect.
- **Others** (SHUNTING, INFO, SPEED, Open Rails functions): their default
  draw state.

The draw state is the aspect's draw state in the type. Each head notes
that the built-in rule chose it, so task 05 can report where a route's
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

Task 05: `sigscr.dat` read and run (the 14 native functions, `mr`/`lr`
aggregation, `sig_feature`, grouping on one signal), replacing the
built-in rule where a route has a script. Then the Open Rails additions
that need world data, stored in the TDB as `tdb-signal-extension.md`
proposes; approach control; location-linked passing paths; curves and
gradients; the player's train driven by the user (acceleration and
braking from the locomotive and the train, the Open Rails comparisons'
largest difference: task 03). Single-track
arbitration beyond first come, first served (decision 2): a train
waiting at a single line without passing paths can still be passed over
by opposing trains that claim it first; Open Rails keeps a loop track
open for a train awaiting a single line. Frames that share unchanged
blocks of aspects and section use, if country-scale routes need it.

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
   `sigscr.dat` waits for task 05. Open Rails additions that need world
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
