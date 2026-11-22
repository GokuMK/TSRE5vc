# Task 21 - The `.sd` for every shape, and as what is placed

Status: future, notes only. These are the user's plans of 2026-11-22,
noted with task [20](20-sd-axes.md) (one axis space for `.sd` files),
which is built first. Nothing is designed or decided yet beyond what the
user said.

## The user's plans (2026-11-22)
1. **Every complex shape reads an `.sd`**: glTF and all future formats,
   not only `.s` and `.e3d`. The `.sd` becomes a generic `ComplexShape`
   feature: one reader, the same blocks for every format (lights,
   emitters, displays, seats, materials, collision, walk roles, distance
   level extension, light schedule, boxes), its positions in one axis
   space (task 20).
2. **In world tile placement, the `.sd` becomes the anchor.**
   - A W file places an `.sd`, and the `.sd` names its shape.
   - That allows several object definitions on one shape: one station
     board model with different displays, one lamp with different lights.
   - A W file can still name a mesh file directly (`.s`, `.e3d`, glTF).
     Such an object gets a plain `.sd` object holding only the shape's file
     name.
3. These bigger steps are a follow-up task, after task 20.

## Notes for the design (to be decided)
- **Task 03** (`03-complex-shape-metadata-sidecar.md`) planned a JSON
  sidecar for non-MSTS formats. The user's plan 1 replaces it: the `.sd`
  for every format, no other side file (the project's rule on side files).
- **glTF `extras`** (`TSRE_seats`, `TSRE_distanceLevelExtension`,
  `TSRE_lightSchedule`, `TSRE_walk`, light extras) and an `.sd` beside the
  same glTF: which one wins, block by block.
- **`GltfShape` parts**: it has none today, so its `rootPart()` is -1 and
  `.sd` entries land in its own space. Named parts (`ShapeHierarchy`) need
  its nodes as parts.
- **The `.sd`'s `Shape ( name )` header** already names the shape's file.
  Placing an `.sd` makes that name the reference. The questions: a shape in
  another folder (roots, as documents have), and what changes when the
  name and the `.sd`'s own name differ.
- **Compatibility**: MSTS and Open Rails read a W file's `FileName` as a
  shape. W files placing an `.sd` would be TSRE-only, or written beside the
  shape name.
- **Editor**: the shape library listing `.sd` objects, placing one, making
  a new definition from a shape, the properties showing which `.sd` an
  object is.
- **Caches**: a loaded shape shared by every `.sd` on it, each `.sd`'s
  blocks per object (today they are per shape).
