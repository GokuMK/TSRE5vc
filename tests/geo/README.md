# Elevation checks

The normal `tsre_elevation_tests` target is offline. Configure this directory
with Qt Core and Network, build, then run `ctest --test-dir <build-dir>`.

## Explicit live checks

`tsre_elevation_tests --live <temporary-geodata-dir> <dataset-id>` prepares two
points through the actual production cache/provider pipeline. Repeating it
checks persistent cache reuse.

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
