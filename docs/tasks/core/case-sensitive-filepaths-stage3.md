# Content case conversion: execution v1

Implemented after stage A and the binary-SD/planner follow-up. This extends the
main executable's `--contentcase` command with conversion, saved-plan execution,
strict verification, and rollback. The supplied `C:\trainsim` and
`C:\MagiPacks\Microsoft Train Simulator` installations have not been modified or
used for write tests. Execution and rollback have also been validated on a temporary copy of the WSL
installation; see the test results below.

## Commands

```powershell
# Default invocation performs supported repairs; a prior dry run is optional.
.\build\TSRE5vc.exe --contentcase "D:\GameRootCopy" --journal "D:\CaseJournal" --report "D:\conversion.md"

# Overlay an extracted update and repair its references in one transaction.
.\build\TSRE5vc.exe --contentcase "D:\GameRootCopy" --overlay-update "D:\ExtractedUpdate\GameRoot" --journal "D:\UpdateJournal" --report "D:\update.md"

# Read-only plan; optional JSON and Markdown outputs must be new.
.\build\TSRE5vc.exe --contentcase "D:\GameRootCopy" --plan "D:\plan.json" --report "D:\plan.md"

# Apply a saved plan, after checking it against a fresh plan.
.\build\TSRE5vc.exe --contentcase "D:\GameRootCopy" --apply "D:\plan.json" --journal "D:\CaseJournal"

# Read-only verification, including exact component spelling on Windows.
.\build\TSRE5vc.exe --contentcase "D:\GameRootCopy" --verify

# Restore original paths and edited bytes using the transaction journal.
.\build\TSRE5vc.exe --contentcase "D:\GameRootCopy" --rollback "D:\CaseJournal\journal.json"
```

`--journal` takes a **new directory**, outside the game root, on the same
filesystem, with an existing parent. If omitted, execution chooses a unique
`TSRE-case-<UUID>` sibling directory. `--rollback` takes its **journal.json file**.
No journal is needed when nothing can or needs to change. Plans from older
scanner versions must be regenerated: saved plans must exactly match the fresh
planner's inventory, reference data, and decisions.

Execution now begins with a lightweight core-directory bootstrap, before the
reference/content analysis. It canonicalizes fixed root directories, including
`ROUTES`, `GLOBAL`, `TRAINS`, `SOUND`, and `TEMPLATE`; fixed `GLOBAL`, `TRAINS`,
and route subdirectories; `TRAINS/TRAINSET`, `TRAINS/CONSISTS`, and
`TRAINS/CONSISTS/RANDOMSETS`; and recognized route extensions such as
`OPENRAILS`, `ADDONS`, `PROCEDURAL`, `TRACKPROFILES`, and `TERRAIN_MAPS`.
Dynamic TRAINSET product folders and arbitrary asset directories are not forced
to uppercase. `ROUTES/<route>/ACTIVITIES/OPENRAILS` is also a fixed structural
directory and is normalized before activity content is scanned.

If several case variants of a fixed directory exist, an exactly canonical
directory is preferred. Other variants are moved whole into the transaction
vault, then unique subtrees are moved back into the canonical tree. Matching
directories are merged recursively. A colliding canonical-side file is kept;
the alternate stays byte-for-byte in `bootstrap-vault` and is reported. This
keeps the live game root usable without silently overwriting either version.
Bootstrap changes, retained conflicts, and later content repairs share one
rollback journal.

`--overlay-update <update-root>` is an execution-only mode for applying an
already extracted content update. It runs after the fixed-directory bootstrap
and before reference analysis. Each update path is resolved component by
component without case sensitivity. When a destination exists, its one existing
spelling is retained and the update bytes replace it; this prevents Linux from
creating pairs such as `Night`/`NIGHT` or `file.ace`/`FILE.ace`. Missing files
and directories are added, while files absent from the update are not deleted.
Fixed structural directories newly introduced by the update use their canonical
spelling. A source with case-only sibling duplicates, a destination with an
ambiguous case-only match, a type conflict, or a link is refused before overlay
mutation.

The update source remains read-only. Source and destination inventories and
hashes are checked again after staging, and the installed overlay is verified
before content references are repaired. Bootstrap moves, update replacements
and additions, and content-case repairs share one v2 journal and roll back to
the exact pre-command target layout. `--overlay-update` cannot be combined with
`--plan`, `--apply`, `--verify`, or `--rollback`; the update transaction itself
produces its plan during checked staging. Both the journal and optional report
must be outside the update source tree as well as outside the game root.

Exit 0 means the requested operation completed with no known outstanding repair
proposals/errors; missing content warnings are allowed. Exit 1 means a partial
conversion or unresolved verification result. Exit 2 means invalid arguments,
stale input, or an execution/recovery error. A verified repair can still have
`coverageCertified: false`; unsupported formats are not certified by this tool.
`applyReady: false` on a dry-run plan describes the lack of whole-plan coverage
certification, not a prohibition on executing independently checked components.

## Reference preservation

The executor reparses each source and checks every edited scalar against its
original value. It patches source spans, rather than saving editor objects:

- Text: preserve UTF-8 (including BOM), UTF-16LE/BE, or the scanner's legacy
  byte-text encoding. Preserve bytes outside filename spans, comments, and
  line endings. Quote/escape replacement values as necessary. Composed quoted
  filename strings can be replaced within their own scalar span.
- Binary S/SD/W/WS/T: replace only supported string payloads and adjust their
  containing block lengths. All other payload bytes stay in place unchanged.
  Offsets are obtained from a fresh schema-aware parse, not accepted as arbitrary
  addresses from an external JSON file.
- Compressed SIMIS: retain the original envelope form and update its declared
  decompressed size. Recompression may produce different compressed bytes;
  unrelated decompressed bytes must remain identical.
- Includes: edits are made in the include that owns the source span, with the
  discovered owner context used for lookup. Conflicting demands on one scalar
  withhold the affected component.
- Consist/activity EngineData/WagonData: update the vehicle-name and directory
  scalars separately, rather than writing a combined path into the name scalar.

Every staged replacement is reparsed. Field structure and all retained scalars
must match, apart from explicitly requested filename changes. A recovered or
incompletely understood source stays subject to rename-targets-only constraints;
if a required patch cannot preserve it, that component is withheld and reported.

## Selection, verification, and remaining limits

Readability and editability apply to individual source files, not to their entire
connected reference graph. Unreadable or incompletely parsed source bytes are not
rewritten; their errors remain in the result. Their filenames and containing
runtime directories can still be repaired. Unknown files are preserved as opaque
content rather than freezing their route. `.tdb`, `.rdb`, `.tit`, `.rit`, and
`sigscr.dat` are explicitly classified as non-reference data; references to their
names can still require renames. This is a narrow exception: `ttype.dat`,
`telepole.dat`, `ssource.dat`, `speedpost.dat`, and `sigcfg.dat` remain parsed
reference-bearing catalogs, and other DAT files are not assumed reference-free.

Known references in noneditable sources still constrain their targets. Selection
withholds only the specific path-component renames that disagree with those
references, then recomputes paths and edits until the constraints stabilize. A
failed preservation writer uses the same local constraints. No error propagates
through a shared shape/texture graph merely because files are connected. The
planner still coordinates seasonal and alternate-representation names globally.

Physical file collisions remain local errors and are never overwritten. During
the fixed-directory bootstrap, alternate directory trees are merged by moving
only unique entries into the canonical tree; colliding alternates remain in the
transaction vault and are reported. Collisions elsewhere remain in place and are
withheld by the normal planner. Links and ancestors carrying links remain
protected. Excluded entries receive no individual repair operations, but can move
with a repaired containing directory. Undiscovered references in unreadable or
unknown content cannot be verified; this limitation remains explicit rather than
preventing supported repairs elsewhere.

Missing content remains a warning. It is not substituted from another scope.
Directory-collision merging is deliberately limited to fixed structural roles in
the bootstrap phase. General duplicate-file consolidation is not enabled: the
executor never overwrites one case variant with another. Unknown ENV water-map
lookup rules, optional-empty-field semantics, and undiscovered include owners
remain explicit limitations from stage 1. Root-wide compatibility is not claimed.

After mutation, a fresh scan checks that every previously resolved supported
reference still selects its original target file, mapped to its new path. Lookup
context, field identity, suffix/representation, and seasonal edges participate in
that comparison. Previously exact references must not become inexact. The final
inventory must contain exactly the expected paths and entry types; edited and
inspected source hashes, and untouched file sizes/timestamps, are checked.
Complete results have no remaining proposals. Partial results retain errors,
withheld operations, writer errors, missing-content warnings, and remaining counts.

## Transaction and recovery

The core-directory bootstrap inventories the tree without parsing content, writes
its recovery manifest and active marker, and applies no-overwrite renames before
the first full content scan. Directory content hashes protect recovery; cached
Merkle-style hashing avoids rereading the same bootstrap files for nested merges.
The resulting lightweight path/type/metadata inventory is checked before content
analysis starts.

All edited sources receive exact-byte backups and staged replacements before
content mutation. A second full preflight scan detects changes during preparation.
This adds scan cost; large-installation performance still needs evaluation.
Replacement bytes and rename-source hashes are checked again before each action.

`journal.json` contains immutable actions and original metadata. `events.jsonl`
is an append-only, flushed recovery log. Each action records an intent before it
runs and a completion afterward. Case-only renames use unique temporary sibling
names; files move before directories, and directories move deepest first. Native
no-overwrite filesystem renames are used without cross-filesystem copy fallback. Replacement
writes are atomic, without direct-write fallback. Edited files retain permissions
and modification times where the platform supports the required calls.
Update-overlay journals use schema v2; the rollback command accepts both the
original v1 conversion journals and v2 overlay journals.

A per-root lock prevents overlapping converter processes. A persistent marker
beside the root identifies an interrupted journal and prevents another conversion
until it is recovered. Successful execution clears the marker. Keep editors and
other content writers closed during conversion; the checks do not make a whole
multi-file game root atomic against arbitrary concurrent writers.

An execution failure before all actions finish still attempts rollback and reports
its outcome and journal path: an unfinished case-only rename pair or partially
updated reference set is not a valid content state. Once every action has finished,
however, a final scan or verification problem is recorded as an iterative `partial`
result. Completed repairs remain in place, the active marker is cleared, and a new
invocation rescans that state instead of spending another full pass undoing it.
Non-writable edit sources are withheld during preparation and reported as writer
errors before the transaction begins. After interruption, explicitly invoke
`--rollback`. Recovery infers whether an intent-only move happened by checking
exact paths and hashes. A torn final log record can be discarded. Rollback is
idempotent and itself recoverable; original edited bytes come from checked backups.
It refuses to overwrite later changed files. A bootstrap directory whose bytes
changed can be moved back only if its complete entry structure still matches the
recorded tree, so later file edits are preserved while additions, removals, and
type changes stop automatic recovery. Recovery also checks the original path/type
layout; unexpected entries (including possible writer temporaries) are reported
and never blindly deleted. If it cannot
finish, keep the journal and backups and resolve the reported obstruction.
Power-loss behavior still depends on filesystem/platform guarantees; tests cover
process-interruption states, not physical power loss. Backups are retained after
success and rollback; there is no automatic backup cleanup command.

Execution selection also preserves implicit S/SD companion stems and exact
seasonal-only texture spellings when another blocked operation removes the edit
which originally coordinated them. Final reference verification retains every
equivalent edge sharing a lookup key; a later duplicate edge cannot hide an exact
edge to the same physical target.

## Validation

Temporary-fixture tests cover text encodings and compression, exact binary payload
preservation, SD root edits, pure renames, conflicting incoming spellings, two-part
vehicle references, shared routes/shapes, seasonal DDS names, stale plans,
interrupted temporary-name moves, torn journal tails, automatic rollback on a
concurrent file change, refusal to overwrite a later edit, fixed-directory
case-variant merging with identical and different file collisions, exact bootstrap
rollback, and isolation of an independent broken route. CLI cases cover direct execution, read-only plans,
saved-plan execution, strict verification, rollback, and identical direct/saved
outputs. Final checks passed:

- Windows: Qt 6.10.1 / MinGW 13.1, main executable and content-case test target
  build successfully; `content_case` CTest passes. The main executable's
  `--contentcase --help` startup succeeds.
- WSL Arch Linux: Qt 6.11.2 / GCC 16.2.1, standalone Qt Core test suite passes
  with fixtures on the case-sensitive `/tmp` filesystem. This is the converter
  module and CLI suite, not a full Linux GUI build.
- Tests operate on temporary fixtures only; neither supplied installation was
  modified, and no large-root execution scan was run.

The standalone Qt Core test project can run on Linux without building the GUI:

```sh
cmake -S tests/contentCase -B /tmp/tsre-content-case-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/tsre-content-case-tests -j 4
ctest --test-dir /tmp/tsre-content-case-tests --output-on-failure
```

### Installation-test verifier correction

The first WSL installation conversion rolled back with an apparent changed
`Tutorial Route.rdb` target. The verifier had keyed all four optional TRK
companions (`.rdb`, `.tit`, `.rit`, `.ref`) identically, so the last edge replaced
the others in its lookup. This was reproducible by comparing a plan with itself.
The key now includes the implicit suffix and the derived-DDS flag; the latter
also distinguishes seasonal ACE/DDS pairs sharing one field and search base.
Regression tests cover unchanged companions, rejection of each missing companion,
execution/rollback with companions, and coexisting seasonal ACE/DDS assets.
The installation journal recorded completed rollback; the installation was not
reconverted while testing this correction.

### Narrow protection regression validation

WSL Qt Core tests cover route database companions connected to world assets,
opaque files carried by runtime directory repairs, noneditable ENV references,
physical collisions, and a broken route shape connected to another route through
a shared global shape. The reference-bearing DAT catalogs listed above are
asserted to remain scanned; `sigscr.dat` is renamed without rewriting its payload.

A temporary copy of `/root/msts/proprietary/msts_app` (4,902 files) was converted
and rolled back after the protection change. It applied 1,542 transaction steps,
left **zero proposed operations, zero withheld operations, zero writer errors,
and zero case-mismatched known references**. The only remaining errors were the
two already broken `JP1Signal2.s` files in MINI and PROCEDURAL. Tutorial's WORLD,
SHAPES, TEXTURES, and ENVFILES directories adopted runtime case, with all 20 world
files present. Rollback restored every original copied path, byte hash, file
permission, and millisecond modification time. The original installation was not
modified. This verifies conversion/preservation; it is not an interactive editor
rendering test.
