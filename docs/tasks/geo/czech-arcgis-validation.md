# Stage B: Czech DMR 5G through a generic ArcGIS ImageServer provider

> Historical Stage B report at `2aaae65`. See [current status and review](elevation-current-status.md).
> DMR 5G now uses four connections, and coarser output grids use area filtering.
> The 300-check result below predates those changes and the additional datasets.

2026-09-17, branch `geo-terrain`.

## Implementation

`ArcGisImageServerProvider` builds an ArcGIS REST `exportImage` request. It shares
the existing bounded downloader, cancellation, cache validation and atomic writes
with `WcsProvider` through `CachedRasterProvider`. Both feed the same `RasterSource`
and `Elevation::Raster`: projection, sampling, offsets, reporting and HGT fallback
are unchanged. No Czech-specific terrain logic or new dependencies were added.

Select **Czechia - CUZK DMR 5G (2 m, Bpv)** in the height dialog or Settings
(`geo.elevation.source=cz.cuzk.dmr5g`). Manual and automatic generation use the same
selection. The earlier automatic-progress fix is unchanged.

## Service contract and configuration

- [Official service metadata](https://ags.cuzk.gov.cz/arcgis2/rest/services/dmr5g/ImageServer?f=pjson)
  advertises 2 m raster spacing, one float32 band, native EPSG:5514 and Bpv
  (Baltic 1957) heights. DMR 5G's original product is an irregular terrain model;
  2 m describes this raster service, not the original point spacing.
- The generic provider takes an ImageServer root URL, appends `/exportImage`,
  and sets `f=image`, `format=tiff`, `pixelType=F32`, `compression=None`,
  `bandIds=0`, and `renderingRule={"rasterFunction":"None"}`. This requests raw
  heights instead of the service's hillshade. See the
  [Esri exportImage contract](https://developers.arcgis.com/rest/services-reference/enterprise/export-image/).
- `bboxSR` and `imageSR` use the configured CRS, EPSG:25833 for this dataset.
  The server reprojects into ETRS89 / UTM 33N, already supported by our sampler.
  `adjustAspectRatio=false` and explicit dimensions preserve the requested grid;
  returned CRS, dimensions and affine transform are checked before caching.
  Bilinear server interpolation is explicit. This is a reprojected output grid,
  not the native Krovak grid. The service selects its default horizontal datum
  transformation; no vertical conversion is requested or performed.
- The cache grid has origin (0,0), 1024-pixel blocks at 2 m spacing. A one-pixel
  halo makes each request 1026 x 1026 pixels / 2052 x 2052 m. Alignment can require
  more than one block per MSTS tile. It does not require a new area-cache scheme.
- Concurrency is configurable from 1 to 4; DMR 5G initially uses 1, as DMR 4G does.
  Existing Polish concurrency is unchanged. The advertised maximum export size
  is 15000 x 4100 pixels; no request-rate guarantee is inferred from that.
- Bounds `[284000,5344000,792000,5696000]` are a conservative UTM envelope of the
  published native extent, not a national coverage mask. A development-only PROJ
  transform with 100 densification points gave approximately
  `[284193.62,5344853.93,790934.01,5695602.07]`; bounds are rounded outward.
- Other suitable ImageServers can use the same provider with catalogue entries
  defining endpoint, supported output CRS, grid, resolution and void policy.
  This first version expects a public service with raw heights in band 0 and
  uncompressed numeric GeoTIFF output; authentication, custom raster functions
  and additional formats are outside this milestone.

## Missing data

Live responses use the existing tiled TIFF reader without any Stage B changes.
Completely absent storage tiles have zero offsets/byte counts and decode to NaN.
At the Polish-Czech border, populated storage tiles also contain unmarked zero
voids. `zeroIsNoData: true` is therefore configured for DMR 5G. Valid zero remains
supported by the common raster and by datasets that leave that option disabled.

Missing samples use the existing HGT fallback with visible counts. The separate
[open border/fallback issue](tsre_geoportal_generic_elevation_ideas.md) remains
unimplemented: there is no secondary online source or zero-fill Apply policy yet.

## Verification

- User acceptance: the user reported "Works great" after testing the implementation.
- Standalone elevation suite: **300 checks, 0 failures**, including existing WCS
  regressions and 26 new ArcGIS checks. No main TSRE build or main tests were run.
- Real Prague, uncovered German, and Polish-Czech border responses decode through
  the common reader. The border production-cache test samples valid elevation,
  a zero void and an absent tile, yielding one primary sample and two HGT samples.
  Cache hashes, pixel registration, output CRS, offsets and cancellation are checked.
- The production `generate()` path fetched Prague successfully: two primary
  samples, one download, no fallback, 23.056 seconds. Repeating it returned the
  same heights (197.451 and 199.014 m) with one cache hit, no download, in 60 ms.
- Live exports with the production block dimensions succeeded in approximately
  22 seconds per request. Uncompressed body sizes were 5,310,222 bytes (Prague),
  1,806 bytes (uncovered) and 1,246,990 bytes (border). Storage-tile padding explains
  why a 1026-square raster exceeds the size of a 1024-square export.
- Response fixtures, URLs, checksums and independent border coordinates are in
  [the evidence directory](evidence/2026-09-17/arcgis/manifest.json). TIFF fixtures
  use Qt-compatible `qCompress` wrapping to reduce repository size; tests restore
  the exact raw response bytes. This fixture compression adds no dependency and
  does not change the uncompressed service/cache format.

After building Release, run the `elevation-ui` and `settings` application suites
and manually preview/apply DMR 5G in a Czech area and at SZKLARSKA. Check the source
and HGT counts, repeat to verify cache reuse, and confirm automatic generation
still has no progress dialog. Datum compatibility and visual terrain seams remain
manual checks; these tests do not establish survey accuracy.

Standalone command from the worktree root, with Qt/MinGW DLL directories on PATH:

```powershell
.\build-geo-tests\tsre_elevation_tests.exe
```
