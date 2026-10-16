# Task 03 - Train Performance: Force and Mass

Status: done (phases 1 to 4, 2026-10-16, branch `feature/simulation`),
compared with Open Rails on every local route. Decisions taken are at the
end.

Sources:
- the Open Rails comparisons of task 02 (phase 7): `scripts/orts-simulation-
  compare.sh`, `scripts/orts-dispatcher-stops.py`, `scripts/orts-compare-
  stops.py`;
- Open Rails (`openrails-linux-spike`), a behavioural reference only, no
  code taken: `AITrain` (`UpdateAccelState`, `UpdateRunningState`,
  `UpdateBrakingState`, `AdjustControlsAccelMore`, `AIPreUpdate`,
  `CreateTrainAction`, `CalculateDistancesToNextStation`), `AI.PrerunAI`,
  `Train.CheckFreight`, `MSTSLocomotive.GetAvailableTractionForceN`.

## Why

The Open Rails comparisons of task 02 found driving the largest
difference left on every local route: our trains accelerated and braked
at a constant 1.0 m/s², Open Rails' AI passenger trains at about 0.4 to
1.0 m/s² and braked at 0.2 to 0.5, so ours gained about 30 s on each run
between stops. Where stops are close they pulled far ahead (JAPAN1's
Odakyu stoppers by 10 to 30 minutes over an hour or two).

The user (2026-10-16): users expect Open Rails parity. "No physics" in
task 01 meant no full physics (brake systems, adhesion, the player's
controls). A simple calculation of force and mass is in for AI trains,
and the driving is chosen per train in code: simple acceleration and
braking, or basic physics. This task is for AI trains; the player's train
is driven as AI for now, and the player's own driving will be a separate,
more advanced system.

## Progress

**Phase 1, the data (done 2026-10-16).** Our own parsers:
- `MstsUnits` (`src/tsre/fileFunctions`): a value with its MSTS unit
  ("39.19t", "230kN", "1400kW", "4000hp", "74.56mph", "150N/m/s",
  "0.0059lbf/mph^2") in SI; quotes round it and a comment after it
  ("117.57t #368000lbs", "166.9t#368000lbs") dropped; an unknown unit
  refused. `ParserX::GetToken`: the next value in a block as written.
- `Eng`: mass, MaxForce, MaxPower, MaxVelocity in SI (the old fields,
  read without units, kept for the editor), ORTSDavis_A, _B, _C, the MSTS
  Friction block (its first row), PassengerCapacity.
- `Consist::fileMaxVelocity`: the consist file's MaxVelocity, which the
  editor's value replaces with its engines' top speed when it lays the
  consist out; the simulation takes the file's, as Open Rails does
  (EUROPE2's steam trains ran at 75 km/h instead of 157).
- The input (`MakeInput`): per train its mass, its engines' force and
  power, its resistance (Davis A, B, C summed over its vehicles: each
  vehicle's ORTSDavis, else its MSTS Friction fitted to Davis as Open
  Rails does, else the default for plain bearings), its driving: Physics
  when every vehicle has a mass and its engines a force or power, else
  Simple with a warning ("driven simply (no mass for ...)"). Every train
  on every local route has complete data.
- The rates (`SetRates`) as Open Rails sets them: a freight wagon makes a
  freight train (0.5 m/s² both ways); else 1.0, braking 1.5 above 40 m/s
  and 2.5 above 55 m/s of the train's top speed after its service's
  efficiency within the route's limit (not the consist's raw top speed:
  EUROPE1's trains braked at 2.5 instead of 1.5); an EMU (electric power
  cars with passengers at both ends) 1.5 and 2.0.
- The driving efficiency: the service's (Open Rails' AI efficiency), the
  player's train 0.9 (Open Rails forces it).

**Phase 2, the physics driver (done 2026-10-16).** `SimulationCore`,
for trains driven Physics (Simple trains unchanged):
- full-power acceleration: the engines' force, limited by their power
  (power / speed), less the resistance (A + B v + C v²) and the gradient
  (m g × the rise from the train's rear to its front over its length,
  from the geometry copy's heights), over the mass;
- starting away from rest, until within (9 − 6 × efficiency) × 0.5 m/s
  of its limit (Open Rails' accelerating state): half the target where
  full power gives that, else efficiency × half the target (Open Rails'
  floor: 0.375 m/s² for a passenger train at efficiency 0.75);
- else, running or within its braking state's reach of what it next
  stops at or slows for: full power, at least half the target;
- holding its speed in Open Rails' running band below the limit, at the
  band's middle ((4.5 − 3 × efficiency) / 4 m/s under it: 0.56 m/s at
  efficiency 0.75), easing down to it from above;
- fixtures: half the target and the floor, full power near a stop and
  its floor, up a 2 % grade (both rules), the braking curve and the
  creep, a red signal's creep, the same events from the same input.

**Phase 3, braking (done 2026-10-16).** As Open Rails' AI plans it, not
from brake forces (decision 3):
- the braking curve at 0.22 × the deceleration (Open Rails' allowance
  for the brakes' delay), held 0.75 m/s above it (its controller keeps
  the speed between half and one m/s over its ideal speed);
- the creep speed (2.5 m/s) at the end: at a station down to the stop;
  10 m short of a reversal, the path's end or a waiting point; 50 m
  short of a red signal and 30 m short of the authority's end, creeping
  on from there at half the creep speed (a train stops 30 m short of a
  red signal: Open Rails' 60 m from its approach point 20 m short of
  it); stopping from the creep speed at the full deceleration;
- lower limits ahead and signal speeds: the curve down to the limit 10 m
  before it;
- the braking state's reach: 1.5 × top speed² / deceleration before a
  station (Open Rails' trigger from full speed), the braking distance
  and 90 m before anything else.

**Phase 4, compared (done 2026-10-16).** The task 02 samples of Open
Rails (lean settings, 17 times real time), our events from the route
suite (`TSRE_SIM_EVENTS`), paired by distance (`scripts/orts-compare-
stops.py`); median arrival difference, TSRE minus Open Rails, over the
stops paired, and the median change between consecutive stops (one run):

| Route, activity | stops | task 02 | task 03 | per run |
|---|---|---|---|---|
| EUROPE1 `aftstorm` | 56 | −396 s | −14 s | −2 s |
| EUROPE2 `itr_e1_s1_w1_t1` | 51 | −200 s | −25 s | −10 s |
| JAPAN1 `itr_e1_s1_w1_t1` (12:00) | 249 | −1154 s | −287 s | −1 s |
| JAPAN1, the same from 10:30 | 328 | | +2 s | 0 s |
| JAPAN2 `itr_e1_s1_w1_t1` | 16 | −24 s | 0 s | +1 s |
| USA2 `itr_e1_s1_w1_t1` | 6 | −500 s | +79 s | |

JAPAN1's trains started before 12:00 are run in Open Rails' pre-run.
To tell its pre-run from its driving, the activity was run from 10:30 (a
copy in a scratch view of the route: symlinks and the changed activity,
the route untouched): those trains then match within seconds. Open
Rails' pre-run steps every 5 s without physics, moving the throttle and
brakes a step an update; it runs JAPAN1's stoppers 11 to 13 % slower than
its own normal running (about 18 s a stop), so at 12:00 they stand 3 to
10 minutes behind where its run from 10:30 has them. Ours run the pre-run
as they run (as designed): where Open Rails' run from an earlier start
would have them.

Fixed on the way (both task 01's): a train starting at its first station
stands its boarding time there as at any stop (it left at once when its
booked departure was its start); the consist's top speed as above.

## What Open Rails' AI does

The behaviour the driver follows (measured against the comparisons):
- **States:** accelerating (from rest), running, braking (within reach
  of its next action: a station, a stop signal, a lower limit, the
  path's end). From a station the next station is often within reach
  at once (1.5 × top speed² / deceleration, 940 m at 25 m/s): those runs
  are braking state throughout.
- **Accelerating state:** the throttle rises while the train gains less
  than half its target; at full throttle and less than that, the speed is
  raised by efficiency × half the target. Up to (9 − 6 × efficiency) ×
  0.5 m/s under the limit, then running.
- **Running and braking states:** below their bands the throttle rises
  20 % an update to full; at full throttle and less than half the target
  the speed is raised by half the target. In the band (running: 0.4 to
  0.75 m/s under the limit at efficiency 0.75; braking: around the ideal
  speed) the throttle eases.
- **The ideal speed when braking:** v = √(2 × 0.22 × D × (d − creep) +
  v²required), at least the creep speed (2.5 m/s; half that within the
  creep distance before a signal or the authority's end); above the
  limit it brakes and clamps to it.
- **The pre-run:** 5 s updates, no physics: speed changes by (throttle
  × target − brake × D) × efficiency; the throttle and brakes step per
  update.

## Not modelled

The throttle's steps and the bands' oscillation (their average is
taken); the slow last approach of an underpowered train to its limit
(Open Rails at 90 % throttle there, no floor); engines' force ramps
(diesels: 10 s to full force), adhesion, force curves, continuous force;
an engine's force falling to nothing above its own top speed (Open
Rails' floor then still raises the speed); Open Rails' pre-run slowness.

## Left

- **Open Rails' pre-run** (decision for the user): keep ours as its
  normal running (now), or slow our pre-run to match where Open Rails
  places those trains at the activity's start (its 5 s throttle and
  brake steps modelled: they cost its JAPAN1 stoppers about 18 s a stop).
- USA2: Open Rails shows 45 mph on stretches where our freight train
  holds 40 mph (speed post selection, to check); Open Rails holds a JAPAN1
  stopper about 3 minutes at Yurigaoka and Chitose Funabashi (overtakes:
  dispatching, task 02's area).

## Design

### 1. Driving per train

`TrainPlan::driving`: Simple (task 01's constant rates) or Physics; the
input chooses Physics where the data is complete, Simple otherwise with a
warning. Set per train in code (a test or a later editor setting can
choose either).

### 2. Data

`TrainPlan::performance`: mass (kg), force (N), power (W), Davis A, B,
C (N, N/(m/s), N/(m/s)²); `drivingEfficiency`; the rates as above.
Brake forces are not read (braking follows Open Rails' plan, decision 3).

### 3. The driver

As in phases 2 and 3 above; deterministic, exact over each step as task
01's driving (the braking curve solved per step), independent of the
frame rate. The pre-run uses the same driving as the run.

## Phases

1. **Data:** done.
2. **Physics driver:** done.
3. **Braking:** done.
4. **Compared:** done.

## Decisions (user, 2026-10-16)

"Yes to all"; this is AI trains, the player's train will be covered by a
separate and more advanced system.
1. **Default:** Physics for every train with complete data (the player's
   too, while it is driven as AI), Simple otherwise.
2. **Open Rails' acceleration floor** (efficiency × half the target at
   full throttle): kept for parity.
3. **Braking:** a per-train deceleration calibrated to Open Rails (its
   planned curve at 0.22 × its deceleration, its creep speeds), not brake
   systems.
4. **Order:** this task before `sigscr.dat` (task 04).
