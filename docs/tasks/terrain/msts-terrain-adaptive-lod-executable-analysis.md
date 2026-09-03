# MSTS executable analysis: adaptive terrain LOD

Status: exploratory reverse-engineering task; success is not assumed.

Related implementation review:
[terrain heightmap resolution support](terrain-heightmap-resolution.md).

## Objective

Analyze an installed Microsoft Train Simulator executable sufficiently to find
evidence about its adaptive terrain-mesh selection logic. The main target is
the relationship among:

- the external E-RAW error buffer;
- the embedded AS (always-select) buffer;
- per-patch `ErrorBias`;
- `terrain_errthreshold_scale`;
- route `.trk` `TerrainErrorScale`;
- `terrain_alwaysselect_maxdist`;
- camera distance, display terrain-detail settings, and emitted terrain
  triangles.

The most valuable result would be recovered pseudocode or a well-supported
selection equation. Partial results are also useful: file-loading call paths,
memory layouts, comparison branches, controlled behavioral observations, or a
clear record of approaches that did not locate the relevant code.

Do not assume that symbols, recognizable function boundaries, or a complete
answer can be recovered. Keep confirmed evidence, strong inference, and
speculation explicitly separated.

## Secondary objective: collect terrain mechanics encountered on the way

Do not restrict the report so tightly to E/AS/ErrorBias that other terrain
mechanics discovered in the same call paths are lost. Record every terrain
mechanism encountered incidentally when there is concrete evidence, even when
it does not answer the primary hypotheses. Examples include:

- height-buffer decoding, floor/scale conversion, and boundary ownership;
- N, F, C, D, or patchset-F buffer handling;
- terrain-patch flags, water, texture/shader selection, transfers, and terrain
  shapes;
- terrain visibility/culling, tile loading, distant terrain, and TD quadtree
  traversal;
- mesh topology, index generation, crack prevention, skirts, or stitching;
- regeneration/invalidation of derived terrain files;
- route, tile, graphics-setting, and camera inputs to terrain quality;
- fixed or dynamic assumptions for sample count, patch count, sample spacing,
  tile extent, and World-tile coordinates.

Do not let an incidental branch derail the primary trace. Add it to a terrain
mechanics inventory with the relevant address, input data/token, observed
behavior, confidence, and a suggested follow-up.

## Analysis-workflow dependency

Follow the separate executable-analysis workflow for environment setup,
tooling, binary handling, evidence capture, and general reporting discipline;
this terrain brief does not repeat those instructions. Terrain-specific
evidence must still identify the exact executable build used, and controlled
fixture runs must detect whether MSTS rewrote/regenerated the tested terrain
files before their behavior is interpreted.

## Primary hypotheses to test

### H1: `ErrorBias == 0` bypasses E for that patch

This is the current leading hypothesis. TSRE sets a modified patch's
`ErrorBias` to zero but does not rewrite E-RAW. MSTS nevertheless displays that
patch at finer resolution. A plausible implementation is therefore:

```text
if patch.ErrorBias == 0:
    force full/finer patch refinement without consulting E
else:
    use E with a threshold affected by ErrorBias
```

This is more specific than merely saying that bias participates in the error
equation. It must be tested. The executable may still load E unconditionally
while bypassing its values for zero-bias patches, so file-open evidence alone
cannot prove or disprove the hypothesis.

### H2: non-zero `ErrorBias` controls how E is used

Installed route data contains only observed values `0.0` and `1.0`, but the
field is a float. Determine whether positive values:

- multiply or divide E;
- multiply an allowed error threshold;
- add a bias to a screen-space/object-space metric;
- select among discrete policies;
- or are effectively treated as boolean by MSTS.

Test at least `0`, `0.5`, `1`, and `2` if controlled experiments are safe. Do
not infer monotonic direction from the field name. Period route-building
documentation establishes only that `1` is the default and `0` gives finer
terrain detail.

### H3: AS overrides the E test

AS appears to preserve authored important vertices, such as those around track,
even when ordinary geometric error would allow removal. The likely rule is an
OR condition:

```text
select vertex = selected_by_AS
             OR projected_error(E, camera) exceeds effective threshold
```

Determine whether AS is consulted:

- before or after E and patch bias;
- only within `terrain_alwaysselect_maxdist`;
- at all distances when the max-distance field is zero;
- as individual requested vertices or as an already dependency-closed mask;
- when `ErrorBias == 0`, where the entire patch may already be refined.

### H4: E is a saturated restricted-quadtree error hierarchy

The observed E values resemble a per-vertex geometric error hierarchy with
maximum errors propagated through dependency ancestors. This is the standard
"error saturation" approach used by restricted-quadtree terrain algorithms.
It is a useful search model, not proof of MSTS's exact implementation.

Historical reverse-engineering recollection from the TSRE author: opening an
E-RAW in a graphics editor appeared to reveal diagonal lines, triangles, and
larger coherent regions. This is a useful clue, but not yet validated evidence.
It fits a hierarchical triangle interpretation: a scalar at a candidate vertex
may measure how poorly the original surface fits the larger triangle that would
remain if that vertex were omitted. Vertices/regions below the active tolerance
can be represented by larger triangles; high-error or forced vertices require
subdivision. If H1 is correct, `ErrorBias=0` bypasses this test for the whole
patch.

E probably does not store explicit triangle records or an area bitmap. The
diagonals may emerge from candidate-vertex placement, alternating triangle
orientation, dependency links, and propagated maxima. They may also be partly
an artifact of displaying little-endian float bytes as ordinary 8-bit pixels,
so the original visual impression must be reproduced with correctly decoded
float data before it guides executable analysis.

Look for code that:

- evaluates a candidate vertex against the interpolation of coarser parents or
  opposite neighbors;
- takes absolute values or maxima of child/descendant error values;
- projects object-space error using camera distance, field of view, viewport
  scale, or a reciprocal distance;
- recursively/subdivision-traverses powers of two;
- forces related vertices to prevent cracks;
- creates index lists or triangle strips from the selected hierarchy.

### H5: route, tile, and display error scales may be separate inputs

In addition to tile token 137 (`terrain_errthreshold_scale`), the route `.trk`
contains `TerrainErrorScale`. MSTS also has a user-facing terrain-detail
setting. Determine whether these are multiplied together, applied at different
stages, or whether one overrides another. Do not confuse the route-level
`TerrainErrorScale` with the similarly named tile-level field.

## Known terrain file data

MSTS binary `.t` files use little-endian token IDs and length-delimited blocks.
The relevant structure is:

| Token | Conventional name | Relevant value |
| ---: | --- | --- |
| `136` | `terrain` | parent terrain record |
| `137` | `terrain_errthreshold_scale` | tile-level float |
| `138` | `terrain_alwaysselect_maxdist` | tile-level float |
| `139` | `terrain_samples` | sample metadata parent |
| `140` | `terrain_nsamples` | sample count `N` |
| `142` | `terrain_sample_floor` | elevation floor |
| `143` | `terrain_sample_scale` | Y quantization scale |
| `144` | `terrain_sample_size` | sample spacing in metres |
| `146` | `terrain_sample_ybuffer` | external Y-RAW filename |
| `147` | `terrain_sample_ebuffer` | external E-RAW filename |
| `148` | `terrain_sample_nbuffer` | external N-RAW filename |
| `281` | `terrain_sample_asbuffer` | embedded opaque AS block |
| `282` | `terrain_sample_usbuffer` | optional embedded unknown block |
| `164` | `terrain_patchset_patch` | 61-byte patch payload; last float is `ErrorBias` |

For a normal `N=256`, 8 m terrain tile:

- Y-RAW is `N * N * 2 = 131,072` bytes of little-endian unsigned height
  samples;
- E-RAW is `N * N * 4 = 262,144` bytes and decodes as little-endian float32;
- N-RAW is `N * N = 65,536` bytes of old normal-shading data;
- AS normally has `ceil(257 * 257 / 8) = 8,257` payload bytes;
- the patch grid is normally `16 x 16`, giving 256 patch records and 16 stored
  samples per patch side.

The AS child itself begins with:

```text
uint32 token = 281
uint32 blockLength
uint8  labelLength
utf16  label[labelLength]
byte   payload[blockLength - 1 - 2 * labelLength]
```

Do not search only for an 8,257-byte allocation. The parser may retain the
whole block, use a generic token reader, round allocations, or immediately
expand bits into another structure.

The candidate AS bit mapping supported best by route data is LSB-first with a
linear index similar to `z * 257 + x`, but orientation, transposition, and the
exact boundary convention remain unproven. In the audited corpus all set AS
bits were inside the active `0..255` rows and columns; none used the extra row
or column even though the standard payload can represent them.

`terrain_sample_usbuffer`/token 282 is still semantically unknown. No example
was found in the local corpus. It may appear near AS in parser code and can be
used as a structural landmark, but do not assume that it is another AS-sized
mask.

The TSRE token dictionary also names terrain fields that its current terrain
loader does not implement: `terrain_sample_cbuffer` (149),
`terrain_sample_dbuffer` (150), `terrain_patchset_fbuffer` (162), terrain
transfers (165/166), and terrain shapes (167/168). If the executable trace
reaches any of them, record their parsing and runtime use in the incidental
terrain-mechanics inventory rather than silently skipping them.

## Existing empirical evidence

An audit of 1,351 installed `.t` files under the local MSTS routes found:

- 436 tiles with AS and no tiles with US;
- AS always used the standard 8,257-byte payload;
- 1,257 E-RAW files, each exactly `N * N * 4` for the audited `N=256` tiles;
- 409,590 set AS bits across the AS-bearing tiles;
- 240,605 AS positions with positive E and 168,985 AS positions with `E=0`;
- positive E at AS positions averaged about `6.69`, compared with about `0.87`
  at positive-E positions outside AS;
- 345,856 patch records, using only `ErrorBias=0` or `ErrorBias=1`;
- both bias values with and without AS, with low patch-level overlap;
- where tokens 137/138 were present, `terrain_errthreshold_scale` was `1` and
  `terrain_alwaysselect_maxdist` was `0`.

These observations strongly suggest that E, AS, and patch bias are distinct:

- E represents geometric importance;
- AS represents authored importance that geometry cannot reconstruct;
- `ErrorBias` is a patch-wide control and may bypass E at zero.

They do not reveal MSTS's exact comparison equation.

## TSRE source landmarks

The TSRE code does not implement MSTS adaptive triangulation, but it provides a
ready parser and identifies where each field lives.

### `.t` parsing and serialization

- [`src/tsre/world/TFile.h`](../../../src/tsre/world/TFile.h) declares
  `errthresholdScale`, `alwaysselectMaxdist`, `sampleEbuffer`, `sampleNbuffer`,
  `sampleASbuffer`, and the per-patch `errorBias` array.
- [`src/tsre/world/TFile.cpp`](../../../src/tsre/world/TFile.cpp) lines 104-148
  parse the terrain root. Tokens 137 and 138 are handled at lines 117-125.
- `TFile::get139()` at lines 150-215 parses sample metadata. E/N filename
  tokens are handled at lines 195-203 and the current fixed-size AS reader at
  lines 205-209.
- `TFile::get163()` at lines 394-430 parses all patch records. `ErrorBias` is
  the final float read at line 426.
- `TFile::save()` writes AS around lines 745-749, E/N filename references
  around lines 772-788, and patch `ErrorBias` at line 886.

The current AS reader/writer is fixed to 257 x 257 and should be treated only
as a useful landmark, not as a complete format specification.

### Token dictionaries and the route-level terrain scale

- [`src/tsre/fileFunctions/TS.h`](../../../src/tsre/fileFunctions/TS.h) lines
  155-191 define the numeric terrain token range 132-168; lines 274-275 add
  water-height token 251 and AS token 281. It also defines route token
  `TerrainErrorScale=1230` around line 1146.
- [`src/tsre/fileFunctions/TS.cpp`](../../../src/tsre/fileFunctions/TS.cpp) lines
  150-186 map those terrain IDs back to their conventional names, line 270
  identifies AS, and line 1141 maps `TerrainErrorScale` to the text spelling
  `terrainerrorscale`.
- `TS.cpp` says it is currently unused by TSRE, but it is still a valuable
  broad token-name catalogue and static-analysis search list. It exposes
  C/D buffers, patchset F, terrain transfers, and terrain shapes that are easy
  to miss when starting only from `TFile.cpp`.
- Token 282/US is absent from this TSRE enum/map, which is itself a useful
  warning that the catalogue is not a complete MSTS format specification.
- [`src/tsre/world/Trk.cpp`](../../../src/tsre/world/Trk.cpp) initializes the
  route-level terrain error scale to 1 around line 34, parses
  `terrainerrorscale` at lines 174-176, and writes `TerrainErrorScale` around
  line 371. [`Trk.h`](../../../src/tsre/world/Trk.h) currently stores it as an
  integer; treat that TSRE type choice as a possible TSRE limitation, not as
  evidence of MSTS's in-memory type.

### TSRE's compatibility fallback

- [`src/tsre/world/Terrain.cpp`](../../../src/tsre/world/Terrain.cpp) lines 89-100
  load the `.t`, Y-RAW, and optional F data. They do not load E-RAW or N-RAW.
- `Terrain::setHeight()` sets the containing patch's bias to zero at line 143.
- `Terrain::setErrorBias(float)` at lines 1069-1075 edits selected patches.
- The coordinate form at lines 1104-1115 edits the patch containing a terrain
  position.

This is the basis for H1: TSRE relies on patch bias zero without updating E.

### Existing research summary

The detailed format review and modern-runtime recommendations are in
[`docs/tasks/terrain/terrain-heightmap-resolution.md`](terrain-heightmap-resolution.md),
especially the sections "The E buffer is an external adaptive-LOD error
field" and "Likely relationship between E, AS, and patch ErrorBias".

## External references

- The independent
  [`msts-tools` terrain grammar](https://github.com/twpol/msts-tools/blob/d34243e2821e34af47f9e917034040f289400fc0/Resources/terrain.bnf)
  places AS, optional US, optional F, Y, optional E, and N in
  `terrain_samples` but treats AS/US as opaque buffers.
- [Open Rails' terrain parser](https://github.com/openrails/openrails-unstable/blob/41fbd610f3221ece60ef762b50d2f708e92eda9d/Source/Orts.Formats.Msts/TerrainFile.cs#L91-L149)
  reads the E/N filenames but skips AS, F, and numeric token 282 with TODOs.
  Open Rails is a parser/compatibility reference, not evidence of MSTS runtime
  behavior.
- A period
  [MSTS route-building guide](https://st2.indiarailinfo.com/kjfdsuiemjvcya0/0/6/6/5/1607665/0/routebuilding11901717.pdf)
  documents `ErrorBias=1` as default, `0` for finer terrain, dynamic terrain
  changes with viewer distance, and the Route Editor's "Clear tile always
  select" command.
- [DEMEX documentation](https://www.digital-rails.com/files/demex_tutorial.pdf)
  describes E/N as regenerable terrain buffers containing error-bias and
  normal-shading information.
- The
  [restricted-quadtree terrain publication](https://www.ifi.uzh.ch/en/vmml/publications/vis-98.html)
  and a later
  [terrain multiresolution survey](https://www.crs4.it/vic/data/papers/tvc2007-semi-regular.pdf)
  explain per-vertex geometric error and maximum-error propagation through a
  dependency hierarchy. Use these to recognize algorithms, not to assume MSTS
  copied a particular implementation.
- A later
  [Microsoft ESP terrain white paper](https://fliphtml5.com/yyld/cvaw/Global_Terrain_Technology_for_Microsoft_ESP_White_Paper/)
  describes Microsoft's use of restricted-quadtree triangulation and a
  screen-space error metric. It is suggestive background from another product,
  not direct MSTS evidence.

## Recommended investigation sequence

At every phase, add concrete incidental terrain discoveries to the terrain
mechanics inventory even if they are outside the immediate E/AS/ErrorBias
branch.

### Phase 1: identify processes, modules, and file access

1. Determine which executable/module performs terrain loading in simulation
   mode and Route Editor mode. Do not assume they use different binaries or the
   same code path.
2. Trace opens, reads, seeks, mappings, and writes for one known tile's `.t`,
   `_y.raw`, `_e.raw`, and `_n.raw` files. ProcMon is sufficient for the first
   pass; debugger/API breakpoints are needed to connect handles to callers.
3. Verify whether E/N are read, memory-mapped, regenerated, or merely checked
   for existence in each mode.
4. Record whether missing E causes generation before rendering, delayed
   generation, fallback triangulation, a warning, or failure.
5. Hash generated files and compare them across runs before drawing conclusions
   from changed behavior.

Useful string/static landmarks may include `_e.raw`, `_n.raw`, `_y.raw`, token
names if text parsing tables survive, error messages, and terrain settings UI
strings. If suffix strings are constructed dynamically, follow the filename
read from token 147 instead.

### Phase 2: find E and AS in memory

1. Break after the known E file is read and identify its allocation. Confirm
   recognizable float values from the fixture before treating it as E.
2. Put read watchpoints on distinctive E entries and follow consumers. An E
   fixture containing a small number of unique finite values can make this much
   easier than natural terrain.
3. Locate the AS payload or its expanded representation. Search both original
   bytes and candidate expanded flags.
4. Use a fixture with a unique AS byte pattern and one selected position where
   E is zero. Follow bit tests, shifts, masks, and coordinate calculations.
5. Look around E/AS consumers for powers-of-two traversal, `257`, `256`, `16`,
   divisions/shifts by 3 for bit addressing, and maximum/absolute-value float
   operations. Constants alone are weak evidence; require data-flow
   confirmation.

#### Reproduce and interpret the visual E pattern

Before treating the remembered diagonal/triangle appearance as algorithmic
evidence, create several views of representative E-RAW files:

- decode the file as an `N x N` little-endian float32 grid and render linear
  and logarithmic heatmaps;
- render separate masks for `E=0`, `E>0`, and several meaningful thresholds;
- render the four individual byte planes of each float and the legacy raw
  8-bit interpretation to identify graphics-editor artifacts;
- separate candidate positions by inferred hierarchy/subdivision level and
  check whether diagonals belong to particular levels;
- overlay E with Y height, local interpolation residual/curvature, AS bits,
  16 x 16 patch boundaries, and patch `ErrorBias`;
- compare observed diagonals with plausible alternating triangle splits and
  with the dependency directions expected from restricted-quadtree or binary
  triangle-tree traversal.

The useful question is not merely whether the image contains triangles, but
whether E at each hierarchical position predicts the error of replacing its
finer surface region with the specific coarser triangle implied by that
position. Record both confirming and contradictory examples.

The target is the first routine that converts E/AS and camera/patch state into
selected vertices, indices, strips, or triangle counts—not merely the file
loader.

### Phase 3: trace `ErrorBias`

1. Use a `.t` fixture with a distinctive safe bias such as `0.5` in only one
   known patch and `1` elsewhere.
2. Locate the parsed float in memory from its final position in token-164's
   61-byte payload.
3. Watch reads of that value while the patch becomes visible or changes LOD.
4. Compare traces for `0`, `0.5`, `1`, and `2` while holding Y, E, AS, camera,
   display settings, and route files constant.
5. Specifically look for a compare-to-zero and branch around E lookup or
   threshold evaluation. On this old x86 executable, inspect both x87 and SIMD
   floating-point paths.
6. Distinguish these possibilities:

```text
bias == 0 -> bypass E / force patch
threshold = baseThreshold * bias
effectiveError = E / bias
effectiveError = E * function(bias)
bias treated as boolean or enum-like value
```

If zero takes a separate branch, follow both successors to the emitted mesh or
selection tree. A branch that only changes preprocessing or cache invalidation
is not yet proof of runtime bypass.

### Phase 4: controlled black-box experiments

Use one visually distinctive, backed-up terrain tile and change only one input
per test. Keep the camera position, field of view, terrain-detail setting,
weather, and tile visibility constant. Prefer MSTS wireframe mode, captured
index/triangle counts, or debugger-observed selection counts over subjective
screenshots.

#### ErrorBias versus E

| Test | E | Patch bias | What it distinguishes |
| --- | --- | ---: | --- |
| A | original | `1` | baseline E-controlled LOD |
| B | original | `0` | candidate E bypass/full patch |
| C | all zero | `1` | whether E suppresses refinement |
| D | all zero | `0` | strongest H1 comparison with C |
| E | distinctive large values | `1` | whether larger E preserves detail |
| F | distinctive large values | `0` | whether zero bias makes E irrelevant |
| G | original | `0.5`, `2` | continuous versus boolean bias semantics |

An especially strong H1 result would be identical fine geometry for multiple
radically different E buffers when bias is zero, combined with clearly
different geometry for those same buffers when bias is one. Confirm that MSTS
did not silently regenerate or replace E before accepting this result.

Do not use malformed NaN/infinity/truncated fixtures until finite, correctly
sized experiments work. Such inputs may test validation but can obscure LOD
semantics or crash the process.

#### AS versus E

| Test | E at target | AS at target | Expected clue |
| --- | ---: | ---: | --- |
| H | `0` | clear | target may be removable |
| I | `0` | set | direct evidence of AS override |
| J | large | clear | E-driven refinement |
| K | large | set | whether AS adds anything beyond E |

Repeat H/I at several distances. Then change
`terrain_alwaysselect_maxdist` from its observed `0` to one or more finite
values. Determine units and whether zero means unlimited, disabled, or a
default supplied elsewhere.

#### Global/tile error scale

Vary `terrain_errthreshold_scale` while E, AS, patch bias, display settings,
route `.trk` `TerrainErrorScale`, and camera remain fixed. Determine whether it
scales the error, the allowed tolerance, or a later distance calculation. Then
vary route `TerrainErrorScale` separately, followed by the MSTS display
terrain-detail setting. Establish whether all three reach the same calculation
or control distinct stages.

### Phase 5: mesh construction and crack handling

Once the selection predicate is located, follow it far enough to establish:

- whether the hierarchy is a restricted quadtree, a binary triangle tree,
  strips/fans, or another structure;
- whether E is already saturated on disk or propagated again in memory;
- how forced AS vertices introduce ancestors/dependencies;
- how a zero-bias patch meets a neighboring normal-bias patch;
- how boundaries between adjacent terrain tiles remain crack-free;
- whether boundary ownership explains the `N x N` E field versus the nominal
  `(N+1) x (N+1)` AS capacity;
- whether the renderer constructs a new index list, incrementally edits a
  retained mesh, or uses several prebuilt LODs.

If feasible, trace the selected count to the relevant Direct3D draw call or
index-buffer construction. Do not assume a particular Direct3D version; old
COM-vtable calls may not appear as ordinary imported draw-function xrefs.

## Questions the final analysis should answer

Answer each as **confirmed**, **probable**, **unsupported**, or **not found**:

1. Does `ErrorBias == 0` bypass E lookup/evaluation for that patch?
2. Does zero mean literally full stored resolution or merely a zero error
   tolerance that can still remove exactly planar/zero-error vertices?
3. How are positive/non-zero bias values interpreted?
4. Is E used directly as stored, rescaled, or regenerated/propagated in memory?
5. What is the E index mapping and what units do its float values represent?
6. Is AS tested independently of E, or converted into the same transient
   hierarchy?
7. Does stored AS already contain dependency closure?
8. What does `terrain_alwaysselect_maxdist=0` mean, and what are non-zero units?
9. Where does `terrain_errthreshold_scale` enter the calculation?
10. Where does route `.trk` `TerrainErrorScale` enter, and how does it interact
    with tile `terrain_errthreshold_scale`?
11. How does the user's terrain-detail graphics setting affect the same rule?
12. How are patch and terrain-tile boundaries kept crack-free when refinement
    differs?
13. Is N-RAW used only as old lighting/normal cache, and can its code path be
    cleanly separated from adaptive geometry?
14. Does MSTS use `terrain_nsamples`, patch count, and sample spacing
    dynamically in these routines, or are 256/257/16/8 assumptions compiled
    into the executable?
15. What other terrain mechanics were found incidentally, and what concrete
    evidence establishes each one?
16. Does the remembered E-RAW diagonal/triangle pattern survive proper float32
    decoding, and can it be mapped to hierarchy levels, triangle orientation,
    interpolation residuals, or error propagation rather than byte artifacts?

## Deliverable

Write findings to a separate research report, suggested path:

```text
docs/research/msts-terrain-adaptive-lod-analysis.md
```

Include:

- executable/module identity and SHA-256;
- tools and runtime mode used;
- fixture files and exact single-variable changes, without redistributing MSTS
  data;
- file-access observations;
- relevant relative virtual addresses and a small call graph;
- recovered structures and pseudocode;
- behavioral results in a table;
- decoded E visualizations and overlays used to distinguish geometric patterns
  from raw-byte display artifacts;
- answers to the questions above with confidence labels;
- a terrain-mechanics inventory covering all incidental findings, preferably
  with columns for mechanic, trigger/token/input, function RVA, evidence,
  confidence, and follow-up;
- unresolved ambiguities and the next most promising breakpoint/experiment.

A negative result is acceptable. If the selection routine is not recovered,
report the deepest confirmed data flow, discarded hypotheses, and reusable
breakpoints/landmarks so another analysis does not start from zero.

## Explicit non-goals

- Do not implement adaptive triangulation in TSRE as part of this analysis.
- Do not change the terrain-resolution task based on an unverified decompiler
  guess.
- Do not reverse-engineer unrelated simulation, copy protection, licensing, or
  content systems.
- Do not assume Open Rails or later Microsoft terrain engines reproduce MSTS.
- Do not require successful full decompilation for the task to be considered
  useful.
