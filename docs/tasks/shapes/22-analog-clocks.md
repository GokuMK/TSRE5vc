# Task 22 - Open Rails' analog clocks

Status: built on `feature/analog-clocks` (worktree `../TSRE5vc-traffic`)
and merged into main, 2026-11-26. The user's request the same day: "Open Rails animated analog
clocks ... check how orts animates them in code", then "Ok, write task,
built on a branch from main ... check, test, if all good, merge back to
main".

## Goal
Station clocks made for Open Rails show the time of day in TSRE as they do
in Open Rails: the shape's hands turned by the game's time. PRUDNIK
(OR_PSK) places 139 of them.

## How Open Rails does it (its code, not its manual)
Read in `openrails-linux-spike` (4d4af890d); paths under `Source/`.

- **Which objects.** A world `Static` or `CollideObject` (both load as
  `StaticObj`, `Orts.Formats.Msts/WorldFile.cs:115-117`) whose shape's first
  animation has an anim_node named `^orts_[hmsc]hand_clock` (any case, any
  suffix) becomes an `AnalogClockShape` (`RunActivity/Viewer3D/Scenery.cs:489-510`).
  The world's Animate flag does not matter. No `.sd` or world token.
  Signals, crossings, pickups, gantries, trains: never.
- **The list file is dead.** `openrails\clocks.dat` / `animated.clocks-or`
  (the manual's and the forums' way) is still read
  (`Simulator.cs:356-361`) but never used: since 9e18ed1e1 (2021-05) a
  clock is known by its hands' names.
- **Hands** (`Shapes.cs:456-583`): `orts_hhand_clock` hours,
  `orts_mhand_clock` minutes, `orts_shand_clock` seconds (a step a second),
  `orts_chand_clock` seconds sweeping (hundredths). A shape may have two
  sets (`...01` and `...02`, two faces).
- **Posing.** No axis is computed: each hand has five keys a quarter turn
  apart (12, 3, 6, 9, 12 o'clock). The time picks key `q` and key `q+1`
  by index (their frames do not matter) and how far between them `a`:
  - seconds: `q = s/15`, `a = (s - 15q)/15` (sweeping: `+ cs/1500`);
  - minutes: `q = m/15`, `a = (m - 15q)/15`;
  - hours: `h %= 12`, `q = h/3`, `a = (h - 3q)/3 + (m/60)/3`.

  `h m s cs` come from the game time in whole hundredths of a second. When
  key `q+1` is missing, `q` becomes 0. Rotation keys are slerped (the
  translation kept), linear position keys lerped. A hand with one key throws
  (unguarded); an animation of 0 frames leaves the clock at rest.
- **The rest of the shape.** Parts that are no hands stay at their rest
  matrix. Every distance level moves (matrices are the shape's).
- **Time.** `Simulator.ClockTime`: stops while paused, follows the game's
  speed; set by the activity, explore or timetable start.

## Content (PRUDNIK, OR_PSK and OR_PSK_NEW)
- 19 of 5,842 route shapes have hands; 16 of them are placed, 139 times
  (all `Static`, 56 tiles; `zegarperon_2.s` 82 times). One placement has
  the Animate flag.
- Every hand: 5 rotation keys at frames 0-4 a quarter turn apart
  (`slerp_rot` or `tcb_key`), some also 5 equal position keys; animation
  4 frames at 30 fps. Matrix names equal the anim_node names in all 19.
- At rest most hands show 12:00 (`zegarperon_2.s`); 36 of the 102 keyed
  parts rest away from their first key (`Uhr01_OR_Jm.s`'s turned faces,
  the DG displays' second face).

## TSRE before
No analog clocks (only digital `ShapeDisplays` with the clock input): the
hands stood at rest (12:00 on most); the one with the Animate flag looped its 4 frames at
30 fps, its hands spinning.

## Design
- `src/tsre/shape/ShapeClock`: the hands of a shape by its part (matrix)
  names (`handOf`, `hands`), Open Rails' quarter rule (`quarter`), and the
  frame of a part's keys that shows a time (`frame`: key `q`'s frame on to
  key `q+1`'s, rotation keys else position keys).
- `ComplexShape::partChannel(part)`: a part's keys in the first animation
  (both MSTS backends; null for others).
- `StaticObj`: a `Static` or `CollideObject` whose loaded shape has hands
  is a clock (looked at once, again when the shape loads again). Its loop
  is stopped (again if its Animate flag is set later), so the other parts
  stay at rest. Its hands are posed where it is drawn (`pushRenderItems`),
  from `WorldClock::hours()`: the simulation's time while one exists, else
  the world's clock. Posed where drawn, not in `updateSim`, because the
  editor's paused world and captures do not update objects.
- The Shape Viewer (the user, 2026-11-26: "start from 00:00 time or so"):
  a clock shape's hands show the viewer's clock, 00:00 when the shape is
  shown, running in real time while Play is on; nothing else moves, Last
  Frame does not apply. Captures set it (`"clockTime": "HH:mm:ss"`,
  `ShapeViewerGLWidget::setClockSeconds`).

## Where it differs from Open Rails
1. Hands are found by part (matrix) names, not anim_node names; the two
   agree in all PRUDNIK clocks (exporters write the same name).
2. The frame between keys `q` and `q+1` is posed with TSRE's evaluator
   (slerp by frame): the same as Open Rails' index slerp while the keys'
   frames increase. A hand's rotation and position keys use the rotation
   keys' frames.
3. A hand with one key stays at its key (Open Rails throws).
4. glTF clocks do not move (`GltfShape` has no part posing); named parts in
   a glTF are ignored for now.

## Checks
- Suite `shape-animation` (text clock shape, both backends): names; the
  quarter rule at 10:30:50.25, before midnight and the next day; hands
  found (an hour hand, a lower-case minute hand with a suffix and
  position keys, a 4-key second hand, a 1-key sweeping hand), a shape
  without animation no clock; each hand's frame (the 4-key hand's last
  quarter on key 0); posed matrices at 03:00 and 10:30 alike in both
  backends.
- Capture `tests/renderer/analog-clocks-viewer.json`
  (`shape-viewer-capture`, `--game-root ~/ORTS/OR_PSK`): the viewer at
  00:00 and at set times.
- Capture `tests/renderer/analog-clocks-prudnik.json`: PRUDNIK's
  `zegarperon_2.s` (UiD 10420) and a DYSTEN clock with a second hand at
  set times.

## Results (2026-11-26, Release)
- `shape-animation` 15 (7 new). With `TSRE_ANIMATION_SHAPES` at PRUDNIK's
  19 clock shapes: 102 keyed parts, 408 / 408 poses alike in both MSTS
  backends; the suite's "frame 0 is the rest pose (95 %)" rule, made for
  stock trains, does not hold for them (66 of 102), as written above.
- Capture `analog-clocks-prudnik.json` (QRhi Vulkan, Xvfb): `zegarperon_2.s`
  reads 10:08 on both faces and 15:45 (the hour hand three quarters on to
  4); the DYSTEN clock 10:08:37, its red second hand on 37 (views a and b
  face it, c and d are from behind or the side).
- Viewer capture: `zegarperon_2.s` at 00:00 (both hands on 12) and 10:08 on
  both faces, the DYSTEN clock 10:08:37 with its second hand on 37.
- `ctest` 11/11; `car-animation`, `shape-materials`, `shape-lights`,
  `shape-emitters`, `shape-axes`, `shape-complex`, `shape-complex-gl`,
  `token-shape-gl`, `displays`, `token-world`, `cab3d`, `eng-lights`,
  `gltf-lod`, `e3d-shape`, `time-of-day`, `level-crossings`, `road-cars`,
  `simulation-ui`, `settings`, `map-view` pass.

## Later
- glTF clocks (part posing for glTF shapes; the same names on nodes).
