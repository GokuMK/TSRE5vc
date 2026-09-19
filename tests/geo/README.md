# Elevation checks

> R1/R2 follow-up (2026-09-19): catalogue growth no longer stops the suite.
> Standalone elevation: 301 passing checks; Settings: 232 passing cases;
> elevation UI: 44 passing cases. Release build passed. See
> [current review and remaining validation gaps](../../docs/tasks/geo/elevation-current-status.md).

The normal `tsre_elevation_tests` target is offline. Configure this directory
with Qt Core and Network, build, then run `ctest --test-dir <build-dir>`.

## Explicit live checks

`tsre_elevation_tests --live <temporary-geodata-dir> <dataset-id>` prepares two
points through the actual production cache/provider pipeline. Repeating it
checks persistent cache reuse.

`tsre_elevation_tests --live-area <temporary-geodata-dir> <dataset-id>` samples
a 16 x 16 grid over approximately 2 x 2 km near 52 N, 19 E. It exercises multiple
production download batches and prints block/cache counts and total elapsed time.
The offline suite also tests the production download transport against a local
HTTP server, including four concurrent replies, cancellation and failure limits.

## Choosing coordinates and checking downsampling

```text
tsre_elevation_tests --live-at <temporary-geodata-dir> <dataset-id> <latitude> <longitude>
```

Use `--live-at` for a national/regional source outside Poland; `--live` and
`--live-area` use locations near 52 N, 19 E. The current CLI passes output spacing
`1.0` to `generate()` in every mode. Its Polish/Czech probes therefore do not
exercise the new fine-to-coarse area filter used by coarser editor terrain grids.
The `--live-area` point layout is also independent of that spacing argument.

The new WCS 1.0 / uint16 / CRS and filter regression cases still need to be added;
see the review's validation checklist. Keep any service downloads explicitly
opt-in, and retain URL, CRS/grid, NoData and cache-hit evidence per dataset.

## WCS request-size benchmark

Build the optional target explicitly:

```text
cmake --build <build-dir> --target tsre_elevation_benchmark
<build-dir>/tsre_elevation_benchmark --live <results.json>
```

Use a Release configuration for meaningful decoder timings. This makes 18
sequential network requests: 512, 1024 and 2048 m cores, both configured Geoportal
formats, three rounds with rotated order. The bounds are nested within the same
2048 m native-grid cell near 52 N, 19 E. All requests include the production
one-pixel margin and explicitly request one sample per metre.

The benchmark bypasses the application cache, records each result immediately,
and checks dimensions, CRS, registration, spacing and four shared height posts.
It uses the production request builder and decoders. Server-side cache state is
not controlled. It is a size/latency probe, not a rate-limit stress test.

Diagnostic limits are deliberately larger than production: a 128 MiB response
cap, 90-second inactivity timeout and 120-second overall deadline. Results flag
responses exceeding the application's current 32 MiB / 45-second limits. A
timeout is not evidence that the server rejects that raster size.

No raster downloads are retained and no application settings or dataset sizes
are changed. Results include public request URLs and timing/grid metadata only.
The benchmark is excluded from normal builds and from `ctest`.

## Equal-area concurrency comparison

```text
<build-dir>/tsre_elevation_benchmark --concurrency <results.json>
```

This compares KRON86 numeric TIFF over one 2048 m core: four neighboring 1024 m
blocks sequentially, the same four blocks with four outstanding requests, and
one 2048 m request. Three rounds rotate the strategy order, for 27 HTTP requests
in total. A single Qt network manager retains connections between groups;
there is no application disk cache. The same one-pixel margin is used throughout.

Each group records its full network wall time and per-request launch, first-byte
and completion times, HTTP/2 use, status and bytes. Decoding happens after the
group finishes, so it cannot block another download. All grids are validated;
SHA-256 hashes compare every float in the assembled 2048 x 2048 core across
strategies and rounds. This uses the application's 30-second transfer timeout,
45-second per-request deadline and 32 MiB per-response cap.

Server cache and network conditions are uncontrolled. This is a bounded
four-request comparison, not a search for the service's concurrency/rate limit.

Use `--concurrency-fresh` instead of `--concurrency` to clear idle connections
before each group. This control helps distinguish connection-reuse problems
from concurrency/size effects, while still permitting connection reuse between
the four sequential requests within a group. The connection policy is recorded
in the output JSON. Compare failed attempts as well as successful timings.
