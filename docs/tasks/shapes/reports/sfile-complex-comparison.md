# SFileComplex initial comparison — 2026-09-10

The opt-in implementation passes the initial stock corpus checks. This is evidence for continued development and review, not authorization to replace/deprecate SFile/C/X. Large routes, hardware GPU measurements and more complex assets remain outside this initial comparison.

## Reproduction and environment

Worktree `/root/TSRE5vc-sfile-complex`, branch `feature/sfile-complex`, documentation baseline `9c6cf1b`. Linux/WSL, CMake/Ninja Release application, Qt 6, Mesa 26.2.2 llvmpipe (LLVM 22.1.8); **software rendering, not an accelerated GPU**. Matching 192×192 cameras and shipped shaders; depth/blending enabled, shadows disabled. Texture loading is synchronous for the test and warmed for eight frames. Temporary case aliases reproduce Windows image lookup without changing originals.

Commands and metric definitions are in [tests/shapes/README.md](../../../../tests/shapes/README.md). Private run: `/tmp/sfile-stock-verified/results.jsonl`, with one log per asset. It records relative paths and source SHA-256 values. No stock files or screenshots were added to the repository.

Input: all 129 `.s` files recursively under `/root/msts/proprietary/msts_app/TRAINS/TRAINSET`: 115 compressed binary, 6 uncompressed binary, 8 UTF-16 text. Adjacent metadata and textures are read where present. No exclusions or crashes.

## Results

| Check | Result |
| --- | --- |
| Complete CPU load, rendering prerequisites and GL upload | 129 / 129 |
| Semantic same-encoding save/read/save | 129 / 129 |
| Rendering normalized saved output through the new loader | 129 / 129 identical to new source rendering |
| Bounds and first-LOD hierarchy part counts against legacy | 129 / 129 |
| New direct/gather images and integer picking | 129 / 129 identical |
| Packed geometry against legacy | 121 exact; remaining 8 differ by at most 0.0000019074 |
| Static transforms against legacy | 121 exact; remaining 8 differ by at most 0.0000004769 |
| Unmodified legacy/new static images | 121 exact; 8 explained differences below |
| Unmodified legacy/new integer picking | 123 exact; remaining 6 explained by legacy matrix-cache collisions |
| Animation sample at 0.17 s | 105 exact; endpoint/caching/float differences are recorded below |
| Compact retained document and source geometry | Both counters zero for all 129; runtime state and GPU data retained |

Application synthetic suites: 31 CPU checks and 46 checks with GL, all passing. Coverage includes CPU-loaded state before GL, edited saves, Partial-to-Complete reload, unsaved-edit protection, compaction, failed/retried Compact buffer reconstruction, per-instance visibility, line/point packets and direct/gather selection. Existing native token CTest and the legacy shape/world GL suite (40 checks) pass.

The standalone document tests exercise every original grammar family, optional fields, all four encoding/compression combinations, unknown text/binary records and tails, edits, truncations, bounded corruptions and oversized inflation. The optimized AddressSanitizer/UndefinedBehaviorSanitizer run passes **1,719 checks**: the synthetic cases plus three semantic round trips for each of the 129 stock files. No sanitizer errors were reported.

## Explained differences and fixes

- Five Metro carriage variants produce 31 differing pixels each; `METROLINER/metrocarriage1.s` produces 73. Their uploaded geometry and computed transforms are exact. Distinct transforms collide in `GLUU::getMatrixHash()`, causing legacy direct rendering to skip a required matrix upload. Assigning unique hashes **only to the test's legacy instance** removes every image and picking difference. The new implementation uploads the required transform; legacy production code is unchanged.
- `GP38/CABVIEW/gp38wiper.s` differs by 1 static pixel and `SD402/SD402.s` by 7. These text assets expose small ParserX/native numeric and matrix arithmetic differences. Geometry/transforms remain within the tolerances above; selection is identical.
- Legacy `buildFrameIds()` falls back to the first key at the final key frame. This changes interpolation near the end of a non-closed track. Correcting only the test instance's endpoint entries removes, for example, Acela's 65 differing animation pixels and Metroliner's 161. The new implementation samples the actual adjacent source keys. Small interpolation/float differences remain on some assets (including SD402); exact emulation of ParserX and legacy floating-point arithmetic is not a requirement.
- Legacy gather rendering is itself sensitive to packet/texture grouping and draw order: only 26 of the 129 static legacy direct/gather images were exact in this run. The new gather path preserves the new direct path's part order and matches it for all 129.
- Initial comparisons exposed and fixed a new gather wireframe flag, a quaternion sign, a zero-valued GL_POINTS/default-packet ambiguity, and an unsafe QHash schema assignment across insertion/rehash. The latter caused intermittent geometry-map serialization failures under different hash layouts. The final run above has zero failures.

The hash/endpoint interventions are diagnostic controls in the comparison harness; they do not modify the legacy classes or stock assets.

User clarification: complex animation has not been validated in TSRE. Differences from legacy output are observations, not sufficient evidence of a defect in the new implementation. Endpoint controls explain some differences but do not establish comprehensive animation correctness in either backend.

## Performance and storage

These are initial diagnostic measurements. Filesystem data was warm; geometry load timing excludes texture loading. The corpus run was not a dedicated isolated benchmark machine. `frame` includes GL completion and framebuffer readback (12 samples); submission values summarize 24 samples per asset, without readback and with prior GL work completed outside the timed interval. Driver calls may still block. There is no independent hardware GPU timing claim.

| Median across 129 assets | Legacy | New Complete | New Compact |
| --- | ---: | ---: | ---: |
| Load (ms) | 1.487, including GL | 14.890 CPU + 0.251 GL | Same initial parser; compacts after upload |
| Frame including readback (ms) | 2.173 | 2.173 | 2.194 |
| Median submission (ms) | 0.579 | 0.573 | 0.607 |

New Compact gather: median frame 2.156 ms; median submission 0.616 ms. First-LOD CPU loading: median 13.400 ms. Global tables still have to be read, so first-LOD loading is not proportional to the number of LODs skipped.

| Asset | Legacy load + GL | New CPU / GL | First LOD CPU | Complete document estimate | Compact runtime estimate |
| --- | ---: | ---: | ---: | ---: | ---: |
| ACELA/acela.s | 3.633 ms | 35.335 / 0.468 ms | 33.558 ms | 28,443,248 B | 23,462 B |
| DASH9/dash9.s | 3.253 ms | 34.014 / 0.635 ms | 28.727 ms | 29,317,412 B | 21,730 B |
| SD402/SD402.s | 17.976 ms | 170.183 / 1.647 ms | 138.589 ms | 63,056,270 B | 45,038 B |

Median Complete document estimate: 10,917,120 B; maximum: 72,702,834 B. Median retained Compact runtime estimate: 9,652 B; maximum: 45,038 B. These are structural estimates, **not process RSS**: they exclude allocator overhead, shared texture storage, and some container bookkeeping. GPU buffers are separate; Acela retains 816,804 B of packed geometry on the GPU. Complete additionally retains source geometry (508,336 B for Acela). Compact clears that source estimate after successful upload.

Binary native-value storage roughly halved Acela CPU loading compared with the first string-backed implementation, but **full source parsing is still substantially slower and has a much larger peak footprint than legacy rendering-only loading**. Compact reduces retained memory; it does not eliminate that loading peak. The software frame medians are close; outliers (including SD402) require isolated hardware measurement before drawing throughput conclusions or enabling this backend by default.

## Remaining scope

See the [format ledger](sfile-complex-format-coverage.md) for the distinction between preservation and rendering. All known grammar families are represented and serialized, but TSRE's renderer still uses the first texture/UV layer, legacy material/light conventions, the first animation and linear/quaternion interpolation. Full TCB weighting, UV callbacks/effects, secondary matrix skinning and complex-box orientation are preserved, not fully evaluated.

Complete saving writes `.s` and `.sd` through separate explicit methods. Unknown same-encoding data is retained; lossless cross-encoding conversion is refused when its schema is unavailable. Saving Compact/Partial requires a full reload. Missing source during Compact GPU reconstruction preserves the prior loaded CPU bounds/state for inspection and retry.

Broader format interoperability, long-lived context/texture-cache stress, route-scale load throughput, animated multi-instance scenes, all LOD/view combinations and hardware rendering remain follow-up validation. Keep the default factory on legacy until that review is complete.
