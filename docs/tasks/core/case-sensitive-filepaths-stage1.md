# Case-sensitive filepaths: stage 1

Implemented on 2026-09-12 following the [agreed design](case-sensitive-filepaths.md).
This is B's read-only scanner and provisional planner. It is available through
the main executable and does not need part A. Existing editor loaders, runtime
path lowercasing, and save behavior have not been changed. No content mutation
mode is implemented.

**Accepted as the first stage-1 version on 2026-09-14.** The user agreed that the
scanner/planner is sufficient to finish this stage and proceed to stage A.
This closes the initial independent-discovery/planning milestone, not all format
coverage work and not readiness to mutate a game root.

For the latest user-run large-root results, see the
[trainsim saved-output review](case-sensitive-filepaths-trainsim-review.md).
It identifies remaining scope/resolver defects and a water-map scalar-selection
bug; earlier warning counts and apparent coverage must be interpreted accordingly.

## Accepted scope and handoff

The latest user-run `trainsim-textures` evaluation resolved all **226 shared-field
conflicts in 208 shapes**, reducing error groups from **369 to 161**, with no new
errors and matching recorded inventory/content fingerprints. It retained
114,787 inspected documents and coordinated 1,988 texture naming groups.
The MSTS installation was also evaluated during this stage; detailed historical
results and read-only verification remain below.

Delivered scope includes independent main-executable CLI dispatch, exact-case
inventory with contextual folded matching keys, bounded text/binary/compressed
SIMIS reference extraction, include ownership, scoped lookup, missing-content
warnings, exclusion rules, directory naming proposals, frozen-reference
constraints, shared/seasonal/ACE-DDS texture coordination, SD companion naming,
and provisional operation reports. Content writers and apply/rollback modes
remain unavailable. Plans retain `applyReady: false` and `coverageCertified: false`.

Known follow-up work is retained rather than treated as complete:

- The latest full-root report has 63 incomplete-discovery groups, 54
  external/invalid groups, 18 related frozen-reference naming groups, 13 shape
  read errors, and 13 cab-include rule/context groups. These are grouped
  diagnostics, not necessarily independent problems. Missing content is warning-only.
- The diagnostic-only follow-up labels escaping relative references in root
  SOUND locomotive SMS files `unsupported-sms-location`; it does not infer vehicle
  ownership or repair this explicitly unsupported layout. No full-root rerun was
  needed for that wording change, so the counts above name the saved report's categories.
- Empty optional filename semantics, CVF EngineData misclassification, the ENV
  water-map scalar selection, binary SD coverage, and remaining include/context
  adapters still need attention in B. Other unclassified formats and implicit
  companion families remain outside certified coverage.
- Stage A must implement case-preserving I/O with distinct logical keys, the
  uppercase structural/route directory and lowercase fixed-file conventions,
  and deterministic generated suffixes. In particular, replace literal S-name
  `+ "d"` construction with the agreed `.sd` companion rule. See the main design
  for producer/consumer, cache, hash, and save-path migration details.
- Stage 3 must implement verified reference edits, execution/rollback, and
  isolation of unresolved components and shared dependencies before mutation.
  An isolated damaged asset does not justify blocking every independent repair,
  but incomplete reference coverage must not be silently certified.

## Usage

```powershell
.\build\TSRE5vc.exe --contentcase 'C:\trainsim' --plan
.\build\TSRE5vc.exe --contentcase 'C:\trainsim' --plan '.\plan.json' --report '.\report.md'
.\build\TSRE5vc.exe --contentcase 'C:\MagiPacks\Microsoft Train Simulator' --plan --report '.\msts-report.md'
```

Put the game-root argument before `--plan` when exporting JSON. Both outputs
are optional; stdout always receives a JSON summary, and stderr shows progress.
Output files must be new, outside the game root, and have an existing parent
directory. Existing outputs are never overwritten. Report paths remain relative
to the caller's working directory. No output directories are created implicitly.

`--contentcase --help` prints usage. During this stage, invocation without
`--plan`, `--apply`, `--rollback`, and `--verify` fails explicitly. The eventual
direct-repair invocation remains the stage-3 design; it is not available now.
Other command modes cannot be combined with `--contentcase`.

Exit codes: **0** means no known read/sync failures were found in the supported
subset, **1** means the report contains failed cases, and **2** means a
usage/output error. Syntax and missing-content warnings alone do not cause exit 1. Neither
exit 0 nor a proposed rename certifies a safe repair. Every plan has
`applyReady: false` and `coverageCertified: false`.

## Implementation and report contract

[ContentCase](../../../src/contentCase/ContentCase.cpp) enumerates exact on-disk
spellings, uses folded logical keys only for matching, and records typed
references with source locations, original scalars, candidate targets, search
bases, and selected context. Existing files are opened read-only. Symbolic links,
junctions, and references escaping the supplied root are not followed.
Directories with known fixed application roles and recognized route directories
are proposed in uppercase. Vehicle folders and custom shared asset directories retain their
spelling unless an uneditable reference requires a different consistent name.
The root and its parents are excluded from renaming. See the naming-policy
correction below; earlier installation reports used blanket directory uppercase.

[ContentCaseDocument](../../../src/contentCase/ContentCaseDocument.cpp) is a
dedicated file-level reference reader using `SimisTextReader`, checked
`FileBuffer` reads, and bounded zlib decompression. It does not construct a route,
world object, renderer, or editor save model. Readers retain source case and
include ownership. Binary adapters cover reference fields in S, W/WS, and T;
text adapters cover shapes/SD, world/terrain, global and route catalogs, TRK,
ACT/TRF/SRV/CON, ENG/WAG/includes, SMS, CVF, ENV, and HAZ. These are subsets,
including references in unused recognized content, not complete simulator schemas.

Route descriptors are recognized directly under `ROUTES/<route>`; root-level
`TEMPLATE` is a route without requiring a TRK. Copied descriptors inside texture
directories or custom containers do not create new route contexts. Terrain tiles
outside recognized route `TILES`/`LO_TILES` directories are excluded, as are
route-bound documents and misplaced global catalog copies without a valid scope.
Custom directories themselves are not blanket-excluded: shared shapes, SD files,
textures, and other resources remain inventoried and available to relative paths.
Includes retain their owner's resource context and are cycle-checked.
All global-shape images are evaluated in every included route's
`TEXTURES`, including `TEMPLATE/TEXTURES`; `GLOBAL/TEXTURES` is not a fallback.
When no route context exists at all, those images remain explicitly unbound. Existing
seasonal textures and ACE/DDS representations are recorded separately, including
DDS alongside an existing ACE and ACE-only or DDS-only seasonal variants.
Reading is bounded to four workers with eight queued documents; graph construction
and report order stay deterministic.

Custom shared shapes reached through relative W/catalog references retain each
referencing route's texture context. Their physical directory is not automatically
their texture base. Unreferenced custom shapes outside TRAINSET are evaluated
against the included routes, like unreferenced global shapes; if there is no route
at all, their texture context stays explicitly unbound.

Lookup is scoped by reference type and owner, never by a root-wide basename
search. ENG/WAG shape and thumbnail references use the vehicle directory; its
shape images retain that vehicle texture base. This also holds when a vehicle
directory is nested under a route. Includes retain the owning vehicle base, and
OpenRails overrides use the parent vehicle directory. Cab graphics use their cab
directory. Sound references use their typed local bases and defined sound
fallbacks. Explicit relative paths are resolved within those scopes and bounded
by the game root. A texture in another vehicle or a route's `TEXTURES` directory
cannot satisfy a missing vehicle texture. Each edge records `searchBases` so the
implemented rules can be reviewed. The sound-region editor marker has its own
`GLOBAL/SHAPES` rule; this is not a general global fallback for arbitrary shapes.
If at least one scoped candidate is inside the root without crossing a link,
absence at valid candidates remains a missing warning even when a later fallback
escapes the root. The rejected fallback is not followed. All-rejected candidate
paths, absolute inputs, and empty inputs retain their error classification.
Signal catalog shapes and textures default to route `SHAPES` and `TEXTURES`.
StaticFlags-dependent global lookup is deferred by request. PassengerCabinFile
uses the ENG/WAG owner directory; FuelCoal uses the CVF directory.
W/WS `speed_digit_tex` uses route `TEXTURES` in both text and binary files.
`ORTS3DCabFile` uses the vehicle's `CABVIEW3D` base; the resolved shape's parent
directory supplies its images. This follows the
[Open Rails 3D cab layout](https://github.com/openrails/openrails/blob/master/Source/Documentation/Manual/cabs.rst#3d-cabs).
Shared includes keep their discovered ENG/WAG owner, including sound lookup.
An orphan 3D-cab include retains an explicit owner-context error rather than
resolving from the include directory. REF descriptions and WAG record identifiers
are not treated as filename fields solely because their values end in `.s`.
`GLOBAL/objects.ref` and `GLOBAL/sigcfg.dat` are excluded as requested, with
unsupported-catalog warnings. Excluded entries retain inventory metadata but
are not parsed or proposed for renaming; their reference coverage is outside
the plan. `scopeWarnings` records these exclusions separately from failures.

The ENV water-map field has no established lookup base yet. If its basename is
absent from the entire inventory, negative evidence is sufficient to report
missing content without blocking. This uses no root-wide substitution: if any
candidate basename exists, the field still fails until its real lookup rule is
implemented. Such missing edges have empty `searchBases`, kind
`absent-water-patch-map`, and a warning explicitly identifying the absence check.
The current MSTS inventory contains no `Wsib-W.raw`. This says nothing about
whether that legacy field is used by the simulator.

Filename candidates come from incoming reference spellings. References retained
from a source with syntax recovery or incomplete discovery are frozen: a source
edit is not proposed, and its target name must satisfy that spelling. Conflicting
fixed spellings are explicit sync failures. Otherwise, global tsection spellings
take precedence over editable SD/world references; remaining choices minimize
the local number of source files needing edits, with existing names winning ties.
This is a heuristic, not a global minimum-edit solver. Fixed system catalog
basenames use the agreed lowercase convention; SD companions follow
the chosen S stem with a lowercase `.sd` suffix. No linguistic CamelCase
generation is attempted. Shared texture fields, representations, and seasons now
receive joint naming proposals; unsatisfiable constraints and remaining full-path
disagreements are reported as errors. Physical
case-only competitors and proposed destination collisions are also reported.

JSON contains the full inventory, reference graph, proposals, diagnostics,
conflicts, include cycles, and fingerprints. Markdown starts with **every failed
case group**, followed by missing-content warnings, syntax warnings/reference recovery, and the summary.
Failures are grouped by source and reason, with at most five example details per
group; JSON includes every affected edge ID. Other diagnostic/operation sections
retain bounded samples. Text offsets are **decoded UTF-16
code-unit offsets**, and binary offsets address the decompressed envelope. These
are discovery locations, not executable byte patches. Multi-scalar references,
string composition, encoding, and enclosing binary lengths still require a writer.

The inventory fingerprint covers paths, file sizes/timestamps, and link/directory
identity flags; directory timestamps are excluded. Inspected source files also
receive SHA-256 hashes. Leaf resources are generally metadata-only unless hashed
for a physical conflict. Thus a stage-1 export is not sufficient for stage-3
stale-plan validation or leaf-byte verification.

## Initial real-root evaluation

The user supplied `C:\trainsim` and `C:\MagiPacks\Microsoft Train Simulator`
for read-only tests. All evaluation outputs are under the repository's ignored
`build/content-case-evaluation` directory. No reports are written inside either
game root. Both final scans completed through the main executable and returned
exit 1 because incomplete documents were found; each produced its JSON plan,
Markdown report, and console summary.

| Measurement | `C:\trainsim` | MSTS installation |
| --- | ---: | ---: |
| Files inventoried | 169,081 | 21,044 |
| Directories inventoried | 1,558 | 528 |
| Documents inspected | 115,685 | 8,175 |
| Documents with incomplete parsing | 386 | 21 |
| Reference edges, including repeated contexts | 1,252,601 | 362,392 |
| Case-mismatched edges | 332,037 | 341,153 |
| Missing candidate edges | 199,979 | 4,466 |
| Context-unbound edges | 40,338 | 181 |
| Unclassified files | 1,332 | 338 |
| Provisional operations | 102,038 | 6,329 |
| Physical/destination collision groups | 0 | 0 |
| Conflicting shared-field groups | 63 | 0 |

The 63 shared-field conflicts include global road shapes whose image references
would need both `cr2l2wnaX.ACE` and `cr2l2wnaX.ace` under independently selected
route texture names. Their target names must be coordinated before any repair;
the planner does not pretend that two edits to one field can both be applied.

Final local artifacts are `trainsim-verified.json` / `trainsim-verified.md` and
`msts-verified.json` / `msts-verified.md`, with corresponding `*-verified-summary.json`
console captures. Markdown is the practical starting point; the full graphs
produce large JSON files. These local artifacts are not checked into Git.

Independent PowerShell snapshots taken before testing and after the final scans
match for **all 170,639 and 21,572 entries**, respectively: no added/removed paths
and no changed sizes, modification ticks, or attributes. Repeated scanner
inventory fingerprints and SHA-256 manifests of inspected source bytes also
match. Verification is retained in `read-only-verification.json`, alongside the
before/after CSV snapshots. Access timestamps were not compared, and leaf-resource
bytes were not independently hashed in full; the scanner has no content-write API.

| Inspected-source manifest | SHA-256 |
| --- | --- |
| `C:\trainsim` | `934fc0603ab0b241ed41ae29fbdd4fdcf1d0a901f4f556a60ceb58b36412b2b1` |
| MSTS installation | `c2398901eaf795bc56024e81002addab2ce6fdab104d0251fc2df4cebbc1138e` |

## Findings and remaining work

### Shared-field conflict example

`global/shapes/cr2l2wnaStrt8m_T85d.s` contains the image reference
`cr2l2wnaT.ACE`. The same shape is evaluated with each route's own texture base:

| Route | Existing texture filename | Current planner's selected filename |
| --- | --- | --- |
| `routes/bbb` | `cr2l2wnat.ace` | `cr2l2wnaT.ACE` |
| `routes/CMK` | `cr2l2wnaT.ace` | `cr2l2wnaT.ace` |

The planner selects names per target using incoming-reference edit costs. Those
independent choices disagree about what to put in one shared S-file field. The
63 reported conflicts count source fields, not necessarily 63 different texture
names or irreconcilable content problems.

A coordinated solution could name both route-local textures `cr2l2wnaT.ACE`,
preserve this S-file reference, and patch other references that disagree. The
best common spelling must account for those other references too. The two route
textures remain separate files and can have different content; no byte-identity
requirement or asset consolidation follows from sharing their filename.

### Grouped incomplete-document diagnostics

The following groups are mutually exclusive, based on the recorded diagnostic
messages. They describe why the scanner stopped, not verified root causes or
proof that the simulator cannot read the files.

| Scanner failure category | trainsim | MSTS |
| --- | ---: | ---: |
| Unexpected top-level token | 183 | 11 |
| Unbalanced/malformed text while reading a block | 135 | 2 |
| Generic failure: no complete blocks reported | 49 | 2 |
| NUL in decoded text / unrecognized encoding or binary representation | 6 | 5 |
| Odd UTF-16 byte count | 4 | 0 |
| Binary SD schema not implemented | 4 | 0 |
| Compressed SIMIS envelope rejected | 3 | 0 |
| Binary block bounds/framing failure | 0 | 1 |
| Other incomplete parse with only an informational diagnostic | 2 | 0 |
| **Total** | **386** | **21** |

Most failures are in text structure handling. The generic "no complete blocks"
message needs improvement: `routes/PeakRail/TUTOR/PR1.trk` is a header-only file,
but other members, such as `CD_Ampz146_003.wag` and MSTS's `us2graincar.wag`,
contain substantial text. A failed skipped block can currently reach this generic
diagnostic without preserving the useful underlying lexer error. It must not be
interpreted as "empty file." The two other cases are `Lights_typ2.inc` and
`Lights_typ2G.inc`, where an earlier decorative separator diagnostic obscures the
reason parsing did not finish.

The binary SD failures are a known adapter gap. Envelope, encoding, and syntax
failures require individual inspection to distinguish damaged content from reader
limitations and simulator-tolerated input. The revised reader below preserves
specific lexer errors and recovers the inspected text patterns; binary SD support
and further format coverage remain separate work.

### Conversion impact: reference discovery and source rewriting are separate

User clarification: non-editable source files should normally impose their
existing reference spellings on target filenames. A whole-document syntax warning
or missing writer is not, by itself, a conversion blocker. The converter is not
required to repair unrelated syntax or save the source through an editor model.

The **386 / 21 figures above are historical syntax/reader diagnostic counts, not
counts of blocked conversions**. The revised executable separates supported
reference-scan completion from syntax diagnostics and freezes recovered source
spellings. See the MSTS-only retest below. Earlier exports are unchanged and should
not be interpreted using the new failure counts.

Report these dimensions separately, per source and affected reference group:

| Dimension | Required assessment |
| --- | --- |
| Syntax diagnostic | What went wrong, where, and which region was skipped or left unread. Preserve the specific lexer error. |
| Reference discovery | Whether the required filename fields and their contexts were fully recovered, partly recovered, or could not be read. State the evidence and affected fields/resources. Full semantic loading is unnecessary. |
| Reference editing | Whether the specific field can be patched safely, or must remain unchanged. Whole-file save support is not required for discovery. |
| Naming constraints | An uneditable reference fixes the spelling of its target path in its lookup context. Choose target renames and edits to other editable references around that constraint. |
| Conversion impact | Warning only; rename targets with source unchanged; or unresolved affected component, with a concrete reason. Do not infer this solely from syntax validity. |

When a known filename-bearing region has been fully read and later damage cannot
hide relevant references, retain the extracted data and classify the syntax issue
as a warning for that conversion. Continue or recover through unrelated regions
where the format permits it. Merely finding some references before a failure does
not establish completeness if an unread region may contain others.

Only a concrete problem prevents the affected filename synchronization: required
references remain unknown, or no legal target naming can satisfy the fixed
references and directory rules. For example, two uneditable references to the same
logical target with incompatible case spellings cannot both be satisfied by one
filename on a sensitive filesystem. Report that specific conflict, rather than
blocking all content because one source cannot be saved.

#### Inspected examples from the supplied roots

- **MSTS `ROUTES/EUROPE1/EnvFiles/UKsnow.env`:** the reported unexpected token
  is the closing `)` at line 422, followed only by whitespace. The scanner already
  extracted ten distinct ACE texture names, including `UKsnowsky.ace`,
  `starsky.ace`, and `WaterTop.ace`. This trailing token does not prevent using
  those references to choose texture filenames while leaving the ENV bytes intact.
  The separate unclassified `world_water_terrain_patch_map ( Wsib-W.raw )` field
  needs its own resource-impact assessment; it is not a reason to discard the
  recovered ACE names.
- **MSTS `TRAINS/TRAINSET/310/310.eng`:** line 349 starts a bare parenthesized,
  comment-like block, with no preceding keyword:

  ```text
  (#_fire temp, fire mass, water mass, boiler pressure,
  _water level, tender_water_mass, tender_coal_mass,
  _smoke_quantity, fire_condition, coal quality )
  ```

  The initial scanner's block grammar rejected this opening `(` and reported
  `Unbalanced/malformed text at UTF-16 offset 8743`. It already extracted
  `OE310Engine.s`, `310Engine.ace`, and `GenSteamEng.sms`. The remaining file
  contains the comment-like text, numeric `EngineVariables`, and closing syntax.
  The revised reader skips this recognized annotation and continues scanning later
  fields. This is a concrete reader-tolerance issue; the example does not justify blocking
  filename synchronization or requiring the ENG file to be rewritten.
  `PENDENNIS/pendennis.eng` has the same pattern at line 331, starting `(_#fire`.
- **trainsim `routes/CMK/OPENRAILS/carspawn.dat`:** a second `CarSpawnerList`
  starts before the first one is closed. The scanner reaches EOF with an outer
  block still open and initially reported offset 674. All sixteen `CarSpawnerItem` shape
  filename fields are nevertheless extracted and resolve exactly in the report.
  The syntax defect alone does not prevent synchronizing these names. Leave the
  catalog unchanged; any question about list grouping/activation is a separate
  simulator-semantic issue.

### Failure-first reporting implementation

The reader now separates `referenceScanComplete` (the supported reference
regions were visited) from syntax validity. It recovers through trailing `)`
tokens, the recognized `(#_...)` / `(_#...)` annotations, and EOF after complete
child fields when only wrapper delimiters are missing. A complete S image table
also remains usable if later rendering geometry is truncated. An unfinished
filename field, unterminated quoted string that may hide later references,
unknown binary record, or other unbounded unread reference region remains a
filename-sync failure. Specific lexer errors are retained.

Comment skipping now follows lexer token boundaries. MSTS's `us2graincar.wag`
contains `comment( 22.805t empty, 106.747t full" )`: that embedded quote is part of
an unquoted atom, not the start of a quoted string. The earlier fast skipping
path treated it as a string opener and swallowed later fields. The scanner-local
fix recovers the later filenames without changing the shared lexer or source file.

`failures` lists file-read/decode failures, reference-discovery failures,
unclassified filename fields, unresolved lookup contexts, physical-name
conflicts, and unsatisfied fixed-reference constraints. Syntax warnings have a
separate list stating whether reference scanning finished and whether a distinct
sync failure exists for that source. Case mismatches with a consistent rename
proposal are not failures. Missing targets under known lookup rules are warnings,
grouped by source in `missingTargets`, with searched locations and all affected
edge IDs. Preserve those unresolved references; do not substitute same-named files
from unrelated directories. Optional absent variants remain non-failures.

`referenceEditPolicy: rename-targets-only` prevents proposing edits to recovered
or incompletely read sources. Syntactically complete, understood source regions
can still have future patch proposals; this does not assert that a writer exists
in stage 1. Unknown-format coverage remains explicitly uncertified.

### Earlier MSTS-only retest of failure-first reporting

The updated executable was tested only on
`C:\MagiPacks\Microsoft Train Simulator`, as requested. `C:\trainsim` was not
rescanned during this revision. Historical local artifacts are
`build/content-case-evaluation/msts-impact-final.json`,
`msts-impact-final.md`, and `msts-impact-final-summary.json`.

The scan inspected 8,175 documents and produced 363,485 reference edges and
6,336 provisional operations. Before missing content was reclassified as warnings,
its first report section listed **1,013 failed case
groups across 990 files**, grouped by source and reason:

| Failure reason | Groups |
| --- | ---: |
| File read/decode failure | 5 |
| Target not found in the scanned context | 751 |
| Filename-like field lacking a typed lookup rule | 123 |
| Required reference discovery incomplete | 57 |
| Lookup context not established | 65 |
| Target receives incompatible fixed spellings | 2 |
| Fixed source reference cannot match selected target/directory naming | 10 |
| **Total** | **1,013** |

These counts include unresolved cases previously present only in graph statuses
or coverage diagnostics; they are not an increase from 21 to 1,013 damaged files.
The 5 read/decode failures are the five copies of `jp1signal2.s` whose decoded
text contains NULs. The 57 discovery-failure groups comprise 49 W files with
unclassified binary records, 6 T files with unclassified extension records,
1 truncated binary S file, and `GLOBAL/objects.ref` with unresolved record
context. Unsupported binary payloads are no longer merely an incidental warning
when they could hide filename information.

Seven previously failed sources now have **warning-only** assessments: the
truncated `JP1SigGant4.s` with its complete image table, three SMS files with
trailing closing delimiters, `310.eng`, `pendennis.eng`, and `us2bnsfcar.wag`.
The comment-token fix also makes `us2graincar.wag` complete with no known failure.
The six snow ENV files retain their extracted texture references despite their
trailing closing delimiters. They still appear under the separate unclassified
RAW-field issue (`world_water_terrain_patch_map`), not as failed texture reads.

Exit 1 now follows the actual failed-case list. A focused fixture containing a
recovered ENV and its resolved texture exits 0 and proposes only the target
rename. Recovered sources are never proposed for reference rewriting. The legacy
JSON `incompleteDocuments` metric is retained as an aggregate of non-clean reads;
it does not determine exit status or the report's failed-case list.

The main executable and `content_case` tests built and passed after the final
reader change. Added fixtures cover trailing syntax recovery, reading references
after legacy annotations, missing wrapper versus filename delimiters, geometry
damage after a complete image table, meaningful skipped-block lexer errors,
embedded quotes in comment atoms, fixed-spelling conflicts, tsection precedence,
and failure-first report/exit behavior.

Fresh before/after snapshots match for all **21,572 entries**: no added/removed
paths and no changed sizes, modification ticks, or attributes. The inventory
fingerprint and all inspected-source hashes also match the earlier scan.
Evidence is saved in `msts-impact-read-only-verification.json`, with
`msts-impact-before.csv` and `msts-impact-final-after.csv`. No installed content
was written. Stage 1 remains read-only, and part A remains outside this change.

#### Follow-up: examples and false positives in the 751 missing-target groups

Inspection of the saved inventory shows that **388 of the 751 groups** are
`.ws` sources referring exclusively to `IMRegionPoint.s` (3,881 edges). The
scanner looked in each route's `SHAPES` directory, whereas the inventory contains
`GLOBAL/SHAPES/imregionpoint.s`. This is a lookup-rule issue; these groups must
not be presented as proof of absent assets. The lookup rule is now corrected;
the latest retest below resolves all 3,881 references to that global marker.
The remaining 363 groups are warnings about targets absent from the implemented
lookup scope; legacy-field and fallback semantics still need review.

Other concrete examples from the saved report (the root-wide inventory checks
were diagnostic only, never resolver fallbacks):

| Source | Requested target | Inventory finding |
| --- | --- | --- |
| `TRAINS/TRAINSET/380/SOUND/380cab.sms` | `a380_power_cruise8.wav` | No file with this basename, case-insensitively, anywhere in the inventory. Two references form one group. |
| `TRAINS/TRAINSET/380/oesleepcar2.wag` | `380sleepcar.ace` | No file with this basename anywhere in the inventory. |
| `ROUTES/USA1/SERVICES/washington - philadelphia.srv` | `Washington - Philadelphia.pat` | No file with this basename anywhere in the inventory. |
| `ROUTES/USA2/Shapes/US2WhisPost.s` | `US2WhistlePost.ace` | Missing from the selected USA2 texture base; copies exist in other routes. Their existence is not permission to substitute a different route's asset. |
| `TRAINS/TRAINSET/DASH9/CABVIEW/dash9.cvf` | `AcWndFrn.ace` | Missing from the selected DASH9 cab base; copies exist under ACELA and HHP. Cab lookup/legacy-field semantics need review. |

Thus "unresolved target" currently means that the scanner failed to associate
the reference with a target under its implemented rules. It can indicate absent
content, a wrong reference, an inactive legacy field, or an incorrect/incomplete
lookup rule. A casing-only conversion cannot manufacture genuinely absent data.

### MSTS-only retest: scoped lookup and missing-content warnings

This revision was tested only on `C:\MagiPacks\Microsoft Train Simulator`.
The latest artifacts are `build/content-case-evaluation/msts-scoped-final.json`,
`msts-scoped-final.md`, and `msts-scoped-final-summary.json`. They supersede the
earlier failure classification above. The scan still inspects 8,175 documents
and produces 363,485 reference edges, with 6,337 provisional operations.

The report now has **262 failed case groups across 241 files**, followed by
**363 missing-content warning groups containing 585 references**. Missing content
alone does not cause exit 1. This installation still exits 1 because the following
read/sync failures remain:

| Failure reason | Groups |
| --- | ---: |
| Filename-like field lacking a typed lookup rule | 123 |
| Lookup context not established | 65 |
| Required reference discovery incomplete | 57 |
| Fixed source reference cannot match selected target/directory naming | 10 |
| File read/decode failure | 5 |
| Target receives incompatible fixed spellings | 2 |
| **Total** | **262** |

All 3,881 `SoundRegion/FileName` references in 388 WS files now resolve to
`GLOBAL/SHAPES/imregionpoint.s`, using a dedicated editor-marker lookup rule.
They no longer appear among missing targets. The remaining absent targets are
warnings scoped to their expected locations. For example, `US2WhisPost.s` still
warns about `US2WhistlePost.ace` under `ROUTES/USA2/TEXTURES`; a copy in another
route does not resolve that edge. DASH9 cab graphics likewise search the DASH9
cab directory, not ACELA's or HHP's cab directories.

The main executable and `content_case` tests built and passed. New fixtures
place same-named resources in another vehicle and in route directories, verify
that the vehicle's missing references remain missing, check vehicle ownership
even beneath a route directory, verify the global marker rule, and confirm that
a missing-content-only CLI run exits 0. The actual scan uses the main executable
with GUI platform initialization disabled.

Fresh snapshots match for all 21,572 installation entries: no added or removed
paths, and no changes to sizes, modification ticks, or attributes. The inventory
fingerprint and inspected-source content hashes also match the original scan.
Evidence is saved in `msts-scoped-read-only-verification.json`, alongside
`msts-scoped-before.csv` and `msts-scoped-after.csv`. Leaf resources are covered
by inventory metadata, not individual content hashes. No game content was written;
`C:\trainsim` was not rescanned in this revision.

### Latest MSTS-only retest: additional lookup rules and scope exclusions

The latest outputs are `build/content-case-evaluation/msts-rules-final.json`,
`msts-rules-final.md`, and `msts-rules-final-summary.json`. They supersede the
262-group assessment above. This revision implements the user-specified route
signal defaults, vehicle-local PassengerCabinFile, CVF-local FuelCoal,
route-context global textures, and root-level TEMPLATE handling. StaticFlags
lookup switching remains deferred by request.

Both FuelCoal resources exist at their expected locations:
`TRAINS/TRAINSET/380/CABVIEW/coal.ace` and
`TRAINS/TRAINSET/SCOTSMAN/CABVIEW/coal.ace`. The 380 reference uses `Coal.ace`,
so that edge is a casing mismatch. `Wsib-W.raw` does not exist anywhere in the
inventoried MSTS root. Its 64 included ENV references are now missing-content
warnings backed by inventory-wide absence, without inventing a lookup base.
The other 12 ENV sources from the previous 76 are inside the excluded backup.

The report explicitly excludes `ROUTES/MyRoute` and
`ROUTES/_TSRE_test_route_backups` because neither has a direct TRK descriptor,
plus the two unsupported global catalogs. These exclusions cover 688 inventory
entries. Inventory metadata is retained, but no excluded source is parsed or
given its own rename proposal. Included routes still resolve their own assets;
the excluded backup routes do not supply global-shape texture contexts.

The scan inspects **7,988 documents**, collects **360,952 reference edges**, and
proposes **6,189 provisional operations**. There are **425 missing-content warning
groups / 1,046 references**, including the absent water maps and additional
global-shape/route texture combinations. These are not counts of broken routes.
There are no remaining unclassified-field or unbound-context failure groups in
this evaluated scope.

**70 failure groups across 60 files remain:**

| Failure reason | Groups |
| --- | ---: |
| W reference discovery: unclassified binary token 262153 | 49 |
| T reference discovery: TSRE material extensions | 3 |
| S reference discovery: truncated binary shape | 1 |
| Source decoding: five jp1signal2.s copies | 5 |
| Two target shapes require incompatible fixed spellings | 2 |
| Ten W sources cannot match the selected target spelling | 10 |
| **Total** | **70** |

The last two rows describe the same two crossing-sign naming conflicts from
target and source perspectives. Improving W reference coverage may remove the
current restriction against proposing edits to those sources.

The main executable and `content_case` tests built and passed. Added fixtures
cover TEMPLATE without a descriptor, each added field, multiple signal-group
shape entries, global images using route textures instead of GLOBAL/TEXTURES,
custom-container exclusions, ignored malformed global catalogs, and an absent
water map becoming an unclassified-rule failure when an unrelated same-named
file is introduced. No source mutation mode is enabled.

The before/after snapshots match for all 21,572 entries, and all 7,988 inspected
source hashes match their prior values. The aggregate content fingerprint is
not compared because the inspected subset changed. Verification is saved in
`msts-rules-read-only-verification.json`, with `msts-rules-before.csv` and
`msts-rules-after.csv`. No installed content was written; `C:\trainsim` was not
rescanned.

### Follow-up, 2026-09-13: isolated failures and remaining work

The user confirmed `ROUTES/ularge/shapes/jp1signal2.s` is broken. Keep its read
failure; do not attempt to repair its content as part of filename conversion.
A failed independent file must not prevent conversion of unaffected components.
The executor must isolate affected operations and dependencies, retain the
failure in its report, and continue elsewhere. Unknown references can still
constrain shared targets, so independence must be established for those targets.
This clarifies the future execution policy; stage 1 still has no apply mode.

Token `262153`, responsible for 49 W discovery groups in the saved report, is
being handled by another agent. Do not duplicate that implementation here.
The remaining scanner work is the three procedural T files with
`tsreterrainbakedmaterials` / `tsreterrainmaterialmap` extension coverage.
`ROUTES/USA2/Textures/Tunnel1_noturrets.s` also requires inspection of the reported
truncation to determine whether required filename information is recoverable.
The two crossing-sign spelling conflicts are represented by 12 groups; recheck
them after the W token update before treating them as independent planner work.
The saved 70-group report has not been regenerated for these clarifications.

### Terrain metadata correction and error terminology, 2026-09-13

The terrain extension errors were gaps in the independent scanner, not missing
support in the application terrain parser. The scanner had conservatively
flagged TSRE extension records instead of classifying their payloads.
Review of `TFile.cpp` and `TFileBakeMetadata.cpp` establishes:

- `TSRETerrainMaterialMap` contains numeric slot/UID pairs, not filenames.
- Version 2 `TSRETerrainBakedMaterials` contains variant labels, revision,
  resolution, and settings/source/validation hashes. These are metadata, not
  texture references. The legacy standalone baked-material string is likewise
  metadata. Unknown versions or malformed bounds remain errors.
- Texture references remain in terrain texture slots, resolved under route
  `TERRTEX`. Existing seasonal counterparts must use the same filename spelling
  as the base texture; the planner now explicitly coordinates their names.
- Bake source hashes include resolved paths and file metadata. A later rename
  can invalidate cached bake validation; do not treat those hashes as filenames
  or rewrite their contents as path strings. Cache regeneration belongs to the
  later execution/runtime validation stage.

Reports now call the diagnostic section **Errors**. An isolated broken/lost
shape, including `ROUTES/USA2/Textures/Tunnel1_noturrets.s`, stays an error but
must not stop conversion of unaffected components. Legacy JSON keys such as
`failures` and `failedCases` remain for compatibility; they count diagnostics,
not a root-wide stop decision. Exit 1 means the scan completed with errors.
No apply mode or failure-isolation executor is implemented in this stage.

The main executable and scanner tests pass, including metadata bounds and
seasonal terrain filename alignment. The MSTS-only retest is saved in
`build/content-case-evaluation/msts-terrain.json`, `msts-terrain.md`, and
`msts-terrain-summary.json`. All three terrain errors disappear: **67 error
groups across 57 files** remain, including the unchanged 49 W token groups.
The scanner inspects 7,988 documents and proposes 6,699 operations. Before/after
metadata for all 21,572 entries and all inspected source hashes are unchanged;
see `msts-terrain-read-only-verification.json` and its before/after CSV snapshots.
No game content was written, and `C:\trainsim` was not rescanned.

### Pulled native Telepole token and scanner integration, 2026-09-13

`main` was fast-forwarded to `77a4a38`, preserving all local modifications.
The pulled change names token `262153` as `Telepole` and adds application
persistence. Its [format notes](../../features/msts-world-telepole.md) establish
that the world record holds numeric placement/configuration values, while
the route's `telepole.dat` supplies `FileName` and `Shadow` shape references.

The independent scanner now recognizes the Telepole world container and scans
`telepole.dat` as a route catalog. Both shapes use route `SHAPES`, with images
using route `TEXTURES`. It scans every included catalog configuration rather
than filtering by the currently selected world configuration index. A fixture
covers native binary Telepole, both catalog shape fields, and the pole texture.
The runtime token implementation was not duplicated or modified here.

The rebuilt main executable and updated `content_case` tests pass. The MSTS-only
scan is saved in `build/content-case-evaluation/msts-token.json`, `msts-token.md`,
and `msts-token-summary.json`. It reports **6 errors in 6 files**, down from 67:
the five `jp1signal2.s` decoding errors and the truncated
`ROUTES/USA2/Textures/Tunnel1_noturrets.s`. All 49 Telepole discovery errors and
all 12 crossing-sign fixed-spelling conflict groups disappear. The latter can
now be represented by ordinary provisional reference edits because the W source
reference scans complete. There are no remaining naming-conflict groups in this
evaluated subset; this does not certify all format coverage or enable apply.

The scan inspects 8,004 documents, records 360,984 reference edges, and proposes
6,722 operations. Sixteen Telepole catalogs supply 32 shape references: 8 exact,
22 casing mismatches, and 2 missing targets in `ROUTES/TUTORIAL ROUTE/SHAPES`
(`telepole.s` and `teleshad.s`). Missing content remains warnings: 426 source
groups / 1,048 references overall.

Verification retains all 21,572 inventory entries with no additions/removals.
All previously inspected source hashes match; the 16 newly inspected catalogs
also match separate post-scan hashes. The metadata comparison is **not wholly
unchanged**: `dxwrapper-train.log` changed from 14,298 bytes to 0, with a new
modification timestamp during the build/scan interval. No other metadata changed.
The scanner and commands used here made no writes to the installation; the cause
of that log change was not established. Evidence is saved in
`msts-token-read-only-verification.json`, `msts-token-before.csv`, and
`msts-token-after.csv`. `C:\trainsim` was not scanned.

### Other findings

- Native counted forests/carspawn/track-type catalogs can have a bare count and
  a trailing `)` without an outer opening block. The reader handles that layout
  within the catalog family. It still diagnoses malformed contents.
- The smaller root gives concrete reference-driven naming examples:
  `GLOBAL/SHAPES/a1t1000r10drndtun.s` is proposed as
  `GLOBAL/SHAPES/A1t1000r10dRndTun.s`, with its SD companion following the same
  stem.
  ?? That is why I sugested reading tsection.dat first for global shapes, like if you open it even in lowercase trainsim, it still contains:  TrackShape ( 226 \n FileName ( A1t1000r10dRndTun.s )

  In other cases tsection and SD spellings disagree; a reference edit is
  proposed when one rename cannot preserve every spelling. This supports keeping
  existing mixed-case names without inventing word boundaries.
  ?? example? In case of conflict, it is easier to fix sd than tsection.dat

- The supplied content also contains damaged wrappers, odd UTF-16 lengths,
  unbalanced text, empty or non-SIMIS files bearing SIMIS extensions, and unknown
  binary tokens. Successful editor recovery would not make these safe to rewrite.
- Earlier scans used descriptors inside backup containers as route roots. The
  latest scope rule instead excludes custom containers directly under `ROUTES`
  that have no TRK of their own, as requested. An incomplete tutorial `.trk`
  still must not create a fictitious route context.
- A global tsection can list definitions absent from a particular installation.
  A shared shape can be considered in several route contexts. Therefore reference
  counts include repeated contexts, and missing candidates are **not counts of
  broken playable routes**. Seasonal masks, effective overrides, and simulator
  fallback precedence need further semantic auditing before execution.
- Unknown text fields with resource-like values are diagnosed, but this heuristic
  cannot discover every extension or implicit reference. glTF/GLB, material
  catalogs, arbitrary extension files, TDB/RDB and quadtree-related families remain
  unclassified or outside the reference subset. Some underscore-prefixed catalog
  entries and extra ENV/rolling-stock/cab fields still need typed adapters.
- The planner does not yet solve all companion families, arbitrary full-path
  shared-context constraints, directory merges, or duplicate consolidation choices.
  Shared texture filenames, ACE/DDS representations, and seasons are coordinated
  as described below. Repeated contexts requesting the same source-field edit
  produce one proposal with multiple edge IDs. All proposals still require
  coverage review, including otherwise simple leaf renames.
- Both evaluations use Windows. Exact spelling is checked against inventory,
  rather than relying on Windows lookup success. A sensitive-filesystem run with
  identical and different case-only physical duplicates, directory collisions,
  Unicode casing, and link fixtures remains necessary before repair execution.

These are scanner/format coverage and future execution issues, not a dependency
on A. Stage 1 establishes that independent readers can collect useful information
from existing mixed-case roots. It does not establish complete incoming-reference
coverage. A can proceed as the next agreed stage; B must retain these coverage
gates when work resumes. Nothing here authorizes or enables applying the proposals.

## Directory naming correction (2026-09-14)

The user's uppercase rule applies to fixed application paths, not every folder
in the game root. The planner now records structural roles by location:
top-level game directories, recognized route subdirectories, vehicle resource
subdirectories, and known seasonal texture locations. The user additionally chose
uppercase recognized route directory names, such as `ROUTES/CMK`; route display
names, logical RouteIDs, and TRK stems remain unchanged.
TRAINSET product folders, `track_b`, `common.snd`, and other dynamic directories
keep their disk spelling by default. A custom `textures` directory is not
classified as structural merely by its basename.

Explicit components in uneditable references can select a different dynamic
directory spelling. Contradictory frozen directory demands are reported as
`incompatible-fixed-directory-spellings`. Fixed application directory names
remain constraints. Reference proposals use the final selectively named tree,
and a valid authored relative path is retained even if a shorter spelling exists.
System-catalog lowercase proposals likewise apply only in their fixed locations.
The agreed lowercase convention for direct-access files is documented in
[B2](case-sensitive-filepaths.md#b2-directory-and-filename-policy).

Focused temporary-root tests cover preserved product/custom names, uppercase
route directories and structural children, cross-route reference edits and
uneditable cross-route conflicts, frozen shared-folder references, reference-driven dynamic
directory renames, real contradictory spellings, valid longer relative paths,
and catalog names outside fixed locations. The existing seasonal-name and
determinism tests remain in the suite. Neither supplied installation was rescanned
for this correction: earlier report counts, especially frozen-reference naming
constraints, must not be treated as results of the updated planner.

The follow-up route-directory preference is implemented and `content_case`
passes, including editable and uneditable cross-route references. The current
`build/TSRE5vc.exe` is running and Windows denied replacing it. The freshly
compiled main-executable objects were therefore linked to
`build/TSRE5vc-contentcase.exe`, leaving the running editor alone. Use this
executable for the next scan; the ordinary executable has not yet been refreshed
with the route-directory follow-up.

From the repository directory, the next user-run read-only test is:

```powershell
.\build\TSRE5vc-contentcase.exe --contentcase 'C:\trainsim' `
  --plan '.\build\content-case-evaluation\trainsim-naming.json' `
  --report '.\build\content-case-evaluation\trainsim-naming.md'
```

These output names were unused when prepared. Existing outputs are never
overwritten; choose another pair of names for subsequent reruns.

## Shared texture coordination and generated extensions (2026-09-14)

Stage B now joins physical texture targets reached by the same source field
across route contexts. Overlapping fields connect transitively: a shape shared
by routes A/B and another shared by B/C coordinate all three targets. Existing
seasonal counterparts and ACE/DDS representations participate in these groups.
Physical files, contents, and route-specific identities remain separate; this
does not consolidate texture versions.

The planner considers existing/authored stems and suffix spellings. It first
satisfies frozen source leaf spellings and generated-extension rules, then
minimizes distinct source files requiring a leaf edit within the group, then
minimizes physical leaf renames, with deterministic lexical tie-breaking. This
is a component-level heuristic, not a global minimum-write solver: directory
spelling and full-path feasibility are checked separately. It preserves authored
relative path structure where that can resolve in the planned tree.

Incompatible frozen demands are reported as
`incompatible-texture-naming-constraints`; affected texture renames and source
edits remain blocked proposals. Independent components can still have proposals.
Unresolved full-path differences remain `shared-field-conflict`. The JSON
`textureNamingGroups` array records member file IDs, the chosen logical name,
cost counts, and the decision. The Markdown report shows the first 100 groups.
Repeated contexts requesting the same physical field edit share one operation;
`edgeIds` retains all those contexts and `edgeId` retains the first for compatibility.

Generated extension policy remains deterministic lowercase; explicit source
suffixes need not be lowercased:

| Authored/requested name | Generated companion/representation |
| --- | --- |
| `Tree.S` or `Tree.s` | `Tree.sd` |
| `Leaf.ACE` or `Leaf.ace` | `Leaf.dds` |

SD targets already follow the selected shape stem and `.sd`. Current
`SFile::loadSd`/legacy code still uses a literal `+ "d"` (thus `.S` would produce
`.Sd`); replacing that construction belongs to stage A, which is not changed here.
ACE-to-DDS generation already uses lowercase `dds` in the inspected runtime
paths. Stage B now inventories existing DDS files even when ACE exists, preserves
the authored ACE suffix in edit proposals, and jointly names both formats in
every existing season. It also finds an ACE-only season when the base texture is
DDS-only. No absent representation or season is created or required.

An explicitly referenced standalone `.DDS` can retain that suffix. If it also
serves an implicit ACE-to-DDS request, the generated `.dds` rule applies; an
uneditable `.DDS` reference then remains a real naming constraint error.

Focused tests cover ordinary shared-field coordination, transitive route groups,
distinct texture identities, frozen-reference priority and incompatible demands,
independent components, one edit per physical source field, existing seasons,
ACE/DDS coexistence, DDS-only bases with ACE-only seasons, authored `.ACE`
preservation, and `.S`/`.SD` input with a proposed `.sd` companion. The existing
temporary-root determinism and read-only checks also pass. Neither supplied
installation was scanned for this implementation; the previous 226 shared-field
conflicts must be measured again using a user-run scan.

The full `TSRE5vc` target linked successfully for this update; the earlier
running-executable lock was no longer present. `TSRE5vc-contentcase.exe` was
also refreshed from the same compiled objects for continuity with the previous
test command. The `content_case` CTest suite passed, and the separate executable's
CLI help was checked. From the repository directory, use new output files:

```powershell
.\build\TSRE5vc-contentcase.exe --contentcase 'C:\trainsim' `
  --plan '.\build\content-case-evaluation\trainsim-textures.json' `
  --report '.\build\content-case-evaluation\trainsim-textures.md'
```

## Unsupported root SOUND SMS layouts

Root-level `SOUND` SMS files with relative audio references that escape the game
root now report `unsupported-sms-location`. The message identifies the source,
reference, root SOUND lookup base, and resulting path outside the gameroot. The
user explicitly excludes this layout from repair support: do not infer a vehicle
context, relocate the SMS, reinterpret extra `../` components, or substitute
similarly named trainset audio. This changes the explanation, not resolution.
`SOUND/SM42cab.sms` and `SOUND/SM42eng.sms` in the supplied report are examples.
A focused fixture verifies that the error remains even when a matching WAV
exists in a plausible GP38 vehicle directory. Earlier scan counts are historical;
the installations were not rescanned for this diagnostic change.

## Automated checks

The main executable and standalone scanner tests built successfully with the
local Qt 6.10.1/MinGW toolchain. **All three selected CTest suites passed**:
`content_case`, `shape_document`, and `simis_tokens`. Main-executable help and
mixed `--aceconv --contentcase` rejection were also exercised without GUI startup.

The `content_case` CTest target uses temporary roots and known binary/text
documents. It checks case-preserving fields, compressed and uncompressed input,
UTF-16, malformed binary bounds/counts, string composition/comments, lexer-string
ownership, native counted catalogs, nested route contexts, activity-to-train
dependencies, includes/cycles, shared-route conflicts, relative reference edits,
fixed catalog naming, repeat-scan determinism, disabled mutation modes, and output
protection. Fixtures compare inspected source hashes after CLI execution.

Build `TSRE5vc` and `tsre_content_case_tests`, then run:

```text
ctest --test-dir build -R "^(content_case|shape_document|simis_tokens)$" --output-on-failure
```

`TSRE_BUILD_CONTENT_CASE_TESTS` controls the standalone scanner test target.
No new tests require installed game content. Real-root tests additionally invoke
the main executable with an invalid `QT_QPA_PLATFORM`, confirming that this mode
uses no GUI platform or editor startup.
