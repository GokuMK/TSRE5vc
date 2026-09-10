# Task 04 - New Shape File Implementation (`SFileComplex`)

Status: class boundary accepted; ParserX review complete; implementation not started.
Updated: 2026-09-10.

## Objective and accepted boundary

Implement `SFileComplex : public ComplexShape` with private storage. Keep `SFile`, `SFileC`, and `SFileX` in the project and working until the replacement is complete and proven. Default adoption and legacy deprecation are separate later steps.

The [SFile legacy findings](sfile-legacy-findings.md) contain the full field, nested-type and caller audit. Application callers already use `ComplexShape` methods; the new implementation does not need public mutable parsing structures, arrays or GPU objects.

This update records the agreed requirements and the requested parser review. It makes no source changes. Task 03 (non-MSTS metadata sidecars) is not a prerequisite; MSTS `.sd` remains supported.

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
| Data retention | `Unloaded`, `Complete`, `Partial`, `Compact`; Complete retains the entire source structure, Partial intentionally omits requested portions, Compact has released source-only data |
| Shape health | `Valid`, `Recovered`, `Broken`; diagnostics distinguish tolerated issues from missing/invalid essential rendering data |
| GPU state | `NotInitialized`, `Ready`, `Failed`; a context-related failure is independent of source health |

This allows a retained Complete document to be Broken for rendering, or a Compact document to remain renderable. Provide concise status/diagnostic queries without exposing mutable storage. Accepted `isLoaded()` contract: report completion of CPU data loading independently of `initGL()`. It must remain true after successful CPU loading when GL initialization is deferred, has failed, or buffers are invalidated. It does not mean Complete retention, valid render data, or GPU readiness. A recovered/Broken document whose CPU loading has finished remains inspectable; its health is reported separately. Initialize bounds/size safely even when source bounds cannot be derived, and report that limitation rather than exposing uninitialized values. A file-open failure is not a completed data load. Rendering methods must check shape health and GPU readiness themselves; do not use a false GPU-ready flag to trigger destructive source reloads.

Caller check (2026-09-10): current application-level shape `isLoaded()` calls guard size, border/box and viewer framing queries (`WorldObj`, shape-based world objects and `ShapeViewerGLWidget`). `GltfShape` also uses it internally for bounds and inspection methods, so it is not literally used only for bounds throughout the repository. This task changes the new class's contract; existing legacy/glTF implementations remain unchanged.

Load preferences must be set before parsing, with full loading as default. Include a first-LOD-only option and record exactly which controls/levels were loaded, skipped, retained opaquely or discarded. Proposed initial meaning: first distance level of each LOD control; retain all shared tables needed by selected levels. If a different global-first interpretation is needed, make it explicit.

Selective loading must still navigate past skipped blocks safely and reach later metadata/animations. Avoid allocating decoded mesh arrays for skipped LODs. Compressed streams may still require decompression/scanning; do not promise random-access I/O savings.

Compaction is an explicit option/operation with a documented retention policy and state transition. Keep the runtime subset needed for TSRE rendering, animation, inspection, bounds, snapping and reinitialization strategy. Report lost full-save capability rather than silently claiming Complete status.

## Realtime rendering and Compact performance

Realtime rendering is the primary use case. Complete mode enables load/save workflows; its preservation requirements must not impose ongoing parser/document overhead on Compact runtime assets.

- Compile renderer-ready draw data and lookup tables during `initGL()`. Steady-state rendering must not traverse a token tree, parse strings, expand original indices, or rebuild unchanged triangle caches.
- Share immutable mesh/material/animation data across instances; keep instance state small. Prefer contiguous arrays and stable indices for hot data. Resolve names outside the per-frame path where possible.
- Release source-only tables, raw text/binary storage and unknown payloads in Compact mode when no longer required. Avoid retaining both a full document and a duplicate runtime model solely for hypothetical saving.
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

After implementation, compare `SFileComplex` with legacy `SFile` using original shapes recursively under `/root/msts/proprietary/msts_app/TRAINS/TRAINSET`. This is the user's initial real-asset comparison scope; more complex asset/scenario testing comes later. The directory was verified on 2026-09-10. No old/new comparison has been run yet.

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

- All known format families read/save/read with preserved values, types, labels, optional presence, indices and nested structure, allowing normalized token/section order; unknown blocks survive same-encoding round-trip, including after editing a known sibling.
- Full default loading, first-LOD selection, explicit compaction and full-save capability are tested separately. Compact/Partial-to-save requires a successful complete reload; a missing source or failed reload must not produce a partial replacement file. `isLoaded()` becomes true after CPU load and before initialization, including without a GL context, and stays true through GL failure/invalidation; bounds queries work at that point. Complete indices survive initialization; failed initialization does not erase source data.
- Missing optional blocks, extra/unknown blocks, unusual ordering, malformed bounded children, invalid counts/indices, quoted parentheses, escaped strings, Unicode, numeric edge cases and truncation preserve recoverable data without hangs/out-of-bounds access.
- Broken shape content survives for inspection; GL readiness and diagnostics remain distinct from source retention/health.
- Text/binary and compressed/uncompressed fixtures; representative stock assets remain local. Record unsupported extensions and encoding conversions explicitly.
- Legacy/gather rendering and picking, hierarchy/texture/content inspection, multiple LODs, animation, per-instance controls, bounds, snapping, seasonal textures, reload and cache invalidation retain expected behavior. Document intentional corrections to legacy behavior.
- Resource ownership survives partial failure, compaction, reload and destruction. Compare load/render time and memory against legacy on named local fixtures.
- Existing legacy checks remain passing; record commands, fixture coverage, visual evidence and limitations. A successful build or one rendered shape is not proof of completion.

Current validation: source/reference inspection and the five temporary ParserX probes above. No new class, parser replacement, factory switch or legacy code change has been made.
