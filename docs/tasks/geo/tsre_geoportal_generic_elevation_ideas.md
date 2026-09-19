# TSRE terrain elevation: Geoportal + generic raster provider ideas

> Implementation status and review: [2026-09-19 elevation update](elevation-current-status.md).
> The proposal below is historical; the border/fallback issue at the end remains open.

## Goal

Add a new high-resolution terrain elevation source to TSRE based on the Polish Geoportal / GUGiK NMT service.

The important design idea is that this should not become a one-off Geoportal-specific path inside the terrain generator. TSRE should continue to ask an elevation source for a height at a geographic point, while the source implementation decides how the data are obtained, cached, transformed and sampled.

The same generic infrastructure should later make it possible to add other national high-resolution elevation services with little or no new terrain-generation code.

This document intentionally does not define a final task structure or implementation plan. The relevant TSRE code, the exact GUGiK services and the best raster libraries still need a more detailed review.

---

## Desired TSRE-side model

From the point of view of the existing terrain generation code, the interface should stay conceptually simple:

```cpp
float getHeight(double lat, double lon);
```

The current HGT implementation already follows roughly this model: TSRE asks for elevation at a latitude/longitude and the HGT backend finds the correct file, converts the position to raster coordinates, samples the raster and returns the height.

Geoportal should behave the same way from the caller's perspective.

Conceptually:

```text
TSRE terrain generator
        |
        v
Elevation source
        |
        +-- HGT
        |
        +-- Geoportal NMT
```

The terrain generator should not need to know whether the value came from HGT, GeoTIFF, WCS, an ASCII grid or some other raster source.

---

## Geoportal / GUGiK NMT

Polish Geoportal provides high-resolution Numerical Terrain Model data, including roughly 1 m and 5 m products.

This is much more detailed than the terrain grid ultimately stored by MSTS, so Geoportal data would normally be downsampled when filling an MSTS terrain tile.

There are two relevant access models.

### Point API

Geoportal exposes point-height services such as:

```text
GetHByXY
GetHByPointList
```

These are convenient conceptually because they look almost exactly like TSRE's desired `getHeight()` abstraction.

However, they are not ideal for filling a whole MSTS terrain tile. Thousands of individual HTTP requests, or even many batched point-list requests, would be unnecessarily slow and would make TSRE depend heavily on network latency.

The point API can still be useful for:

- testing and diagnostics,
- checking individual heights,
- profiles or isolated queries,
- fallback use.

It should probably not be the main backend for bulk terrain generation.

### Raster / WCS access

The more attractive solution is to request a raster covering the required terrain area, then satisfy all `getHeight(lat, lon)` calls locally from that raster.

Geoportal can provide raster terrain through WCS in formats such as:

- GeoTIFF,
- Arc/Info ASCII Grid.

For TSRE this is much closer to the existing HGT model:

```text
HGT:
load raster file
-> sample many points locally

Geoportal:
request raster for area
-> cache it
-> sample many points locally
```

The first request for an area may involve network access. After that, height lookup should be a normal in-memory raster sample.

---

## Geoportal source behaviour

A Geoportal elevation source could work conceptually like this:

```text
getHeight(lat, lon)
    |
    +-- Is the required raster already in cache?
    |       |
    |       +-- yes -> sample it
    |
    +-- no
            |
            +-- determine required area
            +-- request it from Geoportal WCS
            +-- decode raster
            +-- cache raster
            +-- sample it
```

The important point is that `getHeight()` remains a point-query interface for the rest of TSRE, but it does not imply one HTTP request per point.

---

## Coordinate systems

HGT is naturally indexed in geographic latitude/longitude coordinates.

Geoportal data may use Polish projected coordinate systems, especially EPSG:2180.

Therefore the Geoportal backend will need coordinate transformation:

```text
TSRE lat/lon
    |
    v
WGS84 / EPSG:4326
    |
    v
Geoportal raster CRS, e.g. EPSG:2180
    |
    v
raster pixel coordinates
```

This transformation should be handled by common CRS/projection code, not by custom formulas embedded in a Geoportal-specific class.

Ideally a raster dataset simply declares its CRS and the shared elevation code performs the required transform.

---

## Shared raster representation

After downloading or loading a terrain file, different raster formats should ideally be converted into one common in-memory representation.

For example:

```cpp
struct ElevationRaster {
    int width;
    int height;

    double originX;
    double originY;

    double pixelSizeX;
    double pixelSizeY;

    CRS crs;

    float noData;
    std::vector<float> data;
};
```

The exact structure still needs review, but the main idea is useful:

```text
GeoTIFF --------\
ASCII Grid ------> ElevationRaster -> sample(x, y)
HGT ------------/
```

Once the data are in this form, interpolation, NoData handling, coordinate mapping and caching can be shared.

---

## GeoTIFF versus Arc/Info ASCII Grid

Both formats are useful.

### GeoTIFF

Advantages:

- very common for modern elevation datasets,
- stores georeferencing and CRS metadata,
- compact,
- suitable for large rasters,
- compatible with Cloud Optimized GeoTIFF concepts if needed later,
- likely to be reusable for many other national services.

For a generic implementation, GeoTIFF is probably the more strategically useful format.

### Arc/Info ASCII Grid

Advantages:

- extremely simple format,
- trivial to parse,
- easy to inspect and debug,
- still used by some national mapping agencies and WCS services.

Disadvantages:

- larger,
- less efficient,
- less rich metadata.

It may still be worth supporting because the implementation cost is low and it broadens compatibility considerably.

---

## Generic provider architecture

The national service itself should be separated from the raster implementation.

A useful conceptual split is:

```text
ElevationSource
    |
    v
RasterElevationSource
    |
    +-- common interpolation
    +-- CRS transform
    +-- cache
    +-- NoData handling
    +-- getHeight(lat, lon)
```

and separately:

```text
ElevationProvider
    |
    +-- WCS provider
    +-- direct GeoTIFF / COG provider
    +-- tiled URL provider
    +-- file-index provider
    +-- custom provider if necessary
```

Geoportal would then ideally be mostly a configuration of a generic WCS provider rather than a completely custom implementation.

---

## Configuration-driven national services

The attractive long-term model is:

```text
generic C++ provider implementation
+
config file describing a dataset/service
```

A dataset config might eventually contain things such as:

```yaml
id: pl.gugik.nmt1
name: Poland - GUGiK NMT 1 m

provider: wcs
endpoint: ...

crs: EPSG:2180
resolution: 1.0

format: geotiff
coverage_id: ...

request:
  max_area: ...
  preferred_tile_size: ...

cache:
  enabled: true
```

The exact schema should only be designed after reviewing several real services.

The config should describe data and service behaviour, not contain programmable transformation logic.

Good candidates for configuration include:

- endpoint URL,
- service type,
- coverage/layer identifier,
- CRS,
- raster format,
- resolution,
- geographic coverage,
- tile size,
- request limits,
- URL templates,
- archive format,
- cache settings,
- attribution/licence metadata.

---

## Do not make the config language too clever

Not every national service will fit a simple declarative format.

Some services may require:

- custom authentication,
- catalogue searches,
- temporary download URLs,
- unusual JSON APIs,
- ZIP archives with dynamic contents,
- multiple API calls before a raster can be downloaded,
- non-standard tile lookup rules.

Trying to encode all of that into YAML or JSON would eventually turn the configuration format into a programming language.

A better approach is:

```text
standard services -> generic providers + config
unusual services  -> small custom provider class
```

The goal should be strong reuse, not 100% configuration-only support.

---

## Dataset-oriented rather than country-oriented design

The configuration should represent datasets, not just countries.

For example Poland can have several terrain products:

```text
pl.gugik.nmt1
pl.gugik.nmt5
```

Other countries may have multiple regional providers or multiple generations of the same dataset.

Therefore the architecture should think in terms of:

```text
dataset
    |
    +-- coverage
    +-- resolution
    +-- provider
    +-- CRS
    +-- format
```

rather than hard-coded classes such as:

```text
PolandProvider
GermanyProvider
FranceProvider
```

---

## Caching

Geoportal data should be cached locally.

The cache may be based on:

- WCS request bounds,
- a regular TSRE-defined raster grid,
- MSTS tile bounds plus a margin,
- provider-native raster tiles.

The exact strategy needs review.

A useful property would be that once an area has been downloaded, repeated terrain generation in the same region does not require another network request.

For sampling near raster edges, downloading a small margin around the requested MSTS tile may be useful.

---

## Relationship to HGT

HGT should remain one implementation of the same general elevation-source concept.

Conceptually:

```text
                    ElevationSource
                         |
          +--------------+---------------+
          |                              |
      HGT source                 Raster/service source
          |                              |
       HGT file                 Geoportal WCS / GeoTIFF
```

There may be an opportunity to reuse more code between HGT and other rasters, but this should be decided only after reviewing the current HGT implementation.

The important requirement is that the existing terrain generator should not need separate logic such as:

```text
Generate terrain from HGT
Generate terrain from Geoportal
```

Instead it should generate terrain from the currently selected elevation source.

---

## Suggested direction

The most promising design at this stage is:

- keep TSRE's elevation access point-based: `getHeight(lat, lon)`,
- implement Geoportal as a raster-backed source,
- use WCS to obtain an area rather than one HTTP request per height,
- prefer GeoTIFF as the main shared modern raster format,
- also consider Arc/Info ASCII Grid because it is simple and still used,
- convert supported input formats into a shared raster representation,
- share interpolation, CRS conversion, caching and NoData handling,
- separate generic provider logic from dataset configuration,
- make standard WCS / tiled-raster services configurable,
- retain a custom-provider escape hatch for unusual national services,
- define configs per dataset rather than per country.

Before implementation, the next useful step is a detailed review of:

1. the existing TSRE HGT classes and call flow,
2. the exact current GUGiK WCS capabilities and limits,
3. available GeoTIFF decoding/projection support already present or suitable for TSRE,
4. how raster caching should align with MSTS terrain tiles,
5. several non-Polish national elevation services to validate that the generic provider/config idea is broad enough.

---

## Open — border coverage, selectable fallback and preserving existing heights

Recorded 2026-09-17 after testing SZKLARSKA. Documentation only; not implemented.

**Observed problem:** a tile crossing the Poland/Czechia border gets Polish data
plus HGT with Geoportal selected, or Czech data plus HGT with DMR 4G selected.
The current fixed HGT fallback prevents combining the two national datasets.

- [ ] Add a Height window choice for missing coverage/NoData. Initial options:
  use HGT or fill with `0`; leave room for future interpolation.
- [ ] Preferred design to evaluate: select one primary source and one optional
  fallback source. For example, Geoportal primary + Czech DMR 4G fallback, or
  either service primary + HGT fallback. Resolve fallback per missing sample,
  so one terrain tile can contain valid data from both sources.
- [ ] Allow **only one fallback**. If neither selected source supplies a valid
  height, use `0`; do not silently add HGT as a third source. Selecting zero
  directly means no secondary source. Interpolation remains a future option.
- [ ] Add an Apply option to preserve existing terrain heights instead of
  overwriting them with zero-filled missing data. Keep missing-data provenance
  separate from the numeric height, so valid measured zero elevations can be
  distinguished from missing samples. Exact UI wording/zero semantics remain open.
- [ ] Validate on a border-crossing SZKLARSKA tile: both source orders, HGT and
  zero fallback, both sources missing, and Apply with preservation enabled/disabled.
  Report primary, fallback and unresolved/zero-filled sample counts visibly.

Keep this in shared source composition and sampling logic, with no country-specific
terrain generation. The final UI/design choice is open; this task records the
requested behavior and the selectable-fallback proposal.
