# Task 18 - MaSzyna `.e3d` Shapes in Routes

## Objective

Place MaSzyna `.e3d` models in a route the way `.s` and glTF shapes are
placed: a W file `Static` naming the file, drawn with the route's textures by
TSRE's rules, with TSRE's extra shape data, and with the per-object data
MaSzyna's scenery models need (a skin, their lamps). The `.mmd` vehicle path
keeps MaSzyna's own lookups (task maszyna 01-04). It is the only exception.

Status: steps 1-4 built 2026-11-11 on `feature/e3d-route-shapes` (the
user's go), not merged; results and what is left below. Review:
[reports/e3d-route-shapes.md](reports/e3d-route-shapes.md). Every question
decided by the user (2026-11-11): 1, 5 and 10 as answered, the others as
recommended.

## Why (the user, 2026-11-11)

- A route asset follows TSRE's rules whatever its format. glTF already
  follows the `.s` rules, and `.e3d` must too. The default texture folder is
  the route's `TEXTURES`.
- No case-insensitive lookups at runtime: fixed upper-case folders, exact
  names, content named offline by `--contentcase`.
- MaSzyna vehicles loaded through `.mmd` keep MaSzyna's lookups, so its
  trains are easy to use. That is the only override.
- Extra shape data: `.s` has its `.sd`, glTF its extras. The user's
  suggestion is an `.sd` beside the `.e3d`.

## Rules

- Follow the review's findings; do not redo it. Check `origin/main` first
  (`git fetch`): the user pushes from other machines.
- No runtime case search and no directory listing for lookups. TSRE mode
  opens names as written: no lower-casing (question 10); `--contentcase`
  repairs names. MaSzyna's lower-casing stays only in MaSzyna mode, for
  `.mmd` vehicles (`MaszynaPaths.h:127-129`). The extension list, `\` as `/`,
  the `:traits` cut and the leading `/` cut are the format's spelling in
  both modes.
- No new kinds of side files. The `.sd` beside the `.e3d` (decided, question 1) is TSRE's existing
  per-shape metadata file.
  Do not build step 2 before it is answered.
- Reuse: `SFileComplex::loadMetadata`'s readers, `ShapeLights::Drawn`,
  `ComplexShape::setRepaint`, the queue's `lightsOn()`, and the W file's
  TSRE tokens. Copy no reader.
- Developer switches are `TSRE_*` environment variables read once, never
  settings. Nothing reads `Settings::` per frame.
- Sources mix CRLF and LF. Edit mixed files in binary mode, and compare
  `git diff --shortstat` with `--ignore-cr-at-eol` before a commit.
- New messages are `qtTrId` ids, in both `translations/*.ts`.
- Never write into the user's content (`~/ORTS/...`). Test roots are scratch
  folders of symlinks plus copies of the files a test changes.
- Work in a worktree from `main` (`../TSRE5vc-e3d-route`, branch
  `feature/e3d-route-shapes`). Commit only on the user's word, and merge
  only on the user's word.
- `feature/maszyna` (simulation, Lua loco scripts, sounds) is another
  session's work. It touches `MaszynaPaths.{h,cpp}` (`soundFolder()` beside
  `places()`) and `Eng.cpp` (sounds, `poseCab`). Do not edit it, and expect
  small neighbour conflicts at the merge.

## Design

### 1. Two lookup modes

- `MaszynaPaths`: a lookup made of rules (`Maszyna`, `Tsre`) and a folder.
  - `Maszyna`: as today (`places()`, MaszynaPaths.cpp:54-68).
  - `Tsre`: one place, `folder/name`. The name is cleaned as `clean()` does
    but **not lower-cased** (question 10): `\` as `/`, `:traits` cut,
    leading `/` cut, a known extension dropped (recognised in any case),
    and a leading `textures/` dropped too. The `.mat` is tried first, then
    the texture extensions as listed, in lower case (MaszynaPaths.cpp:77-94).
  - `expectedPlace()` follows the mode.
- `MaszynaMaterial::find`/`exists` take the lookup, and the cache key holds
  its rules (MaszynaMaterial.cpp:378-396).
- `E3dShape`:
  - `vehicleFolder` becomes the lookup (E3dShape.h:166).
  - It overrides `setLoadOptions` to read a new key,
    `ShapeLoadOption::MaszynaLookups` (ComplexShape.h:33-39).
  - Without the key: `Tsre`, with the texture path as the folder (plus the
    `.sd`'s season folder once step 2 is in).
  - With the key: `Maszyna`, folder `MaszynaPaths::vehicleFolder(pathid,
    texPath)` as today. The directory listing is then used only here.
  - `detachedCopy` carries the mode (E3dShape.cpp:213-217).
  - The missing-material message (E3dShape.cpp:164-175) gets a TSRE-mode
    text.
- Setting the key:
  - `Eng::resolveShapeIds` (Eng.cpp:649-667) and `Eng::cab3DShapeOf`
    (859-868), when `definition->maszyna != nullptr`.
  - The Shape Viewer, for a bare `.e3d` whose folder or one of two parents
    holds an `.mmd` (question 6).
  - Nowhere else. An ENG naming an `.e3d` gets TSRE's rules, with its
    textures in the vehicle's folder as for `.s`.

### 2. The `.sd` beside the `.e3d` (decided, question 1)

- `x.e3d` reads `x.sd` (`ContentPath::withExtension`), with the reader
  `SFileComplex::loadMetadata` uses (SFileComplex.cpp:649-700). Factor that
  code into one shared metadata reader rather than copying it.
- The fields read:
  - `ESD_Detail_Level`, `ESD_Snapable`.
  - `ESD_Bounding_Box` and `ESD_Complex`: snap points as
    SFileLegacy.cpp:889-911 makes them.
  - `ESD_Alternative_Texture`: the season folder on the TSRE-mode folder.
  - `ESD_TSREDisplay`, `ESD_TSRELight`, `ESD_TSREEmitter`,
    `ESD_TSREDistanceLevelExtension`, `ESD_TSRELightSchedule`.
  - Not `ESD_TSREMaterial`: the `.mat`'s `tsre_*` keys are its place
    (question 2).
- `E3dShape` overrides:
  - `getEsdDetailLevel`, `isSnapable`, `addSnapablePoints`.
  - `displays` and `displayProblems`.
  - `lights`, `lightProblems`, `setLightStrength`, `pushLights`, through
    `ShapeLights::Drawn` as the other shapes do.
  - `emitters` and `emitterProblems`.
  - `distanceLevelExtension`, `lightSchedule`.
- Space: positions are in the model's space (TSRE's drawing space, as
  glTF's), not MSTS's mirrored one.
  - An entry without `ShapeHierarchy` sits at the model's origin.
  - `ShapeLights::partOf` and `ShapeEmitters::partOf` take part 0 for any
    shape with parts (ShapeLights.cpp:283-290, ShapeEmitters.cpp:198-201).
    For an `.e3d`, part 0 is the first record, often carrying the Z-up
    turn. Add a `ComplexShape` hook for the root part: 0 by default, -1 for
    `E3dShape`. Check displays for the same rule.
- Read in both modes (question 3). A reload reads it again, and the
  messages come once a load (`firstReport`).

### 3. Per object: skin, lamps, seasons

- Skin: the W file's `TSRERepaint ( "name" )` on `Static`, `Gantry` and
  `CollideObject` (question 4).
  - Read it in `WorldObj::set` beside `TSRELightRange` (WorldObj.cpp:574-581)
    and write it in `StaticObj::save` (StaticObj.cpp:420-452).
  - `StaticObj::load` calls `setRepaint(shapeState, name)` after
    `newState()`.
  - A field in `PropertiesStatic` beside the light schedule, with undo; copy
    and paste keep it.
  - Generic: any shape with repaints takes it. An `.e3d` repaint in TSRE
    mode finds `<skin>`, `<skin>,1`… in the texture folder, as MaSzyna's
    node skin is found (AnimModel.cpp:252-275).
- Lamps (TSRE mode only):
  - `Light_OnNN` parts are shown and `Light_OffNN` hidden while
    `queue.lightsOn()`, which `StaticObj` already sets from the object's and
    the shape's schedule (StaticObj.cpp:216).
  - Keep it to two shared at-rest packet sets, so objects still instance.
    MaSzyna binds the names once per model (Model3d.cpp:1628-1641).
  - Vehicles are not touched.
- Seasons (TSRE mode): of `winter_variant`, `spring_variant`,
  `summer_variant` and `autumn_variant`, only `Game::season`'s is shown, set
  at load. Today all four draw at once.

### 4. `--contentcase` (in this task: TSRE mode does not lower-case, question 11)

- `.e3d` and `.mat` as families (ContentCase.cpp:36-61). An `.e3d`'s `TEX0`
  names are texture edges against the shape's texture contexts
  (`shapeContexts`, ContentCase.cpp:286, 516-541), as written, with the
  format's other spelling (`\` as `/`, traits, leading `/`, the extension
  list in lower case: a `x.DDS` file is renamed `x.dds`).
- A `.mat`'s `texture*` keys are edges too.
- The `.sd` companion as for `.s` (ContentCase.cpp:518).
- Repairs as for `.s`: rename the file to the spelling its references use; a
  `.mat` (text) is rewritten only where its references disagree. An `.e3d`
  is never edited: a `TEX0` clash is reported (none in the pack).
  As built (2026-11-11): the `.mat` too is a fixed reference for now, so a
  clash between spellings is reported for either (Results).
- In the pack (counted 2026-11-11, review section 6): 0 of 6,319 `TEX0`
  names and 8 of 2,015 `.mat` texture keys have capitals; the 8 are in
  `textures/elektryczne/slupy_wn` (`400kV`) and `textures/siec_trakcyjna_ph`
  (`STS_szare…`), naming lower-case files.
- Report on a scratch copy only.

## Steps

Each step ends with `cmake --build build -j8` (all targets), `ctest`, and
the app suites (`./build/TSRE5vc --test --test-suite <name>`, one at a time
under Xvfb).

1. **Lookup modes** (design 1). Suites: `e3d-shape` sets the option for its
   vehicle case (E3dFileTestSuite.cpp:641), and new cases:
   - TSRE mode finds `TEXTURES/x.mat`, `TEXTURES/sub/x.dds`, and
     `textures/sub/x` as `TEXTURES/sub/x`.
   - It does not find a texture beside the model or in `TEXTURES/MASZYNA`.
   - A repaint in TSRE mode.
   - One file in both modes gives two shapes and one `E3dModel`.

   Unchanged: `e3d-file`, `e3d-corpus`, `maszyna-vehicle`, `maszyna-cab`,
   `vehicle-files`, `cab3d`, and an ET22 capture against one made before.
2. **The `.sd`** (design 2). Suite cases:
   - Each field read.
   - A light on a named part follows its pose; one without a hierarchy sits
     at the model's origin.
   - An emitter.
   - `None` stops drawing past the last level.
   - Snap points.
   - A reload.

   `shape-lights`, `shape-emitters`, `displays` unchanged.
3. **Per object** (design 3). Suite cases:
   - `TSRERepaint` read, written and drawn.
   - Lamps follow `lightsOn()` with the packets still shared.
   - Variants by season.

   Route check on a scratch root:
   - A copied route plus a `.ref` line
     `Static ( Class ( "MaSzyna" ) FileName ( "x.e3d" ) )` and models from
     `~/ORTS/maszyna/models` with their textures laid out by the rule.
   - Place, save, reload, select, snap.
   - Captures by day and at night (xvfb, QRhi; `DISPLAY=:97` for `import`).
4. **`--contentcase`** (design 4). Until it is in, a name whose case
   differs from its file is missing in TSRE mode (in the pack: the 8 `.mat`
   keys).

## Results (2026-11-11, Release, `feature/e3d-route-shapes`)

Built as designed, steps 1-4. What differs or is left is listed after the
checks.

- **Lookups** (`MaszynaPaths::Lookup`, `MaszynaMaterial::find`/`exists` by
  lookup, `ShapeLoadOption::MaszynaLookups`): set by `Eng` for a MaSzyna
  definition's body, extra shapes and cab models, and by the Shape Viewer
  for a bare `.e3d` beside an `.mmd` (`MaszynaPaths::besideVehicle`).
  `.mat` values keep their case when read (keys are compared in lower
  case); MaSzyna's rules lower-case them at the lookup as before.
- **`.sd`**: `ShapeMetadata`, the reader `SFileComplex::loadMetadata` now
  uses too. `ComplexShape::rootPart()` (0, -1 for `E3dShape`) for lights and
  emitters without a `ShapeHierarchy`. Displays already hang on the shape's
  space without one.
- **Per object**: `TSRERepaint ( "name" )` read in `WorldObj::set`, kept by
  copies, written by `StaticObj::save`, a Repaint field under Lights On in
  the properties panel (undo). `StaticObj` gives its state the repaint when
  it draws and the name changed, so an undo or a paste needs nothing more.
  MaSzyna's `none` is no repaint. Lamps: `Light_On00`-`07` shown and
  `Light_Off00`-`07` hidden while the queue's lights are on, as one second
  shared at-rest set; `*_variant` by `Game::season` (empty: summer).
- **`--contentcase`**: `.e3d` (`TEX0`) and `.mat` (texture keys, any
  season's block, `[ a b ]` sets) read; a placed model is a shape with its
  `.sd` companion and its route's texture contexts; a vehicle's (under
  `TRAINS/TRAINSET`) is `maszyna-vehicle-lookups` coverage, not read.
  Edges `maszyna-texture`: `.mat` first, then the texture extensions; a
  `.mat` found is read in the same folder. `Document::editable` false for
  both: `rename-targets-only`, and `patchDocument` refuses them.

Checks:
- Suites: `e3d-file` 71, `e3d-shape` 78 (TSRE rules, both modes in one
  `ShapeLib`, the `.sd`'s fields, lights and emitter on the model's origin,
  snap points, lamps lit and unlit sharing packets, winter variant, a
  reload, a vehicle's model unchanged), `maszyna-vehicle` 94, `token-world`
  72 (`TSRERepaint`); `tsre_content_case_tests` (a placed model, its `.sd`, a
  `.mat` with a season block, a clash, a missing name, a vehicle's model
  left alone). `ctest` 11 of 11. All 128 app suites run: every one that
  needs no arguments passes; the others stop for their missing game root,
  route or case file.
- `e3d-corpus` over the pack: 6,197 files, no failures, totals as before;
  `maszyna-vehicle-corpus`: 3,688 rows, the same as `feature/maszyna`'s
  build gives (task 02's 3,692 predates its catalogue rule).
- ET22 in the Shape Viewer against `feature/maszyna`'s build: four views,
  RMSE 0.
- Route check on a scratch root (MSTS `MINI` as symlinks, MaSzyna's
  `pkp/sem-rg-i` signal and `hekto3_setki_slupek` digit plates, their
  textures laid out in the route's `TEXTURES` by the rule), placed by the
  capture harness through the route's own placing (`Route::placeObject`; a
  `repaint` key for placed objects added to it): the signal textured, its
  lenses dark by day and lit at night, its `.sd` light and snap points; the
  plates: `hekto7` draws the 7, `Hekto7` the missing texture (names as
  written), none plain grey; picking hits them. Saving was not run on the
  scratch root (its `WORLD` is the user's, linked); the W round trip is the
  suite's.

Differs from the design or left:
- `.mat` rewriting: not done. MaSzyna's names carry traits, extensions and
  `textures/` that the converter's rewrite would have to keep; a clash
  between spellings is reported instead, for `.mat` as for `.e3d`.
- `--contentcase` reads a placed model's names in its route's `TEXTURES`
  only: not the `.sd`'s season folder, and no seasonal candidates as `.s`
  textures get.
- Lamps switch together with the object's lights; MaSzyna's per-object lamp
  states (`lights`) stay under Later.
- A missing repaint draws the missing-texture colour (a warning is told);
  no repaint draws plain grey, as before.
- Not run: the route editor's save and reload on a real route copy, and
  snapping one object to another.

## Questions (all decided by the user, 2026-11-11)

1. **Extra data: an `.sd` beside the `.e3d`?** Decided (the user,
   2026-11-11): yes, `x.sd`, the same name rule and reader as `.s`,
   optional.
   - It is TSRE's existing per-shape metadata file, which the
     no-side-files rule names as a place to extend, and `.e3d` has no
     extensible block.
   - Rejected: a TSRE chunk inside the `.e3d`, which edits MaSzyna's binary
     assets (the licence reason for drawing them directly), needs a writer,
     and is lost on export. Also rejected: a JSON sidecar (task 03).
   - If `x.s` and `x.e3d` share a folder and an `x.sd`: report it.
2. **`ESD_TSREMaterial` for `.e3d`?** Decided: no. A `.mat` holds
   `tsre_metalness`, `tsre_roughness` and `tsre_orm`, and a bare texture name
   can get a `.mat`.
3. **Does a `.mmd` vehicle's `.e3d` read its `.sd` too?** Decided: yes.
   The `.sd` belongs to the model file, and the override is only about
   lookups. It allows `ESD_TSRELight` on MaSzyna locos later.
4. **The W field for a skin.** Decided: `TSRERepaint ( "name" )`,
   generic for any shape with repaints. MaSzyna's `none` means no repaint.
5. **Bare texture names.** MaSzyna looks in
   `textures/<the model's folder>/` first. Decided (the user, 2026-11-11):
   one folder. A route's `.e3d` takes its textures from the route's
   `TEXTURES`, and putting the right textures there is the route
   designer's job. No second place in TSRE mode and no import in this task
   (the scenery import stays under Later). With the pack's `textures/` as
   `TEXTURES` the rule finds 81 % of the scenery models' references; the
   rest need their textures laid out by the designer (11 names clash).
6. **The Shape Viewer and a bare `.e3d`.** Decided: MaSzyna's rules when
   the file's folder or one of two parents holds an `.mmd`. That is today's
   test, moved to the viewer and run once per file opened. Otherwise TSRE's
   rules with `textureDirectory()`.
7. **`dynamic/…` names in TSRE mode (16 in `models/`).** Decided:
   missing, with a message; a route model uses route textures. Not the
   fixed map to `[[Trainset]]` that vehicle mode uses.
8. **The shared `TEXTURES/MASZYNA` in TSRE mode.** Decided: no.
9. **Past the last level.** Decided: TSRE's default (drawn on), as for
   every shape; `ESD_TSREDistanceLevelExtension ( None )` gives MaSzyna's
   cut.
10. **Lower-casing in TSRE mode.** Decided (the user, 2026-11-11): no.
    TSRE mode opens names as written, like every other route asset, and
    `--contentcase` repairs names whose case differs. MaSzyna mode (`.mmd`
    vehicles) keeps MaSzyna's lower-casing as part of its lookups.
    - Inside the models it hardly matters: no `TEX0` name has a capital,
      and 8 `.mat` keys do (design 4).
    - MaSzyna's scenery text is where the case differs (review section 6):
      about 910 names differ from their files, and 597 files are named in
      more than one spelling, so renaming cannot satisfy them. That is the
      scenery import's job (Later), not the runtime's.
11. **`--contentcase` in this task, or a core task?** Decided: step 4
    here, after the runtime steps. Without lower-casing it is the only fix
    for a name whose case differs, so it is no longer optional.

## Later

- Glare points (type 257) as disc-only lights computed at load. Stars (258).
- `smokesource_*` parts as emitters computed at load
  (`data/smokesource_*.txt`).
- Billboard parts facing the camera (`setCameraDirection`, as impostors).
  Clocks from the game time.
- Event-driven parts (`anim: true`): gates, arms, doors. MaSzyna signals
  (`sem/`) stay scenery; TSRE signals are `sigcfg` shapes.
- MaSzyna's per-object lamp states and colours (`lights`, `lightcolors`;
  fractions blink).
- `.e3d` trees in forests (ForestObj.cpp:135-139); `.e3d` and glTF in the
  ruler's node shape list (Ref.cpp:360-380); floor lines
  (`getFloorBorderLinePoints`, which glTF lacks too).
- A MaSzyna scenery import: copy chosen models and their textures into a
  route, laid out by the rule, and report the clashes. It runs offline, so it
  may resolve the scenery's names by MaSzyna's lower-casing and writes the
  files' real names into the W files; the runtime never sees the scenery's
  spellings (`PKP\EP09_V1`, `Rail_screw_used1`, `braniewo/base.ctr` for
  `Braniewo/Base.ctr`).
- `E3dShape` and `DoubleSided` (3D cab review: the option is ignored).

## Related

- [reports/e3d-route-shapes.md](reports/e3d-route-shapes.md): the review.
- `docs/tasks/maszyna/01-e3d-models.md` ("Models from `models/` and
  `scenery/` placed on a route" was left out there),
  `reports/rolling-stock-format-review.md`.
- Tasks 14, 15, 16 (the `.sd` blocks), renderer 46 (light schedule).
- `docs/tasks/core/case-sensitive-filepaths.md` (the converter).
