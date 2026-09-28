# Continuous Ruler Placement

## Objective

Give Rulers the same direct, continuous mouse workflow as Flex Track and Flex
Road without coupling Ruler geometry to the Flex solver or TDB/RDB placement.

## Implemented interaction

- Object tools expose `FLEX TRACK`, `FLEX ROAD`, and `RULER` as one tool row.
- The profile selector is always visible below that row and is filtered to
  TRACK, ROAD, or STATIC/RULER templates for the active tool.
- Ruler mode adds an editable node-shape selector below the profile. It lists
  route-local ordinary shapes from the REF catalog; an author may also type a
  filename that is not currently present in the catalog.
- The first Ruler click creates one object with a fixed start point and a
  provisional endpoint that follows the mouse.
- Each following click commits that endpoint and begins the next segment on
  the same Ruler object.
- Escape discards only the provisional endpoint, finishes the current line,
  and leaves Ruler placement armed for the next line.
- Right-drag rotates the camera. The mouse wheel changes the placement height;
  Ctrl uses the smaller height step.
- Existing generic Ruler placement and point editing remain available.

## Optional node shapes

A Ruler may store one ordinary route shape in its existing `FileName` field.
The shared shape asset is rendered once at every explicitly authored Ruler
node. The first and last shapes follow their adjacent span; an internal shape
uses the bisector of the incoming and outgoing span directions. The same
ordinary static-shape coordinate conversion, animation state, materials, and
LOD data remain in effect.

This option is additive. It does not disable `Template3D` geometry using
`Placement ( Nodes ... )`, so an author may use either method or both. Its
main purpose is to let a complex existing MSTS shape, such as an overhead-line
pole, remain shared by the normal shape library instead of being copied into a
procedural profile mesh.

## Geometry and coordinates

Continuous placement uses explicit append, update-last, duplicate-last, and
remove-last operations on `RulerObj`. The placement tool does not use the Flex
curve solver and does not modify either track database.

Ruler points remain relative to the Ruler owner's MSTS tile. Conversion uses
the integer tile delta and local position separately, with double precision
for the intermediate calculation. A complete world position is never reduced
to one float.

Preview geometry is invalidated at no more than 20 Hz. The current complete
Ruler procedural shape is regenerated lazily by the renderer.

## Undo model

The provisional endpoint is live editor state and is removed explicitly when
the line is finished or cancelled. `Undo::StateCancel()` only discards the
pending snapshot; it does not restore mutated object data.

- The first accepted segment completes the Ruler placement undo action.
- Before each later provisional segment, the committed Ruler is snapshotted.
- Undo therefore removes one later segment at a time.
- After the first segment is reached, the next undo removes the whole newly
  placed Ruler.
- Changing the profile during placement updates the active Ruler. If no new
  point is accepted afterward, that profile-only change receives its own undo
  action when the line is finished.
- Changing the optional node shape follows the same live update and undo rule.

## Acceptance checks

- Place plain and profile-driven multi-point Rulers.
- Select, change, clear, save, and reload an ordinary node shape; confirm it is
  drawn only at authored nodes and coexists with profile-defined node geometry.
- Cross at least one world-tile boundary.
- Change height with wheel and Ctrl+wheel.
- Rotate the camera with right-drag during preview.
- Press Escape before and after accepting the first segment.
- Switch between all three continuous tool buttons during preview.
- Undo several points and finally the complete Ruler.
- Confirm legacy Ruler placement and point editing still work.
