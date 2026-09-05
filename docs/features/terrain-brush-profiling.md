# Interactive terrain brush profiling

Diagnostic instrumentation, disabled by default. No saved setting is changed.
Close any existing TSRE instance before launching so two processes do not write
to the same log.
Run `build/TSRE5vc.exe` with `TSRE_TERRAIN_BRUSH_PROFILE=1`, using the project
root as working directory. No dedicated batch launcher is required or tracked.

The paged backend uses optimized normal calculation and packing. The temporary
reference mode has been removed after correctness and interactive validation.
See [terrain normal calculation](terrain-normals.md) for the CPU-only tests and
benchmark. Preserve each run's log.

From PowerShell in the project root:

```powershell
$env:TSRE_TERRAIN_BRUSH_PROFILE = '1'
& .\build\TSRE5vc.exe
# Before launching normally from this same shell:
Remove-Item Env:TSRE_TERRAIN_BRUSH_PROFILE
```

Alternatively, from Command Prompt in the project root, a child shell limits
the variable to this launch without changing the parent shell or Windows settings:

```bat
cmd /c "set TSRE_TERRAIN_BRUSH_PROFILE=1&&build\TSRE5vc.exe"
```

The executable inherits the variable. Qt reads it once through
`qEnvironmentVariableIntValue()`; missing or zero disables profiling.
These commands do not modify `settings.txt`. Preserve `log.txt` before another
TSRE launch, which overwrites it.

## Test procedure

1. Open the empty `ularge` test route, using the **On GPU / Experimental**
   (paged) terrain backend and the usual renderer pipeline. Let terrain loading
   finish. Keep the window visible and keep camera, shadows and other rendering
   settings unchanged throughout each comparison.
2. Select additive heightmap `+/-`, brush size 50. Paint near the start-tile
   centre for around five seconds. Release, pause, then make a second stroke.
   This separates new undo snapshots from subsequent events in a stroke.
3. Repeat near the four-tile corner (local X/Z approximately 1024/1024), if
   useful. A centre-only capture is already enough for the first diagnosis.
4. Close without saving this test route. Keep a copy of `log.txt` **before any
   subsequent TSRE launch**, because the application overwrites that file.
5. Send the log, with the route, brush size and approximate test positions.
   An equivalent 1024 test using the same physical brush size/settings is useful
   for comparison, but not necessary to collect the first 2048 measurement.

This measures real editing with normal mesh refresh enabled; do not comment
out invalidation or refresh calls. It does not automate strokes or save terrain.

## Reading the records

Search `log.txt` for `[terrain-brush-profile]`. Values ending in `_ms` are CPU
wall-clock milliseconds, not GPU timer queries. There are no forced GPU waits,
fences or `glFinish()` calls and no timer call per vertex.

`kind=brush` is one complete `Route::paintHeightMap()` event. Its context records
World/local position, UI brush size and mode. The stages are:

| Field | Meaning |
|---|---|
| `total_ms` | Whole route brush operation, excluding this summary's formatting/log write |
| `paint_ms` | Terrain library editing, including undo, bounds and synchronous mesh refresh |
| `undo_ms` | Heightmap undo lookup/allocation/copy time; `snapshots` counts actual new snapshots |
| `bounds_ms` | CPU patch-bound refresh, including bounds recomputed during edge fill |
| `mesh_ms` | Paged dirty-patch refresh, including edges, vertex construction and uploads |
| `edges_ms` | Raw edge filling and associated dirty-bound updates inside mesh refresh |
| `build_ms` | Temporary vertex allocation, height reads, normal generation and normal packing |
| `vertex_ms` | Output-array allocation/initialization and height writes, excluding normals |
| `normals_ms` | Normal calculation/normalization, gap-bit read and packed-normal writes |
| `upload_ms` | CPU time in buffer bind/write/release calls, including any driver stall there |
| `objects_ms` | World-tile discovery and route terrain-object updates after terrain editing |

**These times are nested, not additive.** `paint_ms` contains `undo_ms`, most
bound work and any synchronous `mesh_ms`; `mesh_ms` contains `build_ms`,
`upload_ms` and `edges_ms`. `bounds_ms` can overlap `edges_ms`. Compare dominant
stages, or use `paint_ms - mesh_ms` for a broad non-mesh remainder; do not sum
all columns. `vertex_ms` and `normals_ms` are subdivisions of `build_ms` (minor
loop/setup/timer overhead remains outside them). GPU draw execution and frame
presentation are not measured here.

Counters include regenerated `patches`, `vertices`, `upload_calls`, and
`upload_bytes`. A geometry rebuild counts one patch; a parameter-only upload
counts an upload but not a rebuilt geometry patch. Upload timing is not proof
that the GPU has completed the transfer, nor does it isolate bandwidth from
driver synchronization.

For the split CPU measurement, profiling mode uses two whole-patch passes over
the same output array: first allocate/fill heights, then calculate and pack
normals. It adds no temporary normal array and no per-vertex timer calls. The
ordinary, non-profiled builder retains its original interleaved loop. This makes
the split useful for identifying the dominant work, but the changed pass/cache
order means it is not an exact decomposition of an earlier interleaved timing.
The GL smoke test compares uploaded vertex bytes from both builders exactly.

`deferred` counts refresh attempts without an initialized paged mesh or current
OpenGL context. Work can then happen in a later render call. Such dirty paged
refreshes receive separate **`kind=mesh`** records with tile name, N and P.
Their `after_brush` value is the latest brush ID on that thread, a correlation
hint—not proof that all their dirty state came from that one event. Read these
records too; a small `paint_ms` with deferred work does not mean refresh is fast.

`legacy_refresh` indicates the legacy backend was asked to refresh. Its later
full rebuild is not split by this profiler; use the paged backend for this test.
First-ever mesh initialization is also outside the incremental patch breakdown,
so wait for initial terrain display before painting.

There is one summary per brush event, and one per standalone dirty tile refresh.
The old per-tile `Paged terrain refresh` debug message is suppressed while a
profile event is active to avoid duplicate logging. File logging happens outside
the measured event but still has some frame overhead: use stage timings, not
profile-mode FPS alone, when drawing conclusions.

## Verification

The opt-in correctness-suite smoke check runs with the same environment:

```powershell
$env:TSRE_TERRAIN_BRUSH_PROFILE = '1'
& .\build\TSRE5vc.exe --test --test-suite terrain-brush
```

It creates a small temporary tile and a real offscreen GL context, verifies
build/upload counters and actual undo snapshot counts, then exercises deferred
refresh. Its `smoke=true` records validate instrumentation only; they do not
represent an interactive rendered route or establish GPU performance.
The instrumented suite also checks nonzero vertex/normal sub-timings and their
containment within total build time. The non-profiled brush suite has 360 cases.
The updated instrumented suite passed 367/367 cases, including exact vertex
buffer byte parity between the ordinary and split builders.

Related: [height-brush performance task](../tasks/terrain/terrain-height-brush-performance.md)
and [height-area editing API](terrain-height-area.md).
