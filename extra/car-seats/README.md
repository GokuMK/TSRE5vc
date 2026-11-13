# Car seats and doors for TSRE's people

TSRE's people board a train, walk along the car's aisle to a free seat near
their door and sit down; those getting off stand up and walk out. A car has
seats once its model's `.sd` file lists them in an `ESD_TSRESeats` block.
TSRE never guesses them from the model while it runs: a wrong guess (a
seat in the cab, a toilet, a table, people walking through a compartment's
wall) would show in every game with nobody to correct it. A car without the
block keeps the plain boarding: people walk in at the door and are gone.

Where they get in and out, its doors, the same block may list too. TSRE
takes them from there when written; else it reads the car's door parts
(`DOOR_A`..`DOOR_F`) each time the car is first seen, and a car without
door parts gets a guess (a door near each end, the floor 1.1 m above the
rail). Written, they are checked on the plan once and cost nothing at load.

`car_seats.py` finds the seats and doors in a car's model, draws plans to
check them, writes the `.sd` files with backups (into the content or into a
staging folder), and takes out seats and doors found wrongly. This guide is
the workflow, for a content maker or an agent mapping a whole trainset.

## What you need
- Python 3 (no packages).
- For MSTS/Open Rails `.s` models: a TSRE build. The script runs
  `TSRE5vc --shapeconv` to read a shape (`--tsre <path to TSRE5vc>`; by
  default the `build` folder of the checkout the script is in). MaSzyna
  `.e3d` models are read directly; `.t3d` text models are not read.
- To look at plans as pictures: a browser (they are SVG), or ImageMagick
  (`magick plan.svg plan.png`).

## The block
In the model's `.sd` (beside `car.s`: `car.sd`; beside a MaSzyna
`int_car.e3d`: `int_car.sd`), inside its `Shape ( ... )`:

```
ESD_TSRESeats (
    Aisle ( 0 1.16 -11.25 11.25 )
    Passage ( 0 1.16 -5.4 0.62 )
    Seat 1 ( -1.07 1.658 -10.358 0 )
    Seat 2 ( -0.57 1.658 -10.358 0 )
    Door 1 ( -1.357 1.144 -4.517 1.301 )
    Door 2 ( 1.357 1.144 -4.524 1.301 )
)
```

All in metres, in the model's space as written in its file (an S file's
before its root matrix mirrors it, as the lights' positions in
`ESD_TSRELight`; a MaSzyna model's own).

| Line | Values | Meaning |
|---|---|---|
| `Aisle` | x y z0 z1 | A straight way along the car (z0 to z1) at x, its floor at y. Seats without an aisle are not used. A car may have several. |
| `Passage` | x y z width | A closed door across an aisle (a partition's). People walk through it for now; kept for doors that open later. |
| `Seat n` | x y z heading | A place: the top of its cushion in the middle; the way the sitter faces in degrees (0 faces +z, 90 +x). `n` is its number, as on the plans; a seat without one is read too. |
| `Door n` | x y z width | A door people use: its outer face across at x (its side: in an S file as written, -x is the car's left, the side of `DOOR_D`..`DOOR_F`), the floor at its threshold y, its middle along the car z, its width. `n` as a seat's. A block may hold doors only. |

A glTF model holds the same in its default scene's `extras`:
`"TSRE_seats": {"aisles": [[x, y, z0, z1]], "passages": [[x, y, z,
width]], "seats": [[x, y, z, heading]], "doors": [[x, y, z, width]]}`
(seats and doors numbered by their place in the list, from 1).

Doors are found as TSRE finds them at load: the parts named `DOOR_A` to
`DOOR_F` and the parts under them (A..C one side, D..F the other), each
side's triangles split along the car where they leave more than 0.35 m
between them; pieces 0.4 to 4.5 m wide are doors. MaSzyna models' doors are
not written: TSRE reads them by the `.mmd`'s door prefix (`animdoorprefix`),
whose parts are often in other models attached to the car.

How people use them: one who has stepped in walks across to the aisle, along
it to the row of a free seat among the six nearest the door, sideways to
stand in front of it, turns and sits. Nothing on that path is an obstacle,
so the path must be clear in the model: bays of facing benches beside a
straight aisle work; see "Limits".

## The workflow

### 1. Scan
Every car of the vehicle folders, each model read once (about a second
each, spread over all cores):

```
python3 -I car_seats.py scan <game>/TRAINS/TRAINSET/<folder> ... --out <scan folder>
python3 -I car_seats.py scan <MaSzyna>/dynamic/<folder> ... --maszyna --out <scan folder>
```

An `.eng`/`.wag` gives its `WagonShape` and its freight animations' shapes
(`FreightAnim`, `FreightAnimStatic`: a car's interior may be a shape of its
own; TSRE reads the seats of all of them, each moved by its `Offset`). A
MaSzyna `.mmd` gives its model, its `lowpolyinterior:` and its
`attachments:`. Models named for a cab are left out.

The scan folder gets `report.md` (the models with seats, by how many vehicle
files use them, with their doors; those with doors but no seats; those
without, by why), `found.json` (everything found) and `plans/`.

### 2. Check every plan
A plan is a top view of the car: along it left to right, across it up and
down.

- Grey: cushions (upward faces at seat height). Dark outlines: walls,
  partitions, backrests.
- Cyan: the aisle. Yellow: passages.
- Green dots: seats, with a tick the way the sitter faces and their number
  in red behind them.
- Magenta bars on the car's sides: doors, numbered D1, D2... beside them.

What to look for:

- **Seats where there are none**: a cab's seat (a driver's console at
  cushion height), a toilet, a table, a heater box or ledge along the wall,
  a luggage shelf, sleeping berths. Note their numbers.
- **Seats facing the wrong way** (into their own backrest), or two seats on
  one place (a cushion modelled twice).
- **The aisle**: it must run where people can walk, from end to end. A
  compartment car (a corridor along one side) gets an aisle through the
  compartments: not usable, leave the car out.
- **Missed seats**: benches without dots. Usually harmless (fewer places).
- **Doors**: one bar per door opening, on the car's side, where the plan's
  outline has its vestibules; none on a cab's door or a closed partition.
  A bar much wider than a door (two doors joined) or a door missing on one
  side: note its number.
- A car with no plan or no seats: see the report's reason. "no floor along
  the aisle" or "no cushions" on a car you know has seats means the finder
  failed on it: report it (with the model), do not write it.

### 3. Write
For the models whose plans are right:

```
python3 -I car_seats.py write <model> ... --root <game root> --archive <path>/<name> --plans <folder> [--readme README.md] [--stage <folder>]
```

- Each model's `.sd` gets the block (seats and doors); its other lines are
  kept. A model without an `.sd` gets a new one (for an `.s`: the route's
  header, `ESD_Detail_Level ( 0 )`, `ESD_Alternative_Texture ( 0 )`; for an
  `.e3d`: the block only). UTF-16 with BOM and CRLF, as MSTS writes them.
- `<name>-backup.zip` holds the `.sd` files as they were before, by their
  paths under `--root` (extract it into the root to undo); `<name>.zip`
  the written ones (to give to others, or to apply: extract into the root).
- `--plans`: a numbered plan of each written `.sd`, drawn from the file.
- Cars with fewer than `--min-seats` (8) seats get no seats: their doors
  only, if they have door parts; without either they are left out.
- `--stage <folder>`: nothing is written into the content. Each `.sd` goes
  to the folder by its path under `--root`; the archives are made as
  above (the backup from the content). `drop` and `plan --sd` with the same
  `--stage` work on the staged files. The set is applied by extracting
  `<name>.zip` into the game root.

Only doors (a model whose seats are written already, or one without
seats):

```
python3 -I car_seats.py doors <model> ... --root <game root> --archive <path>/<name> --plans <folder> [--stage <folder>]
```

Its block's Door lines are replaced by those found, its other lines kept; a
block (and an `.sd`) is made where there is none. Models without door parts
are left as they are.

### 4. Take out wrong seats and doors
By the numbers on the plan (doors with a `d`):

```
python3 -I car_seats.py drop <model> 12 13 40 d3 --root <game root> --archive <path>/<name>-drop [--stage <folder>]
python3 -I car_seats.py plan <model> --sd --out <folder> [--root <game root> --stage <folder>]
```

The others keep their numbers. `plan --sd` draws the `.sd` as it is now
(the staged one with `--stage`).

### 5. See them in TSRE
- Shape Viewer: open the model, 3D View > Seats: the aisle, passages,
  seats and doors (magenta frames) drawn over it. Look from inside the car
  (the roof hides them).
- In a run with people at stations, `TSRE_PEOPLE_LOG=1` logs each second
  how many people are in cars, sitting, and how many cars have seats, each
  car as it is filled, and each car's doors with where they came from
  (`.sd`, or its door parts).

## Mapping a trainset (for an agent)
Deliverable: a report and a staged set (`--stage`), the content never
written.

1. `scan` the folders given.
2. Look at every plan in `report.md`'s "Seats found" (convert to PNG if your
   viewer needs it). Cars of one model family often share a layout: check
   each model anyway, they differ in details.
3. Report, per model or per family where the plans are alike:

   | Model (or family) | Vehicle files | Seats | Verdict | Notes |
   |---|---|---|---|---|
   | `<folder>/<car>.s` | 2 | 76 | right | |
   | `<folder>/<car>.s` | 1 | 58 | drop 57, 58 | a bench in the cab's back room |
   | `<folder>/<car>.s` | 4 | 40 | wrong | compartment car: aisle through the compartments |

   Verdicts: **right**, **drop** (the numbers; doors as `d3`), **wrong**
   (why; leave out), **none** (the report's reason, and whether the car
   has seats you can see). Doors are checked on the same plan.
4. List the families with no seats found by reason (no interior modelled,
   seats painted on flat panels, compartments, the finder failed) and how
   often they are used, so the next fixes go where they matter; and the
   passenger cars without door parts (TSRE guesses their doors).
5. Stage the set: `write --stage` the models whose plans are right (seats
   and doors), `doors --stage` the passenger cars with door parts but no
   usable seats, then `drop --stage` the numbers your verdicts name. Look
   at the staged plans (`--plans`) once more.

## Limits
- Aisles are straight lines along the car; people go sideways from the
  aisle to a seat. Compartment cars, seats behind tables and double-deck
  cars (stairs) are not handled.
- A seat is a cushion with a backrest above it: a toilet or a berth against
  a wall can pass. Check the plans.
- The car's floor is the height with the most upward area inside the body;
  the cushions 0.33 to 0.58 m above it, of the car's usual height (a cab's
  higher seats are left out).
- Seats and doors are found in the model's most detailed level only.
- Doors only from door parts: a car whose doors are part of its body (no
  `DOOR_` parts) gets none written; TSRE guesses them.
