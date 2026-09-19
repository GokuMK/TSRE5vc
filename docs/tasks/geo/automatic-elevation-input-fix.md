# Automatic elevation: suppress progress windows

2026-09-16, `geo-terrain`.

The fix is limited to `HeightWindow.cpp`: automatic loading keeps its progress
window hidden and non-modal, resets its auto-show timer, and skips progress
updates that could show it again. Manual preview keeps its existing progress
window. Automatic fallback reports use ShowWithoutActivating.

The existing worker and event-loop flow is retained. There are no new queues,
viewport/camera pauses, input guards or terrain publication paths.

## SZKLARSKA inspection (read-only)

`C:/trainsim/ROUTES/SZKLARSKA` contained 19 terrain descriptors, 19 RAW files,
19 World files and 19 populated 2048 m quadtree entries: 18 additional tiles.
The original appears to be `-11df9890.t` / `w-005514+014891.w`.
Inventory: `build-elevation-diagnostics/szklarska-inventory.json` in the worktree.
No route files were changed or deleted.

There is no exposed quadtree rebuild/remove command in this checkout. Route
loading reads TD files; it does not rebuild them by scanning TILES. Cleanup
therefore needs matching TD restoration or a separate rebuild helper.

## Verification

Diff checked. Main build/tests were not run, as requested. After a Release build,
verify that automatic generation shows no progress window and releasing the
movement key stops the camera. Manual preview should still show progress and
support Cancel.
