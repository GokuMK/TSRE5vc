# Task 01 - Vehicle definitions (the ENG refactor)

Status: done on `feature/vehicle-definitions` (2026-10-28), not merged.

The roadmap's first driving task (decided 2026-10-17: before any driving
task): one vehicle definition per ENG/WAG file, read once and not changed
after loading. It holds the file's whole block tree, so scripts can read
any parameter (Open Rails' additions too), and typed SI fields for what C++
uses. It is read with `SimisTextReader`, not by extending `ParserX`. The
editor's `Eng` and the simulation both read it by const pointer. The
encoding policy was deferred to this task (2026-10-22): MSTS wrote UTF-16
only, TSRE added ASCII, community content also has UTF-8 with or without a
byte order mark.

## TSRE today

- **`Eng`** (`src/tsre/trains/Eng.cpp`, 1,474 lines) is three things in one
  class:
  - the file's data, read by `Eng::load` with `ParserX` (about 40 keys, 260
    lines);
  - one car's state: its place on the track, drawing, animation, lights,
    sounds;
  - old play-mode code: `Ruch`, `PlayDriver`, `TrainNetworkEng`.
- **`EngLib`** keeps one `Eng` per file, by its path key. The Consist Editor
  loads every file under `TRAINS/TRAINSET` (`loadAll`); the route editor
  loads the files its consists name.
- **A consist copies the file's data for each car.** `new Eng(lib->eng[id])`
  copies some 35 fields; only the lights block and the 3D cab are shared
  (`shared_ptr`).
- **The simulation** (`MakeInput`, main thread) copies type, mass, force,
  power, top speed and resistance into `Sim::Vehicle`. The resistance
  becomes Davis coefficients: from `ORTSDavis_*`, else converted from
  MSTS's `Friction`, else Open Rails' default formula from the mass.
- **`ParserX`** reads from a `FileBuffer`:
  - it converts the text to UTF-16, and skips a UTF-8 byte order mark (a
    stop-gap, bc80f6f);
  - it splices includes into the buffer (`FileBuffer::insertFile`), looking
    first in the `OPENRAILS` folder, then in the vehicle's folder;
  - it reads numbers through the units task's `FileNumberReader`.
- **`SimisTextReader`** is the bounded lexer that the shape, sound, sigcfg
  and environment readers use. Its `decode` takes UTF-16 LE or BE with a
  byte order mark, else UTF-8, else Latin-1.
- **`EnvNode`/`EnvText`** keep an environment file as a token tree that
  writes back unchanged. That costs one `QString` and one `QVector` per
  token: fine for editing a few files, too heavy for every vehicle.
- `Eng::reload` reloads the shapes only, never the ENG file.

**Open Rails** is the behavioural reference. It was checked in both
`openrails-linux-spike` (2026-10-03) and upstream master `8aafb3147`
(2026-09-29):
- **It reads about 460 distinct paths** (`wagon(…` and `engine(…`) across
  its rolling stock classes and subsystems:
  - 287 are read as numbers, 204 of them with a unit;
  - 55 are blocks (curves, tables, subsystems);
  - the rest are strings or are read in other ways.
- **It reads a file as a stream.** For each item it dispatches on the
  item's path, for example `Parse("wagon(coupling(spring(r0")`.
  - A key given twice takes the last value, so an override file can
    re-open `Wagon ( ... )` after including the original.
  - A few blocks add rather than replace: each `Coupling` adds a coupler.
- **Include paths** (`STFReader.cs`: `Path.GetDirectoryName(FileName) +
  "/" + filename`):
  - A path is relative to the folder of the file that holds the include. A
    nested include is relative to its own file.
  - The only special name is `[[SameName]]`: it is replaced by the name of
    the file holding the include, as in `include ( ../[[SameName]] )`.
  - There are no roots and no absolute paths.
  - A missing file is a warning, and reading goes on without it.
- **The first line of every file is taken as its SIMIS header and never
  read**, include files too. The manual tells authors to leave it blank
  before an include.
- **Comments:**
  - `comment ( ... )` and `skip ( ... )` blocks are ignored;
  - a token starting with `#` or `_` is ignored, together with the item
    after it.
- **Encoding:** text goes through `StreamReader`, which detects the byte
  order mark; a file without one is read as UTF-8.
- An `OpenRails` folder beside the file overrides it.

**Content on this machine:**

| | Vehicle files | Encodings | With `include` | Tokens: per vehicle / each file once |
| --- | --- | --- | --- | --- |
| MSTS | 108 | UTF-16 with byte order mark | 0 | 55,000 |
| TS_STARTER_ROUTE | 100 | UTF-16 with byte order mark | 2 | |
| OR_PSK | 4,350 | 4,287 UTF-16, 63 ASCII | 2,486 (531 in `OPENRAILS`) | 12.2 M / 3.0 M |

- **What OR_PSK's includes point at:**
  - 1,443 distinct include files; `Standard_Coupler_PL.inc` alone is
    included by 1,341 vehicles.
  - 15,821 of its 16,291 include lines point into
    `TRAINS/TRAINSET/common.inc`, nearly all as `../common.inc/…`.
  - None leaves the game folder, and none uses `[[SameName]]`.
- **What is broken:**
  - 418 include lines name a file that doesn't exist. Most come from copies
    moved one folder deeper (`PCC_Fals/stare/`), so their `../` no longer
    fits.
  - 139 files have unbalanced parentheses, 5 of them include files.
- **First lines:** 936 include files start with a comment on their first
  line, which Open Rails drops anyway. Others hide a mistake behind Open
  Rails' rule:
  - 12 start with a comment whose quote never closes, for example
    `comment ("Size, InteriaTensor, … SM31)`; 8 of them are included;
  - 6 start with a line of stars, dashes or junk, for example
    `b  ß00 hbcomment (…`.

  One, `SA134/Agregat_SA134_B.inc`, which no vehicle includes, opens a block
  on its first line. Open Rails drops that line, so the block's `)` would
  close the including `Engine` block.
- **Size:** the median OR_PSK vehicle is 365 tokens; the largest is 124,000
  tokens with its includes.

## Design

### New code, and what it replaces

| | Where | Why new |
| --- | --- | --- |
| `SimisTree` | `src/tsre/fileFunctions/`, beside `SimisTextReader` | A compact read-only tree; `EnvNode` costs about 5 times the memory. |
| `VehicleDefinition` and its reader | `src/tsre/trains/`, beside `Eng` | What `Eng::load` and `Eng`'s file fields become: moved, not a parallel copy. It has no `Game`, renderer or editor dependencies, so the simulation thread and test executables can use it. |
| The key catalogue | `src/tsre/trains/`, a table | Every key's quantity, so Lua gets SI values (see below). |
| Include roots | in the reader | The user's request (below). |

- **`EngLib` stays the library.** It keeps one definition per file instead
  of one `Eng` per file.
- **No new folder.**
- **`SimisTextReader::decode` gets the encoding policy.**
- **`EnvNode` stays** for files TSRE edits and writes. It could be built
  from `SimisTree` later.

### The tree

- **One decoded text and one flat array of nodes per file.** Each node is a
  value or a block, holding its offset and length in the text, its first
  child, its next sibling and its line. That is about 20 bytes a token,
  against about 100 for `EnvNode`. All of OR_PSK would take roughly 60 MB
  of nodes and 36 MB of text, if each file is read once (an estimate, to
  be measured in step 5).
- **Shared includes (user, 2026-10-28):**
  - Each file is read once into its own tree and shared by const pointer.
    Its include items stay as nodes with their written path.
  - Each definition resolves its own includes. A path can depend on the
    vehicle (`[[Vehicle]]`, below), so the same shared file may lead to
    different files in different vehicles.
  - The shared files are cached by path and modification time. A changed
    file is read again into a new tree, while definitions made before keep
    the old one.
- **An include that doesn't balance its parentheses can't be a subtree.** It
  is inlined into the including file instead, as Open Rails does, and
  reported. OR_PSK has 5 such files.
- **The first line** (the user's, 2026-10-28). Open Rails never reads a
  file's first line, taking it for the SIMIS header; TSRE reads it unless it
  is one:
  - a SIMIS header is skipped, a stray character before it too
    (`<SIMISA@@…`);
  - a line that doesn't read alone is skipped and reported: one that leaves
    a quote open or closes a block it didn't open. Content made for Open
    Rails can hide such a line there, like the broken comments above.
  - anything else is read. Data found there is reported, since Open Rails
    reads that file differently.
- **Open Rails' other rules, applied when the tree is built:** `comment`
  and `skip` blocks, and `#` or `_` tokens with the item after them, are
  left out, so scripts never see them.
- **Lookup by path, as Open Rails dispatches.** For example,
  `last("wagon(mass")` gives the last such item through every include, and
  `all("wagon(coupling")` gives each one in order.

### Include paths and file references

**Open Rails' form stays as it is**: a path relative to the file that holds
the include, with `[[SameName]]` replaced by that file's name.

TSRE's current fallback is dropped (user, 2026-10-28). Today, when an
`OPENRAILS` copy's include isn't found, TSRE also tries the vehicle's
folder. Names are exact, by the content-case rule.

**Named roots (user's request; form and set agreed 2026-10-28)** let
authors write where a file is instead of counting `../`. They use Open
Rails' bracket form, as in `[[SameName]]`, and their names are matched
without regard to case:

| Root | Folder |
| --- | --- |
| `[[Trains]]` | `<game>/TRAINS` |
| `[[Trainset]]` | `<game>/TRAINS/TRAINSET` |
| `[[Vehicle]]` | the vehicle's own folder: where its ENG/WAG file is, the original's folder for an `OPENRAILS` copy, even inside a shared include |
| `[[Here]]` | the folder of the file holding the include (the same as no root, for clarity) |
| `[[Global]]` | `<game>/GLOBAL` |
| `[[Sound]]` | `<game>/SOUND` |
| `[[Game]]` | `<game>` |

For example, `include ( "[[Trainset]]/common.inc/Standard_Coupler_PL.inc" )`
reads the same from any folder depth, so copies moved deeper keep working.
`[[Vehicle]]` lets a shared include pull in a per-vehicle file, such as
`[[Vehicle]]/Lights.inc`.

- **Safety:** every include, with or without a root, must resolve inside
  the game folder. A path that leaves it is refused and reported. None in
  the local content does.
- **Open Rails compatibility:** Open Rails doesn't know these roots. It
  would look for a folder literally named `[[Trainset]]`, warn, and go on
  without the file. Roots are a TSRE feature (user, 2026-10-28): content
  using them loads in TSRE only, and the feature could later be offered to
  Open Rails as a pull request.

**The same roots in the vehicle file's other paths (user, 2026-10-28).**
Without a root, these are found as Open Rails and TSRE find them today;
with a root, the root gives the folder and no other folder is searched:

| Key | Found without a root in (Open Rails' rule) |
| --- | --- |
| `Wagon ( WagonShape )`, `FreightAnim`, `ORTSFreightAnims ( … Shape )` | the vehicle's folder |
| `Wagon ( Sound )`, `Engine ( Sound )` | the vehicle's `SOUND` folder, then `<game>/SOUND` |
| `Engine ( CabView )` | the vehicle's `CABVIEW` folder |
| `ORTS3DCab ( ORTS3DCabFile )` | the vehicle's `CABVIEW3D` folder |

For example, `Sound ( "[[Trainset]]/common.snd/EU07.sms" )`. The definition
holds each of these as the full path it resolved to, so the editor, the
sounds and the simulation all use one rule. The SMS and CVF files' own
references (WAV files, cab images) are not part of this task.

### Encodings (agreed 2026-10-28)

The policy lives in `SimisTextReader::decode`, so every reader shares it:
- **Accepted:** UTF-16 LE or BE with a byte order mark; UTF-8 with or
  without one (ASCII is UTF-8).
- **Accepted and reported:** UTF-16 without a byte order mark, recognised
  by zero bytes in its first characters.
- **Anything else** is read as Latin-1, as now, and reported, naming the
  file.

Problems go to the editor's error list (`ErrorMessagesLib`, as `SigCfg`
reports), naming the file and line, once per file.

### Every key to Lua: the key catalogue

**Every key in the file reaches Lua through the tree**, including keys
neither TSRE nor Open Rails reads.

To get SI values, the reader must know each key's quantity: `MaxForce` is a
force, `MaxVelocity` a speed, and a bare number in each has a default unit.
The **key catalogue** is a table with one entry per path, for example
`engine(maxforce` → Force, newtons by default. It is generated from Open
Rails' parse code (the 460 paths, their units and defaults) and checked by
hand where the code reads several values or a table. Lua then gets
`engine.maxforce` in newtons without naming a unit. A key that isn't in the
catalogue comes as text, or as a plain number on request.

**Typed C++ fields** stay for what C++ code reads: today's 40 keys, then
what each physics task adds. They are written as the code needs them, from
the same catalogue.

The catalogue also lets the error list name unknown keys, which are often
typos. That report is a later option, not part of this task.

How Lua sees the tree (tables, proxies, lists for repeated blocks) belongs
to the loco script task. This task gives it the lookup and the catalogue.

### The definition

`VehicleDefinition` is made once by the reader, never changed, and held by
`std::shared_ptr<const VehicleDefinition>`. It holds:
- **Its files:** the file read and each include it resolved. The file read
  is the `OPENRAILS` copy when `core.content.loading.preferOpenRailsEng` is
  on and the copy exists, else the original.
- **The tree, with the definition's include resolutions.**
- **Today's typed fields in SI, unchanged in meaning:**
  - names and search keywords;
  - shapes and freight animations;
  - size, mass and wheel radii;
  - couplings;
  - type and the engine's type;
  - the brake system;
  - force, power, current and top speed;
  - Davis and Friction;
  - passenger capacity;
  - sounds;
  - the 3D cab;
  - the lights block, kept as its text as now.
- **The resistance as Davis coefficients, worked out once,** with where they
  came from: given, converted from Friction, or Open Rails' default from the
  mass. `MakeInput`'s `resistanceOf` moves here.
- **The problems found while reading it.**

The typed fields follow Open Rails' rules: the last value wins, and
couplings add.

### Who uses it

- **`EngLib`** holds one definition per file key, and makes a new one when
  a file is reloaded.
- **`Eng`** keeps one car's state and holds its definition's pointer. The
  copy constructor shares the definition instead of copying fields. Call
  sites read `def->massKg` and so on instead of `massKg`, in about 25
  files.
- **Reload** in the Consist Editor and the Shape Viewer reads the ENG file
  again into a new definition, as well as the shapes. Cars made before keep
  the old one.
- **`Sim::Vehicle`** shrinks to the definition's pointer, its flip, its car
  id and its `EngLib` id for drawing. The simulation thread reads the
  definition directly. A running simulation keeps the definitions it
  started with until it ends.
- **Not touched in this task:**
  - The play-mode code in `Eng` (`Ruch` for play, `PlayDriver`,
    `TrainNetworkEng`, `updateSim`) stays where it is (user, 2026-10-28:
    leave it or move it to a better place). The driving tasks replace it:
    the player's train runs in the simulation thread, and the external
    link moves there.
  - The consist and sound readers.
  - Physics, which is the next driving task.

## Steps

1. **`SimisTree`, `SimisDocument` and the encoding policy** (done
   2026-10-28, below).
   - The reader covers:
     - shared and inlined includes;
     - `[[SameName]]` and the roots;
     - the game-folder check;
     - Open Rails' first-line and comment rules.
   - A new suite, `vehicle-file`, has fixtures for:
     - every encoding;
     - nested, missing, cyclic and unbalanced includes;
     - each root, and a path that leaves the game folder;
     - a per-vehicle file through `[[Vehicle]]` in a shared include;
     - the first-line and comment rules;
     - last-wins and couplings that add;
     - units.
2. **The key catalogue** (generated from Open Rails, checked by hand) **and
   `VehicleDefinition`'s typed fields**, with a comparison tool (done
   2026-10-28, below).
   - Fixtures: the roots in shape, sound and cab paths and the default
     lookups without them; the `OPENRAILS` override.
   - Every ENG/WAG file on MSTS, TS_STARTER_ROUTE and OR_PSK is read by both
     the old `Eng::load` and the new reader, and the shared values
     compared.
   - Each difference is explained: either Open Rails' rules where `ParserX`
     differed, or a bug fixed.
3. **`Eng`, `EngLib`, `Consist` and the editors on the definition;**
   `Eng::load`'s `ParserX` code removed (done 2026-10-28, below).
4. **The simulation on the definition** (done 2026-10-28, below).
   `Sim::Vehicle` shrinks. The checks:
   - the seven MSTS routes give identical stops and events;
   - all 266 PRUDNIK trains finish as before;
   - the simulation, timetable and activity suites pass;
   - the renderer captures with trains are unchanged.
5. **Reload, problems in the error list, and measures in Release** (done
   2026-10-28, below):
   - the Consist Editor loading all of OR_PSK, before and after;
   - memory with and without shared includes.

Each step is built (`all`) and checked with ctest before its commit, on
`feature/vehicle-definitions` in its own worktree.

## Step 1 done (2026-10-28)

**Code** (`src/tsre/fileFunctions/`):
- `SimisTree` holds one file's tree.
- `SimisDocument` holds a file with its includes, the roots, the
  game-folder check and the shared cache.
- `SimisTextReader::decode` holds the encoding policy. It gives a note for
  UTF-16 without a byte order mark and for Latin-1.
- `SimisTextReader` can start at an offset; `SimisTree` uses that to skip
  the header line.

**Fixed in `SimisTextReader::skipBlock`:** a quote now starts a string only
where an item starts, as `scan()` and Open Rails read it. MSTS's own
`US2GrainCar.wag` has `comment( 22.805t empty, 106.747t full" )`; skipping
that comment used to run to the end of the file.

**Open Rails' includes, checked in its code.** `STFReader.ReadItem` passes an
included file's items through the including file's tree, so an include's
extra `)` closes the block holding the include. OR_PSK's
`PR_E186-143_p1.eng` includes `Parametr_TRAXX_F140.inc`, which has one
extra `)`, inside its `Engine` block. So its train control include,
controllers, cab sound, cab view and engine name all fall outside the
block, and Open Rails reads none of them. TSRE now reads them the same way
and reports it.

**Suites:**
- `vehicle-file`: 25 checks on fixtures, content-free.
- `vehicle-file-corpus`: every ENG/WAG file under the game folder. It
  reports problems by kind, each counted once per file, plus time and
  memory.

**Measures (Release, this machine):**

| | Files | Time | Trees | Nodes | Tree memory | Process peak |
| --- | --- | --- | --- | --- | --- | --- |
| MSTS | 108 | 5 ms | 108 | 28,040 | 1.4 MB | 45 MB |
| TS_STARTER_ROUTE | 100 | 5 ms | 107 | 28,088 | 1.4 MB | 46 MB |
| OR_PSK | 4,350 | 556 ms | 5,788 | 1.52 M | 67 MB | 125 MB |

**Problems in OR_PSK** (each kind counted once per file; MSTS has one, a
stray `)` in `US2BNSFCar.wag`):

| Problem | Files |
| --- | --- |
| A `)` that closes no block | 109 |
| A block not closed at the end of the file (`Wagon` 33, `Engine` 11) | 44 |
| An include not found, mostly copies moved one folder deeper (`stare/`, `xhl/`) | 86 |
| An include whose parentheses do not balance, read in place | 22 vehicles (5 files) |
| Neither UTF-16 nor UTF-8: read as Latin-1 (Windows-1250 Polish in comments) | 28 |
| A first line leaving a quote open: not read (first-line rule as changed after step 5) | 8 |
| Data read on the first line: junk lines of stars, dashes, `b  ß00 hbcomment (…` (the same) | 6 |
| An unterminated quoted string (`\"` at a string's end, as Open Rails reads it too) | 2 |

## Step 2 done (2026-10-28)

**The key catalogue** (`src/tsre/trains/VehicleKeys`):
- `scripts/vehicle-keys-from-openrails.py` reads Open Rails' `case` labels
  and the reads in their bodies, and writes `VehicleKeysTable.inc`. It took
  463 paths from upstream `8aafb3147`:
  - 342 with values, in order;
  - 116 blocks that their own parsers read (curves, controllers, brake and
    power subsystems);
  - 5 that count only by being there.
- `VehicleKeys.cpp` maps Open Rails' 23 unit names to TSRE's quantities and
  the unit a bare number is in (`psi`, `psi/s`, `ft^3`, `l`, `mph`, `inhg`,
  `lb/h`, `kj/kg`, `min`, `h`; SI for the rest). No unit name is left
  unmapped (checked).
- The insides of the 116 blocks reach Lua as written until a physics task
  catalogues them.

**`VehicleDefinition`** (`src/tsre/trains/`) holds:
- today's typed fields, unchanged in meaning;
- the resistance, worked out once (`MakeInput`'s rule moved here), with
  where it came from;
- the file paths: shape, freight animations, sounds, cab view (newly read)
  and 3D cab shape. With a root they come from the root; without one, from
  Open Rails' places: the vehicle's folder, its `SOUND` then the game's
  `SOUND`, `CABVIEW`, `CABVIEW3D`.
- the lights block, written back as text by `SimisDocument::write` for
  `EngLightsBlock`.

**Also fixed in the tree:** a quote ends an unquoted item and is dropped
(`x.sms"`), as Open Rails reads it.

**Suites:**
- `vehicle-file`: 33 checks. New ones cover the definition (SI fields,
  roots and Open Rails' places, the `OPENRAILS` copy, the resistance, the
  lights) and the catalogue.
- `vehicle-definitions-compare`: reads every ENG/WAG with both readers and
  compares the fields.

**Comparison of the old `Eng::load` with the new reader:**

| | Files | Alike |
| --- | --- | --- |
| MSTS | 108 | 108 |
| TS_STARTER_ROUTE | 100 | 100 (sound paths alike once `\\` is `/`) |
| OR_PSK | 3,826 | 3,748 |

All 78 OR_PSK differences come from the old reader. In each case the new
reader does what Open Rails does:
1. **The old reader stopped at a `)` that closes no block at the top
   level**, losing the `Engine` block after it: ED78, ET41, EN57-857rb.
   Open Rails skips such a `)`.
2. **A `#` comment** such as `WagonEmptyWeight ( 26.8t ) #prazdny vagon`
   threw the old reader off. Its brakes, couplings and Davis values were
   lost: CDvagFalls, ZSSKCvagFalls.
3. **Junk on the first line of light includes** (SM42, SU42): `b  ß00
   hbcomment ("…")`. Its unknown words made the old reader skip the next
   block, the `Lights`. Open Rails never reads that line. TSRE now reads it
   too (the first-line rule after step 5), but only as harmless values, so
   the lights load: SM42-010 parses 22 lights either way.
4. **Broken quotes, tokenised as Open Rails does:**
   - PCC_Fals's unclosed `BrakeEquipmentType( "…` swallows its
     `BrakeSystemType`, in Open Rails too;
   - the PPS name;
   - CD130's sounds;
   - the EP05/EU05 freight shapes ending in `"`.
5. **Includes in blocks nobody reads** are still listed among the files.
   For example, EN57A's `Engine` sits inside its unclosed `Wagon`; all
   three readers lose that engine.
6. **BLG Laaers' nested `Sound ( Sound ( … ) )`** gives no usable sound in
   any reader.

**Time:** each definition read on its own, nothing shared, takes 3.4 s for
OR_PSK against `Eng::load`'s 1.5 s (Release). Held together, sharing their
includes, they took 0.56 s in step 1.

## Step 3 done (2026-10-28)

**What `Eng` keeps:**
- `definition`, a `std::shared_ptr<const VehicleDefinition>`, never null:
  an empty definition stands in for a car whose file isn't read yet.
- Its identity: `name`, `path`, `pathid` and `hashid`.
- Its per-car state.
- Its shape ids in each shape library (`shapeIds`, `freightShapeIds`,
  `cab3DShapeIds`).

**What changed:**
- **`Eng::load`** is `VehicleDefinition::read`; the 350 lines of `ParserX`
  reading are gone, and with them the `OPENRAILS` path fields.
- **The copy constructor** shares the definition instead of copying some
  35 fields.
- **Shapes, freight animations, sounds and the 3D cab** use the
  definition's resolved paths.
- **`Coupling` and `Cab3D`** are now the definition's types, so
  `Eng::Cab3D` still names them.
- **Readers in the editors** (Consist Editor, Shape Viewer, `Consist`) and
  the simulation's `MakeInput` read `definition->…`. That was 23 files,
  patched where the compiler pointed.
- **Tests that built an `Eng` by hand** now give it a definition of their
  own.
- **The content-path fixture's include files** got a blank first line
  here. After step 5 that was undone: TSRE reads a first line that isn't a
  header.
- **The comparison suite is gone** with the old reader. Its results are
  under step 2.

**Checks:**
- Build `all` and ctest (11 of 11) pass.
- These suites pass: `vehicle-file`, `eng-lights`, `cab3d`, `car-animation`,
  `content-path`, `units`, `units-content` (all of OR_PSK through
  `EngLib`), `consist-preview-gl`, `map-view`, `sound`, `rmlui`, `script`.
- So do all of these: `simulation-*`, `timetable-*`, `activity-check`,
  `activity-edit`, `activity-roundtrip`, `activity-ui`.
- Captures made with one settings file are identical to main's (rmse 0,
  no picking differences):
  - `simulation-trains.json` on EUROPE1: its 4 views;
  - `shape-viewer-views.json` on MSTS: 2 shapes, 2 engines, the 310 tender
    with its freight animation, and a consist.

## Step 4 done (2026-10-28)

**`Sim::Vehicle`** holds:
- the definition, shared with the editor's cars and read by the simulation
  thread;
- its flip, its car id and its `EngLib` id for drawing.

**What it reads from the definition,** through accessors:
- `length()`, the length with the coupler (it was `Eng::getFullWidth`, now
  `VehicleDefinition::length`);
- `type()`, `passengerCapacity()`, `mass()`;
- `force()`, `power()` and `maxSpeed()`, which keep the old rule: engines
  only;
- `davis(k)` and `davisLeading()`, which also keep the old rule: zero
  without a known mass.

**Also changed:**
- `MakeInput`'s copy of the resistance rule is gone; the definition works
  it out once.
- Tests build vehicles as before with `{length, flip, type}`, now a
  constructor that gives the vehicle a definition of its own.

**Checks** (main's binary and the branch's, with one settings file):
- **Seven routes** (EUROPE1, EUROPE2, JAPAN1, JAPAN2, USA1, USA2,
  BNSF_SCENIC): `simulation-route` with `TSRE_SIM_TRACE` and
  `TSRE_SIM_EVENTS` gives the same stops, waits, events and twice-simulated
  timetables. Only the timings differ, and the old reader's "converting to
  UTF16" lines are gone.
- **PRUDNIK** (`PSK_2026.timetable_or`, 266 trains): its 1,867 lines of
  stops and events are the same. Its three failing checks fail on main too:
  two static trains with broken paths, and no MSTS activities on the route.
  The input is smaller, 3,853 KiB against 4,046 KiB, since vehicles share
  definitions.
- **The suites:** all of `simulation-*`, `timetable-*`, `activity-check`,
  `activity-ui`, `map-view`, `consist-preview-gl` and `vehicle-file` pass.
- **The `simulation-trains.json` capture** is still identical to main's.

## Step 5 done (2026-10-28)

**Reload** in the Consist Editor and the Shape Viewer (`Eng::reload`):
- It reads the ENG file again into a new definition. Files that haven't
  changed come from the cache.
- It drops what the car made from the old definition: its lights, its
  posed parts, its sounds.
- It names its shapes again, then reloads their files.
- Cars made before keep their definitions.
- The old reload indexed a shape library it had no id for, which inserted
  0; that is gone too.

**Problems** go to the editor's error list (`ErrorMessagesLib`) as warnings,
each once, giving the file and the line.

**Lights shared:**
- Vehicles whose `Lights` block is one node of a shared include file
  share one `EngLightsBlock`, written, compressed and parsed once.
- When something in the block is included through `[[Vehicle]]`, each
  vehicle gets its own.
- `SimisDocument::write` builds one string instead of a temporary for
  every nested block. Its first form took 1.4 s of the load.

**Measured** with `vehicle-load`: the Consist Editor's
`EngLib::loadAll`. The same file was built into main `6c6d173` and the
branch, both Release, on this machine.

| | Vehicles | main | branch |
| --- | --- | --- | --- |
| OR_PSK | 3,717 | 1.47 s, +183 MB | 0.84–0.87 s, +96 MB |
| MSTS | 108 | 15 ms, +1.1 MB | 17 ms, +2.5 MB |

- **The trees with and without shared includes** (`vehicle-file-corpus`,
  OR_PSK): 67 MB shared, against 259 MB if every vehicle held copies of
  its own.

**Checks:**
- `vehicle-file` passes, 35 checks. New ones cover reload, cars made
  before, problems told once, and the lights sharing.
- `eng-lights` now expects a second load of an unchanged file to share its
  lights.
- Build `all` and ctest pass.
- The simulation, timetable, map, sound, cab and consist suites pass.
- The `simulation-trains.json` and Shape Viewer captures are identical to
  main's.
- `timetable-doors.json` is identical too, but its train 119 is not on the
  track on main either: the case was made on Windows content whose numbering
  differs from this machine's converted OR_PSK.

## Content to fix

[`reports/or-psk-vehicle-file-problems.md`](reports/or-psk-vehicle-file-problems.md)
lists every OR_PSK file with a reading problem, by file and line, with the
number of vehicles reading it. It is made by
`TSRE_VEHICLE_REPORT=<file> TSRE5vc --test --test-suite vehicle-file-corpus --game-root <game folder>`
and can be made again after fixing.

**To fix** (608 file lines, as of 2026-10-28):
- a `)` closing no block (125);
- an include not found (418). Most are copies moved one folder deeper
  (`stare/`, `xhl/`), where `[[Trainset]]` would help.
- 5 include files whose parentheses don't balance;
- 46 blocks not closed;
- 12 first lines with a comment whose quote never closes;
- 2 unterminated strings.

The include files no vehicle reads are listed too (*none* vehicles).

**To look at:**
- 12 files with data on the first line, which Open Rails doesn't read;
- 44 files in a Windows code page (read as Latin-1; Polish letters, mostly
  in comments).

## Decisions (user, 2026-10-28)

1. **Shared includes:** yes.
2. **Open Rails' include rules** checked and followed; TSRE's fallback to
   the vehicle's folder dropped.
3. **Include paths get named roots**, since `../../` is unclear and unsafe;
   Open Rails has none. They use Open Rails' bracket form. The set:
   `[[Trains]]`, `[[Trainset]]`, `[[Vehicle]]`, `[[Here]]`, `[[Global]]`,
   `[[Sound]]`, `[[Game]]`.
   - Roots are a TSRE feature; they may later be offered to Open Rails as
     a pull request.
   - Shape, sound and cab paths in vehicle files take the same roots.
4. **Everything is exposed to Lua.** This led to the key catalogue; typed
   C++ fields are kept for what C++ reads.
5. **Encodings:** as listed, with Latin-1 for the rest, as now.
6. **No new folder:** the new code goes beside the code it replaces.
7. **The play-mode code in `Eng`** stays for the driving tasks.
8. **The first line is read unless it is a SIMIS header** (after step 5).
   Open Rails' skipping of every first line works around include files that
   start with a header. TSRE's lexer reads a header as one word, so it
   recognises the header and needs no blind skip. A first line that leaves
   a quote open or closes a block it didn't open is still not read, and is
   reported; data read on a first line is reported as an Open Rails
   difference.
