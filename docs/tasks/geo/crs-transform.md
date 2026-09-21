# CRS transform extraction and shared projection path

Updated 2026-09-21. The first milestone is implemented on the geo-terrain
branch. Later milestones remain design tasks.

## Goal

Keep coordinate conversion separate from raster decoding and acquisition, while
preserving both requirements TSRE already has:

- elevation lookup needs a small, fast forward conversion for every terrain
  sample;
- a route projection needs accurate forward and inverse conversion across routes
  that may span hundreds of kilometres.

The code may eventually be reusable outside TSRE, but the current class is an
internal TSRE component with only the operations the application uses. It is not
intended to reproduce an EPSG database or datum-transformation framework.

## Milestone 1: extraction

`src/tsre/geo/CrsTransform.h/.cpp` now owns the elevation-side conversions that
were previously mixed into `ElevationRaster.cpp`.

The public surface is deliberately small:

```cpp
Geo::CrsTransform transform(epsg);
if (transform.valid())
    transform.forward({latitude, longitude}, projectedPoint);
```

The transform is constructed once per elevation source and reused for its sample
points. Construction precomputes ellipsoid and series constants. `forward()` is
allocation-free, has no Qt dependency and performs no EPSG lookup, file access or
network access. Catalogue validation uses the same class through `supports()`.

The initial definitions remain supported, and the source work added two explicit
national conversions:

- EPSG:4326 geographic longitude/latitude;
- EPSG:3857 Web Mercator;
- EPSG:2056 Swiss LV95 through the official bounded approximation;
- EPSG:3035 ETRS89 / LAEA Europe;
- EPSG:2180 Poland CS92 and EPSG:3794 Slovenia D96/TM;
- EPSG:3045, EPSG:3067 and ETRS89 / UTM zones EPSG:25828..25838.
- EPSG:2169 Luxembourg TM with the official ETRF2000-to-LUREF2020
  Molodensky-Badekas conversion;
- EPSG:27700 British National Grid pseudo-projection plus an injected regular
  horizontal shift grid, currently the official OSTN15 Lite asset.

Only geographic-to-projected conversion is exposed. Input and output order are
always explicit TSRE types: latitude/longitude input and easting/northing output,
including EPSG definitions whose authority axis declaration uses another order.
Each compiled-in definition keeps a bounded area of use.

`ElevationRaster` now contains raster parsing, sampling and NoData handling. It
uses aliases for the common geographic/projected point types so this extraction
does not require a broad terrain API change.

## Route projection remains unchanged in milestone 1

`GeoTsreTransverseMercatorCoordinateConverter` retains its current sixth-order
Engsager/Poder forward and inverse series. It also precomputes its coefficients,
scale and origin northing in the constructor. This is the stronger implementation
needed for scenery and track coordinates over long routes.

The elevation transform retains its previous fourth-order forward series during
the extraction. Current source domains are bounded, and the independent reference
grids already require sub-millimetre agreement. Keeping the formula stable makes
this milestone a code-ownership and precomputation change rather than a silent
numeric migration.

## Milestone 2: one Transverse Mercator kernel

Extract the route converter's mathematical core into a Qt-independent value type
used by both route and elevation adapters. Do this as a separately reviewed
numeric change.

The kernel should accept explicit ellipsoid and projection parameters rather than
EPSG numbers:

- semi-major axis and inverse flattening;
- central meridian and scale factor;
- false easting and false northing;
- sixth-order forward coefficients and inverse coefficients.

The elevation adapter will continue to map the small compiled-in EPSG set to
those parameters and enforce each dataset's area of use. The route adapter will
continue to apply TSRE's origin northing and tile/world conventions outside the
mathematical kernel. This keeps an EPSG catalogue, route coordinates and raster
coordinates as three distinct responsibilities.

Before switching either caller:

1. Compare the shared forward path against all existing independent PROJ
   reference grids, including edge points rather than only projection origins.
2. Preserve or improve the current 1 mm elevation test tolerance.
3. Test route forward/inverse round trips at the origin and across representative
   routes hundreds of kilometres long, including points on both sides of the
   central meridian.
4. Benchmark construction separately from repeated conversion. Measure at least
   65,536 forward points in a Release build using one preconstructed transform.
5. Compare the shared sixth-order path with the current route implementation and
   the extracted elevation implementation before removing either duplicate.

There should be no timing assertion in the ordinary test suite. Record measured
Release results in this document when the milestone is performed. The expected
fast path remains a few trigonometric operations and a fixed six-iteration series,
with no heap allocation, locks or dynamic dispatch per point. If terrain profiling
later shows conversion as material, add a batch API before introducing parallelism
inside this small component.

## Possible later scope

Additional formulas can be added as small explicit strategies when the source CRS
and datum relationship are fully understood. The class now accepts one generic
regular horizontal shift grid, but does not perform file access; the elevation
provider downloads and parses OSTN15 Lite once, then injects its numeric values.
Vertical datum conversion, operation selection and a broad CRS catalogue remain
outside this class. Those needs are the decision boundary for the deferred
[PROJ integration](proj-integration.md).

If this code later becomes a separate `miniproj`-style project, first stabilize a
parameter-based API and add independent reference data. Decide its standalone
licence and ownership before moving code out of TSRE; the current extraction is
part of the TSRE codebase.

## Milestone 1 acceptance

- [x] Projection code no longer lives in the raster decoder.
- [x] Elevation sources reuse a preconstructed transform.
- [x] Existing supported EPSG set, bounds and axis behavior are preserved.
- [x] No new runtime or build dependency is introduced.
- [x] Focused geo tests pass in Release after the Luxembourg/Wales extension (378 checks).
- [ ] Sixth-order shared TM kernel is implemented and benchmarked.
- [ ] Route projection is migrated to the shared kernel without accuracy loss.
