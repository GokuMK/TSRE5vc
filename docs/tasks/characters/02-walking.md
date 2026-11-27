# 02 - Walking

Status: built on `feature/characters` 2026-11-20 (the user's go; step 5
the same day), not merged. Design in [00-task-roadmap.md](00-task-roadmap.md) (the controller,
collisions); the controlled person in [01](01-controlled-person.md).

## Goal
Walk on the route: the camera at the character's eyes, its keys and the
captured pointer, colliding with the route's objects, the terrain and the
cars, within the route's reach of a track; Walk and Fly in the run's
panel. Riding, doors, seats and cabs are task 03.

## Steps
1. **The collision core** (`src/tsre/characters`, no route needed):
   - `CollisionMesh`: a shape's triangles in its own space, listed by
     cells of a grid over x and z (a metre, coarser past 256 cells);
     the floor under a point, a ray, a capsule's contacts (the nearest
     points of a segment and each triangle within its radius).
   - `CollisionWorld`: meshes placed by matrices (a turn, a move, one
     scale), moved when a car moves; the terrain's height; the same
     queries in the world's frame.
   - `CharacterBody`: kinematic, a capsule floating a step (0.35 m) over
     its feet, the feet on the floor found below (the middle, else four
     points half its radius round it: an edge, a gap): steps and walkable
     slopes (45 degrees) walked up, steeper ones not; down a step it keeps
     to the floor, more and it falls; gravity, jumps, a ceiling stops a
     jump; the capsule pushed out of what it touches (on the ground,
     aside only: walls stop it, it slides along them); where it may not
     be (the reach), it stays out. Moves in steps of at most 0.1 m and
     1/60 s.
2. **Collision triangles by part:** one read for every shape kind (a
   `ComplexShape` static, by the file's kind: S files, glTF, e3d), on a
   worker without making a drawn shape: each part's triangles of the most
   detailed level placed at rest, with its surface (opaque, alpha-tested,
   blended) and whether it is vegetation (MSTS cruciform light materials;
   a glTF file with an impostor). The rules (vegetation never blocks,
   every other part does), `.sd` `ESD_TSRECollision` and a world object's
   `TSRECollision` (Properties: Collision): Default, Solid, None, Box (the
   shape's `ESD_Complex` boxes, else its bounding box).
3. **The route's collision world** about the character: the objects near
   it placed in cells as it walks (a worker reads their shapes, cached by
   file), the terrain's height, the reach (100 m from the nearest track
   by default; the route's `.trk` `TSRECharacterReach`), the cars from the
   simulation's frames as drawn.
4. **The character:** a `GameObj` the camera rides; drawn by a crowd of
   the run's humans set, posed by Locomotion; keys (move, run, walk
   slowly, jump, first or third person), the captured pointer, the head's
   motion smoothed; Walk and Fly in the run's panel, the nearest allowed
   place from the camera; stations' people walking round it.
5. **The run keeps its character** (the user, 2026-11-20, after the
   report of steps 1-4): Fly leaves it standing where it is; Walk goes
   back to it, Here walks it from the camera; the run's end lets it go.

## As built
- **Step 2:** `ComplexShape::readTriangleParts` (by the file's extension:
  `SFileComplex::cpuTriangleParts` (triangle lists only, the first level,
  each matrix at rest, by prim_state: TexDiff opaque, else alpha-tested by
  its prim_state, else blended, PBR shaders by theirs; light materials -9
  and -10 vegetation), `GltfShape::cpuTriangleParts` (level 0 by node and
  material surface; all vegetation with an impostor beside the file),
  `E3dShape::cpuTriangleParts` (the most detailed level's mesh parts, those
  hidden at the start and under them left out, by their passes; parsed
  from the file, not the shared model whose vertices go to the renderer)).
  `Characters::shapeCollision` (`src/tsre/characters/ShapeCollision.h`):
  the mesh by mode, cached by file and mode, any thread. `ShapeMetadata`
  reads `ESD_TSRECollision` and tells its bounding boxes from its complex
  ones; `WorldObj::tsreCollision` read, copied and written by
  `StaticObj::save` (`TSRECollision ( Box )`), Properties: Collision.
  `ShapeLoader::cancel` returns on a worker: a shape made and let go
  there (triangles read: walk areas, collisions) never had a loader job,
  and the loader's table is the main thread's (it was read from workers).

- **Step 3 (objects, terrain, reach):** `Characters::RouteCollision`:
  the objects' meshes placed within 48 m, read ahead on workers within
  96 m, read at once within 16 m; placed again each 4 m or when a read
  ends; the frame the character's tile's; the terrain's height
  (`TerrainLib::tryGetHeight`); the reach from the TDB's vector sections
  (every 2 m, within the reach and 150 m, read again 100 m on), the route's
  `.trk` `TsreCharacterReach` (read and written back) or 100 m. The
  trains' cars within 40 m every update, as last drawn (`Eng::lastDrawn`:
  a moving train's only when drawn within the last second, a car not
  drawn would be where it was; a standing one's as last drawn), each its
  shape's mesh (the main shape; a MaSzyna car's other models not yet).
- **Step 4 (the character):** `Characters::Character`, a `GameObj` the
  camera rides (its place at the eyes, the look free: a mount with no box
  and no turn limit), its `KeyTaker` (W A S D and the arrows, Shift run,
  Ctrl slowly, Space jump, C crouch, V first or third person); speeds:
  walk 1.4, run 4.0 (ahead only), slowly 0.7, back and aside at most 1.0,
  crouching 0.8 m/s; it speeds up and slows down at 10 m/s² (2 in the
  air); its body faces the camera's way while it moves, standing turns to
  it past 60 degrees; drawn by a crowd of one (a human of the route's set,
  else TSRE's, whatever the people setting says; the route's name picks
  it), posed by Locomotion from the way it actually went (walls stop the
  legs too); the jump's loop while in the air; the eyes at the head joint,
  0.09 m up and 0.11 m ahead, smoothed over 0.05 s; third person 3 m
  behind along the look, in front of what is between. The near plane
  (0.2 m) clips its own head: none hidden. `Route::pushRenderOverlays`
  draws it after the station people.
- **The view:** Walk in the run's panel (under Reset; Fly while walking;
  off while a train is driven): the nearest place from the camera
  (`Character::placeNear`: the floor straight under it within the reach
  where a body fits, else rings 2 m apart to 60 m; none: told without
  waiting). The pointer captured (the application's blank cursor: the
  view may be a native window; it goes back to the view's middle each
  move, turning the look as the free camera's drag does); Esc lets it go,
  a click takes it again; clicks are the character's while walking.
  Walking ends when the camera rides something else, the run stops or a
  train is driven. Its keys let go of with it (the view held them: a
  crash found by the captures, a released character's keys called).
- **The look's pointer** (2026-11-26, the user's report: turning left and
  right pushed the view up or down at a low sensitivity):
  - **The cause:** at fractional scaling (the Deck's 250 %) the view's
    middle in view pixels lies between screen pixels, so the centred
    pointer lands up to a screen pixel off it; every move read against
    the middle carried that error, as large as a slow move's own counts.
    Measured: Qt reads it 0.00, 0.40 view pixels off at scale 2.5.
  - **Windows (done):** `LookPointer::position` (`src/routeEditor`) reads
    the cursor in screen pixels (`GetCursorPos`); a move is the cursor
    against where the last centring put it (`centrePointer`), divided by
    the scale, so the sensitivity is as before. A stale queued event
    reads the cursor as it is now; the centring's own event reads nothing.
  - **Elsewhere (done, the fallback):** Qt's event positions less the
    landing error, learned from the event the capture's centring makes
    (under a view pixel; a larger first reading is a move, nothing
    learned). `TSRE_LOOK_QT_POINTER=1` forces this path on Windows for
    testing. Both paths tried by the user on Windows.
  - One log line a capture: `Character: the look's pointer centred; Qt
    reads it X, Y view pixels off the middle` (Windows), or `the look
    reads Qt's positions; the centred pointer lands X, Y ...` (fallback).
  - The look's speed is `core.explore.mouseSensitivity` (System → Explore
    mode → Mouse sensitivity, until a Controls tab exists), not the
    editor's `core.camera.mouseSensitivity`.
  - Open: X11 and Wayland, under Left.
- **Station people walk round it:** each nearby station's Detour crowd
  has an agent for it (`StationPeople::Impl::placePlayer`): asked the
  character's velocity each step, no avoidance of its own, put again
  where the character is when they part by over a metre (logged with
  `TSRE_PEOPLE_LOG=1`).
- **Captures:** `renderer-capture` views take `"walk": {"keys",
  "seconds", "third", "back", "fly"}` (`RouteEditorGLWidget::diagnosticWalk`:
  Here, or Walk back to the run's character with `back`; the keys held in
  steps of 1/30 s, then the characters held for the capture; Fly after it
  with `fly`). A view without `walk` ends walking where its camera is:
  the character left standing.
- **Step 5, the run's characters:** `Characters::Cast`
  (`src/tsre/characters/Cast.h`), on the main thread: the run's
  characters (one for now) and the player's (the one the camera rides,
  none while flying), of a run (`SimulationController::shownRun()`, the
  run shown's id: a start, a reset or running back to an earlier time
  makes a new one). The view's tick updates them all; one of another run,
  or none running, lets them go (`leaveCharacters`, also when the route is
  left, before its shapes go). A character left (`setControlled(false)`):
  its keys let go of, idle, its own way (not the camera's), landing when
  in the air; at rest past 400 m from the camera on the ground (neither
  moved nor drawn). Fly: the camera a free one behind its eyes along the
  look (third person's place, in front of what is between); the camera
  riding something else (Drive, Follow) leaves it where the camera was.
  Walk: back to it, looking its way, level; none of this run: Here. Here
  (the panel's, under Stop; the speed's list a column narrower): the
  nearest place from the camera as before, the same character moved
  there standing (made when none). Drawn and among the station people's
  agents walking or left (`Route`, `StationPeople`: the run's first).

### Fixed later: a station that is one large shape (2026-11-26)
The user's report from a hardware test: at CMK's Krakow Glowny (game root
CMK_E30) the walking character started on the ground and the station had
no collisions. Its platforms are one shape (`CMK-krg-p1.s`, about 420 m
long) whose origin lies past their south end, in the next tile.
`RouteCollision` looked only in the tiles within `Ahead` (96 m) of the
character, and took an object's reach as half its bounds' diagonal about
its origin. Under the middle of the platforms neither found the shape, so
there was only terrain, and Explore's walking start (which probes the
same collision world) stood the character on it. Now:
- objects are also looked for in the tiles within `Ahead + LargestReach`
  (1024 m). Those tiles and their objects are not waited for, and only
  shapes already loaded count there, since their size is not known before;
- an object reaches as far as its bounds' farthest corner from its origin
  (`RouteCollision::reachOf`, 10 m at least). A shape not loaded yet (not
  drawn, no bounds) reaches `UnknownReach`, 50 m, a stop-gap (2026-11-27;
  10 m before). The median shape reaches 13-30 m on the stock routes, CMK
  and PRUDNIK, and 69-95 % of objects reach past 10 m; 50 m covers 70-97 %
  of them.

Platform II's middle, north end and south end now stand on the platform's
floor (0.65 m over the rail; the middle was on the terrain, 1 m lower).
The walk areas take their objects the same way since (task humans 04's
"Fixed later").

## Left
- **A radius in the world object's block** (the user, 2026-11-27): how far
  its shape reaches from its origin, written by TSRE whenever it knows the
  shape (placed, moved, its shape changed, its tile saved with the shape
  loaded), as `TSREWalk` and `TSRECollision` are. It would replace the 50 m
  `UnknownReach` for shapes not drawn yet, here and in the walk areas.
  MSTS's own spheres were reviewed for this first (all six stock routes,
  707 W files) and are no fit:
  - `ViewDbSphere`s form a tree, one root per tile (about 1.2 km) and up to
    two levels below; an object's `VDbId` points at a sphere of any level,
    shared by most often 3-5 objects (16 % point at the tile's root): a
    culling structure, not a bound per object. The sphere an object points
    at has a median radius of 36-182 m.
  - Only MSTS's Route Editor builds them. On the stock routes 87 % of static
    objects have one, and track, forests, hazards, platforms, sidings, car
    spawners and transfers never do. The routes made in TSRE barely have
    them (CMK 4 %, PRUDNIK 2 % of objects): TSRE writes them back as read,
    new objects get none, and moved ones keep their old one.
  - 87-98 % of objects lie inside theirs (their shape's declared sphere,
    not its geometry: stray points stick out).
- The head's motion as a player setting (comfort): smoothed only, for
  now.
- Moving cars behind the camera (not drawn) do not stop it.
- MaSzyna cars' extra models (interiors, cabs) in their collision: task
  03 (the user: the complex cabs are in that format).
- A character left on a track: a train goes through it (the renderer's
  only; the simulation's part, task 04).
- The pointer (see "The look's pointer" above):
  - **X11:** `LookPointer::position` is a commented placeholder
    (`XQueryPointer` on the root window, `<X11/Xlib.h>`, libX11 linked in
    the Linux build): to be written and tested on Linux. Until then Linux
    reads Qt's event positions less the learned landing.
  - **Wayland:** Qt's own Wayland platform cannot put the pointer back
    (`QCursor::setPos` does nothing): a look there needs the
    relative-pointer and pointer-constraints protocols, a task of its own.
    TSRE runs through XWayland there; whether the X11 path works under
    XWayland is untested.
  - The landing (Qt path) is learned once a capture: a window moved while
    the pointer is captured reads it stale until the next click.
- Gamepads.

## Checks
- `character-body` (21): on the ground, a 0.3 m step walked up, a 1 m
  platform's edge stopping it a radius off, a wall slid along, a 30 degree
  ramp walked up and a 60 degree one not, stairs of 0.17 m, off a ledge
  falling and landing, a jump (0.50 m), a jump under a ceiling, a turned
  and a scaled mesh, a mesh moving into it pushing it, the reach, rays;
  step 5's cast (no route, a floor of its own): the player's turns to the
  camera's look, Fly's place 3 m behind the eyes along the look, one
  left keeps its way (not the camera's) and its keys let go of, resting
  500 m from the camera and not 3 m, a new run's character letting the
  old go, cleared; a shape's reach from its origin (centred, its origin at
  one end of 420 m, not loaded).
  With `TSRE_COLLISION_SHAPES` (a SHAPES folder; 400 shapes at most,
  `TSRE_COLLISION_LIMIT`): every shape's collision read in three modes,
  Default as Solid without vegetation, Box boxes, None nothing:
  - EUROPE1: 154 of 154 read, 21 with vegetation (`TREE3.S` 0 of 16
    triangles by default), 55 with alpha-tested parts, 1.1 ms a shape;
  - PRUDNIK (the first 400): 400 read, 152 with vegetation (`2dhzgr01.s`
    40 of 92), 275 with alpha-tested parts, 2.7 ms a shape;
  - MaSzyna `models/przytorowe`: 120 e3d models read, 7 with parts cut by
    alpha.
- `token-world` 76 (+4): `TSRECollision` read, copied, written by its
  name, not written for Default.
- `new-route` 52 (+2): `TsreCharacterReach` read and written back, not
  written when unset.
- **Live** (`tests/renderer/characters-walk-views.json`, PRUDNIK at
  Raszowa, the PSK_2026 run at 12:11): Walk from the camera on the bank
  outside the platform's mesh fence; W 2 s: 1.9 m along the camera's way,
  stopped at the fence (alpha-tested: it blocks), the eyes looking over it
  at the passengers on the platform; third person behind him; W and Shift
  3 s: run to the fence, stopped there. On the platform
  (`walk3`): W 4 s, stopped by the standing EN57's side at the platform's
  edge; A 3 s, 2.97 m along the train (side steps at 1 m/s), first and
  third person; its agent among Raszowa's people. Images in
  `~/characters-02`.
- **Live, step 5** (`tests/renderer/characters-left-views.json`, the same
  run): Walk from the bank, W 2 s to the fence (643.36, 160.12, 245.11),
  Fly: the camera 3 m behind its eyes, the character left standing at the
  fence (seen from the bank's free view too, among the passengers behind
  the fence); Walk back from a free camera on the platform: at the fence
  again (643.30, 245.16: 7 cm, its last steps' speed run out after the
  capture's hold), first person, looking its way, the same eye height as
  steps 1-4's capture; Here from the platform: the same character (one
  human loaded) moved there (641.00, 160.80, 247.40); the bank's view
  again: the fence without it. Images in `~/characters-02/left`
  (`raszowa-left.jpg` the sheet).
