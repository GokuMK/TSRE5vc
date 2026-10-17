# Task 34 - Train Lights

## Objective

Draw the lights of rolling stock from the `Lights` block of ENG and WAG
files, as MSTS and Open Rails define them: lamps glowing on the cars and
headlight beams lighting the track and the mist. On the QRhi renderer with
local lights on the lamps are emissive and glow through the bloom (task 24)
and the beams are spot lights of the light grid (task 21); elsewhere the
lamps are plain discs. Which lights are on follows the train's state, which
the simulation will supply; until then a test state turns on the lights of
a train in service, front lights at its front and tail lights at its end.

## Content (C:/trainsim, 2026-10-17)

- 959 files with lights (82 ENG, 304 WAG, 573 includes); 17 lights per file
  in the middle, 47 files with more than 100, up to 2075: LED lamps built of
  5 cm glow dots.
- Test train: CMK, activity `file0.act`, a static consist of one
  `PKOR_Vectron/ID_6193-490_PL.eng`, whose lights come from
  `common.inc/PKOR/lok_elektryczne/Vectron/Lights_1.inc`: 1101 lights,
  3 cones (shunting, dim, bright; front, first unit) and 1098 glows of 5 to
  14 cm, white at the front for the first unit and the last one reversed,
  white or red at the back for the last unit and the first one reversed, in
  variants for the headlight switch off, dim, bright and dim or bright. Back
  lights face backwards (azimuth 180). At Bright a single Vectron shows
  about 65 white glows and a beam at the front, 92 red glows at the back.
- Not used anywhere: `ShapeIndex`, `ShapeHierarchy`, `ORTSGraphic`.
- Lights with several states (flashing): 1.5 % (2 to 28 states).
- Conditions in use: Headlight and Unit (all values), Control (10,000 lights
  for AI trains only, 14,000 for the player's only), Service, Penalty,
  TimeOfDay (3000 night-only), Coupling (a few); Weather and Horn only as
  "ignore".

## Format

As Open Rails reads it (`Orts.Formats.Msts/LightCollection.cs`):

- `Lights ( count ORTSGraphic? Light* )`; the count is ignored (often
  wrong).
- `Light`: `Type` (0 glow, 1 cone), `Conditions` (repeatable), `Cycle`,
  `FadeIn`, `FadeOut`, `States ( n State* )`, `ShapeIndex`,
  `ShapeHierarchy`, `ORTSGraphic`.
- `State`: `Duration`, `LightColour` (ARGB hex), `Position` (x y z),
  `Radius`, `Azimuth` and `Elevation` (three values each), `Transition`,
  `Angle`.
- Condition values: Headlight 0 ignore, 1 off, 2 dim, 3 bright, 4 dim or
  bright, 5 off or bright, 6 off or dim; Unit 0 ignore, 1 middle, 2 first,
  3 last, 4 last reversed, 5 first reversed; Penalty and Service 1 no,
  2 yes; Control 1 AI, 2 player; TimeOfDay 1 day, 2 night; Weather 1 clear,
  2 rain, 3 snow; Coupling 1 front, 2 rear, 3 both; Open Rails' `ORTSBattery`,
  `Brake`, `Reverser`, `Doors`, `Horn`, `Bell`, `MU`.

Open Rails rules kept:

- A light is on when any of its `Conditions` blocks holds (all its entries
  other than "ignore"); a light without conditions is always on.
- Every cone gets a reversed copy (x and z mirrored, azimuth turned by 180,
  first and last unit turned into their reversed variants), as MSTS made
  them.
- The direction is the middle value of `Azimuth` (about the vertical) and
  of `Elevation`, from the car's forward direction.
- A glow is a flat square `Radius` wide facing that direction (not turned
  to the camera).
- A cone's half-angle is `Angle` / 2 and its reach
  (`Radius` / 2) / sin(`Angle` / 2): the Vectron's are 58, 88 and 135 m.
- Positions are in the car shape's space through its matrix 0 (or the
  `ShapeIndex` / `ShapeHierarchy` matrix).

## Design

### Parser and data

- `EngLights` (`src/tsre/trains/EngLights.{h,cpp}`): reads the block with
  `ParserX` from the file buffer at the `Lights` token of the `Wagon`
  section in `Eng::load`. A later `Lights` block replaces an earlier one.
- Everything Open Rails reads is kept, also what the first version does not
  use, so later steps need no parser work.
- Kept as text, read on first use: `Eng::load` keeps the block as the file
  has it (`ParserX::KeepBlock`, `EngLightsBlock`); the first draw reads it
  (`Eng::getLights`). A car's copies in consists share the block and what
  is read from it (user, 2026-10-17: no second reading of the ENG file,
  which an earlier version of this step did).
- Why (measured 2026-10-17, C:/trainsim, 3597 cars, 1397 with lights): the
  Consist Editor loads every ENG and WAG file (`EngLib::loadAll`) and draws
  none; reading the lights there made its load 37 % longer (about 1.4 s),
  as the variants of a locomotive each read the same large include (the
  Vectron's 24,000 lines). Kept as text, its load stays about 5 s (runs
  scattered from 3.6 to 6.1 s either way). Kept compressed (`qCompress`,
  level 1; user, 2026-10-17): 3 MB instead of 116 MB of text there. In
  time it is about the same as reading every block at load (both about
  1 to 1.5 s over skipping them, within the runs' scatter); it saves the
  read lights of every car (estimated 30 MB) and their reading where no
  car is drawn.
- A workaround, not the real solution (user, 2026-10-17). Reading every kept block would take
  1.4 s; the route editor reads only those of the cars it draws.
- For the ENG parser's rework (moving ENG to `SimisTextReader`, user,
  2026-10-17): read each include file once and share it, which also
  removes the duplicated light blocks; no workaround here meanwhile.
- Other work on ENG goes on in parallel. Code goes where it belongs (in
  `Eng` and `Consist` where it fits them), with merges kept in mind: no
  refactoring of their code on the way.

### Which lights are on

- `EngLights::State`: what Open Rails evaluates per car: headlight switch
  (off, dim, bright), first, last, reversed, penalty, player's train, in
  service, day, weather, coupled at the front and at the rear, battery,
  brakes, reverser, doors, horn, bell, multiple unit. `EngLights::isOn`
  evaluates a light's conditions as Open Rails does.
- The simulation fills the state per car (a later task). Until then
  `Consist` gives each car its place (first and last car of the consist,
  reversed when flipped, coupled to its neighbours) and the rest is a test
  state: a train in service, headlights at Bright, the player's, no
  penalty, battery on, brakes released, reverser forward, doors closed,
  horn and bell off (user, 2026-10-17).
- Day and weather follow the environment, as in Open Rails (the sun above
  the horizon; the weather's rain or snow), not switched by hand.
- The lit set is rebuilt only when the state changes.

### Drawing lamps (glows)

- A lens per light: a disc `Radius` wide at its position, facing its
  direction, double-sided (as signal lights), inside the car's transform,
  so lamps follow the car through curves and grades. Built once the shape
  has loaded, as signal lights.
- QRhi with local lights on: emissive, `LightColour` in linear light times
  its alpha times an emission constant (signals: 4); glow at the signal
  daylight scale (task 26: 50 % by day, full at night), as train lamps must
  show by day as signals do.
- Merged: the steady lit lenses of a car in one colour are one mesh, built
  when the lit set changes: a Vectron at Bright draws 2 packets instead of
  about 160. Lights with several states are drawn one by one with the
  shared disc.
- Glow at a distance: glow-only emitters (as signals: a glow splat, no
  light) per cluster of lenses, binned on a grid of about 0.25 m as
  emissive surfaces are (`EmissiveEmitters`), not one per LED dot.
- OpenGL, or QRhi with local lights off: plain discs in the light's colour,
  merged the same way.
- Lamps cast no shadows; clicking one selects its car.

### Headlight beams (cones)

- Not drawn as geometry (Open Rails draws cones only for debugging): a spot
  light of the light grid at the cone's position and direction, outer
  half-angle `Angle` / 2, inner about 0.7 of it, reaching the cone's reach
  (the car asks for that reach with `setLightReach`, at most 250 m).
- Strength: TSRE's own, not Open Rails' (user, 2026-10-17: our lights are
  adjusted for our engine): at its reach a beam lights a surface facing it
  to a faint 0.01 (task 21's units, the sun's light 0.7), I = 0.01 pi D^2,
  D the reach; shorter beams (dim, shunting) are weaker. Tuned by eye.
- A cone placed beyond the car's end (half its ENG `Size` length) shines
  from that end, moved back along the beam: content places cones ahead of
  the car for Open Rails' headlight (the CMK Vectron's 1.5 m beyond its
  end, 2.3 m ahead of its lamps), and a beam starting in the air looked
  wrong in mist (user, 2026-10-17). No ENG change needed.
- Every train's beams light the scene, not only the player's lead
  locomotive's as in Open Rails: terrain, MSTS shapes, PBR, and the fog
  volume, where lamps already light the mist (task 33), so beams show in
  mist without more work.
- Lamps' daylight rule (task 21: 3 % by day, full at night, full where the
  sky enclosure hides the sky, as in tunnels).
- No shadows (local lights cast none): beams pass through objects.

### Cost

- Per car and frame: a few submissions and its emitters, gathered with the
  others.
- The light grid is kept only while no light moves (task 21): while a train
  moves it is rebuilt every frame, about 1 microsecond per light. Enough for
  the editor's test drive; a grid of its own for moving lights is a later
  step if routes with many lamps need it.

## Steps

1. Parser and state: `EngLights`, the test state, unit tests.
2. Lamps: lenses, emitters, both material paths; captures.
3. Beams: spot lights, reach and strength; night captures, mist.
4. Flashing states (`Duration`, `Cycle`, `Transition`) and fading on and off
   (`FadeIn`, `FadeOut`), the last step (user, 2026-10-17).

## Verification

- Unit tests: the Vectron's 1101 lights and 3 reversed cones, colours,
  positions, conditions; `isOn` against Open Rails' rules; at Bright a
  single locomotive shows white at its front and red at its end, and the
  other way round when flipped.
- Captures, CMK activity `file0.act`, views of both ends (below), noon and
  night, also in mist: lamps at the shape's lenses, left and right as on the
  shape, red at the end, the beam on the track at night.
  - `core.startup.camera=-5351,14855,-476.84,247.58,105.35,141.93,-12.14`
  - `core.startup.camera=-5351,14855,-425.03,247.58,122.10,246.50,-12.14`
- Consist Editor load time before and after.
- OpenGL renderer: plain discs.

## Decisions (user, 2026-10-17)

| Topic | Decision |
|---|---|
| Test state | A train in service with the headlights at Bright, not every condition but the unit ignored (that would light the Vectron's off, dim and bright variants of the same lamps together, and red and white lenses at the same places at the back). |
| Day and weather | From the environment, automatic as in Open Rails. |
| Flashing and fading | The last step of this task. |
| OpenGL | Plain discs in the light's colour, as signal lights. |
| Code placement | Where it fits (also in `Eng` and `Consist`), no file splits made only for easier merges; merges with the parallel ENG work kept in mind. |

## Implementation status (2026-10-17)

| Step | Commit | Notes |
|---|---|---|
| Merge | `746de49` | `feature/simulation` merged first, so the lights build on its `Eng` and `Consist`. |
| 1. Parser and state | `053234b`, this change | `EngLights` (reader, conditions, test state); kept as text, read on first use (`28af5fe` read the file again, replaced); suite `eng-lights`. |
| 2. Lamps | `a08ec0b` | `CarLights`; `Consist::lightsTrain` (place in the consist); day and weather set by the editor each frame. |
| 3. Beams | this commit | `CarLights::beams`: spot lights of the lit cones on a packet without a mesh, reaching the cones' reach. |

- Lamps are placed through the shape's root, which both shape loaders draw
  as an x mirror (matrix 0's own transform left out): a light at (x, y, z)
  sits at (-x, y, z) in the shape's drawing space, 1 cm out along its
  direction.
- Merged per colour: the Vectron at Bright draws 2 packets (65 white
  lenses, 92 red ones), 8 segments per LED dot.
- Glow splats: one per 0.25 m cell of lenses of a colour, as bright as the
  lenses' area, overlapping dots counted once (no more than the disc around
  them).
- Captures (CMK, `file0.act`, noon and 22:00, the two views and closer
  ones): at night the three white lamps of the head signal at the front
  and the red tail lamps at the back glow; by noon they show lit in their
  housings; OpenGL draws the lamps' shapes in plain colour.
- Beams: a spot light per lit cone at the cone's place (x mirrored), from
  the car's end at the latest, full within 0.7 of its half-angle and none
  beyond 1.5 of it (half at the cone's edge, as Open Rails' headlight);
  strength 0.01 pi D^2 (faint at its reach D), reach D (the car asks for
  it, at most 250 m). They are on a packet without a
  mesh, submitted with the lamps: gathered with the lights, never drawn,
  so a locomotive out of view still lights the track ahead. QRhi with
  local lights only; the lamps' daylight rule (3 % by day).
- Captures (CMK, 22:00 and 1:00 in ground mist): the Vectron's long beam
  (135 m) lights the track from about 10 m ahead of the cab (the cone,
  3.5 m up, reaches the ground there), ballast and sleepers visible and
  fading with the distance, and in the mist comes out of the cab's front
  as a faint cone. The first version (Open Rails' strength, from the
  cone's place) saturated the track white and started 2.3 m ahead of the
  lamps in the air.
- To tune (user, later): the beams' strength (BeamLightAtReach) and the
  lamps' size and brightness.

- Fog with moving or turning camera: the beams' (and every lamp's) light
  drags with the camera (the fog volume's history, task 33). Open: three
  fixes there made it worse and were reverted (see task 33, "Lamps' light
  dragged by the camera").
- Lights seen through the body (user, 2026-10-17, not reproduced since):
  no local light has shadows, and the lamps' glow emitters light the fog
  through the signals' fog grid at its 50 times gain, so a lamp behind the
  car can light the fog in front of it. If seen again, the cheap fix: leave
  the lamps out of the fog grid (the beams light the fog) or let them light
  the fog in front of them only. Shadows from local lights are task 21's
  later work.

## Later

- Lights attached to animated parts (`ShapeIndex`, `ShapeHierarchy`) and
  own glow textures (`ORTSGraphic`): unused in our content.
- Lamps in the Shape Viewer and the Consist Editor.
- Shadows from headlights.
