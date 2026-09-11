# Context-aware multi-word shape labels

SFileComplex now accepts unquoted multi-word labels at recognized text block
headers. For example, `matrix drzwi 1_002 (...)` retains the full name
`drzwi 1_002`. This is an explicit compatibility extension beyond the native MSTS
header reader, rather than an attempt to reproduce Open Rails' suffix skipping.

## Parsing boundary

`SimisTextReader::readBlockHeader` is reusable and has no shape-token whitelist.
Its caller must already have consumed a block keyword. It accepts the opening
parenthesis directly, one quoted label, or unquoted words joined into one label.
A quoted name is not combined with trailing words. Recovery stops at a closing
parenthesis, EOF, a non-atom suffix, or its bounded recovery limit (256 words /
4096 UTF-16 code units). Existing single-item labels are not subject to that
multi-word recovery limit.

The shape document reader keeps the existing normal-header path. It only adds
extended recognition where a known container's declared scalar prefix/suffix has
been consumed and the next item is a recognized block keyword. The main `shape`
header is also an explicit block context. Unknown mixed-content extension bodies
keep their existing inference; this change does not claim to infer arbitrary
unknown grammars. The SD filename remains a value, not a header or label.

Compact's packed table reader falls back to the ordinary document reader if it
encounters a multi-word row label. This preserves shared recovery semantics and
avoids adding work to ordinary dense numeric reads. Binary parsing is unchanged.

## Retention and rendering

Recovered headers produce a diagnostic and a Recovered runtime shape, rather than
Broken. Complete retains the complete names on matrices, animation nodes and other
source blocks. Its normal writer quotes the labels, so saving and reloading a
recovered file no longer requires recovery. Compact keeps the same matrix names
for rendering and retains its existing no-save policy.

No source shape was edited. Legacy, SFile/C/X, animation matching and renderer
ownership behavior are unchanged. Existing degradation of damaged animations is
retained independently of label recovery.

## Validation and timing artifacts

Private artifacts are under `/root/shape-compat-20260911/label-recovery/`:

- `affected.json`: the 16 previously failing coach files.
- `affected-roundtrip.log`: original parsing and normalized save/reload checks.
- `suite.log`: application CPU/GL suite.
- `corpus.py`, `corpus/results.jsonl`, per-shape logs: Compact/Complete rendering
  comparison against the previous corpus snapshots, including source hashes.
- `SFileDocument-before.cpp`, `SimisTextReader-before.cpp`, `TSRE5vc-before`:
  pre-change baselines.
- `parse-bench.cpp`, `performance.py`: initial separate-process timing experiment.
- `paired-bench.cpp`, `before-document.h`: tighter same-process parser comparison.

Initial separate-process results varied substantially even on the unchanged
binary reader. They must not be used as evidence of a speedup or regression.
Final controlled timings and the completed corpus summary follow below.

## Final parsing-time comparison

Both standalone parsers were built with the same compiler and `-O2 -DNDEBUG`.
The before build uses frozen pre-change document/lexer source. Inputs are read
before timing; timings include document parsing, decompression and UTF-16 decoding,
with document destruction outside the measured interval. Each result is the median
of 60 retained samples, after warm-up, with repeated before/after/after/before order.

The benchmark ran on CPU 0 with `QT_HASH_SEED=0`, ASLR disabled for both test
processes, and the entire corpus process group paused. Other CPU cores were only
0.9–2.9% busy during the final run. No compiler or other test ran concurrently.
These are parser timings, not end-to-end shape loading or GL timings.

| Input | Encoding | Compact before → after (ms) | Change | Complete before → after (ms) | Change |
|---|---|---:|---:|---:|---:|
| `SD402.s` | UTF-16 | 15.179 → 15.352 | +1.14% | 31.327 → 31.304 | -0.08% |
| `CD_163046.s` | UTF-16 | 33.548 → 33.008 | -1.61% | 69.673 → 70.508 | +1.20% |
| `prouzek.s` | UTF-16 | 0.100 → 0.102 | +1.65% | 0.188 → 0.185 | -1.89% |
| `CD_193290.s` | Compressed binary | 26.582 → 26.494 | -0.33% | 81.224 → 79.486 | -2.14% |
| `CD_193290_MS.s` | Compressed binary | 17.515 → 17.134 | -2.17% | 53.558 → 52.583 | -1.82% |
| `CD_193290_FG.s` | Compressed binary | 6.206 → 6.096 | -1.76% | 27.418 → 26.598 | -2.99% |

No material parsing-time regression was observed. The largest increase on a large
text input was 1.2%, or 0.84 ms; unchanged binary controls varied by up to about 3%.
Do not interpret the negative differences as a demonstrated optimization benefit.

Exploratory measurements exposed both benchmark-layout bias and avoidable parser
code-generation overhead. The final implementation keeps recovery behind the
existing alphabetic block-keyword test, preserves direct copying for normal labels,
and outlines label handling using only its sparse header storage. It does not pass
the numeric block record into that helper.

Primary raw data: `v4-performance-raw.json`, `v4-performance-summary.json`,
`v4-performance-environment.json`; runner: `v4-performance.py` and
`run-v4-performance.py`. The latter records the live corpus process group used for
this run; substitute the active group or run with no corpus process when repeating.
`provenance.json` records source and executable hashes. Earlier standalone and
same-process experiments, including the identical-parser control, are retained
for audit but are not the primary timing result.

## Completed compatibility validation

- Final application build passed; document suite: **233 checks, zero failures**;
  application CPU/GL suite: **108 checks, zero failures**.
- All **16** affected coaches parse in Complete and Compact, initialize GL, retain
  full matrix names and have matching comparison snapshots between modes.
- Real-coach document/normalized-save/reload checks: **441 checks, zero failures**
  (233 base checks plus 208 corpus checks across the 16 files).
- Full corpus: **4,306/4,306** shapes render in both modes. Each mode reports
  **4,289 Valid, 17 Recovered, zero Broken**. The recovered cases are the 16 spaced
  label coaches and the ET41 animation-damage case handled by the earlier fix.
- All **4,289 previously successful** cases match their prior Compact/Complete
  snapshots exactly for the compared fields: bounds, size, metadata, LOD structure,
  matrix names, part metadata, buffers, transforms, direct/gather/picking images,
  sampled animation images and texture readiness/error counts.
- Compact and Complete comparison snapshots agree for every file.
- All **4,306 original source hashes** match the reference run and remain unchanged.

The full corpus run began before the final cold-path-only performance changes and
resumed on the final build. To cover that distinction explicitly, **all 203 UTF-16
files were replayed on the final build**: all render, the 186 previously working
text files retain their prior comparison results, and Compact/Complete agree.
The binary parsing implementation was not changed. The final text replay records
and verifies the final executable hash on every result.

This verifies regression against the previous new-parser modes. It does not claim
that their previously documented differences from Legacy have been eliminated.
Missing textures already present in the reference collection remain unchanged.

Summaries: `corpus-summary.json` and `corpus-final-text-summary.json`. Final test
logs: `suite-final.log` and `affected-roundtrip-final.log`. The renderer tests ran
with `/mnt` mounted read-only; generated outputs stay on Linux.
