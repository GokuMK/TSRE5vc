# Task 07 - Coupling and Train Changes

Status: designed 2026-10-21; part A done (phases 1, 2a, 2b, 3); phase
B0 done, its results and the question of going on with B1 to B5 below
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
- the local content: the 22 activities with wagon events, their player
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

**Phase B0, feasibility (done 2026-10-21).** A planner as a tool
(`src/tsre/sim/WorkPlanner`, not run by the core): `PlanWork` takes the
input and plans the player's train's work from where it starts, the
loose consists where the activity has them.
- **The yard:** the player's train (its vehicles from end A to end B and
  the track its body covers) and the standing cuts.
- **Moves:** a Dijkstra search over (vector node, direction, the train's
  leading end). It goes through junctions as they connect (a facing one
  either way, a trailing one to its input), reverses where the whole
  train has passed the junction it came in by (its length and 15 m of
  free track beyond it), and is stopped by standing cars (not the cut it
  couples to). A reversal weighs as 300 m in choosing a way.
- **Goals,** in the order the event chain makes them active: events not
  about cars taken to fire when active (one ending in failure never, one
  ending in success last). Of the wagon events active together, the first
  that can be planned is done, after any whose outcomes would take its
  level down. One none can be planned is reported and taken as done.
- **Patterns:**
  - Pickup: couple at the cut's end next to the cars asked for, then cut
    the others off; else take the whole cut (PickUp asks only that the
    cars be in the train). The end and the leading end are chosen by the
    metres, and by what later goals want: an assemble's order, and the
    cars of drop-offs to come at an end that can lead into their siding.
  - Set-out: a block at an end of the train goes into the siding first
    and is cut off. A block in the middle: the whole train goes towards
    the siding, the part beyond the block is left there, the block is set
    out, and the part is coupled again. Where to leave it is tried at the
    node ends of the way's last legs, its reversals and where the train
    stands; the shortest is kept. The whole train asked for goes into the
    siding as it is.
  - Assemble (done when a train or a cut is exactly the cars, in order or
    reversed, as the core checks): pickups, then the cars not asked for
    cut off; cars asked for without an engine set out as a block (into
    the siding for AssembleTrainAtLocation). Cars of a cut asked for
    between two parts of the train are put in between: the train is cut
    there, the part with an engine fetches the cars and comes back.
- **The tool:** the route suite with `TSRE_SIM_PLAN=1` prints each
  activity's briefing, goals, plans (metres, reversals, couplings, cuts,
  each step) and the way back to the path's end. Fixtures:
  `simulation-work` (10) on two yards, one siding facing the train and one
  behind the junction. They cover a pickup, a pickup leaving the car
  beyond, the rear car reversed into a siding, no way for the front car
  there, a car from the middle, a car put in between, the whole train,
  and the event chain's order.

**B0's results** (`TSRE_SIM_PLAN`, 2026-10-21). Extra driving is the
work's metres and the way back to the path's end, less the path's
length:

| Activity | The briefing's work | Goals (✗ not planned) | Extra driving | The plan |
| --- | --- | --- | --- | --- |
| homestre, winterun, dawnoil | a run; the train as it starts | assemble ✓ | 0 | nothing to do |
| rsrescue | reverse into the siding, couple, take the loco and tender out and push them into siding 1, couple to the carriages | dropoff 2, assemble 9 | 1.4 km | the same moves |
| barcarso | set out the bar car at Shonwies, the restaurant car in its place | dropoff 1, assemble 17 | 2.4 km | set out from the middle; the train cut at the gap, the restaurant car fetched into it |
| murder | set out the sleeping car at Stams siding, leave carriages at Vols | dropoff 1, assemble 13 | 2.0 km | the same |
| replloco | through the station, reverse into the north siding, couple to the train | assemble 18 | 0.2 km | the same, one reversal |
| tnrescue | reverse into the siding, couple to the front of the train | pickup 7 | 0.1 km | the same, one reversal |
| autotrnsetout | set out car 0-16 on Essex Siding 2 | dropoff 1 | 2.9 km | from the middle, the rest left by the siding |
| hotbox | leave car 0-6 in the Glacier Park hole track | dropoff 1 | 1.5 km | the same |
| freighttutorial | drop an empty lumber car, couple to a cut of full ones | pickup 1, assemble-at 1 | 1.7 km | in the event chain's order (the pickup activates the drop) |
| local_cutbank | hoppers into Grain Elevator 1, the boxcar onto the team track, 13 grain cars from North Elevator Siding 3 | dropoff 1, pickup 13, dropoff 5 | 2.0 km | the same |
| local_one | two boxcars onto the Columbia Falls plywood spur | dropoff 2 | 0.6 km | the same |
| local_two | pick up 5 woodchip cars and 2 boxcars, leave 2 centerbeams | dropoff 2, pickup 7 | 2.4 km | the same |
| mainduty, whitpull, mornimod | the train into a siding | dropoff or assemble-at | 0 to 1.1 km | the same |
| yard_one | two cuts into one outbound cut on Siding 8 | pickup 5, pickup 3, dropoff 9 | 6.2 km (path 0.8 km) | the same, long |
| nnmxdgds | the helper (behind the lead loco) onto Essex Siding 2 | dropoff 1 | 2.2 km | the train run to Essex, the 39 cars left, the helper put in with the lead loco, back (76 km before a fix below) |
| 4_lumberrun | Everett switching by its work order | 8 goals | 45 km (path 4.8 km) | found, long: cars must lead into their sidings |
| yard_two | the westbound cars out of two cuts and a logger, into one cut on Siding 8 | 3 pickups, dropoff 5 ✗ | 5.3 km | the cars stand between others in the cuts: sorting |
| whitefishydsw | sort the yard into outbound cuts | 2 of 9 goals; 5 dropoffs ✗ (sorting), 2 ✗ impractical | - | the same: sorting |

- **41 of the 48 goals are planned, and all the work of 20 of the 22
  activities.** Planning takes under 0.8 s per activity (2.3 s for
  whitefishydsw). In 18 activities the extra driving is under 3 km and
  the moves are the ones the briefing asks for.
- **Beyond the patterns:**
  - *Sorting:* gathering cars from between others in several cuts into
    one ordered cut (whitefishydsw, yard_two).
  - *Long yard work:* 4_lumberrun, 45 km beyond its 4.8 km path. Its
    pickups take long ways so that the cars can lead into their drop
    sidings later; the cars left for a middle set-out stand where the
    train is, kilometres from the siding. Run-arounds (below) do not
    shorten it. With a smaller look-ahead penalty its work is 32 km, one
    drop then impractical.
  - nnmxdgds's 76 km was a bug (below), not a missing run-around.
- **Not in B0:** AI trains and signals; the work planned from the train's
  start, not from where it leaves the path (the extra driving takes the
  path's own length off); a plan's moves run by the core.
- **For B1 to B5:** the planner's output is what B1 to B5 need: route
  parts with reversals and an action at the end of each step. The core
  runs them as paths, with the couplings and cuts of part A.

**B0, after the user's decisions (2026-10-21).**
- **Run-arounds:** a block that must lead into a siding the other way is
  left standing where the train is (or on its way to the siding), the
  rest of the train goes round it by a loop or crossovers and couples to
  its other end, then puts it in; tried beside the direct way, the
  shorter kept. Fixture: a loop, the car behind the engine, a siding
  beyond facing the train (`simulation-work`, 11 checks).
- **Impractical work is skipped:** a goal whose plan is more than 10 km
  of moves beyond the way to its place, or more than 12 reversals, is
  not done; the run's log will say so (B1 to B5).
- **Two planner bugs fixed:** the search let a move pass through the cut
  it couples to (a run-around "found" with no loop); and a pattern's
  second way round (which part of the train to leave) started from the
  yard its first way's trials left (nnmxdgds's helper: 113 km, now
  2.2 km).
- `TSRE_SIM_PLAN=<activity file>` plans that activity only.

**Phases B1 to B5, the autopilot running its plans (done 2026-10-21).**
- **The player's route made from the plan** when the input is made
  (`ApplyWork`, from `MakeInput`): its path up to where the work leaves
  it (its stops, waits and passing paths kept), the work's moves as route
  parts (a new part at each reversal, starting with the train's body), a
  coupling going on over the cars taken, a cut a waiting point where the
  core cuts the cars off (`WaitPlan::keep`, the cars kept at the front or
  the rear). On the work's moves the train may pass a stop signal into an
  occupied block as with permission (60002), up to where it comes back to
  its path (`workFrom`, `workTo`).
- **Back to the path:** the nearest way to the rest of the path that
  skips none of its stops, waits and event places (a skipped one weighs as
  5 km of driving), not to where the rest of the path would run into cars
  left standing; the rest of the path is then run as it is, with its
  stops, waits and passing paths. Where the work makes active an event
  ending the activity in success at a place the rest of the path does not
  pass, the way back goes there instead: hotbox's End Activity on the
  other track (along the path the train runs into End without success, as
  in task 06), 4_lumberrun's at the yard it started from (its path ends
  elsewhere).
- **Cars an assemble leaves** (those not asked for, or the cars asked for
  without an engine) stand off the paths of trains: off an AI train's path
  whenever a track within 3 km has room (murder's 5 carriages left on the
  main line held two AI trains for good); off the player's path or an
  event's siding only for less than 3 km more. Within decision 2: the
  autopilot still cuts on the main line where it must.
- **Skipped work orders:** each in the run's log at the start (`WorkSkipped`:
  "work order X skipped, too complicated: …" or "… no plan for it: …");
  why a wagon event did not fire names the autopilot's reason and where
  its cars are.
- **The core:** several standing trains may stand in one section (cars
  set out beside a loose consist in the same siding: local_two). Each is
  considered: a train attaches to the nearest ahead on its part, goes on
  past those off its part, and its placement still fails on one under it.
  Before, a section knew one of them, and the train ran through the other
  or waited for good.
- **A location event's why:** passed after another event lowered it now
  reads "when it was no longer active" (replloco's Passed Pians, a warning
  the rescue takes away).
- **Fixtures:** `simulation-work` (21): six plans run by the core to the
  path's end (a pickup, a rear car reversed into a siding, a car from
  behind the engine, a run-around by the loop, the whole train, a car put
  in between), the way back to a path going on past a reversal, the way
  back to the place ending the activity, cars not asked for set out off an
  AI train's path, and a set-out beside a loose car then that car taken
  from the siding's other end (it fails with one standing train per
  section).

**B1 to B5's results** (the route suite, 2026-10-21). Of the 65 local
activities 52 end in success and 13 never end, none in failure (task 06:
34, 28 and 3); 41 of the 48 wagon events fire (task 06: 3). The work
orders' activities, task 06 against now:

| Activity | Task 06 | Now |
| --- | --- | --- |
| rsrescue | allstops 0/1, assemble 0/1, dropoff 0/1, location 1/1; no end | allstops 1/1, assemble 1/1, dropoff 1/1, location 1/1; success at 11:32:15 |
| barcarso | allstops 0/1, assemble 0/1, dropoff 0/1, time 0/1; no end | allstops 1/1, assemble 1/1, dropoff 1/1, time 1/1; success at 17:40:41 |
| murder | assemble 0/1, dropoff 0/1, location 4/6; no end | assemble 1/1, dropoff 1/1, location 5/6; success at 18:26:24 |
| replloco | allstops 0/1, assemble 0/1, location 2/2, passengers 0/1, time 0/1; no end | allstops 1/1, assemble 1/1, location 1/2 (Passed Pians: a warning, not due), passengers 0/1, time 0/1; success at 11:21:41 |
| tnrescue | allstops 0/1, location 0/1, pickup 0/1; no end | allstops 1/1, location 0/1, pickup 1/1; success at 08:22:59 |
| autotrnsetout | dropoff 0/1, location 1/2; failure at 10:08:18 | dropoff 1/1, location 1/2; success at 10:17:02 |
| dawnoil | assemble 1/1, location 4/5, time 0/1; success at 07:22:56 | the same |
| freighttutorial | assemble-at 0/1, location 12/19, pickup 0/1, time 7/9; failure at 10:01:53 | assemble-at 1/1, location 14/19, pickup 1/1, time 7/9; success at 10:08:26 |
| hotbox | dropoff 0/1, location 4/6, time 0/2; failure at 17:01:39 | dropoff 1/1, location 5/6, time 0/2; success at 16:55:36 |
| local_cutbank | dropoff 0/2, location 0/4, pickup 0/1; no end | dropoff 2/2, location 1/4, pickup 1/1; success at 13:21:22 |
| local_one | dropoff 0/1, location 1/2; no end | dropoff 1/1, location 2/2; success at 08:20:45 |
| local_two | dropoff 0/1, location 0/1, pickup 0/1; no end | dropoff 1/1, location 1/1, pickup 1/1; success at 13:14:28 |
| mainduty | dropoff 0/1, location 1/1; no end | dropoff 1/1, location 1/1; success at 14:16:00 |
| mornimod | assemble-at 0/1; no end | assemble-at 1/1; success at 09:33:39 |
| nnmxdgds | dropoff 0/1, location 1/2; no end | dropoff 1/1, location 2/2; success at 13:22:58 |
| whitefishydsw | assemble-at 0/1, dropoff 0/7, location 0/1; no end | assemble-at 1/1, dropoff 1/7, location 0/1; success at 17:13:49 (its success event; 6 drop-offs skipped: sorting, impractical) |
| whitpull | dropoff 0/1, time 1/1; no end | dropoff 1/1, time 1/1; success at 09:41:57 |
| yard_one | dropoff 0/1, pickup 0/2; no end | dropoff 1/1, pickup 2/2; success at 11:07:18 |
| yard_two | dropoff 0/1, pickup 0/3; no end | dropoff 0/1 (sorting), pickup 3/3; no end |
| 4_lumberrun | dropoff 0/3, location 0/1, pickup 0/5, time 1/1; no end | dropoff 3/3, location 1/1, pickup 5/5, time 1/1; success at 09:32:46 |
| homestre, winterun | no end (not their work: passengers, places) | the same |

- **Trains still waiting at the end:** yard_one's AI train at the signal
  whose block holds the loose cars (unplated, STOP; Open Rails the same),
  as before; hotbox's AI train behind the player's train standing at End
  Activity on its track, after the activity has ended.
- **Left:** sorting (yard_two, whitefishydsw), the next task; phase 6
  (the local activities against task 06's table, Open Rails where it
  tells something).

**Sorting (done 2026-10-21)**, as B7 describes it.
- **The planner** (`sort`): deliver a block where its cars are wanted, set
  a block aside, fetch a cut; the cheapest each time. Into a long yard
  track only as deep as the cars gathered there will need (Whitefish's
  tracks are 1.2 to 2 km: as deep as the track allows, every move was
  1.5 to 3.5 km). A block is set aside only where it shuts off neither the
  siding, the cars still to fetch, the sidings of the wagon events to come
  nor the way back to where the train started (whitefishydsw's locos were
  boxed in before that).
- **Tried** when the plain set-out finds no plan or one over 5 km, the
  shorter kept: murder's sleeping car (now 4.9 km, was 6.2) and
  local_one's set-out (0.2 km shorter) take the sorted plans.
- **Pickups on the way:** a pickup whose cars an active drop-off wants is
  planned after it; the drop-off's fetching brings them into the train,
  and they fire then (yard_two's three pickups).
- **Limits** (the user, 2026-10-21): a sorting goal is impractical beyond
  20 reversals (every other goal 12) or 10 km more than the way there;
  sorting gives up past 20 km or 40 reversals.
- **Found on the way:** the route suite's `TSRE_SIM_PLAN` print planned on
  the input with the work already applied (a different path); it now
  plans from the input without it. When the work's first move runs the
  whole path, the run ends with the work (no way back). The core ends a
  train's run when its own cut-off cars stand on the rest of its last
  part (it waited for them for good). `TSRE_SIM_YARD=<activity>` prints
  the yard: the cuts and their cars in order, the player's train, the
  wagon events and the tracks round them.
- **Fixture:** `simulation-work` (23): a ladder yard, the two cars asked
  for between others in a cut on one siding gathered on another, the car
  between them set aside off the track leading there; the run fires the
  drop-off and ends.
- **Results** (the route suite): yard_two: dropoff 1/1, pickup 3/3,
  success at 15:18:18 (the drop-off 6.2 km and 15 reversals, the pickups
  on its moves). whitefishydsw: dropoff 6/7, assemble-at 1/1, success at
  18:34:39 (drop-offs of 10.1 km and 18 reversals, 7.7 and 14, 6.9 and
  15, 5.8 and 8, 5.4 and 7, one done by the others' moves; the 13 cars
  for Whitefish Siding 529 skipped as impractical, 33 km: a block would
  have to come to the cars gathered there the other way round). Of the 65
  local activities 53 end in success and 12 never end, none in failure;
  47 of the 48 wagon events fire. Nothing else changed.
- **Planning time** (the user asked, 2026-10-21): 3.7 s for whitefishydsw
  was a debug build's; a release build (-O3) took 0.36 s, now 0.17 s
  (every other local activity under 0.08 s). The searches are cheaper (no
  state copied each step, the least costs hashed, entries queued before a
  cheaper way was found skipped), and sorting's go no further than it may
  drive (20 km), so they do not grow with the route. Searches that find no
  way still cover the network within their reach.
- **A big route** (the user asked: PRUDNIK, 7673 vector nodes against
  USA2's 408; no activity of its own). `TSRE_SIM_BENCH=1` plans a yard job
  on any route: in the yard with the most stub sidings within 1.5 km
  (PRUDNIK's has 24), cuts of 8, 6, 5 and 4 cars, a drop-off sorting five
  of them into another siding and a pickup of two, from a track beside
  the yard and from 25 km away. Release build: PRUDNIK 0.95 s and 0.78 s,
  USA2 35 ms. 95 % of the search was in searches that found no way, most
  of them to the end of a cut against a buffer (the planner tries both
  ends of each cut): those are now refused at once (`approachable`).
  PRUDNIK then 0.10 s and 0.18 s, the same plans; whitefishydsw 0.13 s.
- **Off the main thread:** the editor (Simulate, running back in time,
  Reset, the Timetable window's Simulate) makes the input without the
  work and plans it on a worker thread (`PlanWorkOrders`); after 0.4 s a
  dialog shows the goals planned so far and Cancel, which stops the start
  and leaves the input as it was (`WorkProgress`). Checked on
  whitefishydsw in a debug build: the dialog at 3 of 8 goals, Cancel
  stopping the planning at once. `simulation-work` (25): a cancelled
  planning leaves the input as it was; the editor's planning on a worker
  thread.
- **Left:** whitefishydsw's 13-car drop-off (a run-round for a delivery to
  cars gathered); phase 6.

**Phase 6, verified (done 2026-10-21).**
- **Every local activity against task 06's table** (the route suite after
  sorting): 46 of the 65 activities run as they did in task 06, the same
  events and the same ends at the same times. The 19 others are the
  work-order activities: 16 never ended and 3 ended in failure, now all
  19 end in success. 47 of the 48 wagon events fire (task 06: 3); the one
  left is whitefishydsw's 13-car drop-off, skipped as impractical.
- **The events still not fired in those 19** are not work: the traps and
  warnings an activity sets for a player who goes wrong (End without
  success, Gone Too Far, "You've gone too far", "You've gone the wrong
  way", "You did not complete all your tasks", Passed Pians, a siding
  warning), times after the activity ended, events never made active,
  stop-here events the train passes (the user's decision of 2026-10-21: no
  stopping at them), points off the track (Open Rails' 2.5 m rule) and
  PickUpPassengers (it never fires, as in Open Rails).
- **Open Rails on local_cutbank** (its player's train on Open Rails'
  autopilot along its path; ours with the work orders left out,
  `TSRE_SIM_NOWORK=1`): both reverse beside the 13 cars at the same time
  (13:04:49 and about 13:04:55) and couple to them; "Pick up cars" fires in
  Open Rails at 13:09:29, in ours at 13:06:59, 150 s earlier. Ours ran at
  line speed to 100 m from the cars and crept only there; Open Rails' AI
  closes on a standing train at most at √(distance × its deceleration) / 2,
  the creep speed at least (its FOLLOWING state, `UpdateFollowingState`;
  the deceleration Open Rails' AI MaxDecel, which the core already has).
  Ours now does the same: 13:07:59, 91 s before Open Rails. The rest:
  after the reversal Open Rails creeps for 50 s before its authority
  reaches the cars, and its controls swing on the way in (200 s over the
  last 800 m, the rule gives 175 s).
- **Open Rails on JAPAN1's itr, ITR (Traffic 05)**: ours took the 10 cars
  standing 85 m ahead of its start and ran its service with them. Open
  Rails closes on them as ours does (13:35:01 to 13:35:20, 70 m) and then
  removes the train ("out of control, reason OUT_OF_PATH"): the cars stand
  across a junction onto the other way than its path, so taken, the
  train's front is off its path. Ours had put its front the cars' length
  along its path instead, wherever the cars stood. Now a train whose cars
  taken stand off the rest of its part is removed as in Open Rails (event
  `Removed`; the player's train stands there). Run from 12:00, Open Rails
  crashes at 12:23 ("Collection was modified" in
  `TrackCircuitSection.TestTrainAhead`, from a signal's update, the AI
  trains bunched behind each other); a copy of the activity starting at
  13:25 runs.
- **After the two changes** the route suite has the same events and ends;
  only times move, the couplings slower as in Open Rails (whitefishydsw's
  success 18:44:58, was 18:34:39; yard_two's 15:20:46, was 15:18:18; a few
  seconds sooner where a short approach may now go faster than the creep
  speed).
- **Fixtures:** `simulation-coupling` (23): closing on the cars within
  Open Rails' rule; a train taking cars that stand off its path is
  removed. The route suite's `TSRE_SIM_TRACE` prints reversals and
  couplings too.

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
22 activities with wagon events on the seven routes (23 written here
before; the planner's run in B0 counts 22):
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
core: for each work order of the 22 activities, whether a plan is found
and how long it is, set against what the activity's briefing tells the
player to do.

**B7. Sorting** (after B1 to B5; the user, 2026-10-21: free space is all
it needs; work with no simple solution is skipped and the run's log warns
of it). A drop-off's cars gathered into one cut in its siding, in any
order (the DropOff rule), from wherever they stand. Each time the
cheapest of these moves is made:
- a block at an end of the train (cars going to the same place, up to an
  engine) taken where its cars are wanted: this drop-off's siding, or the
  siding of a drop-off still to come; coupled to the cars gathered there
  and cut off with them as one cut; only as deep into a long yard track
  as all those cars will need;
- a block no drop-off wants set aside on a track no train's path takes,
  where it shuts off neither the siding, the cars still to fetch, the
  sidings of the wagon events to come nor the way back to where the train
  started;
- with none of the cars in the train, a cut that has some fetched, the
  cars beyond the last of them left where they stand.

Each move takes cars out of the train or brings wanted ones in, so the
search ends. It is tried when the plain set-out finds no plan, or one
over 5 km long (the shorter kept). A pickup whose cars an active
drop-off wants is planned after it: the drop-off's fetching brings them
into the train, and the pickup fires then.

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

After B0:
- B1 to B5 go ahead, with run-around moves; sorting (whitefishydsw,
  yard_two) is a task after them. Sorting needs free space, and there are
  plenty of sorting algorithms.
- A work order with no simple solution is skipped, and the run's log
  warns that it is impractical (overcomplicated work would not be fun for
  a player either).

After B1 to B5:
- Sorting first, then phase 6.
- A sorting goal may take up to 20 reversals (each siding visited off a
  ladder takes about two); every other goal keeps 12.

Before:

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
