# Task 01 - Ref Template Items (Glob Expansion)

## Objective
Implement `.ref` template items (`Template ( )`) that expand into many normal ref items by scanning a directory and matching filename wildcards (glob).

Scope (for this task):
- Build the in-memory palette at **load/manual reload time**.
- Produce **expanded normal items** so multiplayer clients receive the same data as today.

Out of scope:
- Per-file `Refresh ( minutes )` scheduling and background updates (separate task).
- A `.ref` editor and "save back the same file we loaded" (separate task).

## Context / Current State
Relevant code paths:
- Addon load: `Route::loadAddons()` loads `routes/<route>/<RouteName>.ref` + `routes/<route>/addons/*.ref`. `src/tsre/world/Route.cpp:459`
- Manual reload button/command: `RouteEditorGLWidget::reloadRefFile()` calls `route->loadAddons()` and refreshes UI lists. `src/routeEditor/RouteEditorGLWidget.cpp:340`
- Parser & store: `Ref::loadUtf16Data()` parses items and appends to `Ref::refItems[class]`. `src/tsre/world/Ref.cpp:38`, `src/tsre/world/Ref.h:57`
- Templates: `Template ( )` sets `RefItem::isTemplate` but is currently ignored, with an in-code comment describing intended behavior. `src/tsre/world/Ref.cpp:155`
- Multiplayer: server answers `request_addons` by serializing `route->ref` via `Ref::saveToStream()`. Client expects a normal expanded item list. `src/routeEditor/RouteEditorServer.cpp:304`, `src/tsre/world/Ref.cpp:175`
- Placement uses `RefItem::filename` as a route-shapes-relative path via `WorldObj::getResPath(...)`. `src/tsre/world/objects/WorldObj.cpp:265`

---

## Requirements

### Functional
- A `.ref` item block containing `Template ( )` is a **template definition**, not a placeable entry by itself.
- A template definition can specify:
  - `Type`: the item block name (e.g. `Static`, `Signal`, ...) - same as normal items.
  - `Class ( ... )`: the target class to populate - same as normal items.
  - `Directory ( ... )`: optional subdirectory under the route shapes root.
  - `Filename ( ... )`: one or more wildcard patterns describing which files to expand (example: `tree_*.s`).
  - Optional metadata to copy into generated items: `Align`, `SelectionMethod`, `Random*`, `Value`, `StaticFlags`.
- Expansion rule:
  - For each matching file under `routes/<route>/shapes/<Directory>`, create a **normal** ref item with:
    - `type` and `class` copied from the template
    - `filename = "<Directory>/<file>"` (or just `<file>` when directory is empty)
    - `description` default derived from the file (see below)
- Templates must not generate duplicates:
  - do not generate an item whose normalized `filename` already exists as an explicit item
  - do not generate duplicates across templates
- Deterministic ordering:
  - within a class, generated items must appear in a stable order across runs (sort matches case-insensitive).
- Multiplayer parity:
  - the serialized addon list must contain expanded normal items (no requirement for clients to re-scan directories).

### Non-Functional
- Use glob/wildcards (not regex) for the first version; keep authoring simple (`tree*.s`, `tree_??.s`).
- Expansion should be fast enough for route load/manual reload (directory scan is acceptable here).

---

## Design

### 1) Parse then expand (batch at end of addon load)
The in-code comment suggests running template processing after all `.ref` files are loaded. `src/tsre/world/Ref.cpp:155`

Proposed flow:
1) During parsing, collect template definitions into a list (do not append them to `refItems`).
2) After all `.ref` files are loaded (end of `Route::loadAddons()` batch), expand templates:
   - Build a set of `existingFilenames` from explicit items already stored in `refItems`.
   - For each template:
     - resolve base dir under route shapes root (`routes/<route>/shapes/` + `Directory`)
     - for each `Filename` pattern, get matches via `QDir::entryList(nameFilters)`
     - for each match:
       - build `relativePath = Directory + "/" + match` (or `match` if no directory)
       - normalize separators to `/`
       - if `relativePath` is not in `existingFilenames`, emit a normal `RefItem` and append to `refItems[class]`
   - Optionally sort each affected class list by `description`/`filename` for stability.

This design keeps multiplayer behavior unchanged: `Ref::saveToStream()` will serialize the expanded normal items.

### 2) Description defaulting
First version (simple and predictable):
- If template provides `Description ( ... )`, treat it as a prefix (e.g. `"Trees - "`), and generate `description = prefix + basename(file)`.
- Otherwise set `description = basename(file)` (or the full `relativePath` if that is more useful in practice).

### 3) Directory semantics
`WorldObj::getResPath(...)` expects `filename` to already include any subdirectory (TSRE does not currently join `Directory + Filename` automatically for normal items). `src/tsre/world/objects/WorldObj.cpp:265`

So template expansion must generate route-shapes-relative paths such as:
- `trees/tree_01.s`
- `houses/brick/house_03.s` (if `Directory` contains slashes)

### 4) Metadata copying
Generated items copy attributes from the template:
- `align`, `selectionMethod`, random transformation ranges
- `value` and `staticFlags` (when those fields are supported/parsed for ref items)

### 5) Persistence (intentionally deferred)
Because `Ref` currently merges data from multiple files into one structure, it cannot save "the same file we loaded" after edits, and `saveToStream()` does not preserve template markers/directories. Keep template persistence and per-file save/edit in the separate task.

---

## Acceptance Criteria / Manual Tests
- Add a template item to an addon `.ref`:
  - `Directory ( "trees" )`
  - `Filename ( "tree_*.s" )`
- Start TSRE (or run `RouteEditorGLWidget::reloadRefFile()`):
  - palette shows one item per matching file
  - placing the item loads the expected shape from `routes/<route>/shapes/trees/...`
- Add an explicit item for one of the matched files:
  - template expansion does not duplicate it
- Multiplayer:
  - `request_addons` returns an already-expanded list (client does not need filesystem scanning).

---

## Open Questions (need confirmation)
- Description default: basename (`tree_01.s`) vs relative path (`trees/tree_01.s`)?
- Should templates scan recursively or only one directory level (recommend non-recursive first)?
- Should multiple `Filename ( ... )` patterns be supported per template (recommend yes: union)?

