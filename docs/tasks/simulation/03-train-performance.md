# Task 03 - Train Performance: Force and Mass

Status: designed 2026-10-16; decisions open (at the end).

## Why

The Open Rails comparisons of task 02 (phase 7) found driving the largest
difference left on every local route: our trains accelerate and brake at
a constant 1.0 m/s², Open Rails' AI passenger trains at about 0.4 m/s²
and brake at 0.2 to 0.5, so ours gain about 30 s on each run between
stops. Where stops are close they pull far ahead (JAPAN1's Odakyu
stoppers by 10 to 30 minutes over an hour or two); on long runs they stay
close (JAPAN2's player within 25 s for two hours).

The user (2026-10-16): users expect Open Rails parity. "No physics" in
task 01 meant no full physics (brake systems, adhesion, the player's
controls: that is for the player's driving). A simple calculation of
force and mass is in for AI trains, and the driving is chosen per train
in code: simple acceleration and braking, or basic physics.

## What Open Rails' AI does

A behavioural reference (`AITrain`, `Train.physicsUpdate`, `MSTSWagon`,
`MSTSLocomotive`); no code taken.

- **Targets:** passenger trains accelerate to 1.0 m/s² and plan braking
  at 1.0 (1.5 above 40 m/s, 2.5 above 55 m/s); freight trains 0.5 both
  ways (Open Rails sets the freight deceleration from the acceleration
  value).
- **Accelerating:** while the train gains less than half its target, the
  throttle rises 10 % an update (40 % in the pre-run). The locomotives'
  physics give the force: tractive effort limited by the engine's force
  and power, less rolling resistance and the gradient, over the train's
  mass. At full throttle, if that still gives less than half the target,
  the speed is raised by Efficiency × 0.5 × target instead: 0.375 m/s²
  for a passenger train at efficiency 0.75, which is what we measured
  (0.38 to 0.45).
- **Holding speed:** throttle and brake in bands around the allowed
  speed.
- **Braking:** where to brake comes from the deceleration target; the
  brake rises in steps and the cars' brakes give the force; at full
  brake the speed is lowered by the target instead. Measured: 0.2 to 0.5
  m/s².
- **The pre-run** (trains started before the activity): no physics; the
  speed changes by throttle × target × efficiency.
- **Data:** each vehicle's mass; the engines' MaxForce, MaxPower,
  MaxVelocity (and Open Rails' force curves); resistance from
  ORTSDavis_A, _B, _C, else the MSTS Friction block, else values from the
  bearing type, axles, frontal area and load; brake forces per car.

## Design

### 1. Driving per train

`TrainPlan` gets a driving mode, set per train in code:
- **Simple:** task 01's constant acceleration and braking (as now).
- **Physics:** this task.

The input chooses Physics for a train whose data is complete (every
vehicle's mass, the engines' force or power) and Simple otherwise, with a
warning saying what was missing.

### 2. Data (our own parsers)

From `.eng` and `.wag` through TSRE's `Eng` (which already reads mass,
MaxForce, MaxPower, MaxVelocity): add ORTSDavis_A, _B, _C, the MSTS
Friction block (converted to Davis as Open Rails does), MaxBrakeForce and
the engine type. The input sums them per train: mass, the locomotives'
force and power, resistance coefficients, brake force.

### 3. The physics driver

- **Tractive force:** throttle × min(force, power / speed), the throttle
  rising at a fixed rate per second (Open Rails raises it per frame, so its
  own rise depends on the frame rate; ours must not, for repeatable runs).
- **Resistance:** A + B v + C v², summed over the vehicles.
- **Gradient:** m g × the grade under the train (the track's pitch,
  which `TrackGeometry` has, averaged over the train's length).
- **Acceleration:** (force − resistance − gradient) / mass, at most the
  target; at full throttle at least Open Rails' floor (Efficiency × 0.5 ×
  target).
- **Braking:** the existing exact braking curve, with a deceleration per
  train from its brake force, the target and the gradient (calibrated
  against Open Rails' measured 0.2 to 0.5).
- **The pre-run** uses the same driving as the run (unlike Open Rails),
  so trains started long before an activity can still differ from Open
  Rails'.

### 4. Tests and measures

Fixtures on hand-made track: a train with a known force, power and mass
accelerates as (F − R) / m and no faster than its target; up a 2 % grade
more slowly; Open Rails' floor; braking distances; a Simple train
unchanged; the same events from the same input. Then the comparisons
with Open Rails again on every local route (`scripts/orts-simulation-
compare.sh`, `scripts/orts-compare-stops.py`): stops within a minute or
two of Open Rails' where they now differ by many.

## Phases

1. **Data:** the fields read; per-train mass, force, power, resistance,
   brake force in the input; the mode chosen, warnings.
2. **Physics driver:** acceleration, resistance, gradient, the floor; the
   mode switch; fixtures.
3. **Braking:** a deceleration per train, calibrated against Open Rails.
4. **Compared:** every local route against Open Rails; the doc.

## Decisions for the user

1. **Default:** Physics for every train with complete data (the player's
   too, while it is driven as AI), Simple otherwise? Recommended.
2. **Open Rails' acceleration floor** (Efficiency × 0.5 × target at full
   throttle): keep it for parity? Recommended: underpowered trains in
   Open Rails do not crawl, and our numbers match it.
3. **Braking:** the deceleration from brake force, target and gradient,
   calibrated to Open Rails' measurements in phase 3? Recommended
   (modelling Open Rails' brake steps and per-car brake forces would be
   the brake systems the user left for the player's driving).
4. **Order:** this task before `sigscr.dat` (now task 04)? Recommended:
   driving is the largest difference on every route; the aspect rule
   matters where aspects carry speeds (the US routes).
