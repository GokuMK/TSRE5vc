# Czech DMR 4G WCS evidence

Captured 2026-09-16 from the official CUZK service. See the
[Stage A report](../../../czech-wcs-validation.md) for interpretation and sources.

- `capabilities.xml`: WCS 2.0.1 GetCapabilities.
- `description-0.xml`: DescribeCoverage for `MD_LAS`.
- `sample-native.bin`, `adjacent.bin`: raw multipart responses for adjacent 32 x
  32 grids at 5 m spacing. Matching `*-url.txt` files preserve requests.
- `outside.bin`: empty coverage inside the advertised bounding rectangle;
  TIFF uses a sparse tile (offset/count zero).
- `border.bin`: 256 x 256 request near the western border. Mixed populated,
  zero-valued and sparse tiles. Matching URL file preserves the request.
- `130_86.bin`: production 258 x 258 Prague request with a one-pixel halo.
- `76_213.bin`, `76_214.bin`: production empty-coverage blocks for the Germany
  fallback test. Each numbered JSON file is the actual cache metadata, including
  request URL, retrieval time and payload hash.
- `projection-reference.json`: 40 development-only PROJ/pyproj control points
  for GRS80 Transverse Mercator, lon_0=15, lat_0=0, k=0.9996,
  x_0=500000, y_0=0. Internal coordinates are easting/northing.
- `live-results.json`: production-path console results for cache reuse, three
  additional Czech cities, missing coverage with synthetic HGT, and Poland.
  Prague's first request succeeded in 911 ms with one download; its repeat is
  recorded in the log. Timings are single probes, not a throughput benchmark.
- `sha256.json`: hashes of retained responses, metadata and the live log.

`.bin` files are complete MIME responses, not bare TIFFs. Byte preservation is
intentional; checked-in attributes disable line-ending conversion for evidence.
Numeric fixtures are used by the small standalone `tests/geo` target.
