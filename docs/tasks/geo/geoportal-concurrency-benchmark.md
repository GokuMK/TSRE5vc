# Geoportal equal-area concurrency benchmark

Measured 2026-09-16 on the local development machine, following the
[request-size benchmark](geoportal-request-size-benchmark.md). This adds an
opt-in benchmark and evidence; application download behavior is unchanged.

## Result

For KRON86 numeric GeoTIFF, four neighboring 1024 m requests in parallel were
substantially faster than both four sequential requests and one 2048 m request.
Each strategy covers the same 2048 x 2048 m core at native 1 m resolution.

The comparison with fresh connections at the start of each batch returned valid
results in all three rounds:

| Strategy | Round 1 | Round 2 | Round 3 | Median | Total response payload |
|---|---:|---:|---:|---:|---:|
| Four sequential 1024 m requests | 23.682 s | 24.572 s | 28.234 s | **24.572 s** | 16.088 MiB |
| Four simultaneous 1024 m requests | 7.966 s | 7.150 s | 7.940 s | **7.940 s** | 16.088 MiB |
| One 2048 m request | 20.363 s | 21.425 s | 20.399 s | **20.399 s** | 16.043 MiB |

These are measured wall times until the whole batch has downloaded, excluding
subsequent decoding and validation. Parallel retrieval was **3.09x faster than
sequential** and **2.57x faster than one large request**, using medians. It was
closer to the time of one 1024 m request than either full-area serial strategy.
The small byte difference comes from extra margins and per-response metadata.

All complete rasters had the expected EPSG:2180 registration, dimensions and
1 m spacing. After removing margins and assembling the four quadrants, every
one of the **4,194,304 float heights** matched the single-raster result: all
successful batch results shared the same SHA-256 hash.

The parallel requests genuinely overlapped: all four were launched together and
all four received body data before the first finished. Qt reported no HTTP/2
use; these results therefore did not depend on HTTP/2 multiplexing.

## Connection-reuse control

An initial run retained the Qt connection pool between batches. It produced:

| Strategy | Round 1 | Round 2 | Round 3 |
|---|---:|---:|---:|
| Four sequential 1024 m requests | 26.286 s, valid | 23.681 s, valid | 22.877 s, valid |
| Four simultaneous 1024 m requests | 7.880 s, valid | 29.993 s, failed | 7.681 s, valid |
| One 2048 m request | 21.626 s, valid | 30.000 s, failed | 19.236 s, valid |

In the failed parallel batch, one request completed and three received no
response before the 30-second transfer timeout. The next single-raster request
also timed out without a response. Other connections had been idle while one
connection handled the longer 2048 m transfer.

To investigate, the second run cleared idle Qt connections before every batch,
while preserving reuse within each sequential batch. All 27 requests in that
run succeeded. The observations are consistent with an idle-connection reuse
problem, but do not establish its exact client/server/proxy cause. Changing
network/server conditions cannot be excluded. Both runs are retained rather than
discarding the failed attempts.

The current application constructs a WCS network manager per generation job.
Fresh-per-batch measurements are therefore relevant to a possible implementation
with four concurrent requests within one job. A future implementation should
retain bounded concurrency, cancellation and per-request failure handling, and
should test idle connection reuse if manager lifetime is extended across jobs.

## Method and limitations

- One 2048 m native-grid cell near 52 N, 19 E; same area for every strategy.
- TIFF only. These measurements do not establish ASCII endpoint behavior.
- Production `coverageUrl()` and numeric GeoTIFF reader, compiled in Release.
- Each response has the usual one-pixel margin: four 1026-square rasters versus
  one 2050-square raster. No application disk cache is involved.
- Three rounds rotate order: sequential/parallel/single, parallel/single/sequential,
  single/sequential/parallel. Server-side cache state is uncontrolled.
- One Qt network manager issues all four parallel requests asynchronously. No
  extra libraries or worker threads are needed for the benchmark.
- Decoding starts after all network requests in a batch finish, so decoder work
  cannot serialize otherwise concurrent transfers.
- Production limits: 30-second transfer timeout, 45-second per-request deadline,
  32 MiB per-response cap. No retries. Maximum four outstanding requests.
- Two complete runs: 54 HTTP attempts total. The initial reused-pool run had
  four failed requests; the fresh-connection run had none.
- This is a small comparison on one machine/area, not a provider quota or maximum
  concurrency test. No explicit rate-limit response was observed. No guarantee
  is made about sustained whole-route throughput or other network conditions.

**Recommendation:** bounded concurrency of four is worth implementing for the
TIFF provider. The current fixed cache grid can retain its reuse benefits while
reducing time spent downloading multiple blocks. This experiment compares 1024 m
blocks; it does not yet decide whether concurrent 512 m or 1024 m blocks are the
best default for a whole route.

## Reproduction and evidence

Build `tsre_elevation_benchmark` as described in [tests/geo/README.md](../../../tests/geo/README.md),
then run either:

```text
tsre_elevation_benchmark --concurrency reused.json
tsre_elevation_benchmark --concurrency-fresh fresh.json
```

- [Initial reused-connection run](evidence/2026-09-16/concurrency-reused-benchmark.json)
- [Fresh-connection control](evidence/2026-09-16/concurrency-fresh-benchmark.json)

Raw results include per-request URLs, start/first-byte/completion times, bytes,
HTTP status/protocol flags, grid validity, batch timings and complete-core hashes.
No downloaded raster bodies are retained. The initial run returned failure due
to its two incomplete batches; the fresh-connection control exited successfully.
