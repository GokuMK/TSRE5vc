# Errors and Messages

Source review: 2026-09-21, including QuadTree recovery's Fix action. This describes
the existing implementation, not the proposed
[improvements](../tasks/editor/errors-and-messages-improvements.md).

## Using the window

Open **Tools > Errors and Messages** in the route editor. The tool window lists
messages collected during loading, validation and certain editor operations.
Opening it refreshes the display; it does **not** start a complete route scan.
Closing/hiding it does not clear the collected messages.

Code can also open the window at a particular message: its row is selected,
scrolled into view, and its details (including Fix, if available) are displayed.
Selection survives list refreshes when new messages arrive. Keyboard selection
also updates the details. Programmatic opening keeps the Tools menu checkmark
in sync; this is a non-modal tool window, not a confirmation dialog.

Newest messages appear first. Columns are ID, Time, Type, Source and Message.
The ID is currently the message's index in an in-memory vector, not a persistent
content ID. Time is displayed as local `HH:mm:ss`.

| Type | Display color | Meaning |
| --- | --- | --- |
| `INFO` | TSRE green | Informational event |
| `WARNING` | Yellow | Potential content problem |
| `ERROR` | TSRE red | Reported content/load problem |
| `ERROR_FIX` | Blue | Producer reports an automatic repair; not a repair button |

Sources are `Other`, `TrackDB`, `RoadDB`, `World` and `Editor`. Source labels use
TSRE's main-label color. There are no dedicated Terrain/Texture/Shape categories.

Click a row to see its message and optional explanatory **Description**. This
description is stored in the code's `action` string: it can suggest a manual
remedy or describe a repair already made, but it is not executable code.

- **Select Object** forwards an attached object to the editor's selection handler.
  With no attached object, the disabled button reads **NO OBJECT**.
- **Jump** appears only when the producer attached a location. The location
  contains world-tile coordinates and local X/Y/Z coordinates.
- An object and a location are independent: a message may provide either or both.
- **Fix** appears for messages with an explicit repair action. Currently this is
  used by QuadTree recovery (see below), not a general repair for every error.
- There is no revalidate, search/filter, clear or report-export control in this
  window. No explicit sorting control is enabled.

The message/description widgets are not currently made read-only. Typing in them
does not update the underlying message or fix the route. The window is fixed at
400 pixels high, with a 730-pixel minimum width; its list has no horizontal scroll.

## What produces messages

### Opening at a message from code

These are GUI-thread APIs on `ErrorMessagesLib`; publish the message first with
`PushErrorMessage(message)`:

- `ShowMessage(message, parent)` opens/raises the window immediately at that
  message. A null/unregistered message is rejected without opening a window.
- `RequestShowMessage(message, isCurrent)` records a deferred request during
  loading. The optional guard must remain safe after its producer is destroyed;
  QuadTree recovery uses a weak session token and active route/library checks.
- `ShowRequestedMessage(parent)` consumes the pending requests, skips stale ones
  and opens at the first valid message. The route editor calls it on the next
  event-loop turn after showing the loaded editor, not in terrain loading code.

Requests do not invoke Fix. A deferred request needs an explicit consumer;
requesting one after startup alone does not immediately raise the window.
Messages currently have process-lifetime storage; these APIs do not redesign
their ownership or make the existing message collection thread-safe.

Only producers calling `ErrorMessagesLib::PushErrorMessage()` populate this
window. It is not a mirror of Qt logging, parser warnings, or every error dialog.
An empty list therefore does not establish that a route is valid.

Representative producers:

| Area | Examples / entry points |
| --- | --- |
| Route loading | Route/global track-section mismatch and conversion information; see `Route.cpp` |
| Track/road databases | `TDB::checkDatabase()`: orphaned or multiply referenced items, invalid positions and node links; existing TDB/RDB load failures |
| Configuration | Existing `sigcfg.dat` and `speedpost.dat` load failures |
| World objects | `Tile::checkForErrors()` calls object-specific checks, including signals, platforms, speed posts, pickups, level crossings, hazards and sound regions |
| Static scenery | `StaticObj::checkForErrors()` warns about unusually large tile-local coordinates and supplies navigation/selection |
| Editor operations | Some object loading/fixup and editing paths emit informational messages |

This is an inventory of examples, not a guarantee of exhaustive checks. World
object checks happen in tile-loading/initialization paths; which tiles were loaded
matters. `Route.cpp` invokes database checks for both track and road databases.
`core.route.loading.preloadAllWorldFiles` also affects available World-reference
data in database validation. Showing the window does not preload World tiles.

### Automatic repair is separate from this UI

`core.route.validation.autoFix` defaults to **false**. When enabled, supported
Tile/TDB validation paths can change route data while checking it. For example,
`Tile::checkForErrors()` changes a returned ERROR to ERROR_FIX, appends a repair
description, marks the object unloaded and modified; TDB checks can delete invalid
interactive items. This is not merely dismissal of a warning.

These existing Tile/TDB repairs are not applied by the window, and a message does
not guarantee they were saved to disk.
Keep backups before using automatic repair. An ERROR_FIX is a historical report,
not proof that every related problem is resolved. Producers sometimes push the
message before modifying its type/action, so an already visible list may show
the old summary until refreshed.

### QuadTree recovery: Fix

QuadTree recovery and save messages use the **World** source: they concern route
terrain metadata, rather than the editor itself.

When a route's QuadTree is missing, TSRE can use a temporary tree reconstructed
from terrain descriptors. With saved-QuadTree lookup disabled, the same recovery
backend ignores saved metadata and reconstructs temporary detailed/distant trees.
This no longer uses the removed `TerrainLibSimple` implementation.

After the route editor opens, missing or rejected QuadTree metadata automatically
opens this window at the first current recovery message (detailed before distant).
Additional messages remain in the list; there is only one opening per load.
This also reports an automatic in-memory adoption when auto-fix is enabled.
Deliberately ignoring an existing tree does not auto-open the window, nor does
the normal absence of distant terrain. Player mode does not auto-open it.
Closing the window does not repeat the request or accept a repair.

Select its recovery message and click **Fix** to adopt the temporary tree as
modified route content. Fix does not write files and opens no confirmation dialog.
The adopted tree appears in the ordinary unsaved-content list; disk writes still
require the ordinary Save workflow. Ignoring Fix leaves saved metadata untouched,
including when other route edits are saved. Global write-disable blocks Fix.

With `core.route.validation.autoFix=true`, eligible missing-index recovery is
adopted automatically, still without startup writes. Existing corrupt/partial
metadata is reported instead of automatically replaced: explicit Fix builds and
activates its reconstruction. An existing intentionally empty index is preserved.

Reconstruction cannot recover intentionally populated nodes with no terrain files.
Skipped descriptors and other limitations are explained in the message. On saving
an adopted replacement, existing metadata is backed up under `TD/recovery-...`.
Write failures are reported and keep the tree modified for retry.

Fix is disabled after adoption or when its route/session is unavailable. Callback
checks also protect against stale actions after route change or tree reload. This
does not repair the older lifetime issues of unrelated Select Object pointers.
See the [QuadTree task](../tasks/terrain/terrain-simple-lookup-migration.md) for
implementation details, limitations and verification.

## Implementation and integration

| File | Responsibility |
| --- | --- |
| [ErrorMessage.h](../../src/tsre/ErrorMessage.h), [implementation](../../src/tsre/ErrorMessage.cpp) | Timestamp, severity/source, description/action, optional coordinate/object pointers |
| [ErrorMessagesLib.cpp](../../src/tsre/ErrorMessagesLib.cpp) | Process-static vector, lazy singleton window, append and visible-window refresh |
| [ErrorMessagesWindow.cpp](../../src/routeEditor/ErrorMessagesWindow.cpp) | Reverse-order list, colors, row selection and signal forwarding |
| [ErrorMessageProperties.cpp](../../src/routeEditor/ErrorMessageProperties.cpp) | Selected-message details, Jump and Select Object |
| [RouteEditorWindow.cpp](../../src/routeEditor/RouteEditorWindow.cpp) | Tools action and connections to editor navigation/selection |
| [Tile.cpp](../../src/tsre/world/Tile.cpp), [TDB.cpp](../../src/tsre/tdb/TDB.cpp) | Main validation/automatic-repair callers |
| [SettingsRegistration.cpp](../../src/settings/SettingsRegistration.cpp) | Auto-fix and preload policy |

Existing producer convention is to allocate an `ErrorMessage`, optionally attach
location/object, and push it once. Object checkers generally push internally and
return the same pointer so the caller can inspect/change repair state: do not
push that returned message a second time. `PushErrorMessage()` returns an empty
QString today; there is no synchronous user response/repair result.

Navigation coordinates must follow the editor's convention. Existing World
producers often use `setLocationXYZ(x, -y, position[0], position[1], -position[2])`;
do not copy those sign conversions blindly to a producer with a different frame.

### Current technical limitations

- Messages are raw pointers in a public, process-global vector. The reviewed
  service has no route-session reset, retention limit, deduplication or cleanup
  API. Object pointers are not validated against deletion/reload before selection.
- `setLocation()` borrows a pointer, whereas `setLocationXYZ()` may allocate one;
  the empty destructor does not establish ownership/cleanup. The copy constructor
  omits location/object, and the default constructor does not initialize type/source.
- Every visible-window push clears and rebuilds the entire `QTreeWidget`.
  A burst of N messages can perform quadratic list work and disturb selection.
  With the window hidden, appends do not rebuild it; showing it rebuilds once.
- There is no synchronization or GUI-thread dispatch in the service. It must not
  be assumed safe to call from texture/terrain workers.
- The row-to-message lookup uses `QTreeWidgetItem::type()` as a vector index.
  There is no stable message identity for future clearing/filtering/eviction.
- Current T-file/terrain failures often use `qWarning()` or returned error strings
  instead. They do not automatically appear here. The same distinction applies
  to standalone scanners and their own reports.

These are source-review findings, not claims that each potential failure has
been reproduced interactively. Follow-up scope and tests are in the
[improvement task](../tasks/editor/errors-and-messages-improvements.md).
