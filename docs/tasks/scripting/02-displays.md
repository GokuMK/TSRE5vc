# Task 02 - Displays

Status: steps 1 to 10 done on `feature/displays` (step 10, from the
user's review, 2026-11-02), not merged; for the user's acceptance. The user guide: `docs/guides/displays.md`
(in Polish: `docs/guides/displays.pl.md`).

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
  quad (two triangles) in full bright (-8) covering the whole front: frame,
  blue screen and clock face, a region of a 2048 x 2048 texture (side 1's
  shared with the housing and the hands, `wyswietlacz_peron_zegar_1_N.dds`;
  side 2's used by that quad only, `wyswietlacz_peron_zegar_2_N.dds`). Not
  suited to texture replacement (the user, 2026-11-01): **both sides are
  squares**, the whole front one LCD (in reality too: the blue screen and
  the analog clock are drawn on one large LCD). The hands are Open Rails'
  clock parts (`ORTS_*Hand_Clock01`, `02`).
- **`wyswietlacz_peron_zegar_wiszacy.s`:** the same housing, its texture
  with an example state baked in; placed several times close to the
  platform line (sharing). Its `.sd` takes `_1`'s two squares (step 4).
- **The camera places** (the user's strings; which shapes they show found
  in this machine's W file, `w-005373+014874.w` of 2026-08-08, the other
  way round from the user's list):
  - Two displays, left and right platform, and a single display: four
    `wyswietlacz_peron_zegar_wiszacy` on platform 3 (p3t7, p3t9; it serves
    no timetable train, the user):
    `core.startup.camera=-5373,14874,760.61,334.57,-690.67,179.07,1.49`,
    `core.startup.camera=-5373,14874,760.88,334.31,-723.48,186.54,24.83`
  - Two displays, left and right platform, and a single display:
    `wyswietlacz_zegar_wiszacy_1` (left) and `_2` (right, a departure baked
    in its texture) on platform 2; another pair by the board, `_3` farther:
    `core.startup.camera=-5373,14874,785.52,334.60,-820.80,176.26,0.56`,
    `core.startup.camera=-5373,14874,784.22,335.16,-818.54,183.73,-7.85`
- **`ORB_Tpo4.s`:** a station's timetable board, departures
  (`ORB_Tpo_ODJAZDY.ACE`) and arrivals (`ORB_Tpo_PRZYJAZDY.ACE`), each a
  quad with a texture of its own: texture replacement, paper.
  - `core.startup.camera=-5373,14874,781.90,333.26,-897.76,88.50,1.49`

The camera strings were taken on the user's machine; they should match
here. Prudnik runs at about 1 frame a second in this machine's editor:
displays are made in the Shape Viewer first, then checked at these places
through the capture harness.

**Deployed in OR_PSK** (steps 2 to 10; the user guide shows TSRE's own
copies, `docs/examples/displays`, not OR_PSK, the user 2026-11-02; the
originals of each step's edits in the scratchpad's
`displays/orpsk-backup/<step>`):

| Document | Shape (`.sd` edited or new) | Kind | Input |
| --- | --- | --- | --- |
| `ROUTES/PRUDNIK/DISPLAYS/odjazdy.rml`, `przyjazdy.rml` | `ORB_Tpo4` (departures, arrivals boards) | Texture, paper | Station |
| `ROUTES/PRUDNIK/DISPLAYS/peron_lcd.rml` | `wyswietlacz_zegar_wiszacy_1` (new `.sd`), `wyswietlacz_peron_zegar_wiszacy` | Squares, glass | Platform |
| `TRAINS/TRAINSET/KS_EN75/DISPLAYS/en75_wyswietlacze.rml` | `KS_EN75_A`, `KS_EN75_B` (new `.sd`s) | Texture, glass | Train |
| `TRAINS/TRAINSET/PKP_Z2B-OR/DISPLAYS/z2_drzwi.rml` | `PKPIC_B11mnouz-70082` (body and door leaves; RefreshRate 1 since step 10) | Squares, glass | Train |
| `TRAINS/TRAINSET/PKP_Z2B-OR/DISPLAYS/z2_drzwi_lua.rml` (+ `z2_drzwi.lua`) | `PKPIC_B11mnouz-70085` | Squares, glass, Lua | Train |

Also `ROUTES/PRUDNIK/TEXTURES/rodzaj_pociagu_{fast,regional,aglo}.png`, and
the timetable `ROUTES/PRUDNIK/ACTIVITIES/OPENRAILS/PSK_2026.timetable_or`
(UTF-16): the seven `#tsre.<field>` rows and `#tsre.extra.kind` for the
trains the examples show (24 trains: S1, IR PIAST, TLK SZTYGAR, IC 108,
109, 112; EIP 5402 and RJ 3588 left without, the fallback). The documents
in OR_PSK and in `docs/examples/displays` are the same files: change both.

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
  dark at night unless a lamp reaches it). Optional for a texture
  replaced (below).
- **Refresh rate;** 0 draws it once (a sign).
- **Inputs:** the simulation's clock; a station's or a platform's
  departures and arrivals; later the train or the car (destination, next
  stop) and the cab (its values).

### Texture replacement (the user, 2026-11-01)
- **The whole texture is the document's canvas, no quirks:** nothing of
  the replaced texture is given to the document. A shape whose screen is a
  region of its texture (the clock display above) is a case for a square.
- **Images in documents** are loaded by file path through TexLib (the UI
  render interface: `TexLib::imageSize`, `TexLib::addTex`), as a page
  shows a logo; never read directly. Showing the replaced texture would
  need an `.sd` option of its own (on or off): not now.
- **The surface is optional** (the user, 2026-11-01): without one only the
  texture is replaced, the parts keep their material. With one, it
  replaces the material of every part showing the image, the `.sd`'s PBR
  factors included (they are part of the shape's material). An image is
  not a material: one image can be on parts with different materials,
  and all of them become the display's.
- **The surface is a generic preset, not an MSTS or a PBR material** (the
  user, 2026-11-01): a PBR part stays PBR (glTF is on the roadmap), its
  PBR material set by the preset; an MSTS part takes a light material.
  Reflections and other material properties are left to the renderer's
  work.

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

### Train data: `#tsre.extra.<key>` rows (the user, 2026-11-01)
- **Why:** a board shows a timetable train by its name, and PRUDNIK's
  names are long (`0846_IR46140SZYNDZIELNIA_KK-O`): Open Rails' timetable
  has no field for a train's number, category, operator, proper name or
  relation, only the name (its menu sorts by it, hence the leading time);
  `#comment` and `#briefing` are free text. A review (2026-11-01): of
  PSK_2026's 266 named trains 52 fit one pattern, 154 have no number at
  all, the `#comment` cells are clean for about 62. So no name format:
  users who want more on a display write the data.
- **Rows `#tsre.extra.<key>`,** one per key, any key; a train's cell its
  value. Read into a map per train (`OrTimetable::Train`, then the
  train's plan), written back by the timetable's save (which writes
  TSRE's own layout and drops rows it does not know). TSRE fixes no key:
  documents and scripts use the keys they want; the example documents'
  convention: `operator`, `category`, `number`, `line`, `name`, and
  `origin` and `destination` (the real ones, where the train comes from or
  runs on beyond the route).
- **Spelling:** dots throughout, `#tsre.extra.<key>`: `.` means nothing
  in Open Rails' timetable syntax (`:` is a time's and
  `train:timetable`'s, `/` a row's qualifier, `$ = +` commands), it is the
  path documents and scripts use (`row.extra.destination`), and TSRE's
  settings keys are dotted too. `#tsre.` is TSRE's namespace for any
  later row of its own, `extra` the users' branch in it: users' keys
  never meet TSRE's rows.
- **Keys:** lower case (Open Rails lower-cases a row's first cell),
  letters, digits and `_` (RmlUi's expressions: `speed-limit` would be a
  subtraction), no `/` (both readers cut a row's cell there). **Values:**
  the cell trimmed, a train's continuation columns' cells joined as Open
  Rails joins them; no separator (`;`, `,`) in them.
- **Open Rails:** an unknown `#` row is "Invalid row definition" in its
  log, the row ignored, the timetable runs. Rows only: an unknown `#`
  column shows in its menu as a train and can make the timetable fail to
  list there.
- **Displays:** each departure or arrival row has the train's `extra`; a
  key not set reads as empty text, so a document shows the parts where
  they are and the train's name where not.
- **Scripts:** a train's Lua scripts read its extras (read-only), with
  the trains' displays (step 8).
- **Activities:** MSTS services have no place for them: later, a block
  of their own in the activity (no side files), the activity editing
  task's. The rows are edited in TSRE's timetable window's train panel
  (simulation 08, done 2026-11-02 on this branch).

### Trains (the user, 2026-11-01)
- **A car does not look for its train:** its train gives it, as the
  simulation's controller already gives each car its doors, wipers and
  pantographs (`Eng::setTrain`, the simulation's train). A car with no
  train (standing, the editor's, a viewer's) has none: its documents show
  what they have without one.
- **`Input ( Train )`:** the document's `train`: `name`, `origin` and
  `destination` (its first and last stops' stations), `delay`, `extra`
  (its `#tsre.extra.<key>` data), and where it is on its way (the user,
  2026-11-01: some displays change with it): `state` (`waiting`,
  `standing` at a station, `held` elsewhere, `running`, `approaching`
  within 500 m of its next stop, `finished`; empty without a simulation),
  `at` (the station it stands at), `next` (the next stop it reaches: while
  standing, the one after), `previous` (the station it left last),
  `platform` (at's, else next's), `departs` (booked, from at, or from its
  first stop while waiting), `arrives` (booked, at next), `distance` (m to
  next, where its route part has it). Documents shared by a train's cars
  (the same document and size), drawn by the route's displays (the same
  budget, the same nearest-first order).
- **A square on a moving part** moves with it: it hangs on its matrix as
  the car's state poses it (doors, pantographs).

### Train fields, stops, date, functions (the user, 2026-11-02; step 10)
- **Why:** step 6 put every part of a train's identity in `extra`: my
  call, not the user's (they proposed the generic map so users can add
  their own data; I took it as replacing the fixed rows). The user: the
  important fields are TSRE's own; TSRE's in-game UI will need them for
  sorting and showing trains, and routes differ in which they use
  (a line, a number, a name).
- **TSRE's train fields,** a recommended set, each optional: `category`,
  `number`, `name`, `operator`, `line`, `origin`, `destination`. Rows
  `#tsre.<field>` (flat: the user, 2026-11-02, over `#tsre.train.`),
  read into named fields of the timetable's train and its plan, written
  back by the save; edited in the Timetable window's train panel with
  the extras, and optional columns of the trains list (simulation 08,
  2026-11-02).
- **`extra` stays the users' own:** `#tsre.extra.<key>`; `kind` (the
  pictograms' class) stays there as its example.
- **Documents:** `row.<field>` and `train.<field>` (`row.number`,
  `train.category`), not under `extra`. `origin` and `destination`: the
  timetable's when given, else the first and last stops' stations on the
  route (the user: yes), for rows and trains; a train document can tell
  which from its stops.
- **`train.stops`** (new: the user took train documents to have the stop
  list; they have only `at`, `next` and `previous`): the train's stops on
  the route, each `station`, `platform`, `arrives`, `departs` (booked)
  and `passed` (left already); a station's rows do not need it (the
  user).
- **Date:** `year`, `month`, `day` (numbers), beside `date`, as `hours`,
  `minutes`, `seconds` beside `time` (the user: US and Polish authors
  write dates differently; RmlUi has no date formatting, and Lua for that
  alone would be unwise).
- **Functions for documents** (the user: a middle ground between a plain
  document and Lua; a list kept in `rml-functions.md`): C++ functions on
  each document's data model; the first batch proposed with this step.
- **Examples** (the user): the example documents also in the repo,
  `docs/examples/displays/` (as `docs/examples/track-profiles/`), in
  their content folders (`ROUTES/PRUDNIK/DISPLAYS/…`,
  `TRAINS/TRAINSET/…/DISPLAYS/…`), with the pictograms and a README giving
  each one's `.sd` block (OR_PSK's `.sd` files are not copied); written
  for reading: a CSS rule's properties one a line, classes instead of
  inline styles. OR_PSK's copies the same.

### The `.sd` block (approved by the user, 2026-11-01, step 9)
One block per display, a named sub-block per value (the user chose this
over positional values, 2026-11-01):

```
ESD_TSREDisplay (
    Document ( "odjazdy.rml" )
    Image ( "ORB_Tpo_ODJAZDY.ACE" )
    Resolution ( 512 1024 )
    Surface ( Paper )
    RefreshRate ( 0.2 )
    Input ( Station )
)
ESD_TSREDisplay (
    Document ( "peron_lcd.rml" )
    ShapeHierarchy ( "MAIN" )
    Position ( -0.207 0.414 0.001 )
    Rotation ( 8.4 -90 0 )
    Size ( 1.389 0.670 0 )
    Resolution ( 1040 500 )
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
- **`Surface`:** `Glass` or `Paper`; optional: none keeps a replaced
  texture's parts' material (a square: `Glass`).
- **`RefreshRate`:** redraws a second; 0 draws it once.
- **`Input`:** optional (the clock only; `Clock` read as the same),
  `Platform` (the nearest platform's departures and arrivals, and the
  clock), `Station` (all its platforms'), `Train` (a car's train); `Cab`
  later.
- **`Document`** may name a subfolder of `DISPLAYS`; a path out of it is
  not loaded (and reported).
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
   boards showing documents with sample data (a clock, a fixed list of
   departures).
3. **Squares:** a mesh of its own, anchored to its matrix; both sides of
   `wyswietlacz_zegar_wiszacy_1`.
4. **Route objects:** a state per placed object with a display; the
   platform found on the first frame; sharing; departures and arrivals
   from the simulation; Prudnik at the user's camera places through the
   capture harness.
5. **Surfaces:** glass and paper under auto exposure, by day and by night;
   refresh rates and the in-view budget; the complex shape backend (the
   Shape Viewer's).
6. **Train data:** `#tsre.extra.<key>` rows read and written
   (`OrTimetable`), kept in each train's plan, the boards' rows' `extra`;
   at Zawiercie a board showing a train from its extras and one falling
   back to its name (captures).
7. **PSK_2026's data:** the rows filled for the trains the tests need
   (OR_PSK's timetable, its original kept). Done with step 6.
8. **Trains:** a front display on an EMU and door displays on a coach,
   with the vehicles' animations (doors, the front's hierarchy); the
   train's data as the display's input; the trains' Lua scripts' extras.
   Candidates (a survey of OR_PSK, 2026-11-01):
   - **Front: KS_EN75** (A and B): `EN75_wyswietlacz.ace` (512²) holds
     only displays, three front and three side bands: a texture
     replacement, the document laid out on the whole texture. Its
     `Pantograph*` matrices carry the bands' other quads, a few mm in
     front, shown by the pantograph controls: replaced, every band shows
     the document, so it draws the same in each. Its 4 runs stop at
     Zawiercie. **PKOR_Elf1** (EN76) as a
     square on MAIN (its display texture also covers the display's
     frame), 13 runs at Zawiercie.
   - **Doors: PKP_Z2B-OR** (`B11mnouz-70082`), the animation test (the
     user: a texture replacement is no challenge; some coaches have the
     display on the body by the door, some on the door, realism not
     needed): on the same coach a square on the body (MAIN, over the LED
     panel painted under the end window, both sides) and a square on a
     door leaf (`DOOR_A/B/D/E`, their own matrices with position keys),
     the door opening and closing (the Shape Viewer's animation controls;
     the doors at a platform stop: IC108 and IC109 stop at Zawiercie).
     159A second; 174A and Z1 have no painted display.
9. **Examples (acceptance):** documents in OR_PSK's `DISPLAYS` folders;
   the doc's user part; the `.sd` block's syntax approved. Reviewed with
   the user (2026-11-01):
   - **The `.sd` syntax approved;** `Input` optional (the clock only;
     `Clock` still read); `Document` may name a subfolder of `DISPLAYS`.
   - **Pictures in documents** from the asset's texture folder, as its
     shapes' (and glTF's): a route's `TEXTURES`, a trainset's own folder
     (the user); a display drawn again once its pictures have loaded
     (TexLib loads them in the background).
   - **Messages for authors** (TSRE's errors and messages): a missing
     document, an `Image` the shape has not, an unknown
     `ShapeHierarchy`, a picture not found, an RML error.
   - **Examples:** the boards' rows with a train-type pictogram (the
     user: fast, regional, agglomeration) from `#tsre.extra.kind`
     (`fast`, `regional`, `aglo`): IC fast, TLK and IR regional (the user:
     some variety), KŚ S1 aglo; three PNGs of TSRE's own in
     `PRUDNIK/TEXTURES`. The coach's panel twice: data binding, and Lua
     (its bottom line in Polish, then English), a `displayupdate` event
     on each redraw (as the HUD's `trainupdate`). A note on what each
     example shows.
   - **The user guide** `docs/guides/displays.md` (and its HTML edition);
     `ESD_TSREDisplay` among TSRE's `.sd` extensions
     (`docs/features/msts-shape-file-format.md`).
10. **Train fields, stops, date, functions** (after the user's review of
    step 9, 2026-11-02; done the same day): the seven fields' rows read
    and written, the plan's fields, documents' `row.<field>` and
    `train.<field>`, `origin` and `destination` merged; `train.stops`;
    `year`, `month`, `day`; the functions' first batch
    (`rml-functions.md`); PSK_2026's seven rows renamed (`kind` stays an
    extra; its original kept); the examples in `docs/examples/displays/`,
    formatted, OR_PSK's the same; the guide in both languages; tests.

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

## Step 3 done (2026-11-01)

The user, after step 2: texture replacement is the whole texture as the
document's canvas, no quirks; the clock display needs squares on both
sides (its whole front one LCD, the analog clock drawn by the document).
- **Squares** (`Ui::ShapeDisplays::pushSquares`): a unit square mesh made
  once (VNTA, as MSTS shapes); per display an item of its own (its
  producer's), on its part as the state poses it (`ShapeHierarchy`, by
  name, any case; the root without one), then `Position`, `Rotation`
  (about x, then y, then z) and `Size`; submitted after the shape, under
  its transform. Glass is drawn as a full bright part (its own night
  texture), paper as a normal one; both sides drawn.
- **Left-handed:** a shape's space is MSTS's, left-handed: seen from +z,
  +x is on the left. The square's texture runs from +x (its left) to -x;
  first drawn the other way, its text came out mirrored.
- **Clock hands:** the data model has `hours`, `minutes` and `seconds`
  (numbers); `peron_lcd.rml` turns its hands with
  `data-style-transform`.
- **OR_PSK:** `wyswietlacz_zegar_wiszacy_1.sd` two squares on `MAIN`, 1 cm
  in front of each side's screen (side 1: `Position ( -0.207 0.414 0.001 )
  Rotation ( 8.4 -90 0 )`; side 2: `Position ( 0.211 0.415 0.001 )
  Rotation ( 8.3 90 0 )`; `Size ( 1.389 0.67 0 )`); `peron_lcd.rml` (1040 x
  500: the next departure on a blue panel, an analog clock);
  `odjazdy.rml` and `przyjazdy.rml` draw their whole board face, without
  the original texture (`peron_zegar.rml` gone).
- **Seen:** Shape Viewer on OpenGL and QRhi (Vulkan): both sides of the
  clock display (text the right way round, the clock ticking; the shape's
  own hands hidden behind the square), both boards' faces.

## Step 4 done (2026-11-01)

- **Read with the shape:** `SFileLegacy::loadSd` reads the
  `ESD_TSREDisplay` blocks with the rest of the `.sd`
  (`ShapeDisplays::readBlock`, shared with `ShapeDisplays::read`);
  `ComplexShape::displays()`. Nothing is read while drawing. The complex
  backend has none yet.
- **Documents shared:** `Ui::DisplayDocument` is a document, its data model
  `display` and its texture, drawn when due (its refresh rate counted in
  wall time: the same at any simulation speed). `Ui::ShapeDisplays` is now
  a placed object's part (or the Shape Viewer's): its state's texture
  overrides and its squares, showing documents drawn by their owner.
- **`Ui::RouteDisplays`:** a placed static object (`StaticObj`, each with a
  state of its own already) with displays is bound on its first drawn
  frame, and again when it moves or its shape or state changes:
  - **its platform:** the platform line (the TDB's two platform items)
    nearest where its readers stand, across the ground, within 300 m: 3 m
    in front of a square (it faces +z), the object itself for a texture
    replaced; measured along the line's track (every 5 m, sampled once
    when a display is first near it).
  - **sharing:** one document for the same file, size, refresh rate and
    inputs (Platform: its line; Station: its station; Clock and none:
    everywhere).
  - **drawing:** each frame of the editor's view, before its scene is
    gathered (`RouteEditorGLWidget::paintScene`), the documents seen in
    the frame before are drawn when due; those no object holds any more
    are freed; all freed when the view's renderer goes. (After step 10:
    the textures of those not seen for a while freed too.)
- **The data** (`RouteDisplays::data`), at the simulation's time (its
  frame's), else the world clock's (`WorldClock`), from the simulation's
  plans (activities and Open Rails timetables alike):
  - departures (not at a train's last stop) and arrivals (not where it
    starts), by booked time; a stop is left out once passed (the train's
    `nextStop`, standing at a station), or without a simulation once its
    booked time is a minute gone; a train finished is left out;
  - the train: its service's name (an Open Rails timetable's column,
    `1055_KS_S1_Gliwice_Czestochowa`); origin and destination: the
    stations of its first and last stops (none where that is this stop:
    it comes onto the route or leaves it here); the platform: the stop's
    item's name;
  - the delay, in minutes: the train's now (leaving late where it stands,
    past its next stop's arrival, past its start), a lower bound;
  - a station's display gets no platform name.
- **OR_PSK:** `wyswietlacz_peron_zegar_wiszacy.sd` takes `_1`'s two
  squares (the same housing; the original in the session's scratchpad);
  `odjazdy.rml` and `przyjazdy.rml` lay out each row on two lines (PSK's
  train names are long), up to 11 rows.
- **Seen** at Zawiercie with `PSK_2026` run to 12:30, on QRhi (Vulkan) and
  OpenGL, at the user's camera places and the board's back:
  - the four `wyswietlacz_peron_zegar_wiszacy` on platform 3 share two
    documents (p3t7's, p3t9's): the clock at 12:30, no train;
  - the two `wyswietlacz_zegar_wiszacy_1` share p2t1's: "13:48 Gliwice",
    `1336_KS_S1_Czestochowa-Gliwice`, the clock;
  - `ORB_Tpo4`'s boards: Zawiercie's departures and arrivals. 12:36
    `1013_IR61120` and 13:41 `TLK 62102` are arrivals only: their paths
    end at Zawiercie.
  - The Shape Viewer as in step 3, on both renderers.
- **Suite `displays`** (12 checks offscreen, 14 with a QRhi): the nearest
  platform line along its track and within its reach (two tracks built as
  the TDB editing suite builds them); a platform's and a station's lists
  from plans made in the test, at two times without a simulation and with
  one (a train standing late, one arrived, one gone).

## Step 5 done (2026-11-01)

- **Surfaces** (`ShapeDisplays::applySurface`): `Surface` is optional
  (`Shape`: none given). With one, the parts showing a replaced texture,
  and a square, take its preset in place of their material: an MSTS part
  the full bright (glass) or normal (paper) light material; a PBR part
  stays PBR, unlit (glass: as painted, auto exposure undone, as glTF's
  `KHR_materials_unlit`) or a matte dielectric (paper), on its base colour
  map with its texture coordinates and alpha mode, without its `.sd`
  factors and its maps. Without one only the texture changes. Reflections
  and the rest are left to the renderer's work.
- **The complex backend** (the Shape Viewer window's, and the route view's
  with `TSRE_MSTS_SHAPE_BACKEND=complex`): its displays from its parsed
  `.sd` (`ShapeDisplays::read(node)`, the same fields as the token
  reader's); its texture overrides, a state with any owning its packets
  (the others still share theirs, drawn instanced). The Shape Viewer showed
  none in its window before: its capture suite took the configured
  backend (legacy); `shape-viewer-capture` takes `"backend"`.
- **The redraws a frame** (`core.rendering.displayRedraws`, 2, advanced):
  the documents due in view, at most that many and one of them loaded,
  never drawn first, then the longest overdue, a farther one as if due
  10 ms a metre later (the nearest of its displays); no distance cut-off.
  Timed in Release at Zawiercie (this machine draws with llvmpipe): a
  redraw 0.08 ms on QRhi (its CPU side), 2.4 ms on OpenGL (llvmpipe's
  drawing included); a load (the file read and laid out, the fonts) 1 to
  17 ms on QRhi, 6 to 54 ms on OpenGL.
- **Seen** at Zawiercie at 12:30 and at 22:30 (`PSK_2026` run to each), QRhi
  with auto exposure and AgX on both backends, OpenGL on legacy: the same
  documents and bindings as step 4, legacy and complex alike; by night
  the glass squares as painted, the boards (paper) dark (no lamp reaches
  them); the Shape Viewer on both backends and renderers as in step 3.
- **Tests:** `displays` (16 with a QRhi) checks the parsed `.sd`'s blocks
  against the token reader's and a frame's redraws; `shape-materials` the
  overrides' packets on both backends (a surface's presets, PBR kept, the
  material kept without one, the other states as they were).

## Step 6 done (2026-11-01), with step 7

- **The timetable** (`OrTimetable`): `#tsre.extra.<key>` rows read
  (`extraKeys` in their order; each train's `extra`, its cells not empty,
  a continuation column's joined), the key as Open Rails reads the cell
  (lower case, cut at a '/'); a key that is no key, a key again and a
  `#tsre.` row TSRE does not know reported and skipped. Written after
  #briefing in their order; a copied train keeps them.
- **The train's plan** (`TrainPlan::extra`), **the boards' rows**
  (`DisplayDocument::Row::extra`); in documents `row.extra.<key>`
  (`ExtraDefinition`: RmlUi binds structs by their members, so the map has
  a definition of its own; a key a train has not reads as empty text).
- **Departures, a fix found preparing the captures:** a timetable's train
  booked to leave its last stop on the route (two times: it runs on
  beyond the route) is a departure there. IR 61120 PIAST, TLK 62102
  SZTYGAR and RJ 3588 leave Zawiercie off the route and were missing from
  its departures.
- **OR_PSK** (originals in the scratchpad's backup): `PSK_2026` has seven
  rows (`operator`, `category`, `number`, `line`, `name`, `origin`,
  `destination`) for 24 trains, from their names, `#comment` and
  `#briefing`: the S1 trains (KŚ, line S1, Częstochowa where they come
  from or go to beyond the route; EN75's four runs among them), IR 61120
  and 16120 PIAST, TLK 62102 and 26102 SZTYGAR, IC 112 SILESIA, IC 108 and
  109 ADRIATIC (the Z2B coaches' trains). EIP 5402 and RJ 3588 left
  without, for the fallback. The documents (`odjazdy`, `przyjazdy`,
  `peron_lcd`): the destination or origin from the train's data where it
  has one; the train as its operator (a badge), category, number, line
  and name, else its name.
- **Captures** (Zawiercie at 12:30, QRhi with auto exposure): the boards'
  rows from the trains' data (POLREGIO IR 61120 PIAST to Warszawa Wsch.,
  KŚ S1 to Częstochowa, PKP IC TLK 62102 SZTYGAR to Lublin), EIP 5402 and
  RJ 3588 by their names; the arrivals' origins beyond the route; the
  platform display's next train (KŚ S1 to Gliwice).
- **Open Rails** (the Linux build, `scripts/orts-timetable-run.sh … load`,
  player 0351 S1): the timetable read with one "Invalid row definition"
  warning in its log for each of the seven rows, its trains pre-run to
  03:51, the 3D view reached. TSRE's own check of PRUDNIK finds the same
  11 problems as before.
- **Tests:** `timetable-read` (+2: the rows read, reported, written back
  the same), `timetable-trains` (+1: the plan's extras), `displays` (+3:
  a row's extras, a document drawing `row.extra.<key>` with a key missing
  and set, a timetable's train leaving its last stop).

## Step 8 done (2026-11-01)

- **Cars** (`Eng`): after its shape in its posed state, a car pushes its
  displays (`RouteDisplays::pushCar`; nothing for a shape without any),
  bound again when its train, shape or state change, let go of before its
  state goes (`Eng::dropDisplays`: a released state is another car's
  next). The simulation's controller gives each car its train
  (`Eng::setTrain`) where it gives it its wheels and pantographs.
- **`Input ( Train )`** (`ShapeDisplays`, both `.sd` readers): the
  document's `train` (`RouteDisplays::trainData`: name, origin,
  destination, next and its platform, delay, extra; the train's progress
  shared with the boards, `progressOf`). A train's cars share a document
  (keyed by the train); drawn with the route's, in the same budget. The
  Shape Viewer's documents get a sample train (IC 108 ADRIATIC).
- **OR_PSK** (new files; nothing there before): `KS_EN75_A.sd`,
  `KS_EN75_B.sd` (`EN75_wyswietlacz.ace` replaced, 1024², glass) with
  `KS_EN75/DISPLAYS/en75_wyswietlacze.rml`: the three front and three side
  bands as the shape maps them, each the same (line or category and
  number, destination; on the sides operator, name and next station,
  centred: the window's frame hides the bands' edges).
  `PKPIC_B11mnouz-70082.sd` with `PKP_Z2B-OR/DISPLAYS/z2_drzwi.rml`: four
  squares, 624 x 168, glass: on the body over the painted LED panels
  (MAIN, both sides) and on the door leaves next to them (DOOR_B,
  DOOR_D). 70085 (the other Z2B) has none.
- **Captures:** the Shape Viewer (both backends alike, OpenGL within
  0.02 % of the pixels): the Z2B's doors closed and open, the door
  leaves' squares moving with them; the EN75's front and sides. At
  Zawiercie (QRhi, auto exposure): IC 108's first coach running in and
  standing at 16:07:45, its left doors open on the platform side, door
  D's square out and along with its leaf, the body's panel in place, both
  "IC 108 ADRIATIC, Warszawa – Rijeka, next Zawiercie" from the
  timetable's rows; the 16:55 S1 (EN75): "S1 Częstochowa" in front, the
  side panel behind its window.
- **Tests:** `displays` (+3: the .sd's `Input ( train )`, a car's train's
  data, a document drawing `train.*`), `shape-animation` (+1: a square
  hung on a keyed part moves with it, found by the part's name; with
  stock shapes only, `--game-root` or `TSRE_ANIMATION_SHAPES`: 47 on
  MSTS's, 15 on the Z2B's).

## Route state (2026-11-01, after step 8)

- `RouteDisplays::trainData`: the state from the train's (status, why it
  stands, its next stop, its route part and distance); the distance to
  the next stop only on the same route part (none across a reversal).
- **OR_PSK:** `z2_drzwi.rml` and `en75_wyswietlacze.rml` show, by the
  state, the station a train stands at (the coach with its departure),
  the one it approaches, its terminus (the coach), or its next station.
- **Captures** (Zawiercie, QRhi): IC 108's coach running in ("Następna
  stacja: Zawiercie"), standing ("Stacja: Zawiercie · odjazd 16:08"),
  gone on ("Następna stacja: Dąbrowa Górnicza…"); the S1's side panel
  standing ("KŚ · Stacja: Zawiercie").
- **Tests:** `displays`: a train without a simulation, waiting, standing,
  running and approaching, each field.

## Step 9 done (2026-11-01)

- **Pictures in documents** from the asset's texture folder
  (`ShapeDisplays::picturesFolder`, in RmlUi's path joining,
  `Ui::Runtime`'s `JoinPath`): a picture (`isPicture`: ACE, DDS, PNG, JPG,
  BMP, TGA…) named in a document in `ROUTES/<route>/DISPLAYS` from the
  route's `TEXTURES`, in `TRAINS/TRAINSET/<dir>/DISPLAYS` from the
  trainset's folder; stylesheets and scripts next to the document.
- **Drawn again once its pictures are there:** a display whose pictures
  were not uploaded before its draw (TexLib loads them in the background;
  QRhi uploads within a budget) is due again after 250 ms
  (`Display::picturesPending`, `DisplayDocument::waitsForPictures`).
- **A display's render interface let go of** before it goes
  (`Rml::ReleaseTextures`, `Rml::ReleaseRenderManagers`, as `UiScreen`):
  with pictures, RmlUi's shutdown released them through a destroyed
  interface (the displays suite crashed; TSRE would have on exit).
- **`displayupdate`:** each draw sends the document the event (its Lua,
  under the UI state's deadline), `time` the clock's seconds after
  midnight.
- **Messages for authors** (Errors and Messages, World; each text once,
  `displayMessage`): a shape's blocks left out (both readers keep why:
  `ShapeDisplays::leftOut`, `ComplexShape::displayProblems`), a document
  not found or outside `DISPLAYS`, an `Image` not among the shape's images
  (`ComplexShape::imageNames`), a `ShapeHierarchy` it has not
  (`Ui::ShapeDisplays::report`, once a shape, on the GUI thread when its
  displays are first bound, a shape whose blocks were all left out too);
  a picture not read (`RenderInterface::LoadTexture`); a document that did
  not load and RmlUi's errors and warnings while a display's document
  loads or is drawn (`Runtime::Reporting`).
- **OR_PSK:** three pictograms of TSRE's own (fast, regional, aglo) in
  `PRUDNIK/TEXTURES` (`rodzaj_pociagu_<kind>.png`); `PSK_2026`'s
  `#tsre.extra.kind` (IC fast, TLK and IR regional, KŚ S1 aglo: 3, 4,
  17 trains); the boards' rows show them. `z2_drzwi_lua.rml` with
  `z2_drzwi.lua` on `PKPIC_B11mnouz-70085.sd` (the IC trains' other Z2B,
  the same squares): the bottom line in Polish, then English, 4 s each.
  Each example document begins with a note on what it shows.
- **Docs:** the user guide `docs/guides/displays.md` and its HTML
  edition (in the guides' index and navigation, with four captures); its
  Polish edition `displays.pl.md` and `displays.pl.html` (the user,
  2026-11-02), each edition linking the other;
  `ESD_TSREDisplay` in `docs/features/msts-shape-file-format.md` and
  `shapes-and-shape-management.md`.
- **Captures** (Zawiercie, QRhi): the departures board at 14:20 with the
  three pictograms (EIP 5402 and RJ 3588 without); IC 108's 70085 coach at
  16:07:45 ("Stacja: Zawiercie · odjazd 16:08") and 16:07:49 ("Station:
  Zawiercie · departs 16:08").
- **Tests:** `displays` (+2: the pictures' folders; a picture from a
  route's `TEXTURES` drawn again until loaded, with the Lua's
  `displayupdate`), `shape-materials` (+1: the authors' messages on both
  backends, a subfolder document none).

## Step 10 done (2026-11-02)
- **Fields:** `TrainFields` (`src/tsre/trains/TrainFields.h`): the seven,
  by an enum, each a key (`TrainFields::key`). `OrTimetable` reads the
  `#tsre.<field>` rows (any case, as Open Rails lower-cases them; a field
  again reported, `timetable.read.field.again`), keeps the rows read
  (`fieldRows`: written back, empty too) and writes them after #briefing,
  before the extras; `cloneTrain` copies them. `TrainPlan::fields`.
- **Documents:** `row.<field>` and `train.<field>`. The timetable name is
  now `row.id` and `train.id` (it was `row.train` and `train.name`: `name`
  is the train's proper name now, and names must not clash). `origin` and
  `destination` are the train's own where given, else its stops' (the
  rows' rule kept: none where it comes onto or leaves the route there).
- **`train.stops`:** each `station`, `platform`, `arrives`, `departs`,
  `passed` (by the state: those before the stop it stands at or runs to;
  all once finished; none without a simulation).
- **Date:** `year`, `month`, `day`; `clock` (seconds after midnight).
- **Functions:** `src/tsre/ui/UiDocumentFunctions.cpp`, registered on each
  document's model (its context's type register): `format_time`,
  `format_date`, `pad`, `default`, `add_minutes`, `minutes_until`,
  `cycle`; wrong arguments: an RmlUi warning, so the author's message
  (`displays.document.message`). They read only their arguments: RmlUi
  evaluates an expression again only when a variable in it changed, and
  the first captures showed `cycle(4, …)` reading the clock behind its
  back drawn once, never again. So the clock is data, `clock` (seconds
  after midnight), and an argument: `clock | cycle(4, …)`,
  `minutes_until(clock)`. List and status: `rml-functions.md`.
- **The Shape Viewer's sample:** rows with fields, one without; the train
  IC 108 ADRIATIC with its fields, running from Częstochowa to Zawiercie,
  three stops, `kind` an extra.
- **OR_PSK** (backups: scratchpad `displays/orpsk-backup/step10`):
  PSK_2026's seven `#tsre.extra.` rows renamed `#tsre.<field>` (`kind`
  stays an extra); the six documents rewritten for the fields and for
  reading (a rule's properties one a line, classes instead of inline
  styles): the boards show a late train's delay; `peron_lcd` the minutes
  until the expected departure (`add_minutes`, `minutes_until`) and the
  date under the clock (`format_date`, Polish); `en75_wyswietlacze` the
  stops still ahead in its side bands while running (`train.stops`);
  `z2_drzwi` its bottom line in Polish, then English by `cycle` (the Lua
  document stays the Lua example), so `PKPIC_B11mnouz-70082.sd`'s four
  blocks redraw every second (`RefreshRate ( 1 )`, was 0.2).
- **Examples in the repo:** `docs/examples/displays/` (the documents, the
  pictograms, a README with each document's `.sd` blocks).
- **Guide:** both languages: the functions (section 5), the data tables,
  the timetable rows (section 7), the examples' folder.
- **Tests:** displays: the functions (a document of eight cells, each green
  when its expressions give what they should), a car's document reading a
  field (`train.operator`) and its stops; the rows' fields, a train's own
  origin and destination before its stops', a car's train's stops (those
  left marked, their platforms and times). timetable-read: the field rows
  read, kept, reported again, written back; timetable-trains: the plan's
  fields.

## Textures out of view (2026-11-02, after step 10)

The user's go, 2026-11-02 ("Do the time limit, should be enough for
now"): documents were never freed while the route is open (see Open, "A
display freed"), so every texture once drawn stayed.

- **The setting** `core.rendering.displayKeepUnseen` (seconds, 60; 0
  keeps them; advanced, applies at once): in `RouteDisplays::frame`, a
  document none of whose displays has been drawn for that long
  (`DisplayDocument::see` times it; `unseenFor`) has its texture freed
  (`DisplayDocument::release`). Its RmlUi document stays loaded and is
  due at once: drawn again (a render, not a load), first of those due,
  in the frame after one of its displays is drawn. Drawn means pushed by
  its object: a placed object is pushed in any direction within 150 m
  and its size (`StaticObj` culls by direction beyond that), within its
  shape's distance levels. Freed a document, with mipmaps: 0.5 MB (the
  Z2 door panel, 624 × 168) to 2.8 MB (a board, the platform LCD) and
  5.3 MB (the EN75's bands, 1024²); on QRhi also its drawer's copies of
  its geometry and font textures (the drawer goes with the texture; a new
  one takes them from the draw list as it draws).
- **A shape takes the texture before it is drawn:**
  `RouteDisplays::overrideTextures` (`StaticObj`, `Eng`, before the
  shape's own submission; `ShapeDisplays::overrideTextures`) sets the
  state's override from the document's texture as it is then. Before,
  `push` set it after the shape's packets were submitted: a shape coming
  back would be drawn one frame with the freed texture's name (GL may
  give it to another texture). Now that frame shows the shape's own
  texture, as a display's first frame does; and a new texture shows a
  frame sooner than before (squares took theirs at once already).
- Each texture freed is logged (`Display texture freed, unseen 60 s:`
  and the document's key).
- **Seen** at Zawiercie (`PSK_2026` at 14:20, the limit 10 s, QRhi and
  OpenGL): the departures board, then 30 frames 2.3 km away looking
  elsewhere (the boards' and the platform's LCD's textures freed), then
  the board again: drawn the same (OpenGL the same pixels, QRhi RMSE
  5·10⁻⁶). A view's `runFor` runs before its camera moves, so a run alone
  does not take the camera away.
- **Tests:** `displays`: a document out of sight (not seen yet: never
  out of sight; its texture freed, its document kept and due, drawn again
  the same); `settings`: the count.

## Open
- **Quoted shape labels** (found 2026-11-01, not this task's): TSRE's
  shape writer quoted every label (`matrix "MAIN" (`), and the legacy
  reader kept a quoted matrix label's closing quote (`MAIN"`): a square's
  `ShapeHierarchy` on such a shape was not found by the legacy backend.
  Fixed on `fix/shape-labels` (ac1f4903): labels written unquoted unless
  multi-word, the legacy matrices reader reads a quoted label whole.
  Merged into main (59a99894, 2026-11-02) and from there into this branch
  (347e01c9); the `shape-materials` messages check names a
  `ShapeHierarchy` the shape has again (`PART`: no message).
- **A display freed** calls `Rml::ReleaseRenderManagers`, which also
  regenerates the fonts in use. Measured 2026-11-02 (Release, the four
  examples, timing code reverted): 35–50 ms per freed document with 3 or
  more others live (8–28 ms with 1–2, none for the last), almost all in
  `ReleaseRenderManagers`; the others' next redraws +0–2 ms. It runs in
  `RouteDisplays::frame` on the GUI thread, which renders. A document is
  freed only when nothing holds it, which is rare: the simulation
  started, restarted, sent back, reset or stopped (all its trains'
  documents at once, about 40 ms each), an object moved to another
  platform or station, a vehicle's definition read again. Tiles are not
  unloaded, a deleted object is kept for undo, a finished train keeps its
  cars (corrected 2026-11-02: this note said a train leaving and an
  unloading tile freed them). So the documents seen stay loaded; their
  textures go after `core.rendering.displayKeepUnseen` (Textures out of
  view, above). Not a fault: the user decides when (2026-11-02: later,
  thinking of a shared render interface). The fixes proposed: a pool of
  render interfaces (a freed display's kept for the next,
  `ReleaseRenderManagers` only at RmlUi's shutdown), about 0.5 ms a free,
  the font textures still one copy a document; or one render interface
  for all displays (one copy of the font textures; both renderers' UI
  drawers made to share one draw list's resources). Unloading documents
  out of view waits for one of them.
- **RmlUi on its own thread** (the user, 2026-11-02: an important future
  task, unavoidable as cab displays may become complex; the first
  releases keep it as it is). RmlUi is one global instance, not
  thread-safe, shared by the HUD, the UI drawers and the displays, so all
  of it runs on the GUI thread, which renders: the documents' updates and
  layout, their Lua (one state for all, up to the UI state's 100 ms limit
  a call before a slow script is stopped), loads (1–17 ms on QRhi, 6–54 ms
  on OpenGL, measured in step 5) and `ReleaseRenderManagers` (35–50 ms, the
  item above). Displays cannot move alone; the plan is in the roadmap's
  Later list, to be done with the shared render interface.
- **Lights after a reversal** (found 2026-11-01, not this task's; the
  user, 2026-11-02: the simulation task's, with its other issues; in
  `docs/tasks/simulation/00-task-roadmap.md`'s open items):
  `Consist::lightsTrain` takes a consist's first car as its head and
  ignores the train's `reversed`; after a path's reversal the white
  lights stay at the old front (the 16:55 S1 at Zawiercie, its path
  reversing near Gliwice: white facing Katowice, red facing Częstochowa).
- **The loco scripts' extras:** open (the user, 2026-11-01: not yet sure
  a loco script needs them; time to think). A loco's Lua script sees its
  vehicle's definition, not its train; giving it the train's data means
  passing the train's plan into its model in the simulation thread
  (init's `vehicle`, or `inputs.train`).
- A car's own data (its number in the train) is not given: the train's
  cars share one document; per car, a document each.
- A train with one stop on the route that runs on (RJ 3588 at
  Zawiercie) has no destination of its own without its data.
- The user has other ideas for per-object textures; the shape's state is
  the way for now.
- Glass's reflection and other material properties: the renderer's work
  (the presets are where they go).
- A display's refresh counts wall time: paused, a clock display still
  redraws (the same picture).
- **Platform is one platform line** (one track's edge), the nearest. At
  Zawiercie each `_1` hangs 1.8 m off island platform 2's axis on p2t1's
  side (6.4 m from its track, 9.9 m from p2t2's), back to back with `_2`
  on p2t2's side, both faces along the platform: p2t1, with margin. The
  board on the axis (`ORB_Tpo4`, 8.1 m from both) shows its station, so
  the line it finds makes no difference. A Platform display on an
  island's axis would be a toss-up: a manual link (a W StaticObj
  attribute) only if content needs one.
- The platform lines are read once per TDB (again when its item count
  changes): platform items moved in the editor are seen after a reload.
- Static objects only; other objects with shapes (signals, pickups,
  level crossings) have none yet.
- The core keeps a train's delay (`SimulationCore`), not in the frame: the
  displays' delay is a lower bound.
