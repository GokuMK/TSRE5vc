# TSRE5vc Geo Projection Update — Transverse Mercator

> Scope: the committed projection milestone through `a88f7f8`. The
> [2026-09-19 elevation review](elevation-current-status.md) explains how route
> projection and raster CRS lookup fit together. Subsequent New Route work is
> in progress separately; the manual observations below were not rerun in that review.

## Purpose

This document summarizes the geo-projection work implemented in `TSRE5vc` in September 2026.

The goal was to add a proper local **Transverse Mercator (TM)** projection while:

- preserving compatibility with existing MSTS routes using Interrupted Goode Homolosine (IGH),
- preserving compatibility with existing TSRE routes using the previous local projection,
- keeping the existing `TsreGeoProjection` route definition,
- minimizing unrelated refactoring,
- making the projection-selection architecture more extensible.

Target branch during development: `feature/geo-terrain`.

Relevant code areas:

- `src/tsre/geo/GeoCoordinates.h`
- `src/tsre/geo/GeoCoordinates.cpp`
- `src/tsre/world/Trk.h`
- `src/tsre/world/Trk.cpp`
- route/editor initialization sites which assign `Game::GeoCoordConverter`

---

## Projection semantics

`TsreGeoProjection` defines the **projection origin**, not the route start.

In TSRE world coordinates, after any tile/world bookkeeping:

- world/projected `X = 0`
- world/projected `Z = 0`

correspond to the geographic projection origin defined by `TsreGeoProjection`.

The route itself may begin elsewhere. `RouteStart` / start tile data is unrelated to the projection origin.

---

## Supported projection types

A generic enum was added to `GeoCoordinates.h`:

```cpp
enum class GeoProjectionType {
    Undefined,
    InterruptedGoodeHomolosine,
    LocalEllipsoidalEquirectangular,
    TransverseMercator
};
```

The existing TSRE projection is described as:

```text
LocalEllipsoidalEquirectangular
```

rather than merely "legacy".

The current class name `GeoTsreCoordinateConverter` was intentionally left unchanged for now. A wider class/name cleanup can be performed later with IDE-assisted refactoring.

String conversion helpers were also added in the geo module:

```cpp
QString GeoProjectionTypeToString(GeoProjectionType type);
GeoProjectionType GeoProjectionTypeFromString(const QString &value);
```

Projection-name serialization/parsing therefore belongs to the geo module rather than `Trk`.

---

## Generic tile ↔ projected/internal conversion

Previously, MSTS and TSRE converters each implemented their own tile ↔ intermediate-coordinate conversion.

This logic was moved into `GeoWorldCoordinateConverter`.

The base converter now uses:

```cpp
tileOffsetX
tileOffsetZ
tileZDirection
```

to describe the tile-coordinate system.

Conceptually:

```cpp
Sample =
    2048.0 * (tileX + x)
    + tileOffsetX;

Line =
    tileZDirection
    * 2048.0
    * (tileZ + (1.0 - z))
    + tileOffsetZ;
```

The inverse begins with:

```cpp
double tileX =
    (coordinates->Sample - tileOffsetX) / 2048.0;

double tileZ =
    tileZDirection
    * (coordinates->Line - tileOffsetZ) / 2048.0;
```

### X decomposition

```cpp
int tileXi = (int)floor(tileX);
double x = tileX - tileXi;
```

### Z decomposition

Z uses:

```text
continuousZ = TileZ + (1 - z)
```

Therefore the canonical inverse is:

```cpp
int tileZi = (int)ceil(tileZ) - 1;
double z = (tileZi + 1) - tileZ;
```

This keeps normalized local coordinates in the expected range:

```text
0 <= z < 1
```

and avoids the previous TSRE behavior where exact boundaries could be returned as `z = 1.0`.

This matches the coordinate normalization logic in `PreciseTileCoordinate::checkCoords()`, where a local coordinate reaching the positive tile edge belongs to the adjacent tile.

---

## MSTS configuration

MSTS IGH now uses the same generic tile/internal logic.

Its effective configuration is equivalent to:

```cpp
tileOffsetX = 2048.0 * 16384.0;
tileOffsetZ = 2048.0 * 16384.0;
tileZDirection = -1.0;
```

This reproduces the original MSTS formulas:

```cpp
Line =
    2048 * (16384 - tileZ - 1 + z);

Sample =
    2048 * (tileX + 16384 + x);
```

IGH itself still performs the geographic conversion:

```text
Latitude/Longitude ↔ Interrupted Goode Homolosine coordinates
```

---

## TSRE local projection configuration

For TSRE-local projections, the existing `TsreGeoProjection` array remains:

```text
[0] center latitude
[1] center longitude
[2] X offset
[3] Z offset
```

The generic tile/internal layer uses the route-provided X/Z offsets and positive Z orientation.

The old local projection remains available through:

```cpp
GeoTsreCoordinateConverter
```

and is now identified conceptually as:

```text
LocalEllipsoidalEquirectangular
```

Its basic behavior remains unchanged.

---

## Transverse Mercator converter

A new converter was implemented:

```cpp
GeoTsreTransverseMercatorCoordinateConverter
```

It derives directly from the generic world-coordinate base rather than from the equirectangular TSRE projection class.

This is important architecturally:

```text
GeoWorldCoordinateConverter
    |
    +-- GeoMstsCoordinateConverter
    |
    +-- GeoTsreCoordinateConverter
    |
    +-- GeoTsreTransverseMercatorCoordinateConverter
```

The base class handles tile ↔ projected/internal coordinates.

Each derived class handles only:

```text
projected/internal ↔ latitude/longitude
```

### Internal coordinate orientation

For TSRE-local projections:

```text
IghCoordinate::Line   = northing-like coordinate
IghCoordinate::Sample = easting-like coordinate
```

Although `IghCoordinate` is now used generically, it has not yet been renamed. A future cleanup may rename it to something such as:

```text
ProjectedCoordinate
```

No such rename was included in this work.

---

## TM parameters

The TM projection currently uses:

```text
ellipsoid = GRS80
lat_0     = TsreGeoProjection center latitude
lon_0     = TsreGeoProjection center longitude
k_0       = 1.0
false easting  = 0
false northing = 0
```

The projection origin therefore maps to:

```text
Line   = 0
Sample = 0
```

before the normal tile/world offsets are applied.

The implementation uses a sixth-order Krüger / Engsager-Poder style Transverse Mercator formulation rather than the short UTM/Snyder approximation.

This was chosen because TSRE routes can extend hundreds of kilometres from the projection center, where a short approximation would accumulate more distortion/error.

The mathematical TM internally starts from an equatorial meridional origin, so the projected northing of `centerLat` is calculated and subtracted. This implements the desired effective `lat_0 = centerLat`.

---

## Why TM improves TSRE routes

The previous local projection uses fixed metres-per-degree values calculated at the center latitude.

That is simple and works well near the origin, but distortion grows with distance.

TM correctly models the changing geometry away from the projection center/central meridian.

A practical test was performed using a newly created Poland route with the projection center near **Łódź**:

```text
Old TSRE local projection:
    approximately 4% length error around Szczecin

Transverse Mercator:
    approximately 0.1% length error around Szczecin
```

The editor can display both game/world length and geographic length, making this difference easy to observe.

This is an empirical editor test, not yet a formal automated accuracy benchmark.

---

## TRK format

The existing token remains unchanged:

```text
TsreGeoProjection ( lat lon offsetX offsetZ )
```

A new token was added:

```text
TsreGeoProjectionType ( "TransverseMercator" )
```

Possible serialized values currently are:

```text
InterruptedGoodeHomolosine
LocalEllipsoidalEquirectangular
TransverseMercator
```

The enum/string conversion is centralized in the geo module.

---

## TRK compatibility rules

`GeoProjectionType` begins as:

```cpp
GeoProjectionType::Undefined
```

After the whole TRK has been parsed, old-route compatibility is resolved.

### Old MSTS route

No `TsreGeoProjection` and no projection type:

```text
-> InterruptedGoodeHomolosine
```

### Old TSRE route

`TsreGeoProjection` exists, but no type token:

```text
-> LocalEllipsoidalEquirectangular
```

### New TM route

For example:

```text
TsreGeoProjection ( 51.7592 19.4560 0 0 )
TsreGeoProjectionType ( "TransverseMercator" )
```

resolves to:

```text
TransverseMercator
```

---

## TRK integrity rule

IGH is currently the only supported global projection that does not require `TsreGeoProjection`.

The intended generic integrity rule is therefore:

```cpp
if (geoProjectionType != GeoProjectionType::InterruptedGoodeHomolosine
        && tsreProjection == NULL) {
    // invalid local projection configuration
    // warn and fall back to IGH
}
```

This is deliberately generic.

Future TSRE projections are expected to be local projections using `TsreGeoProjection`. There is currently no anticipated need for additional whole-world projections.

A combination such as:

```text
InterruptedGoodeHomolosine + TsreGeoProjection present
```

is unusual but is not explicitly forbidden. The extra local parameters are simply irrelevant to IGH.

---

## TRK saving behavior

For compatibility with plain MSTS data:

```text
tsreProjection == NULL
```

writes no TSRE projection metadata.

When `TsreGeoProjection` exists, both the projection parameters and projection type should always be written:

```text
TsreGeoProjection ( ... )
TsreGeoProjectionType ( "..." )
```

This is more future-proof than omitting the type for the current default projection.

`Undefined` is an internal sentinel and should not normally be intentionally serialized as the projection of a valid local route.

---

## Projection factory

Projection construction was centralized with a static factory on the base converter:

```cpp
GeoWorldCoordinateConverter::Create(
    GeoProjectionType type,
    double *projection);
```

Conceptually:

```cpp
switch(type) {
case GeoProjectionType::InterruptedGoodeHomolosine:
    return new GeoMstsCoordinateConverter();

case GeoProjectionType::LocalEllipsoidalEquirectangular:
    return new GeoTsreCoordinateConverter(projection);

case GeoProjectionType::TransverseMercator:
    return new GeoTsreTransverseMercatorCoordinateConverter(projection);

case GeoProjectionType::Undefined:
default:
    return nullptr;
}
```

After TRK parsing and integrity validation, route/editor initialization can therefore be reduced to:

```cpp
Game::GeoCoordConverter =
    GeoWorldCoordinateConverter::Create(
        trk->geoProjectionType,
        trk->tsreProjection);
```

This avoids duplicating projection-selection switches at multiple initialization sites.

---

## Manual verification performed

The following practical checks were performed:

### MSTS IGH routes

Existing MSTS routes using IGH load and operate normally after the generic tile/internal refactor.

### TM route

A new Poland route was created with its projection center near Łódź.

Observed distance error near Szczecin:

```text
LocalEllipsoidalEquirectangular: ~4%
TransverseMercator:             ~0.1%
```

This strongly indicates that:

- TM orientation is correct,
- center/origin handling is correct,
- tile/internal mapping is correct,
- forward and inverse geographic conversion are behaving plausibly over long route distances.

---

## Automated tests still recommended

Formal synthetic/unit tests were intentionally deferred because of development time/token constraints.

Future tests should cover at least:

### Projection center

```text
center lat/lon -> projected 0,0
projected 0,0 -> center lat/lon
```

### TM forward/inverse

Known reference points compared to an independent implementation such as PROJ.

### Round trip

```text
lat/lon -> projected -> lat/lon
```

over many random points.

### Generic tile mapping

```text
tile -> internal -> tile
```

including:

- positive tiles,
- negative tiles,
- `x/z = 0`,
- values close to `1`,
- exact tile boundaries.

### MSTS regression

Compare generic base-class conversion against the original MSTS formulas.

### TRK compatibility

Test:

1. plain MSTS route,
2. old TSRE route with only `TsreGeoProjection`,
3. explicit equirectangular route,
4. TM route,
5. invalid local projection without `TsreGeoProjection`,
6. unknown projection type.

### Save/reload

Verify that new local routes save both:

```text
TsreGeoProjection
TsreGeoProjectionType
```

and reload with the same projection.

---

## Deferred cleanup / possible future work

The following items were intentionally not included in this implementation.

### Rename `IghCoordinate`

It now serves as a generic projected/intermediate coordinate, so the name is misleading outside MSTS IGH.

Possible future name:

```text
ProjectedCoordinate
```

This should be done separately using IDE/refactor tooling because the type is widely used.

### Rename existing TSRE converter

The current:

```cpp
GeoTsreCoordinateConverter
```

could eventually become something explicitly describing:

```text
LocalEllipsoidalEquirectangular
```

Again, this is a separate mechanical refactor.

### Direct tile ↔ lat/lon convenience functions

The base converter could later expose direct helpers composing:

```text
Tile -> Projected -> LatLon
LatLon -> Projected -> Tile
```

This was discussed but intentionally skipped for now.

### More projections

The enum/factory/TRK architecture is now ready for additional local projections if they become useful.

---

## Current architectural summary

After this work, the intended model is:

```text
TRK
    |
    |  GeoProjectionType
    |  TsreGeoProjection parameters
    v
GeoWorldCoordinateConverter::Create(...)
    |
    +-- Interrupted Goode Homolosine
    |
    +-- Local Ellipsoidal Equirectangular
    |
    +-- Transverse Mercator
```

with a shared generic layer for:

```text
MSTS/TSRE tile coordinates
        ↕
projected/internal coordinates
```

and projection-specific implementations only for:

```text
projected/internal coordinates
        ↕
latitude/longitude
```

This keeps tile/world bookkeeping independent from geographic projection mathematics and makes future projection work substantially cleaner.
