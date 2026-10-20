# Task 07 - Coupling and Train Changes

Status: designed 2026-10-21, not started (branch `feature/simulation`).
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

Nothing yet.

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
  of yard_one; one of JAPAN1's itr).
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
3. **DropOff, TSRE's rule:** the listed cars must be in the siding, held
   by any train: a loose consist, the player's or an AI train, coupled or
   not; one end of that train in the siding (Open Rails' less strict end
   rule; MSTS wants every listed car in it). Done in task 06's checks
   (2026-10-21): on the seven routes whitpull ("leave your train in
   Whitefish Siding 3") and nnmxdgds now end successfully, local_cutbank
   fires one of its two drop-offs. **The autopilot**: the listed cars into
   the siding, cut off unless they are the whole train ("all your units":
   the train already matches); it may cut engines off while one stays in
   the player's train.
4. Phase B0 first; **B1 to B5 go ahead only if B0 succeeds.**
