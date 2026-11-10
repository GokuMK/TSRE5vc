# TSRE's interactive 3D cab: the chain, and where MaSzyna's cabs plug in

Review for task maszyna 04 (2026-11-10, feature/maszyna at 10efd763). Line
numbers are for that tree.

## 1. The chain

- **Choosing the cab shape:** `VehicleDefinition` reads `ORTS3DCab` and
  `ORTSAlternate3DCabViewPoints` into `Cab3D` (shapes, viewpoints: head
  position, start direction, rotation limit; VehicleDefinition.h:38-46,
  138-153); `cab3DShapePath` the last file named that exists; the `.cvf`
  found as Open Rails finds it (`CabView::read`). Per definition, shared by
  every car of the ENG. MaSzyna's definitions set none of it today
  (MaszynaVehicle.cpp:540-590); `MaszynaVehicle` only collects `cabModels`.
- **Loading:** `Eng::cab3DShapeOf` (Eng.cpp:761-771) adds the shape to the
  ShapeLib (`DoubleSided`); ShapeLib picks the backend by extension, so an
  `.e3d` cab is an `E3dShape` (which ignores `setLoadOptions`).
- **Drawing:** `Eng::pushRenderItemBetween` (Eng.cpp:1272ff): with a
  `cabViewpoint` it draws the cab shape in the car's placement (the body
  until the cab has loaded), keeps `cabPlacement` and `cabView` for the
  mouse, poses the cab (`poseCab`), `ShapeLod::apply`, pushes the cab and
  the lamp stand-in; the body is not drawn but posed so its lights push.
  Loads skipped. The cab is in the scene and the shadow pass.
- **The camera:** `Consist::rideCab3D` needs `Eng::cab3DViewpoint(rear) >=
  0` and a cab shape (Consist.cpp:873-885); `Eng::cab3DCameraPosition`
  (Eng.cpp:700-739, x mirrored for the MSTS root), `Eng::cab3DMount`
  (744-759: start look, turn limits, a 0.5/0.3/0.5 m move box). Entered by
  Drive (the driven cab vehicle), keys 1/2/3 while following, Play and the
  editor's riding; without a `Cab3D` a generic cab window eye (what MaSzyna
  vehicles get today).
- **Driving vs viewing:** every ridden cab is posed; only the driven cab
  gets values (`SimulationController.cpp:1350`, `Eng::setCabControls`);
  the mouse only on the driven cab of the train that has the keys
  (RouteEditorGLWidget.cpp:3070-3089).

## 2. Controls, matching, posing

- `CabView`: the `.cvf`'s controls (kind, type, style, order, ScaleRange,
  frames, positions, values; Open Rails' tables).
- `CabParts::match` (CabView.cpp:561-599): part names `TYPE:order[:p1[:p2]]
  [-sub]` to controls; only `partCount`, `partName`, `partLastFrame` used.
- `Eng::poseCab` (Eng.cpp:976-1080): an own state per cab; each frame a
  control's value to a key frame (`CabView::frame`) and `setPartFrame`;
  controls without key frames skipped (every control on an `E3dShape`).
  Outside parts by `CarAnimation`.
- Lamps are key-framed parts (0/1); no visibility toggling. Bright overlays
  kept out of picking (`SFileComplex::partBright`). Digits and screens not
  drawn.
- Key frames (`partLastFrame`, `partAtFrame`, `setPartFrame`) exist in the
  S file backends only; `partTriangles`, `partBright`, `loadData` in
  `SFileComplex` only. Generic: `partCount`, `partName`, `partParent`,
  `partRest`, `setPartTransform`, `clearPose`, `partMatrix`,
  `matrixByName`, `enableSubObjByName`, `setClassLight`, primitives and
  highlight.

## 3. Picking and the mouse

- `CabPicker` (CabPicker.h/.cpp): generic core (parts with part-space
  triangles, uvs and a sphere; `pick(origin, dir, place)` nearest hit);
  `make` and `load` SFileComplex-bound, `load` on a `QThreadPool` worker,
  the shape released on the main thread.
- `Eng::pickCab` places parts with `cabPlacement * partMatrix(state,
  part)` (generic); `cabRay` unprojects in the view the cab was drawn in.
- The mouse (RouteEditorGLWidget.cpp): press → `cabPress` (a dragged kind
  builds a screen path from `Eng::cabPartPaths`, which samples key frames;
  else "press"), drag → the nearest path frame through `CabView::valueAt`,
  release → "release", wheel → "+"/"-"; hover label after 0.4 s.
- A pick is the command `cab.<index>` with "press", "release", a number or
  "+"/"-"; `LuaLoco::command` (needs `definition->cab3DView`) calls
  `tsre.cabview.commands`, whose `{name, value}` the loco runs.

## 4. Values back

`SimulationHost::publish` → `SimulationCore::cabValues` (needs
`cab3DView`) → `LuaLoco::cabValues` → `cabview.values(controls, out.values,
state, speed)`, one value per control → `DrivenFrame::cabControls` →
`Eng::setCabControls` → `poseCab`. The controls table `{type, kind, units,
style, min, max, order}` built once per `CabView`. `tsre/cabview.lua`:
`M.types` (Open Rails type → value), `M.values`, `M.actions` (type →
commands), `M.commands`; a loco script extends them (`require
"tsre.cabview"`), shared by the Lua state. All keyed by Open Rails' type
names and `.cvf` units.

## 5. Displays

`Ui::ShapeDisplays`: image displays by image name through
`setTextureOverride` (S file backends only), square displays on a named
part (generic). Displays come from `.sd` only, and the 3D cab branch draws
none. Cab displays are a future task (scripting roadmap).

## 6. For a second cab kind

Assumes `.cvf`/S file: the `VehicleDefinition` cab fields; the viewpoint
and mount (Cab3D); `poseCab` (names, key frames); `pickCab` and
`CabPicker::make`/`load`; `cabPartPaths` (key frames); the mouse code's
use of `CabView` (kind, frames, `valueAt`, names); the simulation's
`cabValues`, `pushCabControls`, the `cab3DView` checks; `cabview.lua`'s
Open Rails names; displays from `.sd`.

Generic already: `DrivenFrame::cabControls`, `Eng::setCabControls`, the
`cab.<i>` command, `cabPlacement`, `cabDrawnView`, `cabRay`, the picker's
core, the hover label, the own state, `ShapeLod::apply`, `CameraMount`,
square displays, the Lua extension.

`E3dShape`: has the part API and per-state transforms, `enableSubObjByName`
(children too; `_on` parts start hidden), `setClassLight`, primitives and
highlight; lacks key frames, `setTextureOverride`, `displays`, `lights`,
`setLoadOptions`, and keeps no triangles on the CPU after load (a picker
needs its own read of the file; `E3dFile::parse` bypasses the cache).
Unverified: MaSzyna's rot (local Y) and mov (local Z) against TSRE's
imported axes, and whether cab models share the car's origin.

## 7. Driving 08's Left items that matter

Hidden controls still picked; `MouseControl ( 0 )` unused; alerter and
reset not modelled (MaSzyna's `security_reset_bt` on 280 of 291 cabs); no
per-control cab sounds; digits and screens not drawn (MaSzyna: `dgt` 106,
pyscreen 229 cab models); night scale open. Constraints to keep: pick in
the drawn view, release grabs when the Eng is rebuilt, release a
worker-read shape on the main thread, ride the driven cab only, keep lit
overlays out of picking.
