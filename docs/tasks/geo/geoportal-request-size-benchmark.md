# Geoportal request-size benchmark

Measured 2026-09-16, starting 16:32 UTC, from the local development machine.
Application dataset defaults remain unchanged at 512 m.

## Results

Three sequential requests per size/format, with rotated size and format order.
Times include connection/server wait and response transfer, excluding decoding.
Sizes below are decoded HTTP response payloads in MiB (1 MiB = 1,048,576 bytes).

| Core size | KRON86 numeric TIFF | EVRF2007 ASCII Grid |
|---|---|---|
| 512 m | 3/3 valid; 1.009 MiB; median **2.603 s** (1.893–10.268 s) | 3/3 valid; 5.182 MiB; median **13.211 s** (12.624–14.128 s) |
| 1024 m | 3/3 valid; 4.022 MiB; median **5.878 s** (5.818–5.941 s) | 0/3 completed; all timed out around **90 s**, before response headers/data |
| 2048 m | 3/3 valid; 16.043 MiB; median **22.843 s** (21.643–35.010 s) | 0/3 completed; HTTP 200 each time, interrupted at **120 s** with 23.989 / 69.346 / 65.583 MiB received |

All complete responses decoded successfully with EPSG:2180, exact 1 m spacing,
the requested dimensions and native-grid registration. The four shared test
posts had identical heights across all successful requests within each dataset.
TIFF decoder times were 1–31 ms; ASCII 512 m decoder times were 33–53 ms. Network
and service time dominate decoding.

The 2048 m ASCII responses started delivering data only after 47–53 seconds,
already exceeding the application's 45-second deadline. Two partial responses
also exceeded its 32 MiB response cap. HTTP 200 with a partial body demonstrates
that these requests were accepted, not that the full rasters were validated.
The 1024 m ASCII failures do not establish an inherent size restriction: only
these specific native bounds/request parameters were tested.

## Consequences for a TSRE tile

For an approximately axis-aligned 2048 m tile offset from the cache grid, a
typical block count is 25 / 9 / 4. Applying the measured TIFF median to those
counts gives the following **estimate**, not a measured full-tile benchmark:

| Core size | Typical requests | TIFF payload | Estimated sequential transfer time |
|---|---:|---:|---:|
| 512 m | 25 | 25.2 MiB | 65 s |
| 1024 m | 9 | 36.2 MiB | 53 s |
| 2048 m | 4 | 64.2 MiB | 91 s |

Actual counts depend on projection, orientation, sample positions and cache
reuse. Exact grid alignment instead gives 16 / 4 / 1 blocks. Neighboring route
tiles reuse cached blocks, making large-block overfetch less significant across
a larger contiguous area. The first 512 m and 2048 m TIFF requests were notably
slower than their repeats; neither service cache state nor Internet conditions
were controlled.

**Recommendation:** 1024 m is a reasonable TIFF default to reduce request count
without the larger overfetch and transfer time of 2048 m for an isolated tile.
Keep EVRF2007 ASCII at 512 m pending separate investigation of its larger-request
behavior. 2048 m TIFF is technically supported by the tested service responses,
and may suit large contiguous batches, but is not an automatic speed improvement.
Adopting it would also require raising the catalogue validator's current 1024 m
maximum. This benchmark does not change application settings, limits or defaults.

No explicit HTTP 429 or rate-limit response was observed. Eighteen sequential
requests cannot establish a provider's request quota or prove that throttling
never contributed to a timeout. No deliberate rate-limit stress test was run.

## Method and reproduction

- Uses `coverageUrl()` and the production TIFF/ASCII readers, built in Release.
- One persistent Qt network manager; sequential requests, no local disk cache.
- One projected grid cell near 52 N, 19 E. All sizes share the same upper-left
  corner, so smaller rasters are nested within larger ones.
- Core sizes 512 / 1024 / 2048 m include the production one-pixel margin on each
  side: actual dimensions are **514 / 1026 / 2050**. Every request explicitly
  specifies its output dimensions at 1 m spacing.
- Size orders: 512–1024–2048, 2048–512–1024, 1024–2048–512. Format order alternates
  by round. Repeated requests use identical URLs; server caches may affect them.
- Diagnostic limits: 90-second inactivity timeout, 120-second overall deadline,
  128 MiB response cap. Application limits remain 30 / 45 seconds and 32 MiB.
- An incomplete response is a failed benchmark sample. Timing statistics for
  complete rasters exclude those failures; their limits are shown separately.

The [raw results](evidence/2026-09-16/request-size-benchmark.json) preserve all
18 attempts, public request URLs, timings, sizes, errors and grid/sample checks.
Large raster bodies are not retained. The benchmark deliberately returns a
nonzero exit code when any request fails; the six ASCII failures above explain
this run's exit code.

Build and run instructions are in [tests/geo/README.md](../../../tests/geo/README.md).
