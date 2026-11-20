# 01 - The controlled person

Status: built on `feature/characters`, 2026-11-20 (the user's go). Design
in [00-task-roadmap.md](00-task-roadmap.md). Waits for the user's look
(the captures below) and task 02, which moves it.

## Goal
A person whose body its owner moves: the clips a player's character needs,
blended from what it does (moving any way at any speed, crouching, actions
such as sitting down or jumping), drawn by the crowd, its head's place read
back for the camera (task 02). Nothing yet moves it in the world: tasks 02
and 03.

## Clips (humans-gen)
Into the shared clips file `people_clips.glb` (48 clips, 2.39 MB; 35 and
1.87 MB before), each `TSRE_use` named (never a person's idle or walk),
found by name as the steps and sitting clips are:

| Clip | From | Notes |
|---|---|---|
| `walk_back` | 100STYLE `Neutral_BW` | one cycle going straight back, 0.78 m/s |
| `walk_left` | 100STYLE `Neutral_SW` | side steps to the left, 0.58 m/s |
| `walk_right` | `walk_left` mirrored | |
| `run` | 100STYLE `Neutral_FR` | 1.57 m/s (the capture room is small) |
| `jog`, `sprint` | UAL1 `Jog_Fwd_Loop`, `Sprint_Loop` | 6.07 and 9.50 m/s as their feet sweep |
| `jump_start`, `jump_loop`, `jump_land` | UAL1 | |
| `crouch_idle`, `crouch_walk` | UAL1 `Crouch_Idle_Loop`, `Crouch_Fwd_Loop` | |
| `interact` | UAL1 `Interact` | |
| `climb_up` | UAL2 `ClimbUp_1m_RM` | onto a platform, root motion |

The idle (`idle_neutral`) and the walk ahead (`walk`) are clips the file had.

- **The other gaits' loops** (`scripts/style_loops.py`): backwards and side
  steps cut from stretches going within 15 degrees of their way from the
  body's facing (the backward walks drift up to 20 degrees aside; the side
  steps' take goes both ways), then turned to go exactly that way with the
  body facing +z; the right side steps the left ones mirrored (left and
  right joints swapped, y and z rotations and the root's x negated: the
  skeleton is symmetric about x). The forward walks and idles are cut as
  before, byte for byte.
- **Feet on the ground:** the new loops are lifted as the walks are
  (`"ground": "walk"` in `clips.json`).

## TSRE
- **`Crowd::Behaviour::Controlled`:** its owner places it and gives its
  clips each frame (`Crowd::control`: clips, times, weights); always its
  own pose at the level its coverage gives, never an impostor, not counted
  in the near cap.
- **Read back:** `Crowd::nodeNamed` (a joint by name, `head`) and
  `Crowd::nodeMatrixOf` (as drawn: place, turn, size), on
  `SkeletalAnimation::nodeIndex` and `GltfShape::poseNodeMatrix`.
- **A clip's motion:** `Crowd::clipMotion`: its ground speed, its way in
  the person's frame (x to its left, z ahead), its length and where its
  stride begins (`SkeletalAnimation::strideStart`: the first foot furthest
  along its way, the foot set down ahead).
- **Ground speeds measured while a foot is down**
  (`SkeletalAnimation::groundMotion`): only the samples whose lowest foot
  is within 3 cm of the clip's lowest, 60 a second (30 before). The median
  over every sample took a run's steps in the air: the UAL sprint measured
  3.68 m/s, slower than the jog. Now, the man's: walk 1.13 (as before, as
  humans-gen measures it), run 1.57, jog 6.07, sprint 9.50, back 0.74,
  left 0.59, right 0.58 m/s. The UAL jog and sprint sweep their feet that
  fast; played slower they would slide.
- **`Humans::Locomotion`** (`src/tsre/humans/Locomotion.h`), what the body
  does as clips:
  - moving by its velocity in its own frame: the idle below 0.05 m/s, faded
    into the moving clips by 0.3 m/s; by way, the two clips on either side
    of it by angle (ahead, back, left, right); ahead, by speed, the two
    clips on either side of it (walk, run, jog, sprint by their speeds) so
    their speeds give it; all in step (one stride's share, each clip at that
    share of its stride from its start), the share going on at the speed
    asked for over the blend's (half to twice);
  - crouching: its crouching idle and walk, faded in and out over 0.2 s;
  - actions: clips in turn over the moving (sit down then sitting; the
    jump's start then its loop), the last looped or back to moving when it
    ends; faded in and out over 0.2 s.
- **The viewer's crowd scene:** its first person a controlled person
  (`CrowdOptions::controlled`; captures: `"crowd": {"controlled": {...}}`),
  its blend logged.

## Checks
- `humans-crowd` 66 (+11): Locomotion on made-up clips (standing, ahead
  at a clip's speed and between two, in step, ahead-left, back,
  back-right, starting off, crouching faded, an action faded in and out
  through its steps, one played once); the man's control clips found,
  named, going their ways; where a stride begins (the first foot furthest
  along the way, ahead or back the same foot ahead, the other half a stride
  on; side steps do not cross); a controlled man's head read back, 1.63 m
  over his feet standing, 0.78 m crouching; its own pose 400 m off with
  no own poses allowed; every walk's speed printed (the passengers' walks
  measure as humans-gen measures them, within 0.02 m/s, with the new
  measure); the old check of walks going one way skips the file's named
  clips.
- `gltf-skin` 86: its humans' clip counts follow the files (33 at least;
  the shared file binds every clip it has, 48). Both checks had failed
  since the stairs clips (35), before this task.
- Every content-free app suite (125, without benchmarks, corpora and
  captures) and ctest 11/11 pass; the route suites need content:
  simulation-route 9, simulation-boarding, route-load on EUROPE1 pass;
  server 168 and route-switch 16 with the template route in `assets`.
- **Viewer captures** (`tests/renderer/characters-controlled-views.json`,
  `shape-viewer-capture` under Xvfb, TSRE_HUMANS_SHAPES humans-gen's
  `out/route/SHAPES`): the man from his right side standing, walking at
  1.4 m/s (walk 0.39, run 0.61), at 3.5 (run 0.57, jog 0.43), 6 (the jog),
  9 (jog 0.14, sprint 0.86), backwards, left, ahead-left (walk and left
  step by halves, rate 1.33), crouch-walking (rate 0.71), sitting (2.5 s:
  sitting), jumping (its loop). Images and a sheet in `~/characters-01`.

## Left
- Moving it in the world, its camera and keys: task 02.
