# Conedit path-identity performance regression review

The original review below was read-only. Subsequent user decision and fix:
use case-insensitive runtime cache identity consistently, store `hashid`, and
retain linear lookups. No physical candidate validation on cache hits and no new
hash indexes. The implementation supersedes the physical-validation and index
recommendations recorded below; they are retained as review history.

Original review: no production code changed. The user's existing Windows EngLib.cpp
slow/fast annotations were preserved. The standalone benchmark and this report
are in the WSL checkout; native benchmark build artifacts are in the ignored
Windows build/englookup-review directory.

## Finding

This is an implementation regression in the case-preserving asset identity work.
The linear search existed before it. The regression replaces a cheap QString
comparison with two full logical-path-key constructions inside every iteration.
Qt progress-dialog visibility is expected behavior and is not needed to reproduce
the slowdown.

`EngLib::addEng()` (Windows annotated source lines 53?55) evaluates
`ContentPath::key(pathid) == ContentPath::key(entry->pathid)` for every loaded
candidate. `ContentPath::key()` (ContentPath.h lines 33?35) calls normalize(),
constructs an absolute path relative to QDir::current(), calls cleanPath(), and
lowercases it. normalize() builds an output string character by character.
This repeats allocations/path processing for both an unchanged request and
unchanged cached entries. These are logical-key operations; do not confuse them
with canonical filesystem identity checks.

For 3,597 successfully loaded distinct entries, insertion into the linear cache
requires 6,467,406 candidate comparisons and 12,934,812 calls to key(). The old
code prepared its path outside the loop and compared stored strings (including
an explicit length check in the historical version).

## Native Windows measurement

Qt 6.10.1, MinGW GCC 13.1.0, CMake Release build. The standalone Qt Core benchmark
enumerated the real C:/trainsim/TRAINS/TRAINSET ENG/WAG path names: 3,597 files.
No content was parsed or modified, and no GUI or event processing was created.
All five strategies recorded zero duplicate hits.

| Strategy | Time | Candidate comparisons |
| --- | ---: | ---: |
| Plain QString equality, linear scan | 27.54 ms | 6,467,406 |
| Current code: construct both keys inside loop | 57,527.50 ms | 6,467,406 |
| Construct request key once, cached entry key still recomputed | 30,389.20 ms | 6,467,406 |
| Compute/store both keys once, retain linear scan | 44.42 ms | 6,467,406 |
| Compute keys once, use hash lookup | 18.69 ms | 0 linear comparisons |

The current comparison loop was about 2,089 times slower than plain comparison,
and about 1,295 times slower than the cached-key linear variant in this run.
This is a lookup-only microbenchmark, not an end-to-end editor startup timing.
Treating every entry as loaded gives the all-loaded comparison count; the real
loader skips failed entries. Cached/hash timings include key preparation and
cache insertion. Parsing, source-override probes, and real duplicate-hit checks
are omitted from every strategy.

Reproduction project: tests/performance/contentPathLookup. Native run output:
tests/performance/contentPathLookup/windows-result.txt. The benchmark uses the
production ContentPath.h, with the Windows checkout supplied as TSRE_SOURCE.
The timing strategies are not drop-in replacement cache implementations.

## Why the progress dialog appears only with the slow code

EngLib::loadAll() constructs QProgressDialog without changing minimumDuration.
Qt defaults to 4,000 ms and suppresses the dialog when the operation is expected
to finish quickly. The slow path crossing that threshold explains the visibility
change. See [Qt QProgressDialog documentation](https://doc.qt.io/qt-6/qprogressdialog.html#minimumDuration-prop).

The existing loop also calls processEvents(AllEvents, 50) after every file;
modal setValue() can itself process events. That can add GUI/reentrancy overhead,
but the timeout is an event-processing limit, not an unconditional 50-ms sleep.
This GUI loop existed before the regression. The no-GUI benchmark proves the
lookup regression independently; it does not quantify any additional editor GUI
cost. See [Qt QCoreApplication documentation](https://doc.qt.io/qt-6/qcoreapplication.html#processEvents).

## Historical recommendations (superseded by the implemented follow-up)

1. Compute the incoming logical key once per lookup and store the key with each
   cached entry (or in an ID-indexed side map). Do not regenerate either key for
   every candidate. Hoisting only the request key leaves roughly half the problem.
2. Keep physical path spelling separate from that key. After a matching key is
   found, retain the selected-source/readability/physical-identity validation.
   A missing case spelling or changed OPENRAILS override must not silently reuse
   a different cached file. Simply reverting to unconditional string equality
   would discard those checks.
3. A key-to-candidate-ID hash can remove linear traversal later, but is not needed
   to fix the measured regression. Handle multiple physical candidates sharing a
   folded key; do not let one overwrite another. Define invalidation for removal,
   reload, and path changes.
4. Apply the same review to EngLib::getEngByPathid(), ConLib::addCon(), and ActLib
   lookup loops: canReuse() computes both keys before its early rejection. Loaded
   consists also call addEng() for their vehicles, repeating the expensive search.
   ShapeLib already computes the request key outside its loop and keeps stored
   keys, illustrating the intended separation.

For first-time unique engine loading, canReuse() is not reached because the
preceding logical-key equality rejects each candidate. Filesystem identity
checks are therefore not the source of the measured 57.5-second miss-loop cost.
The OPENRAILS readability probe is outside that loop and is another per-call
cost, not the quadratic repeated-key cost.

## Historical follow-up: stored hashid and lookup-loop audit

User preference: keep a precomputed string identity on the asset, analogous to
Texture::hashid, so candidate comparison is ordinary string equality. This does
not require introducing a numeric digest. Preserve the actual path separately.
The earlier benchmark showed that caching keys fixes the immediate regression;
for the stated scale of hundreds of thousands of assets, also use a string-key
index to avoid whole-library scans. At 100,000 unique entries, linear incremental
insertion entails 4,999,950,000 candidate comparisons even with cheap keys.
Existing maps keyed by numeric asset ID do not provide lookup by content identity.

Reviewed every production call site of ContentPath::key/canReuse/sameLocation in
src, plus direct path/hash string comparisons and the asset-library entry points.
This is a source audit of identity lookup, not a profile of all rendering or
parser loops. No production code was changed.

| Location (WSL source) | Current work | Recommendation |
| --- | --- | --- |
| EngLib.cpp addEng, line 52 | Rebuilds both keys on every candidate | Stored hashid and key index; validate selected base/OPENRAILS source only for matching candidates |
| EngLib.cpp getEngByPathid, line 85 | canReuse rebuilds both keys on every candidate | Same engine identity index |
| ConLib.cpp addCon, line 39 | Same repeated-key miss cost | Stored hashid/index; retain unsaved consist handling |
| ActLib.cpp GetAct/AddAct/AddService/AddTraffic/AddPath, lines 58/78/112/134/230 | Same repeated-key miss cost across five lookup loops | Stored per-asset key and per-library indexes; retain unsaved/modified object handling |
| ActLib.cpp sameRoute, line 25; query loops lines 92?214 | Rebuilds parent/route paths per candidate, can perform filesystem equivalence; several queries lowercase both names repeatedly | Store route identity and logical name; compute requested route/name once and index queries where appropriate |
| MstsSoundDefinition.cpp AddDefinition, line 480 | Same repeated-key miss cost | Stored SMS identity and key index |
| ShapeLib.cpp addShape, lines 69?76 | Request keys computed once, stored path/context keys compared; filesystem checks only after key/context matches | No EngLib-style miss regression; replace full scan with index over shape identity, texture lookup context, and season |
| TexLib.cpp getTex/addTex, lines 165/179/321 | Stored hashid aliases and request key; full collection scan, then canReuse on matching aliases | Preserve alias strings; index aliases to candidate IDs; maintain index on reload/retirement |
| OrtsTrackProfileRenderer.cpp profileTextureId, lines 299?325 | Already hashed by route/texture; fallback/readability/identity probes still run on repeated hits | No whole-cache scan; separately evaluate caching source validation within the content/reload lifecycle |

SoundLib itself has no implemented asset lookup. PaintTexLib, ImageLib, AceLib,
DdsLib, and MapLib perform resource creation/decoding rather than the duplicate
identity scan under review. EngLib::getEngByPointer is a pointer comparison scan,
not the path-key regression. Traversals that intentionally enumerate/update all
assets should not be replaced indiscriminately.

### Historical identity rationale (superseded)

- Slash cleanup gives equivalent authored separators a common logical spelling.
- A known absolute base prevents relative requests from different contexts from
  sharing one key accidentally. Resolve this once at the API boundary; repeated
  QDir::current() calls are not a requirement of case-sensitive loading.
- Dot-component cleanup and case folding retain the chosen logical sharing rule.
  A folded key groups candidates; it does not prove they are the same physical
  file, nor authorize rewriting the path used for opening content.
- For example, Tree.s and TREE.s can coexist as different files on Linux. If only
  Tree.s exists, a request for TREE.s must not succeed merely because Tree.s was
  cached first. Physical checks after a folded-key match prevent that.
- ENG base content and an OPENRAILS override may have one logical vehicle name
  but different selected sources. Likewise, a shared shape with different route
  texture directories or seasons must not reuse an incompatible rendering context.
- Synthetic texture IDs are not filesystem names and retain their exact identity.

These requirements do not justify regenerating keys inside a scan, checking the
filesystem for unrelated candidates, or revalidating unchanged selection on every
hit without a defined cache/reload policy. Exact spelling/source hits can use a
fast path within a cache lifecycle; promises about detecting external removal or
new overrides on every request are a separate freshness policy. That policy must
be explicit rather than silently removed during optimization.

Suggested lookup structure: prepare requested hashid once; fetch its candidate
IDs from QHash; compare stored strings/context; use physical validation when the
candidate requires disambiguation or source refresh. Preserve numeric IDs for UI
and existing references. Keep candidate buckets for case-fold collisions instead
of overwriting an existing physical variant. Update stored keys/index entries on
path changes, removal, reload, and texture alias retirement.

## Implemented follow-up

Stored lowercase string keys now drive EngLib, ConLib, ActLib, and SMS lookups.
ActLib route filtering uses stored route keys and allocation-free case-insensitive
name comparisons. ShapeLib retains its stored path/context keys and removes
physical-file checks. TexLib uses stored aliases and distinct ACE/DDS source
representations, with fallback probes only on misses. The ORTS profile cache no
longer revalidates physical paths on a hit. Physical cache validators were removed.

Tests now require case-variant requests to reuse cached assets, including distinct
case-only files. Source changes are picked up on explicit reload. Tests retain
route/season separation, ACE/DDS separation, generated aliases, alias retirement,
failed-load recovery, and unsaved-object behavior; add rename/copy maintenance
and an actual EngLib lookup workload of 3,597 in-memory engine entries.

No hash indexes were introduced. Initial file loading still opens the preserved
physical path; the converter supplies consistent names/references for Linux.
The runtime cache does not perform a directory search to fix an uncached path.

### Follow-up validation

The rebuilt `/root/TSRE5vc/build/TSRE5vc` passed on WSL's case-sensitive temporary
filesystem: content-path **64/64**, terrain-material **551/551**, and orts-profile
**21/21**. The actual EngLib workload completed 3,597 successful case-variant
lookups in **654 ms in the WSL Debug build**. This is not directly comparable to
the earlier native Windows Release microbenchmark and is not a full content-load
timing. The earlier 44-ms cached-key linear variant remains the native optimized
measurement of the intended comparison strategy. No production files were copied
to the Windows checkout and no user content was changed during validation.

## Hash-index experiment after the cached-key fix (WSL)

The requested test was completed in `/root/TSRE5vc`. Production code still uses
cached-string linear lookup; this experiment adds only standalone test code.
An additional `QHash<QString,int>` gives a measurable improvement over the fixed
linear implementation, especially with large libraries and repeated queries.
It is separate from fixing the original repeated-key regression.

WSL Arch Linux, Qt 6.11.2, GCC 16.2.1, CMake Release. Real input: 3,597 distinct
ENG/WAG names enumerated read-only under `/mnt/c/trainsim/TRAINS/TRAINSET` (zero
duplicate logical keys). Timing excludes enumeration and content parsing. Both
variants retain numeric-ID storage, pointer indirection, loaded-state checks,
and stored string keys. The index is additional storage, not a replacement for
the numeric map. Each measurement uses one warmup and the median of three rounds,
with alternating variant order and expected-ID validation on every lookup.

The practical rows below include one production key computation per request,
along with index growth and object/map insertion for cold population:

| Assets | Workload | Requests | Cached linear | Additional index | Speedup |
| ---: | --- | ---: | ---: | ---: | ---: |
| 3,597 | Cold population | 3,597 | 63.10 ms | 14.25 ms | 4.4x |
| 3,597 | Case-variant hits | 10,000 | 238.49 ms | 52.26 ms | 4.6x |
| 3,597 | Misses | 10,000 | 425.51 ms | 52.77 ms | 8.1x |
| 10,000 | Cold population | 10,000 | 546.58 ms | 38.56 ms | 14.2x |
| 10,000 | Case-variant hits | 2,000 | 349.32 ms | 8.33 ms | 41.9x |
| 10,000 | Misses | 2,000 | 135.97 ms | 8.49 ms | 16.0x |
| 100,000 | Case-variant hits | 1,000 | 5,048.62 ms | 4.64 ms | 1,087.8x |
| 100,000 | Misses | 1,000 | 9,350.24 ms | 3.88 ms | 2,411.8x |

For the real library, initial population saves about 49 ms in the lookup portion.
That alone is a modest absolute improvement after the cached-key fix. Repeated
lookups also benefit; actual editor gain depends on their count and on time spent
parsing, loading textures, rendering, and processing GUI events.

At 100,000 synthetic entries the full-library scans become expensive even for a
bounded batch of 1,000 requests. Cold insertion at that size was deliberately
not timed: incremental linear deduplication would make 4,999,950,000 comparisons.
The synthetic paths have a common prefix and fixed-length numeric filenames;
misses append `.missing` and may be rejected cheaply by length. These distributions
are scale probes, not a prediction of exact timing for every library.

The raw output also separates lookup with keys prepared beforehand. On the real
library, 10,000 hits took 169.43 ms versus 1.76 ms, and misses 374.28 ms versus
2.05 ms. Including key preparation narrows that difference substantially. Some
short timings and linear runs varied; raw minimum/maximum values are preserved.
Do not compare these WSL Release timings directly with the earlier Windows run
or the existing Debug application test.

Recommendation: an index is worthwhile for large libraries and frequent lookups.
Keep `QString hashid` on assets and the numeric-ID map, adding `hashid -> ID` for
lookup. With the agreed case-insensitive cache identity, case-only variants share
one cached object; this does not require physical-file validation or candidate
buckets for different case spellings. A real implementation must keep the index
consistent with failed-load recovery, removal, explicit reload and identity changes,
and preserve shape/texture context and alias rules when extended to other caches.
This test does not implement or validate those lifecycle operations, nor measure
index memory overhead. Qt string storage can be shared, but index entries/buckets
still add memory. The simplified entries do not reproduce full Eng object layout.

All expected IDs and misses passed. No content files were modified, no GUI was
started, and no production hash index was introduced. Reproduction source and
commands: `tests/performance/contentPathLookup/index.cpp` and its README; complete
results: `tests/performance/contentPathLookup/wsl-index-result.txt`.

## EngLib index implementation

The next implementation step adds the measured string-key index to EngLib only.
`engIds` maps the existing `hashid` to the existing numeric ID. Both `addEng` and
`getEngByPathid` use one lookup helper; it checks the indexed entry's presence,
loaded state and stored key without scanning the library or probing the filesystem.
Duplicate `addEng` requests increment the reference count; `getEngByPathid` does
not. Numeric IDs and the public ID-to-object map remain available to consumers.

All production registrations go through `addEng`. Missing/failed loads remain
ineligible and can be retried. `removeBroken` removes a key only when it still
points to the failed ID being removed, preserving a later successful retry under
that key. `removeAll` clears both maps before numeric IDs are reused. The existing
object lifetime policy is unchanged. Source audit found no production writes to
an engine's path identity after construction: full content reload clears/reloads
the library, and `Eng::reload` reloads associated shapes. Future filename changes
or bulk registration must maintain the index through the library.

The collection test now writes 3,597 small ENG fixtures in the suite's temporary
directory and registers them through the actual `EngLib::addEng`, rather than
inserting test objects directly into its public map. It checks every case-variant
get and duplicate add against the expected ID. Additional lifecycle assertions
cover reference counts, non-inserting misses, failed-load retry before and after
removal, successful replacement surviving removal of the older failed entry,
clear/reload, and stale keys after numeric-ID reuse. Timing remains observational;
there is no machine-dependent performance threshold in the test.

Validation: the WSL Debug executable rebuilt successfully and `--test --test-suite
content-path` passed **76/76** on a case-sensitive temporary filesystem. The actual
3,597 indexed EngLib gets took **29 ms**, compared with **654 ms** for the earlier
cached-string linear Debug run. This comparison is observational, not an end-to-end
Conedit timing. All 3,597 duplicate adds also returned their original IDs without
increasing the library size. Log: `/tmp/tsre-eng-index-content-path.log`.
The whitespace check passed. User installations were not modified. Implementation
and validation were performed in the WSL checkout.
