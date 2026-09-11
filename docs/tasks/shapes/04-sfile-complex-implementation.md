# Task 04 - New Shape File Implementation (`SFileComplex`)

Status: opt-in implementation milestone ready for visual and practical testing in `feature/sfile-complex`. SFileLegacy is now the default; SFileComplex remains opt-in and original SFile/C/X remains available as a fallback.
Updated: 2026-09-11.

## Current implementation and validation

SFileComplex provides Complete and pre-load Compact modes, separate CPU loading and GL initialization, and normalized saving from Complete storage. SFileLegacy joins the original loaders while separating loading from GL generation. Select the backend through `TSRE_MSTS_SHAPE_BACKEND`: `legacy` for SFileLegacy, `complex` for Complete, or `complex-compact` for Compact. Unset selects SFileLegacy; `old` selects original SFile/C/X. `TSRE_MSTS_FIRST_LOD_ONLY=1` applies only to the Complex backends; preferences are fixed when a ShapeLib is constructed.

- [Latest three-mode compatibility results](reports/sfile-three-mode-comparison.md): all 4,306 collection inputs now initialize GL in Compact and Complete after the two documented recovery fixes. Existing Legacy/new rendering and animation differences remain observations requiring practical review.
- [Label recovery and final parsing-time checks](reports/sfile-label-recovery.md): 16 spaced-label coaches recovered, all 203 UTF-16 files replayed on the final parser, 233 standalone checks and 108 CPU/GL checks passing. No material parsing-time regression observed.
- [Malformed-animation handling](reports/sfile-malformed-input-investigation.md): unusable animations can be discarded for static rendering; damaged Complete documents still refuse saving.
- [Current loading/storage measurements](reports/sfile-optimization-results.md) and [subsequent UTF-16 measurements](reports/sfile-utf16-optimization-results.md) supersede the initial slow-loader results. The label report measures the latest incremental parser change, not end-to-end application loading.
- [Preservation versus renderer coverage](reports/sfile-complex-format-coverage.md) records implemented source retention and runtime limitations.

The implementation history below retains measurements and check counts from each stage. Statements about the initial heavyweight tree, 9–12× loading cost, outstanding optimization, and earlier Broken shapes describe those historical builds, not the current implementation. Compare timings only within each report's stated workload and conditions.

## Remaining practical acceptance

- Visually compare Legacy, Compact and Complete in Shape Viewer and representative routes/trainsets, through direct and gathered rendering, including transparency, textures, LOD changes and picking.
- Exercise animated trains and named controls in actual use. Complex animation differences need independent visual validation; Legacy is not an established correctness reference for every animation.
- Exercise multiple instances, visibility controls, reload, seasonal textures, route switching and sustained rendering. Check responsiveness and resource behavior during normal use.
- Exercise Complete editing/save/reload workflows on private copies; verify Compact requires a full reload before saving.
- Record practical findings with SFileLegacy as the default before removing old classes or adopting SFileComplex by default. Persistent matrix reuse and the renderer ownership redesign remain separate follow-up work in [Task 05](05-sfile-legacy-load-gl.md) and the [renderer task](../renderer/02-renderer-core-generic-queue.md).


## Objective and accepted boundary

Implement `SFileComplex : public ComplexShape` with private storage. Keep `SFile`, `SFileC`, and `SFileX` in the project and working until the replacement is complete and proven. Default adoption and legacy deprecation are separate later steps.

The [SFile legacy findings](reports/sfile-legacy-findings.md) contain the full field, nested-type and caller audit. Application callers already use `ComplexShape` methods; the new implementation does not need public mutable parsing structures, arrays or GPU objects.

The requirements and ParserX review below preceded implementation. Current implementation and validation are recorded at the end of this task. Task 03 (non-MSTS metadata sidecars) is not a prerequisite; MSTS `.sd` remains supported.

## Loading, preservation and saving

The default load must retain the complete shape file structure, including data TSRE does not render. Parsing is not mesh conversion.

- Support text and binary shapes, including compressed/uncompressed envelopes and associated `.sd` metadata.
- Read and represent every known field, with correct scalar types, labels, semantically significant ordering, optional-field presence, repeated blocks, original indices and hierarchy. Do not reduce multi-entry UV/texture lists to their first entry or flatten primitives while parsing.
- Preserve unknown blocks and unrecognized trailing payloads with their owning block and enough relative placement information to avoid changing unknown semantics, using opaque source data. A switch `default` may decline to interpret a block, but must not discard it in Complete mode.
- Provide saving for all represented fields, including non-rendered fields, labels, optional values, nested structures and unknown data where the target encoding can represent it. A load/save/load must preserve content; GPU buffers are never the serialization source.
- Keep parsed source values separate from defaults, repairs, generated normals, coordinate conversion, winding changes and other renderer-derived values. Reading must not silently rewrite the source document.
- Full preservation is the default. Selective loading and explicit compaction are exceptions and must be visible in state/capability queries.
- Saving an edited document must serialize those edits; retaining original bytes alone is not an implementation of saving.

### Format coverage checklist

Build a field-by-field coverage ledger against the local grammar, stock examples, interoperability findings and exporter. Each entry needs read/write support, preservation behavior, render relevance and a fixture. The legacy class's subset is not the coverage specification.

| Family | Required coverage |
| --- | --- |
| Header and volumes | Header flags and optional fields; volume records |
| Global tables | Shader/filter names; points, UVs, normals, sort vectors, colours, named matrices, images and textures |
| Materials and lighting | Light materials, light model configurations and UV operations; complete vertex/primitive states, texture-index lists and flags |
| Geometry | Vertex attributes and every UV reference; vertex sets; primitive-state changes; triangle, line and point primitive records and their indices/normal indices/flags |
| Subobjects | Full headers, geometry info/nodes/maps, culling data, shader/light configuration lists, optional SubObjID |
| LOD | All LOD controls, their headers/bias/scale, distance levels, distances, hierarchies and subobjects |
| Animation | Animation headers, named nodes, controllers, all documented key types and parameters, including data not currently animated by TSRE |
| Named data and extensions | Named geometry/reference records, additional known extensions, unknown blocks and trailing fields |
| Metadata | Existing `.sd` fields, bounding boxes, texture alternatives and TSRE extensions; preserve unknown metadata where possible |

“Supported for loading/saving” does not mean “supported by the renderer.” Preserve unsupported render features and report their rendering limitations.

## CPU loading and GL initialization

`ComplexShape::load()` currently covers data loading and GL buffer initialization. Its interface will be split in the future. Keep the two stages separate internally now:

1. A CPU-only stage reads the source document, records diagnostics, resolves references and evaluates rendering prerequisites. It must be usable without an OpenGL context.
   Set `isLoaded()` after the CPU loading stage has finished and CPU bounds/size are available, before GL initialization. It reports data loading, not GPU readiness.
2. A separate `initGL()` (or `initBuffers()`) builds the triangle/render cache from original indices, applies rendering conversions, and creates/uploads GPU buffers. Keep its implementation separate from parsing and serialization.
3. For current compatibility, `load()` calls this initialization stage at its end when a context and sufficient valid data are available. Keep an internal CPU-only entry point for tests/tools and the future interface split.

Missing GL context or an upload failure does not make otherwise valid file data Broken. Record GL readiness/failure separately and permit retry. A Broken document remains owned and inspectable, and must never reach unsafe GL operations.

Complete mode retains original index arrays after upload. Compact mode may release them only after successful cache/buffer creation and only if the remaining data supports required runtime behavior. Failed initialization must not discard the source data needed for diagnosis/retry.

Explicitly account for GL context loss, buffer rebuilding, animation, bounds, inspection, LOD switching and reload before freeing arrays. If a compact asset needs source reload for reconstruction, record that dependency; do not assume a VAO/VBO alone is sufficient.

## State and preload preferences

Keep data retention, shape health and GL readiness distinct. Proposed internal representation (names can change):

| Dimension | Suggested values and meaning |
| --- | --- |
| Data retention | `Unloaded`, `Complete`, `Partial`, `Compact`; Complete retains the entire source structure, Partial intentionally omits requested portions, Compact retains the required runtime subset and temporary data needed for GL initialization |
| Shape health | `Valid`, `Recovered`, `Broken`; diagnostics distinguish tolerated issues from missing/invalid essential rendering data |
| GPU state | `NotInitialized`, `Ready`, `Failed`; a context-related failure is independent of source health |

This allows a retained Complete document to be Broken for rendering, or a Compact document to remain renderable. Provide concise status/diagnostic queries without exposing mutable storage. Accepted `isLoaded()` contract: report completion of CPU data loading independently of `initGL()`. It must remain true after successful CPU loading when GL initialization is deferred, has failed, or buffers are invalidated. It does not mean Complete retention, valid render data, or GPU readiness. A recovered/Broken document whose CPU loading has finished remains inspectable; its health is reported separately. Initialize bounds/size safely even when source bounds cannot be derived, and report that limitation rather than exposing uninitialized values. A file-open failure is not a completed data load. Rendering methods must check shape health and GPU readiness themselves; do not use a false GPU-ready flag to trigger destructive source reloads.

Caller check (2026-09-10): current application-level shape `isLoaded()` calls guard size, border/box and viewer framing queries (`WorldObj`, shape-based world objects and `ShapeViewerGLWidget`). `GltfShape` also uses it internally for bounds and inspection methods, so it is not literally used only for bounds throughout the repository. This task changes the new class's contract; existing legacy/glTF implementations remain unchanged.

Load preferences must be set before parsing, with full loading as default. Include a first-LOD-only option and record exactly which controls/levels were loaded, skipped, retained opaquely or discarded. Proposed initial meaning: first distance level of each LOD control; retain all shared tables needed by selected levels. If a different global-first interpretation is needed, make it explicit.

Accepted clarification: the consumer requests Complete or Compact **before loading starts**, independently of the resulting health and GPU state. The parser must apply that request while reading and allocating storage. For a Compact request, skip source-only fields and unknown payloads that are unnecessary for the runtime contract using bounded traversal; do not construct Complete storage and discard it afterward. Keep dependencies required by selected geometry, animations, bounds and other runtime queries. The request remains fixed for that load; entering a Complete/save workflow after data was omitted requires a full reload.

Selective loading must still navigate past skipped blocks safely and reach later metadata/animations. Avoid allocating decoded mesh arrays for skipped LODs. Compressed streams may still require decompression/scanning; do not promise random-access I/O savings.

An explicit later Complete-to-Compact operation may release an already loaded document's source-only data, but does not replace applying a Compact request during parsing. Releasing temporary geometry/index arrays after successful `initGL()` is a separate cleanup step. Keep the runtime subset needed for TSRE rendering, animation, inspection, bounds, snapping and reinitialization strategy. Report lost full-save capability as soon as required source data is omitted rather than claiming Complete status until upload.

## Realtime rendering and Compact performance

Realtime rendering is the primary use case. Complete mode enables load/save workflows; its preservation requirements must not impose ongoing parser/document overhead on Compact runtime assets.

- Compile renderer-ready draw data and lookup tables during `initGL()`. Steady-state rendering must not traverse a token tree, parse strings, expand original indices, or rebuild unchanged triangle caches.
- Share immutable mesh/material/animation data across instances; keep instance state small. Prefer contiguous arrays and stable indices for hot data. Resolve names outside the per-frame path where possible.
- For a Compact load request, avoid allocating source-only tables and retaining raw text/binary or unknown payloads unless needed for runtime processing. Release temporary rendering arrays only after successful buffer creation. Avoid constructing a full document and a duplicate runtime model solely for hypothetical saving.
- Avoid recurring heap allocations and unnecessary matrix/material work in render/gather/update paths. Cache static results and invalidate them on relevant changes; dynamic animation still updates correctly.
- Measure load/decompression/parsing, cache generation/GL upload, retained CPU/GPU memory, peak loading memory and steady-state update/render costs separately. First-LOD loading should avoid decoded storage and GL work for skipped levels.
- Compare Compact against legacy under the same build, assets, texture cache conditions, LOD, instance count, animation and rendering pipeline. Investigate measured regressions before adoption; document correctness/performance tradeoffs. Do not invent a percentage target before a baseline exists.

## Permissive parsing and Broken shapes

Use token dispatch (`switch/case` with a preservation fallback), bounded blocks and deferred reference validation. Accept optional omissions and extra blocks without rejecting the whole shape. Resolve references after parsing rather than requiring dependent tables to appear first. Exact input token/block order is not a general preservation requirement: saving may emit a normalized shape with a consistent section order. Preserve semantic sequence and associations, including indexed table positions, primitive-state changes followed by primitives, and animation key order. Do not sort/reindex those blindly. For unknown content, retain its owner and conservative relative placement where normalization cannot be shown to preserve meaning.

The terrain reader provides a useful dispatch example in [TFile.cpp](../../../src/tsre/world/TFile.cpp), around line 140: bounded children, a token switch and advancement to the child end. Its current default branch skips unknown data and its outer catch fails the load; those aspects do not satisfy this shape task's preservation/recovery requirements.

- Missing optional data: retain absence; use documented runtime defaults where appropriate.
- Unknown well-framed block: retain opaque content and continue with siblings.
- Invalid known child with a trustworthy end boundary: retain its source/error, contain the failure to that child and continue where safe.
- Missing essential mesh data, unusable indices or required hierarchy: retain the document and mark it Broken for rendering, with field/block diagnostics. Do not delete it or let the failure escape as an all-or-nothing shape rejection.
- Unbalanced/truncated text or invalid binary length: stop at the last trustworthy boundary, retain the recoverable document and opaque remainder, and report the structural failure. Do not guess arbitrary sibling boundaries or read outside the input.
- Distinguish missing optional fields from explicit zero/empty values. Validate counts against actual bounded records and allocation limits; never trust a declared count blindly.

Whether a Broken document can display a safe subset is a later explicit rendering policy. The initial safe behavior can be to skip its draw submission while still supporting inspection and diagnosis. Internal bounded-reader errors may be caught locally; the requirement concerns preserving the shape object and recoverable content, not forbidding all internal error mechanisms.

## ParserX review

Reviewed [ParserX.h](../../../src/tsre/fileFunctions/ParserX.h), [ParserX.cpp](../../../src/tsre/fileFunctions/ParserX.cpp), [FileBuffer.cpp](../../../src/tsre/fileFunctions/FileBuffer.cpp), [ReadFile.cpp](../../../src/tsre/fileFunctions/ReadFile.cpp), and their use in the existing shape/terrain readers.

### Recommendation

Use a new bounded structured-text reader, initially consumed by `SFileComplex`. Keep the reader in one reusable class, similar in scope to `ParserX`, under `src/tsre/fileFunctions/` (working name `SimisTextReader`). It must support future migration of other SIMIS text readers without depending on shape-specific structures, OpenGL, or `SFileComplex`. Nested token/result/scope types and private helpers are fine; avoid a multi-class parser framework. Shape token dispatch, field semantics and shape serialization belong in the shape implementation, outside this generic reader. Leave `ParserX` and its legacy callers unchanged. It is useful as a compatibility reference but is not an acceptable foundation as-is for complete, permissive, saveable shape documents.

Small improvements could fix individual numeric/string bugs or add bounds checks. They would not supply source spans, a preserved document structure, unknown-block round-tripping, explicit end/error results or reliable recovery. Adding those facilities changes the parser contract substantially; a new reader has a clearer boundary. A repository search found `ParserX::` use in 56 source files, so changing shared behavior requires a separate compatibility effort.

### Findings and consequences

| Area | Current behavior | Consequence |
| --- | --- | --- |
| Bounds | `FileBuffer::getShort()` calls `checkPayload()`, which checks bounds only when a `ScopedLimit` is active. Several ParserX string/number loops lack local EOF checks; ordinary text parsing does not automatically install a limit. `GetAlternativeTokenName()` also accesses `data[off - 2]` directly. | Truncated/malformed text can read out of bounds. A global scope helps but does not define child recovery. |
| Block boundaries | `SkipToken()` counts every parenthesis, including those inside quoted strings; it has an arbitrary iteration cap and returns the same result for different termination conditions. `NextTokenInside()` has no general quote-aware nested scanner. | Unknown blocks cannot reliably be skipped/preserved when strings contain parentheses. |
| Numbers | `GetNumber*()` uses manual float accumulation, unit conversion and addition. A positive exponent sign is treated incorrectly; decimal divisor arithmetic can overflow for long fractions. Unsigned/hex parsing lacks overflow diagnostics. | Values may change during loading; counts/flags must use exact integer readers, not float conversion. |
| Strings | `GetStringInside()` recognizes concatenation only when `+` immediately follows the closing quote. Helpers do not provide a uniform raw-versus-decoded string contract. | Whitespace-separated concatenation is not handled as intended; saving needs explicit escaping and lexeme preservation. |
| Cursor/error contract | Empty string can represent end/missing content; methods rewind the shared byte offset; some numeric routines scan across delimiters. | Missing fields can desynchronize subsequent reads; errors need location and block context. |
| Preservation | Reads produce scalar values and advance the cursor; skipped blocks, spelling, labels/optional presence and source spans are not captured as a document. | Cannot meet complete load/save requirements by simply using the current getters. |
| Encoding | ParserX consumes little-endian 16-bit units. `toUtf16()` zero-extends individual non-BOM bytes; it is not a UTF-8 decoder. | Use explicit envelope/encoding handling; preserve Unicode and distinguish byte encoding from structured-text parsing. |
| Writer helpers | `AddComIfReq()` only covers limited quoting conditions and is not a complete escaping/round-trip writer. | Introduce a paired serializer with tested string and number rules. |

### Executed probes

On 2026-09-10, compiled the current, unmodified `ParserX.cpp` and `FileBuffer.cpp` into a temporary Linux C++/Qt probe outside the repository. Every probe installed `FileBuffer::ScopedLimit` to avoid deliberately exercising an unchecked out-of-bounds read. Build used C++17, Qt5Widgets and function-section garbage collection; no TSRE source/test changes were made.

| Input / call | Observed result |
| --- | --- |
| `1e+3 ) ` / `GetNumberInside` | `4`, rather than `1000` |
| `"a" + "b" ) ` / `GetStringInside` | `a`, leaving the concatenation unread |
| `"a)b" ) sibling ( 1 ) ` / `SkipToken` then `NextTokenInside` (starting inside a block) | Next token reported as `b`, rather than `sibling` |
| `-1 ) ` / `GetUInt` | `0`, cursor still at offset 0; no invalid-unsigned diagnostic |
| Unterminated quoted string / `GetStringInside` | Throws at the active scope end; safety here depends on the added scope |

These are focused reproductions, not an exhaustive fuzzing or encoding test. Unscoped malformed-input safety findings above come from source inspection.

### New reader requirements

- Explicit input/end bounds and progress guarantees; structured token kinds, source spans and line/column diagnostics.
- Quote/escape-aware parentheses; token names and optional labels; access to raw spans when preservation requires them, without duplicating every lexeme alongside decoded data; whitespace around concatenation supported.
- Signed/unsigned/hex integers with width/range checks; locale-independent floating-point conversion including exponent signs. Preserve unsupported numeric/string forms opaquely rather than silently changing them.
- Text BOM/encoding handling tested independently; use the reference corpus to establish accepted non-Unicode encodings. Preserve original bytes when encoding cannot be identified safely.
- Provide bounded block traversal and source spans so consumers can preserve optional presence, repeated/unknown blocks, opaque tails and meaningful order. Do not require a full generic document tree for every consumer. Shape storage and normalized output order are the responsibility of the shape implementation. Normalize names for dispatch; preserve unknown spelling where needed.
- Bounded nested recovery and explicit EOF/error status. Pair the reader with tested shape serialization and reuse generic lexical formatting helpers if needed; this does not require a separate generic writer framework. Keep the parser independent of OpenGL.
- Reuse existing native token IDs and bounded binary-reader utilities where suitable; check envelope/decompression failures explicitly. A new text reader alone does not solve binary preservation.

## Knowledge resources and findings

### Local MSTS interoperability workspace

Reviewed `/root/msts/README.md` and the preservation/interoperability guidance in `/root/msts/MSTS_REVERSE_ENGINEERING_GUIDE.md`. Microsoft media, executables and stock assets remain private under `proprietary/`; do not copy them into TSRE fixtures. Publish independently written findings and self-created fixtures. The user's authorization includes MSTS executable/app investigation within that project's rules; no executable was run for this review.

Primary local grammar: `/root/msts/proprietary/msts_app/UTILS/FFEDIT/newshape.bnf` (UTF-16), SHA-256 `111e1bba2cf3ac57d14fdf19a0c5da89102008d2cd2cdeaf62868d34688c0e4f`.

The grammar includes substantially more than the legacy rendering subset: UV operations and lighting, line/point primitives, vertex sets, complete geometry/subobject headers, optional SubObjID, multiple LOD controls, animation controllers and named geometry data. Use it to seed the coverage ledger, then verify ambiguous or inconsistent definitions against examples and behavior rather than treating it as infallible.

Stock corpus location: `/root/msts/proprietary/msts_app`. Header-only checks of `TEMPLATE/SHAPES/watercolumn.s`, `jp2signal9.s`, and `jp1perredsp.s` confirmed compressed and UTF-16 text examples are available. These files were not fully parsed or rendered during this review. Additional reference material includes `proprietary/techdocs-original/How to write shape data files.doc` and `Creation of a Simpler shape.doc`; their contents were not reviewed in this pass.

### Blender exporter

Reviewed [export_msts.py at revision 0e651aaf14a021bd15d5d0772c867f449fb6be68](https://github.com/pwillard/Blender_MSTS_ORTS_Exporter/blob/0e651aaf14a021bd15d5d0772c867f449fb6be68/io_export_mstsexporter/export_msts.py). Its `STFWriter` emits UTF-16 text. The writer supplies useful examples of labeled primitive states, nested texture-index lists, vertex UV/color fields, vertex sets, subobject geometry/header records, and ordered top-level tables. Use these to design synthetic round-trip fixtures; it is an exporter subset, not an exhaustive shape specification. Blender was not executed.

## Accepted save policy and remaining implementation defaults

User clarification (2026-09-10): Compact is intended for workflows without shape saving, such as the route editor and gameplay. Most shape consumers do not need saving. Complete mode is the mode designed for saving.

If a Compact shape enters a workflow requiring save, a full reload into Complete mode is required first. Apply the same requirement to a Partial load that discarded source fields/LODs. Do not retain otherwise unnecessary opaque data solely to make Compact saveable, and do not add reduced export as a workaround in this task.

The save workflow must ensure Complete retention before edits that need serialization. If edits already exist, a full reload must not silently discard them: preserve/reapply supported edits or return an explicit unresolved-edit result. If the source cannot be reloaded completely, saving cannot proceed. Complete retention is a save prerequisite, not a guarantee that a Broken document is valid.

Other proposed implementation defaults, to make the task concrete:

- Normalized saving is acceptable: use consistent token spelling, formatting and section order for known data while preserving values, optional presence, hierarchy and meaningful sequence. Exact input token order, whitespace and compression output are not requirements. Preserve same-encoding unknown payloads and their necessary context; retain raw spans only where useful for preservation/recovery, not for every unchanged known field.
- Target text and binary saving for known fields, with compressed/uncompressed envelope support. Unknown binary-to-text or text-to-binary conversion cannot generally be lossless without a schema; report an unsupported conversion instead of dropping data. Verify the supported encoding matrix with fixtures.
- Broken documents remain inspectable and retain recovered source. Saving recovered data must report incompleteness and must not masquerade as a successfully repaired, valid shape.

## Initial old/new comparison corpus

After implementation, compare `SFileComplex` with legacy `SFile` using original shapes recursively under `/root/msts/proprietary/msts_app/TRAINS/TRAINSET`. This is the user's initial real-asset comparison scope; more complex asset/scenario testing comes later. The directory was verified on 2026-09-10. The initial comparison is now implemented; see the comparison report linked below.

- Enumerate all `.s` files case-insensitively, with adjacent `.sd` and appropriate texture roots. Record relative paths, hashes and load options in a local manifest; account for every file as success, failure or an explicitly explained skip.
- Compare CPU-loaded bounds, LOD/hierarchy/part counts, materials/textures and animations where present. Separately record differences due to legacy data loss or bugs; legacy output is not the sole correctness oracle.
- Render representative locomotives, passenger vehicles and freight stock through both implementations using matching cameras, LODs, animation times, textures, lighting and renderer settings. Compare picking and both legacy/gather paths. Exercise repeated instances of the same shape.
- Record repeatable timing and memory results for legacy, new Complete and new Compact, plus first-LOD loading. Separate cold/warm texture cache conditions, warm-up and timed runs; report sample counts and variability. Measure CPU submission and GPU time separately where tooling permits, and identify software rendering if used.
- Check Complete save/reload on temporary outputs without modifying the original corpus. Compare semantic data before/after normalization, and verify equivalent rendering on representative outputs. Compact save workflows must reload completely first.
- Keep proprietary originals private; commit only independently written findings, aggregate comparison results and synthetic tests. Add a comparison report beside this task with commands, environment, coverage, differences and unresolved issues.

Synthetic parser, malformed-input and round-trip tests remain part of implementation correctness. This initial stock comparison is not a claim of comprehensive compatibility or automatic authorization to deprecate legacy classes; additional complex testing is deferred to a later phase.

## Implementation stages and acceptance

1. Implement the single-class reusable bounded SIMIS text reader and shape serializer boundary, establish the field coverage ledger and synthetic parser/round-trip fixtures.
2. Implement complete CPU load/save and `.sd` handling, permissive recovery, diagnostics, preload options and retention states. Keep GL-independent tests possible.
3. Implement `initGL()` with derived geometry/cache ownership, compaction rules, retry/context-loss behavior and the complete required `ComplexShape` runtime contract.
4. Compare old/new implementations on the initial TRAINSET corpus above through explicit development/test selection. Keep the legacy factory default and legacy tests working; ensure backend/load preferences do not collide in the asset cache.
5. Collect reviewed proof before default adoption or deprecation.

Acceptance must cover:

- A consumer's pre-load Compact request controls parsing and allocation before GL initialization: unnecessary source records are never materialized, omitted data makes full saving unavailable immediately, and required source indices survive until successful upload. Verify this independently of final retained-memory counters and compare loading peaks against Complete mode.

- All known format families read/save/read with preserved values, types, labels, optional presence, indices and nested structure, allowing normalized token/section order; unknown blocks survive same-encoding round-trip, including after editing a known sibling.
- Full default loading, first-LOD selection, explicit compaction and full-save capability are tested separately. Compact/Partial-to-save requires a successful complete reload; a missing source or failed reload must not produce a partial replacement file. `isLoaded()` becomes true after CPU load and before initialization, including without a GL context, and stays true through GL failure/invalidation; bounds queries work at that point. Complete indices survive initialization; failed initialization does not erase source data.
- Missing optional blocks, extra/unknown blocks, unusual ordering, malformed bounded children, invalid counts/indices, quoted parentheses, escaped strings, Unicode, numeric edge cases and truncation preserve recoverable data without hangs/out-of-bounds access.
- Broken shape content survives for inspection; GL readiness and diagnostics remain distinct from source retention/health.
- Text/binary and compressed/uncompressed fixtures; representative stock assets remain local. Record unsupported extensions and encoding conversions explicitly.
- Legacy/gather rendering and picking, hierarchy/texture/content inspection, multiple LODs, animation, per-instance controls, bounds, snapping, seasonal textures, reload and cache invalidation retain expected behavior. Document intentional corrections to legacy behavior.
- Resource ownership survives partial failure, compaction, reload and destruction. Compare load/render time and memory against legacy on named local fixtures.
- Existing legacy checks remain passing; record commands, fixture coverage, visual evidence and limitations. A successful build or one rendered shape is not proof of completion.

## Implementation history — initial implementation (2026-09-10)

Worktree: `/root/TSRE5vc-sfile-complex`, branch `feature/sfile-complex`, based on the committed documentation at `9c6cf1b`. Main is kept separate.

- `SFileComplex` implements `ComplexShape` directly through private owned data. `loadData()` is CPU-only, `initGL()` builds packed geometry and buffers, and compatibility `load()` invokes both when a context exists.
- `SimisTextReader` is a single reusable class for bounded SIMIS token/string/numeric reading. ParserX and SFile/C/X remain unchanged.
- `SFileDocument` owns typed source records, labels, repeated blocks, extra roots and opaque binary data. See the [format coverage ledger](reports/sfile-complex-format-coverage.md).
- Retention, source health and GPU readiness are separate. Compact/Partial saves require full reload. Unsaved edits prevent compaction and `reloadComplete()`; explicit legacy-compatible `reload()` discards edits. A failed Compact buffer rebuild leaves the previous CPU bounds/state inspectable and can be retried when the source becomes available.
- `field()` / `setField()` access existing scalar values without exposing mutable containers (`points/point[0]`, or `sd/esd_detail_level`). Edits invalidate derived GPU caches. `save()` writes the shape; `saveMetadata()` writes `.sd` separately. Saving exports a snapshot; it does not retarget the asset's source path or clear edit protection. An explicit `reload()` discards the in-memory edits when that is intended.
- Runtime data retains matrices, material references, animation keys, LOD/visibility information, bounds and per-instance state after compaction. Complete source records remain available for saving. A pre-load Compact request now skips unused records during parsing and releases its temporary document after CPU extraction; required geometry/index arrays survive until successful GL initialization.
- The development factory originally exposed `TSRE_MSTS_SHAPE_BACKEND=complex` or `complex-compact`; it now also accepts `legacy` for SFileLegacy. The subsequent default switch selects SFileLegacy when unset; `old` explicitly selects original SFile/C/X. `TSRE_MSTS_FIRST_LOD_ONLY=1` applies to the new backend. Preferences are captured per ShapeLib so a library cache does not mix backends/options. Existing renderer packets gained an explicit point primitive sentinel because zero already means default triangles.
- Tests cover CPU load/GL independence, full/partial/compact retention, malformed data, serialization, runtime rebuild and stock comparisons. The [comparison report](reports/sfile-complex-comparison.md) records commands, measured results and remaining limits.

This initial implementation does not authorize default adoption or legacy deprecation. Complex-route stress tests and broader rendering features remain a later validation phase, as requested.

## Larger-file follow-up comparison

The requested three files in `TRAINS/CD_193_290` were compared in three runs each. See the [CD_193_290 report](reports/sfile-complex-cd193290-comparison.md): total new load times are 9.07–11.90× legacy; static rendering, picking and save/reload agree; main/FG animation behavior differs; neither implementation is a validated correctness reference. No implementation changes were made during this comparison.

## Parser performance follow-up

The [UTF-16 and Open Rails investigation](reports/sfile-parser-performance.md) isolates decoding, tokenization and retained-document construction. Large original UTF-16 SD402 is also about 9.5× slower through the current full loader. UTF-16 decoding is cheap; generic storage, token handling and runtime conversion need changes. The unchanged local Open Rails C# parser was built independently and measured on all three requested compressed shapes, identical uncompressed payloads and normalized UTF-16 exports.

User clarification: Complete only needs editing and saving without data loss. For example, a point array can reconstruct its source block; a separate object for each coordinate is unnecessary. Recommended next work is to optimize Complete itself into dense typed arrays/records, preserve optional and unsupported data with its necessary context, and remeasure. Compact should reuse that model while skipping/releasing unneeded records. The current heavyweight tree does not justify two separate implementations; no public class split is selected. Keep `isLoaded()` after CPU loading, indices until successful GL initialization, and full reload before Compact enters a saving workflow. Substantial loading/peak-memory improvement remains outstanding; Open Rails parity does not satisfy the realtime target.

The external-parser check found and fixed a malformed text-export subheader, with an exact-header regression check. All three corrected UTF-16 exports load in Open Rails with validation and no warnings. Animation differences remain unclassified observations until independently validated.

## Implemented storage/loading optimization

The [optimization results](reports/sfile-optimization-results.md) record the completed pass and repeated measurements. Complete uses contiguous native numeric arrays and lightweight block records, with separate storage for labels, exceptional values and unknown payloads. The text reader avoids ordinary-token allocation and converts numeric fields once. Compact applies the consumer's requested mode during parsing; bounded capacity estimates exclude ignored top-level blocks. There is still one `SFileComplex` implementation.

Complete CPU loading on CD main/MS/FG decreased from 415/282/134 ms to 92/58/29 ms; requested Compact measures 80/41/20 ms. Original UTF-16 SD402 decreased from 170 to 53 ms. Complete document estimates are roughly 15× smaller on the CD shapes. Legacy remains faster in these load comparisons; default adoption is not implied.

Both CTest suites and all 51 application shape/GL checks pass. ASan/UBSan runs pass 2,112 stock-inclusive checks and 238 CD-inclusive checks, including pre-load Compact and exact original binary payload preservation. All 129 stock shapes pass the new Complete/Compact render and picking comparison; the three CD shapes pass repeated checks. Source indices, CPU-loaded state, reload-before-save and failed-rebuild behavior retain their accepted contracts. Legacy classes and the default backend are unchanged.

## Compact loading follow-up

CPU stage profiling identified document reading as the main Compact cost, followed by extraction; document destruction was negligible for the binary CD shapes. Compact now reads regular point, normal, UV and vertex tables into native arrays without per-row block records. Binary parsing bounds every row and nested UV payload. Text parsing uses an independent reader cursor; a noncanonical or invalid row restarts the general parser for that table. Counts never control an unbounded allocation. Runtime coordinate/reference validation, first-LOD selection and nesting limits remain enforced.

On CD main, the temporary document falls from 20.99 MiB to 4.71 MiB and materialized records from 358,874 to 644. Complete retains its editable source structure; Compact still retains geometry and indices until GL upload and requires Complete reload before saving. Remaining temporary numeric tables are copied into runtime arrays during extraction, so this is not a zero-copy loader or a measurement of peak process memory.

Both CTest suites and 56 application shape/GL checks pass. ASan/UBSan with leak detection passes 2,121 stock-inclusive and 247 CD-inclusive checks. All 129 stock shapes pass Complete/Compact rendering and picking comparisons. The final depth-limit guard and stage-counter adjustments are covered by the focused checks, full sanitizer corpus and repeated CD/text comparisons. Current timing methodology and results are in the [Compact follow-up section](reports/sfile-optimization-results.md#compact-loading-follow-up).

The authorized timing rerun completed in three alternating pairs per asset. Generic→packed Compact CPU medians are 77.47→42.06 ms (CD main), 41.50→22.70 ms (MS), 20.45→8.88 ms (FG), and 52.12→41.25 ms (original UTF-16 SD402). All 30 asset processes pass. Windows still showed 13–26% baseline activity before/after testing and some timing outliers remain; the report includes all sample ranges and qualifies these as current-condition measurements, not fully idle-host validation. The deterministic temporary-storage reductions are unaffected by that timing qualification.

## UTF-16 loading optimization

The [UTF-16 results](reports/sfile-utf16-optimization-results.md) record the completed follow-up. Profiling identified lookahead and token/storage overhead. The reusable `SimisTextReader` now has fixed lookahead storage, kind-only lookahead, and consuming checked numeric reads over UTF-16 spans. Compact table-name comparisons avoid temporary QString conversions. Regular integer arrays append directly into native storage in Complete and Compact modes, with cursor/value rollback to the permissive parser for irregular content. Numeric syntax/range checks, Unicode decoding, source preservation and the single-class reader boundary remain intact.

On original SD402, paired application Compact CPU loading decreases from 39.87 to 28.24 ms and Complete from 67.49 to 57.75 ms. Compact CPU + GL is 29.25 ms against the same-run SFileX 16.22 ms. Document-only Compact parsing decreases from 26.99 to 15.81 ms. Large generated CD text files also improve; the report separates those document-only results from application timing and includes the tiny wiper's non-improving Compact sample. Host activity remained 10–23%, so timings are qualified rather than described as fully idle-host validation.

Both CTest suites, 56 application shape/GL checks, all 129 stock render/picking comparisons, and 12 paired application runs pass. ASan/UBSan passes 2,131 stock-inclusive checks and 251 checks over the large generated UTF-16 shapes. Old/new Complete exports are byte-identical for SD402 and all three CD text files. No legacy parser/backend or animation behavior was changed, and default adoption remains separate.
