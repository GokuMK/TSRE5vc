# Large trainsim scan: saved-output review

Reviewed the user-generated `build/content-case-evaluation/trainsim-token.json`
and `trainsim-token.md`, completed on 2026-09-13. This review reads those saved
outputs and the scanner implementation; it does not rerun the game-root scan,
change installed content, or change scanner code. No independent before/after
installation verification was supplied for the user-run scan.

## Result and interpretation

| Measure | Count |
| --- | ---: |
| Inventoried files | 169,081 |
| Inspected documents | 115,689 |
| Reference edges, including repeated contexts | 1,581,250 |
| Error groups | 2,224 |
| Files with errors | 2,192 |
| Missing-content warning groups | 41,796 |
| Missing reference edges | 450,729 |
| Shared-field naming conflicts | 128 fields in 110 shapes |
| Physical case-only collision groups | 0 |
| Provisional operations | 135,716 |

The errors predominantly expose repeated scanner scope, field-classification,
and resolver limitations. They do not establish 2,192 damaged files. A single
broken asset remains an error and must not prevent conversion of unaffected
components. Missing content remains warnings. The existing stage-1 output is
not an executable or certified repair plan.

## Error groups

| Report code | Groups | Finding / proposed action |
| --- | ---: | --- |
| `context-unbound` | 898 | 896 T files under root-level `track_b`, plus `global/shapes/objects.ref` and `global/shapes/sigcfg.dat`. Extend the requested custom-directory and misplaced-catalog exclusions. |
| `unclassified-reference-field` | 583 | Primarily W speedpost textures and OR 3D cab fields; details below. Add typed rules and distinguish descriptions/identifiers from filenames. |
| `external-or-invalid` | 414 | Includes a confirmed fallback classification bug, empty fields, and fields interpreted in the wrong family; details below. |
| `fixed-reference-cannot-match` | 140 | Frozen source strings conflict with proposed names or uppercase directory spellings. Most examples involve shared train folders; revisit after reference recovery and avoid unnecessary reference canonicalization. |
| `shared-field-conflict` | 110 | 128 fields in 110 S files need coordinated texture names across route contexts. |
| `reference-discovery-incomplete` | 65 | Four binary SD files and assorted INC/ENG/WAG/CVF/SMS/REF/TRK discovery issues. |
| `source-read-failed` | 13 | Shape decoding errors: 6 NUL-text, 4 odd UTF-16 byte counts, 3 invalid/oversized compressed envelopes. Isolate affected assets; do not assume all are irreparable without inspection. |
| `incompatible-fixed-spellings` | 1 | `trains/trainset/common.snd/Flirt3/KISS_MAKRO_K.wav` receives both `KISS_MAKRO_K.wav` and `KISS_MAKRO_k.wav` from frozen sources. |
| **Total** | **2,224** | Counts are source/reason groups, not independent root causes. |

### Scope exclusions are still too narrow

The current custom-directory rule only excludes containers directly under
`ROUTES` without a direct TRK. Consequently `track_b/TILES` is still scanned,
producing 896 errors despite the user's instruction to ignore custom directories.
The two global catalog copies under `global/shapes` also evade the exact-path
exclusions for `global/objects.ref` and `global/sigcfg.dat`.

The saved graph additionally reveals spurious route contexts from
`routes/CMK/TEXTURES/CMK.trk` and `routes/bbb/textures/cmk.trk`. The scanner promotes
these misplaced descriptors to route roots. Catalog discovery by basename also
accepts `global/shapes/tsection.dat`. Scope must respect the standard locations
for descriptors and catalogs; an extra copied file inside a resource directory
must not redefine the resource base. These defects affect warning counts and
potentially proposals, not just the error list.

### Missing field adapters and false filename candidates

The 583 groups divide as follows (each row counts source groups):

| Field / context | Groups | Interpretation |
| --- | ---: | --- |
| W `speed_digit_tex` | 496 | Example `pusty.ace` in a Speedpost. The scanner handles a related catalog field but lacks the W adapter. Use the appropriate route texture rule. |
| `orts3dcabfile` | 68 | ENG/INC references such as `../../common.cab/PKOR_Flirt3/ED160_3D.s`; add the OR 3D cab shape/owner context. |
| `orts3dcabfile` plus `sound` in shared INC | 6 | These sources also need their include-owner family/context. |
| `graphic` in shared cab INC | 7 | Some includes are scanned without a discovered CVF owner. Preserve owner scope rather than matching arbitrary resources by basename. |
| REF `description` | 4 | Descriptions such as `DFSTower.s` are currently flagged by the filename-extension heuristic. A description is not an asset edge. |
| WAG leading `wagon` value ending in `.s` | 2 | Example `PKP_Sggrss-043-0a.s`; distinguish the record identifier from the actual WagonShape field. |

### External-path classification is unreliable for missing fallbacks

The 2,461 external/invalid edges split into the following 414 source groups:

| Saved path pattern | Edges | Source groups |
| --- | ---: | ---: |
| At least one in-root base and one escaping fallback | 2,066 | 360 |
| Empty reference | 269 | 35 |
| All listed bases escape the root | 109 | 2 |
| Absolute/invalid input spelling | 17 | 17 |

`resolve()` sets `escaped=true` if any attempted fallback escapes. If all valid
in-root candidates are missing, that flag changes the final result from missing
to external. For example, a relative sound reference can remain inside TRAINSET
from the vehicle base but escape from the final root `SOUND` fallback. An invalid
fallback must not turn missing content at valid scoped locations into an error.
This identifies 360 groups requiring reclassification/review; it does not certify
every currently implemented base or prove all of their targets are genuinely absent.

Keep actual external paths separate. The 35 empty-field groups need optional-field
semantics rather than an external-path label. Of the 17 absolute/invalid edges,
16 have kind `enginedata` in CVF sources; examples synthesize `//SP45.eng` from
cab data. This indicates the global EngineData adapter is being applied outside
its intended consist/activity family and needs context-specific classification.

### Reader recovery and fixed sources

The 65 incomplete-discovery groups consist of 32 INC, 19 SMS, 4 binary SD,
4 ENG, 3 WAG, 1 CVF, 1 REF, and 1 TRK. Diagnostics include:

- 22 sources with no document blocks found; inspect whether these are empty,
  inactive, alternate syntax, or unsupported documents before deciding impact.
- Unterminated quoted strings, anonymous blocks inside lights/sound streams,
  and blocks following stray closing delimiters.
- Four `global/shapes/DB/DB23_*.sd` files requiring binary SD handling.

The 140 frozen-source mismatch groups span 77 WAG, 35 ENG, 16 CVF, and 12 SMS
files. In 112 groups, the saved examples involve converting shared directory
spellings such as `common.snd` or `common.inc` to uppercase. That proposed rename
was based on an overly broad naming policy, corrected after this report:
dynamic shared directory names do not require uppercase. Preserve consistent
existing spelling, or rename to satisfy an uneditable reference. The corrected
planner also tests whether an original reference resolves against the planned
tree before proposing canonicalization solely to shorten a relative path.
These saved counts describe the old policy; no new installation scan has been
run to quantify the reduction. Genuine conflicting frozen spellings remain errors.

### Shared shapes still require coordinated naming

There are 128 conflicting fields across 110 shape sources, represented by 110
error groups. Examples propose both `CONCRETE.ace` and `concrete.ace`, or
`cr2l2wnaT.ACE` and `cr2l2wnaT.ace`, for one shared S image field across routes.
Coordinate the names of its route-specific targets, including existing seasonal
counterparts. These conflicts do not imply users must choose between asset versions.
The report finds no physical case-only collision groups.

## Warning quality also needs correction

Of 450,729 missing edges, 364,046 are textures. Of those, 356,728 originate in
global shapes evaluated across route contexts. Counts therefore include repeated
definitions and route combinations, not that many distinct missing files.
Another 3,600 missing texture edges originate under the custom `track_b` tree.

One concrete scanner bug affects all 75 `absent-water-patch-map` edges: their
retained source scalars are `["256", "Wsib-W.raw"]`, but the scanner records
**`256` as the missing filename**. The fixed first-scalar assumption is wrong.
Extract the resource scalar using the actual field layout, then apply the
absence test. The existing absence findings about the real `Wsib-W.raw` basename
must not be confused with these incorrectly constructed edges.

Vehicle override contexts also need review: the output contains
`OpenRailsCZSK/CABVIEW` bases, while current owner adjustment only recognizes
`OpenRails`. Missing resources must continue to be resolved solely by their
defined scopes; do not substitute same-named assets from other routes/vehicles.

## Proposed next work

The following list records the original review. The scope and adapter changes
described below are now implemented; their large-root counts await a user-run
retest. The saved `trainsim-token` report has not been regenerated.

1. Correct scope discovery/exclusions and the mixed-fallback external-path bug.
2. Add W speedpost/OR cab adapters; constrain field classification by family;
   fix water-map scalar selection and non-reference descriptions/identifiers.
3. Review include/override contexts, binary SD, and recoverable text layouts.
4. Coordinate shared-field and seasonal naming, then revisit frozen-source conflicts.
5. Retain genuine asset read errors and true external dependencies as isolated
   errors; keep missing content as warnings.

Use focused fixtures for these changes. Let the user run the next full trainsim
scan; do not automatically repeat it. The current report remains an unchanged
record of the submitted run, not a corrected set of counts.

## Implemented follow-up: the two largest groups

The user clarified that custom directories can contain shared shapes referenced
through `../...` paths, while the custom terrain tiles cannot be referenced.
The scanner now excludes by document type/location, not by the whole custom tree:

- TRKs only establish routes directly under `ROUTES/<route>`; `TEMPLATE` remains
  an explicit root-level route. A TRK copied into `TEXTURES` cannot change bases.
- T files are inspected only in recognized route `TILES`/`LO_TILES` locations.
  Route-bound catalogs outside a route, including copies in `global/shapes`,
  are excluded with scope warnings. Exclusion warnings show counts and examples.
- Shared S/SD/texture files remain available in arbitrary in-root directories,
  including custom containers under `ROUTES` and the root-level `track_b` tree.
  A shared S reached from two routes retains both route texture contexts.
  Unreferenced custom shapes outside TRAINSET are analyzed against included
  routes rather than assuming their physical folder contains their textures.
- Text and binary W/WS `speed_digit_tex` fields use route `TEXTURES`.
- `ORTS3DCabFile` values were already readable; the missing piece was the typed
  lookup/shape-texture adapter. They now resolve from the owning vehicle's
  `CABVIEW3D`, with images in the resolved shape's directory. See the
  [Open Rails documentation](https://github.com/openrails/openrails/blob/master/Source/Documentation/Manual/cabs.rst#3d-cabs).
  Shared includes retain vehicle ownership and scoped sound lookup. Includes
  with no discovered owner report a context error instead of inventing a base.
- REF `Description` and WAG/ENG `Wagon` record identifiers are excluded from the
  resource-extension heuristic. They are not file references.

The executable and `content_case` tests build and pass. New fixtures exercise
custom shared shapes used by two routes, unreferenced shapes inside custom
containers, ignored malformed custom tiles, misplaced TRKs/catalogs, text/binary
speedpost textures, and shared 3D cab includes with shape/image/sound references.
An orphan-include fixture checks that unresolved ownership stays visible.
No installation scan was run for this revision and no installed content was
written. The user will run the next trainsim test.

Other review findings remain separate work: mixed fallback classification,
water-map scalar selection, the broader include/override cases, binary SD,
text recovery, and shared-field naming coordination. These changes do not
claim that every one of the original 898/583 groups disappears.

Suggested next command, from the repository directory:

```powershell
.\build\TSRE5vc.exe --contentcase 'C:\trainsim' `
  --plan '.\build\content-case-evaluation\trainsim-scope2.json' `
  --report '.\build\content-case-evaluation\trainsim-scope2.md'
```

Both output files must be new. Exit 1 means the scan completed with errors.

## User-run scope2 results, reviewed 2026-09-14

Reviewed the saved `trainsim-scope2.json` and `trainsim-scope2.md`; no installation
scan or scanner code changes were performed during this review. The original
`trainsim-token` outputs remain unchanged.

| Measure | Previous | Scope2 |
| --- | ---: | ---: |
| Error groups | 2,224 | 852 |
| Files with errors | 2,192 | 821 |
| Inspected documents | 115,689 | 114,787 |
| Missing-content warning groups | 41,796 | 39,189 |
| Missing reference edges | 450,729 | 318,641 |
| Shared-field naming conflicts | 128 | 226 |
| Shapes with shared-field conflicts | 110 | 208 |
| Physical case-only collision groups | 0 | 0 |
| Provisional operations | 135,716 | 140,163 |

### Requested changes confirmed

- All **896 `track_b/TILES` files** are excluded. All **2,276 S files under
  `track_b` remain inspected**. Their graph contains 26,598 texture edges and
  15,392 existing seasonal texture candidates. Custom asset folders were not
  discarded with the terrain tiles.
- All **898 former context-unbound groups** disappear, including the misplaced
  global catalogs. Copied TRKs inside texture folders no longer define routes.
- All **496 W speedpost-field error groups** disappear. The saved graph resolves
  the speed-digit references against their route texture directories.
- Of **76 ORTS3DCabFile reference edges**, 65 resolve exactly, 1 has a casing
  mismatch, 4 are missing-target warnings, and 6 lack a discovered include owner.
  These are edge counts, distinct from the previous 74 source error groups.
  A working example is `PKOR_ED160/CABVIEW3D/../../common.cab/PKOR_Flirt3/ED160_3D.s`.
- Across all field adapters, **576 unclassified-field groups** disappear. The
  7 remaining groups are shared cab INC files with Graphic fields and no typed
  owner context. Six formerly unclassified 3D-cab INC sources now have explicit
  owner-context errors rather than guessed lookup bases.
- Two earlier incomplete-discovery groups disappear because their misplaced
  REF/TRK documents are excluded. The remaining read/discovery errors are unchanged.

The four missing 3D-cab edges require ordinary scoped missing-content review,
not a conversion stop. Examples include the `xhl` subdirectory copies of
`PL_REG_EN57AL-2112ra.eng`/`rb.eng` referencing `..//kabinaAL_3D.s`, and the
ST44 include used by `Orlen_M62-OR` referencing
`../../ev_DM62-Alias/CABVIEW3D/cab_or.s`. These examples do not establish whether
the expected vehicle/override context is correct for each custom layout.

### Remaining errors

| Category | Groups | Change / interpretation |
| --- | ---: | --- |
| External/invalid references | 414 | Unchanged; the prior review identified 360 groups with mixed in-root and escaping fallback bases. |
| Shared-field naming conflicts | 208 | 226 fields, up by 98 fields in 98 additional `track_b` shapes. |
| Frozen-reference naming constraints | 140 | Unchanged. |
| Incomplete reference discovery | 63 | Includes binary SD and the previously reported include/text syntax cases. |
| Shape read/decode errors | 13 | Unchanged; isolate affected assets. |
| Unclassified Graphic fields in cab includes | 7 | Cab383 shared INC files need owner-aware classification. |
| Ownerless 3D-cab includes | 6 | EP09, S200, ST44, Tp4, Tw1 and Ty2 includes; filenames were read, owner not discovered. |
| Incompatible fixed WAV spellings | 1 | The existing `KISS_MAKRO_K.wav` / `KISS_MAKRO_k.wav` conflict. |
| **Total** | **852** | Source/reason groups, not independent damaged assets. |

All previous 128 shared-field conflicts remain. The **98 added fields are all
in `track_b` shapes**, now evaluated with route texture contexts. Example:
`track_b/beton2/A1t1_5mStrtConcrete.s` receives proposals `Concrete6.ace` in three
contexts and `concrete6.ace` in two. Coordinate those target spellings while
retaining each route's actual texture asset and existing seasonal counterparts.
This does not require selecting one route's asset bytes for another route.

The next largest bounded fix is the external/missing fallback classification,
already diagnosed in the first review. Shared-field name coordination is the
larger planner task. The water-map scalar-selection bug remains separate,
unfixed work; scope2 still contains 75 such warning edges. None of the remaining
isolated errors should imply a whole-root conversion stop.

## Implemented: missing target versus rejected fallback, 2026-09-14

The resolver now records whether at least one candidate path was inside the
game root and did not cross a link. If those valid candidates are absent, a
later rejected fallback cannot change the result to `external-or-invalid`:
it remains `missing` (or `optional-missing` for an optional edge). Candidate
locations, precedence, and target selection are unchanged. No root-wide basename
substitution or traversal of rejected paths is introduced.

For example, a reference `../../B/SOUND/Missing.wav` in
`TRAINS/TRAINSET/A/SOUND/A.sms` searches `TRAINS/TRAINSET/B/SOUND/Missing.wav`.
If missing, the root `SOUND` fallback would escape the root, so it is rejected
without converting the missing warning into an error. A valid root `SOUND`
fallback for an ordinary filename still works. If every candidate escapes or
crosses a link, the reference remains an error. Absolute and empty inputs retain
their existing error classification; their field-specific semantics are separate
review work.

Focused fixtures cover the missing-relative case with an unrelated same-named
asset present elsewhere, an existing relative target, a valid root fallback,
all-escaping/absolute/empty references, and CLI exit behavior. The previous 360
mixed-base source groups are the intended target of this correction; the exact
new counts await a user-run scan. Neither installation was scanned for this fix.
The main executable and `content_case` tests built and passed after the change.

Suggested next output names:

```powershell
.\build\TSRE5vc.exe --contentcase 'C:\trainsim' `
  --plan '.\build\content-case-evaluation\trainsim-fallback.json' `
  --report '.\build\content-case-evaluation\trainsim-fallback.md'
```

## User-run fallback results, reviewed 2026-09-14

Compared the saved `trainsim-fallback.json`/`.md` with scope2. The exact expected
**360 external/invalid error groups disappear**, with no new error groups and
no other error categories changed. Total errors fall **852 to 492**.

The same 1,425,280 reference edges and 140,163 provisional operations remain.
Missing-reference edges increase by **2,066**, from 318,641 to 320,707, matching
the previously identified mixed in-root/escaping fallback edges. Missing-warning
source groups increase from 39,189 to 39,453 (+264 rather than +360 because some
of those sources already had other missing-content warnings). This is a
classification correction, not additional content becoming missing.

| Remaining error category | Groups |
| --- | ---: |
| Shared-field naming coordination | 208 |
| Frozen-reference naming constraints | 140 |
| Incomplete reference discovery | 63 |
| External/invalid input or lookup | 54 |
| Shape read/decode errors | 13 |
| Unclassified cab-include Graphic fields | 7 |
| Ownerless 3D-cab includes | 6 |
| Incompatible fixed WAV spellings | 1 |
| **Total** | **492** |

The remaining 54 external/invalid groups comprise 50 CVF and 4 SMS sources.
The earlier pattern analysis remains applicable: 35 groups contain empty
references, 16 contain CVF EngineData incorrectly passed through the consist
adapter, and 3 involve absolute or all-escaping paths. Examples of the latter
include `/k31_compressor.wav` in `common.snd/SD85/SD85cab.sms` and
`../../gp38/sound/...` in root `SOUND/SM42cab.sms`. Field semantics and legitimate
lookup contexts still need review; the fallback fix deliberately did not
reinterpret these cases.

There are still 226 conflicting fields in 208 shapes and no physical case-only
collision groups. Their naming coordination is the largest remaining planner
task. The submitted outputs were reviewed without rerunning a scan, changing
scanner code, or writing installed content. No independent installation
before/after verification is claimed for this user-run test.

## User-run naming results, reviewed 2026-09-14

Reviewed `trainsim-naming.json`/`.md`, produced by the separate
`TSRE5vc-contentcase.exe`, against `trainsim-fallback.json`. The naming-policy
correction removes **126 frozen-source error groups**. Total errors fall from
**492 to 369**, covering 358 source/target entries. The difference includes three
new directory-level conflict summaries; these describe incompatible existing
reference strings, not new damage to content.

| Error code | Previous groups | Current groups |
| --- | ---: | ---: |
| `shared-field-conflict` | 208 | 208 |
| `fixed-reference-cannot-match` | 140 | 14 |
| `incompatible-fixed-directory-spellings` | 0 | 3 |
| `reference-discovery-incomplete` | 63 | 63 |
| `external-or-invalid` | 54 | 54 |
| `source-read-failed` | 13 | 13 |
| `unclassified-reference-field` | 7 | 7 |
| `context-unbound` | 6 | 6 |
| `incompatible-fixed-spellings` | 1 | 1 |
| **Total** | **492** | **369** |

The remaining frozen directory demands are:

- `trains/trainset/common.cab`: `Common.Cab`, `Common.cab`, and `common.cab`.
- `trains/trainset/common.snd`: `Common.snd` and `common.snd`.
- `trains/trainset/common.snd/Wagony_Osobowe/Hamulce`: `Hamulce` and `hamulce`.

One physical directory cannot satisfy all these exact spellings while the
relevant source strings remain unedited. The planner currently freezes sources
with syntax recovery or incomplete discovery; this does not establish that their
formats can never be patched. Thirteen of the remaining fourteen source-level
naming error groups concern these three directory conflicts. The other concerns
the previously identified `KISS_MAKRO_K.wav` / `KISS_MAKRO_k.wav` conflict.
Directory/target summaries and source-level errors are related diagnostics, not
eighteen independent content problems.

The selective directory policy is visible in the actual proposals:

- Six route directory leaf renames, including `routes/PeakRail` to
  `ROUTES/PEAKRAIL` and `routes/bbb` to `ROUTES/BBB`. This run reports no new
  source-level route-naming error group.
- No directory rename proposals under `track_b`.
- Only two dynamic-directory renames: `Wagony_Osobowe/Jazda` to
  `Wagony_Osobowe/jazda`, and its `4ANc` child to `4anc`, to satisfy frozen
  authored references. This is reference-driven selection, not forced lowercase.

Proposed operations fall from **140,163 to 93,454**: 16,627 file renames,
182 directory renames, and 76,645 reference-edit proposals. These are still
provisional operations; no writer or apply mode is enabled.

Reference discovery is unchanged by count: **114,787 inspected documents** and
**1,425,280 reference edges**. Missing content remains **39,453 warning groups /
320,707 references**. Shared fields still require coordinating **226 fields in
208 shapes** across their texture contexts. Physical case-only collision groups
remain zero. The 63 incomplete reference scans, 54 external/invalid groups, and
cab include context/rule gaps remain as described in the preceding review.

The two saved scans are not byte-identical input snapshots: seven entries under
`routes/bbb` have changed metadata, and the inspected tile
`routes/bbb/tiles/-11db43a0.t` has a different content hash. No inventory paths
were added or removed. All other recorded inspected-source hashes match. This
limits any claim of identical inputs, although the graph counts and all error
categories unrelated to naming are unchanged. The comparison used saved reports
only; neither installation was rescanned or written during this review.

## User-run texture coordination results, reviewed 2026-09-14

Compared `trainsim-textures.json`/`.md` with `trainsim-naming.json`. **All 226
shared-field conflicts in 208 source shapes are resolved by the planner.** The
208 `shared-field-conflict` error groups disappear, with **no new error groups**
and no changes to any other error category. Total errors fall **369 to 161**,
covering 150 entries. This is a dry-run planning result; no repair was applied.

| Remaining error code | Groups |
| --- | ---: |
| `reference-discovery-incomplete` | 63 |
| `external-or-invalid` | 54 |
| `fixed-reference-cannot-match` | 14 |
| `source-read-failed` | 13 |
| `unclassified-reference-field` | 7 |
| `context-unbound` | 6 |
| `incompatible-fixed-directory-spellings` | 3 |
| `incompatible-fixed-spellings` | 1 |
| **Total** | **161** |

All **1,988 recorded texture naming groups** have a coordinated proposal; none
reports incompatible texture naming constraints. These groups include shared
fields, representations and seasons, not just formerly conflicting fields.
Full-path/frozen directory constraints are separate and remain among the errors
above. Physical case-only collision groups remain zero.

Concrete examples from the output:

- Group 1249 selects `concrete6.ace` jointly for six distinct files: the base and
  snow textures in CMK, plus the base textures in bbb, dk28, kielce, and
  test_group_z_1. All six already have that leaf spelling. The shared references
  now receive compatible proposals rather than route-dependent alternatives.
- Group 1250 selects `cr2l2wnaT.ace` for CMK's base/snow files and bbb's base
  file. It proposes one leaf rename for bbb's `cr2l2wnat.ace`. The three physical
  files remain distinct, including the different snow texture.

Provisional operations fall **93,454 to 68,103**. The new counts are 16,627 file
renames, 182 directory renames, and 51,294 source-field edits. File/directory
rename counts are unchanged, while edit proposals decrease by 25,351 as naming
is coordinated and repeated route/representation contexts share field edits.
An unchanged rename count does not imply that every selected name is unchanged.

The scan still inspects **114,787 documents** in the same inventory of 169,081
files and 1,558 directories. Reference edges increase **1,425,280 to 1,425,467**
(+187): exact references increase by 186 and case-mismatch references by one.
Missing content remains **39,453 warning groups / 320,707 references**; all other
reference-status counts are unchanged. Existing representation discovery did not
introduce missing-content errors or require absent DDS files.

Both the inventory SHA-256 and inspected-content SHA-256 match the naming run.
Unlike the preceding comparison, there is no difference in those recorded input
fingerprints. Unhashed leaf content is not independently verified by this claim.
The review streamed the saved JSON and recorded compact comparison data in the
ignored evaluation directory. It did not run either installation scan or modify
installed content. Remaining work is the previously identified parser/lookup
coverage and frozen-reference issues; no further shared-texture naming policy
decision is indicated by this test.
