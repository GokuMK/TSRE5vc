# Shape parser performance investigation — 2026-09-10

These are the **pre-optimization** measurements and design findings. The subsequent [implemented optimization and new results](sfile-optimization-results.md) replace the performance/state limitations described below while recording the remaining gap to legacy loading.

The current loading cost is unsuitable for the intended realtime Compact workflow. UTF-16 decoding is inexpensive; token processing, the generic retained document, and runtime extraction need optimization. The local Open Rails parser was successfully isolated and measured: its compressed-input times are close to the current new TSRE CPU loader on these three files, but its retained shape data is far smaller.

Complex animation differences remain observations. Legacy complex animation has not been validated in TSRE and is not a correctness oracle.

## Measurement boundaries

All measurements use the same Linux/WSL environment with warm filesystem data. C++ uses the CMake Release build. The new CPU stage harness has one warmup and seven samples, reporting medians; previous document destruction occurs outside timing. No GL context or texture loading is involved. Stages are independently measured and must not be added together: document reading already includes decoding and tokenization. Runtime extraction is excluded from document-only results.

The C# harness links the unchanged local `/root/openrails/Source` parser with the real MonoGame math types. It runs on .NET 8.0.30, SDK 8.0.424, with tiered compilation disabled. It measures `new ShapeFile(path, suppressWarnings)` including file access, decompression, typed shape construction and optional validation. Each case has an initial call, four warmups and eleven samples; forced GC is outside each timed interval. These are measurements of that source snapshot on Linux .NET 8, not timings inside the Windows game. The source directory has no Git metadata; hashes below identify it.

## Original compressed binary shapes: Open Rails comparison

The prior TSRE values are from the [three-run CD comparison](sfile-complex-cd193290-comparison.md); Open Rails values are medians of eleven constructor calls. The workloads differ: legacy includes GL geometry initialization, new CPU includes TSRE runtime extraction and `.sd` loading, and Open Rails constructs its source model without TSRE runtime/GL work. These are useful loading-cost comparisons, not identical-function benchmarks.

| Shape | Legacy load + GL | New full CPU load | Open Rails parse | Open Rails parse + validation |
| --- | ---: | ---: | ---: | ---: |
| `CD_193290.s` | 46.55 ms | 415.26 ms | 431.07 ms | 429.25 ms |
| `CD_193290_MS.s` | 27.16 ms | 281.67 ms | 273.23 ms | 275.43 ms |
| `CD_193290_FG.s` | 11.31 ms | 133.59 ms | 135.92 ms | 143.64 ms |

The small apparent improvement with validation on the main file is measurement variability, not an optimization. Parse-only ranges were 412.45–490.10, 266.99–282.15 and 133.70–138.36 ms respectively. All files completed both modes without parser warnings. Point/matrix/LOD/subobject counts were respectively 50110/27/1/34, 34595/27/1/4 and 10390/9/1/11.

| Shape | New document storage estimate | Open Rails retained managed delta | Open Rails managed allocation per parse |
| --- | ---: | ---: | ---: |
| Main | 355,976,888 B | 16,151,152 B | 36,286,016 B |
| MS | 239,644,060 B | 11,123,480 B | 23,785,856 B |
| FG | 120,208,880 B | 6,246,664 B | 13,682,808 B |

These are different counters, neither peak memory nor process RSS. The C++ estimate excludes allocator overhead; the C# deltas exclude native memory. Open Rails also does not promise this task's complete preservation of unknown/optional fields. Nevertheless, the storage difference and code representation support using dense typed data for known numeric fields.

## Compressed-stream control

Temporary uncompressed binary files contain the **identical decompressed payload bytes**, with only the outer envelope changed. The unchanged Open Rails parser takes 159.71, 98.78 and 50.86 ms respectively without validation (156.99, 99.92 and 51.73 ms with validation).

Open Rails `SBR.Open()` places `BinaryReader` directly over `DeflateStream` for compressed input and performs many small scalar reads. The control demonstrates a substantial cost associated with its compressed stream path. It does not separately measure raw inflate time and stream-call overhead, so attributing the entire difference to the compression algorithm would be unjustified. No Open Rails source was patched or optimized for these numbers.

## UTF-16 results

The existing original text asset `SD402/SD402.s` took **170.18 ms new CPU versus 17.98 ms legacy including GL**, about 9.5× in the stock comparison. Small wiper files are too small and their GL/setup overhead too prominent to infer a universal text-parser ratio: Acela's wiper was 2.43 ms new CPU versus 3.62 ms legacy including GL.

The following fresh stage measurements use original SD402/Acela text and temporary normalized UTF-16 exports of the three CD shapes. CD files on disk are originally compressed binary; the text results below are explicitly a conversion experiment. Export time is excluded.

| UTF-16 input | Decode bytes to QString | Tokenize only | Complete document read | Open Rails typed parse |
| --- | ---: | ---: | ---: | ---: |
| CD main, generated | 4.33 ms | 156.87 ms | 659.34 ms | 751.66 ms |
| CD MS, generated | 2.66 ms | 102.51 ms | 440.86 ms | 489.92 ms |
| CD FG, generated | 1.51 ms | 51.20 ms | 234.34 ms | 244.43 ms |
| Original SD402 | 0.593 ms | 25.86 ms | 113.63 ms | Not measured |
| Original Acela wiper | 0.0014 ms | 0.217 ms | 1.289 ms | Not measured |

Open Rails also completed validation on all three generated text shapes without warnings, with medians 742.15, 490.93 and 241.07 ms. Counts match the binary inputs; this does not prove equality of every source field.

Tokenization accounts for roughly 22–24% of document-read time on the large text inputs. It already exceeds the old complete load time on SD402, so storage changes alone cannot reach legacy performance. UTF-16 decoding itself is under 1% of document-read time.

A separate 100,000-number microbenchmark uses simple literals and verifies matching sums: `SimisTextReader::next()` + `number()` takes **19.15 ms**, while `ParserX::GetNumber()` takes **1.78 ms**, about 10.8×. This compares the APIs' scanning/conversion work, not full shape parsers; the bounded reader also provides token kinds and validation. The new API allocates token strings and uses locale conversion. Crucially, current runtime extraction calls `Node::number()` / `QString::toDouble()`; `SimisTextReader::number()` is used by editing/serialization. Optimizing that helper alone will not remove the observed full-load slowdown.

## Where the current design spends work

The new document-only binary medians are 223.21, 153.03 and 77.91 ms, excluding runtime extraction and GL. They are already several times the old full load. Full CPU times from the earlier application run are higher again; subtracting the two sets is not a precise profile because their allocation state and harnesses differ.

On this build `sizeof(Node)` is **168 bytes**, including three QString objects, a vector and two QByteArrays even for a single numeric scalar. Main has 359,251 block nodes and **1,728,622 scalar nodes**; MS has 225,993 / 1,180,842 and FG 132,661 / 570,687. A four-byte binary number therefore occupies an entire node before allocator/vector-capacity overhead. Making binary values native avoided some strings but left that representation intact.

Code review also identifies avoidable work, not yet individually profiled:

- Token strings are allocated even when callers only need a temporary span or numeric value.
- Text parsing performs string/schema lookups and constructs generic nodes, then annotates the tree and extracts a second runtime representation.
- Runtime `integer()` / `indices()` still stringify native binary integers and parse them back through QString.
- Compact currently constructs the complete document before freeing it after upload. Its low final memory use says nothing about load latency or peak memory.

## Recommended next implementation step

User clarification: Complete means editable and saveable without losing data; it does not require a generic object for each source scalar. A contiguous point array is sufficient to reconstruct the `points` block on save. The current representation is an implementation cost, not an inherent cost of full preservation.

**Optimize Complete's storage first, then remeasure before deciding whether separate implementations are warranted.** Use the same typed data model and shared format/rendering code for Complete and Compact wherever possible. Compact can omit unneeded records during reading and release source arrays after upload. No `SFileComplete` / `SFileCompact` public class split is selected or justified by the current benchmark alone.

The consumer selects the requested mode before loading. Compact must influence parsing and allocation from the start; materializing Complete data and discarding it afterward is too late. This is distinct from releasing temporary rendering arrays after successful GL initialization. The current implementation's pre-load option exists, but its full-document-first behavior still needs replacement.

1. Keep one reusable bounded SIMIS reader, but support allocation-free views for ordinary tokens and direct checked numeric reads. Retain owned strings only when required by decoded escapes or storage lifetime. Preserve bounds, diagnostics and numeric edge-case behavior.
2. Replace the heavyweight Complete tree with typed records and contiguous numeric arrays, with explicit optional presence. For example, serialize `points` from its count and an array of coordinates; do not retain a scalar node for each coordinate. Preserve unsupported extensions, exceptional values and recovered fragments separately with their necessary parent context. Normalized output is already accepted; original whitespace/token order does not require retaining a generic tree. Confirm complete semantic round trips before interpreting the new performance results.
3. Apply the consumer's pre-load Compact request throughout parsing: use the same typed arrays/runtime records, skip unneeded source fields with bounded traversal, and never materialize the full editing representation for a Compact request. Retain source indices until `initGL()` has successfully built triangle caches/buffers, then release them. Keep CPU-loaded/bounds state independent of GPU initialization, and require a complete reload before save. Do not require a second implementation merely to avoid the current generic tree.
4. Eliminate integer string round trips and duplicate conversion/extraction passes. Reuse decoding, schema/dispatch and rendering routines across both paths so format fixes do not need two independent implementations.
5. Repeat the same three files and large original UTF-16 SD402, recording CPU load, GL init, transient/retained memory, semantic round trips and rendering checks. Do not accept Compact on the strength of final retained-memory numbers or Open Rails parity alone.

At the time of this investigation, the storage/loading redesign was outstanding. It has since been implemented and measured in the optimization report linked above. Legacy remains the default.

## Interoperability fix and reproducibility

The first Open Rails text run rejected our generated header `JINX0st_…`. The writer used `%11` in a QString format, which Qt treats as placeholder eleven rather than placeholder one followed by literal `1`. Own-reader round trips had tolerated this invalid subheader. The writer now concatenates the file-kind character explicitly to produce the required `JINX0s1t______\r\n` subheader. A synthetic exact-header assertion was added, and the three regenerated UTF-16 files loaded in the unchanged Open Rails parser. No loading-performance optimization is claimed from this formatting fix.

Validation after the fix: Release application and benchmark builds succeeded; both CTest suites (`simis_tokens`, `shape_document`) passed; the application `shape-complex-gl` suite passed all 46 checks. The final C++ benchmark completed with matching numeric checksums, and all three Open Rails input encodings completed with and without validation.

Commands and timing boundaries are in [tests/shapes/README.md](../../../../tests/shapes/README.md) and the [Open Rails harness README](../../../../tests/shapes/openrails/README.md). Original files were untouched; temporary conversions remain under `/tmp/sfile-parser-inputs`. To reproduce the uncompressed control, for each original compressed file write `b'SIMISA@@@@@@@@@@' + zlib.decompress(original_bytes[16:])` into a private temporary file; compare the payload bytes before timing.

Local raw measurements: `/tmp/sfile-parser-binary.jsonl`, `/tmp/sfile-parser-text.jsonl`, `/tmp/sfile-openrails-results.jsonl`, `/tmp/sfile-openrails-uncompressed-results.jsonl`, `/tmp/sfile-openrails-text-results.jsonl`. Open Rails stderr logs beside these results are empty for the successful runs. Original shape hashes are in the CD comparison report.

Unchanged Open Rails source SHA-256:

| File under `/root/openrails/Source` | SHA-256 |
| --- | --- |
| `Orts.Formats.Msts/ShapeFile.cs` | `46ae52130a57568cb665bc777fdf169baa46af5245908686dffd92d295c10548` |
| `Orts.Parsers.Msts/SBR.cs` | `c85c511365736acac216dac3e759ba0747141c0bee915d8fee56c7f90452107c` |
| `Orts.Parsers.Msts/STFReader.cs` | `530186323b27428885081de3ee7117ed761d4987364ecd403197ea69c257057c` |
| `Orts.Parsers.Msts/TokenID.cs` | `c75a1fad6e30c3f59e466f1ae5b015c76845c5c6bbfd688c237ca6f40efe0755` |
