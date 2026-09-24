# QuadTree recovery and removal of TerrainLibSimple

Status: implemented and automated tests passed, 2026-09-21; user reports recovery
and targeted startup-message presentation working in interactive testing.
Replaces the earlier arithmetic fake-QuadTree proposal.

## Selected design

All local routes use `TerrainLibQt`, including when
`core.advanced.useQuadTree=false`. The duplicated `TerrainLibSimple` and its
private fixed-8-m `setHeight256()` helpers are removed.

Disabled saved-QuadTree lookup means **reconstruct a temporary real tree from
terrain descriptors**, not "always return a 2 km tile". This shares reconstruction,
supports variable footprints and encourages repairing the route. It scans once at
load; it does not scan disk per sample or per render lookup. Existing height
editing, edge caches, LOD, precomputed/paged rendering and picking are reused.

Missing QuadTree metadata is a route defect, not a new valid storage format.
An existing deliberately ignored tree is not necessarily damaged. Ignoring its
metadata does not validate it.

## Startup and repair policy

| Situation | Rendering/lookup | Persistence |
| --- | --- | --- |
| Saved lookup enabled, valid index (including intentionally empty) | Saved tree | Existing behavior |
| Saved lookup enabled, index missing | Temporary reconstruction | E&M Fix adopts it; auto-fix may adopt automatically |
| Saved index/TD unreadable, malformed or incomplete | No automatic reconstructed replacement | E&M offers Fix; reconstruction/activation happens on explicit Fix |
| Saved lookup disabled | Temporary reconstruction, existing saved metadata not parsed | E&M Fix can adopt; existing ignored metadata is never automatically replaced merely because lookup was disabled |
| No distant index and no distant descriptors | Empty distant lookup | No spurious missing-distant error |

With `autoFix=false`, ignoring E&M means the temporary tree is not adopted.
Ordinary route Save must leave saved metadata untouched and missing metadata absent.

**Fix and eligible auto-fix only adopt in memory and mark modified.** The adopted
tree appears in the ordinary unsaved-content list. The user still confirms saving
through the ordinary save dialog. There are no additional confirmation dialogs,
save prompts or approval menu commands. Discarding/cancelling does not write the
reconstruction. Global write-disable prevents repair acceptance and disk writes.

Automatic adoption is restricted to genuinely missing indexes with no skipped
descriptors. Existing damaged metadata requires explicit Fix even with auto-fix
enabled. A missing index with surviving TD files still requires preservation of
those files before the eventual replacement save.

## Reconstruction

Implementation: [QuadTreeRecovery.cpp](../../../src/tsre/world/QuadTreeRecovery.cpp).

- Scan `TILES` and `LO_TILES` independently. World files are not an inventory.
- Decode the terrain filename into placement and hierarchy level; inspect
  descriptor sample count/spacing/patch count and check physical-size agreement.
- Support every level in the existing TD hierarchy: 2, 4, 8, 16, 32, 64, 128,
  256 and 512 km. Do not hardcode reconstruction to 2 km or distant 32 km.
- Do not generate heights, textures or meshes while scanning. Missing/corrupt RAW
  resources remain terrain-loader failures; a reconstructed index is not a full
  route-health certificate.
- Report unsupported names/layouts and filename/descriptor disagreements rather
  than guessing placement. Report skipped descriptors in the recovery message.
- Preserve the normal tree's smaller-populated-node-first lookup when larger
  and smaller authored footprints coexist.
- A descriptor scan cannot recover deliberately populated entries without payloads.
  State that limitation in E&M. An originally empty valid index is not missing.
- Keep terrain coordinates separate from the independent 2048 m World grid.
  Test every coordinate sign and the filename whose numeric payload is zero.
- Empty-space lookups must not grow the TD map. Existing failed terrain loads stay
  cached by the common backend until explicit reload/replacement.

The abandoned arithmetic `Direct2km` alternative would have avoided the startup
scan but could not discover larger footprints. It is retained here as a design
comparison, not implemented alongside reconstruction.

## Persistence and creation

`QuadTree::SavePolicy::{Immediate,Deferred}` makes insertion saving explicit.
Existing tile creation keeps immediate saving on ordinary saved trees.
Reconstruction uses deferred insertion. Temporary trees refuse disk saving even
if an existing creation caller requests immediate saving. Adopted recovery trees
also defer subsequent insertions to ordinary Save.

That preserves today's immediate terrain-payload creation behavior without letting
B or automatic tile generation accidentally persist a temporary recovered tree.
A wider migration of all creation to save-on-route-save is outside this task.

On confirmed save:

- Create `TD` if needed, respecting write-disable.
- Before replacing metadata from a reconstructed tree, back up existing index and
  TD files for that domain into a uniquely named `TD/recovery-...` directory.
  Do not rewrite the other domain, World files, heights or textures.
- Use checked atomic-per-file writes, TD files before the index. Retain modified
  state until every write succeeds; report failures and allow ordinary Save retry.
- This is not a filesystem-wide transaction: a crash between file commits may
  require restoring the retained backup. Do not claim all-file atomicity.
- Keep temporary/adopted state separate from dirty state. A temporary tree is not
  a pending route save. A repaired-in-memory message is not a successful save.

The existing tree loader now distinguishes missing, loaded and invalid metadata.
It validates index structure, TD block framing/counts and subdivision depth before
adopting the loaded tree. A failed checked load does not overwrite a previously
loaded tree object.

## Errors and Messages integration

Missing/invalid metadata requests non-modal E&M presentation after the route
editor opens, selecting the first current recovery message and showing its Fix
details. Auto-fix adoption is also reported this way. Ignored existing metadata,
healthy routes and normal absence of distant terrain do not request attention.
Requests are consumed once, do not repeat on closing the window, and expire with
the route/recovery session. Player mode does not auto-open the editor tool.
This reusable presentation API is independent of repair/adoption and never
writes metadata; no modal confirmation dialog was added.

See [current window behavior](../../features/errors-and-messages.md) and
[general improvement backlog](../editor/errors-and-messages-improvements.md).

An optional Fix button belongs to the selected recovery message. It uses explicit
callbacks, never parses the old free-text `action` description. A weak session
guard, active-library check and route-path check prevent a stale message from
acting on another route or a reloaded tree. Successful adoption disables Fix;
unsaved state remains visible through the ordinary save workflow.

For a temporary tree already used by rendering, Fix adopts that current tree,
including tiles created since startup. For invalid saved metadata, Fix scans at
invocation time and activates the replacement; it does not use a stale startup
snapshot. The broad diagnostics model/ownership/filtering rewrite is not a
prerequisite for this small action integration.

## Construction and network boundary

[Route.cpp](../../../src/tsre/world/Route.cpp) always constructs the common local
backend. [RouteClient.cpp](../../../src/tsre/world/RouteClient.cpp) always uses
`TerrainLibQtClient` and the server-supplied index, never local recovery scanning.
The existing remote setting policy forces saved-QuadTree mode. No multiplayer
protocol redesign or local-disk repair on a remote client is claimed.

Fix/save callbacks are guarded, but this does not solve the older raw object
pointer lifetime issues in unrelated E&M Select Object messages.

## Verification

Standalone target: `tsre_quadtree_tests`, registered as CTest
`quadtree_recovery`. It exercises the production metadata code, not a fake parser.
App suite `quadtree-recovery` additionally checks E&M action/session behavior and
TerrainLibQt's unsaved/save integration without an OpenGL context.

```powershell
ninja -C build -j 2
ctest --test-dir build --output-on-failure -j 1
build/tsre_quadtree_tests.exe
build/tsre_quadtree_tests.exe --scan 'C:/MagiPacks/Microsoft Train Simulator/ROUTES'
build/tsre_quadtree_tests.exe --scan 'C:/trainsim/routes/CMK'
build/TSRE5vc.exe --test --test-suite quadtree-recovery
```

Use the configured Qt/MinGW PATH; CPU app tests may use
`QT_QPA_PLATFORM=offscreen` and `QT_FORCE_STDERR_LOGGING=1`.
Build with at most two jobs. Corpus scans are read-only; writes use temporary
fixtures.

Recorded verification:

- Full default build succeeds, including standalone targets, with two build jobs.
- All nine registered CTest suites pass, including standalone QuadTree recovery.
- App recovery suite: 636 passed, 0 failed. Covers targeted/deferred E&M presentation,
  selection retention, stale requests, the Fix button, missing/invalid/empty metadata,
  auto-fix adoption, no-write-until-Save, disabled-mode 512/4 loading and height
  editing, first distant-tile creation, expired actions and save failures.
- Existing app suites: terrain-grid 66/66, terrain-edges 52/52, terrain-brush
  360/360, terrain-normals 132/132, terrain-tfile 78/78, terrain-material 575/575,
  new-route 27/27.
- Read-only corpus: MSTS installation 20 indexes + 1,169 terrain descriptors;
  CMK two indexes + 1,194 descriptors. All accepted by checked index loading and
  filename/layout agreement checks. No source route metadata was modified.
- CMK reconstruction of 1,194 detailed descriptors: three warm runs 141, 158,
  129 ms, mean 143.006 ms. This is descriptor-only startup work, not mesh loading,
  cold-disk performance or a whole-route FPS benchmark.
- All 18 task-owned English/Polish translation entries were checked. The full
  translation validator has an existing unrelated failure: the committed English
  catalogue contains 36 messages without IDs. A full lupdate also exposes existing
  source-less IDs elsewhere. Unrelated catalogue entries were preserved rather
  than removed/reworked in this terrain task.

Interactive acceptance should exercise a copy of a route with missing metadata:
view/edit variable-size terrain, ignore Fix and save other content, Fix and cancel,
Fix and confirm ordinary Save, reopen, B creation, distant terrain, water, shadows,
both mesh backends and mixed-resolution edges. Also test an existing corrupt tree
and disabled saved lookup. Never remove original user TD metadata for a test.

## Related

- [Terrain task index](README.md).
- [T-file structure/parser](terrain-tfile-structure-and-parser.md).
- [Adjacent edge cache](terrain-adjacent-edge-cache.md).
- [Basic terrain LOD](terrain-basic-discrete-lod.md).
- [Height brush performance](terrain-height-brush-performance.md).
