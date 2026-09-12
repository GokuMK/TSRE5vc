# Case-sensitive filepaths and game-root repair

Status: design only. Reviewed 2026-09-12 against `d425ec2`. No runtime, parser,
content, or filename changes are part of this task.

Revision: incorporates the author's inline review comments. The selected approach
is offline content conversion with simple application paths. Supporting two
different assets distinguished only by case is not a goal.

## Decision summary

Replace the Android-era lowercase-content requirement with case-preserving
paths for filesystem access and case-insensitive logical asset keys where useful.
Provide an offline command mode in the main TSRE executable that inventories a complete game root and
repairs inconsistent names for the same behavior on Windows and Linux.
Prefer renaming a target to the spelling already used by its references.
Rewrite references only when renaming cannot satisfy them all, or when the user
explicitly chooses a different spelling.

The application requests fixed uppercase directories such as `ROUTES`, `TRAINS`,
and `GLOBAL`. On a case-insensitive filesystem those requests also work with
existing alternative casing. When a required directory is missing, a diagnostic
can identify a likely wrongly named directory and offer to run the repair tool.
The application does not maintain directory aliases or transparently repair
paths while loading. The converter makes all content directories below the game
root uppercase and brings references into agreement with actual filenames.

`Tree.ace` and `tree.ace` in the same resource context mean the same intended
asset. Lowercase comparison/hash inputs can express that identity, but the key
must never replace the correctly spelled path passed to the filesystem. Two
physical files competing for that identity are a content conflict to resolve.

This means **stop forcing lowercase**, not prohibit naturally lowercase names.
If every reference says `largetree.s`, retaining that name is a valid repair.
Recovering `LargeTree.s` from an existing reference is worthwhile; inventing
word boundaries from `largetree.s` should not be a prerequisite for editing.

Agreed delivery order:

1. **B, dry-run only.** Build the standalone file inventory, reference extraction,
   and repair planner, invoked through the main executable. Evaluate whether it
   collects the necessary information from real content. No content writes,
   renames, or save-path work in this stage. If completing the dry run requires
   part A, stop and report the specific dependency; do not implement A early.
2. **A: runtime path and identity correction.** Remove destructive case folding,
   use fixed directory conventions, and separate file paths from lookup keys.
   Address any relevant lowercasing in reusable file-level readers/writers too.
3. **B, repair execution.** Continue with renames, duplicate consolidation,
   preservation-oriented reference edits, verification, and rollback. The finished
   tool can repair directly; a user-run dry run remains optional.
4. **Optional naming improvements.** Prefer existing readable spellings or
   explicit overrides; defer automatic CamelCase generation.

No further user policy decision is required to start stage 1. The conventions
are settled: uppercase content directories below the supplied root; preserve
filename spelling in I/O; use folded logical keys within resource context;
unify case variants of references; and report competing physical files. Normal
implicit suffix rules and reference-driven name selection are described below.
Remaining details such as per-format reference fields/search roots, preservation
coverage, and exact CLI spelling are implementation or discovery work, not a
reason to delay the scanner. A conflicting pair's intended version is naturally
a per-game-root decision reported by the tool, not a global policy to guess now.

The source review below establishes concrete migration hazards. It does not
certify every MSTS/Open Rails extension, and no installed game root was scanned.
The repair tool must report its coverage rather than claim an arbitrary root
is completely repaired just because TSRE can render it.

## A. Findings in the current code

Source links are relative to this document. Function names identify the reviewed
locations; line numbers will change during implementation.

### A1. The existing flag is insufficient

[`Game.cpp`](../../../src/tsre/Game.cpp) initializes `Game::caseInsensitiveFS`
to `true`. The production-source search found conditional uses, but no automatic
filesystem detection or production assignment that makes it a complete Unix
mode. Tests sometimes override it. Turning it off alone does not solve this task.

| Area / source | Current behavior and consequence | Proposed change |
| --- | --- | --- |
| [`ShapeLib::addShape`](../../../src/tsre/shape/ShapeLib.cpp), [`TexLib::addTex`](../../../src/tsre/texture/TexLib.cpp), [`EngLib::addEng`](../../../src/tsre/trains/EngLib.cpp), [`ConLib::addCon`](../../../src/tsre/trains/ConLib.cpp) | Conditionally lowercase `pathid`; the result participates in identity and may be passed to file loading. This conflates the lookup key with the path to open. | Preserve path spelling; resolve identity separately and use the same identity policy at every entry point. |
| [`Eng`](../../../src/tsre/trains/Eng.cpp), constructors and `load` | The two-argument constructor lowercases paths unconditionally; Open Rails override paths, include roots, and include strings are lowercased in other paths too. The library constructor only avoids part of this. | Preserve both original and override paths; fix nested includes and fallback roots as well as top-level opening. |
| [`ActLib`](../../../src/tsre/trains/ActLib.cpp) | `GetAct` folds conditionally, but `AddAct`, `AddService`, `AddTraffic`, and `AddPath` fold unconditionally. `Service` and `Traffic` themselves retain the supplied spelling, so their stored paths can disagree with the library's deduplication candidate. | Migrate lookup, insertion, constructors, and all by-name consumers together. |
| [`Path::Path`](../../../src/tsre/trains/Path.cpp), [`ConLib::loadSimpleList`](../../../src/tsre/trains/ConLib.cpp) | The path constructor and consist list force lowercase independently of the flag. | Preserve discovered filenames and bind UI entries to asset identity. |
| [`Route`](../../../src/tsre/world/Route.cpp), [`RouteClient`](../../../src/tsre/world/RouteClient.cpp) | Lowercase `trk->routeName`. This is the TRK `FileName` stem used to build related filenames, not merely the displayed route title. | Preserve stem, route folder, and logical RouteID as separate values. Update local and client paths together. |
| [`TSectionDAT::loadGlobal`](../../../src/tsre/tdb/TSectionDAT.cpp) | Lowercases include strings. It first checks the route's Open Rails tsection override, then global tsection. | Preserve includes and their source ownership; inventory every override context. |
| [`ForestObj`](../../../src/tsre/world/objects/ForestObj.cpp), [`Environment`](../../../src/tsre/world/Environment.cpp), [`ProceduralMstsDyntrack`](../../../src/tsre/procedural/ProceduralMstsDyntrack.cpp) | Lowercase resource paths and/or referenced texture names before loading. | Preserve reference strings and use fixed uppercase directory paths for built-in resources. |
| [`SFile`](../../../src/tsre/shape/SFile.cpp), [`SFileLegacy`](../../../src/tsre/shape/SFileLegacy.cpp), [`SFileComplex`](../../../src/tsre/shape/SFileComplex.cpp) | Construct lowercase seasonal folders; old shape loading derives the SD companion using `pathid + "d"`. | Use uppercase seasonal directories and deterministic companion suffixes. The converter normalizes implicit companion names; no runtime case-search mechanism is needed. |
| [`TerrainSeason`](../../../src/tsre/world/TerrainSeason.cpp) | Already searches actual directory spellings, but case-insensitive fallback can choose the first matching directory/component. Missing seasonal directories are synthesized in lowercase. | Replace case-search fallback with fixed uppercase directories and correctly spelled references. Keep seasonal fallback order (for example snow to base); season fallback and case repair are separate concerns. |
| [`LoadWindow`](../../../src/routeEditor/LoadWindow.cpp), [`CELoadWindow`](../../../src/conEditor/CELoadWindow.cpp) | Lowercase recent directories, including when reading saved recent-path lines. A mixed-case game-root parent can be lost before content loading begins. | Preserve selected paths and persisted values. A migrated cache cannot reconstruct case already discarded; rediscover or let the user reselect. |
| [`Ref`](../../../src/tsre/world/Ref.cpp), [`Route::loadMkrList`](../../../src/tsre/world/Route.cpp) | REF refresh deduplicates folded relative filenames; marker maps use folded filenames. Competing physical files can collapse silently. | Retain folded keys where they represent one logical asset, preserve the file spelling separately, and report competing files through repair diagnostics. |
| [`SettingsDialog`](../../../src/settings/ui/SettingsDialog.cpp), [`SettingsProfile`](../../../src/settings/SettingsProfile.cpp) | Profile-file identity and some comparisons fold case. These are outside MSTS content but affect case-sensitive editing workflows. | Audit physical profile paths separately from intentionally case-insensitive profile-name policy. |

There is also a large **literal-path migration**, beyond `toLower()`:

- `Route` checks `/routes` and `/global`, creates lowercase route subdirectories,
  but loads `/ENVFILES/editor.env` with uppercase `ENVFILES`.
- `WorldObj` constructs `/global/shapes`, `/routes/.../shapes`, and texture roots.
- `EngLib` enumerates `/trains/trainset`, while `Consist` constructs
  `/TRAINS/TRAINSET/...` for `EngineData` and `WagonData`.
- Discovery and creation cover activities, services, traffic, paths, terrain,
  route addons, sound, procedural assets, and settings files.

Changing the content to `ROUTES/GLOBAL/...` before correcting these callers will
break existing lowercase literals on a case-sensitive filesystem. New-route
creation, Save As, import/copy paths, exports, and route/client startup must use
the same uppercase directory constants/conventions as readers. These do not
require a layout-discovery service or a runtime case-alias map.

### A2. File hashing, sharing, and actual failure risks

Most legacy asset libraries do **not** hash filenames into numeric asset IDs.
They allocate sequential integer handles, keep maps indexed by those handles,
and linearly scan stored path strings to find an existing asset. Removing case
folding does not inherently break the map implementation. Inconsistent identity
at its callers will create duplicate objects, miss reloads, or hide conflicting
content. Lowercase lookup keys themselves remain useful under the chosen
one-logical-asset-per-case-insensitive-path convention.

| Mechanism | Finding | Required treatment |
| --- | --- | --- |
| `ShapeLib::shape` | Integer handles plus a linear `getPathId()` comparison. `texPath` is ignored when matching existing shapes. A global shape can therefore reuse the first route's texture context. | Keep handles stable. Use a key containing the folded shape path and texture/search context; include backend/load/season options that alter the cached asset, or explicitly partition/invalidate those contexts. |
| `TexLib`, [`Texture::hashid`](../../../src/tsre/texture/Texture.cpp) | `hashid` is a list of string aliases, not a numeric digest. The two-string overload normalizes paths; the one-string overload and `getTex` compare their input directly. DDS textures acquire an ACE alias, and ACE requests can fall back to DDS. | Distinguish the requested reference, resolved source, and supported format alias. Resolve source-selection policy before deduplication; register aliases consistently and do not let a loaded DDS accidentally hide a distinct ACE request. |
| `EngLib`, `ConLib`, `ActLib` | Sequential handles and path scans, with the producer/consumer mismatches above. `ActLib` also searches service/traffic/path logical names without route context. | Add route-scoped identity for route-local assets and explicit indexes for logical names. Preserve format-defined symbolic matching separately from physical filename matching. |
| [`TFile::getMatByTexture`](../../../src/tsre/world/TFile.cpp) | Compares lowercased texture strings. | Retain case-insensitive logical texture matching within its resource context. The converter unifies spellings and reports competing physical files. This does not imply merging material records with different shader parameters. |
| [`TerrainMaterialSource::key`](../../../src/tsre/world/TerrainMaterialSource.cpp), `shaderKey` in [`TerrainProceduralMaterial`](../../../src/tsre/world/TerrainProceduralMaterial.cpp) | SHA-256 inputs explicitly lowercase texture names. Case variants with identical shader parameters produce the same material key. | This is desirable for one intended texture. Retain folded hash inputs; retain exact reference strings in material data used for loading/saving. Ensure route/texture context scopes the cache. Version signatures only if their schema or meaning actually changes. |
| Other terrain comparisons | Procedural brush selection compares filenames case-insensitively; `proceduralSaveCompleted` removes texture aliases using case-insensitive path comparison. `Terrain::preparePaintTexture` uses the global flag to decide source/target equality. | Use a consistent logical key for selection/invalidation and preserve actual edit paths. Do not make identity depend on host OS. Different seasonal directories and source/target roles remain different contexts; a missing or conflicting target must not be hidden by key equality. |
| Terrain bake fingerprints | `bakeSourcesKey` includes path, existence, size, and modification time as well as source keys; bake metadata persists signatures. | A rename can make an existing bake stale. Invalidate/revalidate derived caches; do not silently reinterpret old signatures as the new scheme. Keep material UiDs and authored material maps stable. |
| [`TrackShape::getHashString`](../../../src/tsre/tdb/TrackShape.cpp), [`ProceduralShape::GetShapeHash`](../../../src/tsre/procedural/ProceduralShape.cpp) | A named track shape returns its filename as a key; otherwise section IDs are concatenated. Procedural keys concatenate additional values and template names. These are generated-geometry keys, not reliable file identities; undelimited components can also collide. | Prefer a structured geometry/template key, including relevant route/profile context and geometry revision. Case renames may cause harmless cache misses today, but must not change section IDs or select another shape. Treat concatenation cleanup as related work, not a prerequisite to all path fixes. |
| glTF embedded images in [`GltfShape`](../../../src/tsre/shape/GltfShape.cpp) | `gltfimg:sha256:...` keys derive from encoded bytes, length, and MIME type. Generated paint textures also have synthetic identities. | Keep these namespaces separate from filesystem paths. No lowercasing or filesystem canonicalization of synthetic keys. |
| Settings file digest | `SettingsManager::fileHash` uses SHA-256 over file bytes. | Content fingerprints need no case-policy change; the path used to select the file does. |

**Recommended key representation:** a normalized, lowercase full logical path
plus the resource context required by that cache. Keep the case-preserving path
in a separate field. Use the same key construction on Windows and Linux; do not
fold basenames alone and accidentally merge textures from different directories.
Do not broaden folding into Unicode normalization or other equivalences without
defining matching converter rules. The converter must use the same key policy.

Existing linear scans can compare these keys; an additional
`QHash<AssetKey, AssetId>` index is an optional performance improvement, not a
requirement for case repair. Keep existing handle-to-object storage.
Let hashing accelerate lookup while full-key equality resolves collisions.
Do not replace paths with bare `qHash()` values, persist process hash values,
or use a content checksum as editable-file identity: two byte-identical files
can be independent edit targets. Embedded immutable images are a different case.

Lowercase keys are neither a disk-path resolver nor a content validator. A cache
hit must not make an otherwise missing/unreadable reference succeed only because
another spelling was loaded first. When request spelling differs from the cached
file path, check ordinary file access before reuse and diagnose failure; do not
search for a differently cased file. Failed loads must not reserve the folded key
and prevent a later valid request. The converter's full inventory detects the
case-only duplicate-file problem; the runtime should report collisions it
encounters rather than promise to support their separate identities. Windows
and Linux compatibility is guaranteed for repaired content, not for an arbitrary
conflicting tree that has never been checked.

Existing ACE/DDS format aliases can remain; they select supported representations
of a texture. They do not justify adding filename-case alias maps. Retaining
lowercase logical keys also avoids changing material identity merely because a
reference's spelling was repaired, even though a fully repaired root already
has consistent reference strings.

Every index needs coordinated insert, reload, alias removal, rename, deletion,
and reset operations. Preserve handles while objects remain live; update or
invalidate renderer references through the existing lifetime rules. A practical
first version of the repair tool runs with the editor closed, so no live rename
protocol is required. Multiplayer asset identity changes need a separate
protocol review; sequential runtime handles should not become persistent names.

### A3. Runtime path contract

Keep three concepts separate:

| Value | Meaning |
| --- | --- |
| Reference | The exact spelling stored in a source file, including relative-path syntax or an implicit extension. Preserve it for display and save. |
| File path | The case-preserving path constructed from the source reference, its base directory, and existing format-defined override/fallback rules. Pass this to the filesystem. |
| Asset key | The normalized lowercase logical path, scoped by the resource context needed for sharing. Never pass this key to the filesystem or serialize it as the source reference. |

Proposed rules:

1. Normalize MSTS path separators at the format boundary. Do not lowercase the
   root, parent directories, or filename for file access. Replace ad hoc `"//"` rewriting
   with root-aware handling so UNC roots are not damaged. Resolve relative
   components against the reference's declared base; do not blindly collapse
   `..` across symlink boundaries.
2. Request fixed uppercase directories: `ROUTES`, `TRAINS`, `GLOBAL`, `SHAPES`,
   `TEXTURES`, seasons, and so on. New-directory creation uses uppercase too,
   including user-named content folders. The application can enumerate route and
   trainset folders normally; it does not create a case-translation map for them.
3. If a required directory is missing, check the parent for likely naming errors
   only to produce a useful diagnostic. Explain the expected path and offer to
   run the separate conversion tool. A name such as `route` may be a typo rather
   than just wrong case: show it as a suggestion, never silently equate it with
   `ROUTES`. Do not load from the suggested directory as a fallback.
4. Open referenced files with the supplied spelling and ordinary OS behavior.
   Keep existing semantic overrides and seasonal/ACE-DDS fallback, but remove
   runtime case-search fallback. No filesystem case-mode probing or persistent
   alias map is needed. The old flag must no longer control either destructive
   path folding or platform-dependent logical key equality. Full exact-case
   verification belongs to the converter, which enumerates components even on
   Windows where a mismatched `exists()` call may succeed.
5. Construct a missing/new file's logical key from its intended path, not an
   empty canonical-file result. Loading failure stays visible; a failed load
   cannot masquerade as a successfully shared asset. Symlink inventory and
   mutation boundaries are converter concerns, not a new runtime resolver.
6. Keep extension recognition case-insensitive, but construct implicit filenames
   with fixed conventional suffixes (`.eng`, `.wag`, `.sd`, and so on). The tool
   repairs `.ENG`/`.SD` and companion stems when needed to satisfy those exact
   generated paths. Explicit filename references retain their chosen spelling,
   including the extension, provided all uses agree. For example, derive an SD
   name by replacing the S suffix with `.sd`, rather than searching for `.SD`,
   `.Sd`, etc. This avoids a blanket extension conversion or a runtime suffix
   discovery system.
7. Keep keyword parsing (`ParserX::NextToken...().toLower()`), shader names,
   season labels, user search, sorting, and other intentional symbolic matching.
   Audit `Qt::CaseInsensitive` and case-folded map keys as well as `toLower()`;
   never use a global search-and-delete edit.

## B. Game-root repair tool

### B1. Interface and guarantees

Illustrative CLI through the main executable, not implemented. `--contentcase`
is the proposed mode switch; a separate executable is not required:

```text
TSRE5vc --contentcase <gameroot>
TSRE5vc --contentcase <gameroot> --plan
TSRE5vc --contentcase <gameroot> --plan <plan.json> --report <report.md>
TSRE5vc --contentcase <gameroot> --apply <plan.json>
TSRE5vc --contentcase <gameroot> --verify
TSRE5vc --contentcase <gameroot> --rollback <journal.json>
```

These are the **finished tool's** modes. Stage 1 implements only `--plan` and
its optional outputs/help. Invocations requesting repairs, apply, or rollback
must exit with an explicit "not implemented in the dry-run stage" message;
they must not fall through into normal editor startup or silently change meaning.
Reports/plans may be written outside the content tree; game-root contents must
remain untouched.

Use early command dispatch in [`main.cpp`](../../../src/main.cpp), following
the existing `--aceconv` console branch and `--refreshpmaptextures` entry point.
Those branches run before normal startup changes the working directory and
initializes editor settings/assets. The new command should likewise retain
caller-relative paths, use its own argument handling and `QCoreApplication`,
and return its status without starting the route editor, GUI, or OpenGL.
[`AceConverterCommand.cpp`](../../../src/aceConverter/AceConverterCommand.cpp)
provides a concrete example of console execution from the main executable.

The command owns the inventory, traversal, and format adapters. It may directly
instantiate a suitable W/T/shape file-level object or use shared decompression,
token, and document code. Sharing the executable does not imply creating a
`Route`, a route-editor session, asset caches, or global active-route state.
Evaluate file-level constructors and read methods too: a class being smaller
than `Route` does not guarantee freedom from lowercase I/O or load side effects.
Writing/saving through those objects is evaluated only when B resumes after A.

For stage 1, discovering actual spelling, resolving case candidates, and tracking
include ownership are normal scanner responsibilities. If a required file-level
read cannot collect correct information without part A's case-handling changes,
stop and report the exact class/function, input example, lost information, and
minimum prerequisite. Do not silently change a global case flag, load an editor
session, or pull A into stage 1 as a workaround. Lack of a saver alone does not
block a read-only scan; record that edit as unsupported/unverified for later B.

The root is required because consists, trainset assets, global shapes, includes,
and route overrides cross route boundaries. In the finished tool, normal invocation inventories,
plans internally, and performs repairs in one run. `--plan` selects a dry run:
it previews changes and conflicts without modifying content. Supplying a JSON
output path after `--plan` exports the plan; `--report` optionally chooses the
readable report's location. Neither output argument is required for a dry run.

`--apply <plan.json>` is an optional workflow for applying a previously exported
plan, with stale-input checks. Users do not need to run `--plan` first or prepare
a JSON file to perform normal repairs. Direct execution uses the same conflict,
coverage, backup, and verification rules as saved-plan execution; it does not
guess resolutions for blocked conflicts.

Keep reports, backups, and staging outside the scanned content tree, or explicitly
exclude their locations from discovery.

Each proposed operation records the original and destination paths, stable
scan-local file ID, source byte fingerprint, reason, affected incoming edges,
required reference edits, encoding/compression, and coverage/conflict status.
Each reference edge records source file and token/string span, exact spelling,
reference type, base directory, route/variant context, candidate targets, chosen
target ID, and whether it is explicit, implicit, optional, or a fallback.

Resolve the graph using these file IDs so planned directory renames do not make
the remaining plan refer to obsolete paths. Group references and candidate files
by the same folded logical key used by the application, within the appropriate
resource context. Retain separate physical-file IDs during planning so a group
containing two files is detected rather than silently collapsed. Exact matches
help establish the original associations; they do not make case-only competing
files a valid portable result. Never choose arbitrary enumeration order.

### B2. Directory and filename policy

Adopt uppercase names for **all content directories below the game root**, not
only the known structural ones. Structural examples are:
`ROUTES`, `GLOBAL`, `TRAINS`, `SHAPES`, `TEXTURES`, `TRAINSET`, `CONSISTS`,
`WORLD`, `TILES`, `LO_TILES`, `TERRTEX`, `TD`, `ACTIVITIES`, `SERVICES`,
`TRAFFIC`, `PATHS`, `SOUND`, `ENVFILES`, and `CABVIEW` where applicable.
This includes `OPENRAILS`, addon/procedural folders, seasons, route folders,
TRAINSET product folders, shared folders such as `COMMON.SOUND`, and arbitrary
user content subdirectories. It changes directory names, not route display names,
logical RouteIDs, TRK file stems, or all filenames. Leave the game-root argument
and its parents as supplied; do not traverse external links to uppercase their
targets. This is the selected project convention, not a claim that every existing
installation already uses these spellings.

Fixed system basenames such as `tsection.dat` use the spelling requested by the
application. The converter repairs their casing; the runtime needs no discovery
service for them. Implicit extensions/companion names likewise follow the simple
construction rules in A3. Explicit file references can retain a different suffix
case where no implicit-path constraint conflicts.

Every directory rename must update references that explicitly contain it. For
example, `..\\..\\common.sound\\Horn.sms` becomes
`..\\..\\COMMON.SOUND\\Horn.sms`. Unknown directories still receive an uppercase
proposal; missing reference coverage blocks applying that proposal, rather than
silently exempting the directory from the policy. User overrides are needed for
actual typos (`route` versus `ROUTES`); uppercasing alone cannot correct them.

For ordinary assets, choose among spellings already present on disk or in
references, considering the **entire path**, not just the leaf. Uppercase
directories and fixed/implicit filename conventions are hard constraints; minimize
edits among names satisfying those constraints:

1. Satisfy unpatchable references first within the fixed naming constraints. If
   an unpatchable reference contradicts a required uppercase directory or implicit
   filename, block that component until patch support is available. If all incoming references demand
   `Tree.s` and disk contains `TREE.s`, rename to `Tree.s`; no W rewrite is needed.
2. If several incoming spellings differ, choose the feasible spelling requiring
   the fewest source-file rewrites, with extra cost for binary/complex files.
   Count distinct affected source files as well as occurrences. Prefer retaining
   an existing name on a cost tie, then an already-authored readable spelling,
   then an explicit stable ordering. Record why the winner was chosen.
3. Patch only conflicting edges with verified support. Two unpatchable shapes
   referencing one texture as `Tree.ace` and `TREE.ace` cannot both be satisfied
   by a single rename. Report the blocked component. Do not create duplicate
   textures, hard links, or symlink aliases by default; those complicate editing
   and portability and can diverge later.
4. Merge case variants of **references** into one chosen spelling and one intended
   file wherever possible. If both `Tree.ace` and `tree.ace` exist, warn and compare
   them before selecting a resolution. Different contents are a blocking conflict:
   require selection of the intended version, or genuinely different names with
   corresponding references if both are needed. Case alone must not distinguish
   the final assets. Keep both originals intact until that decision is applied.
5. Plan companion groups and implicit references together: S/SD, ACE/DDS and
   seasonal variants, TRK-related stems, W/WS tile pairs, and terrain descriptor
   and sample companions. Preserve tile-coordinate naming and numeric track,
   activity, and material identifiers. Do not beautify generated tile names.
6. Keep valid unreferenced filenames as found unless a fixed/implicit naming rule
   or collision requires repair. Still scan recognized unreferenced
   files for outgoing references: an unused REF, consist, or shape can be used
   in the editor tomorrow.

Distinguish three cases in the report:

| Input | Repair outcome |
| --- | --- |
| One file, several reference spellings | Choose one filename, rename if useful, and rewrite only incompatible references. All uses share the same logical key and file. |
| Two case-only filenames, byte-identical contents | Report duplicate files and propose consolidation in the apply plan, keeping reversible backups. Verify outgoing references in context, companions, and inbound references first; byte equality alone is insufficient for reference-bearing files. Explicitly record both original file IDs mapping to the retained file. |
| Two case-only filenames, different contents | Warn and block automatic consolidation. Require an intended-version choice or separate names differing by more than case. Do not silently overwrite either file or declare the root repaired. |

Directories need the same treatment recursively. `SHAPES` and `shapes` can be
consolidated by the tool if their child plans are compatible; differing child
files or unsupported reference edits block the affected merge. A plain directory
rename must never overwrite the other tree. Consolidation is limited to names
competing for the same logical identity, not general content deduplication across
unrelated paths, routes, or seasonal directories.

Renaming is possible without parsing the target's body, even for a compressed
ACE or S file. However, proving the rename safe requires finding its incoming
references and companion constraints across the supported graph.

### B3. Is a more elegant naming policy worth it?

| Policy | Benefits | Costs / limits | Recommendation |
| --- | --- | --- | --- |
| Follow existing references | Usually few or no source rewrites; naturally restores names such as `LargeTree.s` already embedded in W/S files. | May preserve lowercase names or inconsistent aesthetics between unrelated assets. | Default. |
| Prefer existing readable mixed-case spelling | Uses author-provided word boundaries; improves browsing without inventing names. | Can require extra edits when most references use another spelling. Mixed case alone does not prove a name is better. | Offer as an optional tie-breaker or policy with an edit-cost preview. |
| Generate CamelCase / title case | Can make a uniformly converted root look more readable. | `largetree` does not encode reliable word boundaries; acronyms, railway stock codes, languages, punctuation, and intentional branding defeat simple heuristics. Rewrites expand across shared content and future package updates. | Defer. Provide user overrides/importable name mappings first. |

The useful extra effort is a deterministic conflict planner and good rename
preview. It solves correctness and recovers existing nice names. A linguistic
renamer has much lower value. Store the selected policy and overrides in the
plan so reruns and package updates make reproducible decisions.

### B4. Dependency graph and proposed scan order

The suggested global-first order is a good **discovery order**. It is not a safe
greedy rename order. Collect all inbound constraints before selecting names.
The hierarchy is a graph, with shared leaves, includes, route overrides, and
occasionally cycles.

| Sources | Targets / dependencies to extract | Scope and caveats |
| --- | --- | --- |
| `GLOBAL/tsection.dat`, its includes, route `OPENRAILS/tsection.dat` | `TrackShapes/FileName` to global shapes; include paths | `TSectionDAT::loadGlobal` prefers a per-route override. Inventory base and every override/include independently, even when shadowed in the current route. Route-local generated `tsection.dat` has a different role; do not use it to replace the global file. |
| Route TRK and directory inventory | Route file stem; environment files; implied related files | Distinguish `RouteID`, `Name`, and `FileName`. Discover TDB/RDB/TIT/RIT, REF, markers, terrain and world data according to each format's rules; a title is not a path. |
| W/WS, REF and addons, signal/speedpost/carspawner/forest/sound definitions | Shape names, direct texture names, SMS and other resource references | Object type determines the resource root. `WorldObj` uses global shapes for tracks and route shapes/textures for other categories. Numeric IDs are separate graph relations. |
| S and SD | Image/texture references; SD association and embedded shape name; seasonal variants | A global S may be loaded with each route's TEXTURES context (`ShapeLib::addShape(path)` supplies the current route texture root). Do not assume `GLOBAL/SHAPES -> GLOBAL/TEXTURES` is the only edge. |
| Terrain T and supported material catalogs/sidecars | Shader textures, sample buffer filenames, material sources, seasonal/baked resources | Include high/low-detail terrain and source assets needed for later editing, not only the currently selected season or baked image. Preserve material UiDs and spatial data. |
| TRK environment references and ENV | Sky/water/environment resources and includes | TSRE's `Environment` reader mainly extracts water; it is not a complete inventory of environment resource use. |
| ACT | Player service, traffic definition, embedded/static train configurations, paths and supported event resources | Activities can embed consists directly; scanning only SRV misses these trains. |
| TRF and ACT timetables | Service references | Preserve symbolic identifiers and resolve their file association in route context. |
| SRV | `Train_Config` to shared `TRAINS/CONSISTS`, `PathID` to route PATHS | [`Service.cpp`](../../../src/tsre/trains/Service.cpp) reads both. A reference can be a stem without an extension. |
| CON and embedded ACT consists | `EngineData` / `WagonData` filename stem and TRAINSET-relative folder | [`Consist.cpp`](../../../src/tsre/trains/Consist.cpp) adds `.eng`/`.wag` and `TRAINS/TRAINSET`. Both filename and directory spelling matter. |
| ENG/WAG and Open Rails overrides/includes | Wagon/freight shapes, sounds, cab resources, additional supported extensions | `Eng` loads only a subset and expands includes. Record the original include source/span instead of flattening ownership. Cab and simulation-only fields require additional extractors. |
| SMS and cab/resource definitions | WAV, cab textures, shared resource paths | [`MstsSoundDefinition`](../../../src/tsre/sound/MstsSoundDefinition.cpp) reads file references, but its runtime sound behavior is not a complete specification of every simulator search root. Common shared folders and relative paths need explicit adapters. |
| glTF/GLB, procedural templates and supported extras | External buffers/images/templates; includes | TSRE supports these alongside MSTS S. URI syntax needs its own adapter; embedded images are not disk renames. Unimplemented formats must appear in the coverage report. |

Suggested implementation traversal:

1. Inventory every directory/file, preserving exact spelling; detect collisions,
   symlinks, fixed layout roles, and file formats before any mutation.
2. Scan global tsection and includes, then all route metadata and overrides.
3. Scan route world/REF/definition files, shapes and companions, terrain, textures,
   environments, sound, and seasonal families in every relevant context.
4. Scan route ACT/TRF/SRV/PAT, then all shared CON and all TRAINSET ENG/WAG,
   overrides, includes, shapes, cabs, sounds, and shared resources. Include
   unused content in each recognized family.
5. Iterate newly discovered dependencies to closure, detecting include cycles.
   Resolve conflicts globally, including any fixed-directory spelling changes.
6. Build one coordinated plan, stage edits, apply renames/consolidations, and
   verify the final graph with exact component spelling. Target associations
   stay unchanged except for duplicate/version resolutions explicitly in the plan.

Filesystem application order is different: stage source edits while old paths
are valid, use temporary names to break rename cycles, and schedule nested
directory/file operations from a path map (for example, child operations before
parent directory renames). A one-pass `GLOBAL`, then `ROUTES` mutation cannot
guarantee shared references still work.

### B5. Existing save support: what TSRE cannot safely rewrite

“Can save” and “can preserve an arbitrary third-party file while changing one
reference” are different capabilities.

| Format / component | Current capability found in source | Suitability for repair |
| --- | --- | --- |
| S through `SFile` / `SFileLegacy` | Rendering loaders; no whole-shape save API in these classes. | Use them for semantic comparison if useful, not as repair serializers. |
| S through [`SFileDocument`](../../../src/tsre/shape/SFileDocument.cpp) / [`SFileComplex`](../../../src/tsre/shape/SFileComplex.cpp) | **A writer now exists:** text/binary and compressed/uncompressed shape output. Complete retention is required. Document encoding rejects damaged, partial, compact, or packed-only loads. Unknown binary payloads/tails can be retained; opaque binary data cannot be converted to text, and unknown text tokens without a binary schema cannot be converted to binary. | Strong candidate for controlled shape edits in the same format after preservation tests. Not a promise of byte-identical text: serialization regenerates formatting. Never save a recovered rendering subset as the original shape. |
| SD metadata | `SFileComplex::saveMetadata` exists with complete metadata; it writes text with optional compression. | Candidate for recognized metadata edits. Preserve original format where supported; do not assume this API is a general binary SD round-trip writer. |
| Global tsection and include files | `TSectionDAT` loads global definitions; `saveRoute` / `saveRouteToStream` emit route-generated sections and paths. | **No general global-tsection save operation.** Do not call `saveRoute` to rewrite global TrackShapes or flatten includes. |
| ENG/WAG and include files | `Eng` parses a subset and records included file paths; no save API was found in the class. | **No general rolling-stock writer.** Dedicated token-span patching is needed when renames cannot satisfy references. |
| PAT | `Path` has a reader; `ActLib::SaveAll` leaves `p->save()` commented out. | **No implemented path-save route here.** Do not require a PAT rewrite when an appropriate stem rename suffices. |
| ACT/SRV/TRF/CON | Save functions exist, generally writing modeled fields as text. Readers contain `SkipToken` paths for unsupported content. | Not lossless general rewriters. Use preservation-oriented edits or restrict saves to a proven supported subset. CON saving is not an ENG/WAG saver. |
| W/WS | `Tile::save` / `saveWS` and object writers exist. Readers can skip/recover unsupported records; text writers reconstruct modeled objects. Existing guards refuse overwriting incomplete binary loads. | Retain those guards. Rendering success or a complete-load flag is insufficient proof of a lossless reference-only rewrite. Unsupported objects/properties/control records must survive repair unchanged. |
| T terrain | Binary writer exists. Certain opaque sample buffers are retained, but unknown-token branches also exist. | Specific opaque preservation does not certify arbitrary round trips. Patch known filename fields while retaining unrelated blocks. |
| TRK, TDB/RDB, REF | Editor writers exist for modeled structures (`Trk::saveToStream`, TDB writers, `Ref::saveToStream`). | Require per-format preservation evidence. Do not regenerate a route database merely to change a filename's case. |
| ENV, SMS, SigCfg, SpeedPostDAT, SoundList | Reviewed components expose loaders, not corresponding complete definition-file savers. World-object `save` methods do not save these external catalogs. | **No general definition round-trip writer identified.** Broader reference discovery also exceeds some current loaders. |
| Cab and other simulator-only extensions | No general cab parser/writer was identified in the reviewed TSRE source inventory. | Add dedicated reference extractors/patchers or report unsupported coverage. Do not declare the whole TRAINSET repaired after just loading `Eng`. |
| glTF/GLB via `GltfShape` | Runtime loader, no whole-asset save API in the reviewed class. | Use a dedicated JSON/container-aware reference adapter if these assets require rewriting. |
| ACE and other leaf resources | ACE decoding/encoding exists elsewhere, but pure renames require no image re-encoding. | Keep bytes unchanged. Do not recompress images as part of a naming fix. |

### B6. TSRE parsers versus generic tools

Use TSRE's format knowledge and compression/token infrastructure, with a
preservation-oriented editing layer. Do not implement the fixer as “load the
route in the editor and Save All.” These loaders may select one override,
load one season, expand includes, recover damaged content, or discard fields
that the fixer must preserve.

[`ReadFile::read`](../../../src/tsre/fileFunctions/ReadFile.cpp) already handles
existing SIMIS compression wrappers, including byte and UTF-16 header variants.
[`FileBuffer`](../../../src/tsre/fileFunctions/FileBuffer.h) supplies checked
binary reads; it does not supply a general binary writer. `SFileDocument` has
its own schema and encoding implementation; it is not automatically a W/T/ENG
document writer. See the existing [FileBuffer guide](../../features/file-buffer.md).

For **text**, decode a recognized wrapper and encoding, locate typed reference
tokens, and retain source byte spans. Replace only the relevant strings,
preserving comments, unknown tokens, quoting, whitespace, line endings, BOM,
and original encoding where representable. Support quoted/unquoted strings and
the format's string composition rules. Patch included files at their original
locations and account for every including context.

For **binary**, locate reference fields using the actual schema, preserve all
unmodified bytes/opaque blocks, and rebuild string lengths and enclosing block
lengths if needed. A case-only ASCII substitution often preserves UTF-16 length,
which offers a narrow first implementation, but Unicode casing and explicit
renames need not preserve length. Never assume a byte search for a string or a
token number identifies a filename field.

Re-encode the original wrapper/format after editing. Recompression may change
the compressed bytes; verification should show that the decompressed payload
changed only at approved fields and necessary framing. Verify untouched files
and pure rename targets by byte fingerprint. A failed decompression or damaged
document blocks rewriting rather than triggering an editor-style recovery save.

`sed` or a generic text replacement can be useful for tightly controlled,
already-decoded fixtures. It is not an appropriate production engine:
compression is only one issue; binary SIMIS, UTF-16, comments, relative lookup
scope, and multiple meanings of a string remain after decompression. A generic
scanner can flag possible references in unsupported files, but cannot certify
them absent or authorize a rename based on guessed string matches.

### B7. Transaction, coverage, and failure handling

- Plan internally before mutation in every mode; a separate user-run dry run or
  exported plan is optional. Classify known leaf resources separately from potential
  reference-bearing files; unrelated executables and documentation do not need
  arbitrary text replacement. A dry run reports exact renames, reference edits,
  ambiguities, unresolved targets, unsupported source families, and the reason
  for each selected spelling. Unknown incoming-reference coverage blocks affected
  renames by default. Where the scope of an unknown format cannot be bounded,
  report the root as uncertified; do not advertise a complete repair.
- Recheck fingerprints and directory inventory before applying. Close editor
  sessions for the apply stage. Detect files added after scanning, destination
  collisions, insufficient space, unreadable files, and permission failures.
- Write staged replacements without overwriting originals. Keep exact backups
  and a durable journal. Multi-file rename is not one atomic transaction: support
  crash recovery/resume or rollback, including directory moves and partial edits.
- Use unique temporary sibling names for case-only renames and rename cycles,
  including on Windows. Consolidate `SHAPES` and `shapes` only through a checked
  child-by-child merge plan with collision handling and backups; a rename must
  never overwrite the other directory. Keep operations on the same filesystem
  when relying on rename.
- Resolve legitimate `..` references inside the game root. Treat escapes,
  absolute external references, symlinks, and junctions as explicit external
  dependencies; do not follow them into unbounded mutation or recursive cycles.
  Do not rename the game-root argument or its parents.
- Preserve file metadata where supported. Limit duplicate consolidation to the
  explicit case-conflict plan, preserve originals in backups, and do not
  substitute links as an incidental cleanup. Maintain a manifest for rollback
  and subsequent content-package updates that may reintroduce old spellings.
- Verify every supported edge still resolves to the **same target file ID**
  after remapping, or the explicitly recorded retained/selected target for a
  consolidation. Verify exact component spelling, one final file per logical
  key/context, uppercase content directories, and the same override/season
  behavior. Distinguish pre-existing missing assets from newly broken edges.
  Mandatory unresolved references prevent a “fully repaired” result; optional
  fallback variants are reported under their format rules.
- A plan with blocked components is not a complete repair. An optional partial
  apply must explicitly identify independent components proven unaffected by the
  blocked ones, and its report must retain the incomplete status.

## Delivery sequence and acceptance criteria

### Stage 1: B, scanner and planner only

Implement `TSRE5vc --contentcase <gameroot> --plan` through early main-executable
dispatch. Read the whole root through independent file-level adapters; do not
reuse route-editor loading. Produce the inventory, typed edges, case-conflict
groups, chosen target spellings, proposed operations, and coverage report.
Planning a future edit does not imply that its writer is implemented or safe.
Mark discovered references separately from supported/verified repair operations.

Keep mutation modes disabled. No rename, consolidation, content save, conversion,
or attempted round-trip write is part of this stage. Read existing W/T/S/etc.
documents directly where their file-level code is suitable, without activating
the editor's save lifecycle. After sufficient read-only evaluation, proceed to A
before adding repair execution.

Verification cases:

- Invocation through the main executable exits through the command handler,
  preserves caller-relative game-root/report paths, and needs no route-editor
  startup, GUI platform, active route, settings initialization, or GPU context.
- All game-root content bytes and directory entries remain unchanged. Test
  planning with a read-only game root and reports outside it; repair/apply/rollback
  requests explicitly fail as unavailable in this stage.
- Mixed directory casing, uppercase input extensions, compressed/uncompressed
  text and binary files, and case-mismatched references are inventoried with their
  original spellings retained. Parsing must not require already-converted content.
- W `Tree.s` -> disk `TREE.s` and S `Bark.ace` -> disk `bark.ace` produce
  traceable proposed renames, without applying them.
- Two routes sharing global shapes and consists; ACT -> TRF/SRV -> CON ->
  ENG/WAG; embedded ACT consists; include cycles and ownership; Open Rails
  overrides; all seasons and high/low terrain families are represented in the
  report as supported, missing, ambiguous, or not yet covered.
- One file with several reference spellings, identical/different case-only
  duplicate files, directory collisions, companions, and unused recognized
  content produce the expected repair proposals or unresolved conflicts.
- Inspect reports against representative real roots and fixtures with known
  expected references. Review counts and source locations, not just whether a
  scan finishes. Incomplete format coverage must remain visible; absence of an
  extracted edge does not prove absence of a reference.

**Stop condition:** if A is required to make this scan/planning correct, stop
stage 1 and report the dependency and minimum prerequisite. Do not make part A
changes under the scanner task. A missing writer is a later-stage limitation;
an inability to read the necessary paths/references correctly is a stage-1 issue.
This stop rule does not authorize implementation now; this document remains
design-only until implementation is requested.

### Stage 2: A, runtime identity and layout

Implement the simple path contract: fixed uppercase directories, exact file
paths, and lowercase logical keys where appropriate. Fix producers and consumers
together; retain useful terrain hash/comparison folding and separate synthetic
texture identities. Share directory constants and deterministic companion/save
path rules. Remove runtime case-search fallback; missing-directory diagnostics
offer conversion. Keep token/search case folding. An indexed asset map is optional.
Do this before promising that a repaired uppercase root is usable.

Verification cases:

- Mixed-case game root and parents, uppercase content directories (including
  route/trainset folders), and mixed-case filename stems work in the local editor,
  shape viewer, consist editor, and client; file access and saves preserve spelling.
- Lowercase key inputs group `Tree.ace`/`tree.ace` identically on both platforms,
  while the case-preserving I/O path remains separate. Repaired content has one
  physical file and one reference spelling in that context. Different directories
  and material parameters remain distinguishable.
- Wrongly cased required directories fail ordinary access on a sensitive
  filesystem and produce a conversion suggestion, without an alias fallback.
  On an insensitive filesystem, ordinary OS matching continues to work.
- Missing-file behavior is independent of cache warm-up order; a failed load
  does not poison a later valid request with the same folded key.
- All `TexLib` entry points agree; ACE/DDS coexistence and fallback do not depend
  on which texture was loaded first. Synthetic and embedded textures still work.
- The same global shape with different route texture roots does not share the
  wrong texture context. Season switching and Open Rails overrides remain correct.
- Save/new/import/recent file paths preserve case; new content directories follow
  the uppercase rule. Lowercase material hashes remain stable for spelling-only
  changes when their schema is unchanged. Path-dependent derived bake signatures
  are revalidated/invalidated as needed without changing authored UiDs.

### Stage 3: B resumes, repair execution

Use the scanner/planner evaluated in stage 1 and the path-handling changes from
A. Extend adapters where necessary, with coverage gating apply. Enable direct
repair and saved-plan execution, starting with filename-only repairs for
components whose inbound references and companion rules are understood. Add
reference patching as described below before attempting conflicting cases that
require it. Retest the dry-run guarantees after mutation support is introduced.

Verification cases:

- W `Tree.s` -> disk `TREE.s`; S `Bark.ace` -> disk `bark.ace`: pure renames,
  original content bytes unchanged.
- Normal invocation performs repairs without a prior dry run. `--plan` leaves
  content untouched, with or without an exported JSON plan. Direct execution and
  applying an unchanged exported plan produce the same repairs and conflict decisions.
- Two routes sharing global shapes and consists; ACT -> TRF/SRV -> CON ->
  ENG/WAG plus embedded ACT consists; include cycles and override contexts.
- Conflicting incoming spellings; case-only duplicate files and directories;
  one-file reference unification; identical-file consolidation with rollback;
  differing-content conflicts; safe and blocked directory merges;
  unreferenced-but-editable content; uppercase input extensions and deterministic
  repaired implicit filenames/SD companions;
  all seasons and high/low terrain families.
- Nested directory renames, case-only rename cycles, interrupted apply/rollback,
  stale plan rejection, external links, and no overwrite of existing content.
- Strict verification also runs on Windows by enumerating exact component
  spellings. A second plan after successful repair contains no new operations.

### Stage 3 continued: preservation edits and optional naming policy

Add source-span text edits and schema-aware binary patches, starting with
high-value reference families. Reuse complete shape documents only within their
proven preservation envelope. Expand ENG/WAG, definitions, cab and extension
coverage before making whole-root compatibility claims. Add the optional
readable-name policy once conflict handling is reliable.

Verification must include compressed/uncompressed text and binary SIMIS,
unknown blocks/tokens, comments, includes, non-ASCII names, and string-length
changes. Compare unchanged payload regions and modeled semantics; inspect the
operation report and reload repaired fixtures using strict resolution. Rendering
alone is insufficient, and the test harness must not manufacture lowercase
aliases to make the repair pass (existing shape fixtures sometimes do this).

No build or runtime tests are required for this design-only change. Implementation
must supply the above fixtures and preservation evidence before enabling apply.
