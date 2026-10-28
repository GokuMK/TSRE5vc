# Task 01 - Vehicle definitions (the ENG refactor)

Status: design, for the user's review (2026-10-28). No code yet.

The roadmap's first driving task (decided 2026-10-17: before any driving
task): one vehicle definition per ENG/WAG file, read once and not changed
after loading, holding typed SI fields for what C++ uses and the file's
whole block tree, so scripts can read any parameter, Open Rails' additions
too. Read with `SimisTextReader`, not by extending `ParserX`. The editor's
`Eng` and the simulation both read it by const pointer. The encoding policy
was deferred to this task (2026-10-22): MSTS wrote UTF-16 only, TSRE added
ASCII, community content also has UTF-8 with or without a byte order mark.

## TSRE today

- **`Eng`** (`src/tsre/trains/Eng.cpp`, 1,474 lines) is three things in one
  class: the file's data, read by `Eng::load` with `ParserX` (about 40
  keys, 260 lines); one car's state (its place on the track, drawing,
  animation, lights, sounds); and old play-mode code (`Ruch`, `PlayDriver`,
  `TrainNetworkEng`).
- **`EngLib`** keeps one `Eng` per file (by its path key). The Consist
  Editor loads every file under `TRAINS/TRAINSET` (`loadAll`); the route
  editor loads the files its consists name.
- **A consist copies the file's data for each car**: `new
  Eng(lib->eng[id])` copies some 35 fields; only the lights block and the
  3D cab are shared (`shared_ptr`).
- **The simulation** (`MakeInput`, main thread) copies type, mass, force,
  power, top speed and resistance into `Sim::Vehicle`; the resistance as
  Davis coefficients from `ORTSDavis_*`, else converted from MSTS's
  `Friction`, else Open Rails' default formula from the mass.
- **`ParserX`** reads from a `FileBuffer`: UTF-16 conversion, a UTF-8 byte
  order mark skipped (a stop-gap, bc80f6f), includes spliced into the
  buffer (`FileBuffer::insertFile`), tried in the `OPENRAILS` folder and
  then in the vehicle's folder. Numbers go through the units task's
  `FileNumberReader`.
- **`SimisTextReader`** is the bounded lexer the shape, sound, sigcfg and
  environment readers use; its `decode` takes UTF-16 LE or BE with a byte
  order mark, else UTF-8, else Latin-1. **`EnvNode`/`EnvText`** keep an
  environment file as a token tree that writes back unchanged (one
  `QString` and a `QVector` per token: made for editing a few files).
- `Eng::reload` reloads the shapes only, never the ENG file.

**Open Rails** (behavioural reference, `openrails-linux-spike`):
- It reads about 430 keys in its rolling stock classes (`MSTSWagon` 112,
  `MSTSLocomotive` 166, diesel 49, electric 14, steam 84, control trailer
  7) and about 400 more in the subsystems (brakes, power supplies,
  controllers, axles).
- It walks the file as a stream and dispatches on the path of each item,
  for example `Parse("wagon(coupling(spring(r0")`. So a key given twice
  takes the last value, and an override file can re-open `Wagon ( ... )`
  after including the original. A few blocks add rather than replace:
  each `Coupling` adds a coupler.
- **Its other parsing rules:**
  - `include ( file )` inlines that file, relative to the including file's
    folder; nested includes are relative to their own file.
  - `comment ( ... )` and `skip ( ... )` blocks are ignored.
  - A token starting with `#` or `_` is ignored, together with the item
    after it.
  - Text is read through `StreamReader` with byte order mark detection;
    a file without a byte order mark is read as UTF-8.
- An `OpenRails` folder beside the file overrides it.

**Content on this machine:**

| | Vehicle files | Encodings | With `include` | Read per vehicle / each file once |
| --- | --- | --- | --- | --- |
| MSTS | 108 | UTF-16 with byte order mark | 0 | 55,000 tokens |
| TS_STARTER_ROUTE | 100 | UTF-16 with byte order mark | 2 | |
| OR_PSK | 4,350 | 4,287 UTF-16, 63 ASCII | 2,486 (531 in `OPENRAILS`) | 12.2 M / 3.0 M tokens |

- OR_PSK's vehicles include 1,443 distinct files; `Standard_Coupler_PL.inc`
  alone is included by 1,341.
- The OR_PSK median vehicle is 365 tokens; the largest is 124,000 tokens,
  with its includes.
- 418 include lines name a file that doesn't exist (for example
  `PCC_Fals/stare/`, old copies whose relative paths no longer fit).
- 139 files have unbalanced parentheses, 5 of them includes.

## Design

### Where

- `src/tsre/vehicles/` holds `VehicleDefinition`, its reader and its
  library. It doesn't depend on `Game`, the renderer or the editors, so the
  simulation thread and test executables can use it.
- `src/tsre/fileFunctions/SimisTree` is the compact read-only tree, next to
  `SimisTextReader`. It is general: `.con`, `.cvf` and `.sms` can use it
  later, but they don't move to it in this task.

### The tree

- **One decoded text and one flat array of nodes per file.** Each node is a
  value or a block, holding its offset and length in the text, its first
  child, its next sibling and its line. That is about 20 bytes a token,
  against about 100 bytes a token for `EnvNode`. All of OR_PSK would take
  roughly 60 MB of nodes plus 36 MB of text, if each file is read once.
- **Shared includes.** An include file is read once into its own tree and
  shared by const pointer between every definition that includes it; a
  definition's tree has an include node pointing to it. The shared cache
  is keyed by path and modification time, so a changed file is read again
  into a new tree while the old one lives on in the definitions that hold
  it.
- **An include that doesn't balance its parentheses can't be a subtree.** It
  is inlined into the including file instead, as Open Rails does, and
  reported. OR_PSK has 5 such files.
- **Open Rails' rules apply when the tree is built**: `comment` and `skip`
  blocks, and `#` or `_` tokens with the item after them, are left out, so
  scripts never see them.
- **Lookup by path, as Open Rails dispatches.** For example, `last("wagon(mass")`
  gives the last such item through every include, and `all("wagon(coupling")`
  gives each one in order. Values are read as text, number, or number in a
  unit (`Units::fromFileToken`, the units task's reader). Lua's access later
  wraps this lookup; its form belongs to the scripting tasks.

### Encodings

The policy belongs in `SimisTextReader::decode`, so every reader shares it:
- **Accepted:** UTF-16 LE or BE with a byte order mark; UTF-8 with or
  without one (ASCII is UTF-8).
- **Accepted and reported:** UTF-16 without a byte order mark, recognised
  by zero bytes in its first characters.
- **Text that is valid in none of these** is read as Latin-1, as now, and
  reported, naming the file. MSTS text was written in the Windows code page
  of the author's machine, which the file doesn't record. Polish names in
  Windows-1250 would come out wrong in Latin-1 but stay readable; Open
  Rails shows replacement characters instead.

Problems go to the editor's error list (`ErrorMessagesLib`, as `SigCfg`
reports), naming the file and line, once per file.

### The definition

`VehicleDefinition` is made once by the reader, never changed, and held by
`std::shared_ptr<const VehicleDefinition>`. It contains:
- **Files:** the file read (the `OPENRAILS` copy when
  `core.content.loading.preferOpenRailsEng` is on and it exists, else the
  original) and each include, in order.
- **The tree.**
- **Typed fields in SI.** Today's 40 keys move here unchanged in meaning:
  - names and search keywords;
  - shapes and freight animations;
  - size, mass, wheel radii;
  - couplings;
  - type and the engine's type;
  - brake system;
  - force, power, current, top speed;
  - Davis and Friction;
  - passenger capacity;
  - sounds;
  - the 3D cab;
  - the lights block, kept as its text as now.
- **The resistance as Davis coefficients, worked out once** (given,
  converted from Friction, or Open Rails' default from the mass) together
  with where they came from. `MakeInput`'s `resistanceOf` moves here.
- **The problems found while reading it.**

The typed fields follow Open Rails' rules: the last value wins, and
couplings add. Today the first `Wagon` block's name is the vehicle's, and
an override file's own `Wagon ( ... )` replaces values as it does in Open
Rails.

**Which keys get typed fields now** (to decide, below):
- **Narrow:** today's 40 keys plus brakes, adhesion and the cab view file.
  Everything else stays in the tree until the physics task needs it.
- **Broad:** Open Rails' 430 or so at once.

### Who uses it

- **`EngLib`** keeps the definitions, one per file key, and makes a new one
  when a file is reloaded. `Eng` keeps one car's state and holds its
  definition's pointer: the copy constructor shares the definition instead
  of copying fields. Call sites read `def->massKg` and so on instead of
  `massKg`, in about 25 files.
- **Reload** in the Consist Editor and Shape Viewer reads the ENG file
  again into a new definition, as well as the shapes. Cars made before
  keep the old one.
- **`Sim::Vehicle`** shrinks to the definition's pointer, its flip, its car
  id and its `EngLib` id for drawing. The simulation thread reads the
  definition directly, and a running simulation keeps the definitions it
  started with until it ends.
- **Not touched:**
  - the play-mode code in `Eng` (`Ruch`, `PlayDriver`, `TrainNetworkEng`);
  - the consist reader;
  - the sound reader;
  - physics, which is the next driving task.

## Steps

1. **`SimisTree` and the vehicle file reader:** includes shared and
   inlined, Open Rails' token rules, the encoding policy in `decode`.
   - A new suite, `vehicle-file`, with fixtures covering:
     - every encoding;
     - nested, missing, cyclic and unbalanced includes;
     - `comment`/`skip`/`#`/`_`;
     - last-wins and couplings that add;
     - units;
     - the `OPENRAILS` override.
2. **`VehicleDefinition`'s typed fields**, and a comparison tool. Every
   ENG/WAG file on MSTS, TS_STARTER_ROUTE and OR_PSK is read by both the old
   `Eng::load` and the new reader, and the shared values compared. Each
   difference is explained: either Open Rails' rules where `ParserX`
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

## To decide (user)

1. **Shared includes** as above: each include file read once and shared
   between vehicles, with unbalanced ones inlined and reported. Is this
   what you planned?
2. **Typed fields now:** narrow (today's keys plus brakes, adhesion and the
   cab view file, the rest from the tree when the physics needs it) or
   broad. I'd go narrow.
3. **Encodings:** the list above. For text that isn't valid UTF-8, Latin-1
   as now, or Windows-1250, or a setting?
4. **Include paths:** relative to the including file only, as Open Rails
   does. Today TSRE also tries the vehicle's folder when the `OPENRAILS`
   copy's include isn't found. Drop that fallback?
5. **Folder:** `src/tsre/vehicles/` for the definition and its reader, and
   `SimisTree` in `fileFunctions`.
6. **Old play-mode code in `Eng`:** leave it for the driving tasks, or
   remove it here where it gets in the way?
