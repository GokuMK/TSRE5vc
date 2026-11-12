# Task 04 - Platform walk areas

Status: built on `feature/humans`, 2026-11-12 (the user: "proceed with next
task"). Checked on the stations of three stock MSTS routes and of PRUDNIK.

## Goal
Where people may walk at each station: a navigation mesh (Recast, Detour)
from the platforms' and buildings' triangles and the terrain, by the rules of
the roadmap's "Where people may walk" (accepted 2026-11-12). Tasks 05 and 06
place and move people on it.

## What was built
- **recastnavigation 1.6.0** in `thirdparty/recastnavigation` (Recast,
  Detour, DetourCrowd; zlib licence; the static library `tsre_recast`; a
  line in the About window). Sources unchanged.
- **`Humans::WalkArea`** (`src/tsre/humans/WalkArea.h`): the rules, on any
  thread, from triangles with roles (floor, solid, ground), platform lines
  with their sides and track lines. Out: a Detour mesh whose polygons carry
  their area (floor, edge zone, ground strip) and a walk flag (reached from
  the seeds), the seeds, and triangles for an overlay.
- **`Humans::StationWalkAreas`** (`src/tsre/humans/StationWalkAreas.h`): the
  route's stations (the platforms of one station name), each gathered on the
  main thread once its world tiles are loaded, then built on a worker:
  - the platforms' tracks every 2 m, the rail's top 0.325 m above the track
    database's height (the track shape's origin), each platform's sides from
    its world object;
  - the tracks within the area;
  - the objects within 16 m of the platforms and their roles, read again on
    the CPU (`Humans::shapeTriangles`, kept once read): MSTS shapes as the 3D
    cab's picker reads them (`SFileComplex`), glTF files with the new
    `GltfShape::readTriangles` (level 0, no textures, no GPU meshes);
  - the terrain every 2 m, as ground.
- **The platforms' reader shared with the displays**
  (`src/tsre/tdb/StationPlatforms.h`): the route displays now read their
  platform lines through it (the brief's "the displays' sampling, moved to a
  shared place").
- **Roles changed per shape and per object**:
  - `.sd`: `ESD_TSREWalk ( Floor | Solid | Ground | Ignore )`
    (`ShapeMetadata::walk`);
  - glTF: the scene's `extras.TSRE_walk` (same values; else the `.sd`'s);
  - a placed static object: `TSREWalk ( ... )` in the world file, the
    Properties panel's "Walk Area" (As its shape says, Floor, Solid,
    Ground, Ignore), saved by static objects.
- **The editor**: View > Walk Areas draws the walk areas of the stations
  within 1.5 km of the camera, built one at a time as the camera comes near:
  floor green, the edge zone yellow, ground strips orange, dropped surfaces
  (roofs, bench seats) grey, seeds as short lines (green on floor, orange on
  ground, red missed). Switching it off forgets them; on again builds them
  again with the edits made since.
- **The error list** (World, warnings, at the station): a platform without
  its world object or without a side set; a platform with its side set
  where there is no platform (the other side taken); a platform with no
  floor (a ground strip used); a platform with neither floor nor ground; a
  station with no walk area.
- **Developer switches**: `TSRE_WALK_AREAS=1` starts with the view on (route
  captures); `TSRE_WALK_DUMP=<folder>` writes each build's input and result
  as an OBJ file (groups: floor, solid, ground, track, platforms, seeds,
  mesh by area) for looking at a station from above.

## The rules as built
The roadmap's rules, with what real stations taught (the changes in
**bold**):

1. Roles by object type: static objects and collide objects floor; signals,
   speed posts, gantries, level crossings, pickups, hazards solid; terrain
   ground; the rest not in the walk area (track, dynamic track, forests,
   transfers, car spawners, ...).
2. Recast's rules: 40° slope, 2 m headroom, 0.25 m step, 0.3 m radius,
   cells of 0.15 by 0.1 m. Triangles of either winding (MSTS shapes are
   left-handed). Where floor and ground meet within a step, the floor wins.
3. Track: within 1.45 m of a track's centre, from 1 m below its rail to
   1.5 m above, nothing is walkable (closed before the radius erodes the
   rest).
4. Seeds every 2 m along each platform. **Each looks out from the track's
   band, up to 3 m, for the platform's edge (the first floor in its window)
   and stands 1 m in from it.** The window: 0.1 m below the rail's top to
   **2.0 m above it (Settle and Carlisle's platforms stand 1.45 m above the
   rail; the US's and Japan's high platforms 1.1-1.2 m)**. **A seed takes
   the lowest walkable floor in the window, not the highest: a bench's seat
   lies above the platform, the floor under it has no headroom.** Roofs lie
   above the window.
5. Only what the seeds reach is kept (a flood fill over Detour's polygons;
   the rest loses its walk flag).
6. **A seed finding no floor looks for the ground, in the window or down to
   2 m below the rail.** Where it finds ground, its 2 m of the platform walk
   on a ground strip, 1.7 to 4.2 m off the track's centre. Ground is closed
   everywhere else.
7. **The edge zone starts at the edge each seed found** (a ground strip's at
   the strip's start) and reaches 0.8 m plus a person's radius in, so a
   waiting body stays behind an 0.8 m line.
8. **Sides**: a platform's world object says left, right or both, as seen
   going from its first track item (flags 0) to its second (flags
   0xFFFF0000), as the simulation's doors read them; left is +1 of
   `WalkArea::Platform::side` in the editor's frame. **When the other side
   finds clearly more platform edges (over twice as many, a quarter of the
   seeds at least), the side is taken as set wrong: the other side is used
   and the error list says so.**

## Checks
- Suite `walk-areas` (20 checks) on made-up stations: 40 seeds on a
  platform's top; the platform floor, the edge zone at 1.95 m, nothing over
  the edge, under a bench (no headroom), on its seat (too high a step) or on
  a shelter's roof; floor under the shelter; the ground behind and the
  track closed; a halt's seeds on the ground and its strip walked, nothing
  past it; a platform on the other side; a platform with its side set wrong
  swapped; `ESD_TSREWalk` read; a glTF file's triangles read on the CPU at
  its node's place with its `TSRE_walk`; the triangle reader keeping a file.
- Routes, every station from above with the view on (route captures from a
  view per station; `TSRE_WALK_DUMP` plots looked at):

| Route | Stations built | Build per station (worker) | Gather (main thread) | Found in the content |
|---|---|---|---|---|
| EUROPE1 (Settle-Carlisle) | 18 of 18 | 0.09-0.99 s (Carlisle, 5 platforms) | 2-8 ms | Garsdale: both platforms' sides set wrong (swapped); Carlisle 5: no platform within 3 m (ground strip); Ormside, Armathwaite: platform items longer than the platforms |
| USA1 (Northeast Corridor) | 7 of 7 | 0.03-1.6 s (Philadelphia, 8 platforms; Washington, 21 platforms, 1.5 s) | 2-15 ms | Edgewood 2: no platform (ground strip) |
| JAPAN1 (Odakyu) | 23 | 0.05-1.6 s | 1-4 ms | Kazamatsuri: both sides set wrong (swapped); Mukogaoka-Yuen 4 likewise; Minami Shinjuku 1 and 2: no platform objects |
| PRUDNIK (OR_PSK) | 7 (Prudnik, Nysa, Dytmarow, Szybowice, two Glucholazy halts, KK-Lokomotywownia) | 0.13-1.5 s (Nysa, 4 platforms, 98,600 triangles) | 4-10 ms | Prudnik Towarowy 1-3: freight platforms without sides (no people) |

Washington's 21 island platforms join at the concourse; Carlisle's floors
under the train shed are walked, its roof is dropped; Prudnik's island and
side platforms are walked around its station building. A halt without a
platform shape walks on a ground strip (the suite's halt; Carlisle 5 and
Edgewood 2 on real routes).

## Left
- MaSzyna's e3d route shapes give no triangles yet (their objects are left
  out of walk areas).
- Terrain holes (tunnels) are not looked at: terrain is sampled everywhere.
- Walk areas are built when the editor's view asks. Task 05 builds them for
  the simulation (when the station's tiles are loaded and the simulation
  runs) and frees them with the tiles.
- The overlay shows the result (walk mesh, edge zones, strips, dropped
  surfaces, seeds); the input's roles (solid, ground, the track's band) show
  in `TSRE_WALK_DUMP`'s files only.
- Per-platform settings in the world's `PlatformObj` (no people here, the
  list of humans, the ground strip's width): later, as the roadmap's rule 8
  says.
