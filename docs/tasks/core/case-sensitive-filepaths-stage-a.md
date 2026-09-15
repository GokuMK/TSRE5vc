# Case-sensitive filepaths: stage A

Implementation following the accepted [design](case-sensitive-filepaths.md) and
[stage-1 handoff](case-sensitive-filepaths-stage1.md). The two supplied installations
are not conversion targets during this work. Tests use temporary content.

## Runtime behavior

Physical paths retain authored spelling, including gameroot, referenced filenames,
trainset product directories, includes, and route `FileName` stems. Separator
normalization preserves UNC prefixes. The old `Game::caseInsensitiveFS` member is
retained for source compatibility; it no longer controls content loading or keys.
Parser tokens, symbolic names, and material-content hashes retain their existing
case-insensitive semantics.

Direct-access structural directories use uppercase. Newly created route directories
use uppercase; discovered directories retain their actual spelling. Fixed catalog
filenames and generated suffixes use lowercase. Enumerating a `.TRK` preserves its
complete filename, including a dotted stem and uppercase suffix, for subsequent
loading/saving. Explicit `.S` still implies a lowercase `.sd` companion with the
same stem in all three MSTS shape backends. Implicit DDS alternatives similarly
use `.dds`, while an explicitly authored ACE suffix is retained.

The path audit covers root checks, editor discovery and recent roots, route
creation/loading/saving, world/terrain/database paths, procedural assets, vehicle
and activity content, include loading, shape/texture libraries, and sound loading.
Bundled `assets` and `appdata` paths keep their existing layout. The planner's
structural directory policy also covers route `ADDONS`, `PROCEDURAL`,
`TRACKPROFILES`, and `TERRAIN_MAPS`; these names are structural only in the route
context, not arbitrary referenced asset directories. Known fixed names
`sigscr.dat`, `terrainmaterials.dat`, `weathertransitions.dat`, and
`PROCEDURAL/shapetemplates.dat` receive lowercase filename proposals.

Seasonal directories use the agreed uppercase spellings. Terrain retains semantic
rain/snow/base fallback and its existing DDS preference, but no longer enumerates
case variants of directories or texture names to make a missing path load.
Root validation can list a differently spelled required directory for diagnosis;
it does not load from that candidate.

## Identity and sharing

Updated after the Conedit performance review: `ContentPath` preserves I/O paths
and constructs case-insensitive logical keys. Runtime caches now match those
keys directly, including differently cased requests. Physical collisions are
converter diagnostics, not a reason to reinterpret cache equality. This replaces
the original stage-A per-hit readability/filesystem-equivalence policy.

Engines, consists, activities, services, traffic, paths, and SMS definitions store
`hashid`; route-owned objects also store `routeHashid`. Incoming keys are prepared
once per lookup. Constructors/copies and supported rename methods maintain stored
keys. Failed entries remain ineligible; unsaved/modified objects participate in
the same logical identity rule. Following the performance experiment, EngLib now
adds a `QHash<QString,int>` path index while retaining its numeric-ID map. Other
libraries currently retain cached-key linear lookup. EngLib registers the index
in `addEng`, removes matching failed IDs in `removeBroken`, and clears it in
`removeAll`. A failed entry cannot mask a later successful retry. Engine filenames
are fixed after construction; the current engine reload flow clears/reloads the
library, while `Eng::reload()` reloads shapes only. Future engine rename or bulk
registration APIs must maintain the index rather than writing directly to `eng`.

Shape sharing includes the texture root and season. Backend and load options
remain fixed per library. Shape path keys and texture lookup keys are stored
separately from source paths; sequential IDs remain unchanged. No persisted
numeric path hash or replacement asset-ID scheme was introduced.

Textures first match the requested representation using stored alias strings.
Only a miss probes ACE-to-DDS fallback and searches its key; an existing ACE is
still distinct from a DDS fallback. Ordinary cache hits do not probe the disk. Synthetic glTF,
terrain, paint, and map identities remain exact strings. Published terrain content
keys can still refer to a formerly private generated texture. Retiring texture
lookup entries after a bake still prevents stale reuse without mutating an
asynchronous loader's source path.

Vehicle cache hits retain the previously selected source. An explicit reload
selects a newly added Open Rails override or changed base source.
Consist discovery remembers the gameroot associated with its list. Services,
traffic and paths keep case-insensitive logical names within their owning route;
activity definitions and timetables carry that route context. Settings profile
selection and per-profile UI state no longer collapse distinct case-only paths.
Profile creation still rejects case-only duplicate names for portability.

## Validation

The `content-path` application test suite exercises real file loading and temporary
fixtures: root conventions, UNC spelling, uppercase/dotted TRK discovery, SD
companions, shape texture/season context, image loading and failed-load recovery,
ACE/DDS cache order, synthetic terrain identities, seasonal lookup, route-scoped
names, service saving, vehicle includes/overrides, and consist discovery.

On Windows the suite tries enabling case sensitivity on its own temporary
directory only, then reports the actual behavior. Wrong-case and competing-file
checks execute when the fixture filesystem supports them. This is test setup,
not runtime filesystem probing or a fallback policy.

Validated on 2026-09-15:

- Windows main executable, converter tests, and portable path tests build
  successfully with Qt 6.10.1 / MinGW 13.1.
- `content-path`: **51 passed, 0 failed**. The temporary Windows directory
  successfully enabled case sensitivity, so wrong-case and competing-file checks
  ran rather than being skipped.
- Standalone core path/season tests on Linux (WSL, Qt 6.11.2): **20 passed,
  0 failed**, on the Linux temporary filesystem.
- `terrain-material`: **551 passed, 0 failed**; `shape-complex`: **54 passed,
  0 failed**; `orts-profile`: **21 passed, 0 failed**; `tdb-load`: **15 passed,
  0 failed**. The settings suite also exited successfully.
- CTest `simis_tokens`, `shape_document`, `content_case`, and `content_paths`:
  **4/4 passed**. Converter fixtures verify the additional fixed catalog and
  track-profile directory conventions.

The first focused run exposed two incorrect test inputs (an SD header declaring
the shape format, and a string literal selecting the Boolean texture overload).
They were corrected before the successful run. A subsequent batch launcher
incorrectly forced an offscreen Qt platform absent from the deployed plugin set;
it produced repeated startup failures and was removed. Those failures are not
counted as test results. Validation above used normal Windows launches.
The `terrain-files` and `route-load` corpus suites were invoked without their
required fixture arguments and rejected the invocation; they are not counted as
validated suites. Neither supplied installation was scanned or modified.

Local validation logs are under `build/stage-a-*` (ignored build artifacts).
No end-to-end conversion is claimed: stage B still has no apply mode.

## Boundary with stage B

This does not add conversion execution, reference writers, or rollback. The
converter remains read-only with `applyReady: false`; existing coverage and
diagnostic limitations in the stage-1 handoff remain. Runtime case searching is
not a substitute for repairing content before loading it on a case-sensitive
filesystem. No full scan of either supplied installation is needed for this
runtime migration.
