# Task 02 - Displays

Status: design (2026-11-01); steps 1 and 2 done on `feature/displays`, not
merged.

The user (2026-11-01): a new feature set, displays. RmlUi documents drawn
into textures on shapes: station displays first (departure boards, clocks),
then trains and cabs. Defined in the shape's `.sd`: where the display sits,
its size, its document, its surface (glass, paper), its refresh rate and its
input data (the time for a clock, a station's or a platform's timetable,
the train or engine). Route displays share a texture where they can (the
same document and the same station or platform), and a display finds its
platform from the nearest platform line.

## TSRE today

- **RmlUi draws only into the frame.** A context renders into a
  `UiDrawList`, which the view hands to its renderer (`Renderer::drawUi`,
  both renderers) over the finished frame. There is no texture target: task
  01's decision 7 left drawing into a texture "for later, with the cab
  displays (the same render interface, another target)".
- **One Lua state for every document.** RmlUi's Lua plugin keeps a single
  global state (`g_L`, `thirdparty/rmlui/Source/Lua/LuaPlugin.cpp`); TSRE
  gives it the UI state, so documents share its globals (`hudUpdate`,
  `locoUpdate`). Data models filled from C++ need no Lua: the HUD's `train`
  model (`UiHud.cpp`, `{{speed}}`). The plugin's leak (a table entry for
  every object handed to Lua) is fixed (2026-10-31, `thirdparty/rmlui/
  README.md`).
- **Shapes:** each placed object has a state of its own (its parts' poses);
  a render item's texture is a TexLib id or a renderer-owned texture
  (`RenderItem::Material::textureObject`). The queue's instances
  (`QueueRenderer::DrawInstance`) carry a matrix, not a material: an
  instance cannot yet show its own texture.
- **The `.sd`** is read as an STF document (`SFileDocument`); TSRE already
  adds lines of its own to it (`ESD_TSREMaterial`, task shapes 13).
- **Platforms and timetables:** the TDB's platform items carry
  `stationName` and `platformName` at their track positions. The
  simulation's plans have each stop's station, platform, platform item and
  booked arrival and departure (`StopPlan`, activities and Open Rails
  timetables alike); a running train's state is in each `SimulationFrame`.
- **Open Rails** has no displays on route objects (their screens are baked
  textures). Its analog clocks turn hands found by part name
  (`ORTS_HHand_Clock`, `ORTS_MHand_Clock`, `ORTS_SHand_Clock`,
  `ORTS_CHand_Clock`, four key frames at 12, 3, 6 and 9; manual 16.18,
  "Animated clocks"; route lists such as `clocks.dat` are no longer used).

## Test content (the user's, OR_PSK's Prudnik)

The user may change OR_PSK for this task: `DISPLAYS` folders, the shapes'
`.sd` files.

- **`wyswietlacz_zegar_wiszacy_1.s`, `_2.s` (`_3.s`):** a hanging platform
  display with a clock, a screen on each side, empty. Each screen is a
  quad (two triangles) in full bright (-8). Side 1's screen is part of a
  texture shared with the housing and the hands
  (`wyswietlacz_peron_zegar_1_N.dds`); side 2's texture is the screen
  alone (`wyswietlacz_peron_zegar_2_N.dds`): **side 2 tests texture
  replacement, side 1 the square**, on one shape. The hands are Open
  Rails' clock parts (`ORTS_*Hand_Clock01`, `02`). The platforms here serve
  no timetable train (the user).
  - Two displays, left and right platform:
    `core.startup.camera=-5373,14874,760.61,334.57,-690.67,179.07,1.49`
  - Single display:
    `core.startup.camera=-5373,14874,760.88,334.31,-723.48,186.54,24.83`
- **`wyswietlacz_peron_zegar_wiszacy.s`:** the same housing, its texture
  with an example state baked in; placed several times close to the
  platform line (sharing).
  - Two displays, left and right platform:
    `core.startup.camera=-5373,14874,785.52,334.60,-820.80,176.26,0.56`
  - Single display:
    `core.startup.camera=-5373,14874,784.22,335.16,-818.54,183.73,-7.85`
- **`ORB_Tpo4.s`:** a station's timetable board, departures
  (`ORB_Tpo_ODJAZDY.ACE`) and arrivals (`ORB_Tpo_PRZYJAZDY.ACE`), each a
  quad with a texture of its own: texture replacement, paper.
  - `core.startup.camera=-5373,14874,781.90,333.26,-897.76,88.50,1.49`

The camera strings were taken on the user's machine; they should match
here. Prudnik runs at about 1 frame a second in this machine's editor:
displays are made in the Shape Viewer first, then checked at these places
through the capture harness.

## Design (the user's decisions, 2026-11-01)

### Where a display is defined
- **In the shape's `.sd`**, one block per display (a TSRE line, as
  `ESD_TSREMaterial`). Two kinds:
  - **Texture replacement:** the display takes one of the shape's textures
    whole, named by its image.
  - **Square:** a part TSRE makes (two triangles), anchored to a matrix by
    name, so it moves with the hierarchy (doors, animations): a position,
    a rotation and a size in that matrix's space, drawn a little in front
    of what it covers.
  - No UV areas (the user: an overcomplicated mess; new triangles would be
    needed anyway).
- **Per placed object** (the user, 2026-11-01): a square is a mesh of
  its own, drawn with its display's texture. Texture replacement goes
  through the shape's state: a placed object's state (animated shapes
  have one each) also carries texture overrides, a generic one (an image
  of the shape shown with another texture). Open Rails' texture overrides
  in the ENG will need the same later. A placed object with a display gets
  a state of its own, as an animated one does.
- **Trains:** in the cab's or the car's shape's `.sd`, not in the ENG or
  the CVF. The legacy CVF screens and digits come later, read into the
  same model.
- **glTF:** a TSRE extension, on a node (a square) or on a material
  (texture replacement), with the same fields.

### A display's fields
- **Document:** an `.rml` file, found in `ROUTES/<route>/DISPLAYS` for
  route objects and `TRAINS/TRAINSET/<dir>/DISPLAYS` for trains.
- **Resolution** in pixels (the document's); for a square, its size in
  metres too.
- **Surface:** glass (a screen behind glass, its own light: shown as
  painted at any hour, with a reflection) or paper (lit by the scene only,
  dark at night unless a lamp reaches it).
- **Refresh rate;** 0 draws it once (a sign).
- **Inputs:** the simulation's clock; a station's or a platform's
  departures and arrivals; later the train or the car (destination, next
  stop) and the cab (its values).

### Data
- **Data models filled by TSRE:** a clock or a board needs no Lua. Lua is
  optional, in the one UI state for now; a state or an environment per
  document would need RmlUi's plugin changed (later, if needed).
- **Looks are the document's:** an LED dot matrix or split-flap is the
  author's choice of style, not a TSRE feature.

### Binding and sharing
- **The platform:** found on the shape's first render frame (as other
  objects set themselves up when they load): the nearest platform line
  from the TDB's platform items. Nothing is stored. A manual link, if it
  comes, is an attribute of the W file's StaticObj (TSRE uses no side
  files).
- **Sharing:** displays with the same document, inputs and resolution
  share one texture, drawn while at least one of them is in view.

### The `.sd` block (proposed, used for the tests; to approve later)
One block per display, a named sub-block per value (the user chose this
over positional values, 2026-11-01):

```
ESD_TSREDisplay (
    Document ( "platform_board.rml" )
    Image ( "wyswietlacz_peron_zegar_2_1.dds" )
    Surface ( Glass )
    RefreshRate ( 1 )
    Input ( Platform )
)
ESD_TSREDisplay (
    Document ( "platform_board.rml" )
    ShapeHierarchy ( "MAIN" )
    Position ( -0.207 0.414 0.001 )
    Rotation ( 8.4 -90 0 )
    Size ( 1.389 0.670 0 )
    Resolution ( 512 256 )
    Surface ( Glass )
    RefreshRate ( 1 )
    Input ( Platform )
)
```
- **The kind follows from what is there:** `Image` replaces a texture;
  `Position` and `Size` make a square.
- **`Document`:** the `.rml` file, in the route's or the trainset's
  `DISPLAYS` folder.
- **`Image`:** the texture replaced, as the shape's `images` name it.
- **`ShapeHierarchy`:** the matrix a square hangs on (optional, the
  shape's root by default), as Open Rails' ENG lights name the part they
  follow.
- **`Position`:** the square's centre, metres, in that matrix's space.
- **`Rotation`:** degrees about x, then y, then z (optional). The square
  lies in the matrix's x-y plane facing +z before it. `QDirection` (a
  quaternion, as the W file's) may be taken instead later, if a placing
  tool writes it.
- **`Size`:** width, height and depth in metres, as a car's size in the
  ENG; a square's depth is 0.
- The square example is side 1 of `wyswietlacz_zegar_wiszacy_1`: its
  screen faces -x, tipped 8.4 degrees down, centred at (-0.197, 0.415,
  0.001); the square 1 cm in front of it.
- **`Resolution`:** pixels. Optional with `Image` (the replaced texture's
  size).
- **`Surface`:** `Glass` or `Paper`.
- **`RefreshRate`:** redraws a second; 0 draws it once.
- **`Input`:** `Clock`, `Platform` (the nearest platform's departures and
  arrivals, and the clock) or `Station` (all its platforms'); more later
  (`Train`, `Cab`).
- The glTF extension carries the same fields.
- **Names and the binary token table:** a name MSTS's token table already
  has keeps that token's values (`TS.cpp`): `Image` one string (the
  shape's `image`), `Position` three numbers (the W file's), `Size` three
  (the ENG's), `QDirection` four (the W file's). `Texture`, `Matrix` and
  `Hierarchy` are the shape file's own with other values, so not used.
  `ESD_TSREDisplay`, `Document`, `Rotation`, `Resolution`, `Surface`,
  `RefreshRate`, `Input` and `ShapeHierarchy` (Open Rails', text only) are
  not in the table: as `ESD_TSREMaterial`, a text `.sd` only for now.
  Binary support may come (the user): the names are chosen so that TSRE's
  own tokens can be added to the table later with these values.

### Not in this task
- Open Rails' analog clock hands: a task of its own, if at all (RmlUi
  clocks are needed anyway: hands cover only plain analog clocks).
- Built-in example documents: a later step (one document will not fit
  every country).
- Cab displays (CVF screens, ETCS) and touch (a click on a display).

## Steps

1. **RmlUi into a texture:** an offscreen target in both renderers; a
   context per display, its draw list drawn into a texture the queue can
   use (`textureObject`).
2. **Texture replacement in the Shape Viewer:** the `.sd` block read (both
   shape backends); texture overrides in the shape's state; `ORB_Tpo4`'s
   boards and side 2 of `wyswietlacz_zegar_wiszacy_1` showing documents
   with sample data (a clock, a fixed list of departures).
3. **Squares:** a mesh of its own, anchored to its matrix; side 1 of the
   same shape.
4. **Route objects:** a state per placed object with a display; the
   platform found on the first frame; sharing; departures and arrivals
   from the simulation; Prudnik at the user's camera places through the
   capture harness.
5. **Surfaces:** glass and paper under auto exposure, by day and by night;
   refresh rates and the in-view budget.
6. **Examples:** documents in OR_PSK's `DISPLAYS` folders; the doc.

## Step 1 done (2026-11-01)

The user's go: "commit and then start work on a new branch".
- **`Renderer::drawUiTexture` / `releaseUiTexture`:** a texture drawn
  from a UI draw list, its top row the texture's first (as an image's),
  mipmapped; a backend texture object for
  `RenderItem::Material::textureObject`.
  - OpenGL: drawn at once through a framebuffer of its own, by a
    `GlUiDrawer` of its own drawing the top row first (projection and
    scissors flipped); then its mipmaps.
  - QRhi: `RhiTextures::createTarget` (a render-target texture in the
    registry); per texture a render target, a pass descriptor and an
    `RhiUiDrawer` (a drawer's uniforms hold one list a frame). The lists
    are drawn when the next frame begins (`beginFrameIfNeeded`), before
    its passes, then their mipmaps. The drawer flips only where the
    framebuffer's y goes up (QRhi on OpenGL).
- **`Ui::Display`:** an RmlUi context of its own with its own render
  interface; `load`, `update` (under the UI state's deadline), `render`
  (its draw list), `draw(renderer)`, `release`.
- **Suite `displays`** (7 checks): the OpenGL renderer offscreen (the top
  row first, the mipmaps, a data binding's change drawn again into the
  same texture, the texture freed); the QRhi drawer into a render-target
  texture on Vulkan (under Xvfb) and on QRhi's OpenGL
  (`--set core.rendering.rhiApi=opengl`).
- Not exercised yet: the QRhi renderer's own queue (`drawUiTextures`):
  step 2's Shape Viewer on QRhi.
- **Found:** QRhi on OpenGL with the app's default surface format (no
  profile) gets a compatibility context and looks for GLSL 120; TSRE bakes
  GLSL 330 only (`RhiContext`, `bakeInline`). The suite asks for a core
  profile; the app's own QRhi-on-OpenGL path is not checked here.

## Step 2 done (2026-11-01)

- **The `.sd` blocks** (`ShapeDisplays::read`, as `SFileLegacy::loadSd`
  reads the file: the header passes, the shape's block is read): the
  proposed syntax; a block without a document, or with neither an image
  nor a square, is left out. Documents' folder
  (`ShapeDisplays::documentsFolder`): `ROUTES/<route>/DISPLAYS`,
  `TRAINS/TRAINSET/<dir>/DISPLAYS`.
- **Texture overrides in the shape's state**
  (`ComplexShape::setTextureOverride`, generic): the legacy backend (the
  drawn shapes' default) draws a state with overrides with its own copies
  of the shared packets that take the image (submitted one by one, not
  instanced), and gives its posed packets the texture; a changed override
  makes the state's packets again. The complex backend: not yet.
- **`Ui::ShapeDisplays`:** a shape's displays for one state: a
  `Ui::Display` each, the data model `display` (`time`, `date`,
  `station`, `platform`, `departures`, `arrivals`: rows of `time`,
  `train`, `destination`, `origin`, `platform`, `delay`) filled before the
  document loads and when due (`RefreshRate`; 0 once), the texture given
  to the state.
- **Display textures repeat**, as a shape's textures do: MSTS coordinates
  run past 0 to 1 (`ORB_Tpo4`'s v from -1 to 0 sampled one row when
  clamped).
- **The OpenGL renderer puts back the bindings the UI drawer changes**
  (vertex array, program, texture unit 0, array buffer): a display drawn
  before the scene left the scene's next draws a `GL_INVALID_OPERATION`.
- **Shape Viewer:** the shown shape's displays with sample data (Prudnik,
  platform 1, three departures, two arrivals; the clock now), drawn again
  every 250 ms.
- **OR_PSK** (the user's permission): `ROUTES/PRUDNIK/DISPLAYS`
  (`peron_zegar.rml`, `odjazdy.rml`, `przyjazdy.rml`: each the original
  texture as its background, the live part where the screen or poster
  is); two blocks added to `ORB_Tpo4.sd`; `wyswietlacz_zegar_wiszacy_1.sd`
  new (side 2's texture replaced). With texture replacement a document is
  laid out as the texture is: the clock display's screen sits at 15.6 %
  from the left and 9.2 % from the top of its 2048 x 2048 texture.
- **Seen:** Shape Viewer captures on OpenGL and on QRhi (Vulkan): both
  boards' lists and the clock display's side 2 live; side 1 still baked
  (its square is step 3).
- **Suite `displays`** (9 checks): the `.sd` blocks (both MSTS headers)
  and the folders added.

## Open
- The `.sd` block's syntax: proposed above, to approve or improve once
  the tests run.
- The user has other ideas for per-object textures; the shape's state is
  the way for now.
- The complex shape backend's texture overrides; a packet-level check of
  the overrides (seen in captures only so far).
