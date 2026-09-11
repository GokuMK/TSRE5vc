# SFileComplex storage and loading optimization — 2026-09-10

This implements the optimization following the [parser investigation](sfile-parser-performance.md). Complete and Compact continue to use one `SFileComplex` implementation. Complete no longer stores a heavyweight object per numeric scalar. Compact's requested mode controls parsing and allocation before CPU loading completes.

The later [UTF-16 optimization results](sfile-utf16-optimization-results.md) supersede this report's text-loading timings. Binary/Compact storage measurements and the earlier milestones remain recorded here.

## Compact loading follow-up

Compact now packs regular point, normal, UV and vertex tables into native numeric arrays without per-row block records. Complete keeps its editable source structure. Unusual layouts use the general permissive parser; coordinate/reference checks, nesting limits and pre-load first-LOD selection still apply. Geometry and indices remain available until successful GL initialization.

| File | Previous temporary Compact document | Packed temporary Compact document | Previous materialized records | Packed materialized records |
| --- | ---: | ---: | ---: | ---: |
| `CD_193290.s` | 20.99 MiB | 4.71 MiB | 358,874 | 644 |
| `CD_193290_MS.s` | 13.97 MiB | 3.30 MiB | 225,764 | 338 |
| `CD_193290_FG.s` | 7.19 MiB | 1.30 MiB | 132,539 | 233 |
| `SD402.s` | 3.88 MiB | 1.05 MiB | 59,968 | 1,090 |

These are deterministic storage estimates including vector capacities, not peak process memory. Packed table words remain included in scalar counts. The remaining temporary numeric tables are copied into runtime arrays during extraction; those arrays survive until upload. GL buffer payload sizes agree exactly between legacy, Complete and Compact for the measured shapes.

The user-authorized rerun completed with three paired Release processes per file and backend; execution order alternated generic/packed, packed/generic, generic/packed. No builds, sanitizer runs or unrelated tests ran concurrently. The generic comparison executable uses the current harness/runtime with packed-table parsing disabled and the previous bounded Compact arena preallocation restored in a temporary copy of `SFileDocument.cpp`. This isolates the table-storage change without adding a production mode or modifying the normal executable.

**Host-idle qualification:** Windows CPU interval samples were 13.24–22.43% before the rerun and 17.93–26.09% afterward, measured with `GetSystemTimes` over six five-second intervals while our benchmarks were stopped. The rerun proceeded on the user's go-ahead, but it is not a verified fully idle-host benchmark. Medians below include every sample, including outliers; no slow sample was discarded. Earlier contended pilot/aborted paired runs are excluded. The initial-pass timing tables later in this report remain historical.

| File | Generic Compact CPU median | Packed Compact CPU median | Median speedup | Generic CPU range | Packed CPU range |
| --- | ---: | ---: | ---: | ---: | ---: |
| `CD_193290.s` | 77.47 ms | 42.06 ms | 1.84× | 77.42–224.99 ms | 41.95–45.88 ms |
| `CD_193290_MS.s` | 41.50 ms | 22.70 ms | 1.83× | 39.98–47.57 ms | 22.62–25.55 ms |
| `CD_193290_FG.s` | 20.45 ms | 8.88 ms | 2.30× | 20.26–23.09 ms | 8.10–22.46 ms |
| `SD402.s` | 52.12 ms | 41.25 ms | 1.26× | 51.96–58.92 ms | 36.58–45.05 ms |

Legacy combines loading and upload; no CPU-only legacy timer is available. The following table compares CPU + GL totals, using medians of each process's summed times. Legacy values come from the packed executable's same-run legacy instance. GL uses llvmpipe software rendering and excludes texture loading; these are not hardware GPU timings.

| File | Legacy backend | Legacy CPU + GL | Generic Compact CPU + GL | Packed Compact CPU + GL |
| --- | --- | ---: | ---: | ---: |
| `CD_193290.s` | `SFileC` | 42.11 ms | 99.72 ms | 64.19 ms |
| `CD_193290_MS.s` | `SFileC` | 36.22 ms | 62.27 ms | 39.15 ms |
| `CD_193290_FG.s` | `SFileC` | 18.49 ms | 23.78 ms | 11.94 ms |
| `SD402.s` | `SFileX` | 16.30 ms | 53.49 ms | 42.05 ms |

The stage profile attributes most of the improvement to reading and extraction. On CD main, median document reading (including file I/O and decompression) falls from 66.12 to 38.56 ms, and extraction/validation from 11.51 to 3.63 ms. Metadata and cleanup are below 0.05 ms each. SD402 reading falls from 47.27 to 40.31 ms; temporary-document cleanup falls from 3.08 to 0.006 ms. Independently calculated stage medians need not sum to the median total.

Reading remains the main cost. Compact still creates temporary numeric tables and copies them into upload arrays; it is not a zero-copy implementation. These results support the memory reduction and lower median loading cost under the recorded conditions, but do not establish universal legacy parity or a fully idle-system speedup. Animation and per-frame rendering algorithms were not changed by this pass.

All 30 rerun asset processes pass loading, save/reload, rendering and requested-Compact comparison checks (three CD shapes plus original SD402 and Acela wiper, three runs with each executable). Legacy, Complete and Compact GL payload byte totals agree on every run.

Rerun evidence: `/tmp/sfile-compact-rerun-{generic,packed}-{cd,text}-{1,2,3}/results.jsonl`; summary `/tmp/sfile-compact-rerun-summary.json`; host measurements `/tmp/sfile-compact-authorized-rerun-environment.jsonl` and `/tmp/sfile-compact-rerun-host-after.json`. The temporary baseline source and build recipe are `/tmp/SFileDocument-compact-baseline.cpp` and `/tmp/build-sfile-compact-baseline.py`. Normal and comparison executables are `build/TSRE5vc` and `build/TSRE5vc-compact-generic-baseline`. Run the existing `compare.py --gl` command for each, alternating order and keeping other work stopped.


Functional verification so far: both CTest suites, 56 application shape/GL checks, 2,121 stock-inclusive and 247 CD-inclusive ASan/UBSan checks with leak detection, and Complete/Compact render/picking checks on all 129 stock shapes. Synthetic cases cover identical binary/text packed values, irregular-table fallback, huge declared counts, vertex labels/multiple or absent UVs, independent document copies, nesting limits and Broken geometry before GL. Legacy loaders, Complete serialization and the default factory remain unchanged.

## Initial optimization changes

- Native numeric words live in contiguous arrays with one-byte type tags. A 32-byte block record stores hierarchy, field ranges, optional presence and scalar/child ordering. The old representation used a 168-byte node even for a single coordinate/index.
- Labels, exceptional text values, unknown text blocks and opaque binary payloads have separate storage. Complete preserves all known fields and same-encoding unknown data. Known text numbers normalize to native 32-bit shape values; binary float bits and the original scalar/child sequence survive saving.
- Binary numeric leaves and index lists copy directly into native storage after checking their bounded payload. Runtime integer extraction no longer formats numbers as strings or converts them through floating point.
- The reusable text reader borrows ordinary token spans, owns decoded quoted strings, and uses checked locale-independent numeric conversion. Block skipping respects quotes/escapes and nesting without building tokens for skipped content.
- A pre-load Compact request skips source-only blocks and unused extensions before materializing their records. Capacity estimates use bounded blocks that will be visited, excluding ignored top-level payloads. Counts are capped by actual payload bounds before being used as allocation hints.
- Compact extracts required CPU rendering arrays from the retained subset and releases its temporary document after CPU extraction. Source indices remain unexpanded until `initGL()`; upload arrays are released only after successful initialization. CPU bounds/metadata and `isLoaded()` remain available before GL. Saving is already unavailable at this point and requires a Complete reload.
- Initial Compact upload does not reopen the source. Context-loss reconstruction reapplies the Compact load request; a failed reload preserves existing runtime/bounds state. Explicit later compaction also sets the mode for future reloads.

## Initial optimization loading results

Measurements use the same Linux/WSL Release environment and original files as the earlier reports. New application results are medians of three separate processes per file, with warm filesystem data and textures outside the load timer. Timed runs were separate from sanitizer/build/corpus stress work. The previous CPU values come from the earlier recorded comparisons. Standalone parser stages use one warmup and seven samples, with previous document destruction outside timing.

The three compressed binary CD shapes use legacy `SFileC`; the original UTF-16 `SD402.s` uses `SFileX`. Legacy measurements combine CPU loading and GL initialization: a separate CPU-only measurement is unavailable. The legacy column below therefore includes GL, while the Complete/Compact CPU columns exclude it. Complete speedup compares previous and optimized Complete CPU times.

| Original file | Legacy backend | Legacy CPU + GL | Previous Complete CPU | Optimized Complete CPU | Requested Compact CPU | Complete speedup |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `CD_193290.s` | `SFileC` | 41.26 ms | 415.26 ms | 91.89 ms | 79.98 ms | 4.52× |
| `CD_193290_MS.s` | `SFileC` | 36.26 ms | 281.68 ms | 58.22 ms | 41.11 ms | 4.84× |
| `CD_193290_FG.s` | `SFileC` | 15.79 ms | 133.59 ms | 29.32 ms | 20.14 ms | 4.56× |
| `SD402.s` | `SFileX` | 15.26 ms | 170.18 ms | 53.20 ms | 53.37 ms | 3.20× |

Totals below are medians of the per-run CPU + GL sums. GL timings use 192×192 llvmpipe software rendering; they do not represent hardware GPU timing.

| File | Legacy backend | Legacy CPU + GL, same runs | Complete CPU + GL | Requested Compact CPU + GL |
| --- | --- | ---: | ---: | ---: |
| `CD_193290.s` | `SFileC` | 41.26 ms | 97.55 ms | 93.90 ms |
| `CD_193290_MS.s` | `SFileC` | 36.26 ms | 71.03 ms | 55.87 ms |
| `CD_193290_FG.s` | `SFileC` | 15.79 ms | 30.44 ms | 23.88 ms |
| `SD402.s` | `SFileX` | 15.26 ms | 54.71 ms | 54.14 ms |

The complete and requested-Compact timers include different cleanup work: Compact releases its temporary source document during CPU loading, whereas Complete retains it. Lower retained memory does not guarantee that Compact wins every latency sample. Both modes still traverse required geometry records and perform runtime extraction; compressed inputs still require inflation. These results are substantial improvements, not a claim of legacy loading parity.

## Initial optimization storage

The following follow-up measurements compare **retained storage after loading and GL upload, before the first draw or texture loading**. CPU values are estimates; GL buffer payload sizes are queried directly for legacy and agree exactly with the new implementation's allocation counters. Each file ran in a separate Release process. These storage runs do not replace the three-run timing medians above.

| File | Legacy backend | Legacy retained CPU estimate | Complete retained CPU estimate | Requested Compact retained CPU estimate | GL buffer payload, identical in all three |
| --- | --- | ---: | ---: | ---: | ---: |
| `CD_193290.s` | `SFileC` | 21.97 KiB | 26.87 MiB | 21.44 KiB | 9.39 MiB |
| `CD_193290_MS.s` | `SFileC` | 10.83 KiB | 18.28 MiB | 9.72 KiB | 7.41 MiB |
| `CD_193290_FG.s` | `SFileC` | 39.89 KiB | 9.17 MiB | 7.14 KiB | 1.99 MiB |
| `SD402.s` | `SFileX` | 24.38 KiB | 4.40 MiB | 34.61 KiB | 1.60 MiB |

Legacy releases source points, normals, UVs and triangle indices during upload; those freed arrays are excluded. Its estimate counts the SFile object, accessible retained arrays, known spare slots, QVector capacities, string contents and animation keys/frame lookup tables. It excludes private state allocations, unrecorded spare primitive slots, spare string capacity, Qt/allocator overhead and allocations leaked outside reachable shape fields. Shared string contents can be counted more than once. Thus small CPU differences should not be treated as exact allocation differences. The new CPU estimates sum source documents, retained geometry and runtime storage; Complete retains editable source data, while requested Compact has released its temporary document and geometry by this point.

The larger legacy CPU estimate for FG is mainly animation storage: 34.56 KiB of its 39.89 KiB consists of animation vectors, including precomputed frame lookup tables. No animation-correctness conclusion follows from this memory difference. GL payloads exclude driver bookkeeping and textures; under llvmpipe their backing memory is on the CPU. None of these figures measures peak loading memory or process RSS.

For comparison with the earlier document representation, source-document storage alone remains:

| File | Previous Complete document | Optimized Complete document | Temporary Compact document |
| --- | ---: | ---: | ---: |
| `CD_193290.s` | 339.49 MiB | 22.60 MiB | 20.99 MiB |
| `CD_193290_MS.s` | 228.54 MiB | 15.04 MiB | 13.97 MiB |
| `CD_193290_FG.s` | 114.64 MiB | 7.74 MiB | 7.19 MiB |
| `SD402.s` | 60.14 MiB | 3.51 MiB | 3.88 MiB |

Legacy has no equivalent retained editing document. Its runtime storage is reported in the preceding table.

Old estimates summed source nodes/content. New estimates include all arena capacities and sparse strings/payloads; neither includes allocator overhead, shared textures or process RSS. These are source-document estimates, not total peak process memory. Compact retains CPU geometry until upload and then releases it; `requested_compact_load_document_bytes` records its temporary document allocation before cleanup.

For the three CD files, pre-load Compact materializes 358,874 / 225,764 / 132,539 blocks and 1,454,302 / 964,344 / 512,381 scalars. Complete retains 359,251 / 225,993 / 132,661 blocks and 1,728,622 / 1,180,842 / 570,687 scalars. Most blocks on these assets describe required geometry, so skipping a few large source-only lists matters more than the number of skipped blocks alone. All three still have one LOD.

## Text reader and document stages

On original UTF-16 SD402, document-only reading decreased from 113.63 to 36.71 ms and tokenization from 25.86 to 7.03 ms. UTF-16 decoding was approximately 0.60 ms. New document reading includes conversion into native fields, which the former text-backed document deferred until extraction.

The 100,000-number microbenchmark decreased from 19.15 to 3.58 ms; ParserX measured 1.69 ms in that same new run. This is a numeric-reader comparison, not a complete shape-loader comparison.

Generated UTF-16 versions of CD main/MS/FG took 329.85 / 171.61 / 76.84 ms for Complete document reading, versus 659.34 / 440.86 / 234.34 ms before optimization. The main file's standalone decode stage measured 43.21 ms versus 4.33 ms previously despite unchanged decoder code and identical input bytes; allocation/cache effects were not isolated, so do not infer a decoder implementation regression from that stage. The smaller generated files decoded in 2.61 / 1.39 ms. All three generated exports are byte-identical to the corrected exports previously accepted by the unchanged Open Rails parser.

## Validation

- Release application, independent document tests and parser benchmark build successfully. Both CTest suites pass.
- The application shape/GL suite passes all 51 checks, including pre-load Compact behavior, immediate save rejection, CPU bounds/metadata, upload after temporarily removing the source, and Compact context-loss reconstruction.
- ASan/UBSan with leak detection passes 2,112 checks for TRAINSET and 238 checks for CD_193_290. Both include 193 synthetic checks. Corpus tests cover three semantic round trips per asset, pre-load Compact on every asset, and byte-for-byte comparison with independently inflated original binary payloads: 121 stock binaries plus all three CD binaries.
- All 129 stock shapes pass Complete loading, save/reload, saved rendering, GPU upload, bounds and new direct/gather picking. Requested-Compact rendering and picking match Complete for all 129. The final allocation-only adjustment is additionally covered by the corpus sanitizer runs and repeated CD checks.
- Legacy comparison observations are unchanged: 121 exact geometry/static-transform matches, eight tiny original-text numeric differences, 121 exact static images, and 105 exact animation samples. Complex animation differences remain observations, not evidence that either implementation is correct.
- Synthetic coverage proves that large ignored binary payloads do not determine Compact arena allocation, corrupt root lengths remain bounded, edited dense points survive saving with unknown content, and document copies do not alias editable storage.
- The storage follow-up rebuild and all five GL corpus checks pass (three CD shapes, original SD402 and Acela wiper). Queried legacy VBO sizes match vertex-count × nine-float-stride calculations and both new modes' buffer totals on every file. Instrumentation is confined to the comparison harness; the legacy loaders are unchanged.

## Reproduction and evidence

Commands and metric definitions are in [tests/shapes/README.md](../../../../tests/shapes/README.md). Use the existing `compare.py --gl` command on `TRAINS/CD_193_290`, repeating it three times with different output directories. For the original text comparison, temporary symlinks to SD402 and Acela's wiper were placed in `/tmp/sfile-optimized-text-corpus`; the harness resolves them to their original source/texture directories.

Final application results: `/tmp/sfile-optimized-release-cd-{1,2,3}/results.jsonl` and `/tmp/sfile-optimized-release-text-{1,2,3}/results.jsonl`. Stock GL correctness run: `/tmp/sfile-optimized-stock/results.jsonl`; its timing values are not used here because other correctness work ran concurrently. Sanitizer logs: `/tmp/sfile-optimized-{stock,cd}-asan-final.log`. Parser stages: `/tmp/sfile-optimized-parser-{binary,text}.jsonl`. Generated exports remain under `/tmp/sfile-optimized-exports`.

Storage follow-up results: `/tmp/sfile-storage-{cd,text}/results.jsonl`. Reproduce with the same GL comparison commands and corpora; the new `legacy_retained_cpu_estimate_bytes`, `legacy_animation_estimate_bytes`, `legacy_gpu_bytes`, `legacy_gpu_size_valid`, `complete_retained_cpu_estimate_bytes`, `requested_compact_retained_cpu_estimate_bytes` and `requested_compact_gpu_bytes` fields are defined in the harness README.

Original assets and legacy SFile/C/X code remain unchanged. The legacy factory remains the default. Hardware GPU, route-scale stress and independently validated complex-animation behavior remain outside this optimization pass.
