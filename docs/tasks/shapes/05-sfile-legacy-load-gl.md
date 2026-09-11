# SFileLegacy: separate legacy loading and GL initialization

Current integration (2026-09-11): SFileLegacy is the default MSTS backend in ShapeLib. `TSRE_MSTS_SHAPE_BACKEND=old` retains the original SFile/C/X fallback; `legacy` explicitly selects the default. Complex modes remain opt-in. The original scope and comparison history below predate this authorized default switch. Route/consist and hardware-GL practical checks remain pending before removal of the old classes.


Source layout: all SFileLegacy implementation methods live in `src/tsre/shape/SFileLegacy.cpp`, with declarations in `SFileLegacy.h`. Binary loading, UTF-16 loading and GL initialization are sections in that one source file. CPU loading and `initGL()` remain separate methods; the original SFile/C/X classes remain in the project.


## Scope

Keep `SFile`, `SFileX`, `SFileC` and the default factory unchanged. Add one independent
`SFileLegacy : ComplexShape`, with the legacy implementation grouped into its main,
binary, UTF-16 and GL translation units. It does not inherit from or invoke the old
three classes. Preserve legacy rendering, animation, bounds, material handling and
LOD policies. This is a rendering-only loader, not a replacement for Complete storage.

## Lifecycle

- `loadData()` reads shape data, computes bounds, reads metadata and builds legacy
  animation frame lookup tables without an OpenGL context. `isLoaded()` then reports
  CPU readiness.
- Both `odczytajlodd` implementations retain each subobject's native vertex references
  and reversed triangle indices. They do not expand triangles or create buffers.
- `initGL()` expands triangles with the original nine-float layout, material alpha and
  winding, and creates the VAOs/VBOs. Source arrays are released only after all uploads
  succeed. Calling without a context returns false without discarding CPU data;
  repeated initialization after success is a no-op.
  Deferring upload retains indexed data across all subobjects/LODs until initialization,
  so pre-GL memory can exceed the old per-subobject temporary storage. The source
  arrays are not retained during normal rendering after a successful upload.
- `load()` remains a convenience wrapper, initializing GL when a context is current.
  Rendering can also initialize a CPU-loaded shape. `reload()` invalidates readiness;
  the next load releases the previous owned storage and reconstructs it.
- The new class initializes owned pointers/counts and releases them on destruction or
  reload. Copying is disabled. It retains the legacy public data surface for now.

## UTF-16 decision

Keep ParserX and the existing `FindTokenDomIgnore` / `NextTokenInside` traversal in
this pass. Replacing the domain-ignoring searches changes structural traversal and
should be a separate task, with malformed/extended file fixtures. Separating GL does
not require it. The fixed `new fvertex[120000]` allocation is replaced with a vector
sized from the file's vertex count, with a basic input-size bound. The same native
vertex type serves both encodings. No new SIMIS text reader is used.

## Validation

The existing isolated-process corpus harness records `joined_*` results alongside
SFileX/C and SFileComplex. It explicitly releases the context for CPU loading and
checks readiness, no created VBOs, no-context init refusal, repeated init, exact bounds,
vertex payloads/matrices, direct images and picking. A synthetic fixture checks upload
with the source temporarily unavailable, array release, and reload for both encodings.

The production gathered renderer uses pointer-keyed QHash iteration for cached
packets. Transparent ordering can therefore vary between independently allocated
instances. Record native gathered pixel differences separately; also compare gathered
images using a test-only renderer that submits cached packets in their original order.
Do not change production rendering to make this comparison pass.

Final correctness checks:

- Release application build and both CTest suites pass.
- The application GL suite passes 68 checks, including upload failure/retry and
  source-file-independent initialization in both encodings.
- All 129 original TRAINSET shapes pass in isolated GL processes. Every LOD's GPU
  bytes/hierarchy, bounds, direct images and picking match SFileX/C exactly.
- Animation images match at three sampled times for all 34 animated stock shapes.
  This proves preservation of legacy behavior, not independent animation correctness.
- The three original CD shapes and their three normalized UTF-16 exports also pass,
  together with SD402 and the Acela wiper in the focused eight-file comparison.
- Ordered gathered images match throughout. Native gathered images differ on 84/129
  stock shapes (maximum 1,887 pixels out of 192 × 192), reflecting the existing
  pointer-keyed packet ordering. This pass does not claim native gathered pixel
  determinism across separately allocated instances.

Validation evidence: `/tmp/sfile-legacy-stock/results.jsonl`,
`/tmp/sfile-legacy-eight-validation/results.jsonl`, `/tmp/sfile-legacy-gl.log`.
All GL checks here use Mesa software rendering under Xvfb, not a hardware GPU.

## Known boundaries retained from legacy

This task does not add Complete/save storage, new tokens, a new text grammar,
first-LOD-only loading, automatic context reconstruction, or new animation semantics.
The synthetic lifecycle fixture uses nonempty image/texture tables because the legacy
text helpers and their callers both skip a closing token; empty tables expose that
existing traversal limitation. Broadening that behavior belongs with a later ParserX
traversal change, not with this GL separation.

The new class is directly constructible for experiments; normal ShapeLib selection
still uses the existing classes. Destroy/reload GL resources on their owning thread,
as with the existing renderer. The lifecycle follow-up below fixes cached-packet ownership
while preserving the existing batching path.


## Repeated loading comparison (2026-09-10)

Release build, Mesa software GL under Xvfb. Three independent processes per input,
each measuring unchanged SFileX/C and SFileLegacy in the same process. Each process
first loads the Complete/Compact comparison shapes, so input I/O is warm. The original
loader is measured before the joined loader; this is not an alternated-order or cold
route-start benchmark. Context release/reacquisition is outside the timers. All runs
are sequential, without concurrent builds or other tests. No samples were excluded.
All 24 focused processes passed the regression checks.

Values below are medians in milliseconds. CPU, GL and total medians are computed
independently; the median total need not equal the sum of the two component medians.
Both legacy implementations load/upload their original full set of LODs. The binary
rows use the original compressed CD files; generated UTF-16 rows use normalized
exports of those same shapes.

| Input | SFileX/C CPU+GL | SFileLegacy CPU | SFileLegacy initGL | SFileLegacy total | New / old |
|---|---:|---:|---:|---:|---:|
| binary/CD_193290.s | 40.979 | 31.446 | 3.558 | 35.367 | 0.863× |
| binary/CD_193290_FG.s | 16.761 | 10.298 | 0.637 | 10.936 | 0.652× |
| binary/CD_193290_MS.s | 37.429 | 25.763 | 3.313 | 29.076 | 0.777× |
| utf16-generated/CD_193290.s | 135.823 | 129.687 | 10.663 | 140.474 | 1.034× |
| utf16-generated/CD_193290_FG.s | 30.201 | 29.576 | 0.629 | 30.194 | 1.000× |
| utf16-generated/CD_193290_MS.s | 57.667 | 54.430 | 3.223 | 57.653 | 1.000× |
| utf16-original/SD402.s | 15.001 | 12.585 | 1.249 | 13.825 | 0.922× |
| utf16-original/acelawiper.s | 3.765 | 0.353 | 0.083 | 0.437 | 0.116× |

Observed ranges, also in milliseconds:

| Input | SFileX/C total range | SFileLegacy total range |
|---|---:|---:|
| binary/CD_193290.s | 40.714–41.181 | 34.415–35.751 |
| binary/CD_193290_FG.s | 16.029–16.815 | 10.437–14.051 |
| binary/CD_193290_MS.s | 35.851–38.568 | 28.660–30.228 |
| utf16-generated/CD_193290.s | 131.179–141.477 | 138.125–140.477 |
| utf16-generated/CD_193290_FG.s | 29.384–37.442 | 29.494–30.642 |
| utf16-generated/CD_193290_MS.s | 56.705–59.499 | 56.177–67.656 |
| utf16-original/SD402.s | 14.742–15.339 | 13.732–13.906 |
| utf16-original/acelawiper.s | 3.687–5.455 | 0.374–0.516 |

The binary medians improved in this harness; generated UTF-16 is essentially tied,
with a 3.4% increase for the largest shape. SD402 improves by about 8%, and the tiny
wiper benefits substantially from removing the unconditional 120,000-vertex
allocation. These are measurements of this setup, not a guarantee of the same gains
on hardware GL or during a cold route load. The generated UTF-16 files live in a
separate temporary directory; their comparison does not independently validate
texture lookup beside the original assets. The original corpus covers that path.

Raw timing/check data: `/tmp/sfile-legacy-benchmark-{1,2,3}/results.jsonl`;
aggregate samples: `/tmp/sfile-legacy-summary.json`. Input symlinks live under
`/tmp/sfile-legacy-benchmark-inputs`; proprietary assets remain unchanged.

Host qualification: Windows `GetSystemTimes` was sampled in six five-second intervals
before and after the timing batch while our tests/builds were stopped.
Before: 17.38%, 19.81%, 13.28%, 14.65%, 16.04%, 16.43% (mean 16.27%).
After: 27.76%, 25.74%, 20.83%, 39.06%, 35.33%, 25.9% (mean 29.10%).
The host was **not verified idle**. Treat small timing differences as inconclusive
under this background activity. Host samples are in
`/tmp/sfile-legacy-host-{before,after}.json`.

## Lifecycle hardening follow-up

- Cached SFileLegacy packets have shared ownership. Cache invalidation releases its
  ownership immediately; the gathered renderer retains queued packets until the frame
  ends. Other producers can continue using the previous raw-pointer convention.
  This preserves batching. As before, shapes and their matrices/GL resources must
  outlive a submitted frame; packet ownership does not transfer ownership of the shape.
- The binary reader publishes each allocated primitive in the cleanup count before
  later reads can throw, so partial input cannot orphan already allocated indices.
- Upload rollback destroys partially created buffers/VAOs and restores incoming
  VAO/array-buffer bindings. Indexed CPU source remains available for retry.
- Readiness is tied to the creating context. A foreign or destroyed context cannot
  reuse the ready flag; explicit `reload()` and loading/uploading in the new context
  are required. This avoids silently accessing source arrays that upload released.

Focused checks cover queued/unsubmitted packet release, partial binary load/retry,
failed-upload binding/resource rollback, foreign contexts and context destruction.
A separate experiment compares per-vertex material calculation/per-part allocation
against hoisted material calculation and one reusable expansion buffer, using the
original largest compressed CD shape. ParserX traversal remains unchanged.

### Upload experiment, 2026-09-11

Kept the two local changes: material/alpha selection once per nonempty part, and a
scratch float array reused across parts/subobjects during one `initGL()` call. The
array grows only when necessary and is released at function exit. Vertex/index
validation, output layout, winding, and ParserX traversal are unchanged. Empty parts
retain the previous behavior of not consulting their material indices.

The baseline includes all lifecycle fixes above but retains per-vertex material
selection and per-part allocation. Compare against that baseline, not the previous
uncorrected executable. Original input:
`/root/msts/proprietary/msts_app/TRAINS/CD_193_290/CD_193290.s`.

Three alternating process pairs (baseline/candidate, candidate/baseline,
baseline/candidate), two warmups plus 15 measured fresh loads/uploads per process:
45 samples per variant. Upload timing includes `glFinish()`; CPU loading is timed
separately. Each temporary shape is destroyed before the next iteration. No concurrent
builds or other tests ran during the timings; all six corpus processes passed.

| Measurement | Corrected baseline median | Candidate median |
|---|---:|---:|
| CPU loading | 32.174 ms | 32.350 ms |
| initGL, including completion | 3.433 ms | 3.097 ms |
| CPU + initGL | 35.894 ms | 35.664 ms |

Upload medians by process pair: 3.556 → 3.255 ms, 3.373 → 3.273 ms,
3.321 → 2.930 ms. Pooled upload ranges: baseline 2.898–6.540 ms;
candidate 2.729–5.981 ms. No samples were discarded. Component/total medians are
computed independently.

The upload saving is about 0.335 ms (9.8%), with a lower candidate median in every
pair. This supports retaining the small local change; it does not establish a
meaningful overall loading speedup. CPU + GL differs by only about 0.6%, and CPU
loading—which the experiment does not change—also varies.

Raw evidence: `/tmp/legacy-upload-{baseline,optimized}-{1,2,3}/results.jsonl`;
summary `/tmp/legacy-upload-summary.json`. The baseline executable is
`/tmp/TSRE5vc-legacy-upload-baseline`, with its upload source at
`/tmp/SFileLegacyGL-fixed-baseline.cpp`.

Host qualification for this experiment (Windows GetSystemTimes, six five-second
intervals with our build/tests stopped):
- Before: 12.93%, 13.66%, 10.55%, 15.7%, 21.0%, 29.71%; mean 17.26%.
- After: 11.68%, 12.93%, 8.69%, 9.3%, 11.88%, 11.11%; mean 10.93%.

The host was not verified idle. These remain software-GL measurements with background
activity; small overall-load differences are inconclusive.


Final follow-up validation: Release build succeeds; both CTest suites pass; the
expanded GL suite passes 94 checks. The retained optimization plus lifecycle fixes
pass all 129 original stock shapes (including all 34 sampled animated shapes),
with exact LOD geometry, bounds, direct rendering/picking and ordered gathered
rendering comparisons. All six large-CD benchmark processes also pass.
The production pointer-keyed gathered draw order remains unchanged.

Evidence: `/tmp/legacy-fixes-gl.log`, `/tmp/legacy-upload-opt-gl.log`,
`/tmp/legacy-hardened-stock/results.jsonl`. SFile/X/C and ParserX remain unchanged;
the normal factory selection remains unchanged. Changes are not committed.

## Follow-up: reuse animation matrices

- [ ] Replace SFileLegacy's per-part, per-frame animation matrix allocations with
  persistent storage allocated once per shape state and matrix. Update matrix
  values in place before submission, and let parts referencing the same matrix
  share that storage. Keep addresses stable through drawing; do not overwrite or
  release matrices while queued submissions still reference them. Recreate storage
  when a reload changes the matrix layout. Verify independent animation states,
  repeated instances, picking and queued-frame lifetime, and check that steady-state
  animation no longer allocates these matrices per frame.

This concerns animation matrices, not the renderer's separate world/view matrix
copies. It is a recorded follow-up, not an implementation change.

Ownership clarification: `OpenGL3Renderer::pushItem` takes ownership of non-shared
items and copies shared items, deleting the queued objects during frame cleanup.
`pushItemsVNTA` instead borrows cached items for batching; clearing its frame queues
does not delete those objects. SFileLegacy's `retainedPackets` support holds optional
shared references through the frame. GltfShape currently submits cached items through
this borrowed path and clears its raw-pointer caches without deleting their objects.
Matrix reuse and cached-item ownership therefore remain separate concerns.
