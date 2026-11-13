# Car seats for TSRE's people

TSRE's people board a train, walk along the car's aisle to a free seat near
their door and sit down; those getting off stand up and walk out. A car has
seats once its model's `.sd` file lists them in an `ESD_TSRESeats` block.
TSRE never guesses them from the model while it runs: a wrong guess (a
seat in the cab, a toilet, a table, people walking through a compartment's
wall) would show in every game with nobody to correct it. A car without the
block keeps the plain boarding: people walk in at the door and are gone.

`car_seats.py` finds the seats in a car's model, draws plans to check them,
writes the `.sd` files with backups, and takes out seats found wrongly. This
guide is the workflow, for a content maker or an agent mapping a whole
trainset.

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

A glTF model holds the same in its default scene's `extras`:
`"TSRE_seats": {"aisles": [[x, y, z0, z1]], "passages": [[x, y, z,
width]], "seats": [[x, y, z, heading]]}` (seats numbered by their place in
the list, from 1).

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
files use them; those without, by why), `found.json` (everything found)
and `plans/`.

### 2. Check every plan
A plan is a top view of the car: along it left to right, across it up and
down.

- Grey: cushions (upward faces at seat height). Dark outlines: walls,
  partitions, backrests.
- Cyan: the aisle. Yellow: passages.
- Green dots: seats, with a tick the way the sitter faces and their number
  in red behind them.

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
- A car with no plan or no seats: see the report's reason. "no floor along
  the aisle" or "no cushions" on a car you know has seats means the finder
  failed on it: report it (with the model), do not write it.

### 3. Write
For the models whose plans are right:

```
python3 -I car_seats.py write <model> ... --root <game root> --archive <path>/<name> --plans <folder> [--readme README.md]
```

- Each model's `.sd` gets the block; its other lines are kept. A model
  without an `.sd` gets a new one (for an `.s`: the route's header,
  `ESD_Detail_Level ( 0 )`, `ESD_Alternative_Texture ( 0 )`; for an
  `.e3d`: the block only). UTF-16 with BOM and CRLF, as MSTS writes them.
- `<name>-backup.zip` holds the `.sd` files as they were before, by their
  paths under `--root` (extract it into the root to undo); `<name>.zip`
  the written ones (to give to others).
- `--plans`: a numbered plan of each written `.sd`, drawn from the file.
- Cars with fewer than `--min-seats` (8) seats are left out.

### 4. Take out wrong seats
By the numbers on the plan:

```
python3 -I car_seats.py drop <model> 12 13 40 --root <game root> --archive <path>/<name>-drop
python3 -I car_seats.py plan <model> --sd --out <folder>
```

The other seats keep their numbers. `plan --sd` draws the `.sd` as it is
now.

### 5. See them in TSRE
- Shape Viewer: open the model, 3D View > Seats: the aisle, passages and
  seats drawn over it. Look from inside the car (the roof hides them).
- In a run with people at stations, `TSRE_PEOPLE_LOG=1` logs each second
  how many people are in cars, sitting, and how many cars have seats, and
  each car as it is filled.

## Mapping a trainset (for an agent)
Deliverable: a report, no content changed unless asked.

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

   Verdicts: **right**, **drop** (the numbers), **wrong** (why; leave out),
   **none** (the report's reason, and whether the car has seats you can
   see).
4. List the families with no seats found by reason (no interior modelled,
   seats painted on flat panels, compartments, the finder failed) and how
   often they are used, so the next fixes go where they matter.

## Limits
- Aisles are straight lines along the car; people go sideways from the
  aisle to a seat. Compartment cars, seats behind tables and double-deck
  cars (stairs) are not handled.
- A seat is a cushion with a backrest above it: a toilet or a berth against
  a wall can pass. Check the plans.
- The car's floor is the height with the most upward area inside the body;
  the cushions 0.33 to 0.58 m above it, of the car's usual height (a cab's
  higher seats are left out).
- Seats are found in the model's most detailed level only.
