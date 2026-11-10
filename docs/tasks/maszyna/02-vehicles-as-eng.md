# Task 02 - MaSzyna vehicles as `Eng`

## Objective

Drop MaSzyna's vehicle folders into the game's trainset
(`dynamic/pkp/et22_v3/` to `TRAINS/TRAINSET/pkp/et22_v3/`) and have TSRE find
them: every vehicle in a folder is an `Eng`, listed in the Consist Editor,
opened in the Shape Viewer, placed in consists on a route, with its wheels,
bogies and pantographs moving. Physics is the task after this one.

Status: steps 1-7 built 2026-11-09 on `feature/maszyna`, not merged; left
below under Results. Depends on [task 01](01-e3d-models.md). Facts are in the
[review](reports/rolling-stock-format-review.md).

## Is there an equivalent of an ENG file?

The roadmap of 2026-10-24 planned generated ENG files, on the assumption a
MaSzyna vehicle has none of its own. The review finds it has:

| MSTS / Open Rails | MaSzyna |
|---|---|
| `.eng`, `.wag` | `<type>.fiz` with `<type>.mmd` |
| `Shape ( ... )` | `.mmd` `models:` |
| 3D cab | `.mmd` `internaldata:`, `cab<n>model:` |
| `.sms` | `.mmd` `sounds:` |
| One file per livery, by habit | One type, skin chosen per placement (`.mat`) |
| Folder of `.eng` files | Folder of types, with `textures.txt` |

**Decided (the user, 2026-11-09): no ENG files. `Eng` reads the `.mmd` and
`.fiz` directly.** Task driving 01 already made `VehicleDefinition` the
interface: nothing in `Eng`, the editors or the simulation reads an ENG file,
only that struct. A second way of filling it is all this needs.

Generated files would be side files (TSRE's rule against them), about 3,500
of them, a step between dropping the folder in and it working, and custom
ENG files that would confuse Open Rails in a game root it shares with TSRE.

A hand-written `.eng` naming an `.e3d` in `Shape ( )` keeps working on its
own, since shapes are chosen by extension. It cannot name a livery.

### What `.mmd` is, and adding to it

A `.mmd` is plain text, read as a stream of tokens: `key: value` pairs in
sections (`models:` ... `endmodels`, `sounds:` ... `endsounds`, `locations:`
... `endlocations`, `internaldata:` to the end of the file), with `include`,
`//` comments and `{ }` blocks. MaSzyna takes one token at a time and compares
it with the keys it knows in that section; **a token it does not know is
passed over** (`vehicle/DynObj.cpp:5209-5213`, `:5905`, `:6523`, `:6617`,
`:6976`, `:6980`).

So an extra value is one more pair, inside the file, and MaSzyna goes on
reading it as before:

```
tsre_somekey: 1.5
```

A `tsre_` prefix keeps it from matching a key of theirs; the only rule is
that a value must not be one of MaSzyna's own keys. The other files are as
open: `.fiz` ignores lines it does not know (and any line holding a `#`),
`.mat` ignores an unknown key with its value or block, and `.e3d` skips
chunks it does not know. Extra TSRE data therefore needs no side file.

## Design

### Trainset subfolders

MaSzyna keeps vehicles in group folders (`dynamic/pkp/`, `cd/`, `road/`...),
and references between them rely on that (`dynamic/pkp/303e_v1/...`,
`../en57_v1/...`). TSRE gets the same for all content.

`TRAINS/TRAINSET/` may hold one level of group folders (the user's rule,
2026-11-09). No list file, found by looking:

- A **vehicle folder** directly holds an `.eng`, a `.wag`, or an `.mmd` with
  its `.fiz`. The scan stops there, so `CABVIEW`, `SOUND`, `OPENRAILS`,
  `skins` and model folders are never searched.
- A folder with none of those is a **group**: its subfolders are checked once
  for vehicle files, and the scan goes no deeper. So a vehicle is at
  `TRAINSET/<vehicle>/` or `TRAINSET/<group>/<vehicle>/`.
- That is all MaSzyna needs: 1,451 of its 1,453 types are at
  `dynamic/<group>/<vehicle>/`, the other two in a `cfg/` folder inside a
  vehicle folder.
- A folder holding vehicles costs nothing extra. A folder without any costs
  one directory read per subfolder: none on MSTS, 2 on the Starter route, 245
  on OR_PSK beyond today's 333 (nearly all `common.cab` and `common.snd`),
  and none of those subfolders turns out to hold vehicles.
- The consist's folder field is the path from `TRAINSET`, with `/`:
  `EngineData ( name "pkp/et22_v3" )`.

Then the user drops the contents of `dynamic/` into `TRAINS/TRAINSET/`, whole
or a single `pkp/et22_v3`, keeping its group folder.

It works for MSTS-format vehicles too, and Open Rails takes it: it builds
`TRAINS/TRAINSET/` + folder + `/` + name (`Simulator.cs:1311-1312`,
`Orts.Menu/Consists.cs:140`), so a nested folder is just a longer path.

Considered:

- *Several trainset folders in `TRAINS/`* (`TRAINS/DYNAMIC/...`, detected or
  listed). `.con` would need `../DYNAMIC/pkp/...`, which Open Rails would
  take only through `..`; `[[Trainset]]` would have several meanings; every
  place that names `TRAINSET` would need the list. Detecting by "any folder
  but `CONSISTS`" would also catch whatever else lies there.
- *A list file in `TRAINS/`:* a side file, which Open Rails would not read.

What changes:

| Where | Change |
|---|---|
| `EngLib::loadAll` | The scan above |
| `Consist.cpp:407`, `:462` | `epath` is the path from `TRAINSET`, not the last folder |
| `ShapeDisplays.cpp:181-240` | `documentsFolder`, `roots`, `picturesFolder` assume `TRAINSET/<dir>/`: they find the vehicle folder by walking up instead |
| `CELoadWindow.cpp:217` | A label counting folders; counts groups now, harmless |

`Consist`'s loading, `VehiclePreload`, `ConsistFileHasEngine` and
`ContentCase` join folder and name already, so they take nested folders as
they are.

### Finding vehicles

`EngLib::loadAll` also takes `*.mmd`. A vehicle type is an `.mmd` with a
`.fiz` of the same name and `Category=train` (leaves out road vehicles, the
ship, and includes such as `cab0.mmd`).

Each folder's `textures.txt` is read once and gives the rows:

- **One `Eng` per livery** (the user, 2026-11-09), as today's one `.eng` per
  livery, and as Open Rails' unstable versions allow sharing shapes between
  such ENGs. 3,504 rows if the whole pack is copied. A row's name is the
  catalogue's (`ET22-003`, with the folder), so the existing search and
  filters work on it.
- A type with no liveries in the catalogue gets a single row with no skin.
  Liveries absent from the catalogue (143 of 3,504) are not listed, and are
  reported per folder.

### Reading: `MaszynaText`, `MaszynaVehicle`

`MaszynaText` (new, `src/tsre/maszyna/`) is the tokenizer for `.mmd` and
`.fiz`: one in-memory copy per file, tokens as views into it (no string per
token), cp1250 decoded only when a value is kept, `include ... end` with
`(pN)` parameters, comments, `;` separators, `[ a b c ]` taking the first.
Each file's text is cached by path and modification time, because includes
are shared widely (937 of 1,453 pairs use them).

`MaszynaVehicle` (new) holds what one type says:

- From `.fiz`, stopping at the sections below (first value of a key counts,
  as in MaSzyna): `Param.`, `Dimensions:`, `Wheels:`, `BuffCoupl.`,
  `Engine:`, `Power:`, `Load:`, `Doors:`. The file's path is kept for the
  physics task.
- From `.mmd`: `models:` to `endmodels` (model files, `#`, animation counts
  and prefixes, `pantfactors:`, `lowpolyinterior:`, `attachments:`) and
  `locations:`. Cab model names and driver positions from `internaldata:`
  are kept as text for later. The remainder is not tokenized. `tsre_` keys
  are read wherever they appear.

Read once per type, shared by its livery rows and by every car made from
them, never per placed car.

### Filling `VehicleDefinition`

`Eng::load` sees the `#` in its name (below) and calls `MaszynaVehicle::read`
instead of `VehicleDefinition::read`. The result is a `VehicleDefinition`
per livery row, sharing the type's data. New members: `maszyna`
(`shared_ptr<const MaszynaVehicle>`, null for MSTS content) and `skin`.

| Field | From |
|---|---|
| `engName` | `<type>#<skin>`, the consist's name for it (`ConListWidget` compares the two) |
| `displayName` | The catalogue's name for the livery, with the folder; else type and skin |
| `searchKeywords` | Catalogue metadata: series, depot, overhaul |
| `type`, `engType`, `wagonTypeId` | `EngineType`: series and induction 4; diesel, diesel-electric and `DumbDE` 5; `Dumb` and `WheelsDriven` by the catalogue's kind letter (`p` gives 6). None: 1 if `LoadAccepted` has passengers, else 2 |
| `shape`, `shapePath` | `models:`, its name with `.e3d` |
| `freightAnims` | `lowpolyinterior:` and `attachments:` as extra shapes, offset 0: `Eng` already draws such shapes with the car |
| `size` | `Dimensions:` W, H, L |
| `massKg` | `M` |
| `wheelRadius`, `driverWheelRadius` | `D / 2` |
| `couplings` | `CType`, `r0` 0, so `length()` is `L`, buffers touching |
| `maxSpeedMps`, `powerW` | `Vmax` (km/h), `PWR` (kW) |
| `resistance` | The existing default from the mass |

`document` stays null for these definitions. `number()`, `text()` and
`files()` already handle that, giving their defaults as for a missing key.
`ok()` and the problem report in `Eng::load` need to be made safe for it.

`Eng` puts the livery in the shape's state
([task 01](01-e3d-models.md#materials-and-liveries)): `poseShape`, right after
`newState`, calls `setRepaint(state, skin)`. A car already has a state of its
own, and every livery of a type shares one shape. The extra shapes are drawn
with state 0 today (`pushRenderItems`, `pushRenderItemBetween`), so on a
MaSzyna vehicle each gets a state too, made and released as `posedState` is.

### Consists

`.con` gains no block. A MaSzyna item's name is `<type>#<skin>`:

```
Engine ( UiD ( 0 ) EngineData ( "201e#201e-037" "pkp/et22_v3" ) )
```

`#` marks it (`<type>#` with no skin), so no file check is needed; `#` occurs
in no type or livery name in the pack. The places that add `.eng` or `.wag`
add `.mmd` for such a name: `Consist` (three), `VehiclePreload`,
`ConsistFileHasEngine`, `ContentCase`. A powered vehicle is written as
`Engine`, the rest as `Wagon`.

`Eng::name` is `<type>#<skin>.mmd`, so `Consist`'s
`eng->name.section(".", 0, -2)` gives the consist's name unchanged, and
`Eng::pathid` carries the skin, so `EngLib` keeps one `Eng` per row.

**In other programs** such a consist does not work, and must not hurt
(read from their code):

- Open Rails reads the line whole: `#` starts a comment only as the first
  character of a token, and never when a string is expected
  (`STFReader.cs:1804`).
- Open Rails' menu looks for `<name>.eng`, finds none and leaves the consist
  out of its list (`Orts.Menu/Consists.cs:43-49`, `:98-100`).
- If it is run anyway, each missing vehicle is a logged warning and is
  skipped (`Simulator.cs:1322-1329`). A consist mixing real and MaSzyna cars
  runs without those cars. With no driveable car left, loading stops with
  "Can't find player locomotive" and an error dialog (`Simulator.cs:791`,
  `GameStateRunActivity.cs:267`), not a crash.
- Old TSRE (v0.7.6): `Eng::load` logs that the file does not exist and
  returns, the `Eng` stays not loaded and of size 0, and `Consist::isBroken`
  reports the consist, as for any missing car. `ParserX` reads the name
  whole, quoted or not (up to whitespace or `)`).

### Files outside the vehicle's folder

MaSzyna's shared folders go in the game root (the user, 2026-11-09):

| In MaSzyna | In the game root |
|---|---|
| `dynamic/` | `TRAINS/TRAINSET/` (`[[Trainset]]`) |
| `textures/` | `TEXTURES/MASZYNA/` (`TEXTURES` is new, with a root `[[Textures]]`: one row in `SimisDocument`'s table) |
| `sounds/` | `SOUND/MASZYNA/` (`[[Sound]]`) |

`GLOBAL` stays Open Rails' shared folder. The MaSzyna folders are not copied
whole, since much of them will never be used.

How the content writes a name does not matter, because TSRE does the looking
itself. Both forms occur, and they end up in the same place:

| Written as | Count | Found at |
|---|---|---|
| Inside the vehicle's folder, or `../` | | As written; the subfolders keep them right |
| `dynamic/pkp/303e_v1/x` | | `[[Trainset]]/pkp/303e_v1/x` |
| `tabor/x`, left to the lookup | 7,230 in `.e3d` | `[[Textures]]/MASZYNA/tabor/x` |
| `textures/tabor/x`, `/textures/tabor/x` | 7,442 in `.e3d` and `.mat` | The same; `textures/` is dropped |
| `silence1.wav`, `sip/stacja.wav` | 140,247 in `.mmd` | `[[Sound]]/MASZYNA/...`, by name without its extension (`.mmd` says `.wav`, the file is `.ogg`) |

No sound is written with `sounds/`.

A leading `/` means nothing: MaSzyna removes it before looking
(`model/material.cpp:554`, `model/Texture.cpp:1700`). Every form is tried in
the vehicle's folder first, so TSRE keeps that order: the name as written
inside the vehicle's folder, then the shared folder. All 7,442 names written
with `textures/` are in practice shared ones: none is found in a vehicle's
folder.

Open Rails opens `SOUND` files by exact name and never lists the folder
(`Orts.Content/MSTSPath.cs:88-129`), so a subfolder there does not disturb it.
No game root here has a `TEXTURES` folder. `--contentcase`'s lists of fixed
folder names take `TEXTURES` (`ContentCase.cpp:583`,
`ContentCaseExecution.cpp:425`).

What that means in practice (counted from the pack):

- `et22_v3` needs 23 files from `textures/` (19 MB) and one from `303e_v1`,
  which needs that folder in the trainset too.
- All vehicles need 698 files (927 MB) from `textures/`: `tabor` 403,
  `ladunki` 143, `posers` 106, a few others.
- `sounds/` holds nearly every vehicle sound (3,994 files, 89 MB, used by 226
  of 229 folders). Needed from the sound task on, and not by this one.
- `data/` has three kinds of small files (load weights, sound overrides, smoke
  definitions) that the physics and sound tasks will decide about.
- `models/` and `scripts/` are not needed: vehicles' load models are in their
  own folders, and `scripts/` is Python cab screens.

The [review](reports/rolling-stock-format-review.md#maszynas-main-folders)
lists every main folder.

A texture not found is drawn in the missing-texture colour and reported once
per vehicle with the paths tried, so it is clear what to copy. A small script
(`scripts/`) can list or copy exactly what the vehicles in `TRAINSET` need.

### Moving parts

`CarAnimation` finds parts by Open Rails' name rules and plays key frames;
MaSzyna has neither. It gets a second constructor taking a rig from
`MaszynaVehicle`, which fills the same fields where it can:

| Part | Rig |
|---|---|
| Wheels | Prefix + 1..n, any case; radius from `D` |
| Bogies | `bogie1/2`, else `boogie01/02`; pivots from their rest matrices |
| Pantographs | Five parts each; computed |
| Doors | By `DoorOpenMethod` and `DoorMaxShiftL/R` in the `.fiz`: shift, turn, fold, plug; computed |
| Door steps, mirrors, wipers | Computed |
| Buffers | Named `buffer_left/right01..`; left at rest here (they follow the couplers) |

- Wheels and bogies use the code paths `CarAnimation::pose` has
  (`setPartTransform` over `rest × turn`), so `Eng`'s placing on bogie pivots
  and the wheels' travelled distance work unchanged.
- The two-state groups (`Group` in `CarAnimation`) today move parts through
  key frames. Rig groups compute a local transform from the group's progress
  (0 to 1) instead: door shift, pantograph arm angles. The formulas are
  written afresh from the behaviour in the review, not copied.
- Pantograph angles come from each pantograph's measured arm lengths, with the
  type's ratios, as in the review.
- The direction of each rotation is checked on captures in the viewer
  (Animation menu), since a sign is easy to get wrong through the root's turn.

### Viewers and editors

These work through `Eng::definition` once `Eng` exists; the changes are:

- Shape Viewer: `.mmd` opens as an `Eng` (its first livery); the file lists
  take `.mmd` and `.e3d`.
- Consist Editor: the list above, and what a vehicle is made of (below).
- The info widgets show `displayName`, sizes and mass as for any vehicle.

### Vehicle files

Engine > Vehicle files and Consist > Train files open one dialog for any
vehicle, ENG or MaSzyna (`VehicleFiles`, `VehicleFilesDialog`): its files as a
tree under the main folders of the game they are in, the vehicle's folder
first. It lists the definition and its includes, shapes and their `.sd`,
materials, every texture (MaSzyna's: only the row's livery), `.sms`, `.cvf`.
Not yet the `.wav` files inside an `.sms` or the images inside a `.cvf`.

- A file not there is marked, where it belongs, so the same dialog on another
  machine shows what still has to be copied.
- Text data opens in the external editor. This replaces Open in external
  editor and Open legacy ENG: the vehicle's own file, when its `OPENRAILS`
  copy is the one read, is a line of its own, marked as not read.
- Archive... writes a zip with paths from the game root, to extract in
  another game root. Missing files and files outside the game root are left
  out, and said so.
- A catalogue line whose `.mat` is not there gives no row, so a partial copy
  lists only what it has. ET22-037 alone is 18 files, 39 MB.

### Until the physics task

AI trains run as any vehicle does, on the definition's mass, length, power
and speed. `SimulationController::drivable` refuses a train whose cars have
`maszyna` set: Drive gets the real model in the next task, not the stand-in.

## Steps

Each step ends with `build all`, `ctest` and the app suites.

1. **Trainset subfolders.** Useful alone. The scan, `epath`, the displays'
   vehicle folder; suite on a scratch game root (symlinks, nothing written in
   the user's content): MSTS vehicles in a group folder listed, a consist
   saved and read back with its nested folder, the same list as before for
   existing content; a nested consist in Open Rails.
2. **`MaszynaText`.** Suite `maszyna-text` (includes, parameters, comments,
   sets, cp1250); corpus: every `.mmd` and `.fiz` in the pack.
3. **`MaszynaVehicle` and the definition.** Suite with small files, and
   `maszyna-vehicle-corpus --game-root <pack>`: all 1,453 pairs read, every
   model found, every catalogue line matched to a type, what is not listed
   in one report; time in Release.
4. **`Eng`, `EngLib`, `.con`, `[[Textures]]`.** `pkp/et22_v3` in the scratch
   game root: vehicles listed, a `.con` saved and read back, consist drawn;
   with and without `TEXTURES/MASZYNA`.
5. **Shape Viewer** opening `.mmd`, liveries shown; capture of a loco and a
   carriage, with `lowpolyinterior`.
6. **Rig.** Wheels and bogies on a moving consist in the Route Editor;
   pantographs and doors through the viewer's Animation menu; suite on rig
   finding (names, any case, missing parts reported once).
7. **Whole pack.** Consist Editor start time with the whole pack in
   TRAINSET, in Release; list usable; a problem report per folder.

## Results (2026-11-09, Release)

- `maszyna-vehicle-corpus`: 229 folders, 1,451 type pairs (the other two of
  the review's 1,453 are in a `cfg/` folder inside a vehicle folder), 1,388 of
  them railway vehicles, 3,692 rows, in 0.7 s. No include missing, every
  vehicle with a mass and a length.
- The whole pack as a trainset: 3,692 rows listed and loaded in 0.4 s (texts
  already in the cache; 0.7 s reading them too).
- Names with capitals: MaSzyna lower-cases every reference, so a file with
  capitals is not found on Linux. 213 such names in `~/ORTS/maszyna`'s
  `dynamic/`, `textures/` and `sounds/` were renamed to lower case (the user,
  2026-11-09; no collisions); `--contentcase` does not read `.mmd`, `.e3d` or
  `.mat`.
- A model not found for 9 types: 5 name their model by the placed vehicle
  (`(p1)`) and have no row in their catalogue, 4 models are not in the pack
  (`sa134_v1` 3, `ep09_v1` 1). 6 catalogue lines have no type, all under
  `road/`, which is not railway stock.
- 210 liveries are not in a catalogue: not listed, told per folder.
- A placed vehicle's name `(p1)` and the skin `(p3)` in an `.mmd` are filled
  from the row's livery.
- Suites: `maszyna-vehicle`. Captures: ET22 as `Eng` with its low-poly
  interior, pantographs raised, a multi-slot wagon livery (`441w`), sliding
  doors (`en57`), a consist of two liveries.
- On a route: an ET22 and two `441w` wagons as the player's train and as an AI
  train at 77 km/h on USA2, on the rails at the right height. Wheels and
  bogies are checked by the suite (the direction a wheel rolls, a bogie's turn
  through the model's frames); a still capture cannot show them turning.
- Written, not yet checked on a model: plug, fold and rotating doors, door
  steps, mirrors, wipers. Sliding doors and pantographs are.
- Not done: a nested consist in Open Rails (only read from its code).

## Done when

`pkp/et22_v3` dropped into `TRAINS/TRAINSET/` shows its vehicles in the
Consist Editor; a consist made of them saves, loads and draws on a route with
turning wheels and bogies, its livery, and correct length; the Shape Viewer
opens its `.mmd` and models.

## After this set

Physics and simulation: the mover as a native model (roadmap of 2026-10-24).
Then lights and couplers states (`_on`/`_off` pairs), cabs and their
controls, sounds. The cab light switch and the carriage lights set the
low-poly interior's section lights (`ComplexShape::setSectionLight`, unlit
until then: [task 01](01-e3d-models.md#lit-sections-the-user-2026-11-10)).
