# Sweden, Wallonia and France elevation-source research

Research date: 2026-09-22

This is source research, not an implementation specification. The probes were
kept to metadata and small HTTP responses; no large raster/archive was
downloaded.

## Decision

| Priority | Source | Result |
|---|---|---|
| Implemented | France, IGN MNT LiDAR HD | Generic numeric WMS and EPSG:2154 support are implemented and live-tested at a 1 m request grid. |
| Implemented | Sweden, Lantmäteriet Markhöjdmodell | Generic authenticated STAC/range-COG support and EPSG:3006 are implemented and live-tested. |
| Deferred | Wallonia, SPW MNT 2021–2022 | Keep as a future user-managed/bulk-download source. The current public delivery is not suitable for selective automatic acquisition. |

Sweden and France both add reusable provider capabilities and are implemented.
Wallonia's
projection is straightforward once Lambert Conformal Conic exists, but its
current file delivery is the blocker.

## Sweden: Lantmäteriet Markhöjdmodell

### Product and live endpoint findings

Official documentation describes a national 1 m regular terrain grid in
SWEREF 99 TM with RH 2000 heights. STAC records use compound CRS EPSG:5845
(SWEREF 99 TM + RH 2000). The horizontal part is EPSG:3006.

- product documentation:
  <https://geotorget.lantmateriet.se/dokument/projects/markhoejdmodell-nedladdning/released/1/>
- product/order page:
  <https://geotorget.lantmateriet.se/geodataprodukter/markhojdmodell-nedladdning-api>
- STAC endpoint: <https://api.lantmateriet.se/stac-hojd/v1>

The STAC root and search endpoint are anonymously readable. A bounded search
near Stockholm returned item `657_67_7525`, collection `mhm-65_6`, and a direct
COG asset:

```text
https://dl1.lantmateriet.se/hojd/data/grid1m/65_6/55/65775_6725_25.tif
```

The sampled item reports:

- `proj:epsg: 5845`;
- `proj:bbox: [672500,6577500,675000,6580000]`;
- `proj:shape: [2500,2500]`;
- `geometriskupplosning: 1.0`;
- COG media type and an 8,309,294-byte file.

The asset returns HTTP 401 without credentials. Lantmäteriet documents both
Basic authentication and OAuth2, after the free product permission is ordered
through Geotorget. Basic authentication fits TSRE's current local-secret model.
Catalogue metadata can remain anonymous; credentials are needed for asset
requests, including byte-range requests.

Official documentation says the current file coverage is 10 x 10 km, while the
live item above is 2.5 x 2.5 km. The catalogue appears to contain a transitioning
or mixed layout. TSRE should not assume a fixed asset size and should use the
range-COG reader so larger current files do not become whole-file downloads.

### Implemented generic changes

1. Allow STAC configuration to search the API root instead of requiring one
   fixed collection. Swedish items are split across regional `mhm-*`
   collections.
2. Pass configured authentication through STAC asset and COG range requests.
   The current range transport has no authentication parameter.
3. Add Basic username/password secret references. Proposed keys:
   - `geo.elevation.se.lantmateriet.username`
   - `geo.elevation.se.lantmateriet.password`
4. Let the STAC profile identify resolution from a configured property such as
   `geometriskupplosning`, as Sweden does not expose it as `eo:gsd` on the
   sampled item.
5. Accept compound EPSG:5845 as the horizontal EPSG:3006 transform while
   retaining RH 2000 in the source description and report.
6. Use the existing generic Transverse Mercator path for EPSG:3006. Its GRS80
   parameters are central meridian 15 degrees, scale 0.9996, false easting
   500000 m and false northing 0 m.

No Sweden-specific terrain sampling or cache logic is used.

### Authorized implementation probe, 2026-09-23

The configured Basic username/password references were resolved from the local
secret profile and used only in authenticated asset range requests. A bounded
Stockholm probe at `(59.31, 18.06)` established the remaining TIFF profile:

- 2500 x 2500 Float32 image;
- Deflate compression;
- TIFF horizontal predictor 2 applied to 32-bit sample words;
- 512 x 512 internal blocks;
- COG index and block tables compatible with the bounded range reader.

The first two-point run fetched one compressed block and returned primary
heights of 31.361 m and 31.2604 m with no fallback. The repeat made zero
downloads, used one cached block and returned identical values. The cache keeps
only revision-specific COG index/tables/blocks; it contains no credentials.
A wider 16 x 16 Stockholm-area probe crossed the internal block layout: it used
188 primary samples and routed 68 source-NoData samples through the configured
fallback while downloading 36 compressed blocks in four-request waves. Its
repeat made zero downloads and found all 37 required blocks in cache.
The resulting source was subsequently confirmed working in the main application.

## Wallonia: SPW MNT 2021–2022

### Product and delivery findings

The current bare-earth terrain model is available as dedicated 1 m GeoTIFF
downloads in EPSG:3812 (Belgian Lambert 2008), with DNG/EPSG:5710 heights.

- 1 m product:
  <https://geoportail.wallonie.be/catalogue/fe13bc84-e371-46ca-9632-8ad4139f1ee5.html>
- INSPIRE entry:
  <https://geoportail.wallonie.be/catalogue/1eafd629-0b5e-4816-a496-e69676eae65b.html>
- INSPIRE ATOM feed:
  <https://geoservices.wallonie.be/inspire/atom/EL_Service.xml>

Anonymous stable ZIP URLs exist, but the predefined packages are large:

- Brabant wallon: about 2.6 GB;
- Hainaut: about 8.8 GB;
- Liège: about 9.5 GB;
- Luxembourg: about 11.2 GB;
- Namur: about 8.9 GB;
- all Wallonia: about 40.9 GB.

The download host did not advertise range support and returned HTTP 200 rather
than 206 to a small Range request. TSRE therefore cannot inspect or extract only
the needed members of these remote archives. The custom-download workflow asks
for an email address and documents processing that can take up to 48 hours; it
is not a stable synchronous API.

The public GeoServer accepts WCS 2.0.1 capabilities requests, but its contents
list is empty. No numeric WCS 1 coverage was found. The public ArcGIS endpoint
is a rendered MapServer rather than a raw numeric ImageServer.

### Consequence for TSRE

Wallonia is currently less suitable for automatic acquisition than Portugal.
Portugal's user-selected delivery produces small tiles that TSRE can index;
Wallonia's predefined route starts with province-scale archives.

EPSG:3812 is not the difficult part. It is an ellipsoidal Lambert Conformal
Conic 2SP CRS on ETRS89/GRS80 and does not require a horizontal datum grid for
TSRE's terrain use. The same generic projection family needed for France can
cover it.

Keep Wallonia deferred until one of these becomes available:

- stable direct URLs for modest individual raster tiles;
- numeric WCS/ImageServer access;
- COGs or another server that honors byte ranges.

A future user-managed source is also possible after a user downloads and
extracts a package. Its actual file layout and TIFF profile still need a bounded
inspection before designing that entry.

## France: IGN MNT LiDAR HD

### Prefer the current LiDAR HD product

The older RGE ALTI route remains available through the Géoplateforme download
API, but its 1 m product is commonly delivered as department-scale archives of
ArcInfo ASCII grids. It is a poor first automatic TSRE path.

The stronger candidate is the current **MNT LiDAR HD**:

- bare-earth DTM derived from classified ground, water and virtual points;
- 0.5 m GeoTIFF;
- 1 x 1 km files;
- Lambert-93/EPSG:2154 with IGN69 heights for metropolitan tiles;
- intended coverage of metropolitan France and the overseas departments except
  French Guiana by 2026, with availability still tracked as a rollout;
- open direct downloads.

Official sources:

- product page:
  <https://cartes.gouv.fr/aide/fr/partenaires/ign/observations-regulieres-territoire/relief/mnt-lidar-hd/>
- download UI:
  <https://cartes.gouv.fr/telechargement/IGNF_MNT-LIDAR-HD>
- download API:
  <https://cartes.gouv.fr/aide/fr/guides-utilisateur/utiliser-les-services-de-la-geoplateforme/telechargement/>
- official 1 km sample and raster tutorial:
  <https://cartes.gouv.fr/aide/fr/guides-developpeur/tutoriels/gestion-des-donnees-raster/mnt/>

The official sample is a 15.3 MB, 0.5 m GeoTIFF. TSRE only needs 1 m output, but
there is no identified server-side resampling endpoint. It should cache the
original tile and sample it at the requested terrain spacing.

### Practical automatic service

The public download interface uses an official vector TMS/MVT tile catalogue:

```text
https://data.geopf.fr/tms/1.0.0/IGNF_MNT-LIDAR-HD-produit
```

The official open-source download client documents the generic `-produit`
schema:

<https://github.com/IGNF/cartes.gouv.fr-telechargement/blob/main/docs/TMS_TA_SPEC.md>

At suitable zooms, each vector feature contains the raster footprint plus its
name, download `url`, format and projection. A live zoom-14 tile near Paris was
only 5,885 bytes and exposed several 1 km MNT records. Crucially, those URLs are
not static whole-file downloads. They are requests to IGN's public numeric
`wms-r` endpoint, for example:

```text
https://data.geopf.fr/wms-r?
  SERVICE=WMS&VERSION=1.3.0&REQUEST=GetMap&
  LAYERS=IGNF_LIDAR-HD_MNT_ELEVATION.ELEVATIONGRIDCOVERAGE.LAMB93&
  FORMAT=image/geotiff&CRS=EPSG:2154&
  BBOX=651999.75,6862000.25,652999.75,6863000.25&
  WIDTH=2000&HEIGHT=2000
```

The WMS capabilities advertise that raw MNT layer in EPSG:2154. A separate
bounded probe changed the request to a 10 m square and `WIDTH=10&HEIGHT=10`.
It returned an anonymous 913-byte little-endian TIFF with:

- one band;
- 32-bit floating-point samples;
- no compression;
- a 10 x 10 grid and GeoTIFF georeferencing;
- an explicit NoData tag;
- plausible terrain samples around 35 m.

This establishes arbitrary bounding-box acquisition and server-side resampling.
TSRE can request a 1 m grid directly from the native 0.5 m product, halving each
dimension and reducing data to one quarter of the native request. The MVT index
is useful for the official download UI and exact publication footprints, but it
is not required for TSRE's first terrain provider: WMS itself can return the
needed bounded raster and NoData reports unpublished areas.

A second probe used the normal TSRE service-block shape: a 1026 m square,
including halo, requested as 1026 x 1026 pixels. It succeeded in about 11.1 s
and returned a 4,211,217-byte GeoTIFF. Four-way concurrency was not tested, but
the service accepts the intended per-block dimensions and the response remains
well below TSRE's 32 MiB HTTP guard.

An EPSG:3857 request also returned a numeric TIFF, but that CRS is not advertised
for the raw layer and its GeoTIFF describes the projection as user-defined.
TSRE should use the advertised native EPSG:2154 route rather than depend on this
undocumented reprojection behavior.

The ordinary Géoplateforme download API is still useful for discovery and bulk
products, but the RGE ALTI responses observed were department-oriented.

### Required generic changes

1. A generic numeric-WMS GeoTIFF provider uses configured endpoint, version,
   layer, format, CRS and optional style. It can reuse the WCS provider's block
   grid, halo, concurrency, cache, TIFF validation and reporting behavior.
2. Generic ellipsoidal Lambert Conformal Conic 2SP conversion configures
   EPSG:2154. Lambert-93 uses GRS80, latitude of origin 46.5 degrees, central
   meridian 3 degrees, standard parallels 49 and 44 degrees, false easting
   700000 m and false northing 6600000 m.
3. The catalogue configures 1 m output blocks even though the native product is
   0.5 m. Four concurrent 1024-pixel core blocks are accepted.
4. Conservative mainland bounds preserve WMS NoData as coverage gaps.
   Overseas products use separate native CRSs/layers and should be separate
   later entries rather than being hidden behind the metropolitan profile.

The provider and projection work are reusable. Numeric WMS services occur
elsewhere, and Lambert Conformal Conic also unlocks Wallonia's horizontal CRS.

## Recommended implementation sequence

1. France numeric WMS and Lambert Conformal Conic 2SP are complete. Retain a
   later application/full-terrain coverage-edge test when convenient.
2. Sweden's authenticated STAC/range-COG extension, asset-profile probe and
   application check are complete.
3. Recheck Wallonia only if its delivery changes or after a small extracted
   sample becomes available. Do not download a province archive merely to test
   the provider.
4. Treat Slovakia's remote BigTIFF/overlay investigation as a separate track;
   none of these findings requires local copies of that 180 GB package.
