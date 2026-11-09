# MaSzyna Task Roadmap

MaSzyna's (EU07's) rolling stock in TSRE: its models drawn as one more shape
kind, its vehicles listed and placed like any other, and later driven by its
physics.

Status 2026-11-09: tasks 01 and 02 built on `feature/maszyna`, not merged.

## Goal of the first task set (the user, 2026-11-09)
- Copy a vehicle folder such as `dynamic/pkp/et22_v3/` and run it in TSRE,
  loaded as the `Eng` class.
- `.e3d` loaded as another `ComplexShape`.
- File handling is TSRE's own and made for speed. MaSzyna's code is for
  learning how things work; its inefficiencies are not copied.
- At the end of the set: models and rolling stock open in the Shape Viewer
  and the Consist Editor.
- Physics and simulation are the following task.

## Where things are
- Release pack: `~/ORTS/maszyna/`
- Source: `../maszyna` (at 66a940d4 when reviewed)
- MaSzyna-Reloaded (Godot), an example of an engine in front of MaSzyna's
  core: see the [harness review](../driving/reports/driving-harness-review.md).

## Earlier decisions that stand (2026-10-24, `driving/00-task-roadmap.md`)
- Models are drawn directly, not converted (which also keeps converted
  assets out of the licence question).
- MaSzyna's mover in C++ as one kind of vehicle model; the host owns motion,
  couplers and buses. That is the physics task.

## The user's answers (2026-11-09)
- **No generated ENG files.** `.mmd` can be extended too, and a game root
  shared with Open Rails stays free of custom ENG files that would confuse it.
- **One Consist Editor row per livery**, as today. Open Rails' unstable
  versions also allow one ENG per livery sharing shapes, so it is consistent.
- **Shared files by where they are in MaSzyna.** `dynamic/` is `[[Trainset]]`,
  `sounds/` goes in the game root's `SOUND/MASZYNA`, `textures/` in a new
  `TEXTURES/MASZYNA` beside it. `GLOBAL` stays Open Rails' shared folder. The
  MaSzyna folders are not copied whole, since much of them will not be used.
- **Trainset subfolders**, so `dynamic/` content can be dropped in: a folder
  with vehicle files stops the scan, a folder without any has its subfolders
  checked once, no deeper (task 02).
- **`.t3d` skipped**: obsolete and very slow to load, and none in the pack.
- **A MaSzyna `.con` line must not crash** Open Rails or old TSRE: checked in
  the code, task 02.
- **Materials** take the S file + PBR route: task 01.
- **Repaints go through the shape's state**, as texture replacement does
  since scripting task 02: one shape per file, the livery in each car's state,
  set after load with `ComplexShape::setRepaint`, which the ENG and Open Rails'
  custom definitions can reuse. A load option only if a shape has to know its
  repaint while loading; `.e3d` does not (task 01).

## Review
- [What the files hold](reports/rolling-stock-format-review.md): formats,
  hierarchy, animation, what the pack really contains, the fit with
  `ComplexShape`, and what not to copy.

## Tasks
- [x] [01 - MaSzyna models as a shape](01-e3d-models.md): `E3dShape`, one
  shared model per file, materials and liveries, parts and pose, levels.
- [x] [02 - MaSzyna vehicles as `Eng`](02-vehicles-as-eng.md): trainset
  subfolders, finding vehicles, `.fiz` and `.mmd`, consists, viewers, moving
  parts.
- [ ] Physics and simulation (not designed yet).
- [ ] Later: lights and couplers states, cabs and controls, sounds.

## What the review changed
- **A vehicle does have an equivalent of an ENG file** (`.fiz` with `.mmd`),
  so the plan of generated ENG files went.
- **A `.e3d` is already laid out as one buffer**: drawing it needs no
  per-part copying at all, which is where the speed comes from.
- **Motion is computed, not keyed**, so `CarAnimation` needs a second way of
  finding parts and moving them. **Levels of detail are per part**, so the
  shape derives its levels from the parts' distance limits. Neither blocks
  anything in `ComplexShape`.
- **A moved folder is not self-contained** (at least 198 of 229 need
  `textures/`), hence the shared folders in the game root.

## Questions
None open for this task set. `data/` (load weights, sound overrides, smoke
definitions) waits for the physics and sound tasks.
