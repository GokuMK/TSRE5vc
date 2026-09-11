# SFileComplex comparison — CD_193_290 (2026-09-10)

Requested follow-up corpus: `/root/msts/proprietary/msts_app/TRAINS/CD_193_290`. All three files were compared in three separate-process runs per file using the existing harness and unchanged implementation. These are compressed binary shapes, so the legacy load path is SFileC; this does not measure the new text reader against ParserX.

The subsequent [parser investigation](sfile-parser-performance.md) adds UTF-16 measurements, Open Rails timings and the proposed loading/storage redesign.

## Loading

Both total columns include CPU loading and GL geometry initialization. Values are medians of three runs, with warm filesystem data and texture loading outside the load timer. Same Release build and software GL setup as the [initial comparison](sfile-complex-comparison.md).

| Shape | Legacy total | New total | Slowdown | Legacy range | New range |
| --- | ---: | ---: | ---: | ---: | ---: |
| `CD_193290.s` | 46.55 ms | 422.32 ms | 9.07× | 45.57–58.52 ms | 413.72–443.80 ms |
| `CD_193290_MS.s` | 27.16 ms | 284.99 ms | 10.49× | 27.00–27.36 ms | 281.50–292.00 ms |
| `CD_193290_FG.s` | 11.31 ms | 134.55 ms | 11.90× | 10.47–11.83 ms | 128.53–158.30 ms |

New CPU-only medians: 415.26, 281.67 and 133.59 ms respectively. Compact uses that same initial parser and releases source storage after upload; these are not improved Compact-load timings.

All three have **one LOD**, so first-LOD loading discards nothing and cannot reduce their retained structure. The harness measures that option later while retaining several Complete documents for round-trip comparison. Its unusually high timings here (about 996–2237 ms for the main shape) are not an isolated comparison of first-LOD versus full loading; memory/allocator conditions differ at that stage. Do not interpret them as a benefit or intrinsic cost of selecting one LOD.

## Storage and static rendering

| Shape | Compressed file | Inflated payload | Complete document estimate | Compact runtime estimate | GPU geometry |
| --- | ---: | ---: | ---: | ---: | ---: |
| `CD_193290.s` | 2,188,625 B | 10,151,071 B | 355,976,888 B (339.49 MiB) | 36,848 B | 9,849,492 B |
| `CD_193290_MS.s` | 1,440,637 B | 6,757,939 B | 239,644,060 B (228.54 MiB) | 20,048 B | 7,774,164 B |
| `CD_193290_FG.s` | 410,722 B | 3,477,835 B | 120,208,880 B (114.64 MiB) | 11,220 B | 2,090,448 B |

Document and source-geometry counters become zero after compaction. Estimates exclude process/allocator overhead and shared textures; they are not RSS. The initial large document allocation still occurs even for Compact workflows.

All nine runs successfully loaded and uploaded geometry, preserved semantic content through save/reload, and rendered the saved output identically to the new source load. For each file:

- Uploaded vertex data and static transforms match legacy exactly.
- Bounds and part counts agree (62 main, 41 MS, 15 FG).
- Static direct images and integer picking match legacy exactly.
- New direct/gather images and picking agree exactly.

Frame times below include GL completion and framebuffer readback, at 192×192 on llvmpipe. They are medians of the three harness averages, each averaging 12 frames. They are not hardware GPU performance measurements.

| Shape | Legacy direct | New Complete direct | New Compact direct | New Compact gather |
| --- | ---: | ---: | ---: | ---: |
| `CD_193290.s` | 27.24 ms | 27.15 ms | 26.73 ms | 26.94 ms |
| `CD_193290_MS.s` | 21.77 ms | 21.35 ms | 22.62 ms | 22.07 ms |
| `CD_193290_FG.s` | 6.83 ms | 6.76 ms | 6.91 ms | 6.94 ms |

## Animation behavior differs; correctness is not established

The animation sample at 0.17 s differs consistently across all three runs:

| Shape | Differing pixels out of 36,864 | Maximum transform-element difference |
| --- | ---: | ---: |
| `CD_193290.s` | 809 | 1.4132 |
| `CD_193290_MS.s` | 0 | 0 |
| `CD_193290_FG.s` | 857 | 2.0000 |

These main/FG differences are substantial, not the small rounding differences from the stock text files. The existing diagnostic legacy endpoint correction does not resolve them (809 main / 986 FG differing pixels afterward), and no static matrix-hash collisions were detected. Their cause is not established by this test. As the user clarified, complex animation has not been validated in TSRE; legacy output is not a correctness oracle. These are behavioral differences, not demonstrated defects in the new implementation. Correctness needs comparison with the intended animation or an independently validated renderer.

The harness exits successfully because its enforced checks cover load/save, bounds, GPU readiness, saved-output rendering and agreement between the new direct/gather paths. Legacy/new animation differences are recorded observations; exit zero does not mean every compatibility observation matches.

## Reproduction and evidence

```sh
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a python3 tests/shapes/compare.py \
  --exe build/TSRE5vc \
  --corpus /root/msts/proprietary/msts_app/TRAINS/CD_193_290 \
  --output /tmp/sfile-cd193290-comparison --gl
```

Local JSON manifests/logs: `/tmp/sfile-cd193290-comparison`, `/tmp/sfile-cd193290-comparison-2`, `/tmp/sfile-cd193290-comparison-3`. First-run static images: `/tmp/sfile-cd193290-images`. Original files were not modified or copied into the repository.

Source SHA-256:

- Main: `a24bed7d4eaf0d6cdf0e9f434eac4088f22c8bc4a5a9f231d9074baf58b0e3ad`
- MS: `c09411fb83b8e07dd517f660125fe69c5069091d1058545498bfe4c2c7ff5d97`
- FG: `488472d9f4c0a4187ae6d16c4b59b8f7cb5cb794ffa12229f73fde410e9bfd23`

No implementation changes were made for this comparison. The results confirm the loading/memory problem on larger binary files and record animation behavior differences without assigning correctness to either implementation.
