# Train comparison runs (task driving 10)

The same train driven the same way in TSRE and in another simulator, side by
side: MaSzyna's own mover (`maszyna-mover/`, built from a MaSzyna source
checkout) for MaSzyna vehicles, Open Rails (`scripts/orts-drive-run.sh`) for
ENG ones.

## A scenario (`scenarios/*.json`)

- `name`, `description`.
- `lineVoltage`: volts at the pantographs (MaSzyna electrics).
- `track`: `length` m of straight track, `grade` per mille (+ up the way
  the train runs).
- `consist`: vehicles front to back; `folder` under MaSzyna's `dynamic/`,
  `fiz` its `.fiz` (MaSzyna's mover), `tsre` its name in TSRE
  (`MaszynaVehicles::definition`), `count` repeats (default 1), `flipped`.
  The first is the driven loco, its front cab occupied.
- `startSpeed`: km/h at the start (default 0). Everything hot started: the
  loco prepared as MaSzyna's driver leaves it (battery, pantographs up, main
  switch, converter, compressor), pipes charged to the driver's valve's
  running pressure, main reservoirs full.
- `commands`: at `t` seconds, one of (MaSzyna's own terms; TSRE takes them
  through the MaSzyna cab's controls, `maszyna.cab`):
  - `direction`: -1, 0, 1;
  - `mainController`: a position, or `{ "to": n, "every": s }` one
    position at a time;
  - `secondController`: a position, or a ramp as above;
  - `trainBrakePosition`: the driver's valve's position (the `.fiz` Cntrl
    rows: FV4a -1 fill, 0 running, 1-5 service, 6 emergency);
  - `localBrakePosition`: 0 to 10.
- `acknowledge`: every that many seconds the driver acknowledges the
  vigilance device (MaSzyna's `SecuritySystem`, TSRE's `alerter` command),
  a press and a release; default 20, 0 never.
- `duration` s, `sample` s between rows.

## The CSV (one row a sample, both sides the same columns)

`t` s, `v` km/h, `s` m, `main` controller position, `actual` the relay's
position, `second` position, `im` A (one motor), `force` kN (the loco's
traction, + forwards), `brakeForce` kN (the whole train's braking at the
rails), `pipe` bar (the brake pipe at the loco), `pipeLast` bar (at the last
car), `cyl` bar (the loco's cylinders), `cylFirst` bar (the first car
behind it), `cylLast` bar (the last car), `mr` bar (the loco's main
reservoir), `er` bar (the driver's valve's equalising or control chamber).
A value a side does not have: empty.
