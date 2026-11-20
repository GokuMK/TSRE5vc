# 03 - Trains

Status: built on `feature/characters` 2026-11-20 (the user's go), not
merged. Design in [00-task-roadmap.md](00-task-roadmap.md) (Riding a moving
train, Trains); the walking character in [02](02-walking.md).

## Goal
The character in the trains: riding in a car's frame (the train carries
it exactly), boarding and stepping off by an open door, sitting on a free
seat as the passengers do, the cab (sitting at the driver's place drives
the train; E held stands up, the controls still by the mouse; out by the
cab's door at a stop and back), using things by a label under the
view's mark and a click or E. MaSzyna cars' interiors and cabs are part
of their collision (the user, 2026-11-20).

## What is there
- **A car's place:** `CarPlacement::placeCar` from its ends on the track
  (`ruch1`, `ruch2`, placed by `SimulationController::placeViewTrains`
  before the character's tick), bogies fitted (`Eng::placeOnCarriers`,
  cached by its ends); `Eng::lastDrawn` keeps the matrix of the last
  paint, a tick behind the character's step.
- **A car's models:** its shape, its freight animations (MaSzyna's
  low-poly interior and attachments, `translate(-x, y, -z)`), its 3D cabs
  (`Eng::cab3DShapePath`, in the car's space; MaSzyna's drawn over the
  body with the interior's `cab<N>` parts hidden, Open Rails' instead of
  the body's own cab); all drawn by the car's matrix.
- **Seats, aisles, doors:** `ShapeSeats` (`ESD_TSRESeats`: seats, aisles,
  doors), read for each car by `StationPeople` (`carSeatsOf`), the per-car
  `taken` table (a rider's index, -1 free); doors from the `.sd`, else the
  shape's door parts, else made up at its ends; open when the car's door
  animation is (`SimulationController::applyDoors`).
- **The cab:** Drive rides the `Consist` (`rideCab3D`, the head's mount),
  the keys the train's (`Consist::keyTaker`); `Eng::pickCab(origin,
  direction)` takes any ray in the camera's tile's space; the hover label
  (`cabHover`, `MapLabelAtlas`). No cab door in any cab's data (MaSzyna's
  side openings found by `findOpenings`).

## Steps
1. **The car's frame:**
   - `Eng::placedMatrix`: the car's matrix now (as the paint places it),
     for the character's tick.
   - `CarCollision`: a car's meshes in its frame (its shape, its freight
     animations', its 3D cabs'), cached by the vehicle; an assumed floor
     (the doors' sill, else 1.1 m over the rail) within its footprint.
   - The character's place on the ground (a tile, metres) or in a car
     (train, car; its place, way and velocity in the car's frame): in a
     car it moves in that frame against that car only; its place in the
     world is the car's matrix times it, each tick, so the train carries
     it exactly. Drawn so; its eyes and the camera too (the look kept in
     the car's frame: curves turn the view; its pitch and cant).
   - **Which frame:** the floor under its feet. Standing on a car's
     triangles in the world (the cars in `RouteCollision` say which):
     into that car's frame; in a car, no floor of the car under it (off
     its edge, the train standing): back to the world. A moving car keeps
     it within its footprint (jumping off later).
   - Walk or Here with the camera in a car: in that car.
   - Its car gone (the train finished, its cars made again): on the
     ground where it was.
2. **Using things:** a small mark in the view's middle while the
   pointer is captured; the usable things near the character (each a
   label, a place in its frame, a reach); the nearest the eyes' ray
   meets within 2.5 m gets its label under the mark (as a cab control's);
   a click or E does it.
3. **Doors:** an open door of a standing train: from the platform,
   boarding (the steps clip from the outside to the sill, then into the
   car's frame at its aisle); from inside, stepping off (to the outside:
   the floor beyond the sill).
4. **Seats:** a free seat (no rider's, the player's own mark in the
   car's `taken`): the character steps to its row and sits as a rider
   (`sit_down` to the cushion, `sitting`), the camera at its eyes; Space
   stands it up (`stand_up`), the seat free.
5. **The cab:** the driver's place (the 3D cab's head): a click sits the
   character and drives the train (Drive's camera, the keys the train's;
   the character not drawn); E held half a second: it stands beside the
   seat in the loco's frame, the train still the player's, the cab still
   drawn, its controls by the mark (the label, a click, a lever dragged
   by the mouse's moves, the view held); the cab's doors (MaSzyna's
   openings, else both sides at the head): out to the ground when the
   train stands, back in. Walk while driving: the character in the
   driver's place, seated.
6. **Passages** (the user, 2026-11-20: most interiors' partition doors are
   drawings on a wall): a car's `.sd` Passages as openings in its collision,
   so the character walks through a drawn door.

## As built
- **Step 1, the car's frame:** `Eng::placedMatrix(tile, matrix)`: the
  car's matrix as `pushRenderItemOnTrack` places it, for the character's
  tick (before the paint; its bogies' fit cached by its ends, so the paint
  finds the same). `Characters::CarCollision`: the car's meshes in its frame
  (its shape, its freight animations' moved as drawn, its 3D cabs' by
  `Eng::cab3DShapePath`, in place), the cache's (`shapeCollision`, by file);
  an assumed floor (1.1 m over the rail) within its footprint (its
  definition's width and length, else its shape's bounds). `Character`: its
  frame the world's or a car's (`Ride`: train, car, the `Eng`, the matrix
  now and back, its yaw, pitch and cant, whether the train moves); its
  body moves in that frame against that car only, its place, eyes (smoothed
  in the frame: the train's speed is no lag) and camera place turned out
  each tick; `getCameraPosition` gives the car's yaw, pitch and cant, so the
  look is kept in the car's frame (each car its own mount id: the look
  carried over at a change). Into a car: standing on a car's triangles in
  the world (`RouteCollision` names its placed cars) or Walk/Here with the
  camera in one (`placeNear`: any train's car as placed now, the camera
  within its footprint and height: its floor under the camera, else along
  its middle); out: off a standing car's footprint (falling), its car gone
  (`placeCar` fails: the train finished, its cars made again). A moving car
  keeps it within its footprint. Station people leave out its agent while it
  is in a car. `TSRE_CHARACTER_LOG=1` logs the place search's cars.
- **Step 2, using things:** `Character::Usable` (Board, StepOff, Sit,
  Drive, CabOut, CabIn): each a label, a car, an index, a sphere in the
  car's frame; found each tick while it is the player's (`findUsables`),
  the nearest the eyes' ray meets within 2.5 m aimed at (`aimAt`, in each
  car's frame as placed now); `use()` by a click (the pointer captured) or
  E. The view: a "+" mark in its middle while the pointer is captured, the
  aimed thing's label under it (`useHover`, the cab label's atlas and quad:
  `labelQuad`).
- **Step 3, doors:** `StationPeople::doorsOfCar` (the people's doors:
  `.sd` Door lines, else door parts, else made up for a car carrying
  people) and `seatsOfCar`; a door usable while open (its group's
  animation, `applyDoors`) and the train standing. Its places in the car's
  frame (`doorPlaces`): the sill (the door's bottom 0.15 m in), inside 0.9
  m in, outside the first floor a step reaches probing 0.4 to 2.0 m out
  (the platform past its edge's gap), else the ground. Boarding: into the
  car's frame, a script: to where the steps clip ends at the sill, the
  `steps_up_N` clip (N by the rise, 0.35 m a step, at most 3) holding its
  place while its height goes up, then inside. Stepping off: to the
  inside, the sill, `steps_down_N` to the outside, then the world's frame.
  A door's script done at once when the train moves off.
- **Step 4, seats:** a free seat of its car (`StationPeople::seatFree`: no
  rider's; `setPlayerSeat` marks it `PlayerMark`, never a rider's index, in
  the car's `taken` table, again whenever that car is made): its feet where
  `sit_down`'s hips land on the cushion (StationPeople's sit plan: 0.1 m
  back, its origin sunk by the clip's drop), the clip, then `sitting`
  looped; Space: `stand_up`, the seat free.
- **Step 5, the cab:** the driver's place (each 3D cab viewpoint's head,
  mirrored as the cab says) usable when the train is drivable: the view
  drives it (`SimulationController::drive` when not driven), the character
  sits there unseen, the camera in the cab (`rideTrain`). E held half a
  second there: it stands beside the place (a floor and room found round
  it), the camera at its eyes, the 3D cab still drawn (`Consist::keepCab3D`
  while it stands in that car and the train is driven); the controls by
  the mark: named under it, a click presses, a held lever follows the
  mouse's moves (a cursor of its own, the view held). Leave (the train to
  the AI) stands it up. The cab's doors: both sides at each viewpoint's
  head, Get out (from inside, the train standing: down `steps_down_N` to
  the ground beside it) and Climb in (from the ground). Walk while driving
  from the 3D cab: the run's character at its driver's place, seated.
- **Step 6, Passages:** `CollisionWorld::addOpening` (boxes where
  `contacts()` leaves triangles out; floors and rays as they are);
  `CarCollision::readPassages` once the car's seats are read (each tick
  until then): each Passage's doorway (its width and a body's radius either
  side, 2.1 m high, 0.35 m either side of its wall: a wall across the
  aisle, as the car-seats tool finds them). OR_PSK_NEW and MaSzyna: 571
  Passages in 95 of 408 `.sd` files, all on an aisle's line (partitions
  across the car); none for a compartment's door in a side wall.
- **Fixes found on the way:** the route's collision gathered once where
  the character began and never again while it stayed within 4 m, so a
  walk begun before the tiles about it were made (a run's first view, a
  character placed in a car) had no objects: a gather that met a tile not
  made or loaded, or an object or its shape not loaded, is done again a
  second on (`RouteCollision::Stats::unready`). The view's character entry
  points (Walk, Here, its tick, captures) set `Game::currentShapeLib` to
  the view's library first (it was whatever the last handler left).
- **Captures:** walk views take `"use"` ("board", "off", "sit", "stand",
  "drive", "standcab", "cabout", "cabin": the nearest of a kind used,
  `RouteEditorGLWidget::diagnosticUse`) and `"useSeconds"`; the walk now
  comes after a view's train view, so a view riding a vehicle walks from
  inside it.

## Left
- **Compartment doors** (side walls along the car; later, the user,
  2026-11-20: saloons' partitions are enough for now, classic carriages'
  small compartments wait): the Passage form can't say a wall's
  direction. Proposed: an optional fifth value (0 or none: a wall across
  the car, as now; 90: a wall along it, x its place across, z the door's
  middle, the width along the car), the car-seats tool finding side walls,
  a door per compartment (its middle, about 0.65 m), and a check walking
  every aisle and join for walls without a Passage.
- Aiming has no test of what is between: a seat or a door can be aimed
  at through a wall (sitting walks there through it). Fix for later: a ray
  from the eyes against its frame's collision, a wall nearer than the aimed
  thing (less its radius: a door's leaf, a seat's back) hides it.
- The body stays upright in the world in a car (no lean with its cant);
  swaying with the train's starts and stops.
- **Gangways between cars** (more than Passages): the car-seats tool
  writing an end door's Passage (at or past its aisle's end, told from a
  cab's partition door); the character handed from a car's frame to its
  coupled neighbour's (the next in the consist, flipped or not) at the
  middle of the gap between their ends; a floor across that gap (half in
  each frame); only where both facing ends have one (a cab's end none).
- Jumping onto or off a moving train; controls limited while the driver
  is out of the cab (the roadmap's later questions).
- The cab: a tap of E at the driver's place (the control under the
  cursor) not yet, only E held. When built: E pressed over a control is
  the control's (pressed, held, let go as by the mouse: a horn, a sander,
  the vigilance button held by E never stands it up); held with nothing
  under the cursor half a second, it stands up (a hint "Hold E: stand
  up" while it counts); the cab's doors made up at both sides of
  each viewpoint's head (MaSzyna's cab openings, `findOpenings`, not
  used); sitting in a cab the core does not drive puts the camera in the
  one it drives.
- Passengers through Passages (and side doors) instead of through walls.
- **Doors that open** (the user, 2026-11-20: the made-up ways are right
  where a car has no animated door; notes first):
  - **MaSzyna cabs' doors:** most cab models animate their doors, under
    names that don't say door (moved by the cab's own animations). To
    check: whether they show from outside (the cab model is drawn only
    while ridden; outside, the body and its low-poly interior are). Then
    Climb in and Get out could open that door, go through its opening
    (its place from the part) and shut it.
  - **Passenger doors from outside:** an Open use on a closed door of a
    standing train (as a modern unit's door button) for an AI train; a
    driven one's doors stay its driver's (a rule needed).
  - **A real climb into a cab:** a clip with grab handles and steps as
    high as a cab's (the steps clip lifts the rest of the way now).

## Checks
- `character-body` (23): an opening in a wall walked through, the same
  wall without it stopping the body (+2).
- **Live, PSK** (`tests/renderer/characters-trains-views.json`, PRUDNIK,
  train 10, the EN57 at Raszowa): Here from a camera in its middle car:
  in its frame (its floor at 1.16 m in it); seat 42 sat on (first and
  third person, the riders round it), stood up; at 12:11:30 out by door 3
  onto the platform (160.80 m: its edge's gap probed over, 0.6 m out),
  back in by it (`steps_up_2`); at 12:12:40 the train 700 m on, the
  character riding inside it at the door (first and third person).
- **Live, MaSzyna cab** (`tests/renderer/characters-cab-views.json`, the
  MaSzyna EN57 en57-1609 in the PSK timetable's second train at Raszowa,
  05:39:05): Here in its front car (3 meshes: body, low-poly interior,
  cab), its driver's place used: driven, the camera in its cab; E held:
  standing in the cab beside the seat (1.05, 1.19, 9.25 in the car), the
  3D cab drawn; out by its side (the driver's side: onto the ballast, the
  platform beyond the next track), back in on the cab's floor. The driven
  cab's textures show magenta in this root (Drive's own view; main not
  checked). A driven MaSzyna cab never settles in a capture (a short
  settle limit).
- Images in `~/characters-03` (`raszowa-train.jpg`, `raszowa-cab.jpg`);
  the MaSzyna root's two files in `~/characters-03/psk-ms-root-files`.
