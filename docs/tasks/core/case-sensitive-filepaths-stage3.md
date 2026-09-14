# Content case conversion: execution v1

Implemented after stage A and the binary-SD/planner follow-up. This extends the
main executable's `--contentcase` command with conversion, saved-plan execution,
strict verification, and rollback. The supplied `C:\trainsim` and
`C:\MagiPacks\Microsoft Train Simulator` installations have not been modified or
used for write tests. Full installation-scale execution has not been evaluated.

## Commands

```powershell
# Default invocation performs supported repairs; a prior dry run is optional.
.\build\TSRE5vc.exe --contentcase "D:\GameRootCopy" --journal "D:\CaseJournal" --report "D:\conversion.md"

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

Resolved reference edges and texture/season/representation groups connect files
whose repair decisions depend on each other. Read/sync errors, unknown source
families, links, and failed writers protect their affected components. Incomplete
reference discovery also protects default asset scopes and discovered lookup
bases. This is deliberately conservative: an incomplete route source can withhold
its route, and an unread shared S image table can protect textures in many routes.
A parent directory rename is withheld when it would carry a protected descendant.
Independent leaf/directory repairs use the paths of the actually selected subset.

Missing content remains a warning. It is not substituted from another scope.
Case-only duplicate-file consolidation and directory-collision merging are **not
enabled in execution v1**. They retain planner errors and are withheld; the
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

All edited sources receive exact-byte backups and staged replacements before
content mutation. A second full preflight scan detects changes during preparation.
This adds scan cost; installation-scale performance still needs evaluation.
Replacement bytes and rename-source hashes are checked again before each action.

`journal.json` contains immutable actions and original metadata. `events.jsonl`
is an append-only, flushed recovery log. Each action records an intent before it
runs and a completion afterward. Case-only renames use unique temporary sibling
names; files move before directories, and directories move deepest first. Native
no-overwrite filesystem renames are used without cross-filesystem copy fallback. Replacement
writes are atomic, without direct-write fallback. Edited files retain permissions
and modification times where the platform supports the required calls.

A per-root lock prevents overlapping converter processes. A persistent marker
beside the root identifies an interrupted journal and prevents another conversion
until it is recovered. Successful execution clears the marker. Keep editors and
other content writers closed during conversion; the checks do not make a whole
multi-file game root atomic against arbitrary concurrent writers.

On an ordinary execution/verification failure, the converter attempts rollback
and reports its outcome and journal path. After interruption, explicitly invoke
`--rollback`. Recovery infers whether an intent-only move happened by checking
exact paths and hashes. A torn final log record can be discarded. Rollback is
idempotent and itself recoverable; original edited bytes come from checked backups.
It refuses to overwrite later changed files or directory contents. Recovery also
checks the original path/type layout; unexpected entries (including possible
writer temporaries) are reported and never blindly deleted. If it cannot
finish, keep the journal and backups and resolve the reported obstruction.
Power-loss behavior still depends on filesystem/platform guarantees; tests cover
process-interruption states, not physical power loss. Backups are retained after
success and rollback; there is no automatic backup cleanup command.

## Validation

Temporary-fixture tests cover text encodings and compression, exact binary payload
preservation, SD root edits, pure renames, conflicting incoming spellings, two-part
vehicle references, shared routes/shapes, seasonal DDS names, stale plans,
interrupted temporary-name moves, torn journal tails, automatic rollback on a
concurrent file change, refusal to overwrite a later edit, and isolation of an
independent broken route. CLI cases cover direct execution, read-only plans,
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
