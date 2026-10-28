# Task 01 - Vehicle definitions (the ENG refactor)

Status: design agreed with the user (2026-10-28); implementation on
`feature/vehicle-definitions`.

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
  line, which Open Rails drops anyway. 6 start with a line of stars, dashes or
  junk that only Open Rails' rule hides, for example `b  ß00 hbcomment (…`.
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
- **Open Rails' rules, applied when the tree is built:**
  - the first line of every file is skipped as its header;
  - `comment` and `skip` blocks, and `#` or `_` tokens with the item after
    them, are left out, so scripts never see them.

  A first line that holds anything other than a SIMIS header, a blank or a
  comment is reported, since Open Rails drops it.
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

1. **`SimisTree`, the vehicle file reader and the encoding policy.**
   - The reader covers:
     - shared and inlined includes;
     - `[[SameName]]` and the roots;
     - the game-folder check;
     - Open Rails' first-line and comment rules.
   - A new suite, `vehicle-file`, has fixtures for:
     - every encoding;
     - nested, missing, cyclic and unbalanced includes;
     - each root, and a path that leaves the game folder;
     - roots in shape, sound and cab paths, and the default lookups
       without them;
     - a per-vehicle file through `[[Vehicle]]` in a shared include;
     - the first-line and comment rules;
     - last-wins and couplings that add;
     - units;
     - the `OPENRAILS` override.
2. **The key catalogue** (generated from Open Rails, checked by hand) **and
   `VehicleDefinition`'s typed fields**, with a comparison tool.
   - Every ENG/WAG file on MSTS, TS_STARTER_ROUTE and OR_PSK is read by both
     the old `Eng::load` and the new reader, and the shared values
     compared.
   - Each difference is explained: either Open Rails' rules where `ParserX`
     differed, or a bug fixed.
3. **`Eng`, `EngLib`, `Consist` and the editors on the definition;**
   `Eng::load`'s `ParserX` code removed.
4. **The simulation on the definition.** `Sim::Vehicle` shrinks. The checks:
   - the seven MSTS routes give identical stops and events;
   - all 266 PRUDNIK trains finish as before;
   - the simulation, timetable and activity suites pass;
   - the renderer captures with trains are unchanged.
5. **Reload, problems in the error list, and measures in Release:**
   - the Consist Editor loading all of OR_PSK, before and after;
   - memory with and without shared includes.

Each step is built (`all`) and checked with ctest before its commit, on
`feature/vehicle-definitions` in its own worktree.

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
