# Critical open issue: distant-terrain elevation acquisition

Recorded 2026-09-19 after user testing. Brief source review at `b5fe6f1`.
Documentation only: no runtime changes, service audit, downloads, builds or tests.

## Problem

Distant terrain covers roughly 32 x 32 km per tile. Downloading that area at the
same 1 m resolution used for detailed terrain is excessive when the output needs
32 m spacing at best, often coarser. Consequences include long generation times,
large caches, service rejections and memory/processing limits.

**Required direction:** assess each dataset for distant-terrain suitability.
Give suitable entries a separately configured coarse acquisition profile and
cache identity; deny unsuitable or unverified entries in distant mode before
starting requests. Keep adding ordinary country entries, but do not treat their
detailed-terrain validation as distant-terrain approval.

## Short implementation review

- `TerrainLibQt::saveEmpty(int x, int z)`, when the current quadtree is low-detail,
  creates distant tiles with 256 samples at 128 m
  spacing: **32,768 m per side**. Loaded tile layouts can differ; use actual
  sample spacing rather than assuming all distant terrain needs 32 m or 128 m.
- Both `TerrainLibQt::setHeightFromGeoGui()` and `setHeightFromGeo()` pass sample
  count and physical size to the same HeightWindow. `HeightWindow::load()` sends
  the resulting output spacing to `Elevation::generate()`, but no explicit
  detailed/distant purpose accompanies it.
- `ElevationSource.cpp` computes download block bounds from catalogue
  `resolution` and `blockPixels`. WCS and ArcGIS requests retain that grid.
  `targetSpacing` controls subsequent local averaging only; it does **not**
  reduce download resolution, bytes or the number of fine cache blocks.
- At 1 m, a 32,768 m square contains 1,073,741,824 source pixels: **4 GiB for
  Float32** (2 GiB for UInt16), before halos and metadata. A fully aligned square
  needs 1024 blocks with 1024-pixel cores; rotation/alignment can increase the
  covered area and request count. These are size estimates, not live benchmarks.
- The same area at 32 m contains 1024 x 1024 pixels, **4 MiB of Float32**; at
  128 m it contains 256 x 256, **256 KiB**. Actual cache overhead depends on the
  chosen block grid, alignment and service limits.
- Increasing `resolution` while retaining 1024-pixel cores is insufficient:
  a 32 m request with the current halo is **32,832 m wide**. Services can limit
  physical extent as well as pixel dimensions; choose a suitable block size too.
- `MaxBlocks=2048` is only an emergency ceiling, not a practical download budget.
  The NoData `fill` mosaic has a 32 Mi source-pixel bound, checked **after block
  acquisition**. At fine resolution, distant generation could download a large
  area and then fail this check. Add preflight budgeting before acquisition.
- Cache paths currently distinguish dataset definitions, but there is no
  distant-specific acquisition profile. Any effective resolution/grid overrides
  must participate in cache identity; coarse data must never be read as fine data.

## Required follow-up

- [ ] Pass explicit detailed/distant purpose through the common generation path,
  preserving actual terrain sample spacing as a separate value.
- [ ] Add per-dataset distant eligibility and acquisition configuration. Default
  unreviewed entries to disabled in distant mode. Show a clear reason before HTTP;
  do not silently use the expensive detailed profile.
- [ ] For enabled entries, record a validated coarse resolution, block dimensions,
  concurrency and any distinct overview/coverage needed by the service. Prefer
  server-side resampling or an appropriate coarse product over fine downloads.
- [ ] Give distant rasters a separate cache namespace or equivalent effective-grid
  identity, including resolution, alignment, CRS, coverage and response format.
  Keep original detailed caches usable and isolated from coarse results.
- [ ] Preflight request count, physical request extent, response dimensions/bytes,
  and processing/fill memory. Evaluate both per-request and whole-tile costs.
- [ ] Audit **every enabled WCS and ArcGIS dataset** for output resolution, service
  limits, actual returned grid, numeric heights, NoData and sensible resampling.
  Capability advertisements alone are insufficient. Record tested / disabled /
  pending status and reasons; no service receives approval from this review.
- [ ] Test a complete distant tile, offset/rotated alignment, adjacent-tile seams,
  cancellation, cache reuse/isolation, missing coverage and NoData filling. Verify
  that unsupported entries fail before downloads and detailed terrain is unchanged.

No distant restriction is implemented yet. This remains a critical open issue
while work continues on additional country services.
