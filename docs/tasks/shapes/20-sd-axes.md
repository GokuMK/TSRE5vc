# Task 20 - One axis space for `.sd` files

Status: being built on `feature/sd-axes` (worktree `../TSRE5vc-sd-axes`).
The user approved the proposal and gave the go on 2026-11-22 ("The proposal
is ok ... When md noted, implement").

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

## Later
Task [21](21-sd-for-every-shape.md): an `.sd` for glTF and every complex
shape, and the `.sd` as what a W file places.
