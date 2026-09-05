# Terrain height-area editing API

Implemented in [TerrainHeightArea.h](../../src/tsre/world/TerrainHeightArea.h)
and [TerrainHeightArea.cpp](../../src/tsre/world/TerrainHeightArea.cpp).

`TerrainHeightArea::getArea()` gathers terrain into a temporary float rectangle.
The caller edits the floats and calls `TerrainHeightArea::setArea()` to scatter
them back to native terrain samples. This is available to future batch,
neighbourhood and other terrain tools; it is not tied to a brush or renderer.

The ordinary editor height brush uses faster **direct tile processing** in
[TerrainHeightBrush.cpp](../../src/tsre/world/TerrainHeightBrush.cpp).
Its `Method::HeightBuffer` branch is also retained as a working get/edit/set
example for all four existing brush modes. The original brush remains under
`TerrainLibQt::paintHeightMapLegacy()` for benchmark comparison and fallback
on unusual grids, not as the default editor dispatch.

## Get, edit, set

```cpp
#include <tsre/world/TerrainHeightArea.h>
#include <tsre/Undo.h>
#include <cmath>

// Bounds are inclusive TSRE global metres. This example raises a rectangle.
// Caller has already checked route/app write permission.
auto area = TerrainHeightArea::getArea(library, minX, minZ, maxX, maxZ);
if (!area.supported)
    return;

for (float &height : area.heights) {
    if (std::isfinite(height))
        height += 2.0f;
}

// When an editor stroke already owns an undo state, reuse that state instead.
Undo::StateBegin();
QSet<Terrain*> changed = TerrainHeightArea::setArea(library, area);
Undo::StateEnd();
// Pass changed to any route-level object-update workflow required by the tool.
```

For a radial brush, use the same get/set calls and replace the editing loop:

```cpp
for (int z = 0; z < area.height; ++z) {
    for (int x = 0; x < area.width; ++x) {
        float &h = area.heights[std::size_t(z) * area.width + x];
        const double dx = double(area.originX) + x * area.spacing - centreX;
        const double dz = double(area.originZ) + z * area.spacing - centreZ;
        const double distance = std::sqrt(dx * dx + dz * dz);
        if (std::isfinite(h) && radiusMetres > 0 && distance < radiusMetres)
            h += strength * (1.0 - distance / radiusMetres);
    }
}
```

That is a simple example kernel, not an exact copy of the legacy brush's
snapping, arithmetic and mode semantics. The executable parity-tested brush
example uses the prepared-selection overload of `getArea()` because it already
needs native slices for its reference-height calculations. It also retains the
legacy mode-1 centre operation before gathering, including synthesized-border
ownership. Other callers need neither this exception nor brush-specific code.

## Area contents and coordinates

- `heights`: contiguous row-major floats, indexed `z * width + x`.
- `originX`, `originZ`: absolute metre position of array sample `(0,0)`.
- `spacing`: metres between samples in both directions.
- `width`, `height`: sample counts, independently sized; not metre dimensions.
- `slices`: selected terrain pointers, their physical bounds, native sample
  rectangles and commit bookkeeping. These exclude the synthesized N+1 border.
- `supported`: selection could be represented; test it before using the array.
- `committed`: the area has already been applied and cannot be applied again.

Global positions use **TSRE internal Z**, not the opposite-sign MSTS file Z:

```text
globalX = WorldTileX * 2048 + localX
globalZ = WorldTileZ * 2048 + localZ
```

The 2048 here is the World-file coordinate lattice, not terrain tile size.
Different physical terrain sizes are handled through each slice's bounds.
Requested bounds are rounded outwards to the selected sample grid, so callers
should use the returned origin/dimensions rather than assume their input bounds
are exact sample positions. Samples in this rounded margin are editable too;
use your kernel's own footprint if the original rectangle must be strict.

By default selection uses the finest discovered spacing and includes mixed
grids. A positive optional `spacingFilter` includes only terrain with that
native spacing. This is how a tool can retain the editor brush's current policy
of skipping differently spaced neighbours. The brush's default mixed-spacing
policy was not changed by the performance work.

Only already loaded, editable terrain participates. Missing, unloaded, filtered
or read-only portions are NaN, not artificial zero-height ground. There may be
partially available rectangles. Invalid/reversed bounds, no eligible terrain,
or unsupported participating sample spacing return `supported=false`; loading
of the terrain itself is unaffected. Supported area grids are aligned
power-of-two integer spacings through 1024 m. Overlapping/nonaligned custom
grids do not have a general editing policy in this API.

Coarse portions are bilinearly interpolated when gathering. Native positions
are copied exactly, avoiding roundoff in a gather/set with no edits. Border
interpolation uses the terrain's existing N+1 height data. `getArea()` does not
refresh those caches, generate meshes, capture undo, or mutate terrain.

## Commit and lifetime

`setArea()` selects each native sample's corresponding buffer entry; it does
not average a fine rectangle back onto coarse tiles. Editing only an extra
interpolated point therefore does not change a coarse native sample. A future
filter/downsampling tool must explicitly implement its desired averaging.

By default only changed finite native heights are written. An unchanged area
does not mark tiles modified, reset ErrorBias or trigger refresh. NaN and
infinity entries are ignored. Loaded/editable state is checked again at commit.

For each affected tile the API:

1. Captures heightmap undo before its first native write, in the caller's state.
2. Accumulates one changed-sample rectangle and a touched-patch mask.
3. Resets touched patch ErrorBias and marks the terrain modified.
4. Invalidates heights/normals and calls `refreshModified()` once for that tile.
5. Calls the terrain library's `updateTerrainHeightmap()` hook and returns the
   affected terrain set. Existing adjacent-edge notification is preserved.

The optional `area.touched` byte array has the same dimensions as `heights`.
If supplied, zero entries are excluded and nonzero native entries are marked
touched even when their height did not change. This exists for legacy brush
compatibility (including zero-alpha ErrorBias/modified-state behavior). Ordinary
new tools should leave it empty and commit only actual changes.

The area owns its temporary arrays but **does not own its terrain objects**.
It is move-only and single-use. Get, edit and set must happen synchronously on
the terrain/editor thread while those tiles and their layouts remain alive.
Do not retain it across tile unload/replacement, unrelated terrain edits, route
changes or asynchronous work. There is no conflict merge or persistent snapshot
protocol. Treat geometry/slices as read-only; do not resize `heights`.

The API does not open/close undo states, check global route/app write policy,
save route files, or perform route-level forest/transfer adjustments. Callers
must retain their normal permission and action lifecycle checks. The library
update hook retains its implementation-specific behavior (including a client's
network update hook); the API does not replace it with its own transport.

## Tests and comparison

`--test --test-suite terrain-brush` exercises legacy/direct/buffer/editor parity,
all four brush modes, undo, mixed layouts, rectangular public-API usage,
non-finite and unavailable samples, no-op/single-use commit, and read-only state.
The opt-in `terrain-brush-benchmark` compares the three algorithms on `ularge`.
See [the performance task](../tasks/terrain/terrain-height-brush-performance.md)
for measurements and the remaining GPU-inclusive interactive/undo work.

The float rectangle at size-50/1 m brush resolution is 801 x 801 floats
(approximately 2.45 MiB). The optional brush footprint adds 641,601 bytes
(approximately 0.61 MiB); ordinary area users do not allocate it.
