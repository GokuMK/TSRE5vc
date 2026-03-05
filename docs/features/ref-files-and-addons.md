# TSRE Ref Files and Addons (Current State)

## Scope
- Project: TSRE5vc
- Goal: document how route `.ref` files and addon `.ref` files are loaded and used today.
- Focus: editor object palette ("what you can place"), parsing, in-memory representation, and placement.
- Non-goal: implement templates or live refresh (see `docs/tasks/addons/01-ref-template-items.md` and `docs/tasks/addons/02-ref-refresh-and-per-file-management.md`).

## Terminology (used in this doc)
- **Ref file (`.ref`):** a text file describing placeable objects (items) grouped into **classes**.
- **Addon ref:** additional `.ref` files stored under `routes/<route>/addons/*.ref` and loaded in addition to the main route ref.
- **Ref item:** one palette entry; maps to one placeable world object type + filename (+ optional metadata).
- **Class:** the palette category key (`Class ( ... )`), used as the primary grouping key in TSRE.

## Evidence Base (Code Anchors)
- Addon loading: `src/tsre/world/Route.cpp:459` (`Route::loadAddons`)
- Ref data model: `src/tsre/world/Ref.h:20`
- Ref parsing: `src/tsre/world/Ref.cpp:38` (`Ref::loadUtf16Data`)
- Template TODO (not implemented): `src/tsre/world/Ref.cpp:155`
- Ref serialization (server + save): `src/tsre/world/Ref.cpp:175` (`Ref::saveToStream`)
- Editor palette wiring + pointer usage: `src/routeEditor/ObjTools.cpp:243` (`ObjTools::routeLoaded`)
- Placement uses selected `RefItem`: `src/tsre/world/Tile.cpp:573` (`Tile::placeObject`)
- Shape path resolution for placed items: `src/tsre/world/objects/WorldObj.cpp:265` (`WorldObj::getResPath`)
- Random placement transforms: `src/tsre/world/objects/WorldObj.cpp:669` (`WorldObj::randomTransform`)
- Multiplayer addon sync: `src/routeEditor/RouteEditorServer.cpp:304` (`request_addons`)

---

## 1. Where Ref Files Come From (Route + Addons)

TSRE loads placeable items from:
1) the **main route ref**: `routes/<route>/<RouteName>.ref`, and
2) **addon refs**: `routes/<route>/addons/*.ref`.

This happens in `Route::loadAddons()`:
- it creates one `Ref` instance and loads the main `.ref` first,
- then enumerates `routes/<route>/addons` and `loadFile()`s every `*.ref` found.

All parsed items are merged into a single in-memory store (`Ref::refItems`). `src/tsre/world/Route.cpp:459`

### Includes inside `.ref`
The parser recognizes `Include ( "<file>" )` and uses `FileBuffer::insertFile(...)` to splice the included file into the current parse stream. `src/tsre/world/Ref.cpp:38`

Note: current include path handling concatenates `path + "/" + incPath`, where `path` is passed from `loadFile(...)` and is currently the *full path of the `.ref` file*. This assumes `path` is a directory, so include resolution may be incorrect unless call sites pass a directory string. `src/tsre/world/Ref.cpp:38`

---

## 2. In-Memory Model: `Ref::RefItem` + `Ref::refItems`

`Ref` stores items grouped by class:
- `QMap<QString, QVector<RefItem>> refItems;` `src/tsre/world/Ref.h:57`
- key: `item.clas.trimmed()`
- value: a list of items for that class

Each `RefItem` is (today) a value type stored inside those `QVector`s. Key fields: `src/tsre/world/Ref.h:24`
- `type`: the item block name, used as the world object type string (`WorldObj::createObj(type)`).
- `clas`: the palette class/category (`Class ( ... )`).
- `filename[]`: one or more filenames (used for selection methods).
- `description`: the palette display label.
- `selectionMethod`: `""`, `SequentialSelection`, or `RandomSelection`.
- `randomTransformation`: optional random rotation/translation ranges applied at placement time.
- `value`, `staticFlags`: additional numeric data passed into newly placed objects (`ref_value` and `staticflags`).

Important practical detail:
- The editor UI (`ObjTools`) stores raw `Ref::RefItem*` pointers to elements inside these `QVector`s. Any in-place mutation that reallocates vectors (append/erase) can invalidate those pointers. `src/routeEditor/ObjTools.cpp:426`

---

## 3. Parsing Rules (Current Behavior)

`Ref::loadUtf16Data(...)` is a token-based parser that:
- skips SIMISA headers (`SIMISA@@@@@@@@@@...`) and any tokens starting with `simis`,
- recognizes and processes: `include`, `skip`, `comment`,
- otherwise treats the token as a **new item type** and reads its `(...)` block into a `RefItem`.

Inside an item block, TSRE recognizes (case-insensitive):
- `template` (sets `RefItem::isTemplate = true`, but currently does not create any items)
- `directory`, `class`, `filename`, `align`, `description`, `selectionmethod`
- `randomRotX/Y/Z` and `randomTranslationX/Y/Z` (allocates `randomTransformation` and fills ranges)
- unknown attributes are skipped

Storage rule:
- only items with `Class ( ... )` set are stored (`item.clas != ""`).
- non-template items are appended into `refItems[item.clas.trimmed()]`. `src/tsre/world/Ref.cpp:153`
- template items are currently ignored (there is only a design comment block). `src/tsre/world/Ref.cpp:155`

---

## 4. Selecting a Filename (Sequential/Random)

Items can list multiple `Filename ( ... )` entries. When placing an object, TSRE chooses a filename using:
- default (no method): first filename
- `SequentialSelection`: cycles through the filenames using a single global static counter (shared across all items)
- `RandomSelection`: `rand() % filename.size()`

See `Ref::RefItem::getNextShapeName()`. `src/tsre/world/Ref.cpp:225`

---

## 5. Editor Palette Integration (`ObjTools`)

The editor UI reads `route->ref->refItems` and populates palette dropdowns/lists:
- class keys are taken directly from the `QMap` keys, sorted case-insensitive
- list items use `RefItem::description` as the visible text

The editor also injects additional pseudo-classes that are not file-backed:
- `#TDB#...` classes for track/road shapes from `TSectionDAT`
- `#TSRE#...` classes for signals, forests, speedposts, sound regions, and TSRE tools

These pseudo-classes are inserted directly into `route->ref->refItems` and are intentionally not written back to disk. `src/routeEditor/ObjTools.cpp:243`, `src/tsre/world/Ref.cpp:180`

When the user selects a list item, the UI stores a raw pointer to the selected `RefItem` as `route->ref->selected`. `src/routeEditor/ObjTools.cpp:479`

---

## 6. Placement: From `RefItem` to `WorldObj`

Placing an object from the palette ultimately calls `Tile::placeObject(p, q, RefItem*, ...)`. `src/tsre/world/Tile.cpp:573`

Key placement behaviors:
- Creates a world object instance by string type: `WorldObj::createObj(item.type)`.
- Picks a filename via `item.getNextShapeName()` and writes it into the object as `ref_filename`.
- Passes additional metadata:
  - `ref_class` = `item.clas`
  - `ref_value` = `item.value`
  - `staticflags` = `item.staticFlags` (only if non-zero)
- Loads the object and applies optional random transforms:
  - `WorldObj::randomTransform(...)` adjusts quaternion + position and marks object modified. `src/tsre/world/objects/WorldObj.cpp:669`

Shape path resolution for file-backed shapes is done later by the object and/or `WorldObj::getResPath(...)`, which expects the item filename to already include any subdirectory (because `RefItem::directory` is not currently applied). `src/tsre/world/objects/WorldObj.cpp:265`

---

## 7. Saving / Multiplayer Sync (`saveToStream`)

`Ref::saveToStream(...)` serializes the in-memory items back into a `.ref`-like form:
- it iterates over `refItems` and skips any class beginning with `#` (the editor-generated groups),
- it writes:
  - `type (...)`
  - `class (...)`
  - `filename (...)` entries
  - optionally `align`, `description`, `selectionmethod`, and `random*` attributes

It does not currently serialize:
- `directory`
- `template` markers
- `value` or `staticFlags`

This serializer is also used by the multiplayer server to send addon data to remote clients (`request_addons`). `src/tsre/world/Ref.cpp:175`, `src/routeEditor/RouteEditorServer.cpp:304`

---

## 8. Known Limitations / Design Pressure Points

- **Templates are not implemented**: `Template ( )` is parsed but produces no palette items today. `src/tsre/world/Ref.cpp:155`
- **Directory is parsed but effectively unused** in placement (filenames must include the directory path themselves), and it is also not written back by `saveToStream`. `src/tsre/world/Ref.cpp:38`, `src/tsre/world/Ref.cpp:175`
- **Pointer stability risk**: editor UI caches `RefItem*` pointers into `QVector` storage; any future live refresh/hot reload needs a strategy to avoid pointer invalidation. `src/routeEditor/ObjTools.cpp:426`

Planned improvements and design options for templates + refresh are captured in:
- `docs/tasks/addons/01-ref-template-items.md`
- `docs/tasks/addons/02-ref-refresh-and-per-file-management.md`
