# 04 - MaSzyna cabs: load, ride, pose, work with the mouse

## Objective

A driven MaSzyna engine is driven from its own 3D cab: the `.mmd`'s cab
model loaded and drawn, the camera at the driver's place, its levers,
switches, needles and lamps posed from the loco script's values, and the
interactive ones worked with the mouse, sending commands to the script as
an ENG's 3D cab does. TSRE's 3D cab (task driving 08) stays one system;
MaSzyna's cabs come in as a second kind of cab description beside the
`.cvf`.

Status 2026-11-10: steps 1 to 5 committed on `feature/maszyna`, not merged
([results](#results-2026-11-10)).

Reviews behind it:
- [Cab controls in `.mmd`](reports/mmd-cab-controls.md): how MaSzyna reads
  a cab, the 387 names, the pack's statistics, what makes the bridge hard.
- [TSRE's 3D cab chain](reports/tsre-3d-cab-chain.md): what assumes a
  `.cvf` and an S file, and what is generic already.

Not in this task: per-control sounds, Python screens (229 of 291 cab
models; RmlUi displays later), digit counters (`dgt`), wipers (`wip`),
analog clocks, the engine room (cab 0), vigilance (task 03 step 7),
control cars proper.

## Decisions so far

- **The user, 2026-11-10:** the cab task follows a working driving HUD
  (task 03, done): load, render, interactive items.
- **Answers to the design's questions** (the user, 2026-11-10):
  - One interface for both kinds of cab description, `CabControls`, with
    driving 08's `.cvf` code moved behind it (section 2).
  - The vehicle is drawn too, with the cab model on top, as MaSzyna does.
    ENG cabs skip the body only because most Open Rails locos have a
    low-poly cab merged into the loco's shape, which a 3D cab cannot
    replace; a MaSzyna body has no such cab in it (section 3).
  - TSRE's mouse, with a drag kept near the stop it last sent (section 5).
  - ET22 first through step 5, then the other engines; sounds, screens,
    counters and clocks later.
- **After step 5** (the user, 2026-11-10): [1] pressed again in the cab
  goes to its next place, round them: every 3D cab's viewpoints looking
  the way the train runs (an Open Rails cab's alternate ones, MaSzyna's
  standing and seated driver), then the window's eye a train without a 3D
  cab has. ET22's cab looked flat, with the controller wheel drawn twice,
  and `.e3d` colours washed out: to find out and fix.
- **Earlier ones that apply:** no side files (the cab is read from the
  `.mmd` as it is); `.mmd` tokens through `MaszynaText` (task 02: includes
  with `(pN)`, `[ a b c ]` gives its first; random includes in the pack
  pick sound sets, horns and converters, not cab controls); MaSzyna's
  lower-casing on the `.mmd` side only; values SI inside TSRE, a file's own
  units only where its numbers are used (as `tsre.cabview` gives a `.cvf`
  its units); TSRE's 3D cab only, no 2D cabs.

## What there is

- **MaSzyna's cab** (`201e.mmd:139-238` for ET22): per end
  `cab<N>definition:` (a box `x1 y1 z1 x2 y2 z2`), `driver<N>pos:`,
  `driver<N>sitpos:` (374 files), optional `driver<N>angle:` (24 files),
  `cab<N>model:` (`\201e/kabina_a.t3d`, read as the `.e3d`; `none` in 15
  files: the controls on the outside model). Then the controls, in two
  syntaxes: `label: submodel anim scale offset friction [endvalue
  endscale]` or `label: { submodel anim scale offset friction key: value
  ... }` (`type:`, sounds). `i-*` lamps: `label: submodel`, shown as
  `submodel_on` / `submodel_off`. Controls written once after the cab 2
  section serve both ends (the same submodel names in kabina_a and
  kabina_b); the cab 0 section comes last.
- **Values and motion:** a control's value in MaSzyna's units (notch
  numbers, the brake handle's positions, km/h, bar, V, A; the pressures
  are bar numbers in MaSzyna's code though its logs say MPa: ET22's gauges'
  scales put 5 bar at 133°), scaled = value ·
  scale + offset; `rot` turns the submodel by scaled · 360° about its own
  Y, `mov` moves it by scaled along its own Z (Gauge.cpp:463-467);
  `rotvar`/`movvar` lerp toward endscale; `friction` a smoothing time
  constant in seconds. ET22's main controller turns −0.0197 of a turn a
  notch, 48 notches: 340°.
- **Drawn:** MaSzyna draws the whole vehicle (body, low-poly interior) and
  the cab model on top, in the car's own matrix
  (opengl33renderer.cpp:3915ff); the cab models share the car's origin.
- **TSRE's 3D cab** (report §1-4): the cab shape drawn instead of the body;
  `Cab3D` viewpoints from the ENG; `.cvf` controls matched to part names
  and posed by key frames; picking on `SFileComplex` triangles; the mouse
  dragging along a path of key frames; `cab.<i>` → `tsre.cabview.commands`;
  `tsre.cabview.values` → the frame → `poseCab`.
- **`E3dShape`** has the part API and per-state transforms (`partRest`,
  `setPartTransform`, `partMatrix`), `enableSubObjByName`,
  `setClassLight`; no key frames, no CPU triangles after load.
  `CarAnimation` already turns MaSzyna parts as `rest · animation` (task
  02: doors, pantographs, mirrors), the convention gauges use.
- **The scripts** (task 03): MaSzyna's levers with `.fiz`'s positions (the
  main and shunt field controllers by notch, the train brake from `Cntrl.`'s
  rows), and values such as `throttleNotch`, `shuntFieldNotch`, `ammeter`,
  `overload`, `resistors`.

## Design

### 1. The cab read from the `.mmd` (C++)

`MaszynaCab` (`src/tsre/maszyna/MaszynaCab.h/.cpp`), read lazily from the
`.mmd`'s tokens as `MaszynaVehicle::physics()` is, shared by the
definition:
- **Ends** 1 and 2: box, driver's and sitting places, angle, model path
  (resolved as task 02 resolves `cabModels`); cab 0 kept for later.
- **Controls** per end, in file order, as MaSzyna's `TCab::Load` reads
  them (from the end's definition to `cab0definition:` or the end): label,
  submodel, animation (`rot`, `rotvar`, `mov`, `movvar`, `wip`, `dgt`),
  scale, offset, friction, endvalue, endscale, `type:`, sound keys kept as
  written (for the sound task). Only the 386 labels MaSzyna knows start an
  entry, which takes as many words as MaSzyna reads for it, whatever they
  are; every other word is passed over, as MaSzyna does (the pack has
  destination signs and block keys without their block after its cabs).
- **Lamps** `i-*` with their submodel; the special entries (`pyscreen:`,
  `clock:`, `i-doors:`, `brakes:`, `eimscreen:`) kept as written for
  later.
- Suites: `mmd-cab` on small files (both syntaxes, includes, the end
  rules, the quirks) and `mmd-cab-corpus` over the pack (1,117 cabs, 666
  drivable, 291 cab models; only the pack's own errors; the time).

### 2. A second kind of cab description (C++)

What the 3D cab code takes from a `.cvf` becomes an interface, `CabControls`
(the user's choice), with two kinds:
- **`.cvf`** (driving 08's code moved behind it, unchanged in behaviour):
  `CabView` + `CabParts`, key frames.
- **MaSzyna**: `MaszynaCab`'s controls for the end ridden.

Per control it answers: a name for the hover label; whether it is dragged
(levers, switches of several positions) or clicked; the stops a drag goes
through and the value each sends; its parts in the cab shape; a part's
local matrix at a stop (the drag path: key frames for a `.cvf`, the
formula for MaSzyna); posing all parts from the values (in the cab's own
state, with its clock for MaSzyna's friction); the Lua fields of the
controls table and the Lua module that reads them (`tsre.cabview`,
`maszyna.cab`).

The places that change: `Eng::poseCab`, `pickCab`, `cabPartPaths`,
`CabPicker::make`/`load`, the mouse code in `RouteEditorGLWidget`
(`cabDragged`, `CabView::valueAt`, `CabView::name`), `LocoScript.cpp`'s
`cab.<i>` and `cabValues` (`definition->cab3DView` checks and
`pushCabControls`), `VehicleModel::cabValues`, `SimulationCore::cabValues`.
`DrivenFrame::cabControls` and `Eng::setCabControls` stay.

`Cab3D` gains what MaSzyna needs and the ENG reading leaves as today: a
shape per viewpoint (MaSzyna has one model per end; Open Rails keeps one
for all), whether the shape's x is mirrored (MSTS shapes yes, `.e3d` no:
`cab3DCameraPosition` mirrors it now), a move box per viewpoint (the cab
box; else the 0.5/0.3/0.5 m default).

### 3. Ride and draw

- A MaSzyna definition with a cab gets `Cab3D`: viewpoints at
  `driver1pos` (front, looking +Z, or `driver1angle`) and `driver2pos`
  (rear), the boxes as move limits; Drive, following and Play ride them as
  they ride ENG cabs. Built: MaSzyna's start (standing) place only; TSRE
  rides the first viewpoint facing the way and cycles none, so the seated
  place (`driver<N>sitpos:`, at the desk) waits for a key to move there.
- **Drawing** (the user's choice): the vehicle as outside (body, its extra
  shapes, `CarAnimation`'s pose) plus the cab model on top, as MaSzyna
  draws it; the models are made for that (wipers, mirrors and some lamps
  are on the outside model, gauges fall back to the low-poly interior).
  ENG cabs keep drawing the cab instead of the body: most Open Rails locos
  have a low-poly cab merged into the loco's shape. Whether to draw the
  body is a property of the cab description (`CabControls`), not a test
  for MaSzyna in `Eng`.
- The cab model loads through the ShapeLib as an `E3dShape`, with the
  car's repaint where it has replaceable textures; the lit sections (task
  03 commit 1fc454cb) drive its cab light (`cablight_sw`).
- To check on ET22 with captures: the side the driver sits on, the
  turning direction of `rot` (the angle's sign through TSRE's axes), the
  cab model's place against the body.

### 4. Pose and values

- **C++:** per control, the value smoothed by its `friction` (an
  exponential toward the target with that time constant, as Gauge.cpp
  does), then `rest · rotY(scaled · 360°)` or `rest · translate(0, 0,
  scaled)` through `setPartTransform`; `rotvar`/`movvar` lerped; a part
  found by `name`, else `name_off`; lamps and `_on` parts by
  `enableSubObjByName`. Controls without a value keep their rest pose.
- **Lua:** `loco/maszyna/cab.lua` (module `maszyna.cab`), as
  `tsre/cabview.lua` is for `.cvf` controls: `M.labels` keyed by
  MaSzyna's labels, each with its value (from the loco's values and state,
  in MaSzyna's units: SI converted here) and its actions (commands); `M.values`,
  `M.commands`, and `M.stops` (the positions a lever takes, from the
  script's levers: `.fiz`'s notches and brake positions). A loco script
  extends it (`require "maszyna.cab"`), as it does `tsre.cabview`. Built:
  the stops asked once and published with the values
  (`DrivenFrame::cabStops`); the module gets the loco's last inputs too
  (the brake pipe, cylinder and line voltage are the host's, not the
  script's values).
- First labels: those ET22 has, as far as task 03's scripts have their
  values and commands (the report's table: mainctrl, scndctrl, dirkey,
  brakectrl, localbrake, alarmchain, sand_bt, releaser_bt, main_on/off_bt,
  pant*_sw, fuse_bt, maxcurrent_sw, compressor_sw, converter_sw,
  cablight_sw, horn; gauges pipepress, brakepress, compressor, scndpress,
  hvoltage, hvcurrent, the speedometer; lamps mainbreaker, motor_ovld,
  resistors, …). The corpus suite lists each label: bound or not, on how
  many cabs, to choose the next ones.

### 5. The mouse

- **Picking:** `CabPicker` gets the `.e3d`'s triangles per part from a
  worker read (`E3dFile::read` past the cache), placed with `partMatrix`
  as today; a submodel's `_on` and plain parts both pick their control;
  only what MaSzyna's control mapper works is picked (switches `_sw`,
  buttons `_bt`, universals, levers; not needles and lamps). Built: hidden
  parts are still picked (driving 08's open item; a MaSzyna control's
  hidden part is its own control's, so it does not matter there).
- **Working** (the user's choice): TSRE's model for both kinds: drag a lever along
  its path, the wheel a stop, click a switch or button ("press",
  "release"); the `type:` (push, impulse, return, toggle) handled by
  `maszyna.cab`. A drag looks for the nearest point only near the stop it
  last sent, so a controller turning nearly a whole turn (ET22's 340°)
  does not jump across its ends; ENG cabs get that too.
- **Hover:** the label's name: the driving panel's text where the label
  is bound to a TSRE command (throttle, train brake …), else the label made
  readable. Built: the label made readable only ("main_on_bt": "Main on").

### 6. Later (each its own task or step)

Per-control sounds (inc/dec, on/off, per value, soundproofing) with the
sound task; `dgt` counters, `wip` wipers, `clock: analog`; Python screens
as RmlUi displays (needs `setTextureOverride` on `E3dShape`); the
low-poly interior's and outside model's gauges and lamps; `cab1model:
none` (15 files); cab 0; vigilance and the cab signal (task 03 step 7);
control cars proper.

## Steps

Each step ends with `build all`, `ctest` and the app suites; driving 08's
suites (`cab3d`, `cabview`) and captures unchanged after step 2.

1. **Reader**: `MaszynaCab` + `mmd-cab`, `mmd-cab-corpus`.
2. **`CabControls`**: the `.cvf` kind moved behind it; `Cab3D`'s per-
   viewpoint shape, mirror and box. No change for ENG cabs.
3. **Ride and draw** ET22: Drive into cab 1 and cab 2, the sitting place;
   captures (axes, side, scale).
4. **Pose**: C++ posing with friction; `maszyna.cab` values for ET22's
   gauges, levers and lamps; captures with levers moved by keys.
5. **Mouse**: `.e3d` picking, stops and paths, commands; the drag near the
   last stop; a test driving ET22 by the mouse alone (start, notch up,
   brake), as driving 08's mouse test does.
6. **More labels and engines**: EU44 (induction), SM42 (diesel), EN57
   (multiple unit), by the corpus suite's list.

## Verification

- `mmd-cab-corpus`: the report's numbers, no problems.
- ENG cabs: driving 08's suites and its USA1/OR_PSK captures the same
  after step 2.
- ET22: each bound control's pose at its stops against MaSzyna's formula
  (part matrices in a test); captures in cab 1 and 2; a mouse-only drive.
- The cab's frame time on this server against an ENG 3D cab's (Release
  build only).

## Results (2026-11-10)

- **Reader** (`MaszynaCab`, `MaszynaVehicle::cab()`): suites `mmd-cab`
  (19 checks on built files) and `mmd-cab-corpus`: 1,453 vehicles read in
  about a second, 1,117 with a cab, 666 drivable, 291 cab models. The
  review counted 1,109 and 658: its script missed the 8 EN57 units in
  `pkp/en57km_v1/*czoper*`, whose `.mmd` starts with a UTF-8 byte order
  mark before its include (MaSzyna skips it). The pack's own errors, read
  as MaSzyna reads them: 77 `i-doors:` without their number (the next
  label taken as the lamp's submodel), 3 entries short of a number (e186,
  ed72), 2 base files in `en57akm_2013_v1/cfg/` whose includes sit beside
  the vehicles including them. The pack also has destination signs and
  block keys written without their block after its cabs; MaSzyna passes
  over them, so the reader knows MaSzyna's 386 labels.
- **`CabControls`** (`trains/CabControls.h`): `CvfCabControls` holds
  driving 08's code; `Eng`, the mouse, `LocoScript` and the simulation
  ask the interface. The definition's `Cab3D` has a shape per viewpoint,
  the mirror, a box and the controls of each end; a MaSzyna vehicle's is
  made on first use (`VehicleDefinition::cab3DOf`), the pack scan reading
  none. ENG cabs unchanged: `cabview` and `cab3d` suites, and PRUDNIK's
  EP09 `cab-mouse` capture lists, picks and sends the same as main's
  build.
- **Ride and draw** (`MaszynaCabControls::cab3D`): ET22 ridden from cab 1,
  its cab model on top of the vehicle drawn as outside (the outside
  model's wiper through the windscreen), the cab model taking the car's
  livery; the head at MaSzyna's standing place, the box as the move limit
  (`CameraMount::moveCentre`). The axes needed nothing: the desk, its
  wheel and handles where the model has them.
- **Pose and values**: `MaszynaCabPose` (rot, mov, rotvar, movvar, wip,
  dgt, the lamps' `_on`/`_off`, friction and MaSzyna's hundredths);
  `maszyna/cab.lua` for ET22's levers, buttons, pantograph and light
  switches, gauges and lamps; other switches and buttons turn without
  doing anything. The train brake's `.fiz` positions kept per notch
  (`c.trainBrake.positions`; ET22: −1 to 6). Suite `mmd-cab-script` (17
  checks): ET22's viewpoints and controls, stops, values, commands.
- **Mouse**: `.e3d` picking (ET22's model: over 30 controls for the
  mouse, read on a worker), drag paths through the stops, the drag kept
  near its last point (both kinds; EP09's capture unchanged by it). ET22
  driven by the mouse alone in a capture: the breaker's off and on
  buttons pressed and released, the main controller three notches up by
  the wheel, the brake handle dragged from running through positions 1, 2
  and 3, the compressor and cab light switches clicked; the controller
  wheel and the brake handle turn with their values. The capture harness
  got a mouse action `["turn", yaw, pitch]`: a 3D cab's desk is often off
  the start view (ET22's at MaSzyna's standing place).
- The Vehicle files dialog lists a MaSzyna vehicle's cab models (its
  Archive missed them).
- Checks: build `all`, `ctest` (11); the app suites (94 pass; 7 need a
  route or cases and stop first, as before); `fiz-corpus`,
  `maszyna-vehicle-corpus`, `maszyna-scripts`, `mmd-cab-*`, `e3d-corpus`
  on the pack; `cabview-corpus`, `driving-corpus` on OR_PSK; task 03's
  ENG drives (USA1 electric, USA2 diesel, EUROPE2 steam) byte-identical.
- To compare with MaSzyna (not run here): the controls' turning direction
  (the matrices are `CarAnimation`'s for MaSzyna parts, task 02).

- **After step 5:**
  - [1] again in the cab: `Eng::cab3DViewpoints(rear)`, `Consist::rideCab3D`
    taking the nth, `RouteEditorGLWidget::rideTrain`'s place (the window's
    eye after the viewpoints); MaSzyna's seated place a viewpoint after its
    standing one. Captures take `"cabPlace": n`.
  - The wheel drawn twice: the low-poly interior's `cab1` part (a whole
    low-poly desk with its own wheel) was drawn inside the detailed cab.
    MaSzyna hides the low-poly interior's `cab<N>` from cab N while its
    detailed model is drawn, all three with `jointcabs:` (36 files;
    `TDynamicObject`'s LowPolyIntCabs, DynObj.cpp:1351). Now TSRE does: a
    MaSzyna car's extra shapes draw in a state of their own even without a
    livery. Made generic after the review (below): the viewpoint lists the
    parts hidden on the car's own shapes.
  - Materials (the user: a preset of PBR factors by MaSzyna's material
    type, not one default; diffuse-only parts perhaps through the non-PBR
    path, if worth it). MaSzyna picks a shader by `shader:`, else by the
    textures filled (none `colored`, one `default`, two `default_2`, a
    reflmap reading slot 1; model/material.cpp), and weighs reflection by
    its `reflection` parameter: zero for `default`, `colored`, sunless and
    shadowless normal maps, one times a map's alpha for `normalmap`,
    `parallax`, `reflmap` (shaders/light_common.glsl). TSRE drew every
    part with full dielectric reflection (KHR specular 1, roughness 0.5),
    a sky sheen MaSzyna's default material has none of, strongest at
    grazing angles (the desk seen from standing).
  - The pack per `.e3d` part (OR_PSK_NEW's links, 6,197 models, 253,643
    parts): the default material 66.5 % of parts (92.6 M triangles),
    liveries' skin slots 22 % (147.8 M; texture + texture2: MaSzyna's
    reflmap), normal maps 4.8 % (17.3 M, 1,742 of them specgloss), colour,
    reflmap and others 1.1 %, materials not found 5.6 %.
  - Built: `MaszynaMaterial::family()` and its preset: KHR specular
    strength 0 for no reflection, `param_reflection` (else 1) for the
    reflective families with the reflmap's, slot 1's or the normal map's
    alpha as the specular map (the PBR shader's KHR_materials_specular
    mask), roughness 0.15 there (MaSzyna's reflection is a mirror's where
    no specgloss map gives it). `glossiness:` no longer a roughness (in
    MaSzyna's shaders it only scales a specgloss map's gloss); task 01's
    two checks changed with it.
  - The non-PBR path for the default material: not taken. PBR with no
    specular is MaSzyna's `mat_default` (ambient and sun diffuse); one
    vertex layout and one pass per model stay, night textures and lamps
    the same for every part. The legacy path lights as MSTS does, not as
    MaSzyna.
  - Captures (seated, ET22): shadows matter most (the sun lit the cab
    through its roof without them); no specular deepens the turquoise and
    takes the sheen off. Still darker and less saturated than MaSzyna:
    its sun diffuse is scaled 2.5 and its ambient 0.65, N·L sharpened.
    With the presets: the desk seated as in MaSzyna's video, one wheel over
    its dial plate; ET22's body (its livery a reflmap) without a sheen.
  - The capture harness's outside eye (`rideTrainVehicle`) leaves a 3D cab
    a view before rode: the cab's mount kept the eye inside the cab box.
  - Specgloss maps (the user: the cheap fix, a shader flag, well
    documented). MaSzyna's `*_specgloss` shaders read R as the strength of
    the sun's highlight, G as gloss (their roughness is 1 − G; G times
    `param_glossiness`, divided by it again), B as metalness: only B is
    where an ORM has it, and a factor cannot turn G into roughness. Now the
    specgloss map goes in the metallic-roughness slot with
    `RenderItem::Pbr::metallicRoughnessLayout` `SpecGloss` (a flag at first): `PbrShading.glsl`
    (bit 32768 of `pbrTextures`, set by both renderers next to MSTS's
    8192 and 16384) reads roughness 1 − G and metalness B (times their
    factors, left at 1), multiplies the specular strength by R, and takes
    no occlusion from it. MaSzyna weighs its sun highlight by R and its
    reflection by a map's alpha; TSRE has one weight for both, the mask's
    alpha times R. A `tsre_orm:` texture still wins over it. 1,742 parts
    in the pack. `default_specgloss` (78 parts) keeps no reflection, and
    with it loses its R-weighted highlights. `param_glossiness` below 1
    (MaSzyna's roughness 1 − G × that) is not reproduced. Checked: the
    e3d-shape suite (a specgloss livery's packet), the 21xm cab
    (`kabina_przod.mat`, normalmap_specgloss) in the Shape Viewer on
    OpenGL and on QRhi (Vulkan, lavapipe; it needs more than 30 frames to
    load there), ET22's cab on QRhi.
  - "Section" out of the generic shape interface (the user: a MaSzyna word;
    "class" one any shape provider can use to group its parts):
    `ComplexShape::setClassLight(state, partClass, level)`. `E3dShape`'s
    classes are the model's lit sections; `E3dFile` keeps MaSzyna's word
    for them, as its format names them.
  - The two renderer issues from the glow review (the user: either the
    night texture code in PBR with a factor, or an exposure factor on
    glTF's emission so parts choose). Built: the factor,
    `RenderItem::Pbr::emissiveExposure`: 0 as painted on the screen (the
    QRhi renderer undoes its auto exposure, as before), 1 as a night
    texture's light (`PbrShading.glsl` takes `NightTextures::Frame`'s
    scale, under cover its enclosed one on QRhi, as the legacy shading's
    night textures), between a mix. One mechanism for both issues:
    - glTF keeps 0: its content and the Shape Viewer's samples unchanged
      (`gltf-pbr-gl` 21 of 21) until a strength for lamps seen by day is
      settled (task renderer 29).
    - S files' PBR parts (task renderer 35): `MstsMaterials::fill` keeps a
      night texture as emission at its night strength, following the
      exposure (1); full and half bright, which the legacy shading draws
      unlit and as painted, now unlit in PBR too (their light was lost).
    - `.e3d`: lamps (threshold 1 or more, not in a class) as painted (0),
      read as on by day; lit classes' parts (cab interiors) follow the
      exposure (1); dusk parts (threshold between 0 and 1; 7,694 parts at
      0.35 in the pack) now glow as night textures (1), as the review's
      table had them; a glowing part without a texture glows its colour,
      not white.
- **Before the merge** (the user, 2026-11-10: three generic refactors, the
  cab light):
  - MaSzyna words out of generic structs: `Cab3D::Viewpoint::hiddenParts`
    (parts hidden on the car's own shapes, body and extra shapes, while
    ridden from it; MaSzyna's `cab<N>`, all three with `jointcabs`) and
    `Cab3D::lightClass` (each end's light class on them) replace
    `cabNumber`, `jointCabs` and `FreightAnim::lowPolyInterior`;
    `Eng::hideParts` keeps what it hid by shape state, forgotten with it.
    `RenderItem::Pbr::metallicRoughnessLayout` (`Gltf`, `OrmClearcoat`,
    `SpecGloss`) replaces the two flags. `VehicleDefinition::cab3DMaker`
    (a function making the cab on first use) replaces the MaSzyna cache:
    the definition no longer calls MaSzyna code for it.
  - The cab light (MaSzyna: 0 off, 0.4 dimmed, 1 on; `set_cab_lights`
    lights the low-poly interior's `cab<N>`, its interior light the cab
    model whole): the driven cab's `cabLight` (`Eng::setCabLight`) lights
    the driven end's `Cab3D::lightClass` on the car's own shapes (seen
    from outside) and the cab's shape whole (the empty class, new:
    `setClassLight(state, "", level)`, all but what glows of its own),
    following the exposure. Glowing colour parts take their colour times
    the light (white before). Captured at 21:30: the desk dark with it off,
    lit with it on; the glass lit too, as MaSzyna's interior light lights
    it (to judge on hardware).

## Open: work left

- The cab light: dimmed (`cablightdim_sw`, 0.4) and the half light
  without 110 V not read; MaSzyna's `InteriorLight` colour not used.
- Hover names: the label made readable; the driving panel's texts for the
  bound labels.
- Hidden parts still picked (driving 08's item).
- Labels without values yet: radio, train heating, converter, compressor
  and light switches do nothing; `lvoltage` a fixed 110 V; `hvcurrent1`-`3`
  all the motors' current; clocks; `i-security_*` (vigilance, task 03 step
  7).
- Materials: MaSzyna's brightness against TSRE's (sun 2.5, ambient
  0.65) to compare on the user's hardware; liveries' reflection on bodies
  to look at; `default_specgloss`'s highlights (one specular weight for
  highlight and reflection).
- Section 6's list; step 6 (EU44, SM42, EN57).

## Questions

None open: answered 2026-11-10 ([decisions](#decisions-so-far)).
