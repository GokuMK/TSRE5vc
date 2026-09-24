# Errors and Messages: safety, usability and content diagnostics

Status: proposed backlog, not implemented or approved as one large rewrite.
Created: 2026-09-21.

Current behavior and source map:
[Errors and Messages](../../features/errors-and-messages.md).

## First concrete repair action: QuadTree reconstruction

Implemented for the [QuadTree recovery task](../terrain/terrain-simple-lookup-migration.md):
an optional **Fix** button in the selected-message details. Ordinary messages
remain informational; they must not get a functional Fix button by default.

- [x] With auto-fix off, Fix adopts the temporary reconstructed tree into the route
  in memory and marks it modified. No disk write occurs on Fix. No new confirmation
  dialog, modal startup prompt, special save prompt or approval menu item.
  Ignoring the message leaves saved metadata untouched, including on route Save.
- [x] With auto-fix on, eligible reconstruction uses the same in-memory adoption
  immediately and reports its result. Both paths appear in unsaved-content reporting
  and require user confirmation through the ordinary save dialog before disk writes.
  Respect global write-disable; cancel/discard must leave disk metadata unchanged.
- [x] Use a typed/explicit action binding, not interpretation of the existing
  free-text `action` description. Bind it to the route session and resolve current
  repair state on invocation; do not capture a dangling tree pointer.
- [x] Disable adopted/unavailable actions; distinguish in-memory repair from saving.
  Report failed adoption and permit retry; failed writes retain modified state for
  retry through ordinary Save. Do not call an adopted in-memory tree persisted.

This small integration is required by the recovery task; it does not require all
the general improvements below to be implemented first.

## Targeted presentation (implemented)

- [x] Reusable immediate opening at a selected message, including scrolling and
  displaying details; keep the menu checkmark synchronized.
- [x] Deferred, one-shot requests for route loading, guarded against stale
  sessions. Open only after the editor is shown, not inside the loader.
- [x] Missing/invalid QuadTree requests attention this way; deliberately ignored
  existing metadata does not. Multiple requests produce one opening, selecting
  the first current message. No repair or disk write occurs from presentation.
- [x] Preserve selection across list rebuilds and support keyboard selection.
- [x] Offscreen widget tests cover selection, incoming-message refresh, one-shot
  consumption, stale requests, read-only Fix details, and recovery policy.

This is independently reusable even if QuadTree startup presentation is later
disabled. It does not implement filtering, incremental list updates or the
broader ownership changes below.

## Goal and boundaries

Make existing content diagnostics reliable, easy to find and safe to navigate,
without making route loading or realtime rendering substantially slower.
Keep validation/repair separate from presentation. Opening, filtering, clearing,
copying or exporting messages must not mutate route content.

This is not a new whole-route validator, general logging framework, or blanket
permission to add automatic repairs. Deliver the stages independently. Future
terrain/no-QuadTree diagnostics should reuse this service, not invent a second
window.

## 1. Message lifetime and identity

- [ ] Give messages stable IDs independent of row number/vector position. Store
  IDs as item data initially; do not require a view rewrite just to fix identity.
- [ ] Define route-session ownership and explicit reset at route teardown/reload.
  Preserve messages emitted before the window opens. Separate app-level messages
  if needed; never let old-route entries navigate into the new route by accident.
- [ ] Own coordinates by value/optional value. Initialize severity/source and
  define copy behavior. Replace ambiguous message ownership with one owned store;
  migrate existing callers through a small compatibility adapter if necessary.
- [ ] Replace durable raw object pointers with resolvable handles: session,
  object kind and identity (World tile + UiD, or TDB/RDB item ID as appropriate).
  Resolve on user action, not per frame. Handle deletion, reload and replacement;
  do not select a new object that merely reused an old ID without verification.
- [ ] Disable selection gracefully for unavailable targets. Keep snapshot
  coordinates useful for Jump even if an object is gone. Guard stale selection
  and clear the details pane when its entry disappears.
- [ ] Finalize repair result before publication, or support explicit row updates.
  Existing post-push ERROR -> ERROR_FIX changes must appear immediately.

## 2. Bounded, inexpensive message delivery

- [ ] Insert/update only changed rows and batch GUI notifications. Preserve
  selection/scroll; hidden windows should perform no row construction work.
  A `QAbstractTableModel` is a candidate, not a requirement if a small incremental
  implementation meets the measurements.
- [ ] Introduce diagnostic codes and optional structured context (file, field/token,
  tile, patch, object) for selected producers. Do not deduplicate by translated
  prose alone. Repeated reports can update count and first/last occurrence.
- [ ] Bound retained entries with a documented policy and visible omitted/repeated
  counts. Avoid silently losing fatal errors; decide severity-aware limits during
  implementation rather than an arbitrary very small fixed cap.
- [ ] For worker producers, enqueue value data and deliver it on the GUI thread.
  No QWidget calls from workers; no route/object ownership crossing that queue.
  Coalesce notification bursts and discard deliveries belonging to closed sessions.
- [ ] Keep reports out of per-vertex/per-sample loops. Report once per failure or
  resource revision, not every render attempt. No content hashing or synchronous
  directory scans in the message insertion path.

## 3. User-facing improvements

- [ ] Read-only, selectable/copyable text; resizable window and usable long-message
  details. Preserve TSRE colors but keep textual severity labels accessible.
- [ ] Search plus severity/source filters; total/filtered counts and repeat counts.
  Add sorting without breaking row-to-record identity or current selection.
- [ ] Copy selected message/context and export the current report as UTF-8 text
  or JSON. Include session/route and timestamps; omit live pointers. User chooses
  destination; this is not an automatic upload.
- [ ] Clear/dismiss with clear semantics: clears reports, does not fix content.
  Clearing is separate from a future explicit revalidation command.
- [ ] Distinguish severity from repair status over time: reported, repair applied
  in memory, verified resolved if rechecked. Do not call something saved/resolved
  merely because it has an ERROR_FIX label.
- [ ] Localize new labels/messages using the existing translation workflow.

## 4. Incremental diagnostic coverage

- [ ] Add terrain categorization and surface actionable T-file/RAW/patch-F load,
  unsupported-layout/read-only and save failures. Include resource path and why
  editing/save was refused. Start at application loader/save boundaries, where
  route/tile context is known; keep low-level parsers independent of widgets.
- [ ] Avoid duplicate reports from parser, TFile and Terrain for the same failure.
  Preserve Qt/CLI diagnostics and standalone test behavior; do not redirect all
  `qWarning()` output into the UI indiscriminately.
- [ ] Report no-QuadTree recovery mode and unsupported footprints clearly when
  [simple lookup migration](../terrain/terrain-simple-lookup-migration.md) is
  implemented. Do not present intentionally populated-without-payload QuadTree
  entries as corruption without context.
- [ ] Review shape/texture/configuration diagnostics separately for useful coverage.
  Do not automatically turn every recoverable parser warning into a fatal error.

Existing auto-fix defaults remain unchanged. Any future user-triggered repair
needs a separately reviewed policy: explicit intent, write-disable checks,
fresh target validation, undo/backup where applicable, and truthful result text.
Checking an error must not silently overwrite a corrupt existing file or replace
it with an empty database.

## Verification and acceptance

- [ ] Record current hidden/visible insertion time and memory for 1k/10k messages;
  compare after changes on the same machine. Measure burst responsiveness and
  repeated-error growth. Normal routes must not pay per-frame reporting costs.
- [ ] Unit tests: ordering, stable IDs, dedup keys, retention, initialized defaults,
  coordinate copies, explicit updates, reset and late worker deliveries.
- [ ] GUI tests: pre-window messages, show/hide, filter/sort, clear, selection
  retention, export, missing location/object, stale targets after delete/reload,
  route switching, and correct coordinate signs for Jump.
- [ ] Temporary content fixtures: malformed T-file, unsupported footprint, missing
  versus corrupt resource, failed save and repair-state update. Verify no content
  changes from reporting/navigation/export, including with writes disabled.
- [ ] Exercise existing World and TrackDB/RoadDB producers. Respect the distinction
  between loaded-content checks and a complete route audit; no false clean bill
  of health from an empty/filtered list.
- [ ] Keep standalone parsers/tests GUI-independent. Build with at most two jobs
  on the user's machine; update the feature doc after each implemented stage.

## Related work

- [Typed T-file model/parser](../terrain/terrain-tfile-structure-and-parser.md).
- [No-QuadTree lookup / TerrainLibSimple removal](../terrain/terrain-simple-lookup-migration.md).
- [Content/path repair planner](../core/case-sensitive-filepaths.md): a separate
  planner with its own reports, not an existing capability of this window.
- [Translation workflow](../../features/translations.md).
