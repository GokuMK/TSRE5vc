# Procedural terrain map resolution and bake sampling

Status: Open Rails dynamic-resolution/detail prototype and runtime validation
complete. TSRE native-resolution map/backend and optimized bake sampling are
implemented and tested; the direct TSRE GPU renderer remains deferred until the
Gather terrain contract is available.

## Objective

Remove the fixed 4096 x 4096 procedural-material-map assumption while keeping
painting, undo, near rendering and saved distant fallback practical on a range
of GPUs. Also stop generating every distant patch miniature through a full
512-square intermediate when a much smaller antialiasing source is sufficient.
Prepare the data model for a later Open-Rails-style direct material-ID renderer,
where the ID map and source/detail textures are sampled by the terrain shader
instead of first generating one RGB near texture per patch.

This task changes resolution handling and bake sampling. It does not redesign
the material catalogue, procedural brush semantics or terrain mesh layout.

## Findings

- The version-1 `.pmap` header already stores width and height. The fixed size is
  an implementation restriction, not a file-format restriction.
- `TerrainMaterialMap` formerly assumed `Side == 4096` in allocation,
  addressing, painting, generation and undo-related paths. It now retains a
  validated per-map side while keeping 4096 as the new-map default.
- TSRE currently generates CPU RGB patch textures and uploads those results. It
  does not yet upload the `.pmap` itself as the terrain material-selection
  texture. GPU map caps and dirty map uploads below belong to the planned direct
  renderer, not the current CPU patch generator.
- An 8-bit material-ID texture consumes 4 MiB at 2048 square, 16 MiB at 4096
  square and 64 MiB at 8192 square. Nine simultaneously resident maps would
  therefore consume approximately 36, 144 or 576 MiB respectively, before
  other terrain resources.
- A local AMD/OpenGL upload measurement found full-map uploads of approximately
  6.4 ms for 4096 square and 18.3 ms for 8192 square. Dirty 64-512-square
  rectangle uploads remained below approximately 0.4 ms. Interactive painting
  should therefore upload dirty rectangles rather than a complete map.
- The existing distant-bake path generates a 512-square near image even when
  the final patch region is only 64 square for P16 or 32 square for P32. This
  was useful for minification and moire reduction, but it performs substantially
  more sampling than the saved output needs.
- Open Rails prototype measurements on three test tiles found material-map
  decompression around 13-14 ms and a complete 4096-square coverage scan around
  67-68 ms per tile. A deliberately overloaded test patch reached six material
  passes and exercised the five-pass fallback; it is evidence that the cap and
  fallback work, not evidence that five is the universally optimal cap.
- In Open Rails this loading work runs on its loader thread during initial load
  and when a new 2048 m terrain tile enters after a tile-boundary crossing. It
  is not executed every frame or for movement within one tile. Runtime updater
  and renderer code do not call `LoaderProcess.WaitTillFinished()`; the old
  terrain list remains active until the loader replaces it. Texture creation
  still uses the shared graphics device, and large managed allocations can cause
  GC, CPU or memory-bandwidth contention, so an indirect hitch remains possible.
- A later three-repetition matrix measured 81 tile preparations on the local AMD
  feature-level-10 system. Median per-tile totals were about 38-98 ms at native
  2048, 95-156 ms at native 4096 and 385-467 ms at native 8192. The original
  dominant-ID runtime reduction was not a cheap safeguard: 8192-to-4096
  reduction alone required roughly 491-507 ms per tile, and 8192-to-2048 roughly
  390-409 ms. Center-nearest remeasurement reduced those stages to 75.2 ms and
  19.0 ms respectively; 4096-to-2048 fell from 125-131 ms to 20.4 ms. These are
  process-cold Open Rails texture-manager measurements with a warm Windows file
  cache. A native 8192-square one-tile boundary test produced no perceptible
  loading hitch. That test also exposed a procedural-to-baked transition flash:
  terrain resources were released during the same loader sweep that retired
  them. Retaining outgoing procedural resources for one additional sweep fixed
  the flash, and the normal 3 x 3 procedural area was restored. Quantitative HUD
  boundary P95/P99 capture remains optional supporting data rather than a
  blocker for this checkpoint.

## Selected direction

### Work allowed before the Gather merge

The Open Rails prototype can move first because its procedural terrain shader
already samples the material-ID map directly. Before choosing a default runtime
cap, add dynamic 2048/4096/8192 loading there and measure the user-visible
result rather than rejecting 8192 from estimates alone.

Implementation status (2026-10-02): the Open Rails branch now accepts all three
sizes, reduces categorical maps to a conservative feature-level cap before
coverage analysis, uploads the effective size, and logs each preparation stage.
The benchmark commands, input-resize helper and results table are prepared in
`openrails-workspace/reviews/procedural-terrain-textures/`. The initial load
matrix is complete there. A temporary 1 x 1 procedural-area test with a native
8192-square map found no perceptible boundary hitch; the accepted implementation
has been restored to its normal 3 x 3 procedural area.

Measurements must separate:

- bounded decompression;
- native-to-effective categorical reduction, when enabled;
- patch coverage/pass-list collection;
- source/detail texture loading;
- material-map GPU upload;
- total loader operation;
- maximum and percentile frame time while repeatedly crossing the relevant
  2048 m tile boundary.

Open Rails performs this work on its loader thread and does not explicitly make
runtime rendering wait for completion. The decisive result is still the observed
frame behavior because graphics-device upload, GC and resource contention cross
thread boundaries. Compare equivalent 2048, 4096 and 8192 maps with the same
painted content, including cold and warm filesystem/texture-cache runs. Keep the
deliberately greater-than-five-material patch as a fallback-correctness stress
case, not as representative evidence for choosing the normal pass cap.

The shared route material catalogue can also gain optional detail properties
without waiting for Gather:

```text
Material (
    UiD ( 1 )
    Name ( "Grass" )
    Texture ( "grass.ace" )
    DetailTexture ( "microtex.ace" )
    DetailScale ( 32 )
)
```

- Missing `DetailTexture` means `microtex.ace`.
- Missing `DetailScale` means `32`.
- Newly created TSRE materials write those defaults explicitly.
- Detail texture names use the same safe route-relative `TERRTEX` filename and
  seasonal resolution rules as other terrain material inputs.
- Scale must be finite and positive. Disabling detail entirely is not introduced
  implicitly in this step; it needs an explicit future representation if wanted.

Open Rails loads and renders the effective per-material detail texture and scale.
TSRE loads, preserves and saves both fields, but its current CPU-generated
near renderer intentionally continues to bind the hardcoded `microtex.ace` and
scale 32. This prevents a second temporary renderer implementation immediately
before Gather. TSRE's direct material-ID renderer will begin using the catalogue
fields after the Gather terrain pass is available.

Changing a detail property must participate in catalogue revision/cache
invalidation in both programs. It must not require repainting the `.pmap`, whose
bytes store only local material IDs.

Keep catalogue `Version ( 1 )`. `DetailTexture` and `DetailScale` are optional
material fields, and their absence has exactly the same effective behavior as
the historical catalogue: `microtex.ace` at scale 32. A new version would not
identify a different interpretation or migration requirement. Updated readers
accept the optional fields; updated writers emit them for newly created
materials and preserve their effective values on later saves.

Implementation status (2026-10-02): both readers accept and validate these
optional Version-1 fields. TSRE writes their effective values on save and gives
new materials the documented defaults. Open Rails uses them in its direct
procedural terrain pass. TSRE renderer consumption remains intentionally
deferred to the Gather renderer integration.

### Native and effective resolution

Accept square, power-of-two maps from 2048 through 8192. Preserve the native
file resolution and select an effective runtime resolution as:

```text
min(file resolution, configured maximum, GPU maximum)
```

The configured maximum is a future graphics/performance setting. Downscaling
is a runtime operation and must not silently rewrite the route's source map.
Keep 4096 as the initial authoring default unless measurements justify changing
it.

Actual hardware limits must win over the setting. A conservative compatibility
policy is 2048 for D3D feature level 9.1-class hardware, 4096 for 9.3-class
hardware and up to 8192 for level 10 or newer hardware, subject to the queried
texture limit and memory policy.

### Categorical downscaling

Material IDs are categories, not colors. Never resize them with linear
filtering or average their byte values. Runtime compatibility reduction favors
speed: select one deterministic center-nearest source ID for each output pixel.
Evaluate the reduced map for renderer coverage/pass lists so those lists
describe the data actually uploaded and sampled. An offline authoring tool may
offer a slower dominant-ID filter separately when preservation is preferred.

Initially it is acceptable to decode the bounded native map and then reduce it.
Prefer combining reduction and coverage collection so the reduced result is not
scanned again. Release the native decoded bytes after the reduced runtime map
and required editor state have been established. Streaming decompression or
row-wise reduction is a later optimization if 8192 load-time memory or latency
requires it.

The future direct TSRE renderer needs a deliberate distinction between the
authoring map and the GPU map. Painting and saving operate on the native
authoring resolution. The renderer may upload a capped representation. Dirty
authoring rectangles must be converted to the corresponding
effective-resolution rectangles before GPU updates. Until that renderer exists,
the current CPU generator samples the native authoring map directly.

### Distant bake sampling

Keep detailed near generation at its configured output size. For a bake-only
cache miss, generate at twice the final patch-region resolution and reduce once:

- P16 with a 1024-square tile bake: generate 128 square, reduce to 64 square;
- P32 with a 1024-square tile bake: generate 64 square, reduce to 32 square.

If the full near image is already valid and resident on the CPU, it may still be
reused. Otherwise, do not create a 512-square temporary merely to produce the
distant miniature.

The former full-`OutputSide` regeneration remains available through
`TerrainMaterialMap::BakeSampling::FullOutput`. The optimized 2 x mode is the
default for missing bake recipes; it does not remove the full path used for
quality comparison, regression diagnosis or deliberately high-quality rebakes.

The 2 x 2 reduction must evaluate four categorical material samples, sample the
corresponding material colors/detail inputs, and average the resulting colors.
It must not average IDs. This retains basic antialiasing while reducing generated
sample counts by 16 times for P16 and 64 times for P32 relative to a fixed
512-square intermediate.

Local Debug microbenchmark (2026-10-03, 20 warm cache-miss runs per case):

| Patch layout | Full `OutputSide` path | Optimized 2 x path | Speed-up |
|---|---:|---:|---:|
| P16 | 16.23 ms | 2.33 ms | 7.0 x |
| P32 | 15.20 ms | 1.67 ms | 9.1 x |

Each run rebuilt one dirty patch region into an existing 1024-square bake and
therefore includes recipe hashing, output allocation/copy and reduction, not
only the categorical sampler. These are local comparative measurements, not a
promise for route-save latency on every machine.

## Implementation stages

Backend stages, independent of the Gather renderer:

1. [x] Replace compile-time map-side assumptions with validated instance dimensions
   in loading, addressing, painting, CPU generation, undo and save code.
2. [x] Add tests for 2048, 4096 and 8192 maps, invalid/non-square dimensions and
   bounded decompression.
3. [x] Make bake-only generation accept the required target/oversampling size and
   use the 2 x 2 path when no reusable near image exists.

Renderer-dependent stages, after the Gather terrain-shader contract is settled:

4. [ ] Add an effective GPU-resolution cap and deterministic categorical reduction.
5. [ ] Add direct terrain material-ID rendering with material-specific base/detail
   inputs. Upload complete maps only on load/reconfiguration and use dirty
   rectangles for ordinary painting.
6. [ ] Retain the saved distant bake and compatibility path; the direct renderer is
   the detailed path, not a reason to remove the MSTS-compatible fallback.
7. [ ] Benchmark loading, paint latency, undo memory, full save and incremental save
   at every supported size before exposing the maximum-resolution setting.

## Acceptance checks

- Existing 4096 maps render, paint, undo and save without migration.
- 2048 and 8192 maps round-trip without changing their source dimensions.
- Capped maps show only valid source material IDs and stable tie behavior.
- A low hardware/configuration cap never requests an oversized GPU texture.
- Small paint strokes do not trigger a full-map GPU upload.
- P16/P32 bake miniatures match the established orientation and patch boundaries.
- High-frequency detail textures are compared for moire against the previous
  512-square-intermediate path; 2 x 2 supersampling is retained unless that test
  shows a material regression.
- Peak CPU and GPU memory, initial load time and tile-boundary hitch time are
  recorded for 2048, 4096 and 8192 inputs.

Automated checkpoint (2026-10-03): the normal terrain-material suite passed 604
checks. The benchmark form passed 610 checks and recorded the comparison above.
The complete CTest set remained green after the implementation.

## Renderer-branch coordination

Review on 2026-10-02 found `feature/gather-renderer` based directly on current
`main`, ten commits ahead. It changes `Terrain.cpp/.h`, `RenderItem`,
`Renderer`, `OpenGL3Renderer`, terrain submission and packet lifetime. Its pass
buckets are implemented, but the dedicated terrain shader/per-pass shader stage
is explicitly still pending.

The backend stages above can be implemented on `main` with little overlap if
they remain inside `TerrainMaterialMap`, `TerrainProceduralMaterial` and their
tests. The direct ID-map renderer should not be independently implemented on
`main` and then handed to the Gather branch for synchronization: it would alter
the same packet state, texture binding and terrain pass that Gather is currently
establishing. Merge Gather first, or base the renderer-dependent terrain work on
Gather after its terrain-shader contract is agreed. Avoid independently changing
both legacy and Gather material-binding interfaces when one retained terrain
resource/pass API can serve the final renderer.
