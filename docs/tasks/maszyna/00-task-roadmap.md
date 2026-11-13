# MaSzyna Task Roadmap

MaSzyna's (EU07's) rolling stock in TSRE: its models drawn as one more shape
kind, its vehicles listed and placed like any other, and later driven by its
physics.

Status 2026-11-13: tasks 01 to 07 in main (07 merged 8c3d8284, on GitHub).
A batch of small items from the tasks' open lists built on
`feature/maszyna` (2026-11-13, see [below](#small-items-2026-11-13)). Next,
the user's order: the brakes and the train lines (a host task), then control
cars proper.

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
  couplers and buses. Since 2026-11-10 (the user) a separate, later
  simulation task; first TSRE's Lua loco scripts fed from `.fiz` (task 03).

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
- For task 03 (2026-11-10): [what TSRE's loco scripts read](reports/loco-script-reads.md),
  [`.fiz`: format, pack and force models](reports/fiz-physics-format.md),
  [cab controls in `.mmd`](reports/mmd-cab-controls.md).
- For task 04 (2026-11-10): [TSRE's 3D cab chain](reports/tsre-3d-cab-chain.md).
- For task 05 (2026-11-10): [MaSzyna sounds in `.mmd`](reports/mmd-sounds.md),
  their semantics and the pack's numbers.

## Tasks
- [x] [01 - MaSzyna models as a shape](01-e3d-models.md): `E3dShape`, one
  shared model per file, materials and liveries, parts and pose, levels.
  Since 2026-11-11 (`feature/cab-shadows-shimmer`, tested live by the user):
  parts taking an alpha livery by slot in the blended pass only, as
  MaSzyna (the ET22's and the EP09's low-poly interiors' copies of the body
  shimmered over it as the train moved), and every blended part keeps only
  what is below its `.mat` opacity. Open: three whole-pack measurements for
  a remote agent (the pack is not on the user's machine), ET22's duplicated
  gutters left as content.
- [x] [02 - MaSzyna vehicles as `Eng`](02-vehicles-as-eng.md): trainset
  subfolders, finding vehicles, `.fiz` and `.mmd`, consists, viewers, moving
  parts. Vehicle files (2026-11-12): every random include set member and the
  sounds, so an archive stands alone; open: leaving the shared
  `SOUND/MASZYNA` out by choice (its "Sounds in an archive").
- [ ] [03 - MaSzyna locos on Lua loco scripts](03-mmd-loco-scripts.md):
  modules on config tables, `.fiz` read in C++ and given to scripts,
  `electric-resistor-mmd.lua` (MaSzyna's series motor model),
  `electric-induction-mmd.lua`, `diesel-mmd.lua`, driven and heard. Built
  2026-11-10 but step 7.
- [ ] Control cars proper (EN57's ra and rb, ENG `Type ( Control )`): for now
  engines without force on their unit's script (task 03).
- [ ] [04 - MaSzyna cabs](04-cabs.md): the `.mmd`'s cab read, ridden and
  drawn, posed from the scripts' values, worked with the mouse; a second
  kind of cab description beside the `.cvf`. Design 2026-11-10; steps 1
  to 5 built 2026-11-10 (ET22), merged into main.
- [x] [05 - MaSzyna sounds](05-sounds.md): vehicle, cab and control sounds
  read from the `.mmd`, Ogg Vorbis, MaSzyna's own sound class, driven by
  the host and `maszyna.sounds`. In main (3c1655e7); its open list in the
  document.
- [ ] Later: couplers' states (the lights' are task 07).
- [ ] [06 - MaSzyna engines](06-engines.md): the force where the Lua path
  is wrong (mechanical and hydraulic diesels, induction controllers, line
  voltage), the breaker, MaSzyna's procedures; each module C++ or Lua by
  its measured cost. Built 2026-11-11 (uncommitted): steps 1-4, vigilance,
  the pressure switch, the door interlock, speed control, ET42's dynamic
  brake; EP09's and EW58's automatic ED and ED from the brake handles wait
  for the brake task. In main (3c1655e7).
- [x] [07 - MaSzyna lamps and exhaust](07-lamps-and-exhaust.md): the
  model's lamps with a beam each through TSRE's car lights, the train's
  rules and the cab's switches; smoke presets fitted to MaSzyna's
  templates; the line voltage rule. In main (8c3d8284).
- [ ] Brakes and the train lines (the user, 2026-11-13: next; a host task,
  MaSzyna's brakes as inspiration): ED and air blending (EP09, EW58), ED
  from the brake handles, `BrakeIntervention`, `Parking`, brake force by
  speed, the valves' fill rates, EP and twin pipe; the train lines carrying
  what now stands in for them (the low voltage, the compressor's and
  heating's supplies, `BatteryStart`, electric train heating of TSRE's
  cars). First a comparison against MaSzyna (the same consist accelerating
  and braking in both), to be designed.
- [ ] Control cars proper (after the brakes).
- [x] ~~Later: MaSzyna's C++ mover as a vehicle model~~: retired
  2026-11-11 (the user), after [the review](reports/mover-core-or-modules.md):
  modules for the engines; MaSzyna's brakes inspire a later host task.

## Small items (2026-11-13)

The user's batch from the tasks' open lists, built on `feature/maszyna`:
- High beams through MaSzyna's light dimmer (the Vectron's), task 07.
- The converter, the compressor's supply, the train heating, the battery's
  voltage, the resistors' fans' switch, the ammeters' branches: task 06's
  progress; the cab's labels: task 04.
- Sounds: the brake cylinder's steps, the emergency valve, rain in the
  cab, the high horn (Shift+Space) and the whistle (cab buttons), the
  direction relays, the cab's ambience; the host's brake sounds' bar/MPa
  fix: task 05.
- ET22's interlock (the main controller held while shunting): task 03.
- The scene's weather set each frame from the run's air
  (`EngLights::setWeather`; it was never set outside tests: AI wipers saw
  no rain).
- The TRK save: `Mountains` was written from `Electrified`,
  `MaxLineVoltage` never (kept as a number with its fraction).
- Docs: task 03's stale items (the line voltage, RVent).
- Found: the pack's `.fiz` and `.mmd` files are ISO-8859, which a UTF-8
  `grep` (ugrep) skips as binary without a word: count with `-a`.

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
None open (task 05's answered 2026-11-10, in [its
document](05-sounds.md#decisions-so-far)). `data/` (load weights, sound overrides, smoke
definitions) waits for the physics and sound tasks.
