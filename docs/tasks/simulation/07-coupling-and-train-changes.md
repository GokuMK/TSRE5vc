# Task 07 - Coupling and Train Changes

Status: designed 2026-10-21; phases 1, 2a, 2b and 3 (part A) done
(branch `feature/simulation`).
Decisions taken are at the end.

Sources:
- tasks 01 to 06 (the simulation core, signals, driving, Simulate, signal
  scripts, activity events: wagon events checked on fixed car lists, car
  ids, the start placement of 2026-10-21);
- the roadmap's notes for this task (2026-10-17) and the user's decision
  of 2026-10-20: the player's work orders are done by TSRE's own full
  autopilot, so an activity can be checked end to end;
- Open Rails (a behavioural reference, no code taken), checked in
  `openrails-linux-spike`: `AITrain.cs` (following a train ahead and when
  to attach, `CheckCouplePosition`, `CoupleAI`, `CoupleAIToStatic`,
  `StealCarsToLivingTrain`, `LeaveCarsToLivingTrain`, `TerminateCoupling`,
  `TestUncouple`, `UncoupleSomeWagons`, `TestUncondAttach`,
  `ProcessEndOfPathReached`; its distances and speeds), `Simulator.cs`
  (`UncoupleBehind`, static consists), `AIAuxAction.cs` (waiting points);
- the local content: the 23 activities with wagon events, their player
  paths, consists and loose consists (the route suite's analysis below).

## Progress

**Phase 1, A1 and A2 (done 2026-10-21).**
- Each vehicle in the input has its share of its train's performance
  (mass, an engine's force, power and top speed, its Davis coefficients,
  the leading one's air drag apart); `PerformanceOf`, `RatesOf`,
  `DrivingOf` work a train out from its vehicles.
- Each train in the core has its own vehicles, length, performance, kind,
  rates and driving (`Runtime`), as its plan has them until they change
  (`refreshConsist`); everything the core read from the plan reads them.
- Loose consists carry their vehicles and are the core's standing trains
  (`Standing`; per section the standing train holding it); task 06's
  wagon checks read the trains' and standing trains' lists.
- Every suite unchanged; on the seven routes the trains run exactly as
  before (the same arrivals).

**Phase 2a, A3 to A6 with standing trains (done 2026-10-21).**
- **Attaching:** a train whose authority reaches a section a standing
  train stands in attaches to it (Open Rails: a static train always; its
  own cut-off cars only once it has been more than 5 m away from them).
  Its authority then runs into that section up to the contact; it creeps
  (2.5 m/s) within 100 m and comes in at 0.4 m/s within 8 m (the stop
  kind `Couple`, "coupling").
- **Coupling:** the standing train's cars join at the leading end (turned
  where the ends meet the other way), the path part extended over them
  where it ends inside them. An AI train on its last part with fewer than
  5 sections left joins the standing train instead and is gone (Open
  Rails' `CoupleAIToStatic` and `CoupleAI`). Events `Coupled` and
  `Joined`, in the simulation log.
- **Cuts:** waiting points 4NNSS (the first NN cars kept) and 5NNSS (the
  last NN) cut there as Open Rails does, never leaving the moving part
  without an engine. The cars cut off are a standing train where they
  stand (`Uncoupled`). A train whose own cut-off cars stand on the rest of
  its path part reverses where it stands.
- **Placement:** a train may start in a section a standing train stands
  in when it is clear of it (Open Rails' `CanPlaceTrain`): ahead, the
  train attaches to it; behind, it leaves it.
- **Events:** the wagon checks read the changed lists: a fixture's
  AssembleTrain fires on the train a coupling made, a DropOff on the car
  a 4NNSS cut left in a siding.
- **Fixtures:** `simulation-coupling` (12 checks). One core check
  changed: a loose consist stops a train from starting only where the
  train would stand.
- **The seven routes** (against phase 1): local_cutbank's player train
  attaches to the 13 cars at its reversal and takes them on ("Pick up
  cars" fires). No drop-offs are done yet (part B), so it runs past
  Cutbank, and the activity's trap there ("You've gone too far and missed
  Cutbank", Location2) ends it in failure at 13:10:38. JAPAN1 itr's ITR
  (Traffic 05) starts now: it takes the 10 cars standing 85 m ahead of it
  and runs its service with them (worth a look in Open Rails in phase 6).
  Every other train and arrival is unchanged.
- **Left:** phase 2b (below).

**Phase 2a, signals into occupied blocks (done 2026-10-21; the user:
with task 07).** Before, TSRE's core gave a block only CLEAR or
JN_OBSTRUCTED and never let a train into an occupied one (task 05 left
OCCUPIED for later). Now as Open Rails (`SignalObject`,
`TrackCircuitSection.getSectionState`, `AITrain`):
- **The block:** a train asking a stop signal whose block another train
  is in gets it cleared with BLOCK_OCCUPIED. That holds when each held
  section is one a standing train stands in, or one a train stands on at
  rest or runs along the same way. A section only reserved for another
  train, a train running the other way, a switch set against the train or
  a crossing another train holds obstruct the block: the signal is not
  cleared, as before.
- **The aspect:** scripts decide (`block_state()` gives BLOCK_OCCUPIED);
  Open Rails' rule for types without a script and TSRE's built-in rule
  show STOP. Above STOP (the plated signals of USA2 and BNSF_SCENIC:
  STOP_AND_PROCEED, RESTRICTING) the train takes the block up to the
  occupied section, attaching to a standing train there as anywhere.
  Once it has taken the rest of the block, the block reads clear again.
- **The approach:** at STOP_AND_PROCEED or RESTRICTING cleared for it, a
  train is down to the creep speed 20 m short of the signal, then goes on
  (Open Rails' AI `SIGNAL_ASPECT_RESTRICTED`), unless the aspect is
  flagged OR_NOSPEEDREDUCTION.
- A signal stays cleared for a train that waits at its STOP into an
  occupied block (Open Rails' enabled train). One core check changed for
  that; two new ones: a plated scripted signal in front of a loose
  consist (STOP_AND_PROCEED, passed at 2.5 m/s 20 m short, the train goes
  in and joins the consist) and the same unplated (STOP, the train stands
  short).
- **The local routes:** only USA1, USA2 and BNSF_SCENIC have scripts that
  can show a permissive aspect, and no train runs differently. USA2
  takes 5.84 s against 5.76 s without the change (three runs each).
  yard_one's Yard_One_Traffic2 still waits at signal 190: it has no
  plate, so its script shows STOP for the occupied block. Open Rails does
  the same (run 2026-10-21, `orts-simulation-compare.sh`): its train
  stops at 11:12:26 about 34 m short of the signal at STOP, 6.7 mi out,
  and still stands there at 11:35 (TSRE's: 11:12:38, 30 m short). Open
  Rails' AI asks to pass a signal at STOP only at waiting point 60002,
  and the path has none.
- **Simplified:** the block reads clear again only once the train has
  taken the rest of it (Open Rails: as soon as the other train has left
  it); call-on is not done (permission at 60002: phase 2b).

**Phase 2b, living trains, 60001 and 60002 (done 2026-10-21).** As
Open Rails' AI (`AITrain.UpdateFollowingState`, `CoupleAI`,
`StealCarsToLivingTrain`, `LeaveCarsToLivingTrain`, `TerminateCoupling`,
`TestUncondAttach`, `TestPermission`, `Simulator.UncoupleBehind`):
- **Attaching to a living train:** the other train must be at rest
  (below 0.025 m/s) within 600 m ahead on the path part. Its front or
  rear must be in the part's last section (the part ends there: a
  reversal or the path's end), or the train has passed waiting point
  60001. The sections up to it are taken, the other's own not; the
  authority ends at the contact. If the other moves off first, the attach
  is dropped and the train stands where it is.
- **Coupling:** with path parts left and no 60001, a train whose leading
  vehicle is an engine takes the other's cars from the end it met up to
  that train's engine. Otherwise it leaves the other its own cars up to
  its own engine. At least one car stays, both go on, and the train
  reverses where it stands (Open Rails moves its reversal point under
  it).
- **Joining:** on its path's last part, or after 60001, a train joins the
  other: its cars go on at the end it met, and it is gone. If it was the
  player's train, the one it joined is the player's from then on (wagon
  and location events read that one).
- **Join and split (60001):** the joined train is carried along. When the
  carrying train next cuts cars off (4NNSS, 5NNSS), the cars cut off are
  that train again, placed on its own path where the path finds them,
  and it runs on (Open Rails' incorporated train).
- **60002:** permission to pass the stop signal at the end of the section
  the train waits in, into an occupied block, whatever the signal shows;
  the train waits 20 s there.
- **Sections shared:** two trains that parted may stand in one section.
  The one that leaves it gives it to the other, and a train's authority
  ends at the near end of cars ahead that it parted from, its own cut-off
  cars too. Before, a 5NNSS cut let the train run on through its cut-off
  cars to its reversal point beyond them; the fixture did not notice and
  now checks that the train reverses where it stands.
- **Texts:** events "took N cars from X", "left N cars to X", "joined X"
  and "runs on its own again, parted from X"; a train carried along shows
  "joined X". Waiting points 60001 and 60002 are decoded (before, they
  were waits of 60001 and 60002 seconds) and described in the editor.
- **Fixtures:** `simulation-coupling` 19 checks (taking and leaving cars,
  joining, no attach where the path goes on past the other train, 60001
  join and split; reversing where it stands after a cut and after taking
  or leaving cars); a core check for 60002 at the unplated signal; the
  path suite's waiting point kinds. The UI suite's deadlock fixture had
  its two trains start at each other's path ends, so under Open Rails'
  rules one now couples to the other; they now reverse just past the
  junction, short of each other.
- **The local routes:** no train runs differently (no local path has
  60001 or 60002, and no living train is attached to).
- **Simplified:** no coupling to a train standing beyond the path part's
  end in its last section (Open Rails' routes end at whole sections, so it
  may); the editor still names the original player's train the player's
  after it joined another.

**Phase 3, A7: the editor (done 2026-10-21).**
- **Frames** carry each train's vehicles and their version (shared from
  frame to frame while unchanged), and the standing trains: name,
  vehicles, each vehicle's two ends (`standingEnds`, facing as in a train
  running toward the track's start), whether gone, whether one of the
  activity's loose consists.
- **The controller** makes a train's consist copy again from its vehicles
  when their version changes. The copy stays the same object, so the
  camera riding it (Follow) and the selection keep it, with its sounds
  stopped first. Copies of the standing trains are made again whenever
  they change, drawn in 3D and on the map, not picked.
- **The activity's loose consists** are not drawn, in 3D or on the map,
  while a simulation shows the activity (`Activity::showsLooseConsists`):
  the standing trains are drawn instead.
- **The Timetable window** lists couplings and cuts with the chosen
  train's other events (already from phase 2a).
- **Why an event did not fire** names where the run left the cars: Check
  Activity's last run from the latest frame (recorded again when cars
  change), Simulate's from the core, the route suite's too. Cars a train
  cut off read "standing where X left it".
- **Fixtures:** `simulation-ui` (66): a loose consist ahead of the player's
  train drawn as a standing train in place of the activity's object, the
  player's train taking it and its consist made again with 4 vehicles, the
  standing train gone, the Timetable window listing "coupled to 2 cars",
  the object drawn again after Stop; `simulation-coupling` (21): the cars'
  places in the why texts.
- **Seen:** captures of USA2 local_cutbank (13:06:40, 13:08): the 13 cars
  standing ahead of the player's train on the map, then taken along in
  one line behind its engines, the pickup event's message beside it; the
  same in 3D.

## Why

Activities are made of trains that change: a train picks up cars and sets
them out, engines run round their trains, AI trains join and split. Task
06 checks the wagon events, but on car lists that never change, so 45 of
the 48 local wagon events cannot fire, and three activities end in
failure for work not done. The user (2026-10-20): TSRE's autopilot does
the player's work orders, so the designer sees an activity run from its
start to its end.

## Current state

- A train's vehicles are fixed in the input (`TrainPlan::vehicles`, its
  `length`, its summed `performance`); its state is a rigid line (the
  front's route distance, its speed, reversed or not). The core reads
  `plan.length`, `plan.vehicles` and `plan.performance` everywhere: the
  track it holds, its rear, its vehicles' poses, its physics, its rates
  (freight or passenger, an EMU's), its boarding time.
- A loose consist only holds its sections for the whole run
  (`StaticConsist`); a train reaching one stops a section short and waits
  for good (three local trains do: the player's train of local_cutbank,
  whose path reverses at the 13 cars its pickup event wants; an AI train
  of yard_one, at a signal whose block holds them; one of JAPAN1's itr,
  which never starts: its front would stand in the loose consist's
  section).
- The core works in whole sections: a train never enters a section
  another train or a consist holds, so trains never meet.
- Task 06 gave each car its activity id (consist × 65536 + UiD) and checks
  wagon events on the player's train and the loose consists.
- The editor draws the simulated trains as copies of their consists made
  once (`SimulationController::consistOf`) and the activity's loose
  consists as the activity's objects.

## The local content (2026-10-21)

From the route suite's analysis (`TSRE_SIM_WORK`, task 07's design), the
23 activities with wagon events on the seven routes:
- **The player's path does not lead through the work.** In 20 of them the
  path is a single part (no reversal), mostly along the main line; the
  loose consists to pick up are off it, and so are the sidings to set
  out in (on the path: a siding in local_cutbank, nnmxdgds, whitpull,
  whitefishydsw and freighttutorial). yard_one's and yard_two's paths are
  811 and 254 m long. MSTS leaves the moves to the player, who leaves the
  path to shunt. Only local_cutbank's path runs into a loose consist: it
  reverses at the 13 cars its pickup event wants.
- **Where the cars to set out are in the player's train** (E an engine,
  w a car, X a car to set out):

  | Activity | Train |
  | --- | --- |
  | local_one | `EwwXX` (the rear end) |
  | local_two | `EXX` (the rear end) |
  | local_cutbank | `EEXwwwww` (behind the engines), `EEwXXXXX` (the rear end) |
  | autotrnsetout | car 17 of 42 |
  | hotbox | car 7 of 22 |
  | barcarso | car 10 of 17 |
  | murder | car 18 of 19 |
  | nnmxdgds | car 2 of 41, between two engines |
  | mainduty, whitpull, yard_one | the whole train, engines included |

  The last three are MSTS's way of saying "leave your train in this
  siding" (their briefings say so): MSTS's DropOff only asks that every
  listed car stand in the siding (below), so they end successfully in
  MSTS and never in Open Rails, which wants the cars uncoupled.
- **MSTS's wagon checks** (`train.exe`, traced 2026-10-21 from the event
  dispatcher `FUN_0059b083`, which calls one check by the event's kind):
  DropOff (`FUN_0059b430`) fires when every listed car stands on the
  siding's track between its two items, coupled or not, moving or not;
  PickUpWagons (`FUN_0059b3bf`) when every listed car is in the player's
  train (no speed limit; Open Rails 1 m/s); AssembleTrain (`FUN_0059b599`)
  when the listed cars, in order or reversed, are coupled to each other
  and to nothing else (as Open Rails); AssembleTrainAtLocation
  (`FUN_0059b737`) both.
- **Pickups** name loose consists (14 PickUpWagons, 3 mixed assembles);
  AssembleTrain lists are mostly the player's train as it starts (task
  06).
- **No Open Rails shunting waiting points**: the local paths have 6
  waiting points, all plain seconds; 4NNSS, 5NNSS and 60001 are tested
  with written fixtures.

## Reference: Open Rails' AI

- **Following and attaching** (`AITrain` following state): a train ahead
  on its route within its authority, standing (below 0.025 m/s) within
  600 m, is attached to when it is a loose consist (always), or stands in
  the last section of this path part with a reversal there or the path's
  end, or the waiting point 60001 asked for it. Not attaching, an AI train
  stays 10 m behind a standing passenger train, 50 m behind a freight one,
  300 m behind a moving one. Attaching, it creeps up (2.5 m/s) and comes
  in at the coupling speed (0.4 m/s); it couples at contact (the two ends
  closer than 0.1 m, `CheckCouplePosition`).
- **Coupling** (`CoupleAI`):
  - to a loose consist while its path goes on (another part, or more than
    5 sections ahead): it takes the cars (`CoupleAIToStatic`) and goes on,
    the formation turned where the ends meet the other way;
  - to a living train (AI or the player's) with path parts left and not
    attaching unconditionally: when its coupling end is an engine it
    takes that train's cars up to its engine (`StealCarsToLivingTrain`),
    else it leaves it its own cars up to its engine
    (`LeaveCarsToLivingTrain`); both trains go on;
  - else (its path's end, or 60001): it joins the other train, its cars
    added at the end it met, and is gone (the player's train becomes the
    joined one when it was the player's).
- **Uncoupling** (`TestUncouple`, waiting points): 4NNSS keeps the first
  NN cars counted from the front in the direction of travel (engines
  included) and waits SS s; 5NNSS keeps the last NN; no uncoupling when
  the kept part has no engine. The cars cut off become a loose consist
  where they stand (`UncoupleBehind`).
- **The end of a path**: an AI train at its path's end is removed, unless
  it stands at a signal within 25 m or at a track end (then it stays
  frozen); a train joined to another is removed with it.
- Open Rails' autopilot of the player's train runs these same AI rules:
  it attaches to loose consists on its path and uncouples at waiting
  points of its path. It does no work orders of its own.

## Design

### Part A: trains that change (the core)

**A1. Each train its own consist.** The input gives each vehicle what the
core needs of it: its length, type, Eng, car id, flip, mass, engines'
force and power, Davis coefficients (today summed into the train's
`Performance`). Each train in the core has its own vehicle list in its
runtime; its length, performance, freight or passenger kind and rates
(task 03) are worked out again whenever the list changes. Everything that
read `plan.length`, `plan.vehicles` or `plan.performance` reads the
train's own.

**A2. Standing consists as trains.** A loose consist is a standing train
of the core: its vehicles, the track it covers (vector node intervals,
its first car at one end) and the sections it holds. They are made from
the activity's loose consists at the start and from cars cut off a
train, and they go when a train couples to them. Frames carry them (their
vehicles' poses), so the editor draws them and hides the activity's own
loose consist objects while a simulation shows the activity.

**A3. Attaching.** A train attaches to a standing train ahead on its route
as Open Rails' AI does (a loose consist always; a stopped train at the
end of this path part with a reversal or the path's end; after 60001).
Then its authority may run into the section the other train stands in,
up to the point of contact (the other's near end on this train's route),
not a section short. It approaches at Open Rails' creep speed and comes
in at the coupling speed, and couples at contact. A section stays held
by both while they are apart; after coupling the joined train holds all.
A train not attaching keeps stopping short of an occupied section, as
now (no contact can happen that was not planned).

**A4. Coupling.** At contact the other train's vehicles join this train's
list at its leading end (turned when the ends meet the other way); the
front moves to the far end of what joined, the route part extended over
the other's track where the part ends inside it. Then Open Rails'
outcomes: a loose consist taken along while the path goes on; at the
path's end (or after 60001) this train joins the other and is gone; with
a living train, cars taken or left up to the engine. Each coupling a
simulation event (`Coupled`: the trains, the cars).

**A5. Uncoupling.** A cut behind car N (counted from the leading end):
the cars beyond become a standing train on the track they stand on; the
train's length and front (when the rear part is kept) follow. Waiting
points 4NNSS and 5NNSS cut as Open Rails does (no cut leaving the moving
part without an engine). The autopilot (part B) cuts the same way. Each
cut a simulation event (`Uncoupled`).

**A6. Events.** Task 06's wagon checks read the trains' and standing
trains' lists as they are; DropOff's "standing consist" is a standing
train.

**A7. The editor.** Frames carry each train's vehicle list (its Eng
files, flips, ids) with a version; the controller makes a train's consist
copy again when the version changes, and makes copies of the standing
trains. The Timetable window lists couplings and cuts; the map draws
standing trains as consists.

### Part B: the autopilot's work orders

**B1. Goals.** The work the activity asks of the player, from its wagon
events in the order they become active: pick up these cars
(PickUpWagons), set out these cars in this siding (DropOff), make the
train exactly these cars (Assemble, at a siding for AssembleAtLocation).

**B2. Moves.** The autopilot does the work with moves of its own, off its
path where the work is off it (nearly always, above): a move is a route
on the track network from where the train stands to a target, with
reversals where the track allows the train to clear a junction, and an
action at its end (couple, cut, set out). Routes are found by a search
over the network's sections and junctions (from the trunk either branch,
from a branch only the trunk; a reversal on a track at least the train's
length beyond the last junction), the train's length known at each step.

**B3. Patterns.** Each goal becomes moves by a small set of shunting
patterns, the ones the local activities need (a set-out: the listed cars
into the siding, then cut off unless they are the whole train, engines
may be cut off while one stays in the player's train: decision 3):
- pick up: go to the cars, coupling end towards them, and couple;
- set out a block at the train's far end from its engines: bring that end
  into the siding first (shoving in, or through a reversal beyond the
  siding's switch when it faces the other way), cut, leave;
- set out a block elsewhere in the train (behind the engines, in the
  middle): cut the train behind the block where it stands (a siding or
  the main line), set the block out from the front part, come back and
  couple to the rest;
- assemble: pick up and set out until the train is the cars listed.

A goal with no plan found is reported (Check Activity: what could not be
planned, and why: the siding cannot be reached, the cars cannot be got
at the right end).

**B4. Leaving and rejoining the path.** The work is done where the path
comes nearest to it: the autopilot leaves the path at the junction
nearest the work (on the path ahead), does the moves, and comes back to
the path there (or further on) to run the rest of it. A goal whose
cars or siding no route from the path reaches is reported as above.

**B5. When it is planned.** On the main thread when the input is made,
from the track database and where the cars stand at the start (they move
only by the autopilot's own moves): the plan is a sequence of route parts
with their actions, which the core runs as it runs paths. A plan that a
run cannot carry out (blocked for good, an event chain asking something
else first) stops and is reported; no re-planning in this task.

**B6. Feasibility first.** Phase B0 builds the route search and the
patterns as a tool on the local content, before any of it runs in the
core: for each work order of the 23 activities, whether a plan is found
and how long it is, set against what the activity's briefing tells the
player to do.

### Not in this task

- AI trains doing work orders (the user's idea of 2026-10-17): the same
  planner may serve them later.
- Open Rails' timetable mode (its own join and split commands).
- Run-round moves through loops for a set-out at a siding facing the
  wrong way where no reversal beyond its switch fits: reported, unless
  B0 finds local activities needing them.
- Coupler physics, slack, couplers breaking.

## Phases

1. **A1 and A2:** vehicles' data in the input, each train's own list and
   what follows from it, standing trains (the loose consists); every suite
   unchanged.
2. **A3 to A6:** attaching, coupling, uncoupling as Open Rails does, the
   events on changing lists. Fixtures: a train taking a loose consist and
   going on, joining one at its path's end, 4NNSS/5NNSS cuts, 60001,
   taking and leaving cars of a living train, a cut without an engine
   refused; the three local trains now held by loose consists.
3. **A7:** frames and the editor (consists made again, standing trains
   drawn, the activity's loose consists hidden while simulated, the
   Timetable window's couplings and cuts).
4. **B0:** the route search and patterns as a tool: the plans for the
   local work orders, tabled here with the briefings.
5. **B1 to B5:** the autopilot running its plans; the local freight
   activities run to their end where the plans exist; Check Activity says
   which goals could not be planned and why.
6. **Verified:** every local activity: wagon events fired, activities
   ended, against task 06's table; Open Rails on an activity with a pickup
   on its path (local_cutbank: its autopilot attaches as ours does).

## Decisions (user, 2026-10-21)

1. Part A as Open Rails' AI does it, part B TSRE's own.
2. The autopilot may cut a train anywhere it stands, the main line
   included.
3. **DropOff, TSRE's rule:** a train that is the listed cars (in any
   order), whatever train it is: a loose consist, the player's or an AI
   train; one end of it in the siding (Open Rails' less strict end rule;
   MSTS wants every listed car in it). Cars to set out from a longer train
   are cut off first; a list of the whole train ("all your units")
   matches it as it is. Done in task 06's checks (2026-10-21): whitpull
   ("leave your train in Whitefish Siding 3") now ends successfully;
   nnmxdgds and local_cutbank wait for their cars to be cut off (part
   B). **The autopilot**: the listed cars into
   the siding, cut off unless they are the whole train ("all your units":
   the train already matches); it may cut engines off while one stays in
   the player's train.
4. Phase B0 first; **B1 to B5 go ahead only if B0 succeeds.**
