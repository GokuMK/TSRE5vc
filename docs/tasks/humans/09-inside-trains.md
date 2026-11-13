# 09 - Inside trains (design)

Status: design, decisions in (2026-11-13). Building waits for the user's go.

## The user's brief (2026-11-13)
- The upgrade: find a car's sitting places, define them and store them
  (in the `.sd`?); find out how people reach them. Removing static people
  is an upgrade too.
- Not every tester has MaSzyna's files: a candidate from PRUDNIK's own
  stock as well as one from MaSzyna.

## The scan
### MaSzyna (`dynamic/`, 1461 `.mmd`)
Each `.mmd` read with its include's parameters put in. A passenger car
has a passengers model (`loads: { passengers: ... }`) or door places
(`locations: doors:`); locomotives have a low-poly interior too (their cab).

| Family | `.mmd` | Interior | Passengers model | Door parts | Step parts | Model (largest) |
|---|---|---|---|---|---|---|
| `pkp/en57_v1` | 134 | 134 | 133 | 134 | 0 | 4.5 MB |
| `pkp/en57_v2` | 109 | 109 | 109 | 109 | 0 | 12.7 MB |
| `pkp/impuls_v1` | 67 | 67 | 67 | 67 | 62 | 8.8 MB |
| `pkp/11xa_v2` (111A, 112A) | 57 | 56 | 28 | 56 | 0 | 4.4 MB |
| `pkp/yb_v1` | 45 | 45 | 45 | 45 | 0 | 15.5 MB |
| `pkp/elf_v1` | 41 | 41 | 35 | 41 | 35 | - |
| `pkp/en57al_v1` | 33 | 33 | 33 | 33 | 0 | 3.8 MB |
| `pkp/en57-2000_v1` | 24 | 24 | 24 | 24 | 0 | 2.2 MB |
| `pkp/z2_v1` | 20 | 20 | 20 | 20 | 20 | 5.1 MB |
| `pkp/120a_v1` | 15 | 15 | 15 | 15 | 0 | 9.7 MB |

Then `14xa`, `wlab`, `113aa`, `16xa`, `134a`, `bdhpumn`, `bhp`, `110a`,
`sa134` and others with fewer than 15 each.

- The interior people would be seen in is the `lowpolyinterior:` model
  (`lowpoly/int_en57-1609s.e3d`: 1.4 MB, 25 515 triangles), one mesh
  (parts `main`, `corridor01`, `detale`, `szyby`): no seat parts.
- The passengers model (`loads/int_en57ra_passengers.e3d`, about 3 MB) is
  MaSzyna's static people. TSRE skips `loads:` (`MaszynaVehicle.cpp`) and
  draws the low-poly interior as an extra part, so in TSRE the static
  people are already gone.

### PRUDNIK (`OR_PSK_NEW`, timetable PSK_2026)
The vehicles of the timetable's consists, counted by the trains using
them. The EN57s are multiple units typed `Engine` without
`PassengerCapacity`.

| Family | Train uses | Interior (most used shape) |
|---|---|---|
| `PKP_EN57-OR` | 147 | Two models, below |
| `PKP_111A-OR` | 95 | Coarse: 8 400 triangles, 3 levels, few seats found |
| `PKOR_Elf1` | 72 | 44 000 triangles; seats found, aisle not |
| `PKP_EN57A-OR` | 66 | The newer EN57 model, below |
| `PKP_112A-OR` | 49 | Coarse, as 111A |
| `PKOR_ED160` | 48 | 144 000 triangles, 2+2 seats; aisle not found yet |
| `PKP_ED250-OR` | 42 | None (252 triangles inside) |
| `PKOR_Impuls1` | 26 | 76 000 triangles, 20 seat groups, aisle 0.71 m |

The EN57s come in two models:
- **Older** (`PKP_`, `PKPPR_`, `KMKOL_`, `SKMW_`: 1192, 1310, 1326, 687,
  ...; about 40 shapes, about 120 train uses): a floor plate at 1.2 m, no
  seats.
- **Newer** (`PREG_`, `KSL_`: 1018, 815, 1531, 1602, 1730, 857, 730;
  about 30 shapes, about 90 train uses): a full interior, seats as their
  own matrices (`SIEDZENIAB`). A few variants of it (24 to 38 seat groups a
  car), so seats are found for each shape, not copied.

None of PRUDNIK's 232 passenger shapes has a part or texture named for
people: no static people to remove there.

### Seats found from the geometry
The most detailed level's triangles in the shape's space; the floor is the
height with the most upward-facing area inside the body; cushions are
upward faces 0.33 to 0.58 m above it, grouped by side and along the car.
The same code on both formats (`.s` through TSRE's converter, `.e3d` by
its records, as `E3dFile`).

| EN57 middle car | Floor above rail | Cushions above floor | Seat groups | Aisle |
|---|---|---|---|---|
| PRUDNIK `PREG_EN57-1018s.s` | 1.16 m | 0.38 m | 38 | 0.88 m |
| MaSzyna `int_en57-1609s.e3d` | 1.12 m | 0.46 m | 34 | 0.79 m |

Benches in bays: facing pairs about 0.5 m apart at the cushions' fronts,
back to back with the next pair.

### How people would reach them
- The floor is level from end to end in both, vestibules included.
- Bench backs leave the aisle free.
- The partitions between vestibule and saloon (z about ±3.7 and ±5.4 m in
  the middle car) have their doors modelled closed, with no parts (in
  `MAIN` in PRUDNIK's). A walk area built from the geometry would wall
  people into the vestibule.

## Candidates
- **MaSzyna: `pkp/en57_v2`**: 109 variants, the most detailed EN57, its
  interiors per number (`lowpoly/int_en57-NNNN{ra,rb,s}.e3d`); TSRE already
  boards people through its door parts (task 06).
- **PRUDNIK: the newer EN57 model**: about 30 shapes, about 90 of
  PSK_2026's train uses; the 12:11 train at Raszowa (task 05's measuring
  case) is one.
- The same train in both: one comparison for the look and the counts.

## Proposal
### The store: the `.sd`
As shapes task 18 decided: an `.s` has its `.sd`, an `.e3d` the `.sd`
beside it, glTF its extras. For a MaSzyna car, the `.sd` beside its
interior model (shared by every car using it). For PRUDNIK, an `.sd` per
shape (each livery has its own `.s`).

```
ESD_TSRESeats (
	Aisle ( 0.0 1.16 -10.6 10.6 )
	Passage ( 0.0 1.16 -5.4 0.7 )
	Seat ( 0.64 1.54 -9.77 180 )
	Seat ( 0.64 1.54 -9.32 180 )
)
```

- `Aisle ( x y z0 z1 )` (added in step 2, so the way through the car comes
  from the `.sd` too, never from the model at load): a straight way along
  the car, across at x, the floor at y, from z0 to z1. Seats without an
  aisle are not used (told).

- `Seat ( x y z heading )`: a place, in the shape's space (as
  `ESD_TSRELight`'s positions): the cushion's top in the middle of its
  front half; the heading the sitter faces (degrees, 0 = +z). A two-seat
  bench is two seats.
- `Passage ( x y z width )`: a closed door people walk through (the
  vestibule's partitions).

### Finding them
One finder in TSRE (C++), the method above plus each seat's facing (from
the backrest: the vertical face along the cushion's back edge) and its
places (about 0.45 m each). Used by a Shape Viewer command (find, show,
write to the `.sd`) and by a mass scan of a route's or MaSzyna's cars, as
the lamps' (assets 05).

### Reaching them
Inside a car, a path in the car's space instead of a walk area: from the
door (`CarDoors`) to its vestibule (the door's z, the aisle's x), through
the passage, along the aisle (between the left and right cushions), to the
bay's row, to the seat. People queue in the aisle behind the one ahead; no
avoidance beyond that. A walk area (Recast, as at stations) is not
proposed: 0.8 m aisles, the closed partition doors, and one per car.

Through the closed partition doors: people walk through them (the doors
are glass and seen from outside). Hiding a door while someone passes needs
its triangles (no parts): later, if it shows.

### Seen
People inside are drawn with the car (its matrix each frame), in the same
crowd, sitting. How many from the simulation: those who boarded, less
those getting off. Only near the camera (a station, riding); farther, only
counts.

Sitting needs clips: sitting down, sitting, standing up (humans-gen, made
as the steps were; task 08's benches need them too).

### Static people
MaSzyna's: not drawn by TSRE now. If TSRE ever draws `loads:`, a car with
seats skips its passengers model. PRUDNIK's: none found.

## Decisions (the user, 2026-11-13)
1. **Seats only from the `.sd`.** Finding them at load risks mismatches: a
   car needs its `.sd` upgraded. A car without `ESD_TSRESeats` keeps task
   06's boarding (people walk in to the track's middle and vanish).
2. **The mass scan writes the `.sd` files**, OR_PSK_NEW's cars' and new
   ones beside MaSzyna's interior models, with archives as the lights'
   (assets 05): the written `.sd` files and a backup of the originals,
   zipped in `~/ORTS` to extract into the game root. An edited `.sd` keeps
   its own lines with the block added; UTF-16 with BOM and CRLF, as the
   route's.
3. **People walk through the closed partition doors** for now.
4. **Sitting clips**: we have them, not yet in our packs. Quaternius UAL1
   (CC0, the mannequin of our other clips): `Sitting_Enter` (1.30 s),
   `Sitting_Idle_Loop` (1.67 s), `Sitting_Talking_Loop` (2.93 s),
   `Sitting_Exit` (1.03 s). Added to `clips.json` as `sit_down`, `sitting`,
   `sitting_talking` and `stand_up`, retargeted as the others, and the
   packs made again (full and basic; appdata's basic set with them).
   - Sitting, the mannequin's pelvis is 0.335 m lower than standing and
     0.245 m further back (standing 0.917 m): a seat about 0.43 to 0.45 m
     high, scaled for each human by its pelvis height.
   - `sit_down` starts standing 0.245 m in front of the seat; `stand_up`
     ends there.
   - A lower seat (PRUDNIK's EN57: 0.38 m): the person sits on the cushion
     and the feet go a few centimetres into the floor, below the windows
     seen from outside.

## Steps (on the user's go)
1. **Clips** (done): the sitting clips in humans-gen, the packs made again
   (basic 2.48 MB); in TSRE, sitting clips are never idles or walks (as
   `steps_*`).
2. **`ESD_TSRESeats`** (done): `ShapeSeats` reads it (with the other `.sd`
   blocks in `ShapeMetadata`), glTF extras `TSRE_seats` (`{"aisles": [[x,
   y, z0, z1]], "passages": [[x, y, z, width]], "seats": [[x, y, z,
   heading]]}`); an S file's mirrored in x where the car is drawn
   (`ShapeSeats::drawn`); station-people +5.
3. **The finder and the scan**: `scripts/car_seats.py` (this scan's code):
   seats with their facing and places, passages; writes the blocks and the
   archives. The Shape Viewer shows a shape's seats and passages to check
   them.
4. **People inside**: from the door along the car's path to a seat, sitting
   down; drawn with the car; those getting off stand up and walk out first;
   counts from the simulation.
5. **The candidates upgraded**: MaSzyna's `en57_v2` interiors and PRUDNIK's
   newer EN57 shapes; checked at Raszowa (the measuring case, 12:11 train).
