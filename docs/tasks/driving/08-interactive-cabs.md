# Task 08 - Interactive cabs

Status: steps 1 to 5 done 2026-10-30 on `feature/interactive-cabs`, not
merged; night, outside parts and hidden controls later (at the end).

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
2. [x] Values and posing: `tsre/cabview.lua`, `DrivenFrame::cab`, the cab
   posed in its own state; checks; a capture of the 183's needles and
   levers following the loco.
3. [x] Picking and the mouse: the kept triangles, the ray, switches by a
   click, levers dragged and by the wheel; checks; driven under Xvfb.
4. [x] Hover names: the label; checks.
5. [x] End to end on both backends (OR_PSK's 183, 749, EP09, Elf 1);
   the doc.
6. [x] The user's go after step 5 (2026-10-31): the combined handle, the
   bell, sander, wipers and cab radio in the scripts; the cab's outside
   wipers.
7. [x] The user's go after step 6 (2026-10-31): AI wipers in rain or snow,
   mirrors, the driven train's doors, the cab's windows.

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

## Step 2 done (2026-10-30)

- **A value's frame** (`CabView::frame`), as Open Rails poses a 3D cab's
  part: the tables Open Rails makes from `NumFrames`, `NumPositions` and
  `NumValues` (each block as it comes, in file order; for a control with a
  graphic the missing positions and values filled, the values sorted,
  reversed when written descending), a multi-state display's from its
  states' `SwitchVal`; then its type's rule (`GetDrawIndex`): a lever's
  nearest value (a per cent taken as a fraction), a switch's state as the
  frame, buttons, the few odd displays; through the positions where they
  pair with the values; at most the last frame. A dial's or a gauge's: the
  value's share of `ScaleRange` times the part's last key frame. A type
  with no rule keeps its frame, as in Open Rails. Not kept: the dynamic
  brake's notch and the combined handle's split (by their values for now).
- **`tsre/cabview.lua`**: each type's value in the control's units from the
  loco's values and the train's speed (Open Rails' `GetDataOf`): speed in
  km/h or mph; pressures in psi, bar, kPa or kg/cm²; the levers as
  fractions; the direction 0, 1, 2; the horn, lights, pantographs (the
  four-position switch as Open Rails numbers it), breaker, master key and
  battery as states; wheelslip, alerter and reset off until modelled. A
  script sharing the module adds or replaces types.
- **The host** asks the driven cab's model for them when it publishes a
  frame (`LuaLoco::cabValues`, `SimulationCore::cabValues`):
  `DrivenFrame::cabControls`, a number per control (NaN none). The script
  calling the module and each `.cvf`'s controls table are made once.
- **The cab posed:** the controller gives the cab vehicle's Eng its values
  each frame (`setCabControls`); the Eng draws its cab in a state of its own
  (`poseCab`), each control's parts set to its frame when it changes.
- **Suite `cabview` (20):** a lever's notch, Open Rails' own example of
  filled tables (the Acela's train brake), switches, a reversed tri-state,
  dials, a display's states, digits not in frames; an electric loco's
  values through `tsre.cabview` (36 km/h, 22.37 mph, throttle 0.5,
  direction 2, the main reservoir in bar, the four-position pantograph 1,
  the master key, a cab radio without a rule left out) and a type added by
  a script.
- **The corpus** reports frames a `.cvf` can pick past a part's keys: the
  Škoda cabs' brake levers (12 frames against 9 keys, 18 against 16), the
  EP05's engine brake; their parts stop at their last key, as in Open
  Rails.
- **Capture** (`tests/renderer/cab-interactive.json`, OpenGL): PRUDNIK's
  PSK_2026 timetable to 12:34, train 92 (1232_IC5610, EP09-039) driven
  and ridden in its 3D cab at 75 km/h. Throttle 0.6 then 0 with the train
  brake at 0.7: the controller's wheel turns back, the brake handle turns
  to applied, the brake pipe's needle falls; the speedometer at 75 and
  71 km/h.
- Unchanged: cab3d 28, driving 67, keys 15, car-animation 15,
  shape-animation 8, steam 11, heating 10, script 32, simulation-core 89,
  simulation-host 19, simulation-ui 80, sound 95, ctest 11/11.

## Step 3 done (2026-10-30)

- **The parts' triangles:** the drawn shape keeps none on the CPU (both
  MSTS backends free them), so the cab's shape is read again on the CPU
  (`SFileComplex::loadData`), on a worker when the cab is first posed:
  `SFileComplex::partTriangles` gives a part's first-level triangles and
  texture coordinates. Only the parts of the controls the mouse works
  (levers, two and three state switches, multi-state switches, combined
  handles), a sphere each (`CabPicker`). Read in 15 to 210 ms (Release;
  the EP09's 212 ms, the Elf 1's 182 ms), hence the worker.
- **The ray:** through the mouse from the view's projection and camera (as
  the frame, without TAA's jitter), against each part placed by the cab's
  placement (kept when the Eng draws its cab) and its posed matrix:
  spheres first, then the triangles (`Intersections::rayTriangle`, giving
  the distance and the point's barycentric coordinates;
  `Intersections::raySphere`), the nearest kept with its texture
  coordinate (for screens later). Hidden controls behind non-interactive
  geometry are not yet excluded by the depth.
- **The mouse** (`RouteEditorGLWidget`, while the train takes the keys and
  the camera rides its 3D cab): a left press on a part works it and keeps
  the mouse (no camera turning, W/A/S/D stay the train's). Two state
  switches and buttons: "press" and, at the release, "release". Levers,
  three state and multi-state switches: dragged along their path on the
  screen, the frame nearest the mouse sent as its value
  (`CabView::valueAt`, the frame rule backwards) when it changes. The
  path: of the picked point and the ends of the part's sphere along its
  axes, the one going farthest on the screen over the frames the control
  can show (a handle's end, not its pivot), moved to start at the press
  at the frame the control shows. Found on the first run: a press near a
  handle's pivot gave a path of 25 pixels, so the first move took the
  brake to full. The wheel over a lever or a switch: a notch or a state,
  "+" or "-". A grab is let go when the view loses the focus or the
  train its keys.
- **Commands:** "cab.<control>" goes to the driven cab's model; it asks
  `tsre/cabview.lua` (`M.actions`) for the loco's own commands and runs
  them through its script: levers by value or a notch, the direction and
  headlights by a step, horn and bail-off held, pantographs, master key and
  emergency toggled, the four-position pantograph switch, the breaker's
  orders, the battery switch.
- **`TSRE_CAB_LOG`:** each press's control, its drag path and every
  command sent.
- **Captures** (`tests/renderer/cab-mouse.json`, mouse actions added to the
  capture suite: press, move, release, wheel at image pixels after the
  view settles, and "list", the interactive parts on the screen): the
  EP09 at PRUDNIK. The train brake handle dragged 210 pixels: 0.05, 0.11,
  0.19, 0.28 sent, the handle turned and the brake pipe's needle falling;
  the controller wheel three notches up by the wheel, the wheel turned;
  the breaker's closing button pressed and released. The direction
  switch's middle (the list's point) lies off its triangles, so the wheel
  there met nothing.
- **Suite `cabview` (22):** the ray (nearest part, texture coordinate, a
  part left out, a miss); the mouse's actions as commands on an electric
  loco (throttle 0.8, the pantograph switch to both, the direction a step
  back, the master key). **Corpus:** each cab's parts for the mouse and how
  long they take; a part's posed matrix is its parent's times its own at
  that frame on both backends (1,700 checks); most controls met first
  from the driver's head.
- Unchanged: keys 15, driving 67, cab3d 28, editor-tools 38, map-view 48,
  rmlui 40, simulation-ui 80, ctest 11/11.

## Step 4 done (2026-10-30)

- **When:** the view's timer (15 ms) asks the cab once the mouse has
  rested 0.4 s on the view with no control held: one ray; a moved mouse or
  a press takes the name away and asks again at the next rest. While the
  parts are still being read on their worker the cab is asked again.
- **The name** (`CabView::name`): the `.cvf`'s `ORTSLabel`; else the
  driving panel's name for the type where it has one (throttle, train,
  engine and dynamic brakes, direction, lights, horn, emergency, bail off,
  battery, pantograph 1, pantographs, engine, steam's controls); else a
  name of the cab's (`ui.cab.*`, 19 new, English and Polish: master key,
  pantographs 2 to 4, the breaker's close, open and authorization, bell,
  sander, wipers, cab light, cab radio, alerter reset, train supply, the
  doors, the combined handle, regulator, reverser); else the type made
  readable (`ORTS_CIRCUIT_BREAKER_STATE`: "Circuit breaker state").
- **Drawn** as the map's labels are: the name painted once by QPainter
  into a `MapLabelAtlas` page (white, a dark halo), one textured quad below
  and right of the mouse, kept in the view, in the 3D view's UI pass in
  pixels after the compass.
- **Suite `cabview` (23):** the names. **Capture**
  (`tests/renderer/cab-hover.json`; the capture suite's "wait" action lets
  the view's timer run): the EP09's train brake handle "Train brake", the
  breaker's closing button "Breaker: close", nothing once moved away.
- Unchanged: ctest 11/11 (the translations' validation with 3,447 ids).

## Step 5 done (2026-10-30)

- **Both backends**, PRUDNIK's EP09 at 12:34 (train 92, driven): on
  OpenGL the three captures (`cab-interactive`, `cab-mouse`,
  `cab-hover`); on QRhi (Vulkan on lavapipe, under `xvfb-run`) one run of
  each kind: the cab posed from the loco's values (the controller wheel
  at 0.6), the brake handle dragged (0.05, 0.11, 0.19, 0.28 sent, the
  handle turned, the brake pipe's needle falling), "Breaker: close" by
  the resting mouse. The same on both.
- **Suites:** cabview 23; unchanged: keys 15, driving 67, cab3d 28,
  editor-tools 38, map-view 48, rmlui 40, simulation-ui 80, settings 297,
  units 18, steam 11, heating 10, script 32, simulation-core 89,
  simulation-host 19, sound 95, car-animation 15, shape-animation 8,
  vehicle-file 40, ctest 11/11.

## Step 6 done (2026-10-31)

After the user asked what could be done now with the combined handles and
the unmodelled switches, and said proceed.

- **The combined handle** (`tsre/controls.lua`), as Open Rails'
  `Combined_Control ( min max split default throttle dynamic|train )`: the
  split as a share of the travel (Open Rails reads the third number as the
  split; the ED78 and EN75 write a step there, 0.05 and 0.125, and get that
  split, as in Open Rails). Both kinds Open Rails moves: throttle and
  dynamic brake (the Elf 1, Flirt 3 and most of OR_PSK's 46 files), throttle
  and train brake (the CD 843 and 854). A missing dynamic brake lever made
  as Open Rails makes it.
  - Its position (`combined`, Open Rails' `GetCombinedHandleValue`): split
    × (1 − throttle) powering, split + (1 − split) × brake braking.
  - The `combined` command (`SetCombinedHandleValue`): its brake moved
    while on, or from a shut throttle past the split, else the throttle;
    "+" and "-" a step toward braking or power (the wheel). The lever it
    moved goes on to the other units.
  - The throttle's keys move the whole handle (`StartThrottleIncrease`,
    `StartThrottleDecrease`): up eases the brake first, down from a shut
    throttle applies it; let go, both stop. With a throttle and train brake
    handle the train brake applied shuts the throttle
    (`StartTrainBrakeIncrease`), sent on to the units.
  - **The split's frame** (`CabView::frame` with the ENG's split,
    `VehicleDefinition::combinedSplit`): Open Rails' rule, the split's
    frame shown only at the split, a frame off it on either side. Applied
    only where the `.cvf` has a value table: without one (the Elf 1's and
    Flirt 3's, `Graphic ( "" )`) Open Rails' split index comes out -1 and
    its handle shows frame 0 for neutral and every power position (read in
    its code, upstream 8aafb3147, since `fb1f454c7`, 2024); not copied,
    those handles go by their value.
- **The bell, sander, wipers and cab radio** (`tsre/controls.lua`): a
  switch each, on, off or "toggle", Open Rails' sounds (bell 10/11, sander
  4/5, wipers 6/7, the radio's 162/163, which no 3D cab's sound file in
  OR_PSK has); published as `bell`, `sander`, `wipers`, `cabRadio`. The
  sander goes on to the other units (Open Rails sands the whole train);
  its grip waits for wheel slip, its air and sand with it.
- **`tsre/cabview.lua`:** `CP_HANDLE` and `CPH_DISPLAY` from the position;
  `BELL`, `SANDERS`, `SANDING`, `WIPERS`, `CAB_RADIO` lamps and switches.
  A switch works as its Style says (Open Rails' `ChangedValue`): `ONOFF`
  the other way at each click, else on while held. The controls' table
  gives the style.
- **The wipers' parts:** the controller sets the driven car's wipers from
  its value (`CarAnimation`'s wipers group, until now only the Shape
  Viewer's); the 3D cab's `EXTERNALWIPERS` parts sweep with them
  (`Eng::poseCab`), at the cab shape's rate. Both by Open Rails' loop
  (`CarAnimation::sweep`, `AnimatedPart.UpdateLoop`): off, a sweep is
  finished before they rest (the outside shape's snapped back before).
- **Keys** (Open Rails' defaults): B the bell while held, Shift+B a
  switch; X the sander while held, Shift+X a switch; V the wipers; Alt+R
  the cab radio. **The panel:** a row of the four, lit while on; their
  keys in the Keys list. Names: `ui.loco.bell`, `ui.loco.sander`,
  `ui.loco.wipers`, `ui.loco.cab.radio` (the cab's hover names now use
  them), Polish texts kept.
- **Suites:** cabview 30 (the split's frame with and without a table; the
  handle, its keys and a throttle and train brake handle through
  `electric.lua`; the switches by their Style, their sounds, the sander on
  the line), keys 16 (the new keys), car-animation 16 (the sweep);
  unchanged: cab3d 28, shape-animation 8, driving 67, steam 11, heating 10,
  script 32, vehicle-file 40, sound 95, simulation-core 89, simulation-host
  19, simulation-ui 80, rmlui 40, editor-tools 38, map-view 48, settings
  297, units 18, ctest 11/11.
- **End to end** (OpenGL, `tests/renderer/cab-combined.json`): PRUDNIK's
  Flirt 3 (train 172, 1158_TLK, ED160-013) at 12:34, driven and ridden in
  its cab: the handle dragged from neutral into power (0.42 to 0.14 sent),
  then back through neutral into the dynamic brake (0.28 to 0.78), the
  handle posed each time; the bell and the sander clicked. The Flirt's
  outside shapes have no wiper parts. **The 3D cab's wipers:** no OR_PSK
  consist runs a loco whose 3D cab has them, so a scratch view of OR_PSK
  (symlinks, the content untouched) put a ZSSK 751 at the head of train 92:
  its wipers switch clicked, the cab's wipers sweeping in two captures,
  clicked off, at rest again.

## Step 7 done (2026-10-31)

After the user asked what else could be done (doors, windows, mirrors to
test, AI wipers) and said proceed.

- **AI wipers:** a train not driven has its lead locomotive's wipers on
  while it rains or snows (Open Rails' AI trains switch them by the
  weather; here the scene's weather at the camera, as the train lights
  take it). A driven train's follow its driver's switch.
- **Mirrors** (`tsre/controls.lua`): a switch on the loco, Open Rails'
  sounds 107/108, its outside shape's `MIRROR` parts (`CarAnimation`'s
  mirrors group, until now only the Shape Viewer's) and the 3D cab's
  `MIRRORS` parts. The cab's `ORTS_MIRRORS` switch.
- **Doors:** the driven train's doors on the driver's left or right (Open
  Rails' Q and Shift+Q, the cab's `ORTS_LEFTDOOR` and `ORTS_RIGHTDOOR`
  buttons and lamps). The cab's loco keeps them on its own sides
  (`s.doors`) and publishes them on the driver's (`doorsLeft`,
  `doorsRight`); the controller opens those sides on every car the way the
  train runs, as it opens an AI train's at a station, and plays Open
  Rails' DoorOpen and DoorClose (105, 106) on each car whose doors move.
  Open Rails locks doors only from a train control script: they open
  anywhere, as there. A 3D cab's `LEFTDOOR` and `RIGHTDOOR` parts follow its
  car's doors (the other side from a rear cab).
- **The driver's facing:** a new input, `cabFacing` (`VehicleInputs`, 1
  toward the loco's front, -1 toward its rear, the way the train runs),
  maps the driver's sides to the loco's own, so the doors and windows stay
  on their sides when a reversal turns the driver round.
- **Windows:** four on each loco as Open Rails numbers them (left and right,
  front and rear cab); the driver's left or right window is that side of
  the cab driven from (Open Rails' Ctrl+Q and Ctrl+Shift+Q,
  `ToggleWindowLeftCommand`), sounds 261/260. `CarAnimation`'s one windows
  group became four (`WindowLeftFront` ...; the Shape Viewer's Windows Open
  sets all four); the 3D cab's `ORTS_EXTERNAL*WINDOW*` parts move with
  them, a key frame a second. The cab's `ORTS_LEFTWINDOW` and
  `ORTS_RIGHTWINDOW` switches work; their lamps, lit while a window moves
  in Open Rails, are not kept.
- **The cab's outside parts** are posed in one loop (`Eng::poseCab`): the
  wipers sweeping, the mirrors, doors and windows moving a key frame a
  second (`CarAnimation::move`, Open Rails' `UpdateState`), there at once
  when first posed. `TSRE_CAB_LOG` lists them with their last key frames.
- **Keys:** Q, Shift+Q the doors; Ctrl+Q, Ctrl+Shift+Q the windows; Shift+V
  the mirrors. **The panel:** mirrors beside the four switches, a row of the
  doors and windows, lit while open; their keys listed. Names in English
  and Polish (`ui.loco.mirrors`, `ui.loco.doors.*`, `ui.loco.window.*`;
  the cab's hover names use them).
- **Suites:** cabview 31 (doors and windows on the driver's sides facing
  the front and the rear, the mirrors, the cab's buttons, their sounds),
  car-animation 17 (four window groups, the two-state move), keys 17;
  unchanged: the rest of the step 6 list, ctest 11/11.
- **End to end** (OpenGL): PRUDNIK's SA134 railbus (train 61, driven at
  Gliwice, `tests/renderer/cab-mirrors.json`): the driven car's two mirror
  arms swing out, the other car's stay in; the doors open on both cars.
  The Flirt 3 (train 172, `tests/renderer/cab-doors.json`): its cab's left
  doors button opens the third car's left doors. A scratch view of OR_PSK
  with a ZSSK 183 at the head of train 92 and `rain.env` as the route's
  `editor.env` (symlinks and copies, the content untouched): train 92 not
  driven, its 183's cab wipers sweeping in the rain unasked; driven, they
  rest (its driver's switch off) and its cab's side windows slide down when
  opened, the right one only by the right window's key.

## Left (later in this task, or after)

- **Night:** the cab light overlays (`ORTS_CABLIGHT`) need a cab light in
  the scripts; the half-bright material (task renderer 35).
- **Hidden controls:** a control behind non-interactive cab geometry can
  still be picked; the depth under the mouse would leave it out.
- **Wheelslip, alerter, reset:** not modelled; their lamps show off, the
  reset button does nothing. The sander's grip waits for wheel slip; the
  alerter is Open Rails' vigilance (its own task).
- **Sounds through an open window** (Open Rails hears outside sounds louder
  in the cab while a window is open): a sound step.
- **Light conditions** on the horn, bell and doors (`EngLights::Train`'s
  `horn`, `bell`, `leftDoorOpen`, `rightDoorOpen`) are not set by the
  simulation yet.
- **The bell's extras:** `DoesHornTriggerBell`, the `ORTSBELL` parts
  (American content).
- **The dynamic brake's notch** is by value.
- **Digits, screens, steam:** wait for content (the user's).
