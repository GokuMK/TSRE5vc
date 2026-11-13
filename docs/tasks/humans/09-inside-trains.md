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
  ...; about 40 shapes, about 120 train uses): a floor plate at 1.16 m
  and simple seat blocks 0.50 m above it (first read as no seats: the
  finder's floor rule, corrected below).
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
3. **The finder and the scan** (done): `scripts/car_seats.py` (since moved
   to `extra/car-seats/`, below):
   - `find` (a summary), `plan` (an SVG top view: cushions, backrests, the
     aisle, passages, seats with their facing; turned to PNG with
     ImageMagick, the check used here), `write` (the `.sd` blocks and the
     archives).
   - A bench's cushions must be of the car's usual height (a cab's seats are
     higher in PRUDNIK's EN57), and none in parts named for a cab (`cab`,
     `kabin`, `maszynist`, `mechanik`, `driver`): MaSzyna's interior holds its
     cab's seats.
   - The Shape Viewer's 3D View > Seats draws a shape's aisle (cyan),
     passages (yellow) and seats (green, a tick for the facing) over it, depth
     tested: seen from inside the car or through its doors.
   - Found here: PRUDNIK's newer EN57 cars 30 to 76 seats each (the middle
     car 76, as MaSzyna's), the modernised `rb` 30 (its multi-purpose area and
     toilet take half the car); the older model none (wrongly: below);
     EN57AL 2103/2112/2117 not taken (single seats, not benches).
4. **People inside** (done):
   - Riders: people in a car with seats, in the car's space, placed each
     frame where the car was drawn (`Eng::lastDrawn`); the car's seats read on
     a worker (a MaSzyna car's from its interior model).
   - Boarding: one who has stepped in through its door walks along the
     aisle to a free seat among the six nearest its door, in front of it,
     turns, sits down (`sit_down`; its place lowered so the clip's seat meets
     the cushion), then `sitting` or `sitting_talking` (a quarter); no free
     seat or no seats: gone as in task 06.
   - Getting off: as a train stands, the simulation's count at each door
     stand up (the nearest to it, a moment apart), walk to it and wait there
     one behind the other; each steps out as the door's turn comes (task 06's
     script down the steps); those not out when it leaves sit again.
   - A train's cars are filled with those on board (the simulation's count,
     or those to get off at its next stop if more) once all its cars' seats
     are read, within 800 m of the camera: far off (60 m), off the screen,
     in the moment it comes into view (a car coming round from behind the
     camera), or just after a jump in time. A car beyond 900 m loses its
     people (filled again as it comes near: a far train's 154 seated people
     cost the crowd for nothing); a car not drawn for 4 s goes with them.
   - People in cars walk by the time gone (up to 2 s a frame), not the
     crowd's 0.5 s: on a slow renderer they kept up badly.
   - `TSRE_PEOPLE_LOG`: in cars, sitting, cars with seats; each stand's
     people sent off.
5. **The candidates upgraded** (done 2026-11-13):
   - OR_PSK_NEW: 49 new `.sd` files in `TRAINS/TRAINSET/PKP_EN57-OR` and
     `PKP_EN57A-OR` (the newer EN57 model and EN57AL 1542, 1687, 2207,
     2219; none existed before). Archives in `~/ORTS`:
     `OR_PSK_NEW-sd-seats-20261113.zip` (the files and a README, to extract
     into the game root) and `-backup.zip` (empty: nothing replaced).
     `kabinaAL_3D.s` (a 3D cab) was written by mistake and taken out; the
     script now skips models named for a cab.
   - MaSzyna: 75 new `.sd` files beside `dynamic/pkp/en57_v2/lowpoly/*.e3d`,
     only the block (`maszyna-sd-seats-20261113.zip` in `~/ORTS`, to
     extract into MaSzyna's folder; its backup empty).
   - Checked at Raszowa: PRUDNIK's 12:11 EN57 (1156_R46421) and MaSzyna's
     `en57_v2` 1609 standing in for 0518_R46403 at 05:38 (task 06's scratch
     game root): people board to seats and get off from them.

## Review (the user, 2026-11-13)
- **People sat 0.2 m too high** (both EN57s). The seat's height came from
  `sit_down`'s joints' median drop (`clipTravel`, the body as a whole for
  task 06's steps: 30 of the 53 joints are fingers, so the hands' 0.20 m)
  where the rule (0.44 m against the mannequin's 0.335 m) is for the
  pelvis's (the man's 0.355 m). Now from the hips
  (`SkeletalAnimation::clipHipsTravel`: the first joint down from the skin's
  top that branches; `Crowd::clipTravelOf(..., Travel::Hips)`). Measured on
  our humans' meshes, sitting:

  | Human | Hips' drop | Thighs above the feet | The rule | Before |
  |---|---|---|---|---|
  | man | 0.355 m | 0.47 m | 0.466 m | 0.26 m |
  | young | 0.279 m | 0.36 m | 0.366 m | 0.21 m |
  | woman | 0.316 m | 0.34 to 0.38 m (her skirt lower) | 0.415 m | 0.25 m |

- The same median put the pelvis 0.07 to 0.12 m too far back (into the
  backrest's foot). Now it sits 0.1 m behind the seat's place (the cushion's
  middle; cushions 0.45 m deep in PRUDNIK's EN57, 0.49 m in MaSzyna's): the
  body's back, 0.08 to 0.15 m behind the pelvis, at the backrest.
- PRUDNIK's 0.39 m cushions: the man's feet 0.08 m into the floor, the
  woman's 0.03 m, the young one's above it.
- humans-crowd +1 (the man's hips sitting down, against his joints'
  median). Checked from the aisle at Raszowa in both EN57s.

## The older EN57, numbered seats, the tool shared (the user, 2026-11-13)
- **The older EN57 has seats** (the user: "may have seats, probably in a
  second model"). In PRUDNIK they are in the car's own shape (`MAIN`):
  simple blocks, cushions 0.50 m above the floor, 0.43 m deep. The finder
  counted a floor face only when all its corners were inside the body; this
  floor is one plate reaching past that margin, so all of it was left out
  and the seats' tops (16 m2) were taken for the floor. Now a face counts by
  its middle: the older model's cars 76 seats (middle) and 58 (ends); the 49
  files written before come out the same.
- **Seats in a second model**: PRUDNIK's EN57s attach only drivers as
  freight animations (`EN57_mech*.s`, `1mech_3E.s`,
  `PKOR_maszynista_*.s`), but a car's interior may be a shape of its own.
  TSRE now reads the seats of the car's shape and of each of its freight
  animations' shapes (`FreightAnim`, `FreightAnimStatic`; a MaSzyna car's
  interior and attachments), each moved where the car draws it (its
  `Offset`, x and z mirrored, as `Eng` draws it). Not met in our content
  yet.
- **Numbered seats** (the user: so a wrong one can be taken out from the
  plan): `Seat 12 ( x y z heading )`, the label read as its number
  (`ShapeSeats::Seat::number`; a seat without one still read; a glTF's
  numbered by its place); station-people +1. The plans show the numbers;
  `car_seats.py drop <model> 12 13` takes seats out, the others keep
  theirs; `plan --sd` draws an `.sd` as written.
- **The tool shared**: `extra/car-seats/car_seats.py` (self-contained: the
  S-text helpers copied from `scripts/lamp_shapes.py`) with its workflow
  `extra/car-seats/README.md`; `scan` maps vehicle folders (`.eng`/`.wag`
  shapes and freight animations, MaSzyna `.mmd` models, interiors,
  attachments) into a report, `found.json` and plans (PRUDNIK's two EN57
  folders: 137 vehicle files, 126 models, 36 s on 12 cores).
- **Written again, numbered** (archives in `~/ORTS`, each with a README;
  they replace the first sets):
  - OR_PSK_NEW: `OR_PSK_NEW-sd-seats-20261113-2.zip`, 101 `.sd` (the 49,
    the older model's 46 and the 6 copies of EN57AL 1542 in
    `PKP_EN57A-OR/xhl`); `-backup.zip` holds the files as they were (the
    first set's 49 and one original, `PKP_EN57-1827s.sd` from 2014 with a
    bounding box, kept with the block added).
  - MaSzyna: `maszyna-sd-seats-20261113-2.zip`, the same 75, numbered.
- **Later**: finding and checking seats in the Shape Viewer itself (the
  user); mapping all of OR_PSK_NEW's and MaSzyna's cars by another agent:
  [11](11-seats-mapping.md).

## Doors in the .sd (the user, 2026-11-13)
The user asked why TSRE reads a car's model at load: for its doors only
(`CarDoors`: the door parts' triangles read again once a model, on a
worker; where each door is, its side, width and threshold height). Seats
come from the `.sd` alone. The user's idea, taken: the doors go into the
same block, `Door N ( x y z width )` (as written in the model's file: in an
S file -x is the car's left, its DOOR_D..F side; y the floor at the
threshold), checked on the finder's plan once; TSRE takes them from there
and reads the door parts only for cars without them (the guess for cars
without door parts stays). Built:
- `ShapeSeats`: Door lines (and a glTF's `"doors"`), mirrored as the seats
  where an S file is drawn; a block may hold doors only (`Seats::none`).
  `carSeatsOf` merges every model's doors (the car's own shape holds them,
  an interior its seats). `StationPeople` takes a car's doors from them
  (+x drawn its left) before reading door parts; the people log names the
  source (`.sd` or the parts). The Shape Viewer's Seats view draws doors as
  magenta frames. station-people +3.
- `extra/car-seats`: doors found as `CarDoors` finds them (the same split),
  in `write` and a `doors` command (only the Door lines, the block's other
  lines kept), on the plans (magenta, D1...), in the scan's report;
  `drop d2`; `--stage <folder>` writes nothing into the content (the
  agent's mapping, task 11: a staged set the user applies by extracting
  its zip, as the lights').
- OR_PSK_NEW: 113 EN57 and EN57AL models got their doors (staged, then
  applied by extracting the zip): 101 `.sd` with seats (Door lines only
  added), 12 new `.sd` for EN57ALs that had none;
  `~/ORTS/OR_PSK_NEW-sd-doors-20261113.zip` (with a README: undo = extract
  the backup, delete the 12 new) and `-backup.zip`. At Raszowa the three
  EN57 cars' doors come from the `.sd`, the same as their parts gave.
- MaSzyna's doors are not written: TSRE reads them by the `.mmd`'s door
  prefix, and the EN57's are in models attached to the car.
