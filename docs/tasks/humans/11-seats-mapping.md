# Task humans 11 - Seats mapping (brief)

For an agent working alone. Report only: no content files changed, no code
changed.

## Why
Task 09 ([09-inside-trains.md](09-inside-trains.md)) seats TSRE's people in
cars whose model's `.sd` has an `ESD_TSRESeats` block. The blocks are found
by `extra/car-seats/car_seats.py` and written only after a person checked
each car's plan (the user, 2026-11-13: a car needs its `.sd` upgraded;
seats are never guessed at load). So far only the EN57s have them. This
task maps every passenger car of our two content sets: which models the
finder gets right, which need seats taken out, which it gets wrong or
misses, and why, so the user can decide what to write next and what the
finder must learn.

## Read first
- `extra/car-seats/README.md`: the block, the workflow, how to read a plan,
  the report format ("Mapping a trainset"), the limits.
- `extra/car-seats/car_seats.py`: run it, do not change it (if it fails on
  a model, report the model and the error).

## The content
- **Open Rails**: `~/ORTS/OR_PSK_NEW/TRAINS/TRAINSET/` (all folders; the
  route PRUDNIK and its timetable `PSK_2026` use them).
- **MaSzyna**: `~/ORTS/maszyna/dynamic/` (scan with `--maszyna`).
- TSRE built: `build/TSRE5vc` in the checkout (pass `--tsre`).

Already written (2026-11-13), for comparison, not to be written again:
`PKP_EN57-OR` and `PKP_EN57A-OR` in OR_PSK_NEW (101 models), MaSzyna's
`dynamic/pkp/en57_v2/lowpoly` interiors (75). Their `.sd` blocks are
numbered; `plan --sd` draws them.

## Steps
1. Scan OR_PSK_NEW's trainset (one scan of all its folders, or folder by
   folder) and MaSzyna's `dynamic` into a scratch folder (not the
   repository: there are thousands of plans).
2. Locomotives, freight wagons and other cars without passengers show up
   as "no cushions" or "no floor": count them, do not look at them one by
   one.
3. Look at the plan of every model with seats found. Families share a
   layout; still look at each model's plan, at least quickly.
4. For passenger cars with no seats found, open a few of each family in the
   Shape Viewer (or look at the plan's outline) to tell "no interior
   modelled" from "the finder missed it".

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

## Not in this task
Writing `.sd` files, changing the finder, the Shape Viewer function (later:
finding and checking seats in the viewer itself).
