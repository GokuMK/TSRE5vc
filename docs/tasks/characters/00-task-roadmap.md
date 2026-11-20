# Characters Task Roadmap

People the player moves: a character the player walks with, the camera at
its eyes, boarding trains, sitting, taking a cab; later several characters
in one activity to switch between. The humans roadmap's second set
("people the player moves", `../humans/00-task-roadmap.md`).

Status 2026-11-20: design, the user's answers in (below), no questions
open. Starts after the server's task 06 is merged into main, on the user's
go. Branch
`feature/characters` (worktree `../TSRE5vc-characters`), from main
d590a8aa.

## The user's goal (2026-11-19)
- **Player characters, plural.** Even alone, an activity may have more than
  one character and the player switches between them (as in GTA), for more
  creative scenarios.
- **The simulation holds basic information about the characters.**
- **First, the player's control:** the camera takes a human as it takes a
  train, anchored to its head and eyes, so its animations and its movement
  move the camera.
- **The bodies and clips come from the humans work;** movement and
  collisions are the new part.
- **Collisions, physics and movement on the renderer's side;** the
  simulation checks only that a character is, or moved, where it is
  allowed.
- Server and client mode is being built alongside (`../server/`): this must
  fit it.

## The user's answers (2026-11-20)
Answers, not a go.
- **Order:** the server's task 06 first, merged into main; then this set:
  the player's control with basic, local simulation support; then the
  server team makes it work over the network (its task 07).
- **Where a character starts:** a run watched (no train driven): at the
  nearest platform; driving: already in the train (its cab). The same for
  activities and timetable sets.
- **Characters written by the activity, and switching between them:** a
  later task. The `.act` has room for them (its activity objects' section
  takes new kinds); a timetable set may have a file beside it, as Open
  Rails has for its own timetable additions.
- **Where a character may walk:** within 100 m of the nearest track by
  default, so it fits what routes are built for; a route may set another
  distance in its `.trk`.
- **What blocks:** alpha-tested parts do (fences); cruciform vegetation
  does not (or a character gets stuck in grass). A shape's `.sd` may say,
  and a world object overrides it in its properties (Default, yes, no,
  other values).
- **MSTS's own collision data:** collide objects and their collide flags
  (`.w`), bounding and complex boxes (`.sd` `ESD_Bounding_Box`,
  `ESD_Complex`); the boxes are almost never written.
- **e3d:** triangle reads upgraded as a generic `ComplexShape` feature (S
  files, glTF, e3d).
- **Drive stays** (the camera in a cab, no body).
- **Mouse:** the pointer captured while controlling a character: a game's
  interaction, of no use in the editor (unlike the free camera, which
  edits while it flies).

Then, the same day:
- **Vegetation:** a material says it (MSTS's cruciform light materials,
  the vegetation flag), not the parts' geometry.
- **The collisions: TSRE's own**, for now.
- **The cab:** Leave stays as now (driving given back to the AI). New:
  **Stand up** (leave the chair): the character stands in the cab, the
  train still driven by the player. The player may get off the loco at a
  stop and look round (have the passengers boarded?) with no AI taking
  over: the train is the player's until Leave. The cab's keys work only
  seated (the cab's view); standing, the character has the keyboard.
  Opening the door and jumping out (a moving train): to answer later.
  Controls limited while the driver is out of the cab: maybe a later
  task.
- **Boxes** only where the route's designer chose them (Box): never
  under Default.
- **Using things as in the cab:** what the character can use (seats,
  doors, the driver's place, anything) shows its label under the cursor;
  a click does it, or a use key (often handier).
- **Standing up** from a seat: the jump key, for now. Not from the
  driver's place: seated there the cab has the keyboard.
- **Standing in the cab** while driving, the cab's controls still work
  (the mouse); the cab's keys only seated.
- **Starting:** a Walk button in the run's panel (Activity Tools, F4),
  under Reset (the speed's list needs less room); it turns into Leave
  while walking. Walk puts the character at the nearest allowed place
  from the camera. Enough for now.
- **The first steps on the renderer's side only:** the simulation need not
  know of characters yet (the camera and the cab's views are the
  renderer's anyway). Collisions, movement and animations first; the
  simulation's part later.

## Server roadmap
Its task 07 (another session's design, 2026-11-17) keeps one person per
player in the simulation. By the user's order, this set builds the
characters on the renderer's side first; the simulation's part comes
later, local first, and the server team then takes it over the network
(its task 07 becomes that step). Characters per activity and switching
come later still.

## What TSRE has

| Need | What there is | Where |
|---|---|---|
| Bodies | 20 humans (full set), 3 basic; levels, impostors, PBR | `HumanSet`, `assets/humans`, `appdata/0.7/humans` |
| Clips | 33 a human: idles, walks (stroll and brisk), sit down, sitting, stand up; steps into a car and out; stairs in a car | the shared `people_clips.glb` (humans-gen) |
| Blending | ozz blends several clips by weight; `GltfShape::setClips` takes several plays a state | `SkeletalAnimation`, `GltfShape` |
| IK | ozz's two-bone and aim jobs are compiled in, unused | `thirdparty/ozz-animation` |
| A person drawn and posed | `Crowd`: steered people (place, way, speed, gait), clips by name (`steerClip`), tiers | `src/tsre/humans/Crowd.h` |
| A camera riding an object | `GameObj::getCameraPosition`, `getCameraMount`, `keyTaker`, `cameraAttached`; `CameraFree` rides a consist or a 3D cab's head this way | `src/tsre/camera`, `Consist` |
| Keys | `KeyTaker` (the ridden object takes keys before the camera), the UI's input router, driving keys | `KeyTaker.h`, `DrivingKeys` |
| Ground height | the terrain's height anywhere | `TerrainLib::getHeight` |
| Floors and walls | a shape's triangles read on the CPU, cached (S files, glTF; not e3d); roles per object type, per shape (`.sd` `ESD_TSREWalk`) and per object (`.w` `TSREWalk`, Properties: Walk Area) | `ShapeTriangles`, task humans 04 |
| MSTS's collision data | `.sd` `ESD_Bounding_Box` and `ESD_Complex` boxes read; `.w` collide objects and their flags read and edited | `ShapeMetadata`, `PropertiesStatic` |
| Walkable areas | Recast and Detour: stations' walk areas (platforms, edge zones, ground strips), the true floor (`floorAt`), Detour's crowd | `WalkArea`, `StationWalkAreas`, `StationPeople` |
| Cars inside | aisles (floors, stairs), seats, doors, passages (`ESD_TSRESeats`); door parts; doors opened by the simulation | `ShapeSeats`, `CarDoors`, task humans 12 |
| Boarding | passengers walk to a door, climb the steps (clip) and go along the aisles to a seat | `StationPeople`, tasks humans 06, 12 |
| Cabs | 3D cabs: the head's place and box (`CameraMount`), clickable controls | tasks cab3d, driving 08 |
| The simulation | session, link (local and remote), commands with sequence numbers, frames, Drive and Leave, a digest of the run's input; passengers counted in the core, walked and drawn on the view's side | `src/tsre/sim`, server tasks 04, 05 |

## What is missing
1. **Characters in the simulation** (later): their state in the core,
   commands, checks, frames.
2. **A collision world** around the character: the walk areas cover
   stations only and give a navigation mesh, not a body that collides.
3. **Triangles by part with their materials:** `ShapeTriangles` gives one
   list of triangles a shape (enough for walk areas); the rules below need
   each part's material (alpha-tested, vegetation). e3d models give none.
4. **A controlled person in the crowd** (clips, times and weights its owner
   sets each frame) and its head's place read back (no shape state's node
   matrix is readable today).
5. **Clips for control:** backwards, sideways, jogging, running, jumping,
   crouching, climbing onto a platform, using things. The sources have
   them; not cut or retargeted yet.
6. **The character as a ridden object:** its eyes, its keys, first and
   third person, the pointer captured.
7. **The distance from the nearest track** for the reach (the view now,
   the simulation later): no spatial index of track lines answers it
   now.

## Design

### The simulation: nothing at first
The first tasks keep the character on the view's side, as the stations'
people are (they read the frames and never write back). What it needs
from the simulation is there already, in the frames: the cars as drawn,
their doors open or shut, the trains' speeds, which train the player
drives. Nothing is written back: standing up keeps the driving as it is
(Drive and Leave as now), the cab's keys are taken by the character's or
the train's `KeyTaker` on the view's side, and the view itself checks
boarding (a door open, the train standing).

The character's own state is kept as the core will keep it later, so that
step only moves it: its human, its place (on the ground: tile and x, y,
z; in a car: train, car, place in the car's frame; in a cab: train and
end), heading, speed, what it does (stand, walk, run, sit, board, step
off, drive).

**Later (with the server's task 07):** the state in the core; commands
(walk, its pose 10 to 20 a second, board, step off, the cab), refusals
with the core's place; simple checks (within the reach of a track, not
faster than 8 m/s, no jumps of place, doors open on a standing train, not
inside a car's box from the ground, within a car's length); frames
carrying characters to other players. Running back and resetting: a
character in a car that no longer runs is put on the ground where its
car was.

### Starting to walk
- **Walk** in the run's panel (Activity Tools, F4): the grid's second
  row, under Reset; the speed's list spans the columns before it. Enabled
  while a run exists; while walking it turns into **Fly** (the user,
  2026-11-20): back to the free camera; Drive's stays Leave.
- **Where:** the nearest allowed place from the camera: the floor straight
  under it (an object, the terrain, a car's floor when the camera is in a
  car), within the route's reach of a track and where the capsule fits;
  else the nearest such place searched outward from there. The character
  faces the camera's way, the camera goes to its eyes. None found: Walk
  says so and nothing changes.
- **Driving** (Drive, the camera in the cab): Walk seats the character at
  the driver's place, the cab's view kept; the jump key stands it up
  (Trains). Drive while walking: the character taken to the new cab's
  seat. Leave (driving): the train to the AI, the character stands where
  it is.
- **Leave** (walking): the character goes; the camera stays where its
  eyes were, a free camera again (in the cab's view when it sat there
  driving).
- **Stop** ends walking; **Reset** and running to a time keep the
  character, one in a car whose train is gone put on the ground where its
  car was.
- Which human: the run's humans set, the player's choice later (a
  setting); a seed until then.

### The controller (task 02)
The character is a `GameObj` the riding camera rides, as a consist:
- `getCameraPosition`: its eyes, from the head joint (a point ahead of it),
  the frame turned as the car it is in (none on the ground), so a curve
  turns the view with the car;
- **the pointer captured** while the character is controlled: hidden and
  held in the window's middle, every move a turn of the look; Escape (or
  the UI taking the keys) lets it go. Qt has no pointer lock: the pointer
  is put back each move (`QCursor::setPos`), which works on Windows and
  X11; Wayland does not allow it (TSRE through XWayland there);
- moving, the body turns to the look's way; standing, only past about 60
  degrees;
- `keyTaker`: move (W A S D and the arrows), run, walk slowly, jump
  (standing up from a seat for now), use (E, the user's choice: the
  thing under the cursor, as a click; free in the driving keys and the
  camera's), first or third
  person; the camera's own keys stay the camera's;
- **using things as in the cab:** a ray from the eyes through the cursor
  (the view's middle while the pointer is captured: a small mark there)
  meets the usable things within reach (about 2 m): seats, doors, the
  driver's place, the cab's door; the nearest gets its label under the
  cursor (as the cab's controls get theirs, `cabHover`), a click or the
  use key does it.
  Each thing a small volume in its car's frame (a seat's cushion, a
  door's opening), tested as `CabPicker` tests its parts; only the ones
  usable now (a free seat, an open door of a standing train);
- **the head's motion moves the camera**, smoothed (a step's sway and bob
  kept, its jerks taken out); how much as a player setting (comfort);
- **first person:** the body drawn with its head shrunk in the main view
  (the shadow from the whole pose), so looking down shows the body and
  legs; **third person:** behind and above, pulled in front of walls;
- **stations' people** walk round it: it is an agent of Detour's crowd
  there, moved by the controller;
- gamepads later.

### Collisions (task 02)
- **The body:** a capsule (0.3 m radius, the human's height), kinematic:
  collide and slide, a step up to 0.35 m, slopes to 45 degrees, falling,
  snapped to the floor going down steps.
- **The reach:** within 100 m of the nearest track's centre line by
  default; the route's `.trk` may set another (`TSRECharacterReach ( m )`,
  the name to settle). Past it the character is held as by a wall; later
  the simulation checks the same distance. Only objects within the reach are
  ever read for collisions.
- **What it collides with:** each shape's triangles of its most detailed
  level, read once per shape file by part with its material (a generic
  `ComplexShape` read: S files, glTF, e3d), cut by the rules below into a
  collision mesh with a small tree for queries, shared by every object
  placing that shape; the objects near the character placed into cells as
  it walks, on a worker (`QThreadPool`); the terrain's quads as triangles.
  No simplified shapes made at load (below).
- **The rules, by default:**
  - vegetation does not collide (grass, bushes, tree crowns), by the flag
    TSRE draws it with (`RenderItem::Material::vegetation`): an MSTS part
    with a cruciform light material (-9, -10: `MstsMaterials`); a glTF
    shape with an impostor level, all of it (its trunk too; a material's
    word if trunks should block); a tree's MSTS trunk is a part of its own
    and blocks;
  - every other part does, alpha-tested ones too (fences, gates, mesh
    panels);
  - by object type as the walk areas: static objects, collide objects,
    signals, speed posts, gantries, level crossings, pickups, hazards
    collide; track, dynamic track and forests do not.
- **The shape's say and the object's:** `.sd` `ESD_TSRECollision`, a world
  object's `TSRECollision` (Properties: Collision), as the walk role is
  written:
  - **Default:** the shape's, else the rules above;
  - **Solid:** every part, vegetation too (a hedge of crossed quads);
  - **None:** walked through (clutter, a shape with broken faces);
  - **Box:** the shape's boxes (its `ESD_Complex` boxes when written, else
    its bounding box): busy shapes of many small parts. Only where the
    designer chose it: never under Default (the user, 2026-11-20).
- **Cars:** each car near the body keeps its shape's collision mesh in its
  own frame; the capsule is moved into that frame for the test. Moving
  trains need no moving bodies: a car moving into the character pushes it
  out. Inside a car the character lives in the car's frame, the floor from
  the car's aisles where it has them, walls and seats from its triangles.
- **The platform's edge** (0.55 to 0.76 m above the rail, more above the
  ballast) is too high for a step: down is a fall, up a climb (the 1 m
  climb clip) or the ramps.
- **The walk areas** take the same triangle reads (e3d stations then
  build too); their roles stay as they are.

#### Simplified shapes or the triangles
The triangles, as drawn, of the most detailed level. Simplified shapes
made at load would be boxes or convex hulls: wrong for what a character
walks in and under (a platform with its shelter, a bridge, a station hall,
a car's inside: a box fills them), or slow (a convex split takes seconds a
shape). The walker needs exact floors, and the triangles are read already
for the walk areas; a capsule's query against a shape's tree costs
microseconds. Memory: only shapes within the character's cells, once per
file. Boxes stay for the shapes they suit (Box above).

#### TSRE's own controller or Jolt Physics
Chosen (the user, 2026-11-20): TSRE's own, for now, behind a narrow
interface (sweep a capsule, cast a ray, the floor under a point), so Jolt
could replace it if rigid bodies are wanted later.

| | TSRE's own | Jolt Physics |
|---|---|---|
| What it is | a kinematic capsule: collide and slide, steps, slopes, falling, written here | a physics library (MIT, C++17) with a kinematic character (`CharacterVirtual`), rigid bodies, constraints, rag dolls, vehicles; used in big games and in Godot |
| The world | queries straight on TSRE's cached collision meshes; nothing to keep in step | its own copy of each mesh and a world kept in step with TSRE's: tiles loaded and dropped, objects moved in the editor, every car near the character moved each frame |
| Moving trains | cars tested in their own frames: riding exact, no moving bodies | cars as kinematic bodies, the character on a moving platform (Jolt handles it) |
| Hard cases | ours to get right: several contacts in a corner, step edges, jitter, sliding on slopes, other characters (capsule against capsule) | solved and tested there |
| Later | no props, no rag dolls, nothing pushed | props pushed, people hit by trains falling, rolling things |
| Precision | TSRE's tiles | single precision: its world kept round the character |
| Building | nothing new | the largest library TSRE would carry; its MinGW build to check |
| The simulation's check | the same either way | the same either way |

### Riding a moving train (task 03)
A character standing on a car belongs to that car: its place, heading and
velocity are kept in the car's frame, and its place in the world is the
car's matrix, as the car is drawn this frame, times its place in the car.
The train carries it exactly: nothing slides, and no physics of moving
floors is needed (the 3D cab's head rides so now).
- **Which frame:** the floor under its feet decides. Its feet on a car's
  triangles (or its aisles): that car's frame; on the platform, the
  terrain or any other object: the world's.
- **Changing frame** (boarding at a door, stepping off): its place and
  velocity turned from one frame into the other once, at the moment its
  feet change floor. The doors are open only while the train stands, so
  the step is between two floors at rest; jumping onto or off a moving
  train is the later question.
- **In a car** it collides with that car only (its mesh is still in its
  frame, the train moving or not); the platform outside is not tested
  while the doors are shut. Walking through gangways to the next car
  later.
- **The camera** rides with it (its eyes in the car's frame, the view
  turned with the car), so curves, the car's roll on cant and its bounce
  move the view as in the cab.
- **The body** stays upright in the car's frame: it leans with the car.
  Swaying with the train's starts and stops is a later touch (a small
  additive clip from the car's acceleration).
- **Riding on the outside** (a step, a roof) is the same rule: standing on
  the car's triangles puts it in the car's frame.

### Clips (task 01)
humans-gen cuts and retargets them as the others, into the shared clips
file, named (`TSRE_use` named) with a new word for what each is
(`TSRE_gait`: forward, back, side, run, sprint):
- 100STYLE `Neutral_BW` (backwards), `Neutral_SW` (sideways, mirrored for
  the other side), `Neutral_FR` (running);
- UAL1 `Jog_Fwd_Loop`, `Sprint_Loop`, `Jump_Start`, `Jump_Loop`,
  `Jump_Land`, `Crouch_Idle_Loop`, `Crouch_Fwd_Loop`, `Interact`;
- UAL2 `ClimbUp_1m_RM` (onto a platform from the track).

The controlled person's pose: a blend by its velocity in its own frame
(forward, back, sideways; walk, jog, run by speed), the clips' phases
kept in step (one cycle), cross-fades of 0.2 s between states (stand,
move, jump, sit, steps, climb). Turning on the spot slides the feet at
first; a turning step clip later.

### Trains (task 03)
- **Boarding and stepping off** by clicking an open door: the passengers'
  door script (the steps clip from the door's outside point to the aisle),
  only by an open door of a standing train (from the frame); walking up
  the steps freely later.
- **Inside a car:** walking (above), riding.
- **Seats** (the user, 2026-11-20: as the passengers sit, the seat marked
  taken): a click on a free seat of the car's `ESD_TSRESeats` (its label
  under the cursor, as a cab's control). The seat is
  taken at once in the same table the passengers take seats from
  (`StationPeople`'s per-car `taken`, by a mark of the player's
  character), so no passenger walks to it or is seated there when the
  car's people are made; a seat a passenger has taken, or is walking to,
  is not offered. The character steps to the seat's row on its aisle and
  sits as a rider does (the same plan: `sit_down` with its hips' travel
  landing on the cushion, facing the seat's way, then `sitting`); the
  camera stays at its eyes, the view out of the window riding with the
  car. The jump key (for now), or E held: `stand_up`, back to the
  aisle, the seat free. Every check of a seat taken must count the character's mark (some
  read `>= 0` today, a rider's index). A car without seats written: no
  sitting. The character is not one of the car's passengers (their
  counts stay as they are).
- **The cab:** a click on the driver's place: the character sits, the camera
  takes the 3D cab's head place (`CameraMount`), the keys go to the train
  (`Consist::keyTaker`), the pointer is free for the cab's controls, as
  now. **Stand up:** not the jump key (seated, the train has the keyboard:
  Space is its brake), and not a press of E (seated, E still uses the
  control under the cursor): **E held** about half a second (the user,
  2026-11-20, for now: easy to change). Seated, a tap of E acts on its
  release, so a hold can be told from it; a hint "Hold E: stand up"
  while seated. The camera back to the
  character's eyes, the keyboard the character's, the pointer
  captured; the train still the player's, driven as left (its controls as set). **Standing in the cab** while
  driving, its controls still work by the mouse: the cab's picker takes
  the ray from the eyes through the cursor's mark, the controls' labels
  as seated, a click or a drag works them (while a drag holds a lever,
  the pointer's moves go to it and the view holds still); the use key
  clicks the control under the cursor. The cab's keys work only seated. Inside the cab the
  character collides with the 3D cab's shape in the loco's frame; it
  walks out by the cab's door (a click) when the train stands, and back
  in to sit down again. **Leave** gives the driving to the AI, as now, wherever the
  character is.
- Jumping out of a moving train, and controls limited while the driver
  is out of the cab: later.
- **Drive** (no body) stays for the editor and runs watched without
  walking.

### Later
- **Characters in the activity** (an activity object of a new kind;
  Open Rails' reading of one to check) and for timetable sets (a file
  beside the set); switching between them (a HUD list, a key); the ones
  left stand, ride, or hold their cab.
- **Other players' characters** (the server's task 07): from the frames,
  interpolated between poses (a tenth of a second behind), drawn by the
  crowd, their clips from speed and what they do.
- **IK:** feet on slopes and stairs (two-bone), the head turned to the
  look (aim: others see where a player looks); hands on handrails.

## Tasks
Proposed; each task's doc is written when it starts. After the server's
task 06 is merged into main; all on the renderer's side.
- [ ] `01-controlled-person.md`: the clips (humans-gen), the crowd's
  controlled person with its blend, the head read back; the viewer and a
  suite.
- [ ] `02-walking.md`: triangle reads by part (`ComplexShape`, e3d too),
  the rules and the Collision setting (`.sd`, `.w`, Properties), the
  collision world and capsule, the reach; the character as a ridden
  object, Walk and Leave in the run's panel, keys, the captured pointer,
  using things (label under the cursor, a click), camera (first and third
  person, the head's motion); stations' people avoiding it.
- [ ] `03-trains.md`: riding in a car's frame, doors, inside cars, seats;
  the cab: sit, Stand up, out by its door at a stop and back.
- [ ] `04-in-the-simulation.md`: the character's state in the core, local
  first; then the server's task 07 over the network.
- Later: characters in the activity and switching; IK.

## Questions for the user
None open. Later: jumping out of a moving train; controls limited while
the driver is out of the cab.
