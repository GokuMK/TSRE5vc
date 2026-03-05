# Task 02 - Ref Per-File Refresh + Per-File Save/Editor Support (Deferred)

## Objective
Capture the design work needed to support:
- Per-file `Refresh ( minutes )` (background or scheduled reload), and
- A future ref editor that can modify and save back **one specific `.ref` file** (main route ref or a single addon ref).

This task is explicitly **deferred**: current workflow uses manual "reload ref" and that is sufficient for now.

## Context / Current State
Relevant code paths:
- Addon load merges main route ref + all addon refs into a single `Ref` instance:
  - main: `routes/<route>/<RouteName>.ref`
  - addons: `routes/<route>/addons/*.ref`
  `src/tsre/world/Route.cpp:459`
- Manual refresh exists today:
  - `RouteEditorGLWidget::reloadRefFile()` calls `route->loadAddons()` and then refreshes the palette UI. `src/routeEditor/RouteEditorGLWidget.cpp:340`
- Ref storage is aggregated and lossy:
  - `Ref::refItems` is keyed only by class (`QMap<QString, QVector<RefItem>>`) and does not track which file contributed each item. `src/tsre/world/Ref.h:57`
  - `Ref::saveToStream()` serializes only a subset of fields and skips editor pseudo-classes. `src/tsre/world/Ref.cpp:291`
- Multiplayer uses the same serialization:
  - server responds to `request_addons` with `route->ref->saveToStream()` output. `src/routeEditor/RouteEditorServer.cpp:304`

Key constraint from current multiplayer behavior:
- Client expects a normal, already-expanded item list (same behavior as today). If templates exist on disk, the server still needs to provide expanded items over the wire.

---

## Why Per-File Refresh/Save Is Hard Today
The current model merges everything into one structure:
`refItems[item.clas.trimmed()].push_back(item);` `src/tsre/world/Ref.cpp:164`

Consequences:
- No provenance: you cannot tell which `.ref` file created which items, so you cannot refresh/remove items from "just that file".
- No per-file save: even if a future UI edits items, there is no way to write changes back to a single source `.ref` file (main vs addons).
- Pointer safety: editor UI caches raw pointers to `RefItem` values stored inside `QVector`s. Any background refresh that mutates vectors can invalidate pointers. `src/routeEditor/ObjTools.cpp:426`

Because manual refresh currently recreates the entire `Ref` (`route->loadAddons()`), the editor avoids incremental mutation and simply rebuilds lists.

---

## Requirements (Future)

### A) Per-file refresh
- Support `Refresh ( minutes )` at `.ref` file scope.
- Apply it only to the file that defines it (main route ref and addon refs can each opt-in).
- Refresh should:
  - reload file content,
  - update the aggregated palette by adding/removing only items owned by that file,
  - be safe even if the UI has a selection.

### B) Per-file save (ref editor support)
- Store enough information to serialize back a single `.ref` file:
  - explicit items as authored
  - template definitions as authored (if templates are preserved)
  - file-scope directives (`Include`, `Refresh`, comments if desired)
- Allow editing an item and saving only the source file it came from.

### C) Multiplayer parity
- `request_addons` must continue to deliver an already-expanded list of placeable items.
- Even if templates are preserved on disk (round-trip), network serialization should still be able to send expanded items.

---

## Design Notes / Proposed Structure

### 1) Introduce per-file containers (`RefFile`)
Store each loaded `.ref` file as a separate unit:
- `path`
- `refreshMinutes`
- `rawBlocks` (or a minimal parse tree for round-trip)
- `explicitItems` (as-authored)
- `templateDefs` (as-authored)
- `expandedItems` (generated, for palette + multiplayer serialization)

Then build a derived aggregated view:
- `refItemsByClass` -> used by the editor palette

### 2) Stable item identity and ownership mapping
To remove/update items on refresh and to save edits back to the right file:
- assign an item ID (stable across reloads) and track `itemId -> owning RefFile`
- for template-generated items, include the generated filename in the ID

### 3) Pointer safety for background refresh
If background refresh is implemented:
- replace raw pointer usage with stable handles (`QSharedPointer` / IDs), or
- synchronize refresh with UI by clearing selections and rebuilding lists before any in-place mutation.

### 4) Serialization modes
Consider two serialization outputs:
- **Round-trip file save:** preserve templates/directives as authored.
- **Expanded network send:** serialize only expanded normal items to keep client behavior unchanged.

---

## Non-goals (for now)
- Implementing per-file refresh scheduling.
- Implementing ref editor UI.
- Implementing per-file save to disk.

These remain future work; manual reload via `RouteEditorGLWidget::reloadRefFile()` is sufficient for current usage.
