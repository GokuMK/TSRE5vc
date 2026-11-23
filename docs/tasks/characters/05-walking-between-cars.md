# 05 - Walking between cars

Status: built on `feature/characters` (worktree `../TSRE5vc-characters`),
2026-11-23, not merged (the user's go: "Ok, proceed"). The brief and the
user's answers the same day. It takes task [03](03-trains.md)'s Left item
"Gangways between cars"; the walking character is task
[02](02-walking.md)'s.

## Goal
The character walks through a train from car to car, through each end door
and over the gap, both on a standing train and on a moving one. It can do
this wherever both facing ends have a gangway.

The user's design (2026-11-23):
- A car gets an end passage where an aisle reaches its end.
- The passage is open only while the car is coupled to one with an end
  passage at the same height, within a margin.
- While it is open, the car has a small floor over the gap. It is used for
  collisions only and is not drawn.
- Each car's floor is its own and fixed in its frame, as in reality. The
  user: "Each car has its own floor square, those are overlapping and even
  when moved, some floor sits under the feet." Nothing is rebuilt each
  frame.
- Content without gangways is fixed in that content (the seats tool),
  never by a rule in code for all cars (the user, 2026-11-22).

## What is there (main 3b2d984a)
- **Riding:** a character in a car is in that car's frame
  (`Eng::placedMatrix`). Its collision world holds that car only
  (`CarCollision::build`): the body, freight animations and 3D cabs, plus
  the assumed floor (`AssumedFloor`, 1.1 m) over the footprint, used where
  no mesh is found. The footprint comes from the definition's size.
- **A moving car** keeps the feet within its footprint
  (`CarCollision::within`, in `Character::update`).
- **Changing frame** happens only between a car and the ground
  (`Character::update`, "Its frame by the floor under its feet"). It
  enters a car when its feet land on that car's triangles in the route's
  world, and leaves it when it is in the air off the footprint. Nothing
  goes from one car to another.
- **Passages** (`Passage ( x y z width )` in `ESD_TSRESeats`, or
  `passages` in a glTF's `extras.TSRE_seats`) are openings in the car's
  collision world: x ± (width/2 + 0.3), y + 0.05 to + 2.1, z ± 0.35. They
  are added once (`CarCollision::readPassages`) and never removed.
  `CollisionWorld` can add, move and remove meshes, but openings can only
  be added or all cleared.
- **The train:** `Consist::engItems` in order, car 0 in front, each with
  its `flip`. Cars are spaced by their full length (`Eng::getFullWidth`,
  the definition's size plus its coupling's r0: `Consist::initPos`). A
  flipped car's shape is turned by π (`CarPlacement::placeCar`). Coupling
  and uncoupling are in the core (its Coupled, Joined and Uncoupled
  events). Coupling slack in the driving sim is up to 0.2 m
  (`SimulationDriving.cpp`, `DefaultSlack` 0.01).
- **Steps:** the body steps up 0.35 m (`CharacterBody`).
  `ShapeSeats::OneStep` (0.25 m) is the step people walk over where two
  aisles join.
- **The seats tool** (`extra/car-seats`) finds aisles, doors, stairs, and
  the Passages where walls cross stairs (`stairs_passages`). It writes in
  the `.sd`'s axes (task shapes [20](../shapes/20-sd-axes.md)).

## Design
### Content: `EndPassage`
- A new line in `ESD_TSRESeats`: `EndPassage ( x y z width )`. It marks
  the door in an end wall: its middle across the car (x), the floor at it
  (y), the wall's face along the car (z), and the door's width. The sign
  of z gives its end (+z is the shape's front). There is at most one per
  end.
- It is a place like the others: in the `.sd`'s axes, mirrored with them
  (`ShapeSeats::mirror`), and also read from a glTF's
  `extras.TSRE_seats` (`endPassages`).
- The seats tool writes it where a level aisle ends at an end wall within
  about 1 m of the body's end. A cab's partition stands 2-3 m in from the
  end, so a cab end gets none; a locomotive has no aisles. The exact rule
  is settled while building, and the tool's plans draw each EndPassage for
  review. A `gangways` command adds them to `.sd` files written before, as
  `stairs` and `axes` do.
- Most models draw their end doors closed. The character walks through
  them as through the stairs' closed doors, until assets with real doors
  come.

### When it is open
Each car checks both of its ends every frame. That is a few comparisons,
with no file reads; the result is kept per end until the neighbour's `Eng`
changes. An end is open when:
1. The car has an EndPassage at that end.
2. A neighbour is on that side: car i-1 or i+1 in `engItems`. An
   unflipped car's +z end faces car i-1, the train's front (to be confirmed
   by the suite's flipped car).
3. The neighbour has an EndPassage at its end facing this car.
4. The two floors (each passage's y in its own car's frame, both over the
   rail) are within `ShapeSeats::OneStep` (0.25 m).
5. Each passage's wall is at most `MaxReach` (1.5 m) from its car's
   coupling end (half its full length). This keeps plates short: a car
   with long buffers gets none.

The open width is the narrower of the two passages; each car's plate is
centred on its own passage's x.

### The floor plates (the user's design)
- Each open end has a plate fixed in its own car's frame. It runs from
  the end wall's z to the car's coupling end, plus `Overlap` (0.5 m) past
  it, at the passage's floor height and the open width.
- Two side walls, 2.1 m high (a Passage's height), run along its edges, so
  nobody steps off into the gap and onto the track. There is no roof.
- At rest the two cars' plates overlap by twice `Overlap`. Cars' ends are
  placed on the track, so the coupling ends stay together on a curve,
  under slack and under different cant. The plates still overlap there,
  and the suite checks it.
- A plate is collision only and is not drawn; the model's bellows are what
  is seen.
- While its end is open, the plate is a mesh in the car's collision world
  (`CollisionWorld::add`), and is removed when the end closes. The end
  wall's opening comes and goes with it, so openings become removable (by
  an index, as meshes are).
- A moving car's footprint (`within`) takes in its open plates.
- The floors may differ by up to 0.25 m where the plates meet. The body
  steps over that (it steps 0.35 m).

### Crossing: car to car
- The character stays in the car whose plate it stands on until its feet
  pass that car's coupling end by `Handover` (0.15 m). The margin keeps
  it from switching back and forth where the two coupling ends are pushed
  together.
- It is then put in the neighbour's frame at once, without going through
  the world. Its place goes through the world from one car's frame to the
  other's. Its velocity, its way and the look are turned by the two cars'
  yaw difference, so the camera keeps looking where it looked, as
  `enterCar` does from the ground. Past the coupling end by `Handover`,
  its feet are over the neighbour's plate (`Overlap` 0.5 m is more than
  `Handover` plus slack).
- Neither car needs the other's mesh. The plate's walls keep the
  character within the passage's width, and before the handover it can't
  reach far enough to touch the other car's end wall.

### Closing
- The end closes when the neighbour is uncoupled, the consist changes, or
  the passages no longer match.
- Before its plate and opening go, a character on the plate or in the
  wall's opening is put back into its car: a step inside the wall at the
  passage, on the floor found there (`CharacterBody::fitsAt`), facing as
  it was.
- Characters left standing (`Characters::Cast`) follow the same rule.

### Not in this task
- Passengers (station people) walking between cars: their routes stay per
  car.
- Jumping off a plate, and riding on the outside.
- **The simulation and the server:** the character's car now changes
  mid-run. Task 04 (the character in the simulation) and the server's
  task 07 carry that; this is flagged to them, not done here.
- **The third-person camera:** its ray tests only its own car's world, so
  at the gap the camera behind may go into the next car's wall. Adding the
  neighbour's body to that ray is for later.
- **Sound** (found while writing this brief, by the code, not checked by
  ear): the listener takes its car only from a `Consist` camera
  (`RouteEditorGLWidget.cpp`, the listener's `attachedTo`). A walking
  character's camera is not one, so in a car it probably hears as from
  outside. This is its own item, for later (the user, 2026-11-23: fine as
  it is for now).

## Steps
1. `ShapeSeats`: `EndPassage` read from the line and from glTF extras,
   mirrored with the axes, and its end.
2. `CollisionWorld`: removable openings. `CarCollision`: each end's state
   (open, width, height), its plate mesh and opening added and removed, and
   `within` with the plates.
3. `Character`: each end's neighbour per frame (the consist, flips),
   opening and closing, the handover between cars, and closing with
   someone on a plate.
4. The seats tool: the `EndPassage` rule in `find`, the `gangways`
   command, plans drawing them, and the README.
5. Sets: OR set 8 (set 7 with end passages) and MaSzyna set 7 (set 6 with
   end passages), staged in `~/humans-11` as before.
6. Docs: the `ESD_TSRESeats` comment in `ShapeSeats.h`, the car-seats
   README, task 03's Left item pointing here, and the roadmap.

## Checks
- A new `car-gangways` suite:
  - `EndPassage` read, and mirrored for `.s` and `.e3d`, with and without
    the axes line.
  - Open and closed: two matching cars are open. These are closed: one car
    without a passage, a cab end, floors 0.3 m apart, a wall too far from
    its coupling end, and the last car's free end. A flipped car has its
    ends swapped.
  - The plates overlap under every point of the line walked across: on
    straight track, on a 150 m curve (carriers fitted), with 0.15 m cant
    on one car only, and with slack of ±0.2 m.
  - Walking across, both ways, on a standing train and at 10 m/s: the
    frame changes once each way, the character doesn't fall, its world
    place has no jump longer than a step, and the look is kept.
  - Uncoupling with the character on a plate puts it back inside its car,
    and it isn't stuck.
- Live: a PSK timetable train of coaches walked through three cars with
  captures (the scratch roots, never `~/ORTS`), and a MaSzyna train whose
  interiors have end doors (to find).
- Build all, ctest, the app suites, `character-body`, `station-people`.

## As built (2026-11-23, `feature/characters`)
- **`ShapeSeats`:** `EndPassage` (its `end()` by z's sign;
  `Seats::endPassageAt(end)`), read from the line and from a glTF's
  `extras.TSRE_seats.endPassages`, mirrored with the axes
  (`ShapeSeats::mirror`, so `drawn()` too), its message
  `shapeseats.endpassage` and the unknown-line message naming it (both
  `.ts` files). `StationPeople`'s merged car seats (an interior as a
  freight animation, MaSzyna's) carry them.
- **`Gangways`** (`src/tsre/characters/Gangways.h/.cpp`): the margins
  (`MaxReach` 1.5, `Overlap` 0.5, `Handover` 0.15, `WallHeight` 2.1,
  `WallDepth` 0.35, `Aside` 0.3, `StepIn` 0.5 m), `reaches`, `matches`,
  `Plate` (`holds`), `plateOf`, `triangles` (its floor from `WallDepth`
  inside the wall to its far end, its two walls from the wall on),
  `opening` (the door's width and `Aside` either side, from `WallDepth`
  inside the wall to the plate's far end: its own car's bellows and a
  gangway's cover there are walked through, the plate's walls keep the
  way), `endTowards`, `handedOver`, `insidePlace`, and `nextCar` with
  `JointLength` 1 m (found on the user's content: the VD Bhp's halves are
  coupled through a 0.2 m car, their shared bogie; a car shorter than 1 m
  is walked over, the gangway joins the cars either side).
- **`CollisionWorld`:** openings by index (`addOpening` returns it,
  `removeOpening`), and meshes added not `openable` (the plate's walls
  stand inside its door's opening).
- **`CarCollision`:** `couplingEnd()` (half the definition's full length,
  as `Consist::initPos` spaces cars, else half its footprint),
  `openEnd(door, width)`/`closeEnd(end)` (the plate a mesh and the opening,
  added and taken away), `plate(end)`, and `within` taking in open plates.
  `passages()` counts Passages only.
- **`Character`:** `gangways()` each tick in a car (both ends: the next
  car on each side by `nextCar`, which end faces it by where it is, so
  flips need no rule; open when both doors match, else closed with
  `stepInside` first), `crossGangway` after it moves (past the coupling end
  by `Handover`: `changeCar`, its place, eyes, velocity, way and look
  turned into the next car's frame, that car's ends opened at once), logs
  ("through to train N's car M", "end closed: back inside").
- **The flips:** an unflipped car's +z end faces the car before it (the
  train's front), as the brief guessed; the code does not rely on it.
- **`extra/car-seats`:** `end_passages` (for each end the level aisle
  reaching furthest; the first wall across its line within 0.7 m of its
  end, either side, and 1.3 m of the model's end, else an open end at the
  aisle's end within 1.3 m; a cab left out: a part named for a cab in the
  room behind it, or a vehicle with a cab (`cab_vehicle`: an `.eng` beside
  the `.s` with a `CabView` or `ORTS3DCab`) with no passenger door into it
  and 3 seats at most), `with_end_passages` (a Passage on that wall becomes
  the EndPassage: a Passage opens its wall coupled or not), `find` with
  them, `sd_seats`/`mirrored`/`block` with `EndPassage` lines, red on the
  plans (E and its floor), and the `gangways` command for written sets
  (a second run writes nothing). README.
  - For MaSzyna interiors only named cab parts make a cab: on the first
    run every doorless end room judged a cab was a coach's (111A, 120A,
    113A, B89, Bhp, EW60 s: 29 ends) and every real cab was found by its
    part.

## Results (2026-11-23)
- **`car-gangways` 28:**
  - Reading: `EndPassage` read, mirrored for `.s` and `.e3d` with and
    without the axes line, and from glTF extras; a short line left out
    with its message.
  - Matching: floors 0.25 m apart open and 0.3 m closed; a wall 1.5 m from
    its coupling end open and 2.75 m (a cab's partition) closed.
  - Facing ends: opposite for two cars the same way round, the same for a
    flipped one; the coupling points meet.
  - Plates overlap on every point walked across, with a step's height at
    the handover: straight, a 150 m curve, carriers, the curve's start
    between the cars, 0.15 m cant on one car, slack ±0.2 m, a 0.2 m joint;
    joints walked over by `nextCar`.
  - Walking across with `CharacterBody` between two cars' worlds (their
    plates, openings, walls drawn closed): standing; up a 0.2 m step; at
    10 m/s into a 150 m curve, both ways. Each handed over once, on a
    floor all the way, no jump. A closed end stops it.
  - A moving car's footprint meets its open plate (found live, below);
    `insidePlace`, `holds`, `handedOver`.
- **Live** (`--game-root` a scratch overlay of OR_PSK_NEW with set 8, never
  `~/ORTS`; PRUDNIK, PSK_2026, train 68, 1707_R60950: SU42, Bhp 18965,
  joint 965_w, Bhp 18968):
  - Standing at Nysa: from 18965's vestibule through its end doors, over
    the joint, into 18968 (`through to train 68's car 3`), up its stairs
    to the upper deck, then back (`through to train 68's car 1`).
  - Moving, at 17:12 after Nysa: the same both ways, `its train moving`.
  - Found and fixed: on a moving train the first try stopped 0.16 m short
    of the end wall. A moving car's footprint ends the body's radius
    inside its size, and the plate's zone began at the wall; it now
    begins `WallDepth` inside it (`Gangways::within`). With the old zone
    the suite fails the moving walks in the same way.
- **Unchanged:** character-body 29, station-people 43 (45 with both staged
  sets as `TSRE_OR_PSK_ROOT` and `TSRE_MASZYNA_ROOT`), shape-axes 19,
  walk-areas 22, humans-crowd 66, shape-lights 53, e3d-shape 81. Build
  all, ctest 11/11, 128 of 147 app suites. The 19 others need content or
  arguments: a game root, `--test-cases`, a corpus folder.
- **Sets** (in `~/humans-11`, outside the repo):
  - OR set 8 (`OR_PSK_NEW-sd-seats-map-20261123-8.zip`, README8-or.md,
    plans in `plans-staged8-or`): set 7 with 480 `EndPassage` lines in 298
    files, 308 end-wall Passages taken out, nothing else changed.
  - MaSzyna set 7 (`maszyna-sd-seats-map-20261123-7.zip`,
    README7-maszyna.md, `plans-staged7-maszyna`): set 6 with 280 lines in
    190 files, 218 Passages taken out.
  - Coaches have both ends; EMU cab cars one (EN57, ED72, EN57AL); cabs
    found by part in MaSzyna (94 ends). Aisles that stop short of the ends
    give none (Impuls, EN75, SA railbuses, ED78, Elf's end cars). A second
    `gangways` run writes nothing.

## Left
- **The VD Bhp's front ends:** a wall 1.4 m inside the end wall
  (18965 at 7.73) has no Passage, so that end's door can't be reached even
  where coupled to another Bhp. In the 17:09 this end faces the SU42 and
  stays closed. A Passage there belongs in the set, after a look at the
  model.
- **Gorlitz88:** its aisles end at a partition 1.9 m from the end (no
  `EndPassage`). Its gangways need that partition's Passage, and the end
  room's floor, written in its `.sd`.
- **SN84:** its end passages come out 0.14 m past the model's end (an
  open end at the aisle's end). Harmless (its middle car has none), but
  worth a look.
- **The third-person camera** tests only its own car's world: in a
  doorway it can sit inside the end doors (seen live, walking back).
- **Real end doors:** most models draw them closed, and the character walks
  through them, as with the stairs' doors.

## The user's answers (2026-11-23)
1. The plate's side walls: yes.
2. The margins (`OneStep` 0.25 m, `MaxReach` 1.5 m, `Overlap` 0.5 m,
   `Handover` 0.15 m): fine if they work. The suite decides; any value
   changed while building is reported with the reason.
3. The line's name: `EndPassage`, which fits the existing `Passage`.
4. Sound: later; fine as it is for now.

No questions open. Building waits for a go.
