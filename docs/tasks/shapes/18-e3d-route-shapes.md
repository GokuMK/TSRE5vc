# Task 18 - MaSzyna `.e3d` Shapes in Routes

## Objective

Place MaSzyna `.e3d` models in a route the way `.s` and glTF shapes are
placed: a W file `Static` naming the file, drawn with the route's textures by
TSRE's rules, with TSRE's extra shape data, and with the per-object data
MaSzyna's scenery models need (a skin, their lamps). The `.mmd` vehicle path
keeps MaSzyna's own lookups (task maszyna 01-04). It is the only exception.

Status: brief, 2026-11-11. Review:
[reports/e3d-route-shapes.md](reports/e3d-route-shapes.md). Questions 1 and
5 decided by the user (2026-11-11); the others are still open, each with
its recommendation. Nothing is built.

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
- No runtime case search and no directory listing for lookups. MaSzyna's
  lower-casing of names and its extension list are the format's spelling
  rules, not searches (`MaszynaPaths.h:127-129`).
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
  - `Tsre`: one place, `folder/name`. The name is cleaned as today
    (`clean()`: lower case, `:traits` cut, leading `/` cut, known extension
    dropped), and a leading `textures/` is dropped too. The `.mat` is tried
    first, then the texture extensions (MaszynaPaths.cpp:77-94).
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
  - The Shape Viewer: open question 6.
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
  - Not `ESD_TSREMaterial`: the `.mat`'s `tsre_*` keys are its place (open
    question 2).
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
- Read in both modes (open question 3). A reload reads it again, and the
  messages come once a load (`firstReport`).

### 3. Per object: skin, lamps, seasons

- Skin: the W file's `TSRERepaint ( "name" )` on `Static`, `Gantry` and
  `CollideObject` (name: open question 4).
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

### 4. `--contentcase` (optional; may become a core task)

- `.e3d` and `.mat` as families (ContentCase.cpp:36-61). An `.e3d`'s `TEX0`
  names are texture edges against the shape's texture contexts
  (`shapeContexts`, ContentCase.cpp:286, 516-541), spelt by MaSzyna's rules
  (lower case, traits, the extension list).
- A `.mat`'s `texture*` keys are edges too.
- The `.sd` companion as for `.s` (ContentCase.cpp:518).
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
4. **`--contentcase`** (design 4), if the user wants it in this task.

## Open questions (for the user)

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
2. **`ESD_TSREMaterial` for `.e3d`?** Recommended: no. A `.mat` holds
   `tsre_metalness`, `tsre_roughness` and `tsre_orm`, and a bare texture name
   can get a `.mat`.
3. **Does a `.mmd` vehicle's `.e3d` read its `.sd` too?** Recommended: yes.
   The `.sd` belongs to the model file, and the override is only about
   lookups. It allows `ESD_TSRELight` on MaSzyna locos later.
4. **The W field for a skin.** Recommended: `TSRERepaint ( "name" )`,
   generic for any shape with repaints. MaSzyna's `none` means no repaint.
5. **Bare texture names.** MaSzyna looks in
   `textures/<the model's folder>/` first. Decided (the user, 2026-11-11):
   one folder. A route's `.e3d` takes its textures from the route's
   `TEXTURES`, and putting the right textures there is the route
   designer's job. No second place in TSRE mode and no import in this task
   (the scenery import stays under Later). With the pack's `textures/` as
   `TEXTURES` the rule finds 81 % of the scenery models' references; the
   rest need their textures laid out by the designer (11 names clash).
6. **The Shape Viewer and a bare `.e3d`.** Recommended: MaSzyna's rules when
   the file's folder or one of two parents holds an `.mmd`. That is today's
   test, moved to the viewer and run once per file opened. Otherwise TSRE's
   rules with `textureDirectory()`.
7. **`dynamic/…` names in TSRE mode (16 in `models/`).** Recommended:
   missing, with a message; a route model uses route textures. The
   alternative is the fixed map to `[[Trainset]]` that vehicle mode uses.
8. **The shared `TEXTURES/MASZYNA` in TSRE mode.** Recommended: no.
9. **Past the last level.** Recommended: TSRE's default (drawn on), as for
   every shape; `ESD_TSREDistanceLevelExtension ( None )` gives MaSzyna's
   cut.
10. **Lower-casing in TSRE mode.** Recommended: keep it as the format's
    spelling, since MaSzyna opens only lower-case names. The pack's scenery
    names are all lower case.
11. **`--contentcase` in this task, or a core task?** Recommended: step 4
    here, after the runtime steps, optional.

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
  route, laid out by the rule, and report the clashes.
- `E3dShape` and `DoubleSided` (3D cab review: the option is ignored).

## Related

- [reports/e3d-route-shapes.md](reports/e3d-route-shapes.md): the review.
- `docs/tasks/maszyna/01-e3d-models.md` ("Models from `models/` and
  `scenery/` placed on a route" was left out there),
  `reports/rolling-stock-format-review.md`.
- Tasks 14, 15, 16 (the `.sd` blocks), renderer 46 (light schedule).
- `docs/tasks/core/case-sensitive-filepaths.md` (the converter).
