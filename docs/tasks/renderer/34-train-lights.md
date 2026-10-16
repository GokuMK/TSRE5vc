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
- Read-only once loaded and shared: the cars of a consist are copies of the
  library's `Eng` (`Eng(Eng *)`) and share the lights (`std::shared_ptr`).
- The Consist Editor loads every ENG and WAG file (`EngLib::loadAll`); today
  it already reads these blocks token by token to skip them. Its load time
  is measured before and after; the lights are read on first use if it
  grows.
- Other work on ENG goes on in parallel, so `Eng` and `Consist` get only
  small hooks: the parser call in `Eng::load`, the shared pointer in the
  copy constructor, one call in `Eng::pushRenderItemOnTrack`, the car's
  place set by `Consist`. Everything else is in new files.

### Which lights are on

- `EngLights::State`: what Open Rails evaluates per car: headlight switch
  (off, dim, bright), first, last, reversed, penalty, player's train, in
  service, day, weather, coupled at the front and at the rear, battery,
  brakes, reverser, doors, horn, bell, multiple unit. `EngLights::isOn`
  evaluates a light's conditions as Open Rails does.
- The simulation fills the state per car (a later task). Until then
  `Consist` gives each car its place (first and last car of the consist,
  reversed when flipped, coupled to its neighbours) and the rest is a test
  state (see the questions).
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
- Strength matched to Open Rails' headlight at half the reach (it lights a
  surface to alpha x colour x (1 - d / reach)): I = pi alpha D^2 / 8 in task
  21's units, D the reach; tuned by eye at night.
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
   (`FadeIn`, `FadeOut`), if wanted in this task (see the questions).

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

## Questions (open)

1. The test state: a train in service with the headlights at Bright
   (player's train, no penalty, battery on, brakes released, reverser
   forward, doors closed, horn and bell off), or every condition but the
   unit ignored? The latter lights the Vectron's off, dim and bright
   variants of the same lamps together, and red and white lenses at the same
   places at the back.
2. Time of day and weather: from the sun and the weather now (the editor
   knows them), or left to the simulation?
3. Flashing and fading (step 4): in this task, or later?

## Later

- Lights attached to animated parts (`ShapeIndex`, `ShapeHierarchy`) and
  own glow textures (`ORTSGraphic`): unused in our content.
- Lamps in the Shape Viewer and the Consist Editor.
- Shadows from headlights.
