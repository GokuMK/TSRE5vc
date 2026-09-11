# SFileLegacy / SFileComplex Compact / Complete collection comparison

## Scope

Compare all 4,306 source shapes in `/mnt/c/trainsim/trains/trainset/`, using
SFileLegacy as the reference. Old SFile/X/C is not executed by this comparison.
The source inventory and lowercase Linux symlink mirror are reused from
[the legacy comparison](sfile-legacy-third-party-comparison.md).
All application processes run under bubblewrap with `/mnt` read-only. Logs and
results are written under `/root/shape-compat-20260911/` on Linux.

## Method

Each file runs in three fresh processes, Legacy then Compact then Complete.
Compact is requested before CPU loading; all modes load all LODs. CPU loading
runs without a GL context, followed by explicit GL initialization. CPU and GL
initialization timings are recorded separately; the latter includes `glFinish()`.
The combined `load_gl_ms` excludes context setup, rendering and texture warming.

Four workers run concurrently; files larger than 8 MiB are serialized within the
pool. Processes have a 90-second timeout, 3 GiB address-space limit and 32 MiB log
limit. Timings are diagnostic single samples, subject to contention and an order/
filesystem-cache advantage for the later modes, not controlled speed benchmarks.
No peak-memory or leak measurement is performed.

Pairwise checks cover CPU/GL success, LOD counts, bounds, size, detail level,
snapping, hierarchy, matrix names, part metadata, submitted geometry buffer hashes,
transforms, every LOD's direct and ordered gathered images and picking, sampled
animation images, and texture ready/error counts. Images use Mesa software GL,
192 × 192, with a common camera taken from Legacy. Size comparisons allow 1e-6
relative/absolute error; transforms allow 1e-5 absolute error. Raw values remain
in results. Other fields and image/buffer hashes are compared exactly. Internal
part IDs and implementation-specific document storage are not equivalence criteria.

The first pilot showed identical geometry but a one-bit size-calculation difference
that changed the automatically chosen camera. Its results are retained in
`three-results/`; the corrected pilot and full run use `three-camera-results/`.
Animation samples use Legacy's first-animation duration at 0%, 25% and 75%.
Differences from Legacy animation are observations, not automatically correctness
bugs. Complete/Compact agreement is evaluated separately. Save/round-trip testing
and independent MSTS/ORTS correctness are outside this rendering comparison.

## Reproduction

```sh
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a \
  python3 tests/shapes/three_compat_compare.py \
  --work /root/shape-compat-20260911 --output three-camera-results --workers 4
```

The runner resumes completed paths. Per-file logs and `results.jsonl` retain
checkpoints even when a mode fails. `TSRE_THREE_COMPAT` selects `legacy`, `compact`
or `complete` in the `shape-complex-corpus-gl` test entry point, returning before
the older multi-implementation corpus harness. `TSRE_COMPAT_DUMP` optionally saves
rendered PNGs to a Linux directory for targeted discrepancy inspection.

## Results

The full run completed **4,306 / 4,306 paths**, representing 4,044 unique source
hashes. No paths were skipped or duplicated. Every source SHA-256 still matches the
previous Legacy comparison; every size/mtime check remained unchanged.

| Outcome | Legacy | Compact | Complete |
| --- | ---: | ---: | ---: |
| CPU-loaded | 4,306 | 4,306 | 4,306 |
| GL initialized and render checks completed | 4,306 | 4,289 | 4,289 |
| Retained as Broken; GL refused | 0 | 17 | 17 |
| Crashes / timeouts | 0 | 0 | 0 |

**Compact and Complete agree on all recorded comparable fields**, including raw
values (not just the numeric tolerances). They produce identical submitted buffers,
static images, picking and sampled animations on all 4,289 successful cases, covering
9,728 LODs. Both have the same 17 failures: 16 UTF-16 coach files with unquoted
spaced labels and one unclosed UTF-16 shape. Legacy rendered all 9,747 source LODs.
Submitted byte counts match public triangle counts throughout the successful checks.
Complete/Compact requested retention is correct for all files; each successful new
shape has Valid health. These findings do not validate saving the full collection.

Against either new mode, Legacy has:

- **3,322** cases matching every comparison criterion.
- **967** successful cases with one or more differences; **666** differ only in
  animation. Of all successful comparisons, **3,988** match every non-animation check.
- **4,117 / 4,289** cases with identical static direct, gathered and picking images
  at every LOD; **172** have at least one static image difference.
- Animation image differences in **780 / 1,629** commonly sampled shapes. These
  overlap other categories and are not automatically new-implementation bugs.

Difference counts below count files, not differing LODs, and overlap:

| Check | Files differing from Legacy |
| --- | ---: |
| Bounds (exact float values) | 144 |
| Submitted buffer bytes | 210 |
| Direct static image | 172 |
| Ordered gathered static image | 150 |
| Picking image | 8 |
| Ready texture count | 72 |
| Part metadata | 7 |
| Matrix names | 1 |
| Sidecar detail level | 1 |

All modes report texture missing/error conditions on **340 shapes**, with identical
error counts. Standalone submodels can need a caller-provided vehicle texture root;
these cases limit complete appearance validation. The 72 ready-texture-count
changes are separate from texture errors (see the loading-policy finding below).
No production parser or renderer fix was made during this comparison.

### Diagnostic performance summary

Cells are **mean (median), milliseconds**, for CPU loading plus GL initialization.
Only the **4,289 files successfully rendered in all three modes** enter the table;
the 17 UTF-16 failures are excluded equally from all timing columns, including Legacy.

| Format | Files | Legacy | Compact | Complete |
| --- | ---: | ---: | ---: | ---: |
| Compressed binary | 4,055 | 28.83 (22.92) | 29.42 (23.49) | 41.06 (32.23) |
| Uncompressed binary | 48 | 40.81 (30.22) | 40.73 (26.59) | 52.86 (33.43) |
| UTF-16 text | 186 | 86.80 (70.96) | 98.82 (85.33) | 120.39 (105.57) |
| **All** | 4,289 | 31.48 (23.30) | 32.55 (23.88) | 44.63 (32.91) |

The aggregate observed mean is about **3.4% higher for Compact** and **41.8% higher
for Complete** than Legacy. These are concurrent, single-sample observations with
fixed mode order, not controlled benchmark claims. They exclude texture warming,
render checks, context setup and save tests. Do not compare them directly to the
previous run's means as an optimization result: the timing boundary now explicitly
includes GL completion and the common-success population excludes 17 files.

### Artifacts and follow-up

Under `/root/shape-compat-20260911/`:

- `three-camera-results/results.jsonl` and per-mode logs: full corrected run.
- `three-summary.json`: coverage, pairwise categories and per-stage timing aggregates.
- `three-provenance.json`, `three-corpus-harness.cpp`: executable/harness hashes and
  the exact test source used for the full run. The subsequent optional raw-buffer
  dump addition was used only for targeted diagnostics, outside corpus timings.
- `three-diagnostics/`: PNGs, float buffers, source-derived diagnostic exports and
  focused difference summaries. All exports are on Linux.

Compact and Complete are internally consistent on this corpus, but neither is yet
a drop-in compatibility replacement for Legacy. Address the two rejection cases
and sidecar-header tolerance, then rerun their affected files. Animation correctness
still needs an independent reference; other observed render differences need review
before claiming Legacy equivalence. Eager texture loading is a separate potential
Compact optimization. Source files and production implementations remain unchanged.

### Targeted diagnostics during the run

PNG reruns use the same fixed Legacy camera. Pixel counts below are out of 36,864;
channel differences are on the 0–255 scale. Compact and Complete agree in these cases.

| Shape | Image | Legacy vs Compact differing pixels | Maximum channel difference |
| --- | --- | ---: | ---: |
| `CD_163-OR/CD_163046.s` | LOD 0 direct / gathered | 37 / 37 | 107 |
| same | LOD 2 direct / gathered | 9 / 9 | 1 |
| `cd_843-024/cd_843024ms.s` | LOD 0 direct / gathered | 36 / 0 | 141 / 0 |
| `CD_193_290/CD_193290.s` | static direct / gathered | 0 / 0 | 0 |
| same | 25% and 75% animation | 809 each | 151 |

The CD163 Z bound differs by about 9.54e-7. ParserX accumulates decimal digits into
floats; SimisTextReader converts through `std::from_chars` into double before typed
storage conversion. This explains why exact float equivalence should not be assumed
for text input, but does not establish the cause of every geometry/image difference.
The CD193 animation discrepancy is consistent with the earlier focused comparison;
Legacy animation is not an independent correctness reference.

Logs, PNGs and pixel counts are under `three-diagnostics/` in the artifact directory.

Additional inspected differences:

- SFileComplex `syncTextures()` requests every declared image; Legacy requests images
  while processing parts. For example, `CTL_182/ctl_182065.s` ends with 16 ready
  textures in Complex and 15 in Legacy, with no texture errors. This is a real
  resource-loading policy difference and a possible Compact optimization topic.
- Some text shapes use doubled backslashes in image names. Legacy retains both;
  the new reader unescapes them. These are recorded as part-metadata differences
  even when images agree (for example the MMP 7.15 m tank containers).
- `MMP_COMMON/FA_30ft/30bs_PAGU.s` declares `matrix 30PAGBS_frame`. Compact and
  Complete preserve that name; Legacy reports `PAGBS_frame`, dropping the leading
  digits. This discrepancy is verified against the read-only source text and is
  not evidence of a new-parser regression.

### Blocking compatibility finding: unquoted matrix labels containing spaces

Sixteen coach submodels retain loaded data but report Broken
health and refuse GL initialization in both Compact and Complete. Legacy renders
them. Representative source: `PKOR_B-111A/PKPIC_B10nou-70915_M.s` (UTF-16).
Its 23-entry matrix block contains `matrix drzwi 1 (...)`, `matrix drzwi 1_006 (...)`
and similar names with unquoted spaces. The document parser's child-block detection
recognizes an opening parenthesis directly after the token or after one label token.
It does not recognize these multi-token labels as matrix blocks. This loses matrix
entries and causes `Invalid matrix reference; Hierarchy/matrix count mismatch;
Invalid primitive state` during extraction.

This is a permissiveness gap relative to Legacy, not an allocation limit or crash.
A follow-up should recover such labels while preserving matrix order/count, keeping
Complete saving capable of writing a normalized quoted label. Production parsing
is unchanged during this comparison.

Example:
    matrix DOOR_E1 ( 1.0 0.0 0.0 0.0 1.0 0.0 0.0 0.0 1.0 -0.0548781156539917 0.029610514640808105 0.2791147232055664 )
    matrix DOOR_E2 ( 1.0 0.0 0.0 0.0 1.0 0.0 1.4901157641133977e-08 0.0 0.9999996423721313 0.04827690124511719 -0.010378837585449219 -0.1901254653930664 )
    matrix drzwi 1_002 ( 1.0 0.0 0.0 0.0 1.0 0.0 1.4901157641133977e-08 0.0 0.9999996423721313 1.4270846843719482 -0.7201777696609497 0.12150764465332031 )
    matrix drzwi 1_004 ( 1.0 0.0 0.0 0.0 1.0 0.0 1.4901157641133977e-08 0.0 0.9999996423721313 1.4270846843719482 -0.7201777696609497 0.12150764465332031 )


### Sidecar header permissiveness

`PKP IC RRT/Interior.SD` contains `ESD_Detail_Level (0)` but uses a shape-style
`JINX0s1t` header rather than the metadata kind expected by the new reader. Legacy
reports detail 0; both new modes report -1. A diagnostic export confirms that the
new reader interprets the leading `Interior.S ESD_Detail_Level (0)` as a block named
`Interior.S` with label `ESD_Detail_Level`, instead of the filename followed by a
metadata child. Geometry still loads. This is another format-tolerance follow-up,
verified against source and document export, not a performance or memory failure.

### Blocking compatibility finding: unclosed input

`PKP_ET41_203E-OR/PKP_ET41-045B_z.s` (UTF-16) loads and renders in Legacy, but
both new modes retain it as Broken and refuse GL initialization with
`Unclosed shape at 4677894`. Source inspection finds 90,125 opening parentheses
and 90,124 closing parentheses; a quote-aware stack scan ends with the outer
Shape opening unmatched and no unfinished quoted string. This is malformed-input
recovery policy, distinct from the spaced-label parser issue. No source repair
was made. The comparison records the refusal rather than counting it as a match.


Raw-buffer follow-up: `CD_163-OR/CD_163046.s` has 386,452 differing float values
across its submitted LOD buffers, all in position/normal/UV fields, with maximum
absolute delta 0.00048828125 (large UV coordinates). The source's first UV in
`common.inc/Lokomotywy/BR232/DBSPL_BR232-045_2.s` is approximately
(-0.06334, -0.235752); it also contains empty `vertex_uvs (0)` lists. The 42 differing
UV components in the untextured seven-triangle part use that first UV in Legacy
and (0,0) in the new modes. Positions, normals and rendered images agree in this
example. These inspected cases do not explain every buffer or image discrepancy.

A diagnostic document export of the spaced-label coach contains 19 matrix blocks
and four `drzwi` blocks instead of the source's 23 matrices, confirming the parser
interpretation described above.

### Files blocked from GL initialization

- `PKOR_B-111A/PKPIC_B10nou-70915_M.s`
- `PKOR_B-111A/PKPIC_B10nouz-71103_M.s`
- `PKOR_B-111A/PKPIC_B10ou-78701_M.s`
- `PKOR_B-111A/PKPIC_B9ouv-78019_M.s`
- `PKOR_B-111A/PKPIC_B9ouv-78063_M.s`
- `PKOR_B-111A/PKPIC_Bdnu-78429_M.s`
- `PKOR_Bc-110A/PKPIC_Bc9ou-70063_M.s`
- `PKOR_Bc-110A/PKPIC_Bcdu-70064_M.s`
- `PKP_110A-OR/PKPIC_Bc9ou-70063.s`
- `PKP_110A-OR/PKPIC_Bcdu-70064.s`
- `PKP_111A-OR/PKPIC_B10nou-70915.s`
- `PKP_111A-OR/PKPIC_B10nouz-71103.s`
- `PKP_111A-OR/PKPIC_B10ou-78701.s`
- `PKP_111A-OR/PKPIC_B9ouv-78019.s`
- `PKP_111A-OR/PKPIC_B9ouv-78063.s`
- `PKP_111A-OR/PKPIC_Bdnu-78429.s`
- `PKP_ET41_203E-OR/PKP_ET41-045B_z.s`


## Follow-up: exact syntax and Open Rails behavior

See [the malformed-input investigation](sfile-malformed-input-investigation.md).
The missing parenthesis is inside the animation section, between original lines
121633 and 121634, not an established omission at EOF. OR accepts the spaced matrix
labels with warnings but uses the first token (`drzwi`) for each separate matrix.
The earlier suggestion to normalize full multi-token labels must not be treated as
proven OR-equivalent runtime behavior. OR also accepts the unbalanced file, but loses
one animation-node name; a diagnostic copy repaired at the exact omission loads cleanly.


Subsequent animation-degradation fix: the ET41 file now initializes GL in both new
modes as Recovered, with its damaged animation section disabled. Targeted checks
match the correctly closed private copy's static output in every LOD. See the
[investigation and validation](sfile-malformed-input-investigation.md). The full-run
counts above remain the historical results; the corpus has not been rerun for this
change, and spaced-label parsing is unchanged.


Subsequent label recovery is implemented and validated across the collection:
all 4,306 files now render in Compact and Complete, with 4,289 Valid and 17 Recovered
in each mode. The previously successful cases have no new comparison differences.
See [context-aware label recovery and parsing-time results](sfile-label-recovery.md)
for the final UTF-16 replay, normalized-save tests and controlled timing table.
