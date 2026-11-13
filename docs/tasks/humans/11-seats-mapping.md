# Task humans 11 - Seats and doors mapping (brief)

For an agent working alone. A report and a staged set of `.sd` files: the
content (`~/ORTS`) is never written, no code changed. The user applies the
set after a look (extracting its zip into the game folder), as the lights'
were.

## Why
Task 09 ([09-inside-trains.md](09-inside-trains.md)) seats TSRE's people in
cars whose model's `.sd` has an `ESD_TSRESeats` block. The blocks are found
by `extra/car-seats/car_seats.py` and written only after a person checked
each car's plan (the user, 2026-11-13: a car needs its `.sd` upgraded;
seats are never guessed at load). The same block holds the car's doors
(`Door` lines, the user, 2026-11-13): TSRE takes them from there instead of
reading the car's door parts at load, and a car without door parts gets a
guess. So far only the EN57s and EN57ALs of OR_PSK_NEW have both. This task
maps every passenger car of our two content sets (which models the finder
gets right, which need seats or doors taken out, which it gets wrong or
misses, and why) and stages the `.sd` files for those it gets right, so
the user can apply them and decide what the finder must learn.

## Read first
- `extra/car-seats/README.md`: the block, the workflow, how to read a plan,
  the report format ("Mapping a trainset"), the limits.
- `extra/car-seats/car_seats.py`: run it, do not change it (if it fails on
  a model, report the model and the error). Its `write`, `doors` and
  `drop` only ever with `--stage <staging folder> --root <game root>`.

## The content
- **Open Rails**: `~/ORTS/OR_PSK_NEW/TRAINS/TRAINSET/` (all folders; the
  route PRUDNIK and its timetable `PSK_2026` use them).
- **MaSzyna**: `~/ORTS/maszyna/dynamic/` (scan with `--maszyna`).
- TSRE built: `build/TSRE5vc` in the checkout (pass `--tsre`).

Already written (2026-11-13), for comparison, not to be written again:
`PKP_EN57-OR` and `PKP_EN57A-OR` in OR_PSK_NEW (101 models with seats, 113
with doors), MaSzyna's `dynamic/pkp/en57_v2/lowpoly` interiors (75 with
seats). Their `.sd` blocks are numbered; `plan --sd` draws them. MaSzyna
doors are not written (TSRE reads them by the `.mmd`'s door prefix): for
MaSzyna, seats only.

## Steps
1. Scan OR_PSK_NEW's trainset (one scan of all its folders, or folder by
   folder) and MaSzyna's `dynamic` into a scratch folder (not the
   repository: there are thousands of plans), e.g.
   `~/humans-11/scan-or`, `~/humans-11/scan-maszyna`.
2. Locomotives, freight wagons and other cars without passengers show up
   as "no cushions" or "no floor": count them, do not look at them one by
   one.
3. Look at the plan of every model with seats found. Families share a
   layout; still look at each model's plan, at least quickly.
4. For passenger cars with no seats found, open a few of each family in the
   Shape Viewer (or look at the plan's outline) to tell "no interior
   modelled" from "the finder missed it".
5. Doors: on the same plans (magenta bars, D1...). Note passenger cars
   without door parts by family (TSRE guesses their doors), and doors
   found wrongly by number.
6. Stage, one staging folder a content set (`~/humans-11/stage-or` with
   `--root ~/ORTS/OR_PSK_NEW`, `~/humans-11/stage-maszyna` with `--root
   ~/ORTS/maszyna`), archives beside them
   (`--archive ~/humans-11/OR_PSK_NEW-sd-seats-<date>` and so on):
   `write --stage` the models whose verdict is right or drop (OR: seats and
   doors; MaSzyna: seats), `doors --stage` OR's passenger cars with door
   parts but no usable seats, then `drop --stage` the numbers your
   verdicts name, then `plan --sd` of each staged model (same `--stage`
   and `--root`) into
   `~/humans-11/plans-staged`. Leave out the EN57s and EN57ALs written
   already, and every model whose verdict is wrong.

## Deliverable
`docs/tasks/humans/reports/11-seats-mapping.md`:
- The commands run and how long the scans took.
- Per content set, the README's table (model or family, vehicle files,
  seats, verdict, notes), most used first; how often a family is used
  comes from the scan's vehicle files (for OR_PSK_NEW also from the
  timetable's consists if you can count them).
- The families without seats by reason, with counts.
- What the finder gets wrong, grouped, each with one example model and its
  plan's numbers: the rules to change, in a list the user can pick from.
- A few plans worth seeing (PNG) in `docs/tasks/humans/reports/11-plans/`,
  no more than ten.
- The staged set: where it is (`~/humans-11/`), its archives (`<name>.zip`
  to apply, `<name>-backup.zip` the originals), how many models with seats,
  with doors, new `.sd` files (models that had none), and the commands
  that made it.

## Not in this task
Writing into `~/ORTS` (the user applies the staged set), changing the
finder, the Shape Viewer function (later: finding and checking seats in the
viewer itself).
