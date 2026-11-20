# 02 - Walking

Status: being built on `feature/characters` from 2026-11-20 (the user's
go). Design in [00-task-roadmap.md](00-task-roadmap.md) (the controller,
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
- **Station people walk round it:** each nearby station's Detour crowd
  has an agent for it (`StationPeople::Impl::placePlayer`): asked the
  character's velocity each step, no avoidance of its own, put again
  where the character is when they part by over a metre (logged with
  `TSRE_PEOPLE_LOG=1`).
- **Captures:** `renderer-capture` views take `"walk": {"keys",
  "seconds", "third"}` (`RouteEditorGLWidget::diagnosticWalk`: the keys
  held in steps of 1/30 s, then the character held for the capture).

## Left
- The head's motion as a player setting (comfort): smoothed only, for
  now.
- Moving cars behind the camera (not drawn) do not stop it; MaSzyna
  cars' extra models (interiors) are not in their collision.
- The pointer: put back with `QCursor::setPos`, which Wayland does not
  allow (TSRE through XWayland there).
- Gamepads.

## Checks
- `character-body` (15): on the ground, a 0.3 m step walked up, a 1 m
  platform's edge stopping it a radius off, a wall slid along, a 30 degree
  ramp walked up and a 60 degree one not, stairs of 0.17 m, off a ledge
  falling and landing, a jump (0.50 m), a jump under a ceiling, a turned
  and a scaled mesh, a mesh moving into it pushing it, the reach, rays.
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
