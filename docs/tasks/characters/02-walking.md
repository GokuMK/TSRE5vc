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

## Checks
- `character-body` (15): on the ground, a 0.3 m step walked up, a 1 m
  platform's edge stopping it a radius off, a wall slid along, a 30 degree
  ramp walked up and a 60 degree one not, stairs of 0.17 m, off a ledge
  falling and landing, a jump (0.50 m), a jump under a ceiling, a turned
  and a scaled mesh, a mesh moving into it pushing it, the reach, rays.
