# Task 08 - Interactive cabs

Status: notes 2026-10-30, the user's; the user's answers in (2026-10-30); design and steps on `feature/interactive-cabs`.

The user (2026-10-30): reviewed OR_PSK's 3D cabs first ("if we have any
cvf files that cover well orts 3d cab features, or should I look for some
more before doing interactive 3d cab task"), then: "If no task for
interactive cabviews, make it now and make notes."

The goal, as discussed: the driven loco's cab works. Its levers, switches,
needles and lamps follow the loco's values, the mouse moves its controls,
and its screens show and take input, Open Rails' content working as it is.

## Today in TSRE

- **3D cabs are drawn** (task shapes 12, `feature/cab3d`, in main):
  `ORTS3DCab` and its alternate viewpoints, the riding camera at each end
  within its head box. Parts are posed by their keyframes
  (`ShapeAnimation`, `setPartFrame`), but no cab part follows a value yet.
- **No `.cvf` is read**, 2D or 3D (`VehicleDefinition` keeps the 2D cab's
  file name only). TSRE draws no 2D cabs.
- **The driven loco is controlled** by the driving panel (RmlUi), the keys
  (task 07) and commands (`SimulationController::command`); the scripts
  publish their values in `train.values`.
- **Selection ids** have a `ShapePart` kind (task shapes 13): a shape's
  instance and the primitive drawn, so the part under the mouse is known.
- **The 3D pointer** (`RouteEditorGLWidget::readPointerPosition`): the
  depth under the mouse, read at most every 50 ms (the latest frame's, or
  exact), unprojected to a world point.
- **Shapes free their CPU arrays** after the upload unless their retention
  keeps them (`SFileComplex::Retention`).
- **Cab displays** are planned in the scripting roadmap: an RmlUi context
  drawn into a texture on a 3D cab's screen part or a 2D cab (offscreen
  targets in the render queue needed).

## Open Rails, as a functional reference

Upstream 8aafb3147: `MSTSLocomotiveViewer.cs` (ThreeDimentionCabViewer),
`Viewer.cs` (mouse), `MSTSLocomotive.cs` (BuildCab3DView),
`EuropeanTrainControlSystem.cs`.

- **The 3D cab's `.cvf`:** `CABVIEW3D/<shape name>.cvf`, else
  `CABVIEW3D/<the ENG's CabView>`, else `CABVIEW/<the ENG's CabView>`.
- **Part names:** `TYPE:order[:p1[:p2]][-subpart]`, matched to the `.cvf`
  control of that type and order (the nth of that type in the file).
  Several parts may make one control (`-1`, `-2`).
- **How a matched part is drawn**, by its control's kind:
  - digital displays: digits drawn on the part (ThreeDimCabDigit);
  - gauges that are not pointers: drawn by code (ThreeDimCabGaugeNative);
  - pointer gauges, dials, levers, switches, lamps, the cab signal display:
    keyframed parts, the frame from the value (AnimatedPartMultiState);
  - distributed power and ETCS screens: drawn into the part's texture.
- **Parts with no `.cvf` entry:** outside wipers, mirrors, doors, the four
  outside windows, `ORTS_ITEM1/2` continuous and two-state.
- **The mouse in a 3D cab:** a lever or switch control (a discrete
  renderer) whose part's pivot lies within 10 cm of the mouse ray; the
  `.cvf`'s `MouseControl` is not asked (in 2D it is). The ETCS screen
  takes no clicks in 3D (its interface is commented out).
- **The ETCS screen's values:** `ETCSStatus`, filled by the train control
  script: level, mode, allowed, intervention, target and release speeds,
  the target distance, what is supervised and the warning level, the
  planning area (speed targets, gradients, track conditions), text
  messages, menu buttons. The built-in system sets only the speeds and the
  warning level. Its screen is about 3,000 lines of C#. The `.cvf` declares
  it with `ScreenDisplay ( Type ( ORTS_ETCS ... ) )` and free key/value
  `Parameters`.

## OR_PSK's content (review 2026-10-30)

Each ENG's `ORTS3DCab` resolved as Open Rails does; the shapes read with
`TSRE5vc --shapeconv -f unicode -c uncompressed`; their part names matched
to the `.cvf` controls (a quick script; TSRE has no `.cvf` reader yet).

116 ENGs have a 3D cab, 65 distinct shape and `.cvf` pairs. 18 cabs (67
ENGs) have named cab parts, nearly all keyframed and matched:

| Cab | ENGs | Kind | Parts matched to the `.cvf` |
|---|---|---|---|
| 749 / 751 (`common.cab`, ZSSKC751) | 25 | diesel | 23 of 24 |
| EP09 (`common.cab`) | 16 | electric | 18 of 18 |
| Elf 1 (EN76) | 6 | electric unit | 16 of 16, a combined handle |
| CD130 | 4 | electric | 22 of 22, 5 outside parts |
| CD181, ZSSKC183, ZSCS125, CD122, CD123 | 8 | electric | 18 to 21, 3 to 5 outside parts |
| Flirt 3 (ED160) | 2 | electric unit | 14 of 14, a combined handle, door buttons |
| 720 (4), 721 (2) | 6 | diesel | 12 of 16 |

- **Covered:** levers (throttle, train and engine brake, combined
  throttle and brake); switches (horn, master key, direction, headlights,
  pantographs, breaker, train supply, cab light, wipers, sanders, bell,
  doors); needles (speed, brake pressures, ammeter, line voltage, RPM, oil
  pressure, engine temperature: every 3D gauge is a pointer); lamps
  (wheelslip, alerter) and the cab signal display; outside windows and
  wipers; alternate viewpoints (122, 123, 181, 183 three each, 125 one).
- **Missing from every OR_PSK 3D cab:** digital displays on a part (2D
  cabs have them: 233 `.cvf` files), gauges drawn by code, screens (no
  ETCS or distributed power in any of the 608 `.cvf` files), a steam 3D
  cab (steam cabs are 2D only: BR03, BR38, BR50 ...), sub-parts, mirrors
  and doors outside, `ORTS_ITEM` parts, glTF cabs, cruise control in 3D
  (2D only, the ED78 Impuls).
- **Quirks:**
  - 49 ENGs (EN57, SM31, SU160, SN84) name the vehicle's own outside shape
    as the 3D cab, `..//x.s` from a `CABVIEW3D` folder that does not exist:
    Windows resolves `..` without the folder, Linux does not. A viewpoint
    only, no cab parts.
  - Misnamed parts: 720/721 `REAR_HLIGH`, `CAB_LIGHT`, `CERVRUCICKA`;
    `PANTOGRAPHTOGGLE1` missing from the 181, 183 and 125 `.cvf` files; the
    CD130's headlight switch has no keys.
  - `PKP_EN57A-OR/xhl`: no `.cvf` found; `kabinaAL_3D.s` missing (2 ENGs).
- **Enough to start:** reading the `.cvf`, parts following values,
  levers and switches moved by the mouse, needles and lamps can all be
  built and tested on OR_PSK (749/751 diesel, EP09 and 183 electric, Elf 1
  with a combined handle).
- **To look for before the later steps:** a 3D cab whose `.cvf` has
  `Digital (` or `DigitalClock (` controls with parts of those names; one
  with `ScreenDisplay ( Type ( ORTS_ETCS ...`; a steam 3D cab (`REGULATOR`,
  `CUTOFF`). The same survey can be run on new content.

**Night (checked 2026-10-30, the user's question):** no 3D cab has night
textures; no `CABVIEW3D` has a `NIGHT` folder, and Open Rails uses those
only for 2D cabs' whole images (task shapes 12's findings). The night
look is in the cab shapes:
- **Bright materials:** full-bright (-8) or half-bright (-11) dials,
  needles, the signal display and lamps: about 7 % of the triangles in
  the Škoda cabs (CD122, CD123, CD130, CD181, ZSSKC183, ZSCS125), under
  1 % in the 749/751. The EP09, Elf 1, Flirt 3 and 720/721 are all in the
  normal material (-5; the 720/721 a little -6, specular): dark at night.
- **Cab light overlays:** `ORTS_CABLIGHT:0:1` to `:5` parts with pre-lit
  textures (`frnt...Light`, `kontrollight`, `749cab01_lit`,
  `CSD-pristroje01_lit`, the CD130's `budiky_noc`, night dials), moved
  into place by the cab light switch: two key frames, off sunk into the
  panel, on a little forward. In the Škoda cabs and the 749/751; there the
  throttle's scale (`THROTTLE:0:1`) and the breaker lamp
  (`ORTS_CIRCUIT_BREAKER_CLOSED:0:1`) have lit overlays too. The EP09 has
  the cab light switch but no lit textures.
- **For this task:** the overlays follow the cab light switch's value
  (`ORTS_CABLIGHT`); until then they rest off. The user (2026-10-30):
  night is a later part of the task.
- **Elsewhere:** TSRE draws half-bright (-11) at full brightness, Open
  Rails at 0.75, partly dimmed at night, so most overlays come out
  brighter than in Open Rails (the light materials item in task renderer
  35). A real cab lamp from TSRE's local lights while the cab light is on
  stays an idea (task shapes 12).

## Picking (the user's notes, 2026-10-30)

- **Buttons, levers, switches:** the selection ids (`ShapePart`) give the
  part under the mouse; clicking objects is easy with them.
- **Screens need a precise point:** where on the screen part, as texture
  coordinates, then the RmlUi context's coordinates.
- **The user's idea:** the pointer's 3D position is there every frame;
  only the mapping is missing. Avoided at the world's scale, but in a cab
  it might replace colour picking altogether.

Notes on it (to be weighed in the design):
- In a cab everything is within about 3 m, so the depth's precision, the
  reason against it at the world's scale, does not matter here.
- From the point to the part: the point lies on a triangle of a posed
  part. That needs the cab's triangles on the CPU (the cab shape kept at a
  full retention, or only its pickable parts' triangles) and the parts'
  posed matrices (the pose is on the CPU already).
- From the point to the screen's coordinates: the triangle's texture
  coordinates at that point. Exact: the view ray through the pixel
  intersected with that triangle's plane, so the depth only chooses the
  triangle; TAA's jitter and a cover glass before the screen then do not
  move the point.
- The pointer is read every 50 ms from the latest frame; a click would
  read it exactly (the pointer's exact mode), hovering can use the latest.
- Alike in cost: a CPU ray cast through the pixel against the cab's posed
  triangles gives the part and the coordinates with no read back; the
  depth (or the ray against the whole cab) keeps hidden controls out.
- Either way it serves hovering (Open Rails names the control under the
  mouse), dragging a lever along its movement, and touching a screen.

**Cost (2026-10-30, the user's question: is a ray cheap for a detailed
cab; the selection pass is not free either).** Counted in OR_PSK's cabs
(the first distance level), the times estimated, not measured:
- A whole cab: 25,000 to 72,000 triangles; the Elf 1 187,000. At some
  10 to 20 ns a triangle, a ray against all of it takes 0.3 to 1.5 ms
  (the Elf 1 2 to 4 ms): fine for a click, too much for hovering every
  frame.
- The named control parts alone: 1,500 to 13,400 triangles (2 to 25 % of
  the cab; the Škoda cabs' count includes their outside windows and
  wipers). Against them only, 15 to 270 µs; with each part's bounding
  sphere tested first (`SFileComplex` keeps one per part), a ray meets a
  few parts, some hundreds of triangles: microseconds. Kept on the CPU,
  those parts are some hundreds of kB.
- A control hidden behind the desk: its hit farther than the depth under
  the mouse (read for the pointer already). Or a tree over the cab's still
  parts, built once at load, if the whole cab is ever needed.
- TSRE's selection pass today: a click draws the whole scene with the
  selection shader, reads the pixel back (the CPU waits for the GPU), then
  draws the visible frame again. About two frames' work: fine for a click,
  not for hovering every frame. A pass drawing the cab alone would be
  smaller, but still a pass and a wait, in both renderers.
- The middle way the user named: the selection id picks the part, the
  intersection runs on that part only; exact and cheap per click, the pass
  still paid each time it is asked.

**The user's decision (2026-10-30):** "ray against interactive parts only
make sense, cheap, and you can make nice hover effects (border, text
label)."
- A ray through the mouse against the interactive parts only (the parts
  matched to `.cvf` controls the mouse can move, and screens), their
  bounding spheres first; the depth under the mouse for hidden ones. No
  selection pass in the cab.
- Hovering: the part under the mouse shown with a border, and a text
  label with the control's name (Open Rails shows the control's
  `ORTSLabel` from the `.cvf`, else its type's name; no OR_PSK `.cvf` has
  an `ORTSLabel`).
- The border: the user is not sure (2026-10-30). Open Rails draws none in
  a 3D cab (`Viewer.cs`, `Popups/ControlRectangle.cs`):
  - on a control the mouse cursor becomes a hand;
  - moving onto another control puts its name in the message list (a
    confirmer message, fading): its `ORTSLabel`, else the type's raw name
    (`THROTTLE`, `ORTS_MASTER_KEY`); train control and power supply
    scripts' controls give their own text;
  - 2D cabs only: Ctrl+F5 shows yellow rectangles round every clickable
    control, 3 pixels instead of 1 round the one under the mouse.
- Already there for it: `ComplexShape::pushPrimitiveHighlight` draws
  chosen primitives again in a flat colour over the shape (the Shape
  Viewer's picked primitives). A border round the part would be new
  drawing.

**Hovering, the user's decision (2026-10-30):** "I don't like a hand
cursor idea. So, for now let's settle on name display only. By mouse
position, shows when mouse hovers over item and not moving. Highlightings
or borders we could test later in the future. When drawing labels, be
careful not to use expensive solutions. I think that map view has some
good ideas to copy."
- No hand cursor.
- Only the control's name, by the mouse: shown when the mouse rests on an
  interactive part, gone when it moves.
- Highlights and borders: to be tried later.
- The label drawn cheaply, as the map's labels are (`MapLabelAtlas`,
  `src/tsre/map/MapLabelLayer.h`): a string painted once by QPainter (font,
  shaping, a halo) into a shared texture page, after that one textured
  quad at a fixed screen size. The cab shows one name at a time; each name
  is painted once.
- So the ray need not run on every mouse move: when the mouse comes to
  rest (and at a press, for clicking and dragging).
- Proposed: the name the `.cvf`'s `ORTSLabel`, else a readable, translated
  name for the control's type (TSRE's own texts), not the raw type name.

## Cab screens: the middle ground (the user's, 2026-10-30)

"Regarding ETCS screens, we need to rethink it anyway, if we implement
them at all using orts definitions or require new rmlui implementations. I
have some middle ground in mind, we do rmlui preset that uses those orts
values."

- **Kept from Open Rails:** the `.cvf`'s `ScreenDisplay` declaring the
  screen (its type, its place in a 2D cab or texture in a 3D one, its
  `Parameters`) and the values under Open Rails' names (`ETCSStatus`).
- **TSRE's own:** a shipped RmlUi preset (RML and Lua) drawing the screen
  from those values (`train.etcs`), into the 3D cab's screen texture, a 2D
  cab's rectangle or a window over the view. Users can restyle or replace
  it; other cab screens use the same way with their own presets.
- **It hinges on:**
  - who fills the values: Open Rails' come from train control scripts in
    C#; TSRE would need a simple supervision from signals and limits
    first, train control scripts in Lua later; without them the screen
    shows the speed and little else;
  - the speed dial's coloured ring: RmlUi draws no arcs; one small C++
    element, the rest RML;
  - the ETCS symbols: TSRE's own drawings, not Open Rails' files;
  - touch: the screen mapping above.
- Proposed: the values named exactly as Open Rails names them; the
  `.cvf`'s type choosing the preset (`ORTS_ETCS`, the ETCS one).

## The user's answers (2026-10-30)

"Current decision is not to use 2D cabs. Unless decision change in the
future, no 2D cabs. Agree with all, if nothing else, proceed."

- **No 2D cabs**, unless that decision changes: 3D cabs only.
- **The mouse:** a left press on an interactive part works the control (a
  click switches, a drag moves a lever); a left press elsewhere and the
  right button anywhere turn and fly the camera as now. A button held on
  a control does not count as turning: W, A, S and D stay the train's
  while a lever is dragged.
- **A lever dragged** along its own path on the screen (from its key
  frames); the mouse wheel steps it a notch. Not Open Rails' motion over
  the `.cvf` sizes, which 3D cabs often copy from their 2D cab.
- **The mapping** from `.cvf` control types to the scripts' values (to
  pose the parts) and commands (to send) in a Lua module beside the loco
  scripts (`tsre/cabview.lua`), which a loco script can add to; values
  turned into the `.cvf`'s units (the scripts give SI). First the types
  OR_PSK uses that the scripts have: the levers, direction, horn,
  pantographs, breaker, master key, lights, and the needles and lamps of
  speed, pressures, current, voltage, RPM and wheelslip.
- **Screens:** a later task of their own, with the supervision filling
  them.
- **Hover names:** the `.cvf`'s `ORTSLabel`, else a readable, translated
  name of the control's type, never the raw type name.
- **Content:** OR_PSK for the first steps; the user looks for 3D cabs with
  digits, ETCS and steam before those parts.
- **Night:** a later part of this task.

## Questions for the design (answered above)

1. 2D cabs too, or 3D first? TSRE draws no 2D cab today.
2. ~~Picking~~: decided (the user's, 2026-10-30): a ray against the
   interactive parts only, with hover effects (a border, a text label).
3. The values: a table from `.cvf` control types to the scripts' values
   and commands (`THROTTLE` to `throttle`, `TRAIN_BRAKE` to `trainBrake`
   ...); which types first.
4. Levers by the mouse: dragged along their movement in the cab, or Open
   Rails' way: the mouse's motion in pixels, vertical or horizontal by the
   `.cvf`'s `Orientation`, over the control's height or width from its
   `Position`, `Direction` giving the sign; the wheel too with a key held
   (`NormalizedMouseMovement`).
5. Screens in this task or their own, with the supervision that fills
   them.
6. More content first (digital displays, ETCS, a steam 3D cab), or
   OR_PSK's for the first steps.

## The design (2026-10-30, on `feature/interactive-cabs`)

What the code has, mapped first:
- **Reading:** `SimisDocument` / `SimisTree` (task 01's compact tree) read
  any MSTS text: UTF-16, the SIMIS header, includes, keywords without
  case; `SimisDocument::resolve` and `QDir::cleanPath` take `..` without
  the folder existing. Names are exact (the content-case rule).
- **The cab is drawn instead of the body** (`Eng::pushRenderItemBetween`)
  in shape state 0, which every loco of its class shares, and is never
  posed. The body's own posing (`Eng::poseShape`: its own `newState()`,
  `CarAnimation::pose`) is the pattern.
- **Parts:** `ComplexShape` gives `partCount`, `partName`, `partLastFrame`,
  `setPartFrame`, `partMatrix` (posed) on both MSTS backends; no name
  lookup. Both backends free a shape's triangles after the upload (the
  legacy one always, the complex one when compact); each part keeps a
  bounding sphere.
- **Values:** each published frame has the driven train's vehicles' values
  by name (`DrivenFrame::values`) and its cab vehicle. Commands are a name
  and a value (a flag, a number or text).
- **The mouse:** the view's handlers take the UI first; a cab handler fits
  before the camera while a ridden object takes the keys (`keysTaker()`).
  The 15 ms timer (`timerEvent`, after `keysTaker()`) can time a resting
  mouse.
- **A label:** `MapLabelAtlas` paints a string once into a texture page on
  both backends; one quad in the 3D view's UI pass (as the compass and the
  built-in HUD are drawn) shows it.
- **Rays:** `Intersections::segmentIntersectsTriangle` answers yes or no
  only; no ray test gives the distance or the point on the triangle.

### 1. The 3D cab's `.cvf` (`src/tsre/trains/CabView.h/.cpp`)

- Read with `SimisDocument` into `CabView`: its controls in file order,
  each with its kind (`Dial`, `Lever`, `TwoState`, `TriState`,
  `MultiState`, `MultiStateDisplay`, `CombinedControl`, `Gauge`,
  `Digital`, `DigitalClock`, `CabSignalDisplay`, `ScreenDisplay` ...), its
  type and style (`Type ( THROTTLE LEVER )`), its order (the nth of its
  type), `Position`, `ScaleRange`, `Units`, `NumFrames`, `NumPositions`,
  `NumValues` (and whether written descending), `Orientation`,
  `DirIncrease`, `MouseControl`, `ORTSLabel`, a gauge's `Style`. Numbers
  as written: the `.cvf`'s own units.
- Found as Open Rails finds it, read with the vehicle definition
  (`Cab3D::cabView`, shared, read-only): `CABVIEW3D/<shape name>.cvf`,
  else `CABVIEW3D/<CabView>`, else `CABVIEW/<CabView>`.
- **The parts:** `CabParts` from the cab shape's part names,
  `TYPE:order[:p1[:p2]][-subpart]`: each control's parts and last key
  frame; the outside parts (wipers, windows, mirrors, doors) apart; names
  matching no control listed as problems.

### 2. Values and posing

- **`tsre/cabview.lua`** (the simulation's Lua, beside the loco scripts):
  a table from control types to a value in the control's units, from the
  loco's published values (speed to km/h or mph, pressures to bar, psi,
  kPa or inHg, current, voltage, levers as fractions, switches as states).
  A loco script may add or replace types.
- **The host** asks it for the driven cab's controls when it publishes a
  frame: `DrivenFrame::cab`, one number per control.
- **The cab vehicle's Eng** draws its cab in a state of its own, its parts
  posed by Open Rails' rules: a lever's, switch's or lamp's frame from its
  value through `NumValues` and `NumPositions`; a dial's or pointer's the
  value's fraction of `ScaleRange` times the part's last frame. Other cabs
  (a followed AI train's) rest.

### 3. Picking and the mouse

- **A shape option keeping the cab parts' triangles** (parts named
  `TYPE:order`), positions and texture coordinates, in both MSTS
  backends; a part's triangles and posed matrix asked by index.
- **A ray** through the mouse: each interactive part's sphere first, then
  its triangles (a ray-triangle test giving the distance and the point);
  the nearest hit; hidden when farther than the depth under the mouse.
- **A left press on a part:** a switch's command (`cab.<control>` with
  "press"; its release "release"), turned by `tsre/cabview.lua` into the
  loco's command (the panel's and the keys' names); a lever dragged along
  its path on the screen (its pivot at the first and last key frames,
  projected), sending its value; the wheel steps it a notch. The camera
  keeps the rest; a button held on a control is not turning.

### 4. Hover names

- The mouse at rest on an interactive part for a moment: the control's
  name by the mouse, gone when it moves. The `.cvf`'s `ORTSLabel`, else a
  translated name for its type (`ui.cab.<type>`).
- Drawn with a `MapLabelAtlas` and one quad in the UI pass.

### Later in this task

Night (the cab light overlays following `ORTS_CABLIGHT`, the half-bright
material), the outside parts (wipers, windows, mirrors, doors).

## Steps

1. [x] The `.cvf` reader and the parts: `CabView`, `CabParts`, found as
   Open Rails finds it; suite `cabview` on fixtures; a report over
   OR_PSK's 3D cabs (as the review counted them).
2. [ ] Values and posing: `tsre/cabview.lua`, `DrivenFrame::cab`, the cab
   posed in its own state; checks; a capture of the 183's needles and
   levers following the loco.
3. [ ] Picking and the mouse: the kept triangles, the ray, switches by a
   click, levers dragged and by the wheel; checks; driven under Xvfb.
4. [ ] Hover names: the label; checks.
5. [ ] End to end on both backends (OR_PSK's 183, 749, EP09, Elf 1);
   the doc.

## Step 1 done (2026-10-30)

- **`CabView`** (`src/tsre/trains/CabView.h/.cpp`): a `.cvf` read with
  `SimisDocument`, its controls (`CabViewControls`, then Open Rails'
  `ORTSCabviewControls`) in file order: kind, type and its style word,
  order (the nth of its type), `Position`, `ScaleRange`, `Units`, `Style`,
  `NumFrames`, `NumPositions` and `NumValues` as written (their counts
  left out; Open Rails' fixing of these tables comes with the posing),
  `Orientation`, `DirIncrease`, `MouseControl`, `ORTSLabel`, `Graphic`. A
  control without a `Type` is a problem and left out. `find(type, order)`.
- **`VehicleDefinition`** finds the 3D cab's `.cvf` as Open Rails does
  (beside the shape by its name, else CabView in `CABVIEW3D`, else in
  `CABVIEW`) and reads it with the definition: `cab3DViewPath`,
  `cab3DView`.
- **`CabParts`**: part names `TYPE:order[:p1[:p2]][-subpart]` (any case)
  matched to controls: each control's parts and last key frame, the first
  part's parameters; the outside parts apart; names no control has
  listed.
- **Suite `cabview` (14):** the controls read (kinds, types, orders, keys
  in any case, tables as written, the label, a control without a type),
  the three places in order and a shared shape through a `CABVIEW3D` that
  does not exist, the parts matched.
- **Suite `cabview-corpus`** (`--game-root`), on OR_PSK: 128 ENGs with a
  3D cab (more than the review's 116: TSRE follows includes), 118 with
  their `.cvf` read; 21 cabs (77 ENGs) with parts matched to controls,
  each of the review's 18 alike (the same counts and the same names no
  control has); 3 problems in their `.cvf` files. New beside the review:
  the EP05's and EU05's `OeBB_1010HGI_Fst.s` matches 9 of its 19 controls
  (its brake gauges, bell and other parts named but not in the `.cvf`
  found).
- Unchanged: vehicle-file 40, cab3d 28, driving 67, keys 15, ctest 11/11.
