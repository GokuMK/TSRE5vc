# SFileComplex UTF-16 loading optimization — 2026-09-10

This follows the [Compact storage pass](sfile-optimization-results.md#compact-loading-follow-up). There is still one reusable `SimisTextReader` and one `SFileComplex`. Legacy ParserX/SFileC/SFileX and the default backend remain unchanged.

## Implementation and profiling findings

The initial SD402 diagnostic measured roughly 8.6 ms for tokenization, 18.6 ms when each token was preceded by copying `peek()`, and 5.0 ms for conversion of preselected float-convertible tokens. These overlapping microbenchmarks identified lookahead/token handling as useful targets; they are not an additive decomposition of document loading.

- Three fixed lookahead slots replace the QVector queue. Reader snapshots retain independent cursor/queue state, including queued quoted strings.
- `peekKind()` classifies ordinary atoms without constructing/copying their token text. Quoted strings still use the normal scanner and report malformed input consistently.
- Consuming `readNumber()`, `readInteger()` and `readUnsignedInteger()` operations work directly on UTF-16 spans when no token is queued. They retain the Token APIs' checked syntax, range and token-consumption behavior. Integer overflow checks compare against precomputed decimal/hexadecimal limits instead of dividing for each digit.
- Compact numeric tables use those reader operations. Their token-name comparisons use Latin-1 string views, avoiding temporary QString conversions for every row.
- Regular integer arrays append native words/type tags directly in Complete and Compact mode. An unusual value, nested extension or missing delimiter rolls back speculative words and cursor state and retries the general parser. Declared counts do not determine allocation.
- Common ASCII whitespace has a direct check, with Unicode whitespace still handled through QChar. UTF-16 decoding, floating-point conversion semantics and Complete serialization rules are unchanged.

## Measurement conditions

The actual pre-change application and parser benchmark were saved before implementation. Final tests alternate before/after, after/before, before/after with no concurrent builds, sanitizers or other test jobs. Document results below pool all 21 measured samples per variant/mode/file: three processes, each with one warmup and seven samples. Application results are medians of three independent processes. No slow samples were removed.

Windows host CPU activity remained 12.50–22.53% in six five-second samples before the final batch and 10.27–12.34% afterward, using GetSystemTimes while benchmarks were stopped. Results therefore describe the recorded conditions, not verified fully idle-host performance. The raw logs retain sample ranges/outliers. For SD402, document Compact samples span 25.28–52.11 ms before and 15.53–29.61 ms after; application Compact CPU samples span 39.78–41.67 ms before and 27.01–29.16 ms after.

## Document parsing

`SD402.s` is the original UTF-16 asset. The three CD inputs in this table are equivalent generated UTF-16 exports of the original compressed binary shapes. These timings include file reading, decoding and document construction, but exclude runtime extraction and GL. Complete retains editable source records; Compact uses packed runtime tables.

| UTF-16 file | Complete before | Complete after | Compact before | Compact after | Compact median speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| `SD402.s` | 55.59 ms | 43.11 ms | 26.99 ms | 15.81 ms | 1.71× |
| `CD_193290.s` | 331.54 ms | 279.75 ms | 256.15 ms | 201.61 ms | 1.27× |
| `CD_193290_MS.s` | 192.48 ms | 175.09 ms | 134.32 ms | 95.65 ms | 1.40× |
| `CD_193290_FG.s` | 79.89 ms | 64.03 ms | 54.68 ms | 34.58 ms | 1.58× |

In the final SD402 diagnostic samples, ordinary tokenization is essentially unchanged (7.03→7.17 ms). Copying lookahead improves from 15.09→12.37 ms; the new kind-only lookahead measures 8.95 ms. Float conversion of preselected tokens is also essentially unchanged (4.60→4.76 ms), and decoding measures 0.66→0.72 ms. The gain comes from avoiding token/cursor/storage overhead, rather than relaxing conversion checks or replacing the decoder.

The simple 100,000-number benchmark measures 3.42→3.59 ms through the Token API, 3.18 ms through the new direct API and 1.71 ms through ParserX in the new runs. All produce the same checksum. This microbenchmark does not establish whole-loader parity, and the new checked number reader remains slower than ParserX on these literals.

## Original SD402 application loading

Application timers include different allocation/cache history from the standalone benchmark, so document-stage medians should not be subtracted from application totals to infer extraction time. Legacy SFileX combines CPU loading and upload and has no separate CPU-only timer.

| Backend/mode | CPU before | CPU after | CPU + GL before | CPU + GL after |
| --- | ---: | ---: | ---: | ---: |
| SFileComplex Complete | 67.49 ms | 57.75 ms | 68.29 ms | 58.92 ms |
| SFileComplex Compact | 39.86 ms | 28.24 ms | 42.50 ms | 29.25 ms |
| Legacy SFileX | Not isolated | Not isolated | 15.72 ms | 16.22 ms |

Compact CPU loading decreases by approximately 29%, and Complete by 14%. New Compact CPU + GL is still about 1.80× the same-run SFileX measurement. GL uses llvmpipe software rendering with texture loading outside the load timer; these are not hardware GPU measurements.

The tiny Acela wiper does not show a consistent Compact improvement: CPU medians are 0.51→0.68 ms, with ranges 0.41–0.60 and 0.35–0.94 ms. Complete improves from 0.94→0.78 ms. These sub-millisecond results are included in the raw evidence; no universal per-file speedup is claimed.

## Preservation and verification

- Both CTest suites pass; the application shape/GL suite passes all 56 checks.
- All 129 stock shapes pass Complete loading, saved rendering, requested-Compact rendering and picking comparisons. Complex-animation behavior is unchanged by this pass and remains a separate validation topic.
- ASan/UBSan with leak detection passes 2,131 stock-inclusive checks and 251 checks over the three large generated UTF-16 shapes. Tests include Complete semantic round trips and pre-load Compact parsing.
- Complete exports from the saved old reader and new reader are byte-identical for original SD402 and all three generated CD text shapes. The export comparison runs separately from timing.
- New synthetic checks exercise repeated lookahead wraparound, independent reader snapshots, queued quoted/nested blocks, direct/token numeric equivalence, Unicode whitespace/BOM handling, signed/unsigned/hexadecimal boundaries, and rollback/preservation of malformed or extended arrays.
- All 12 final application processes (two original text assets × two executables × three runs) pass their comparison checks. Original assets and legacy classes are unchanged.

## Reproduction and evidence

Commands and metric definitions are in [tests/shapes/README.md](../../../../tests/shapes/README.md). The pre-change binaries are `build/TSRE5vc-utf16-before` and `/tmp/tsre-shape-parser-profile-before-utf16`; current targets are `build/TSRE5vc` and `build/tests/shapes/tsre_shape_parser_bench`. The baseline parser has the new profiling stages that use the original Token APIs; only the current executable has the kind-only/direct-read stages.

Final stages: `/tmp/sfile-utf16-final-{before,after}-{complete,compact}-{1,2,3}.jsonl`. Application results: `/tmp/sfile-utf16-app-{before,after}-{1,2,3}/results.jsonl`. Aggregated samples: `/tmp/sfile-utf16-summary.json`. Host checks: `/tmp/sfile-utf16-host-{before,after}.json`. The batch recipe is `/tmp/run-sfile-utf16-final.py`.

Correctness evidence: `/tmp/sfile-utf16-stock/results.jsonl`, `/tmp/sfile-utf16-gl.log`, `/tmp/sfile-utf16-stock-asan.log`, `/tmp/sfile-utf16-generated-asan.log`. Byte-identical exports remain in `/tmp/sfile-utf16-exports-{before,after}`; proprietary inputs/outputs stay outside the repository.

Further work can target document construction and large-input decoding if profiling justifies it. A parser/class split, unchecked numeric shortcuts, animation changes and default-backend adoption are not part of this pass.
