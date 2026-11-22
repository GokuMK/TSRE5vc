# Task 20 - One axis space for `.sd` files

Status: built on `feature/sd-axes` (worktree `../TSRE5vc-sd-axes`),
2026-11-22, not merged. The user approved the proposal and gave the go on
2026-11-22 ("The proposal is ok ... When md noted, implement"). MaSzyna
set 6 goes with the merge.

## Goal
A position in an `.sd` should mean the same place whatever model it sits
beside: an `.s`, an `.e3d`, or a glTF (and any later format, task
[21](21-sd-for-every-shape.md)). The user's case: displays, exhausts and
lights written for an S model stay where they are when the model is
upgraded to glTF and keeps its `.sd`.

## How it works now (main 91d21e97)
All formats agree on y and z (+y up, +z the model's front). They differ
only in the sign of x: whether the model is left- or right-handed.

| Model | Its `.sd` is read in | How it reaches the drawn model |
|---|---|---|
| `.s` | the S file's space, MSTS (left-handed: +x the model's right) | mirrored in x: the root part (the first matrix) for lights and emitters, a `.s` suffix check for seats (`ShapeSeats::drawn`) and collision boxes (`ShapeCollision`) |
| `.e3d` | the e3d's own space (right-handed: +x the model's left) | as written (`E3dShape::rootPart()` is -1) |
| `.glb`, `.gltf` | no `.sd` | lights from `KHR_lights_punctual`, seats from `extras.TSRE_seats`, both in glTF space (right-handed) |

As a result:
- One `.sd` line puts a thing on opposite sides of an S model and of its
  e3d twin. `E3dShape` warns when an `.s` and an `.e3d` of one name share
  an `.sd` (`maszyna.e3d.sd.shared`).
- The conversion is in four places: the S root part, `ShapeSeats::drawn`,
  `ShapeCollision`'s boxes, and e3d snap points using the boxes as written.
- A display square without `ShapeHierarchy` is placed in drawn space
  (identity, `UiShapeDisplays.cpp` `squareMatrix`/`squarePart`), not on
  the root as the guide says and as lights and emitters do. The examples
  all name `"MAIN"`, so nothing shows it today.
- The square's mesh is an MSTS one: its texture runs along -x, so its text
  reads correctly once the S root mirrors it. Unmirrored, on an e3d, it
  would read backwards.
- Content: the only e3d `.sd` files with positions are seats written by
  `extra/car-seats` (75 in `~/ORTS/maszyna`, 150 in MaSzyna set 5). There
  are no e3d lights, emitters, displays or boxes.

## Design (decided 2026-11-22)
1. **`ESD_TSREAxes ( MSTS )` or `ESD_TSREAxes ( glTF )`**, one line in the
   `.sd`'s shape block. Without it: MSTS, for every model format.
   - `MSTS`: as S files are, left-handed, +x the model's right.
   - `glTF`: right-handed, +x the model's left, as glTF is and as TSRE
     draws `.e3d` and glTF models (Blender's glTF export too).
   - The user's third value ("as the shape file") is left out: it is
     today's behaviour and breaks the upgrade case; any file can say MSTS
     or glTF instead.
   - Any other value: read as MSTS, and the blocks it moved say so.
2. **A model's own axes**: `.s` MSTS; `.e3d`, `.glb`, `.gltf` (and later
   formats) glTF.
3. **Converted once, at read**, into the model's own axes where the
   `.sd`'s differ: mirrored in x. Everything that uses the values stays as
   it is.
   - Points (light, emitter, display, seat, door, passage positions;
     aisles' x): x negated.
   - Directions (lights', emitters'): x negated.
   - Display rotations (degrees about x, then y, then z): `(rx, -ry, -rz)`.
   - Seats' headings: negated.
   - Boxes (`ESD_Bounding_Box`, `ESD_Complex_Box`'s box): x ends swapped
     and negated; a complex box's translation x negated, its rotation
     `(rx, -ry, -rz)`.
   - A block on a named part (`ShapeHierarchy`) is mirrored in that part's
     own axes. This is right where the glTF's parts are a straight
     conversion of the S file's, as its root is.
4. **Display squares**: without `ShapeHierarchy` they hang on the root
   part, as documented. The square keeps its MSTS face on every model: on a
   model of glTF axes its x is flipped in its matrix, so the text reads as
   it does on the S model.
5. **`E3dShape`'s shared-`.sd` warning goes**: one `.sd` for an `.s` and
   an `.e3d` now puts things in one place.
6. **Migration** (the user's choice: the proposal's (a)): the seats tool
   writes `ESD_TSREAxes ( glTF )` into every e3d block it writes; its
   numbers stay as its plans show the model.
   - A new `axes` command adds the line to e3d `.sd` files written before
     without it: MaSzyna set 6 is set 5 with that line in each file.
   - The tool reads the line: values in the model's space for finding and
     plans, written back in the file's own.
   - TSRE before this task ignores the line and reads an e3d's `.sd` that
     way already, so set 6 can go on before or after this change. The 75
     files installed now need it once this change is in.

## Steps
1. `ShapeAxes` (`src/tsre/shape/ShapeAxes.h/.cpp`): the names, a model's
   own axes, the mirrors.
2. The readers: `ShapeMetadata::from(root, model axes)` (`SFileComplex`,
   and `E3dShape` through `ShapeMetadata::read`), `SFileLegacy::loadSd`,
   `ShapeSeats::readFor`. Each block is mirrored where the `.sd`'s axes
   are not the model's own.
3. Displays: the root for squares without `ShapeHierarchy`; the square's
   face by the model's axes.
4. `E3dShape`: the shared-`.sd` warning removed (both `.ts` files).
5. `extra/car-seats`: the line written and read, the `axes` command,
   README.
6. Docs: the displays guide, the `.sd` section of
   `docs/features/msts-shape-file-format.md`, task 18's note on e3d `.sd`
   positions, the roadmap.
7. MaSzyna set 6 staged in `~/humans-11` (outside the repo).

## Checks
- A new `shape-axes` suite:
  - The names: MSTS, glTF, any case, unknown, missing.
  - Every block mirrored, and left as written, for `.s` and `.e3d` with
    and without the line, through both readers of S files and through
    `ShapeSeats::readFor`.
  - One MSTS `.sd` beside an `.s` and an `.e3d` of one name puts things in
    one place once drawn: the `.s` through its root part, the `.e3d` as
    written.
  - The display square: on the root without `ShapeHierarchy`, its face the
    same on both models.
- `station-people`: the seat check of an e3d `.sd` follows the new
  default.
- Build all, ctest, the app suites.
- The seats tool: `axes` over MaSzyna set 5, its result compared line by
  line, a second run writing nothing, `plan --sd` the same before and
  after.

## As built (2026-11-22, `feature/sd-axes`)
- `src/tsre/shape/ShapeAxes.h/.cpp`: `fromName`, `ofModel` (by the
  extension alone, no file system: a display square asks every frame),
  `mirrors`, `unknown` (the message), and the mirrors of a point, a
  rotation, a heading and a box.
- Each block has its mirror: `ShapeLights::mirror`, `ShapeEmitters::mirror`
  (position, direction), `ShapeDisplays::mirror` (centre, `(rx, -ry,
  -rz)`), `ShapeSeats::mirror` (which `drawn()` now uses for S files).
- The readers:
  - `ShapeMetadata::from(root, model axes)` and `ShapeMetadata::mirror()`
    (`SFileComplex` passes MSTS; `read()` passes `ofModel(shapePath)`, so
    `E3dShape`, `ShapeCollision` and the walk areas get the model's axes).
  - `SFileLegacy::loadSd` reads the line and mirrors its displays, lights,
    emitters and boxes (a complex box's translation and rotation too).
  - `ShapeSeats::readFor`.
  - `ShapeDisplays::read(shapePath)` (tests).
  - An unknown value: read as MSTS, its message in the problems of each
    block family it moved (lights, emitters, displays, seats), reported as
    those are.
- `Ui::ShapeDisplays::squareMatrix`: a square without a part (none named,
  or one the shape has not, as the message always said) hangs on
  `rootPart()`; its x scale negated on a model of glTF axes.
- `E3dShape::reportMetadata`: the shared-`.sd` warning removed
  (`maszyna.e3d.sd.shared` replaced by `shapeaxes.unknown` in both `.ts`
  files).
- `extra/car-seats`: `own_axes`, `text_axes`, `mirrored`; `sd_seats` reads
  in the model's space; `sd_text` writes `ESD_TSREAxes ( glTF )` into an
  `.e3d`'s `.sd` holding no other places (else its block in the `.sd`'s
  axes); `stairs` and `aisles` write back in the `.sd`'s axes; the `axes`
  command.
- Docs: the `.sd` section of `docs/features/msts-shape-file-format.md`;
  the displays guide (English and Polish, Markdown and HTML), the lights
  and emitters guides (Markdown and HTML); task 18's space note; the
  car-seats README.
- MaSzyna set 6 (`~/humans-11/maszyna-sd-seats-map-20261122-6.zip`,
  README6-maszyna.md): set 5's 150 files with the line, and the 75 EN57
  interiors (`dynamic/pkp/en57_v2/lowpoly`) task humans 09 wrote straight
  into `~/ORTS/maszyna` (in no set before) with it too: 225 files, each
  one line longer; the backup holds the 75. No other content root has an
  `.sd` beside an `.e3d` or glTF (`~/ORTS` surveyed).

## Results (2026-11-22)
- `shape-axes` 19: names and model axes; every block as written or
  mirrored for `.s` and `.e3d`, with and without the line and with an
  unknown value (its message in each family's problems); seats through
  `readFor` and `drawn` in one place for both models; both S backends
  mirror a glTF `.sd` alike (the legacy box too); one MSTS light in one
  place through the S root and beside the `.e3d`; a square without
  `ShapeHierarchy` on the root; the `.s`'s and the `.e3d`'s square
  matrices equal, face included.
- `station-people` 43 (45 with `TSRE_MASZYNA_ROOT` and `TSRE_OR_PSK_ROOT`):
  the e3d seat check now reads the `.sd` for the `.e3d` and finds the `.s`'s
  places.
- Unchanged: shape-lights 53, shape-emitters 15, displays 37, e3d-file 71,
  e3d-shape 81, shape-animation 8, character-body 29.
- Build all, ctest 11/11, 117 of 128 app suites (the 11 others need
  content, the same as on main), simulation-route 9/9,
  simulation-boarding 1/1 and route-load on EUROPE1.
- The seats tool:
  - `axes` over set 5 wrote 150 files, and a second run wrote none.
  - Line by line, set 6 is set 5 plus the one line before each seats
    block (UTF-16, CRLF).
  - Read back in model space, set 6 gives set 5's values as written
    (150 of 150).
  - A fresh `write` of the MaSzyna `bhp-a` gives set 6's lines and the
    line. One of the S Bhp 18965 gives set 7's lines and no line.

## Later
Task [21](21-sd-for-every-shape.md): an `.sd` for glTF and every complex
shape, and the `.sd` as what a W file places.
