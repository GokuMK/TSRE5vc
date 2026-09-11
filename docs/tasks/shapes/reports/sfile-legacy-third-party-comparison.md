# SFileX/C versus SFileLegacy: third-party trainset collection

## Scope and source protection

User-provided source: `/mnt/c/trainsim/trains/trainset/`. The source is read-only.
Inventory: 4,306 `.s` files, 2,946,884,829 bytes; 76,452 files in the full tree.
No linked subdirectories were skipped and no case-normalization collisions were found.

The Linux-only working directory is `/root/shape-compat-20260911`. Its `trainset/`
subdirectory contains lowercase directory names and symlinks to the original files,
including sidecars, textures and shared cab directories. No source files are copied
back, renamed or saved. Relative shared-texture references retain the tree structure.
The test processes run under bubblewrap with `/mnt` remounted read-only. A child
`statvfs` check confirms `ST_RDONLY` on the source mount. This also protects source
files reached through the Linux-side symlinks. Manifests, logs and results remain on
Linux; source size/mtime is checked after each comparison.

## Comparison design

Each file is tested in two independent application processes: original `SFile`
(using SFileX/C) and new `SFileLegacy`. Neither depends on SFileComplex parsing or
round-trip success. Crashes and timeouts in one implementation do not suppress the
other implementation's test. JSON checkpoints preserve the last completed phase.

Comparisons include:

- Load acceptance; SFileLegacy additionally loads without a current GL context.
- Exact hashes of bounds, matrices, shaders, materials, texture names, sidecar data,
  animation keys and legacy frame lookup tables.
- Every LOD's hierarchy, subobject/part metadata and complete GPU buffer contents.
- Every LOD's direct rendered image, ordered gathered image and picking buffer.
- Animated images at the beginning, quarter and three-quarter positions of the first
  animation, matching the animation selected by legacy runtime behavior.
- Texture readiness and missing/error counts, so texture limitations remain visible.

The ordered gathered renderer avoids the existing pointer-keyed QHash draw-order
variation; this does not modify production batching. Matrices are invalidated and
direct rendering primed after LOD changes, consistently for both implementations.
Images are 192 × 192, rendered through Mesa software GL under Xvfb. This compares
implementations, not independent MSTS/ORTS animation correctness or hardware-GPU
appearance. Timings from concurrent workers are not performance benchmarks.

Processes have a 90-second timeout, a 3 GiB address-space limit, no core dumps and a
32 MiB log limit. Limit-related failures must be inspected before being attributed
to a loader. The initial pilot used two workers; the resumed full run uses four,
with files larger than 8 MiB serialized to limit memory pressure. Completed results
are retained across restarts; each path is tested even when source hashes duplicate
another shape, because its sidecars and texture environment may differ.

Statuses distinguish exact matches, shared rejection, old-only failure, new-only
failure, output differences and incomplete/crashed runs. Shared failures are not
reported as successfully rendered shapes.

## Reproduction

```sh
python3 tests/shapes/legacy_compat_inventory.py \
  --source /mnt/c/trainsim/trains/trainset \
  --work /root/shape-compat-20260911

LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a \
  python3 tests/shapes/legacy_compat_compare.py \
  --work /root/shape-compat-20260911 --workers 4
```

The inventory and comparison scripts require a Linux output directory. The comparison
uses bubblewrap and `prlimit`; `QT_HASH_SEED=0` and two llvmpipe threads per process
keep comparisons reproducible and avoid excessive renderer threads. The test entry
point is `shape-complex-corpus-gl` with `TSRE_LEGACY_COMPAT=old` or `new`; these modes
return before all SFileComplex/document tests.

## Results — 2026-09-11

All **4,306 / 4,306 shapes matched**. Both implementations completed every file;
there were no differences, crashes, timeouts, shared rejections or one-sided failures.
The result paths exactly cover the inventory, with no missing or duplicate paths.
There are 4,044 unique shape-file SHA-256 hashes; duplicate content was still tested
at every location with its own sidecar and texture environment.

| Source format | Files | Exact matches |
| --- | ---: | ---: |
| Compressed binary | 4,055 | 4,055 |
| Uncompressed binary | 48 | 48 |
| UTF-16 text | 203 | 203 |
| **Total** | **4,306** | **4,306** |

- **9,747 LODs** matched in layout, full buffer bytes, direct images, ordered gathered
  images and picking buffers. Shapes contain up to eight LODs; none had zero LODs.
- **1,646 shapes** qualified for animation image sampling. Of these, 786 produced
  visibly different images between sampled positions; both implementations agreed.
  All stored animation keys and legacy lookup tables were included in metadata hashes,
  even when sampled images were unchanged.
- **4,306 CPU-only load checks** and **4,306 subsequent GL initialization checks**
  passed for SFileLegacy. Every buffer read succeeded in both implementations.
- The largest source, `common.cab/PKOR_EP09/EP09_3D.s` (62,915,152 bytes), matched,
  as did the large EN57 models.
- All source size/mtime checks remained unchanged. Application processes had `/mnt`
  mounted read-only throughout; all generated artifacts were written on Linux.

### Texture and rendering limits

**340 shapes reported missing/error textures in both implementations**, with matching
counts. They are included in the matches, not silently excluded. Their geometry and
metadata were still compared, but their complete textured appearance is not established
by this standalone-shape test.

Examples include a missing `blank.dds` reference in `CD_Bmz234/CD_Bmz234_525.s` and
shared submodels under `common.fa/mech/` whose relative texture paths can depend on
the owning vehicle's texture directory. This harness supplies the shape's own directory
as its texture root. The test does not classify every texture error as a missing asset
versus a different required caller context.

The result establishes equivalence to current TSRE behavior under software GL. It
does not establish independent animation correctness, all possible camera/view states,
or native hardware-GL behavior. Concurrent-run timings are not used for performance
claims.

### Artifacts

All raw artifacts remain outside the repository under `/root/shape-compat-20260911/`:

- `manifest.json`, `inventory.json`, `formats.json`: inventory and source classification.
- `summary.json`: final coverage and validation counts.
- `results/results.jsonl`: all 4,306 old/new snapshots, source hashes and stat checks.
- `results/<log_key>-old.log` and `results/<log_key>-new.log`: per-process logs;
  each result records its key.

The resumed run initially wrote to `pilot/`; that directory was renamed to `results/`
after successful completion. No production loader change was needed for this comparison.

## Removal assessment

There is **no compatibility blocker found in this collection**. Together with the
earlier stock-corpus and lifecycle checks, this supports switching production use to
SFileLegacy and retiring SFile/X/C in the next migration task.

This task does not delete SFile/X/C or switch the factory. After compatibility is
established, the production factory in `ShapeLib.cpp` must be switched, obsolete
includes and the unused `RouteEditorGLWidget::sFile` declaration removed, and tests
that intentionally use the old implementation migrated or archived. Route-scale and
hardware-GL testing remain separate from this file-level comparison.

Before completing removal, build and run the application with the factory switched,
including a route/consist smoke test that exercises normal texture-root selection and
both direct and gathered rendering. Retain these results as the old-implementation
baseline when migrating tests. Hardware-GL validation remains useful because this
collection was rendered with Mesa software GL.
