# Poland GUGiK EVRF2007 source-sheet provider

Implementation and bounded validation, 2026-09-23. This replaces the slow
EVRF2007 ASCII WCS entry while preserving dataset ID
`pl.gugik.nmt1.evrf2007`. The KRON86 numeric-GeoTIFF WCS entry remains
independent.

## Why the source changed

The EVRF2007 WCS returned a 512 m ASCII block in a median 13.2 seconds. Larger
1024 m and 2048 m requests did not finish within 90 and 120 seconds. A typical
2 km terrain request could consequently need about 25 serial blocks and several
minutes.

GUGiK's official WFS sheet catalogue exposes direct links to the original NMT
files. Four current sheets covering the bounded test area downloaded in parallel
in about 43 seconds during research. The implemented provider repeats this
workflow automatically and converts each source only once.

## Generic provider contract

Provider `wfs-file-catalog` is configured by:

- a WFS endpoint and feature-type regular expression;
- sheet, year, format, resolution and direct-URL field names;
- accepted source formats;
- WFS and source axis-order rules;
- a stable readable cache directory, resolution, CRS, bounds and concurrency.

The provider obtains current feature-type names from `GetCapabilities`, queries
only the requested projected footprint, filters 1 m records and selects the
newest record per sheet. It downloads at most four files concurrently. Nothing
in terrain generation refers to Poland or a GUGiK dataset ID.

Current Arc/Info ASCII Grid files and historical nested ZIP/XYZ files are
accepted. ZIP expansion is bounded to 256 MiB, nested to two archive levels and
restricted to supported grid entries. Historical nested ZIP/XYZ support is based
on an inspected official 2018 sample; the current Arc/Info route has the live
end-to-end validation described below.

## Axis handling

WFS 2.0 EPSG:2180 bounding boxes use the CRS's formal northing/easting axis
order. TSRE stores projected coordinates conventionally as easting/northing, so
catalogue footprints are normalized immediately.

Downloaded grids need a stronger rule. An earlier current sheet and the
historical XYZ sample used northing/easting values, while the four 2025 sheets
in the live integration probe used ordinary easting/northing grid axes. The
generic `catalog-detected` mode decodes both possible regular-grid orientations
and retains the one matching the official WFS footprint. It rejects a grid if
neither orientation plausibly matches.

## Local cache

Successful source files become classic tiled Deflate Float32 GeoTIFFs in:

```text
geoPath/pl_gugik_nmt1_evrf2007/
```

Each TIFF has a JSON sidecar containing source URL, sheet, year, original byte
size, SHA-256 and normalized bounds. `.tsre-elevation-catalog.json` caches WFS
query footprints and asset metadata. A repeated request contained by an indexed
footprint works offline and does not contact WFS. Clear this named directory when
an explicit catalogue refresh is wanted. Old hashed WCS cache blocks are outside
this provider and can be removed by the user.

Raw ASCII/ZIP input remains only in memory and is discarded after successful
conversion. Four live source files of about 34.8 MB each became 3.9-4.2 MB local
TIFFs. GUGiK's source server returned complete files rather than byte ranges, so
the implementation does not pretend these assets are COGs.

Adjacent source rasters are cropped into one small request-aligned mosaic before
sampling. This supplies interpolation neighbours across sheet boundaries and
avoids artificial one-value gaps. The mosaic retains NoData where no source
sheet supplies a value, allowing the selected fallback and report to work.

## Bounded live result

The control coordinate `(52.0, 19.0)` lies exactly where four 2025 sheets meet.
The first acquisition downloaded and converted all four concurrently. Including
WFS discovery, transfer and conversion it completed in about 54.6 seconds; the
four source files totalled 139,170,635 bytes (132.7 MiB).

After the seam mosaic fix, an offline repeat returned two primary EVRF2007
samples, 105.570 m and 105.563 m, with four cache hits, zero downloads and no
fallback in 546 ms. A nearby request covered by one cached sheet returned two
primary samples in 120 ms.

The focused offline suite passes 412 checks. It covers catalogue rejection,
axis normalization, unheaded XYZ import, local tiled-GeoTIFF round trip, offline
catalogue/cache sampling and fallback. The main TSRE application build and UI
test remain for the user at the integration checkpoint.

## Remaining limits

- There is no documented higher quota for WFS-discovered direct files. The
  client limits work by footprint, uses four connections and relies on persistent
  cache; it should not be used as a bulk national downloader.
- The server ignores byte-range requests on sampled source files. First use can
  download more area than the terrain footprint because official sheet bounds
  are fixed.
- A completed cached query is reused until its named cache directory is cleared;
  TSRE does not poll for newer annual sheets on every terrain generation.
- Detailed 1 m sheets are unsuitable for 32 km distant-terrain requests. The
  separate distant-terrain resolution/eligibility task still applies.

Official endpoints:

- WFS catalogue: <https://mapy.geoportal.gov.pl/wss/service/PZGIK/NumerycznyModelTerenuEVRF2007/WFS/Skorowidze>
- Product information: <https://www.geoportal.gov.pl/pl/dane/numeryczny-model-terenu-nmt/>
