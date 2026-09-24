# TSRE orthophoto / aerial imagery sources — Europe + selected world countries tracker

**Original research date:** 2026-09-22  
**Scope:** global imagery suitable for TSRE distant terrain, European national/regional orthophoto sources suitable for detailed terrain and texture generation, plus selected important non-European TSRE user countries.  
**First global target:** ESA WorldCover Sentinel-2 RGBNIR annual composite 2021 v200, ~10 m.  

This is a research/tracking document, not a final implementation specification. Endpoints, layer names, rate limits, access rules, licences, attribution wording, image dates and available resolutions should be rechecked from live capabilities/provider metadata before shipping a source.

The corresponding elevation research remains in the separate elevation tracker. This file deliberately does **not** replace or modify it.

---

# 1. Why imagery differs from elevation

For elevation TSRE needs numeric samples, so rendered WMS/WMTS map imagery is usually useless. For orthophoto/imagery, the rendered RGB image is exactly what TSRE needs.

That changes the provider priority considerably:

```text
ImagerySource
    |
    +-- WMTS / ArcGIS cached tiles / XYZ/TMS
    |
    +-- WMS / ArcGIS export
    |
    +-- direct COG / HTTP range
    |
    +-- STAC + COG
    |
    +-- indexed/downloaded raster collections
```

For many countries, **WMTS is likely the simplest and most efficient provider**:

- the server has already reprojected/resampled the source;
- tiles are already compressed as JPEG/PNG;
- no GeoTIFF decoder is needed;
- Web Mercator tile pyramids often already contain suitable detailed and distant levels;
- downloading a 10–25 cm national orthophoto as raw raster files is unnecessary when TSRE only needs texture pixels.

WMS is also useful because the server can directly render a requested bounding box at the requested pixel dimensions. It is especially attractive where no suitable WMTS grid exists.

COG/STAC remains strategically important for providers such as SWISSIMAGE and global satellite mosaics, and much of the byte-range / BigTIFF / COG work from elevation should be reusable.

---

# 2. Intended detailed versus distant policy

The imagery source used for **detailed terrain** and **distant terrain** should not necessarily be the same.

## Detailed terrain

Preferred order:

```text
high-resolution national/regional orthophoto
        |
        +-- opportunistic OpenAerialMap imagery where useful
        |
        +-- global Sentinel-derived fallback
```

Typical source resolution: 0.05–1 m.

## Distant terrain

For 32 km and larger terrain footprints, downloading native 10–25 cm aerial photography is normally wasteful.

Preferred direction:

```text
ESA WorldCover Sentinel-2 annual composite (~10 m)
        |
        +-- current Copernicus quarterly mosaic later
        |
        +-- Landsat / other coarse fallback if ever needed
```

A 32 × 32 km area at 10 m contains only about 3200 × 3200 source pixels. That is already ample detail for distant terrain and dramatically smaller than sub-metre orthophoto acquisition.

National orthophoto should therefore **not automatically win for distant terrain simply because it has higher resolution**. The user may explicitly request it, but the default distant profile should favor a coarse prepared mosaic.

---

# 3. Reusable imagery architecture suggested by the survey

## 3.1 WMTS / cached tile provider

Likely the highest-value first local provider family.

Needed concepts:

- capabilities or static layer/tile-grid definition;
- tile matrix set;
- CRS;
- tile size;
- image format, normally JPEG or PNG;
- URL template / KVP endpoint;
- optional API key;
- source/date attribution;
- bounded concurrent downloads;
- persistent tile cache.

Useful examples:

- Czechia ORTOFOTO_WM ArcGIS cached tiles in EPSG:3857;
- Netherlands PDOK aerial imagery WMTS;
- France Géoplateforme WMTS;
- Flanders orthophoto WMTS;
- Poland Geoportal WMTS;
- Lithuania ORT10LT Web Mercator ArcGIS cache;
- Denmark Web Mercator WMTS with API key.

A generic Web Mercator tile reader may cover several of these without country-specific raster code.

## 3.2 WMS / ArcGIS export provider

Useful when:

- the source does not offer WMTS;
- native imagery is much finer than the desired output;
- TSRE wants an exact rectangular texture;
- the service supports server-side reprojection.

For imagery, WMS `GetMap` is not a compromise: server-rendered RGB is a legitimate source product for TSRE.

Potential examples:

- Croatia;
- Slovakia;
- Wallonia;
- Slovenia;
- Estonia;
- Czechia;
- Poland;
- France.

## 3.3 Direct COG / range provider

The elevation project already established the basic value of remote range-readable GeoTIFFs.

Imagery adds new profiles:

- RGB / RGBNIR rather than one numeric band;
- usually UInt8 or UInt16;
- JPEG-in-TIFF is common;
- channel ordering matters;
- radiometric scaling/stretch may be required for satellite reflectance data.

High-value candidates:

- ESA WorldCover Sentinel-2 annual composites;
- SWISSIMAGE COG;
- Sweden STAC/COG when access is granted;
- OpenAerialMap COG assets.

## 3.4 STAC discovery

STAC remains a discovery layer, not a raster format.

Potential flow:

```text
terrain footprint
    -> STAC search
    -> choose newest / preferred imagery item
    -> select RGB asset
    -> COG/range reader
    -> texture generation
```

For imagery, STAC selection policy must additionally consider:

- acquisition date;
- cloud coverage;
- season;
- source resolution;
- overlapping scenes;
- preference for prepared mosaics over individual scenes.

## 3.5 Cache identity

Imagery cache identity should include at least:

- dataset/layer;
- product year or acquisition period where relevant;
- tile matrix / resolution;
- source rendering style if multiple RGB/false-colour variants exist;
- detailed versus distant acquisition profile.

Changing from a 2021 annual Sentinel composite to a 2026 quarterly mosaic must not silently reuse old imagery cache entries.

---

# 4. Global sources

## ESA WorldCover Sentinel-2 annual composites

**Status: ✅ implemented as the first global/distant-terrain WMTS source**

WorldCover publishes Sentinel-2 annual RGBNIR composites for **2020 and 2021**.

Important properties:

- approximately 10 m resolution;
- global land coverage;
- Sentinel-2 L2A source imagery;
- yearly median/percentile composite rather than one raw scene;
- four bands: RGB + NIR;
- UInt16 values in the 0–10000 range;
- EPSG:4326;
- 1° × 1° COG grid;
- public anonymous AWS bucket in `eu-central-1`;
- free of charge for all users;
- WMS/WMTS renderings also exist.

The 2021 v200 product should be the first TSRE target unless testing gives a reason to prefer 2020.

The major advantage is not merely 10 m resolution but **cloud mitigation through annual compositing**. It avoids requiring TSRE to discover and merge dozens of raw Sentinel scenes for ordinary distant terrain.

COG path is preferable for deterministic offline caching and direct control of RGB composition. A WMS/WMTS proof-of-concept may be even easier.

Important implementation note: RGBNIR values are reflectance-like UInt16 values, not ready 8-bit screen RGB. TSRE needs a tested true-colour stretch/gamma/conversion. Do not simply cast UInt16 to UInt8.

Sources:

- https://esa-worldcover.org/en/data-access
- https://registry.opendata.aws/esa-worldcover/

## Copernicus Sentinel-2 Level-3 Quarterly Mosaics

**Status: 🟡 excellent future current-imagery source; OAuth/API access**

Copernicus Data Space now exposes prepared quarterly Sentinel-2 mosaics:

- B02/B03/B04/B08 at 10 m;
- 3-month composites;
- cloud handling is already part of the mosaic algorithm;
- an observations band and data mask are available;
- pixels with no valid observations remain NoData;
- access is through Sentinel Hub BYOC / Processing API;
- OAuth client credentials are required.

These would solve the biggest weakness of WorldCover 2021: age.

Recommended later policy:

```text
choose one preferred quarter for the whole terrain texture
        |
        +-- use adjacent quarter only for holes if needed
        |
        +-- avoid arbitrary per-pixel season mixing
```

Season consistency matters. A patchwork of spring, summer and winter observations can look worse than a slightly older but visually coherent annual mosaic.

Sources:

- https://documentation.dataspace.copernicus.eu/Data/Others/Sentinel2_Mosaic_access.html
- https://documentation.dataspace.copernicus.eu/Data/Others/Sentinel2_Mosaic_Algorithm.html

## Raw Sentinel-2 L2A COG + STAC

**Status: 🟡 powerful fallback / advanced mode, not first choice**

Anonymous public Sentinel-2 L2A COG archives exist with Earth Search STAC discovery.

Advantages:

- current imagery;
- global;
- 10 m visible/NIR bands;
- COG + STAC architecture fits TSRE.

Disadvantage: TSRE would need scene selection and cloud handling itself.

Use this only after prepared annual/quarterly mosaics unless a user specifically wants acquisition-date control.

Source:

- https://registry.opendata.aws/sentinel-2-l2a-cogs/

## OpenAerialMap

**Status: 🟡 opportunistic high-resolution global supplement**

OpenAerialMap is a worldwide catalogue of openly licensed aerial/satellite/UAV imagery rather than one uniform global layer.

Properties:

- CC BY 4.0 imagery;
- API supports metadata search;
- TMS endpoints are available for many items;
- raw GeoTIFF download is available;
- processed OAM imagery is stored as COG;
- coverage and date/resolution are highly irregular.

This makes it useful as an optional source:

```text
national ortho unavailable
    -> check OpenAerialMap
    -> otherwise WorldCover
```

It should not be assumed to cover every route.

Sources:

- https://openaerialmap.org/
- https://openaerialmap.org/legal/
- https://docs.openaerialmap.org/api/api/

## Landsat Collection 2

**Status: 🟠 useful historical/coarse fallback, lower priority**

USGS Collection 2 products are distributed as COGs and have STAC metadata.

For modern TSRE distant terrain, Sentinel-2 at 10 m is more attractive than ordinary Landsat 30 m RGB.

Landsat's main strategic advantage is **history**. It may later be valuable for routes intended to depict older periods.

Sources:

- https://www.usgs.gov/landsat-missions/landsat-collection-2
- https://www.usgs.gov/landsat-missions/spatiotemporal-asset-catalog-stac

---

# 5. Status legend for local sources

The tracker statuses primarily describe research and implementation
attractiveness. Runtime support is stated explicitly on implemented entries;
WorldCover and Poland are the first implemented sources.

| Status | Meaning |
|---|---|
| 🟢 | Strong automatic candidate: public/open, technically straightforward |
| 🟡 | Good source; requires a small reusable feature, API key, or additional live validation |
| 🟠 | Valuable data but access/licensing/provider workflow is significantly more complicated |
| 🔴 | Poor candidate for general automatic TSRE distribution |
| ⚪ | No suitable source confirmed in this research pass / deeper research still needed |

---

# 6. Strong first local candidates

## Poland — GUGiK Geoportal

**Status: ✅ implemented through the generic projected ArcGIS MapServer export provider**

One of the strongest candidates.

Current orthophoto program includes:

- classic orthophoto around 5 cm for urban areas on a roughly two-year cycle;
- around 10 cm for non-urban GUGiK imagery on a longer cycle;
- 25 cm ARiMR imagery on a roughly two-year cycle;
- selected products down to 3 cm;
- standard and high-resolution service layers;
- true-ortho in selected areas;
- free download and use.

Public services:

```text
Standard WMS:
https://mapy.geoportal.gov.pl/wss/service/PZGIK/ORTO/WMS/StandardResolution

Standard WMTS:
https://mapy.geoportal.gov.pl/wss/service/PZGIK/ORTO/WMTS/StandardResolution

High-resolution WMS:
https://mapy.geoportal.gov.pl/wss/service/PZGIK/ORTO/WMS/HighResolution

High-resolution WMTS:
https://mapy.geoportal.gov.pl/wss/service/PZGIK/ORTO/WMTS/HighResolution

True ortho WMS:
https://mapy.geoportal.gov.pl/wss/service/PZGIK/ORTO/WMS/TrueOrtho
```

Provider documentation explicitly notes an important tradeoff: WMTS uses prepared tiles and can appear blurred at extreme zoom, while WMS renders from the original orthophoto files.

Current TSRE policy:

- MapServer export for detailed terrain because bounded images are much faster
  than hundreds of 256-pixel EPSG:3857 WMTS requests and proved more reliable
  than the WMS front end;
- selectable 4096, 2048 and 1024 total sizes; the 4096 mode uses four parallel
  2048 exports and took 8.0 seconds versus 23.2 seconds for one 4096 export in
  a direct comparison;
- bounded retries because the service occasionally returns transient empty 404
  responses;
- original-sheet WFS downloads remain a future offline/import option.

Sources:

- https://www.geoportal.gov.pl/en/data/orthophotomap-orto/
- https://www.geoportal.gov.pl/en/services/view-services-wms-and-wmts/

## Czechia — ČÚZK Ortofoto

**Status: ✅ implemented through the generic ArcGIS MapServer export provider**

The initial WMTS entry required roughly 400--500 requests for one detailed
terrain tile. TSRE now uses bounded MapServer exports, with selectable 4096,
2048 and 1024 total sizes. The 4096 mode is composed from four concurrent 2048
blocks; a direct comparison took 5.1 seconds instead of 7.8 seconds for one
4096 export. The lower modes use one request. The 12.5 cm source is
detailed-terrain approved; the rolling cache expires after 30 days.

Current national orthophoto:

- approximately 12.5 cm spatial resolution;
- CC BY 4.0;
- no public-access restriction;
- WMS available;
- cached ArcGIS Web Mercator service available.

Native WMS:

```text
https://ags.cuzk.gov.cz/arcgis1/services/ORTOFOTO/MapServer/WMSServer
```

Particularly attractive cached service:

```text
https://ags.cuzk.gov.cz/arcgis1/rest/services/ORTOFOTO_WM/MapServer
```

The latter is:

- EPSG:3857;
- 256 × 256 tiles;
- fused map cache;
- JPEG/PNG mixed;
- useful LOD pyramid;
- directly compatible with ordinary Web Mercator imagery logic.

This should be one of the easiest TSRE proof-of-concept local providers.

Sources:

- https://geoportal.gov.cz/php/micka/record/basic/CZ-CUZK-ORTOFOTO-R
- https://ags.cuzk.gov.cz/arcgis1/rest/services/ORTOFOTO_WM/MapServer

## Netherlands — PDOK Luchtfoto RGB Open

**Status: ✅ implemented through the generic tiled-WMS provider**

TSRE uses `Actueel_ortho25`, the latest complete nationwide 25 cm RGB layer,
through WMS in EPSG:3857. PDOK limits one image to 2500 pixels, so the 4096
quality setting is composed from four parallel 2048 requests; a live run took
about 2.8 seconds. The 2048 and 1024 modes use one request. The 8/5 cm winter
layer remains optional future work because it exceeds the useful detail of the
current 0.5 m terrain overlay. The rolling cache expires after 30 days.

Excellent open national source.

Current product characteristics:

- annual national imagery;
- summer imagery: 25 cm;
- winter imagery: 8 cm, with some 5 cm areas;
- RGB open data since 2016;
- WMS and WMTS.

Endpoints:

```text
WMS:
https://service.pdok.nl/hwh/luchtfotorgb/wms/v1_0

WMTS:
https://service.pdok.nl/hwh/luchtfotorgb/wmts/v1_0
```

PDOK's tiled service is a natural first choice for TSRE.

Sources:

- https://www.pdok.nl/ogc-webservices/-/article/pdok-luchtfoto-rgb-open-
- https://www.pdok.nl/-/luchtfoto-2025-nu-beschikbaar-bij-pdok

## France — IGN / Géoplateforme orthophotos

**Status: 🟢**

France has public national aerial imagery through the current Géoplateforme services.

Important layers include:

```text
ORTHOIMAGERY.ORTHOPHOTOS
HR.ORTHOIMAGERY.ORTHOPHOTOS
ORTHOIMAGERY.ORTHOPHOTOS.BDORTHO
```

The service catalogue exposes WMS-Raster and WMTS. The high-resolution BD ORTHO RGB layer is described at 20 cm.

Current map catalogue:

- https://cartes.gouv.fr/
- WMTS infrastructure: https://data.geopf.fr/wmts

This is an attractive generic WMTS target and avoids adding Lambert-93 support merely to obtain imagery.

Sources:

- https://cartes.gouv.fr/
- https://cartes.gouv.fr/aide/fr/partenaires/ign/generalites-ign/actualites/

## Belgium — Flanders

**Status: 🟢**

Digitaal Vlaanderen produces annual winter orthophotos covering Flanders and Brussels.

Current 2026 work mosaic:

- 15 cm;
- updated progressively;
- public;
- WMS/WMTS;
- Belgian Lambert 72 source data, but rendered tile services avoid requiring local projection support.

Current work-layer WMTS:

```text
https://geo.api.vlaanderen.be/OFW/wmts?layers=ofw
```

The stable/latest final mosaic should be preferred over the interim work mosaic for normal users when available, because Digitaal Vlaanderen notes that the interim product has lower geometric/visual quality.

Source:

- https://www.vlaanderen.be/datavindplaats/catalogus/orthofotowerkbestand-middenschalig-winteropnamen-kleur-202601-vlaanderen

## Belgium — Wallonia

**Status: 🟢**

SPW publishes public orthophotos.

2025 spring coverage:

- full Wallonia;
- 25 cm;
- RGB + NIR source acquisition;
- public/HVD;
- map services and downloads.

2025 summer imagery also exists for partial coverage.

Catalogue:

- https://geoportail.wallonie.be/catalogue/c28d5647-4b33-4cec-baa5-dda1f8443678.html
- https://geoportail.wallonie.be/catalogue/baad11a6-f55b-4333-acc1-206f3c76fb22.html

ArcGIS/WMS service should be preferred initially over downloading full RGB raster products.

## Spain — IGN/CNIG PNOA

**Status: 🟢**

PNOA is an excellent national orthophoto source.

Current product family:

- 25 cm in most current acquisitions;
- 15 cm in selected high-resolution production;
- regular nationwide update program;
- WMS/WMTS;
- current definitive orthophotos also distributed as COG.

Services:

```text
WMS:
https://www.ign.es/wms-inspire/pnoa-ma

WMTS:
https://www.ign.es/wmts/pnoa-ma
```

This is useful both as a WMTS provider and later as another image-COG case.

Sources:

- https://pnoa.ign.es/pnoa-imagen/especificaciones-tecnicas
- https://pnoa.ign.es/pnoa-imagen/estado-del-proyecto

## Slovakia — GKÚ Orthophotomosaic

**Status: ✅ implemented through the generic ArcGIS MapServer export provider**

Imagery distribution is substantially easier for TSRE than the Slovak elevation package because rendered WMS/WMTS is perfectly usable.

Third production cycle:

- 2023 west;
- 2024 centre;
- 2025 east;
- 15 cm;
- RGBN, 8-bit;
- TIFF + TFW for downloads;
- EPSG:5514 for native files;
- CC BY 4.0 web services;
- entire country now represented by the third-cycle acquisition sequence.

Public services:

```text
WMS:
https://zbgisws.skgeodesy.sk/zbgis_ortofoto_wms/service.svc/get

WMTS S-JTSK:
https://zbgisws.skgeodesy.sk/zbgis_ortofoto_wmts_sjtsk/service.svc/get
```

TSRE uses the official cached Web Mercator ArcGIS MapServer export. The UI
offers 4096, 2048 and 1024 total sizes. The 4096 mode uses four concurrent 2048
blocks: a Bratislava comparison took 3.9 seconds, versus 11.0 seconds for one
4096 export. The official WMS took 8.3 seconds for one 4096 request and did not
benefit from four-way parallelism. The rolling cache expires after 30 days.

Huge RGB/RGBN ZIPs are also downloadable, but the bounded export is much more
appropriate for an interactive terrain preview.

Sources:

- https://www.gku.sk/geoportal-en/zbgis/orthophotomosaic/3rd-cycle/
- https://www.gku.sk/gku/produkty-sluzby/zbgis/wms.html
- https://zbgis.skgeodesy.sk/zbgis/rest/services/Ortofoto/MapServer
- https://www.skgeodesy.sk/gku/produkty-sluzby/na-stiahnutie/zbgis.html

## Lithuania — ORT10LT

**Status: 🟢**

Lithuania has a particularly convenient public cached Web Mercator service:

```text
https://www.geoportal.lt/arcgis/rest/services/NZT/ORT10LT_Web_Mercator_102100/MapServer
```

Properties:

- EPSG:3857;
- fused 256 × 256 map cache;
- JPEG/PNG mixed;
- public service;
- current ORT10LT includes 2022–2025 aerial imagery plus some satellite imagery.

This is another strong generic ArcGIS cached-tile case.

Sources:

- https://www.geoportal.lt/arcgis/rest/services/NZT/ORT10LT_Web_Mercator_102100/MapServer
- https://www.geoportal.lt/metadata-catalog/

---

# 7. Strong COG / file-oriented imagery sources

## Switzerland + Liechtenstein — SWISSIMAGE

**Status: 🟢 strongest European direct-COG imagery candidate**

SWISSIMAGE is exceptionally attractive for validating image COG support:

- latest Swiss orthophoto mosaic;
- free downloads for data from 2017 onward;
- EPSG:2056;
- 0.10 m or 0.25 m depending on area;
- COG;
- RGB 3 × 8-bit;
- JPEG95 compression;
- Liechtenstein included in the same product/update stream.

This requires **JPEG-compressed RGB TIFF** support, which is a different raster profile from the numeric LZW/Deflate COGs already used for elevation.

Source:

- https://www.swisstopo.admin.ch/en/orthoimage-swissimage-10

## Austria — BEV Orthophoto

**Status: 🟡**

BEV publishes nationwide current orthophotos:

- about 20 cm GSD for modern imagery;
- 3-year update cycle;
- whole Austria;
- raster delivery including TIFF/JPEG;
- some current digital products are available as free downloads.

The viewing-service side has registration/access considerations, so the exact automatic TSRE route needs another endpoint-level pass.

A direct downloadable-file/index provider may be more appropriate than relying on the BEV WMS.

Sources:

- https://www.bev.gv.at/en/Services/Products/Remote-Sensing.html
- https://www.bev.gv.at/Services/Produkte/Luftbildprodukte/Orthophoto-Farbe.html

## Estonia

**Status: 🟢**

Estonia is attractive in both web-service and file forms.

Current orthophoto acquisition:

- commonly 20 cm;
- very high-resolution urban campaigns can be finer;
- nationwide map-sheet downloads are free;
- newest orthophoto mosaic is exposed through public WMS;
- open-data use with attribution.

Public WMS documentation:

- https://geoportaal.maaruum.ee/eng/services/public-wms-wfs-p346.html

Downloads:

- https://geoportaal.maaruum.ee/eng/spatial-data/orthophotos/download-orthophotos-p662.html

This could start as WMS and later gain indexed-file support.

---

# 8. Good sources with authentication or access friction

## Finland — NLS orthophotos

**Status: 🟡**

Excellent national data:

- whole Finland;
- 0.5 m RGB;
- CC BY 4.0;
- typical update cycle every 3 years, Northern Lapland slower;
- TIFF downloads;
- WMS/WMTS;
- WCS can return 0.5 m GeoTIFF orthophotos;
- Web Mercator is supported by map services.

Open API access requires an NLS API key for relevant interfaces. TSRE already has elevation-side NLS secret handling, but imagery should keep its provider/configuration independent.

Sources:

- https://www.maanmittauslaitos.fi/en/maps-and-spatial-data/datasets-and-interfaces/product-descriptions/orthophotos
- https://www.maanmittauslaitos.fi/en/maps-and-spatial-data/datasets-and-interfaces/map-interface-services/map-image-service-wms-wmts
- https://www.maanmittauslaitos.fi/en/rajapinnat/api-avaimen-ohje

## Denmark — GeoDanmark spring orthophoto

**Status: 🟡**

Technically excellent:

- latest service currently uses 2025 imagery;
- annual spring acquisition before leaf-out;
- standard 15 cm GSD resampled to 12.5 cm;
- some municipalities choose 10 cm;
- RGBNIR source;
- native EPSG:25832;
- WMS and WMTS;
- dedicated Web Mercator WMTS exists.

Access uses API key or OAuth.

Web Mercator WMTS:

```text
https://wmts.datafordeler.dk/GeoDanmarkOrto/orto_foraar_webm/1.0.0/WMTS?apikey=...
```

This is a very clean authenticated tile-provider example.

Sources:

- https://datafordeler.dk/dataoversigt/geodanmark-ortofoto/ortofoto-foraar-wms/
- https://datafordeler.dk/dataoversigt/geodanmark-ortofoto/ortofoto-foraar-wmts/
- https://datafordeler.dk/dataoversigt/geodanmark-ortofoto/ortofoto-foraar-web-mercator-wmts/

## Sweden — Lantmäteriet Ortofoto Nedladdning

**Status: 🟠**

Modern distribution architecture:

- STAC API;
- COG;
- no licence fee;
- current endpoint:
  `https://api.lantmateriet.se/stac-bild/v1`

However:

- access must be ordered through Geotorget;
- account permission is required;
- the product is subject to a legal/GDPR review.

Technically this is close to ideal once authorized, but it is not suitable as a frictionless built-in public default.

Source:

- https://geotorget.lantmateriet.se/geodataprodukter/ortofoto-nedladdning-api

## Norway — Norge i bilder

**Status: 🟠**

Excellent nationwide imagery infrastructure, but new services use controlled tokens.

Current services include:

```text
WMS:
https://services.norgeibilder.no/wms/ortofoto

WMTS UTM32:
https://tilecache.norgeibilder.no/wmts/utm32_euref89

WMTS UTM33:
https://tilecache.norgeibilder.no/wmts/utm33_euref89

WMTS UTM35:
https://tilecache.norgeibilder.no/wmts/utm35_euref89

WMTS Web Mercator:
https://tilecache.norgeibilder.no/wmts/webmercator
```

A time-limited token is required. Token creation is tied to a GeoID user with service access.

The older `wms.geonorge.no` services are being kept in parallel through 2026 but should not be used as a new TSRE dependency.

Source:

- https://www.geonorge.no/nib

## Latvia

**Status: 🟠**

National orthophotos are available, but recent services require an authorization step.

Current relevant coverage:

- RGB/CIR 2022–2024;
- nationwide;
- WMS;
- free data, but authorized access after signing the licence;
- older cycles also exist;
- native raster files use LKS-92 TM map-sheet organisation.

Source:

- https://www.lgia.gov.lv/en/wms-servisi
- https://www.lgia.gov.lv/en/orthophoto-maps-0

---

# 9. Other strong national/regional cases

## Portugal

**Status: 🟢 / 🟡 pending exact current WMTS endpoint validation**

The new 2025 mainland orthophoto is an excellent dataset:

- 25 cm;
- RGB + NIR;
- PT-TM06 / ETRS89;
- 8 × 5 km files;
- open/high-value dataset;
- WMTS distribution is listed by DGT.

The imagery side may be easier than the current login-gated elevation download workflow because the official orthophoto catalogue explicitly exposes viewing distribution.

Source:

- https://www.dgterritorio.gov.pt/atividades/cartografia/cartografia-topografica/ortofotos/ortofotos-digitais
- https://snig.dgterritorio.gov.pt/

Interesting extra: Portugal has also experimented with national Sentinel-2 cloudless mosaics, reinforcing the usefulness of a separate coarse/distant imagery profile.

## Croatia

**Status: 🟢**

DGU exposes current orthophotos to anonymous users through WMS.

Current endpoint:

```text
https://geoportal.dgu.hr/services/inspire/orthophoto_2025_2026/wms
```

Older yearly/cycle services remain available as well.

This is a strong simple WMS-provider case.

Source:

- https://geoportal.dgu.hr/cms/en/data-and-services/

## Slovenia

**Status: 🟢 / 🟡**

GURS orthophoto products include:

- DOF025 — RGB 0.25 m;
- DOF050 — RGB 0.50 m;
- DOF050IR — false-colour 0.50 m;
- TIFF distribution;
- WMS preview/service;
- public/open-data publication.

WMS capabilities:

```text
https://ipi.eprostor.gov.si/wms-si-gurs-ins/ows?service=wms&version=1.1.1&request=GetCapabilities
```

Before automatic persistent caching, recheck the exact current reuse/download terms and preferred current layer.

Source:

- https://podatki.gov.si/dataset/ortofoto

## Luxembourg

**Status: 🟢**

Luxembourg's 2025 official orthophotos are very high quality:

- summer and winter products;
- <= 10 cm GSD;
- public WMS;
- RGB and infrared variants;
- country-wide coverage.

The WMS route is much easier for TSRE than implementing JPEG2000 solely to consume the large downloadable country mosaics.

Public map WMS infrastructure:

- https://wms.geoportail.lu/public_map_layers/service

---

# 10. Countries where the obvious national route is less attractive

## Germany

**Status: 🟠 nationally; Länder research needed**

BKG has a technically excellent nationwide DOP20:

- 20 cm;
- WMS;
- WMTS;
- nationwide.

But general access requires a licence agreement and fees (published from €125 for the WMTS product), so it is not a good default TSRE source.

As with elevation, the better path is likely **Land-by-Land open imagery services**. Several Länder publish DOP as open data; a separate deep German regional pass is justified.

Sources:

- https://gdz.bkg.bund.de/index.php/default/webdienste/digitale-orthophotos/wmts-digitale-orthophotos-bodenauflosung-20cm-wmts-dop.html
- https://gdz.bkg.bund.de/index.php/default/digitale-geodaten/digitale-orthophotos/digitale-orthophotos-bodenauflosung-20-cm-dop20.html

## Ireland

**Status: 🔴 / 🟠 for national automatic use**

Tailte Éireann MapGenie provides an excellent nationwide current orthophoto service:

- 25 cm;
- full 26-county coverage;
- WMS/WMTS/tiled services;
- historical imagery also available.

But MapGenie is a controlled/premium service rather than an anonymous open source suitable for a default TSRE integration.

Source:

- https://tailte.ie/services/mapgenie/

## England

**Status: 🟠 as a national default; useful project imagery**

Environment Agency Vertical Aerial Photography is open and high quality:

- 10–50 cm;
- RGB/NIR/4-band;
- orthorectified;
- captures since 2006.

However it is collected project-by-project, with coverage ranging from a few to hundreds of square kilometres, rather than one continuously current national mosaic.

Source:

- https://www.data.gov.uk/dataset/4921f8a1-d47e-458b-873b-2a489b1c8165/vertical-aerial-photography

Wales, Scotland and Northern Ireland need a separate imagery-focused review rather than assuming that their elevation portals expose equivalent current orthophoto under equally open terms.

---

# 11. Other countries — first-pass status

This table is intentionally conservative. “Not confirmed” means this imagery pass did not yet establish a current, open, technically suitable automatic route.

| Country / territory | First-pass finding | Status |
|---|---|---|
| Albania | National aerial imagery exists through ASIG; current automatic/public service terms need a deeper check | ⚪ / 🟠 |
| Andorra | National imagery portals exist; direct modern orthophoto service/download path needs review | ⚪ |
| Belarus | No suitable open automatic national orthophoto source confirmed | ⚪ |
| Bosnia and Herzegovina | Fragmented mapping authorities; no clean nationwide automatic source confirmed | ⚪ |
| Bulgaria | National cadastral/mapping imagery exists, but no suitable open national automatic ortho path confirmed | ⚪ |
| Cyprus | DLS/INSPIRE imagery should be investigated; no implementation-ready source confirmed yet | ⚪ |
| Greece | Ktimatologio/government imagery exists, but licensing/API suitability for automated TSRE use needs review | ⚪ / 🟠 |
| Hungary | National orthophoto exists; open automatic access/licence needs a dedicated review | ⚪ / 🟠 |
| Iceland | Current national high-resolution aerial base-map work is promising; revisit when the new program's download/services are fully established | 🟡 future |
| Italy | National/regional orthophoto distribution is fragmented and licence-sensitive; regional review needed | 🟠 |
| Malta | No suitable source confirmed in this pass | ⚪ |
| Moldova | No suitable source confirmed in this pass | ⚪ |
| Romania | National imagery exists, but no clean open current automatic source confirmed in this pass | ⚪ / 🟠 |
| Serbia | High-resolution imagery products exist; public automatic reuse/access needs review | 🟠 |
| Turkey | National imagery services exist but access/reuse is not an obvious anonymous-open path | 🟠 |
| Ukraine | No current nationwide source selected for TSRE in this pass | ⚪ |

---


# 12. Outside Europe — main TSRE user countries and strong additions

This section records non-European countries only where the result is important to TSRE users or a particularly useful provider architecture was found. A light pass also checked several other large countries; weak/fragmented results are deliberately not listed here.

## United States — USGS / USDA NAIP

**Status: ✅ implemented for the contiguous United States through generic WMTS and ArcGIS ImageServer providers**

The United States is one of the easiest high-resolution non-European imagery targets.

TSRE now includes two contiguous-US detailed-terrain choices. The fast source
uses reusable cached WMTS tiles. Live validation found that this cache stops at
zoom 16: the delivered ground pixel is therefore roughly 1.5--2.2 m across the
configured bounds, despite the primarily 0.6 m underlying NAIP mosaic. Finer
WMTS requests return HTTP 400.

The high-resolution source uses the dynamic `USGSNAIPPlus` ImageServer. It
offers 4000, 2048 and 1024-pixel total images, corresponding to approximately
0.5, 1 and 2 m terrain grids for a detailed tile. The 4000 mode is composed
from four parallel 2000-pixel exports. A bounded comparison took 7.3 seconds,
versus 18.5 seconds for one 4000 export. Both rolling sources use a 30-day local
expiry. Alaska, Hawaii and US territories remain separate future entries
because their source imagery and licensing differ from contiguous-US NAIP.

### The National Map cached imagery service

USGS exposes a public cached imagery-only basemap:

```text
https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer
```

Important properties:

- ArcGIS `MapServer`;
- **single fused Web Mercator cache**, EPSG:3857;
- 256 × 256 tiles;
- WMTS is exposed by the service;
- imagery is predominantly USDA **NAIP** over the conterminous United States;
- additional high-resolution orthoimagery is used where available;
- the current NAIP-oriented imagery layer is described by USGS as primarily **0.6 m, 4-band imagery presented in natural colour**;
- geometrically corrected/orthorectified;
- USGS describes the imagery layer as **Public Domain**.

The current National Map imagery service metadata still contains some older source-vintage wording, so before shipping TSRE should verify the current cache date/content rather than assume every tile is the newest NAIP acquisition.

For TSRE this is architecturally excellent: it can use the same generic Web-Mercator cached-tile provider planned for Czechia and Lithuania.

### NAIP Plus dynamic ImageServer and source data

USGS also exposes the current dynamic mosaic directly:

```text
https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPPlus/ImageServer
```

The service advertises 0.3 m pixels, natural-colour rendering, 4-band UInt8
data, and exports up to 4000 x 4000 pixels. This makes approximately 0.5 m the
useful maximum for one 2 km TSRE terrain image without splitting it into
multiple requests.

NAIP itself is also an excellent downloadable product:

- 2003–present programme;
- 1 m historically, standard changed to **0.6 m** beginning in 2018;
- optional 0.3 m acquisitions in some coastal states;
- RGB or 4-band RGB+NIR;
- typically refreshed on a cycle of three years or less;
- public domain;
- GeoTIFF and JPEG2000 are distributed by USGS.

Direct EarthExplorer downloads require an account, so the cached National Map service is much more attractive for zero-configuration TSRE imagery.

Recommended TSRE route:

```text
detailed terrain:
USGSNAIPPlus ImageServer for maximum detail
        |
        +-- USGSImageryOnly Web Mercator cache for speed/reuse
        |
        +-- WorldCover global fallback

distant terrain:
WorldCover 10 m by default
        |
        +-- USGS imagery only if explicitly requested
```

There is little reason to download 0.6 m NAIP over a 32 km distant-terrain footprint by default.

Sources:

- https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer
- https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPPlus/ImageServer
- https://www.usgs.gov/media/images/tnmcorps-naip-imagery-layer
- https://www.usgs.gov/centers/eros/science/usgs-eros-archive-aerial-photography-national-agriculture-imagery-program-naip
- https://apps.nationalmap.gov/services/

## India — ISRO/NRSC Bhuvan high-resolution mosaic

**Status: 🔴 / 🟠 technically excellent, but unsuitable for built-in automatic TSRE use under the current terms**

India has much better official imagery than a first search might suggest.

In 2025 Bhuvan announced a new country-scale high-resolution natural-colour mosaic built from Cartosat-2E/2C/2D/2F imagery:

- scenes acquired roughly **2022–2024**;
- about **40,000 scenes**;
- orthorectified;
- approximately **1.5 m** processed spatial resolution;
- natural-colour composite;
- radiometrically balanced / contrast enhanced;
- approximate CE90 planimetric accuracy of 5 m;
- available for viewing in the Bhuvan EO portal under `HR Data (1m)` / `2024-22`.

This would otherwise be an outstanding TSRE detailed-terrain source.

### Current blocker: Bhuvan terms

The current Bhuvan Terms of Service are a serious blocker for an automatic third-party TSRE integration. Unless prior written authorization is obtained, the terms prohibit, among other things:

- accessing content through technology/means other than those provided by Bhuvan;
- making derivative works;
- redistributing content;
- mass downloads or bulk feeds of imagery/content.

Bhuvan documentation also says high-resolution 1 m imagery is for **visualization**, whereas moderate-resolution datasets are the products offered for ordinary free download.

Historical developer/forum documentation shows Bhuvan satellite imagery has been made available through WMS/tiled services to external applications, but it also instructed developers to contact Bhuvan/NRSC for intended satellite-imagery use. That is not a sufficient basis for shipping an unrestricted TSRE provider against the current terms.

Therefore:

- do **not** scrape or reverse-engineer the Bhuvan viewer;
- do **not** build automatic caching against the high-resolution mosaic without explicit NRSC permission;
- WorldCover/Sentinel should remain the default legal global imagery path for India;
- Bhuvan can be revisited if NRSC provides written authorization or a documented third-party imagery API/licence appropriate for TSRE.

Sources:

- https://bhuvan.nrsc.gov.in/updates/bhuvan_jan2025.html
- https://bhuvan.nrsc.gov.in/terms.php
- https://bhuvan.nrsc.gov.in/wiki/index.php/Frequently_Asked_Questions
- https://bhuvan.nrsc.gov.in/wiki/index.php/How_to_use_WMS_services

## Japan — GSI seamless / year-by-year aerial-photo tiles

**Status: 🟢 / 🟡 very useful official XYZ imagery, but heterogeneous coverage/source dates**

Japan produced the strongest result in the wider non-European scan.

GSI (Geospatial Information Authority of Japan) publishes ordinary XYZ photo tiles directly.

### Latest seamless photography

```text
https://cyberjapandata.gsi.go.jp/xyz/seamlessphoto/{z}/{x}/{y}.jpg
```

Properties:

- zoom levels 14–18;
- combines GSI orthophotos with forestry imagery, local-government aerial photography, simplified aerial photography and other imagery;
- some gaps/areas can fall back to satellite imagery;
- directly usable as an XYZ source by a generic TSRE tile provider.

### Year-by-year imagery

GSI also exposes yearly photo tiles from 2007 onward:

```text
https://cyberjapandata.gsi.go.jp/xyz/nendophoto{year}/{z}/{x}/{y}.png
```

with zoom levels 14–18.

This is particularly interesting for TSRE because the user could choose a specific imagery year rather than always taking the latest seamless mosaic.

Older historical photo tile sets are also exposed for several periods back to the mid-20th century, making Japan unusually attractive for historical-route work.

### Reuse caveat

GSI lists the photography tile family under content usable according to its content rules, with source attribution required. Some tiles contain imagery from third parties and require additional attribution; for example, small parts of the 2023 imagery include Axelspace GRUS imagery.

The seamless product is not a uniform one-survey national orthophoto, so TSRE should expose acquisition/source metadata where practical and should not promise uniform resolution/date.

Recommended initial source:

```text
GSI seamlessphoto XYZ
```

and add year-specific selection later if users want historical matching.

Sources:

- https://maps.gsi.go.jp/development/ichiran.html
- https://maps.gsi.go.jp/help/termsofuse.html
- https://www.gsi.go.jp/gazochosa/gazochosa40001

---

# 13. Cloud and seasonal issues

Global imagery has problems that elevation does not:

- clouds;
- cloud shadows;
- haze;
- snow;
- varying vegetation;
- varying sun angle;
- scene boundaries;
- acquisition dates;
- radiometric differences.

For TSRE, **visual consistency often matters more than absolute recency**.

Recommended policy:

1. Prefer prepared annual/quarterly mosaics over raw scenes.
2. Keep one season/product consistent over the entire generated terrain texture.
3. Use another period only to fill true holes, not simply because an individual pixel is newer.
4. Store product/year/quarter in cache identity and metadata.
5. Expose acquisition/product information to the user where practical.
6. Avoid promising “cloud-free”; even good mosaics can contain NoData or residual artifacts.

For detailed national orthophotos, acquisition seams and year boundaries can still occur, but providers generally deliver a visually coherent mosaic already.

---

# 14. RGB generation and resampling

## National orthophotos

Most web services already return display-ready JPEG/PNG RGB. TSRE can composite/resample those directly.

## Satellite composites

WorldCover RGBNIR stores four UInt16 bands. TSRE needs an explicit rendering transform:

```text
B04 -> R
B03 -> G
B02 -> B

reflectance/scaled UInt16
    -> clamp / stretch
    -> optional gamma
    -> 8-bit RGB texture
```

The exact stretch should be measured against real WorldCover tiles and compared with the provider's own true-colour WMS rendering.

## Downsampling

For detailed imagery, oversampling a source to a much larger TSRE texture adds no information.

For distant terrain, selecting approximately the source resolution needed by the destination texture is preferable to fetching high-resolution aerial pixels and averaging enormous areas locally.

WMTS already gives us a natural pyramid; choose the closest suitable zoom level rather than always fetching maximum zoom.

---

# 15. Suggested implementation order

## Phase A — prove generic web imagery with a simple public tile source

Best candidates:

1. **Czechia ORTOFOTO_WM** — cached ArcGIS Web Mercator, public, 256 px tiles.
2. **Netherlands PDOK** — public WMTS.
3. **France Géoplateforme** — public WMTS.
4. **Flanders** — public WMTS.
5. **Poland** — public WMTS/WMS.

This phase should establish:

- image tile cache;
- Web Mercator tile math;
- JPEG/PNG decode;
- composition across tile boundaries;
- texture resampling;
- cancellation/progress;
- attribution metadata;
- detailed versus distant requested zoom.

## Phase B — global distant terrain

Implement **ESA WorldCover 2021 RGBNIR annual composite**.

Possible first route:

- use its true-colour WMS/WMTS as a quick functional proof;
- then add direct COG acquisition for deterministic source caching and reuse of Tier-C raster infrastructure.

Validate:

- real COG compression/layout;
- RGB band ordering;
- reflectance-to-display conversion;
- NoData;
- seams at 1° tile boundaries;
- 32 km and 100 km²+ distant terrain footprints;
- traffic/cache size.

## Phase C — WMS provider

Add generic WMS `GetMap` for sources without ideal tile services:

- Croatia;
- Wallonia;
- Slovakia;
- Slovenia;
- Estonia;
- Poland high-resolution mode.

## Phase D — image COG profiles

High-value targets:

- SWISSIMAGE JPEG95 COG;
- WorldCover UInt16 RGBNIR COG;
- OpenAerialMap COG;
- Sweden STAC/COG when authenticated.

This requires broader TIFF/image decoding than elevation:

- RGB/RGBA;
- UInt8/UInt16;
- JPEG-in-TIFF;
- multiband COG;
- potentially planar/interleaved layouts.

## Phase E — authenticated imagery

Reuse the settings/secrets concepts already proven by elevation, while keeping imagery credentials/source IDs separate:

- Finland;
- Denmark;
- Norway;
- Sweden.

---

# 16. Practical shortlist

## First global source

**ESA WorldCover Sentinel-2 RGBNIR 2021 v200**

Why:

- ~10 m is ideal for distant terrain;
- global;
- annual composite greatly reduces cloud problems;
- public/no-login;
- COG;
- manageable data volume per TSRE terrain area.

## First local source

Two especially good choices:

**Czechia ORTOFOTO_WM**
- simplest Web Mercator cached ArcGIS path;
- public;
- high resolution;
- useful test of generic cached-tile support.

**Netherlands PDOK Luchtfoto RGB**
- standard WMTS;
- public;
- excellent nationwide imagery;
- straightforward generic OGC path.

Implementing both would test that the imagery code is not ArcGIS-specific.

## First image COG source

**SWISSIMAGE**
- public;
- high resolution;
- actual RGB COG;
- validates JPEG-compressed image TIFF support.

## First authenticated tile source

**Denmark Web Mercator WMTS**
- existing API-key concepts are already familiar from elevation;
- server already gives Web Mercator JPEG tiles.

---

# 17. Main conclusions

1. **Global distant imagery is solved well enough by Sentinel-derived products.**  
   We do not need a mythical free worldwide 25 cm orthophoto.

2. **WorldCover 2021 should be the first global implementation.**  
   It trades recency for consistency and low cloud contamination, which is a good trade for distant terrain.

3. **Local imagery is often easier than local elevation.**  
   WMTS/WMS/ArcGIS rendered map services are valid final imagery rather than merely visualisation products.

4. **A generic WMTS/Web-Mercator tile layer may unlock many countries quickly.**

5. **WMS remains important for exact resolution/bounds and services without useful WMTS.**

6. **COG/STAC work from elevation remains valuable.**  
   It can be reused for WorldCover, SWISSIMAGE, Sweden and OpenAerialMap, but image COGs require RGB/JPEG/multiband extensions.

7. **Detailed and distant imagery must have separate acquisition policy.**  
   Never download 10–20 cm national orthophoto over tens/hundreds of square kilometres by default merely because it is available.

8. **Cloud handling should initially be delegated to prepared mosaics.**  
   Raw Sentinel scene compositing can remain an advanced/future feature.

9. **Season/date are first-class imagery metadata.**  
   Texture generation should prefer one coherent source period rather than patching arbitrary dates together.

10. **Germany and several other countries need regional follow-up.**  
    The obvious national service is not always the best open source.

---

# 18. Sources retained for follow-up

Global:

- https://esa-worldcover.org/en/data-access
- https://registry.opendata.aws/esa-worldcover/
- https://documentation.dataspace.copernicus.eu/Data/Others/Sentinel2_Mosaic_access.html
- https://registry.opendata.aws/sentinel-2-l2a-cogs/
- https://openaerialmap.org/
- https://www.usgs.gov/landsat-missions/landsat-collection-2

Europe:

- Poland: https://www.geoportal.gov.pl/en/data/orthophotomap-orto/
- Czechia: https://ags.cuzk.gov.cz/arcgis1/rest/services/ORTOFOTO_WM/MapServer
- Spain: https://pnoa.ign.es/pnoa-imagen/estado-del-proyecto
- Netherlands: https://www.pdok.nl/ogc-webservices/-/article/pdok-luchtfoto-rgb-open-
- Flanders: https://www.vlaanderen.be/datavindplaats/catalogus/orthofotowerkbestand-middenschalig-winteropnamen-kleur-202601-vlaanderen
- Wallonia: https://geoportail.wallonie.be/catalogue/c28d5647-4b33-4cec-baa5-dda1f8443678.html
- France: https://cartes.gouv.fr/
- Switzerland: https://www.swisstopo.admin.ch/en/orthoimage-swissimage-10
- Austria: https://www.bev.gv.at/en/Services/Products/Remote-Sensing.html
- Finland: https://www.maanmittauslaitos.fi/en/maps-and-spatial-data/datasets-and-interfaces/product-descriptions/orthophotos
- Denmark: https://datafordeler.dk/dataoversigt/geodanmark-ortofoto/ortofoto-foraar-web-mercator-wmts/
- Sweden: https://geotorget.lantmateriet.se/geodataprodukter/ortofoto-nedladdning-api
- Norway: https://www.geonorge.no/nib
- Slovakia: https://www.gku.sk/gku/produkty-sluzby/zbgis/wms.html
- Portugal: https://www.dgterritorio.gov.pt/atividades/cartografia/cartografia-topografica/ortofotos/ortofotos-digitais
- Croatia: https://geoportal.dgu.hr/cms/en/data-and-services/
- Slovenia: https://podatki.gov.si/dataset/ortofoto
- Estonia: https://geoportaal.maaruum.ee/eng/services/public-wms-wfs-p346.html
- Lithuania: https://www.geoportal.lt/arcgis/rest/services/NZT/ORT10LT_Web_Mercator_102100/MapServer
- Latvia: https://www.lgia.gov.lv/en/wms-servisi
- Germany: https://gdz.bkg.bund.de/index.php/default/webdienste/digitale-orthophotos/wmts-digitale-orthophotos-bodenauflosung-20cm-wmts-dop.html
- Ireland: https://tailte.ie/services/mapgenie/
- England: https://www.data.gov.uk/dataset/4921f8a1-d47e-458b-873b-2a489b1c8165/vertical-aerial-photography
- USA: https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer
- India: https://bhuvan.nrsc.gov.in/updates/bhuvan_jan2025.html
- Japan: https://maps.gsi.go.jp/development/ichiran.html
