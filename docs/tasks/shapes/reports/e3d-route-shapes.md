# MaSzyna `.e3d` as route shapes: placing, lookups, extra data

Review for [task 18](../18-e3d-route-shapes.md), 2026-11-11. Code read on
`main` at 4bc139ce (the same as `origin/main`); nothing was built or run.
Line numbers are for that tree. Pack numbers were counted with throwaway
scripts from `~/ORTS/maszyna/models` (3,335 `.e3d`: MaSzyna's scenery models)
and `scenery/*.inc`; MaSzyna's source is `../maszyna` at 66a940d4.

## Summary

- **Placing already works mechanically.** A W file's `Static ( FileName ( x.e3d ) )`
  loads, draws, picks and saves as a glTF one does. What is wrong is where its
  textures come from.
- **`E3dShape` gets the route's `TEXTURES` but never looks there.** Every
  `.e3d`, not only a `.mmd` vehicle's, takes MaSzyna's lookups: its own folder
  (or a parent holding an `.mmd`), then the game root's `TEXTURES/MASZYNA`.
  Finding that folder lists up to four directories for each model file.
- **The split is small.** Every lookup goes through one value
  (`E3dShape::vehicleFolder`) into `MaszynaPaths::places`. A load option set
  by the `.mmd` vehicle path can keep MaSzyna's rules there, and every other
  use gets one folder: the texture path, as `.s` and glTF have it.
- **`--contentcase` checks the `.e3d` a W file names, nothing inside it.** It
  does not read the `.e3d`'s `TEX0` names or a `.mat`'s textures, and does
  not link an `.sd` beside an `.e3d`. It does not read glTF's URIs either.
- **Extra data: by what it describes.** Per-shape TSRE data belongs in an
  `.sd` beside the `.e3d`, read by the code `SFileComplex` already uses;
  per-object data in the W file; material values in the `.mat` (already
  there); MaSzyna's own part names computed at load. Not a chunk inside the
  `.e3d` (that edits third-party binary assets) and not a JSON sidecar.
- **MaSzyna scenery needs per-object data the W file lacks.** 36 % of the
  models have skin-slot parts (a third of the scenery's model lines name a
  skin: kilometre posts, signs), and 585 models have lamps switched per
  object.
- **Separable from the simulation work.** No file in common with the Lua loco
  scripts, `src/tsre/sim` or sound. `MaszynaPaths.cpp` and `Eng.cpp` are
  touched by both, in different functions.

## 1. Placing: what already works

| Link | Where | For `.e3d` |
|---|---|---|
| W file entry | `StaticObj::set` keeps `FileName` as written (StaticObj.cpp:99-136); `save` writes it back (420-452) | Any extension |
| Shape folder | `WorldObj::createObj`: `Static` in the route's `SHAPES` (WorldObj.cpp:114-118); `getResPath` for a `.ref` item (281-283) | Same |
| Loading | `StaticObj::load` → `ShapeLib::addShape(path)` (StaticObj.cpp:83-97), texture path the route's `TEXTURES` (ShapeLib.cpp:76-78), class by extension (103-109) | `E3dShape` |
| Editor placing | `.ref` items take any `FileName` (Ref.cpp:93-97); the properties panel edits it (PropertiesStatic.cpp:49-72) | A `.ref` line places it |
| Threads | `detachedCopy`, `loadDetached`, `adopt` (E3dShape.cpp:213-247) | Yes |
| Levels | Levels from the parts' ranges (E3dFile.cpp `computeLevels`), chosen by `ShapeLod` | Yes |
| Drawing | `StaticObj::pushRenderItems` → `ShapeLod::apply` → `pushRenderItem` (StaticObj.cpp:213-229) | Yes |
| Picking, box | Selection ids per packet (E3dShape.cpp:829-902), `getBoxPoints` (106-120) | Yes |
| Shadows | The object's `castsShadows`; no `shadowFloorLevel` (the view's level casts) | Yes |
| Collision | Only `CollideObject` flags in the W file, not shape data | Same as `.s` |
| Lights, emitters, displays | `ComplexShape` defaults: none (ComplexShape.h:180-196) | None |
| Snapping, detail level, floor lines | Defaults: not snapable, no level, no lines (ComplexShape.h:48, 226-228) | None (glTF neither) |
| Open Rails | A shape it cannot read becomes an empty shape with a log line (`openrails` Shapes.cs:69-82) | Invisible there, as glTF in OR master |

Gaps on the editor side, shared with glTF: the ruler's node shape list takes
only `.s` (Ref.cpp:360-380); forest tree shapes take `.s`, `.gltf` and `.glb`
(ForestObj.cpp:135-139). The ruler's list is a small fix; trees can wait.

## 2. How `.e3d` finds its files today

- `E3dShape` keeps its texture path (E3dShape.cpp:76-83) and passes it only
  to `MaszynaPaths::vehicleFolder` (E3dShape.cpp:130). That returns the
  texture path if it holds an `.mmd`, else the nearest of the model's folder
  and two parents holding one, else the model's folder
  (MaszynaPaths.cpp:96-108). "Holds an `.mmd`" lists the folder
  (`holdsVehicle`, MaszynaPaths.cpp:21-23). For a route model that is the
  route's `TEXTURES` (6,857 entries at Prudnik), `SHAPES` (11,840), the route
  folder and `ROUTES`, on every model file read.
- Each name is then cleaned (lower case, `:traits` cut, leading `/` cut, a
  known extension dropped; MaszynaPaths.cpp:29-52) and tried in two places:
  the vehicle folder, then `dynamic/…` in `TRAINS/TRAINSET`, `textures/…` or
  anything else in `TEXTURES/MASZYNA` (MaszynaPaths.cpp:54-68). In each place
  `.mat` comes first, then `.dds .tga .ktx .png .bmp .jpg .tex`
  (MaszynaPaths.cpp:19, 77-94; `QFile::exists`).
- A `.mat`'s own textures go the same way (MaszynaMaterial.cpp:253-294).
  Materials are cached by folder, name and season (378-390). Repaints
  (E3dShape.cpp:423-450), the Archive's `usedFiles` (578-600) and the missing
  message (164-175) use the same folder.
- None of it is a case search. Lower-casing is MaSzyna's spelling rule (it
  lower-cases what it opens; MaszynaPaths.h:127-129). Extension probing is
  the format's too, since names carry no extension. What breaks TSRE's rules
  is the places: two folders, one found by listing directories, and a
  game-root folder for a route object.
- So a route `.e3d` whose textures are in the route's `TEXTURES` draws in the
  missing-texture colour. It finds them only in `SHAPES` beside it or in
  `TEXTURES/MASZYNA`.

MaSzyna itself sets the texture path of a scenery model `models/<dir>/x` to
`textures/<dir>/`, then tries the name from the pack root, then from
`textures/` (MdlMngr.cpp:82-101, Texture.cpp:1931-1955, material.cpp:700-707).
A vehicle sets it to its folder while its models load (DynObj.cpp:5238-5243).

## 3. TSRE's rule for `.e3d`

- `.s`: the texture path, plus the `.sd`'s season folder
  (SFileLegacy.cpp:546, 675; `TexLib::addTex(texPath, name)` 1202).
- glTF: URIs relative to the texture path, or to the file's folder when it is
  empty (GltfShape.cpp:2019-2024, 2075).
- `.e3d` by the same rule: a name relative to the texture path, in that one
  folder. MaSzyna's spelling and extension rules stay, being the format's. A
  leading `textures/` is dropped, since MaSzyna's texture root is the texture
  folder. The texture path is the route's `TEXTURES` for world objects, the
  ENG's folder for an ENG naming an `.e3d`, and in the Shape Viewer `TEXTURES`
  beside `SHAPES` or the file's folder (ShapeViewerGLWidget.cpp:974-981).

How the pack fits, taking its `textures/` as the route's `TEXTURES`:

| `TEX0` references in `models/` (6,319) | |
|---|---|
| Found by the rule | 5,098 (81 %) |
| Bare names MaSzyna finds only in `textures/<the model's folder>/` | 1,192 |
| `dynamic/…` names | 16 |
| Missing in MaSzyna as well | 13 |
| Models with every texture found | 2,586 of 3,335 |

An import that copies a model's bare-named textures to the root of
`TEXTURES` finds them too. Only 11 bare names would then stand for two
different files. The alternative rule, MaSzyna's
`TEXTURES/<the model's folder in SHAPES>/` first, means a second place to try
(open question in the task). No `TEX0` name in `models/` has a capital or a
non-ASCII letter, and `textures/` has no capitals at all. Five model file
names do; a W file opens them as written.

### The split

Every lookup takes one value, so the change stays narrow:

- `MaszynaPaths`: a lookup with its rules, MaSzyna's or TSRE's, and its
  folder. `places()` returns MaSzyna's two places as today, or TSRE's one
  (folder/name, `textures/` dropped). `expectedPlace()` follows.
- `MaszynaMaterial::find`/`exists` take the lookup, and the cache key holds
  its rules.
- `E3dShape`: `vehicleFolder` becomes the lookup. A new load option picks
  MaSzyna's rules. `E3dShape` does not override `setLoadOptions` today (the
  3D cab review noted it ignores `DoubleSided`), and `detachedCopy`
  (E3dShape.cpp:213-217) must carry the option.
- Who sets the option: `Eng::resolveShapeIds` (Eng.cpp:649-667) and
  `Eng::cab3DShapeOf` (859-868) when `definition->maszyna` is set. A MaSzyna
  definition's body, low-poly interior and attachments come through
  `shapePath` and `freightAnims` (MaszynaVehicle.cpp:580-592). The Shape
  Viewer needs a rule of its own for a bare `.e3d` (open question).
- `ShapeLib` already keys shapes by their options (ShapeLib.cpp:89-91), so
  one file used both ways makes two shapes. They share one `E3dModel` and
  one mesh, because `E3dFile::read` caches by path.
- The e3d-shape suite builds its shape beside an `.mmd`
  (E3dFileTestSuite.cpp:621-641) and has to set the option.

## 4. What MaSzyna's scenery models hold

| In `models/` (3,335 files) | Count | In TSRE today |
|---|---|---|
| Parts with a skin slot (material -1..-4) | 2,272 parts in 1,207 files (36 %) | Untextured without a repaint; a placed object has no way to name one |
| Model lines in `scenery/*.inc` naming a skin | 2,084 of 6,058 (34 %; `hekto(p7)`, `znaki/…`) | No W field |
| `Light_OnNN` / `Light_OffNN` parts (switched per object by the scenery's `lights`) | 585 files | `Light_On` starts hidden, so lamps always look off (E3dFile.cpp:233-235) |
| `winter_variant` … `autumn_variant` | 10 files | Every variant drawn at once |
| `smokesource_*` parts (chimneys) | 55 parts | Nothing |
| Free spot lights (type 257: glare points) | 909 parts in 300 files | Not drawn |
| Stars (type 258: point lamps) | 91 parts | Not drawn |
| Billboard parts | 117 parts in 12 files | Drawn as modelled, not facing the camera |
| Wind; clock hands (seconds, minutes, hours) | 29 files; 60 parts in at most 35 files | Still |
| `anim: true` (moved by events: gates, arms, doors) | 361 parts in 141 files | Still, at rest |
| Distance limits | 1,000 m (10,599 parts), 15,000 m, 1,400 m, 500 m, 100 m … | Levels; drawn on past the last (TSRE's default), where MaSzyna draws nothing |
| Root record carrying the Z-up turn | 455 files (the rest are baked) | Fine for drawing; matters for `.sd` positions (section 5) |
| Unknown chunks | none | |

MaSzyna binds lamps and variants by name once per model
(`TModel3d::instance_parts`, Model3d.cpp:1628-1641). A node sets the skin and
the lamp states (AnimModel.cpp:252-330, 331-407).

## 5. Extra shape data

### What each `.sd` field means for `.e3d`

| `.sd` field | What it does | glTF | `.e3d` |
|---|---|---|---|
| `ESD_Detail_Level` | The editor's default detail level | None | Applies |
| `ESD_Alternative_Texture` | Season folders under the texture path | None | Applies to the TSRE-rule folder; the `.mat`'s season blocks stay |
| `ESD_Bounding_Box`, `ESD_Complex` | Boxes; snap points from the first (SFileLegacy.cpp:889-911), floor lines | Computed bounds only | Applies |
| `ESD_Snapable` | Editor snapping | None | Applies |
| `ESD_TSREMaterial` | PBR factors per image (task 13) | Its own PBR materials | Not needed: the `.mat` takes `tsre_metalness`, `tsre_roughness`, `tsre_orm` (MaszynaMaterial.h:23-24) |
| `ESD_TSREDisplay` | Display squares (scripting 02) | None | Applies |
| `ESD_TSRELight` (14, 16) | Lights on a named part | `KHR_lights_punctual`, extras `OPENRAILS_light_name`, `TSRE_on`, `TSRE_viewDistance` (GltfShape.cpp:1028-1032) | Applies |
| `ESD_TSREEmitter` (15) | Smoke from a named part | Not yet (node extras planned) | Applies |
| `ESD_TSREDistanceLevelExtension` (16) | Drawing past the last level | Scene extras `TSRE_distanceLevelExtension` (GltfShape.cpp:1050) | Applies; `None` gives MaSzyna's behaviour |
| `ESD_TSRELightSchedule` (renderer 46) | When the lights are on | Scene extras `TSRE_lightSchedule` (GltfShape.cpp:1051) | Applies; also a natural switch for `Light_On`/`Light_Off` parts |

glTF carries a subset, in its own file. `.e3d` has nothing like glTF's
extras: no text block, and only names in `TEX0` and `NAM0`. `.t3d`, its text
form, is left out of TSRE. So `.e3d` can mirror `.s` (an `.sd`) or carry the
data inside its file, as glTF does.

Positions in an `.e3d`'s `.sd` have to mean the model's space: TSRE's drawing
space, as in glTF, not MSTS's mirrored one. An `.sd` written for an `.s` is
not interchangeable with one written for an `.e3d`. There is a trap here.
`ShapeLights::partOf` and `ShapeEmitters::partOf` hang an entry without a
`ShapeHierarchy` on part 0 of any shape with parts (ShapeLights.cpp:283-290,
ShapeEmitters.cpp:198-201). For MSTS, part 0 is the root matrix. For an
`.e3d` it is the first record, which carries the Z-up turn in 455 scenery
models and in vehicles (`banan`), and models may have several roots. glTF
escapes this only because it reports no parts.

### Options

| Option | For | Against |
|---|---|---|
| A. `.sd` beside the `.e3d` (`x.sd`, as `ContentPath::withExtension` makes it) | TSRE's existing per-shape metadata file, read by the same code (`SFileDetail::Document` and the `ShapeDisplays`, `ShapeLights`, `ShapeEmitters` readers, as `SFileComplex::loadMetadata` uses them, SFileComplex.cpp:649-700). Authored, documented and converted (`--contentcase` already reads `.sd` files) as for `.s`. Leaves third-party models untouched. Optional. | Strictly a side file for `.e3d`, since MaSzyna does not know it. `x.s` and `x.e3d` in one folder would share `x.sd`. |
| B. A TSRE chunk inside `E3D0` holding the `.sd` text | One file. MaSzyna skips unknown chunks (Model3d.cpp:2454) and so does TSRE (E3dFile.cpp:82-128). | Edits MaSzyna's binary assets, the licence question that "drawn directly, not converted" avoided (maszyna roadmap). Needs a binary writer to author it. Lost when a model is exported again (MaSzyna's writer keeps only its own chunks, Model3d.cpp:1842-1928). |
| C. The W file | Per-object values, where TSRE already extends it (`TSRELightRange`, `TSRELightSchedule`, WorldObj.cpp:574-581) | Per-shape data repeated in every placement |
| D. The `.mat` | Material values already (`tsre_*` keys) | Only materials |
| E. Computed at load from MaSzyna's names | No storage at all. MaSzyna's own way (`Light_OnNN`, `*_variant`, `smokesource_*`, lit sections already, task maszyna 01) | Only what MaSzyna's names say |
| F. JSON sidecar (task 03) | | Against TSRE's no-side-files rule; never built |

### Recommendation

Use each place for what it holds:

1. **Computed at load (E)** for what MaSzyna's names define: `Light_OnNN`
   shown and `Light_OffNN` hidden while the object's lights are on (the
   queue's `lightsOn()`, which `StaticObj` already sets from its schedule,
   StaticObj.cpp:216), and a season's `*_variant`. Later, the glare points as
   disc-only lights and the `smokesource_*` emitters.
2. **The W file (C)** for per-object values: the existing light range and
   schedule, plus a repaint (`TSRERepaint ( "name" )`, free in `TS.cpp`'s
   token table) feeding the generic `ComplexShape::setRepaint`
   (ComplexShape.h:163). That also covers an ENG's or Open Rails' repaints
   later.
3. **The `.mat` (D)** for material values, as today.
4. **An `.sd` beside the `.e3d` (A)** for TSRE-only per-shape data. It
   matches the user's suggestion, and the no-side-files rule names "extend
   the `.sd`". It is TSRE's existing metadata file for route shapes, not a
   new kind of file, and `.e3d` has no extensible block of its own. Its
   positions are in the model's space, and an entry without `ShapeHierarchy`
   sits at the model's origin.

Not B: it edits assets TSRE deliberately draws as they are. Not F.

## 6. `--contentcase`

The converter (`src/contentCase`, run as `TSRE5vc --contentcase <root>`):

- Resolves a W or `.ref` `FileName` as a shape in the route's `SHAPES`
  (ContentCase.cpp:337, 439-446), so the `.e3d` file itself is case-checked.
- A MaSzyna `.con` row resolves to its `.mmd` (ContentCase.cpp:360-363).
- Reads references only from its families (`familyOf`, ContentCase.cpp:36-48).
  `.e3d`, `.mat`, `.mmd`, `.gltf` and `.glb` are "unclassified" (60). Texture
  jobs and the implicit `.sd` companion are made only for `.s`
  (ContentCase.cpp:366, 516-541). An `.sd` naming `x.e3d` in its `shape`
  field is still resolved (367).

So it skips what an `.e3d` names. An adapter would read `TEX0` (a chunk walk),
apply MaSzyna's spelling (lower case, traits, the extension list), resolve
against the shape's texture contexts (the `shapeContexts` it already
collects), read a `.mat`'s texture keys, and add the `.sd` companion.
MaSzyna content is lower case already: the user renamed 213 files in the
pack (2026-11-09), and `models/`' `TEX0` names have no capitals. So the
adapter matters for completeness more than for this pack.

### Case in the pack (counted 2026-11-11)

TSRE mode opens names as written (the user, 2026-11-11, question 10), so
every name whose case differs from its file needs `--contentcase`. Counted
with a throwaway script: every token of `scenery/*.scn/.scm/.inc/.ctr`
(4,611 files, comments cut) that resolves by MaSzyna's rules, compared with
the file's real name.

| What names it | Distinct names | Case differs | Uses that differ |
|---|---|---|---|
| `TEX0` in `models/` | 6,319 | 0 | |
| `.mat` texture keys (`models/`, `textures/`, `scenery/`) | 2,015 | 8 | |
| Scenery: models | 3,017 | 114 | 1,641 |
| Scenery: includes and other scenery files | 3,492 | 464 | 18,460 |
| Scenery: vehicle folders (`dynamic`) | 295 | 117 | 4,092 |
| Scenery: textures | 1,816 | 174 | 65,717 |
| Scenery: sounds | 2,913 | 43 | 67 |

- Mostly capitals written for lower-case files (`PKP\EP09_V1`,
  `Rail_screw_used1`, `pkp/D1_stojaca.t3d`), the case MaSzyna's
  lower-casing hides. Some go the other way: of the files the scenery
  names, 124 scenery files and 5 models have capitals in their real names
  (`braniewo/base.ctr` names `Braniewo/Base.ctr`), which MaSzyna itself
  cannot open on Linux.
- Names put together from include parameters (`(p1)`) count only where a
  parameter is a whole name, so the scenery rows are a lower bound.
- 597 files are named in more than one spelling (`dynamic/PKP/412W_V1`,
  `dynamic/pkp/412w_V1`, `dynamic/pkp/412w_v1`), so renaming files cannot
  satisfy every name in the scenery as it is.
- Only the first two rows reach TSRE in task 18. The scenery's names matter
  to a scenery import, which runs offline and can write the real names.

## 7. Independence from the simulation work

| Work | Files |
|---|---|
| Simulation (`feature/maszyna` 742e56f5..ab561de0, worktree `../TSRE5vc-maszyna` with uncommitted task 06) | `appdata/0.7/scripts/loco/**`, `src/tsre/sim/*`, `src/tsre/sound/*`, `MaszynaSound*`, `MaszynaDiesel*`, `MaszynaFiz*`, `MaszynaCab*`, `MaszynaText*`, `MaszynaVehicle*`, `Eng.cpp` (sounds, `poseCab`), `Consist.*`, `CabControls.h`, `VehicleDefinition.h`, `DrivingKeys.cpp`, `LocoScript.cpp`, test suites |
| This work | `MaszynaPaths.*`, `MaszynaMaterial.*`, `E3dShape.*` (perhaps `E3dFile.*`), `ComplexShape.h` (an option key, a root-part hook), `ShapeLights.cpp`, `ShapeEmitters.cpp`, `Eng.cpp` (two `addShape` calls), the Shape Viewer, `StaticObj.*`/`WorldObj.cpp`/`PropertiesStatic.*`, `src/contentCase/*`, `E3dFileTestSuite.cpp`, `TestRunner.cpp`, both `.ts` files |

`git diff main feature/maszyna -- src/tsre/maszyna src/tsre/shape` touches
no shape file. In `MaszynaPaths.{h,cpp}` the branch adds `soundFolder()`
next to `places()`, and in `Eng.cpp` it changes `updateSounds`,
`soundEvent` and `poseCab`, not the `addShape` calls. Expect one-line
neighbour conflicts at most, plus the test lists.

## 8. Risks

- **Route objects lose MaSzyna's own folder rules.** A model copied without
  its textures re-laid draws partly untextured. 1,192 bare names need an
  import that flattens them, or the mirror rule.
- **Skins.** Without a per-object repaint, 36 % of the scenery models draw
  some parts untextured. This is the first thing a placed MaSzyna model
  shows.
- **Lamps and animation.** Lamps look off, billboards edge-on, clocks
  stopped, gates and arms at rest. MaSzyna's signals (`sem/`, 329 models)
  are scenery models switched by events. TSRE signals are `sigcfg` shapes
  with MSTS keyframes, so an `.e3d` signal is out of scope.
- **Drawn farther than MaSzyna draws.** TSRE draws the last level on to the
  object distance. That is the same rule as every shape, and `.sd`
  `ESD_TSREDistanceLevelExtension ( None )` restores MaSzyna's cut.
- **`cien` parts.** MaSzyna's painted ground shadows (blended quads) add to
  TSRE's real shadows.
- **The `.sd` space.** An `.sd` copied from an `.s` would be mirrored in x.
  The guide has to say so.
- **Open Rails.** It shows no `.e3d` (an empty shape and a log line), as for
  glTF in its master.

## 9. The assumptions checked

- *ComplexShape receives the route's texture path*: yes. `ShapeLib` passes
  the route's `TEXTURES` (ShapeLib.cpp:76-78, 108). But `E3dShape` does not
  use it as a texture folder (section 2).
- *Only the `.mmd` path breaks TSRE's rules*: no. Every `.e3d` uses
  MaSzyna's lookups today. The `.mmd` path is only where they are right.
- *`--contentcase` fixes e3d naming*: no. It checks the `.e3d` file a W file
  names, but not the textures and materials inside (section 6).
- *e3d carries no extra data*: right. Unknown chunks would survive in
  MaSzyna, but writing them means editing the assets (section 5).
