# TSRE high-resolution elevation sources — Europe tracker

**Original research date:** 2026-09-17  
**TSRE implementation sync:** 2026-09-21 — `feature/geo-terrain`; Austria/Switzerland and Luxembourg/Wales COG milestones after tracker commit `31b9198`
**Scope:** high-resolution European national/regional terrain models, implemented non-European sources, and world-scale fallback/acquisition options.  
**World fallback:** automatic catalogue-based HGT download is now implemented.

This is a research/tracking document, not a final implementation specification. Exact coverage bounds, grid origins, NoData behaviour, service limits and licences should be rechecked from `GetCapabilities`, `DescribeCoverage`, ArcGIS service metadata, provider documentation, or real downloaded files before shipping a new dataset.

---

## 1. Current TSRE elevation architecture

The `feature/geo-terrain` branch has moved significantly beyond the state captured by the original 2026-09-17 research pass.

Current generic pieces include:

- caller-side geographic point sampling (`getHeight(lat, lon)` conceptually);
- a catalogue-driven source model shared by Settings, the elevation dialog and automatic terrain generation;
- **24 configured datasets**, including 22 European entries, USGS 3DEP and the world HGT file source;
- deterministic persistent raster/service caching and stable user-managed directories for file sources;
- `provider: "file"` with one-degree HGT grids, projected range COGs, single national range COGs, and STAC-discovered GeoTIFF tiles;
- catalogue-level `defaultFileSource`, currently `world-hgt`, used as the service fallback;
- primary-source-first preparation: the world fallback is prepared/downloaded only for points unresolved by the selected WCS/ArcGIS source;
- Arc/Info ASCII Grid decoding;
- classic numeric GeoTIFF decoding for one-band `float32`, signed `int16` and unsigned `uint16`; range COG blocks additionally support `float64` conversion, LZW, Deflate, linked overviews and external block tables;
- WCS 2.0.1 provider with separate subset/scaling axes and bounded opt-in handling of server-expanded grids;
- WCS 1.0.0 provider with configurable wire-format spelling (`requestFormat`);
- ArcGIS ImageServer provider with server-side request/output CRS selection through `bboxSR` / `imageSR`;
- profile-local service secrets with HTTP Basic API-key and query-parameter API-key authentication;
- exact raster/grid validation and configuration-hashed cache namespaces;
- fine-source downsampling: when source resolution is finer than the target terrain spacing, TSRE samples a bounded area footprint instead of simply taking one source point;
- Web Mercator footprint correction by latitude;
- dataset-level NoData policy: strict fallback by default, with opt-in neighbour filling used by Netherlands AHN;
- local HGT fallback/final world fallback that preserves valid zero heights and rejects HGT void value `-32768`.

### Raster CRS support

`Geo::CrsTransform` currently accepts:

- EPSG:2180 — Poland CS92;
- EPSG:3794 — Slovenia D96/TM;
- EPSG:4326 — geographic;
- EPSG:3857 — Web Mercator;
- EPSG:2056 — Switzerland LV95, using the official swisstopo approximation;
- EPSG:3035 — ETRS89 / LAEA Europe;
- EPSG:3045 — Czech INSPIRE / ETRS89 TM33 variant used by DMR4G;
- EPSG:3067 — Finland ETRS-TM35FIN;
- EPSG:2169 — Luxembourg TM after the official LUREF2020 datum transformation;
- EPSG:27700 — British National Grid with an injected OSTN15 Lite shift grid;
- EPSG:25828 … EPSG:25838 — generic ETRS89 / UTM zones 28N–38N.

The lightweight projection code is intentionally bounded rather than a general PROJ replacement. Several recently added services avoid unsupported national projections by asking the server for a TSRE-supported CRS.

### Features still deliberately missing / deferred

The earlier WCS/authentication blockers have largely been removed. The main remaining reusable gaps are now:

- a generic **degree-grid GeoTIFF file/download provider** after the HGT and projected/STAC milestones;
- arbitrary georeferenced local-file collection indexing;
- broader GeoTIFF / COG / BigTIFF profiles beyond the verified LZW/Deflate Float32 and LZW Float64 cases, including scale/offset where required;
- generic ATOM / indexed-download / irregular catalogue acquisition;
- local support for remaining national projections such as EPSG:28992, EPSG:2154, EPSG:3301 and EPSG:3812 when server-side reprojection is unavailable;
- vertical datum conversion;
- dynamic source-resolution discovery for heterogeneous/multi-resolution mosaics;
- explicit coarse acquisition profiles and cache identities for distant terrain; detailed-service success does not make a source safe for ~32 km distant tiles;
- selectable secondary online fallback beyond the current default file fallback.

WCS 1.0, API-key handling and server-side reprojection through preconfigured endpoint/query parameters are **no longer** general blockers for the sources already integrated.

---

## 2. Status legend

| Status | Meaning |
|---|---|
| ✅ | Implemented in TSRE and successfully exercised in development/manual testing |
| 🟢 | Supported/configured by current code; remaining work is mainly live endpoint/quality validation or metadata cleanup |
| 🟡 | Very promising, but one small/reusable TSRE feature is still missing |
| 🟠 | Good data, but requires a new provider family, auth infrastructure, or non-trivial projection work |
| 🔴 | Access/licensing/coverage makes it unattractive for automatic TSRE use now |
| ⚪ | No suitable official automatic high-resolution source identified in this research pass |

A dataset being present in `elevation-datasets.json` means it passes the catalogue/code contract; it does **not** by itself prove that the live government endpoint was re-tested at the current revision.

---

# 3. Implemented / tested baseline

Current branch catalogue snapshot, updated 2026-09-21:

| Dataset ID | Area / product | Provider | EPSG / grid | Current status |
|---|---|---|---:|---|
| `world-hgt` | World HGT terrain / Mapzen Skadi automatic fallback | file / HGT | 4326 | ✅ implemented + live download/cache probe |
| `at.bev.als-dgm1` | Austria BEV ALS-DGM 1 m, 2025 mosaic | file / projected range COG | 3035 | ✅ bounded live block + cache probe |
| `lu.act.dtm2024` | Luxembourg ACT DTM 2024, 1 m overview | file / national range COG | 2169 | ✅ bounded live block + cache probe |
| `gb.wales.lidar.dtm1` | Wales LiDAR DTM 1 m | file / national range COG | 27700 + OSTN15 Lite | ✅ bounded live/cache probe + user-confirmed application test |
| `ch.swisstopo.swissalti3d.2m` | Switzerland + Liechtenstein swissALTI3D 2 m | file / STAC GeoTIFF | 2056 | ✅ bounded live tile + cache probe |
| `pl.gugik.nmt1.kron86` | Poland NMT 1 m KRON86 | WCS 2.0.1 | 2180 | ✅ |
| `pl.gugik.nmt1.evrf2007` | Poland NMT 1 m EVRF2007 | WCS 2.0.1 | 2180 | ✅ |
| `cz.cuzk.dmr4g` | Czech DMR4G 5 m | WCS 2.0.1 | 3045 | ✅ |
| `cz.cuzk.dmr5g` | Czech DMR5G 2 m request grid | ArcGIS ImageServer | 25833 | ✅ |
| `us.usgs.3dep.conus` | USGS 3DEP CONUS 2 map m request grid | ArcGIS ImageServer | 3857 | ✅ architecture/live validation source |
| `no.kartverket.nhm.dtm1` | Norway NHM DTM 1 m | ArcGIS ImageServer | 25833 | ✅ |
| `es.ign.mdt05` | Spain MDT05 5 m | WCS 2.0.1 | 25830 | ✅ |
| `si.gurs.dmr1` | Slovenia DMR1 1 m | ArcGIS ImageServer | 3794 | ✅ |
| `de.lgb.dgm1.bb-be` | Brandenburg + Berlin DGM1 | WCS 2.0.1 | 25833 | ✅ |
| `de.nrw.dgm1` | Nordrhein-Westfalen DGM1 | WCS 2.0.1 | 25832 | ✅ |
| `de.hessen.dgm1` | Hessen DGM1 | WCS 2.0.1 | 25832 | ✅ |
| `de.bw.dgm1` | Baden-Württemberg DGM1 | WCS 1.0.0 | 25832 | ✅ with whole-metre precision caveat |
| `de.st.dgm1` | Sachsen-Anhalt DGM1 | WCS 2.0.1 | 25832 | ✅ user-confirmed application test near Magdeburg |
| `fi.nls.dem2` | Finland NLS Elevation Model 2 m | WCS 2.0.1 + Basic API key | 3067 | ✅ live authenticated full-grid test |
| `nl.pdok.ahn.dtm05` | Netherlands AHN DTM, 1 m request/cache grid | WCS 2.0.1 | 25831 | ✅ live server-reprojection + NoData-fill test |
| `gb.ea.lidar.dtm1` | England EA LIDAR DTM, 2 map m request grid | WCS 2.0.1 | 3857 | ✅ live full-terrain-grid test |
| `ee.maru.dtm1` | Estonia DTM, 2 map m request grid | WCS 1.0.0 | 3857 | ✅ live full-terrain-grid test |
| `dk.datafordeler.dhm.terraen` | Denmark DHM/Terrain, 1 m request grid | WCS 1.0.0 + query API key | 25832 | ✅ live authenticated full-terrain-grid test |
| `be.vlaanderen.dhmv2.dtm1` | Flanders DHMV II DTM, 2 map m request grid | WCS 1.0.0 | 3857 | ✅ live integration/cache probe and user-confirmed application test |

The branch's own status documents distinguish **configured**, **live point/block tested**, and **full terrain-grid tested**. The statuses below preserve that distinction rather than treating a valid JSON entry as automatic proof of nationwide service quality.

## Poland — GUGiK Geoportal NMT

**Status: ✅ working**

National NMT, 1 m. This was the first implementation and validates both GeoTIFF and ASCII Grid WCS paths.

### GeoTIFF / KRON86

```json
{
  "id": "pl.gugik.nmt1.kron86",
  "name": "Poland - Geoportal NMT 1 m (KRON86, GeoTIFF)",
  "provider": "wcs-2.0.1",
  "endpoint": "https://mapy.geoportal.gov.pl/wss/service/PZGIK/NMT/GRID1/WCS/DigitalTerrainModelFormatTIFF",
  "coverage": "DTM_PL-KRON86-NH_TIFF",
  "format": "image/tiff",
  "axisX": "x",
  "axisY": "y",
  "crs": 2180,
  "verticalDatum": "PL-KRON86-NH",
  "resolution": 1,
  "origin": [160828.34326572, 796521.669409553],
  "bounds": [160828.34326572, 98928.8977745594, 876029.97009323, 796521.669409553],
  "blockPixels": 1024,
  "concurrentRequests": 4,
  "zeroIsNoData": true,
  "attribution": "GUGiK / Geoportal.gov.pl"
}
```

### ASCII Grid / EVRF2007

```json
{
  "id": "pl.gugik.nmt1.evrf2007",
  "name": "Poland - Geoportal NMT 1 m (EVRF2007, ASCII Grid)",
  "provider": "wcs-2.0.1",
  "endpoint": "https://mapy.geoportal.gov.pl/wss/service/PZGIK/NMT/GRID1/WCS/DigitalTerrainModel",
  "coverage": "DTM_PL-EVRF2007-NH",
  "format": "image/x-aaigrid",
  "axisX": "x",
  "axisY": "y",
  "crs": 2180,
  "verticalDatum": "PL-EVRF2007-NH",
  "resolution": 1,
  "origin": [160828.34326572, 796521.669409553],
  "bounds": [160828.34326572, 98928.8977745594, 876029.97009323, 796521.669409553],
  "blockPixels": 512,
  "zeroIsNoData": true,
  "attribution": "GUGiK / Geoportal.gov.pl"
}
```

Sources:

- https://www.geoportal.gov.pl/pl/dane/numeryczny-model-terenu-nmt/
- https://github.com/GokuMK/TSRE5vc/blob/feature/geo-terrain/src/tsre/geo/elevation-datasets.json

---

## Czechia — ČÚZK DMR4G

**Status: ✅ working**

- national 5 m DTM;
- WCS 2.0.1;
- TIFF;
- EPSG:3045;
- useful proof that the WCS code is not Poland-specific.

```json
{
  "id": "cz.cuzk.dmr4g",
  "name": "Czechia - CUZK DMR 4G (5 m, EVRS)",
  "provider": "wcs-2.0.1",
  "endpoint": "https://ags.cuzk.gov.cz/arcgis2/services/INSPIRE_Nadmorska_vyska/ImageServer/WCSServer",
  "coverage": "MD_LAS",
  "format": "image/tiff",
  "axisX": "x",
  "axisY": "y",
  "crs": 3045,
  "verticalDatum": "EVRS",
  "resolution": 5,
  "origin": [292001.732277163304, 5657998.94293634221],
  "bounds": [292001.732277163304, 5375998.94293634221, 780001.732277163304, 5657998.94293634221],
  "blockPixels": 256,
  "concurrentRequests": 1,
  "zeroIsNoData": true,
  "attribution": "CUZK / geoportal.cuzk.cz - INSPIRE Elevation GRID (DMR 4G)"
}
```

---

## Czechia — ČÚZK DMR5G

**Status: ✅ working**

- detailed terrain model exposed as an ArcGIS ImageServer;
- requested as a 2 m float raster;
- EPSG:25833;
- validates the generic `arcgis-imageserver` provider.

```json
{
  "id": "cz.cuzk.dmr5g",
  "name": "Czechia - CUZK DMR 5G (2 m, Bpv)",
  "provider": "arcgis-imageserver",
  "endpoint": "https://ags.cuzk.gov.cz/arcgis2/rest/services/dmr5g/ImageServer",
  "format": "image/tiff",
  "crs": 25833,
  "verticalDatum": "Bpv (Baltic 1957)",
  "resolution": 2,
  "origin": [0, 0],
  "bounds": [284000, 5344000, 792000, 5696000],
  "blockPixels": 1024,
  "concurrentRequests": 4,
  "zeroIsNoData": true,
  "attribution": "CUZK / geoportal.cuzk.cz - DMR 5G"
}
```

Service:

- https://ags.cuzk.gov.cz/arcgis2/rest/services/dmr5g/ImageServer

---

## Spain — IGN/CNIG MDT05

**Status: ✅ working in the current local work; tested successfully around Canfranc**

- national WCS 2.0.1;
- mainland + Balearics;
- 5 m grid produced from PNOA LiDAR;
- EPSG:25830;
- TIFF;
- good proof that generic ETRS89/UTM support works.

```json
{
  "id": "es.ign.mdt05",
  "name": "Spain - IGN/CNIG MDT05 (5 m)",
  "provider": "wcs-2.0.1",
  "endpoint": "https://servicios.idee.es/wcs-inspire/mdt",
  "coverage": "Elevacion25830_5",
  "format": "image/tiff",
  "axisX": "x",
  "axisY": "y",
  "crs": 25830,
  "verticalDatum": "Orthometric heights / REDNAP",
  "resolution": 5,
  "origin": [0, 0],
  "bounds": [-250000, 3790000, 1245000, 4910000],
  "blockPixels": 512,
  "concurrentRequests": 1,
  "zeroIsNoData": false,
  "attribution": "IGN/CNIG - MDT05, CC BY 4.0"
}
```

Good test coordinate:

```text
Canfranc Estación: 42.7515, -0.5146
```

Sources:

- https://servicios.idee.es/wcs-inspire/mdt
- https://www.idee.es/resources/documentos/RD_wcs_v2_0.pdf
- https://api-coverages.idee.es/collections?f=html

---

# 4. Implemented / configured since the original research pass

## Norway — Kartverket NHM DTM 1 m

**Status: ✅ in current branch**

The earlier tracker described this as local/unpushed work. It is now in `feature/geo-terrain` as `no.kartverket.nhm.dtm1`.

- ArcGIS ImageServer;
- 1 m request grid;
- float elevation raster;
- EPSG:25833;
- NN2000 heights;
- no new provider or projection work required.

Endpoint:

```text
https://hoydedata.no/arcgis/rest/services/NHM_DTM_25833/ImageServer
```

Configured extent:

```text
[-100275, 6399725, 1150255, 8000275]
```

Sources:

- https://hoydedata.no/arcgis/rest/services/NHM_DTM_25833/ImageServer
- https://data.norge.no/nn/data-services/8650cb43-e1c8-3aaf-b707-1449518d2f96/hoydedata-laser-topobaty-nasjonal-detaljert-terrengmodell-25833-wcs

---

## Slovenia — GURS DMR1

**Status: ✅ in current branch**

The old blocker is gone: EPSG:3794 support was added to the raster projection layer and the source is now configured as `si.gurs.dmr1`.

Endpoint:

```text
https://geohub.gov.si/image/rest/services/TEMELJNI_RASTRI/DMR1/ImageServer
```

Properties:

- 1 m DTM;
- ArcGIS ImageServer;
- EPSG:3794 (D96/TM);
- configured extent `[374846.96, 31291.93, 622607.96, 193755.93]`;
- Slovenian national height system.

Source:

- https://geohub.gov.si/image/rest/services/TEMELJNI_RASTRI/DMR1/ImageServer

---

## Germany — current DGM1 implementation

Germany remains fragmented by Land, but five state/regional entries are now present in the branch catalogue.

### Brandenburg + Berlin — ✅

Current entry: `de.lgb.dgm1.bb-be`

- WCS 2.0.1;
- 1 m;
- EPSG:25833;
- DHHN2016;
- endpoint now used by TSRE:
  `https://isk.geobasis-bb.de/ows/dgm_wcs`;
- coverage `bb_dgm`.

This replaces the older candidate endpoint/coverage recorded in the first research pass. It was manually brought to a working TSRE configuration.

### Nordrhein-Westfalen — ✅

Current entry: `de.nrw.dgm1`

- WCS 2.0.1;
- 1 m;
- EPSG:25832;
- DHHN2016;
- endpoint requires GeoTIFF options forcing an uncompressed, predictor-free, non-tiled response compatible with the bounded TSRE TIFF reader:

```text
https://www.wcs.nrw.de/geobasis/wcs_nw_dgm?GEOTIFF:COMPRESSION=None&GEOTIFF:PREDICTOR=None&GEOTIFF:TILING=false
```

The original response failed the TSRE TIFF contract; the endpoint options resolved that.

### Hessen — ✅

Current entry: `de.hessen.dgm1`

- WCS 2.0.1;
- 1 m;
- EPSG:25832;
- WCS axes are `E` / `N` rather than `x` / `y`;
- DHHN2016;
- endpoint: `https://inspire-hessen.de/raster/dgm1/ows`;
- some edge requests may legitimately fall outside actual coverage despite the rectangular catalogue bounds.

### Baden-Württemberg — ✅ implemented, quality caveat remains

Current entry: `de.bw.dgm1`

- WCS 1.0.0;
- 1 m request grid;
- EPSG:25832;
- DHHN2016;
- endpoint: `https://owsproxy.lgl-bw.de/owsproxy/wcs/WCS_INSP_BW_Hoehe_Coverage_DGM1`;
- coverage `EL.ElevationGridCoverage`.

This source drove the addition of generic WCS 1.0.0 support and unsigned-16 TIFF decoding.

**Known quality issue:** the WCS 1 path works geometrically, but observed returned elevations are effectively whole-metre/integer values (for example 500, 501 m). The WCS 2 path can preserve better height precision, but testing produced a 1025 × 1026 raster where TSRE requests/validates 1026 × 1026. A future implementation may prefer resampling the one-pixel dimension mismatch over accepting the WCS 1 integer-height precision loss. This is deliberately left as an agent/implementation decision.

### Sachsen-Anhalt — ✅ working

Current entry: `de.st.dgm1`

- WCS 2.0.1;
- 1 m;
- EPSG:25832;
- DHHN2016;
- endpoint: `https://geodatenportal.sachsen-anhalt.de/ows_INSPIRE_LVermGeo_ATKIS_EL_DGM_WCS`;
- coverage `Coverage1`.

The user confirmed successful application use near Magdeburg at
`52.1310, 11.6390`. A separate quantitative block/cache/coverage-edge report was
not retained, so this confirms practical operation rather than nationwide coverage.

### What changed in TSRE because of the German work

- WCS 1.0.0 is now a supported provider type, not a future item.
- uint16 elevation TIFF samples are supported.
- WCS TIFFs without explicit CRS GeoTIFF keys may inherit the configured EPSG, while an explicit conflicting CRS still fails.
- existing query parameters on the endpoint are preserved, which is required by NRW's TIFF-format controls.

The national BKG DGM1 remains unattractive for general anonymous TSRE distribution because of access/licensing constraints.

---

# 5. Recent WCS integrations that removed the old blockers

The original tracker grouped the following countries under “one generic feature is missing”. Those blockers are now implemented in the branch.

## Finland — NLS Elevation Model 2 m

**Status: ✅ implemented and authenticated live-tested**

- national 2 m LiDAR-derived DTM;
- WCS 2.0.1;
- native EPSG:3067 / ETRS-TM35FIN, already handled by the lightweight TM projection;
- N2000 heights;
- profile-local API key using HTTP Basic username / empty password;
- no key in catalogue URLs or cache metadata;
- live 1026 × 1026 Float32 response at exactly 2 m;
- a 256 × 256 terrain-grid generation produced 65,536 primary samples from one download with no HGT fallback.

The implementation also detects an HTTP 200 HTML rejection page instead of passing it to the TIFF decoder. NLS rejected the uppercase GeoTIFF compression/predictor/tiling options tested during research, so the entry deliberately uses the default uncompressed Float32 response.

---

## Denmark — Datafordeler DHM/Terrain

**Status: ✅ implemented and authenticated live-tested**

- national LiDAR bare-earth DHM/Terrain;
- native product spacing 0.4 m, but TSRE requests/caches a 1 m grid;
- WCS 1.0.0;
- EPSG:25832;
- DVR90 heights;
- query-parameter API key resolved from profile-local secrets;
- service-specific `requestFormat: "GTiff"`;
- authenticated 1026 × 1026 Float32 output validated at exact 1 m spacing;
- full 256 × 256 terrain generation returned 65,536 primary samples, no fallback, nine downloads; repeat used cache only.

This integration generalized query-parameter API-key handling without exposing the key in public URLs, cache metadata or error messages.

---

## Netherlands — PDOK AHN

**Status: ✅ implemented and live-tested**

- native AHN coverage is `dtm_05m` in RD New / EPSG:28992 with NAP heights;
- TSRE asks PDOK to reproject horizontally to EPSG:25831, already supported locally;
- current request/cache grid is 1 m, server-resampled from the native 0.5 m source;
- WCS 2.0.1 `SUBSETTINGCRS` / `OUTPUTCRS` and uncompressed TIFF options are carried in the endpoint query;
- negative and zero NAP heights remain valid;
- `noDataPolicy: "fill"` joins available blocks and fills reachable holes before terrain sampling;
- live Float32 output and exact requested UTM grid dimensions were confirmed;
- full cached terrain-grid testing produced 65,536 primary samples with no HGT fallback after NoData filling.

No local RD New implementation or vertical datum conversion is required for this source.

---

## England — Environment Agency LIDAR Composite DTM

**Status: ✅ implemented and live full-grid tested**

- approximately 1 m native DTM in British National Grid / EPSG:27700;
- WCS 2.0.1;
- server-side horizontal reprojection to EPSG:3857;
- TSRE request/cache grid: 2 Web Mercator map metres;
- EPSG:3857 requests require subset axes `X/Y` but scaling axes `i/j`;
- the service expands returned envelopes slightly, so the entry opts into bounded `allowExpandedGrid` validation while retaining the actual TIFF transform;
- valid zero/negative heights are preserved;
- a full 256 × 256 grid produced 65,536 primary samples, no HGT/unavailable samples; repeat used cache only.

The rectangular catalogue bounds are only availability hints; the EA source has real coverage gaps and ordinary world-HGT fallback remains important.

---

## Estonia — Maa- ja Ruumiamet DTM 1 m

**Status: ✅ implemented and live full-grid tested**

- national 1 m DTM, native EPSG:3301 / L-EST97;
- TSRE uses WCS 1.0.0 server reprojection to EPSG:3857 instead of implementing local Lambert Conformal Conic;
- 2 map m request/cache grid;
- service requires `FORMAT=image/tiff`; generic WCS1 `GeoTIFF` spelling is rejected, so the entry uses `requestFormat`;
- TIFF decoder was extended to accept valid padding in the final strip;
- full 256 × 256 testing produced 65,536 primary samples with no fallback; cache repeat reproduced the same range.

A 1024-core request was also tested and reduced request count/time compared with the currently configured 512-core entry; the catalogue choice can be revisited independently.

---

# 6. Download / COG / STAC sources

These are strategically interesting because one generic download provider could unlock several countries at once.

## Austria

**Status: ✅ first projected range-COG source implemented and live-probed**

BEV ALS-DGM:

- national;
- 1 m;
- bare-earth ALS/LiDAR;
- GeoTIFF;
- free;
- 50 × 50 km download tiles;
- EPSG:3035;
- generally around ±0.5 m elevation accuracy.

No comparable simple WCS/ImageServer was identified in this pass.

Source:

- https://www.bev.gv.at/Services/Produkte/Digitales-Gelaendehoehenmodell/ALS-Hoehenraster.html

TSRE uses the dated 2025 50 km file-name template, local EPSG:3035 conversion,
and strict HTTP byte ranges. The official files are roughly 6.8 GB BigTIFF COGs;
the Vienna probe fetched only a 256 KiB index plus one 586,993-byte internal LZW
block and returned about 171.6 m. A repeat was cache-only. Full terrain UI,
tile-boundary and coverage-edge checks remain useful.

---

## Switzerland

**Status: ✅ first STAC GeoTIFF source implemented and live-probed**

swissALTI3D:

- bare-earth high-precision DEM;
- 0.5 m and 2 m grids;
- 1 km² tiles;
- Cloud Optimized GeoTIFF;
- current Swiss CRS LV95 / EPSG:2056;
- very high-quality terrain data.

Source:

- https://www.swisstopo.admin.ch/en/height-model-swissalti3d

TSRE queries the official STAC collection, selects the newest 2 m EPSG:2056
asset for each footprint and downloads the complete roughly 1 MB source TIFF.
The Bern probe returned about 540.3 m and repeated from cache. The 0.5 m files
are deliberately skipped. Adjacent files are joined on their common 2 m grid;
an exact 1 km boundary probe returned two primary samples with no HGT seam
fallback. Detailed 1 km assets are not yet approved for distant terrain.

---

## Liechtenstein

**Status: 🟠**

Liechtenstein uses/distributes swissALTI3D under its cooperation with swisstopo, so the technical path is effectively the Swiss one.

Source:

- https://www.llv.li/en/national-administration/civil-engineering-and-geoinformation-office/national-survey/landscape-models

---

## Sweden

**Status: 🟠**

Current Lantmäteriet `Markhöjdmodell Nedladdning`:

- national terrain model;
- 1 m;
- COG;
- STAC API;
- current STAC endpoint:
  `https://api.lantmateriet.se/stac-hojd/v1`;
- access permission/auth is managed through Geotorget.

The older WCS had a convenient `hojdgrid_1m`, but current distribution has moved toward STAC/COG.

Sources:

- https://geotorget.lantmateriet.se/geodataprodukter/markhojdmodell-nedladdning-api
- https://www.lantmateriet.se/en/geodata/our-products/product-list/elevation-model-download/
- https://www.lantmateriet.se/sv/geodata/vara-produkter/Produktnyheter/Geografisk-information/markhojdmodell-nedladdning-utokas-med-mer-innehall/

Needs:

- STAC/COG provider;
- auth/permission handling;
- SWEREF 99 TM CRS support if data are consumed natively.

---

## Luxembourg

**Status: ✅ implemented and bounded-live-tested**

Very attractive recent dataset:

- DTM 2024;
- full-country LiDAR;
- 0.5 m;
- COG GeoTIFF;
- CC0;
- EPSG:2169.

No numeric WCS was identified on the open-data page; WMS + downloadable COG are provided.
TSRE selects the exact 1 m internal overview, downloads only the required Float64
LZW blocks plus index tables, and uses ACT's official LUREF2020 transformation.
A two-point probe returned 306.786 m and 306.62 m; its repeat was cache-only.

Source:

- https://data.public.lu/en/datasets/inspire-annex-ii-theme-elevation-elevationgridcoverage-dtm-2024/

The native 0.5 m image is deliberately skipped because TSRE terrain does not
benefit from spacing below 1 m. The detailed source remains unsuitable for
distant terrain until coarse overview policy and separate cache identity exist.

---

## Wales

**Status: ✅ implemented, bounded-live-tested and user-confirmed**

Welsh Government publishes national LiDAR DTM as direct COG files, including a 32-bit DTM COG.

Sources:

- https://datamap.gov.wales/maps/lidar-viewer/
- https://datamap.gov.wales/maps/lidar-data-download/

Native CRS is EPSG:27700.

TSRE range-reads the official Float32 Deflate COG. On first use it downloads the
official Ordnance Survey OSTN15/OSGM15 Lite developer ZIP, retains only its
99,959-byte 20 km shift grid under `assets/geo/`, and uses bilinear OSTN15
corrections for horizontal placement. A two-point probe returned 132.857 m and
132.837 m; its repeat was cache-only. The user then confirmed application use at
`52.1394178, -4.5713131`. The source remains disabled for distant terrain.

---

## Slovakia

**Status: 🟠**

Excellent data:

- DMR 5.0;
- whole Slovakia;
- LiDAR-derived;
- 1 m;
- TIFF;
- CC BY 4.0;
- whole-country ZIPs available;
- smaller extracts available through MAPKA;
- second cycle is moving to 0.5 m DTM products.

Sources:

- https://www.gku.sk/gku/produkty-sluzby/na-stiahnutie/zbgis.html
- https://www.gku.sk/geoportal-en/zbgis/als/provision-als-products/

Distribution is currently much more file/catalogue-oriented than WCS/ImageServer.

---

## Portugal

**Status: 🟠, high future interest**

DGT's new mainland LiDAR acquisition is excellent:

- captured 2024–2025;
- average 10 points/m²;
- terrain products in GeoTIFF;
- DTM resolutions 0.5 m and 2 m;
- PT-TM06 / ETRS89;
- open data;
- as of June 2026, approximately 90% of mainland coverage was available;
- current portal download supports areas up to about 200 km² per access;
- DGT explicitly states that API access is planned for the infrastructure later.

Sources:

- https://www.dgterritorio.gov.pt/descarregamento-de-dados-lidar-de-portugal-continental
- https://www.dgterritorio.gov.pt/atividades/cartografia/cartografia-topografica/modelos-digitais

Very promising once a stable programmatic API endpoint appears.

---

## Romania

**Status: 🟠**

The National Cartography Centre has produced a nationwide DTM v3:

- 189 tiles;
- ASCII;
- mixed 1 m / 2 m / 5 m depending on source quality;
- 100 tiles at 5 m and 88 tiles at 1–2 m;
- free distribution through ANCPI/CNGC channels.

Source:

- https://cartografie.ro/index.php/ro/termeni-si-conditii/item/248-generarea-actualizarea-modelului-digital-al-terenului-pentru-intreaga-suprafata-a-romaniei

Interesting data, but no clean WCS/ImageServer route was identified in this pass. A catalogue/tile-download provider may be needed.

---

## Ireland

**Status: 🟠**

Ireland has excellent open LiDAR DTM, but coverage is assembled from several survey programmes rather than one simple national numeric ImageServer:

- GSI: typically 1 m;
- TII / OPW: often 2 m;
- several very high-resolution local products;
- GeoTIFF;
- CC BY 4.0;
- EPSG:2157 / Irish Transverse Mercator.

The ArcGIS services found in the GSI image folder are often hillshade (`DTM_HS`) rather than raw floating-point elevation, while FeatureServer coverage grids include links to actual raster files.

Sources:

- https://data.gov.ie/dataset/open-topographic-lidar-data
- https://gsi.geodata.gov.ie/imagehost/rest/services/Lidar
- https://gsi.geodata.gov.ie/server/rest/services/Lidar/IE_GSI_LiDAR_Coverage_OPW_NASC_IE26_ITM/FeatureServer

Likely future path: indexed-tile/catalogue download provider.

---

## France

**Status: 🟠**

RGE ALTI provides national high-resolution terrain products:

- 1 m and 5 m;
- classic delivery format is ArcInfo ASCII Grid;
- metropolitan France and overseas territories;
- usual metropolitan CRS is Lambert-93 / EPSG:2154.

Documentation:

- https://geoservices.ign.fr/sites/default/files/2021-07/DL_RGEALTI_2-0.pdf

A clean current public numeric WCS fitting TSRE was not identified in this research pass. Modern IGN distribution should be reviewed separately before implementation.

Main blockers:

- distribution/API discovery;
- EPSG:2154 Lambert projection if native data are required.

---

## Italy

**Status: 🟠**

MASE/PST has many 1 m LiDAR DTM products, but coverage is not one clean continuous national 1 m grid. Data are often concentrated around river corridors / hydrogeological-risk areas and distributed through the SIM/PST portal.

Example download page exposes:

```text
LiDAR DTM Grigliato 1x1
```

Source:

- https://sim.mase.gov.it/portalediaccesso/mappe/grid/c98e9e410e2b4d1d8e6aabf50212d92d

Useful data, but currently more like file-cache acquisition than a national WCS/ImageServer backend.

---

# 7. Regional / fragmented cases

## Belgium

Belgium should be treated as regional datasets rather than one national source.

### Flanders

**Status: ✅ implemented; small live integration/cache probe, broader coverage validation pending**

DHMV II DTM is now a catalogue source:

- 1 m LiDAR bare-earth terrain product;
- WCS 1.0.0 at `https://geo.api.vlaanderen.be/el-dtm/wcs`;
- coverage `EL.GridCoverage.DTM`;
- server-side reprojection to EPSG:3857, avoiding local Belgian Lambert support;
- TSRE request/cache grid 2 Web Mercator map metres;
- heights in TAW;
- `FORMAT=GeoTIFF` works through the generic WCS1 path;
- NoData is distinct from zero; coastal/polder zero elevations remain valid;
- no authentication required.

A production probe at `(51.05, 3.72)` returned 7.14634 and 7.21656 m from the primary source with one download and no fallback; repeating used one cache hit and no download. This establishes end-to-end integration, but not yet a full terrain-grid or national coverage/edge validation.

Sources:

- https://www.vlaanderen.be/datavindplaats/catalogus/digitaal-hoogtemodel-vlaanderen-ii-dtm-raster-1-m
- https://geo.api.vlaanderen.be/el-dtm/wcs

### Wallonia

**Status: 🟠**

Current MNT:

- 0.5 m and dedicated 1 m products;
- LiDAR 2021–2022;
- GeoTIFF;
- EPSG:3812 / Belgian Lambert 2008;
- full and custom downloads.

Sources:

- https://geoportail.wallonie.be/catalogue/a004e570-99d6-4fe5-b83d-49b774409278.html
- https://geoportail.wallonie.be/catalogue/fe13bc84-e371-46ca-9632-8ad4139f1ee5.html

There is a public ArcGIS MapServer, but not a confirmed raw numeric ImageServer:

- https://geoservices.wallonie.be/arcgis/rest/services/RELIEF/WALLONIE_MNT_2021_2022/MapServer

Needs either file/download support and EPSG:3812, or another service route.

---

## Germany

Germany is still not one national open-data source, but the current TSRE branch now covers a useful group of Länder directly.

### Currently configured in `feature/geo-terrain`

- Brandenburg + Berlin — ✅ WCS 2.0.1, EPSG:25833;
- Nordrhein-Westfalen — ✅ WCS 2.0.1, EPSG:25832;
- Hessen — ✅ WCS 2.0.1, EPSG:25832;
- Baden-Württemberg — ✅ WCS 1.0.0, EPSG:25832, with the whole-metre precision caveat described above;
- Sachsen-Anhalt — ✅ WCS 2.0.1, EPSG:25832, user-confirmed near Magdeburg.

### National BKG

**Status: 🔴 for general TSRE distribution**

A national DGM1 WCS exists and is technically attractive, but access is subject to licensing/eligibility rather than general anonymous public use.

Source:

- https://gdz.bkg.bund.de/index.php/default/wcs-digitales-gelandemodell-gitterweite-1-m-wcs-dgm1.html

### Bavaria and several other Länder

DGM1 is commonly available as open GeoTIFF/COG/download tiles rather than a simple anonymous numeric WCS. These remain strong future targets for a generic tiled-file / COG provider.

---

## United Kingdom

Treat separately by nation/agency.

### England — ✅
Environment Agency LIDAR Composite DTM is implemented through WCS 2.0.1 with server-side EPSG:3857 output, separate subset/scaling axes and bounded expanded-grid handling. Full 256 × 256 terrain-grid generation and cache reuse were validated.

### Wales — ✅
The direct national 1 m DTM COG is implemented with bounded range reads,
Deflate Float32 decoding and an on-demand official OSTN15 Lite shift grid.

### Scotland — 🟠
Scottish Remote Sensing Portal provides public-sector LiDAR in 10 km tiles and web services, but a single simple national raw numeric DTM WCS comparable to England was not confirmed in this pass.

Source:

- https://environment.gov.scot/data/useful-data-sources/

### Northern Ireland — 🔴 / ⚪
No equally convenient open high-resolution DTM service was identified in this pass; OSNI/LPS licensing and product access need a separate review.

---

# 8. Other European countries — research status

The following table is intentionally conservative.

“Not identified” means **this research pass did not find a clean official national high-resolution service suitable for automatic TSRE integration**. It does not mean no elevation data exist.

| Country / territory | Best finding in this pass | Status for current TSRE |
|---|---|---|
| Albania | National LiDAR/DTM exists and is published through ASIG; prepared-data/ATOM downloads now require registered access | 🟠 new download/auth path |
| Andorra | 5 m raster DEM downloads exist; separate LiDAR 360 portal exists; no raw WCS/ImageServer found | 🟠 file provider / old projection |
| Belarus | No suitable open national high-resolution automatic DTM service identified | ⚪ |
| Bosnia and Herzegovina | No suitable national automatic HR DTM service identified | ⚪ |
| Bulgaria | Official mapping products exist, but no open national HR numeric DTM WCS/ImageServer identified; several products remain fee/request based | 🔴 / ⚪ |
| Croatia | DTM/LiDAR data exist; anonymous INSPIRE ATOM and registered services exist, but current official DTM supply is still request/fee/file oriented | 🟠 / 🔴 |
| Cyprus | Public INSPIRE Elevation download/WMS/ArcGIS REST exists; numeric raster resolution/service needs deeper inspection | 🟠 research needed |
| Greece | Government DEM datasets exist, but no clean current national high-resolution public WCS/ImageServer was confirmed | ⚪ |
| Hungary | DDM 5 × 5 m product exists; most areas are licensed/paid, with limited free INSPIRE examples | 🔴 |
| Iceland | High-resolution national DEM products exist, but no simple current WCS/ImageServer fitting TSRE was confirmed; native CRS is also non-UTM | 🟠 |
| Kosovo | No suitable official automatic HR DTM service identified | ⚪ |
| Latvia | Nationwide LiDAR exists; 1 m DTM product exists, but access is more file/product oriented and no simple anonymous WCS was found | 🟠 |
| Lithuania | National LiDAR/elevation products exist, but no clean high-resolution WCS/ImageServer was confirmed in this pass | 🟠 / ⚪ |
| Malta | No suitable official national HR numeric terrain service confirmed in this pass | ⚪ |
| Moldova | No suitable official national HR numeric terrain service confirmed in this pass | ⚪ |
| Monaco | No separate suitable national HR service identified | ⚪ |
| Montenegro | No suitable official national HR automatic DTM service identified | ⚪ |
| North Macedonia | No suitable official national HR automatic DTM service identified | ⚪ |
| Russia | No suitable broadly open national high-resolution automatic DTM service identified for this project | 🔴 / ⚪ |
| San Marino | No separate suitable automatic HR DTM source identified | ⚪ |
| Serbia | High-precision 1 m DTM exists for selected flood-risk/LiDAR regions, but not as a simple open nationwide service; official supply is regulated/fee-based | 🔴 |
| Turkey | Official elevation/geoid services exist, but access to relevant national services is institutional/restricted; no open HR DTM backend identified | 🔴 |
| Ukraine | No suitable current official national HR automatic terrain service identified in this pass | ⚪ |
| Vatican City | No separate public terrain service identified | ⚪ |

### Useful references for some of these cases

Albania:
- https://asig.gov.al/en/aerial-photography-in-the-republic-of-albania/
- https://asig.gov.al/en/3d-digital-terrain-model-is-published/
- https://asig.gov.al/en/important-notice/

Andorra:
- https://www.iea.ad/interpretacio-mapa/88-sigma/cartografia/models-digitals-del-terreny
- https://geolidar360.govern.ad/geoportal/

Croatia:
- https://geoportal.dgu.hr/cms/en/data-and-services/
- https://dgu.hr/proizvodi-i-usluge/podaci-topografske-izmjere/digitalni-model-reljefa/180

Cyprus:
- https://www.data.gov.cy/en/dataset/486
- https://eservices.dls.moi.gov.cy/inspire/rest/services/INSPIRE/EL_Elevation/MapServer

Hungary:
- https://inspire.lechnerkozpont.hu/inspire/elevation-dem/HU.EL.DEM2022NFO.html

Latvia:
- https://www.lgia.gov.lv/en/digital-height-models-0

Serbia:
- https://www.rgz.gov.rs/digitalni-model-terena

---

# 9. Outside Europe — implemented and future country/place sources

This tracker began as a Europe survey, but non-European country/place integrations should be recorded here rather than mixed into the world fallback section.

## United States — USGS 3DEP CONUS

**Status: ✅ implemented / ArcGIS ImageServer validation source**

Current catalogue entry:

```json
{
  "id": "us.usgs.3dep.conus",
  "name": "USA - USGS 3DEP CONUS (2 m request grid)",
  "provider": "arcgis-imageserver",
  "endpoint": "https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer",
  "format": "image/tiff",
  "crs": 3857,
  "verticalDatum": "NAVD88",
  "resolution": 2,
  "origin": [0, 0],
  "bounds": [-13914936.35, 2753408.10, -7347086.39, 6446275.84],
  "blockPixels": 1024,
  "concurrentRequests": 4,
  "zeroIsNoData": false,
  "attribution": "USGS / The National Map - 3DEP"
}
```

Important interpretation:

- the entry covers the **contiguous United States** request extent, not Alaska/Hawaii and not a claim about every US territory;
- `resolution: 2` is a **Web Mercator request/cache grid**, not a statement that the native 3DEP mosaic is uniformly surveyed at 2 m;
- the underlying 3DEP ImageServer is heterogeneous in source resolution/coverage;
- TSRE's sampler compensates Web Mercator footprint size by latitude when averaging finer source data;
- this source was important architecture validation for the generic ArcGIS ImageServer path and server-side output CRS handling.

No other non-European country-specific sources have yet been promoted into this tracker. Future additions can be listed here while `world-hgt` remains the universal fallback.

---

# 10. World coverage and global fallback

## Implemented: `world-hgt`

**Status: ✅ implemented, automatic no-login download and live cache probe**

The old special-case local HGT fallback is now a normal catalogue dataset and the default file source:

```json
{
  "id": "world-hgt",
  "name": "World - HGT terrain (automatic Mapzen Skadi tile download)",
  "provider": "file",
  "format": "hgt",
  "fileGrid": "degree",
  "directory": "world_hgt",
  "crs": 4326,
  "verticalDatum": "EGM96 for Mapzen downloads; user-supplied files may differ",
  "bounds": [-180, -90, 180, 90],
  "zeroIsNoData": false,
  "concurrentRequests": 4,
  "download": {
    "urlTemplate": "https://s3.amazonaws.com/elevation-tiles-prod/skadi/{latitudeBand}/{tile}.hgt.gz",
    "compression": "gzip"
  },
  "attribution": "Mapzen Terrain Tiles and contributing data providers"
}
```

### Runtime behaviour

- user files live under `geoPath/world_hgt/`;
- local uncompressed `.hgt` files take precedence over downloaded `.hgt.gz` files;
- missing tiles are downloaded anonymously from the AWS-hosted Mapzen/Tilezen Skadi archive, up to four at a time;
- gzip CRC/ISIZE, decompressed size and HGT grid dimensions are validated before atomic storage;
- provenance and SHA-256 are stored in a sidecar;
- HGT dimensions determine sample spacing at runtime, so the file source does not pretend every user tile has one fixed resolution;
- zero is valid; HGT `-32768` is NoData;
- Mapzen downloads are described as EGM96, while user-supplied HGT files may use another vertical reference;
- Mapzen Skadi is a **heterogeneous global composite**, not a uniform worldwide 30 m survey.

Most importantly, service generation prepares the selected WCS/ArcGIS source first. `world-hgt` is prepared/downloaded only for unresolved points, so successful national high-resolution generation does not trigger unnecessary global fallback downloads.

### Validation milestone

At the current milestone:

- **359 standalone elevation checks passed**;
- live probe `(50.05, 19.05)` downloaded `N50E019.hgt.gz` (5,447,758 bytes);
- two sampled source heights were 241 m;
- repeating the probe used the stored compressed file with zero downloads;
- Release application build succeeded;
- Settings passed 246 checks and elevation UI passed 61 checks.

This means world coverage is no longer a separate manual prerequisite for ordinary terrain generation.

## Other global sources researched

The world-scale resolution ceiling for universally/free/easy data is still roughly 30 m, but access is much easier than the old Earthdata-login workflow.

| Source | Access | Format | Approx. resolution | Current TSRE status / interest |
|---|---|---|---|---|
| Mapzen / Tilezen Skadi | anonymous AWS | `.hgt.gz` | usually SRTM-class, heterogeneous | ✅ implemented as `world-hgt` |
| OpenTopography public mirror — SRTM GL1 | anonymous direct | 1° GeoTIFF | ~30 m | 🟠 strong first degree-GeoTIFF candidate |
| OpenTopography public mirror — NASADEM | anonymous direct | 1° GeoTIFF | ~30 m | 🟠 same future provider family |
| OpenTopography public mirror — ALOS AW3D30 | anonymous direct | 1° GeoTIFF | ~30 m DSM | 🟠 same future provider family |
| OpenTopography public mirror — Copernicus GLO-30 | anonymous direct | 1° GeoTIFF | ~30 m DSM | 🟠 same future provider family |
| Copernicus GLO-30 AWS Open Data | anonymous | tiled COG GeoTIFF | ~30 m DSM | 🟠 needs compressed/COG/range support |
| GEDTM30 | anonymous OpenTopography COG | huge COG | ~30 m **bare-earth DTM** | 🟠 strategically attractive after COG support |
| Viewfinder Panoramas | no login | HGT ZIP/catalogue | global ~90 m; selected ~30 m | 🟢 useful manual/user-supplied HGT source |
| DLR SRTM X-SAR WCS | no login | WCS | ~25–30 m | 🔴/supplement only: about 43% land coverage |

### Next global provider milestone

The branch documentation now recommends **one concrete degree-grid GeoTIFF product** as the next file-provider milestone. A single regular 1° GeoTIFF provider could unlock SRTM GL1, NASADEM, ALOS AW3D30 and Copernicus GLO-30 mirrors with mostly catalogue differences.

The current TIFF code is deliberately not enough for arbitrary global downloaded rasters: general compression, BigTIFF, large-file windowed reads and COG/range access remain separate work. GEDTM30 is particularly interesting for railway terrain because it is intended as a bare-earth DTM, but it belongs after that more advanced reader work.

References retained in the branch research:

- `docs/tasks/geo/global-sources-review.md`
- `docs/tasks/geo/local-elevation-sources.md`
- https://registry.opendata.aws/terrain-tiles/
- https://globaldemportal.github.io/
- https://registry.opendata.aws/copernicus-dem/
- https://www.viewfinderpanoramas.org/dem3.html

---

# 11. Candidates ranked by implementation friction

The original easy-WCS queue is now largely exhausted. Finland, Netherlands, England, Estonia, Denmark and Flanders moved from research candidates into the current catalogue during this work.

## Tier A — finish validation / source-quality decisions with current code

### 1. Flanders DHMV II
- generic WCS1 path works;
- live integration/cache probe passed and the user confirmed it in the application;
- a retained full-grid/coverage-edge report would still improve the evidence record.

### 2. Baden-Württemberg DGM1 precision follow-up
- WCS1 grid path works reliably;
- both WCS1 and WCS2 yielded whole-metre `UInt16` heights when comparable;
- WCS2 also had the 1025 × 1026 native-size rounding problem;
- investigate original downloadable DGM1 files if fractional source heights matter more than service convenience.

## Tier B — next reusable file-provider family

### 3. Degree-grid GeoTIFF downloads
Pick one concrete global 1° product first, then generalize only as needed:

- SRTM GL1;
- NASADEM;
- ALOS AW3D30;
- Copernicus GLO-30 mirror.

This is now the clearest next step after `world-hgt`: predictable filenames, no login, direct files, and immediate reuse for several global products.

## Tier C — compressed COG / range / STAC

A narrow COG/range and STAC layer now supports Austria, Luxembourg, Wales and
Switzerland. Verified profiles cover LZW Float32/Float64, Deflate Float32,
overview selection, external block tables and separate OSTN15 preparation.
Further extensions could unlock:

- GEDTM30 global bare-earth ~30 m;
- official Copernicus GLO-30 AWS COGs;
- Sweden 1 m STAC/COG;
- other modern cloud-hosted regional datasets.

## Tier D — irregular download/catalogue workflows

Still useful later:

- Slovakia 1 m TIFF/catalogue;
- Ireland LiDAR programme/tile catalogues;
- Romania mixed-resolution tile set;
- Italy regional/portal LiDAR;
- Portugal as its new LiDAR distribution/API matures;
- Albania and other ATOM/registered catalogue cases.

A second deep WCS search may still find hidden services, especially for fragmented regions, but the return on effort is now lower than it was before the recent WCS integrations.

---

# 12. Architectural observations from the expanded survey

The generic design is holding up well. The current implementation demonstrates six useful acquisition/file-discovery paths rather than country-specific terrain code:

```text
1. Catalogue file source — implemented
   World HGT / Mapzen Skadi
   local user HGT files

2. WCS — implemented provider family
   Poland
   Czech DMR4G
   Spain
   Brandenburg + Berlin
   Nordrhein-Westfalen
   Hessen
   Sachsen-Anhalt
   Baden-Württemberg (WCS 1.0)
   Finland (Basic API key)
   Netherlands (server reprojection + fill)
   England (separate subset/scaling axes + expanded grid)
   Estonia (WCS1 request-format override)
   Denmark (query API key + GTiff)
   Flanders (WCS1 server reprojection)

3. ArcGIS ImageServer — implemented provider family
   Czech DMR5G
   Norway
   Slovenia
   USGS 3DEP CONUS

4. Projected range-COG files — implemented first profile
   Austria BEV 2025 ALS-DGM 1 m

5. STAC-discovered GeoTIFF files — implemented first profile
   Switzerland + Liechtenstein swissALTI3D 2 m

6. Single national range COG — implemented profiles
   Luxembourg ACT DTM 2024, exact 1 m overview
   Wales LiDAR DTM 1 m with OSTN15 Lite

7. Regular downloaded GeoTIFF / tiled files — next provider extension
   global SRTM GL1 / NASADEM / ALOS / Copernicus mirrors
   several German Länder
   Slovakia and other predictable tile sets

8. Broader STAC / COG profiles and indexed catalogues — later extensions
   GEDTM30
   official Copernicus AWS COGs
   Sweden
   Ireland and other catalogue-based sources
```

The runtime flow has also become clearer:

```text
selected primary dataset
        |
        v
prepare + sample primary source
        |
        +---- valid --------------------> terrain height
        |
        +---- unresolved
                    |
                    v
        default file source (`world-hgt`)
                    |
                    +---- valid --------> fallback height
                    |
                    +---- unresolved ---> generation failure / report
```

This keeps country-specific C++ as an escape hatch rather than the default. National quirks are increasingly represented by small generic catalogue options: request format, authentication mode, separate WCS axes, server reprojection encoded in endpoint parameters, bounded expanded-grid acceptance and NoData policy.

The most important architectural gap is no longer “support more WCS syntax”. It
is now broadening the first verified COG/STAC profiles safely—additional
compression and TIFF layouts, arbitrary local collection indexes—and adding
explicit coarse-source behavior for distant terrain.

---

# 13. Practical next step

Work remains intentionally split:

- this tracker/research thread identifies data sources, records service/file quirks and keeps the acquisition landscape current;
- implementation agents decide how reusable TSRE capabilities should be added and validated.

Current short queue after the World HGT milestone:

1. **Flanders** — working and user-confirmed; retain a full-grid/coverage-edge report when convenient.
2. **Baden-Württemberg** — decide whether the whole-metre WCS service is sufficient or whether downloadable original DGM1 files should replace/supplement it.
3. **Choose one global 1° GeoTIFF source** — SRTM GL1 or NASADEM are simple first candidates; inspect real adjacent files, compression, sample type, NoData and edge registration before coding the provider.
4. **Implement the minimum degree-grid GeoTIFF file provider** needed by that source, preserving originals under a stable dataset directory just like `world_hgt`.
5. **Then evaluate COG/range support** for GEDTM30, official Copernicus GLO-30 and the high-value European STAC/COG datasets.
6. **Distant terrain** remains a separate critical issue: detailed sources should not automatically be allowed to download their full native/request resolution for ~32 km distant tiles.

If we want another WCS-heavy research pass before switching provider families, it should be a deliberate **deep search for hidden numeric services** (for example Wallonia/other fragmented national portals), because the obvious national WCS shortlist has mostly been exhausted.

---

## Notes

- Implementation state in this revision was synchronized against `feature/geo-terrain` head `c8a952d0` on 2026-09-19.
- The current catalogue has **24 datasets**, including the Luxembourg and Wales file sources.
- `world-hgt` is both a selectable file source and the configured fallback for unresolved service samples; local files take precedence over automatic Mapzen downloads.
- Resolution listed here means published/source spacing or TSRE request/cache spacing as explicitly stated; a dense request grid does not imply equivalent native survey accuracy.
- Web Mercator “metres” are map metres. TSRE compensates footprint size by latitude for sampling, and several catalogue names explicitly say “request grid” to avoid implying uniform native resolution.
- DTM/bare-earth products are preferred over DSM products for TSRE, but the global Mapzen fallback is a heterogeneous composite and should not be described as a uniform DTM survey.
- Service availability, access rules and licences can change; entries should be rechecked before shipping.
- “Config-only” means config-only against the current branch capabilities, not an older public snapshot.
