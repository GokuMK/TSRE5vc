# Global elevation source review

**Implementation status, 2026-09-19:** the recommended first step is now
implemented as catalogue dataset `world-hgt`. It reads `.hgt` and `.hgt.gz` from
`geoPath/world_hgt`, downloads missing Mapzen Skadi tiles through an optional
file-source `download` definition, validates gzip/HGT data before storing it and
serves as the configured service fallback. The next staged item is a concrete
degree-grid GeoTIFF source; COG/range providers remain future work.

Yes. The global situation is actually better than I remembered—not in **resolution**, which is still basically ~30 m for freely available worldwide data, but in **accessibility**. There are now several genuinely no-login sources that are much easier than NASA Earthdata.

I also think I found the webpage you used last year: **Viewfinder Panoramas**. It is exactly the sort of old-school, simple page you described: world coverage maps/search plus downloadable HGT elevation files. As of March 2026 it still says the whole world is covered at 3″ and 15″, with 1″ data for selected areas. Its 3″ files are standard 1201×1201 HGT tiles with SRTM voids repaired from other sources. ([Viewfinder Panoramas][1])
[Viewfinder Panoramas DEM downloads](https://www.viewfinderpanoramas.org/dem3.html?utm_source=chatgpt.com)

For TSRE, I would currently rank the interesting global sources like this:

| Source                                 | Login/key                | Format                        | Resolution                        | Coverage           | TSRE interest                             |
| -------------------------------------- | ------------------------ | ----------------------------- | --------------------------------- | ------------------ | ----------------------------------------- |
| **Mapzen / Tilezen AWS Terrain Tiles** | None                     | `.hgt.gz`, GeoTIFF, Terrarium | usually ~30 m, variable by source | Global             | **Excellent immediate fallback**          |
| **OpenTopography public mirror**       | None                     | 1° GeoTIFF tiles              | 30 m / 90 m                       | depends on product | **Excellent generic tiled-raster source** |
| **Copernicus GLO-30 AWS**              | None                     | COG GeoTIFF                   | ~30 m                             | almost global      | Probably best consistent DSM              |
| **GEDTM30**                            | None for bulk/COG access | COG                           | ~30 m                             | near-global land   | Very interesting **bare-earth DTM**       |
| **Viewfinder Panoramas**               | None                     | HGT                           | global 3″ ≈90 m; selected 1″      | Global             | Very easy/manual fallback                 |
| **DLR elevation WCS**                  | None                     | WCS                           | ~25 m SRTM X-SAR                  | only ~43% of land  | Useful supplement, not fallback           |

### Mapzen / Tilezen is surprisingly attractive

AWS still officially hosts the old Mapzen Terrain Tiles as an Open Data dataset, anonymously, with **no AWS account required**. The dataset includes `skadi` tiles in SRTM-style HGT format, as well as GeoTIFF/Terrarium products. ([Rejestr Danych Otwartych na AWS][2])

The Skadi layout is particularly convenient:

```text
skadi/
    N50/
        N50E004.hgt.gz
        N50E005.hgt.gz
        ...
```

Each is a **1°×1° HGT**, EPSG:4326, signed 16-bit big endian, EGM96 height reference—the same basic format family TSRE already understands. Mapzen describes Skadi as essentially SRTMGL1-format tiles extended to global coverage. ([GitHub][3])

And current `geo-terrain` is already well positioned for this: your `readHgt()` determines the raster dimension from file size rather than hard-coding 1201 or 3601, so there isn't really a raster-format problem. The missing pieces would mostly be:

```text
lat/lon
  -> construct Skadi URL
  -> download .hgt.gz
  -> gunzip
  -> cache as N50E004.hgt
  -> existing HGT reader
```

That could arguably be simpler than another WCS provider.

The caveat is that Mapzen is **not one homogeneous global survey**. It composites SRTM with GMTED, ArcticDEM, EU-DEM, 3DEP, national datasets, etc. Mid-latitude land is largely SRTM-class ~30 m; some areas are better and some high-latitude/global fill is coarser. ([GitHub][4])

So I would label it something like **“Global Terrain Tiles”**, not “Global 30 m DEM”.

### OpenTopography now has anonymous direct GeoTIFF mirrors

This was the most interesting new discovery.

A new 2026 project called **Global DEM Portal**, now also published as a QGIS plugin, downloads several major global DEM products directly from OpenTopography's public object store **without an OpenTopography account or API key**. ([Global DEM Portal][5])

Its current source code gives deterministic 1° tile patterns for:

```text
SRTM GL1       ~30 m
NASADEM        ~30 m
ALOS AW3D30    ~30 m
Copernicus 30  ~30 m
SRTM GL3       ~90 m
Copernicus 90  ~90 m
```

The files are normal GeoTIFFs, one per degree. The source URLs are generated entirely from `N50E004`-style coordinates; there is no catalogue query needed.

This means a generic future TSRE provider such as:

```text
provider: "degree-geotiff"
urlTemplate: ...
```

could potentially expose **four different global ~30 m datasets** with practically the same code.

The main thing I would test before handing this to an agent is the exact TIFF encoding of each mirror. The files are fairly large, so some are almost certainly compressed, whereas the current TSRE TIFF decoder deliberately only accepts uncompressed TIFF. That is a raster-code issue rather than an acquisition problem.

[Global DEM Portal](https://globaldemportal.github.io/?utm_source=chatgpt.com)

### Copernicus GLO-30 has an even more official no-login route

There is an official AWS Open Data bucket for **Copernicus DEM GLO-30 Public**. No account or subscription is required. It consists of 1° Cloud Optimized GeoTIFFs and even contains a `tileList.txt`. ([Rejestr Danych Otwartych na AWS][6])

This is probably the cleanest **modern, homogeneous global DSM** candidate.

The drawback for current TSRE is explicitly documented by AWS:

* DEFLATE compression;
* floating-point predictor 3;
* tiled COG layout;
* 3600 rows rather than HGT's overlapping 3601;
* variable longitude pixel counts toward the poles.

([Copernicus DEM][7])

So acquisition is trivial, but it pushes us toward improving the TIFF/COG reader.

Also, it isn't absolutely complete at 30 m: AWS says a small set of tiles for some countries remains unreleased in GLO-30 Public. GLO-90 is fully worldwide. ([Copernicus DEM][7])

### GEDTM30 is potentially more interesting for trains than Copernicus

This one is new enough that it wasn't really part of our earlier discussion.

**GEDTM30** is a 2025/2026 global ~30 m **bare-earth DTM**, not a DSM. It combines Copernicus DEM, ALOS and object-height models, trained/refined against about **30 billion ICESat-2/GEDI ground observations**. OpenTopography added it in July 2026. ([opentopography.org][8])

That's conceptually attractive for TSRE because railway terrain generation wants ground rather than tree canopy/buildings.

It is CC BY 4.0, EPSG:4326, ~30 m and covers roughly 134 million km² of land. ([portal.opentopography.org][9])

But technically it isn't the easy first implementation. The distributed global product is a huge range-readable COG; the filtered version is stored as **Int32 decimetres with a 0.1 scale factor**. ([object.cloud.sdsc.edu][10])

So supporting it properly means something closer to:

```text
HTTP Range
+ COG TIFF directory access
+ DEFLATE
+ Int32
+ TIFF scale/offset
```

That's a substantial reusable provider, but probably very worthwhile later.

### Viewfinder is almost certainly the site you remember

The description fits extremely well. It looks like a webpage from twenty years ago, has geographic coverage maps/search, ZIP/HGT downloads and no account system.

Interestingly, it is also unusually relevant to TSRE: its terms explicitly say that using the DEM data to produce **flight-simulator meshes is welcome**, provided the source is properly credited or permission is obtained. ([Viewfinder Panoramas][11])

The only problem is resolution. The guaranteed global set is **3 arc-seconds ≈90 m**, not 30 m. Its 1″ catalogue has substantially better regional coverage now—including newer Europe/Japan material—but isn't one uniform worldwide 30 m dataset. ([Viewfinder Panoramas][1])

So I'd keep Viewfinder primarily as a **manual HGT source/user recommendation**, rather than TSRE's automatic global source.

### Public WCS: not much compelling at global scale

I did find a legitimate no-login one: **DLR EOC's Elevation WCS is online today**. ([geoservice.dlr.de][12])

It offers a global mosaic of the German SRTM X-SAR DEM at roughly 25–30 m through WCS. Unfortunately, X-SAR only imaged about **43% of Earth's land surface**, because its swath was much narrower than NASA's C-band SRTM instrument. ([geoservice.dlr.de][13])

So it's interesting for filling particular areas, but useless as the universal TSRE fallback.

DLR's newer **TanDEM-X 90 m** really is pole-to-pole, but direct data downloading requires registration/login, and their public visualization interfaces are WMS/WMTS rather than the simple anonymous numeric WCS we'd want. ([geoservice.dlr.de][14])

Old public SRTM WCS servers exist in search results, but I wouldn't build TSRE around a decade-old university CGI service when anonymous S3/object storage is now available.

### What I would do

For TSRE's **automatic world fallback**, I would investigate **Mapzen/Tilezen Skadi first**. It has almost the perfect shape for your existing implementation: predictable `N50E004` addressing, 1° HGT, no login, no key, and AWS is still officially publishing the bucket as Open Data.

Then I would treat **anonymous tiled GeoTIFF** as the next generic provider. That immediately opens SRTM GL1, NASADEM, ALOS and Copernicus through the current OpenTopography mirror.

And separately, I would keep **GEDTM30** on the roadmap as potentially the best world-scale *terrain* source once we have proper COG/range support. For railway terrain, its DTM nature is considerably more interesting than merely swapping SRTM DSM for another DSM.

So this research actually suggests a nice next split:

**easy provider:** remote `.hgt.gz` / Mapzen → very little code
**next generic provider:** 1° downloaded GeoTIFF → several global datasets
**later advanced provider:** COG/range → GEDTM30, Copernicus AWS, Switzerland, Sweden, etc.

[1]: https://www.viewfinderpanoramas.org/dem3.html?utm_source=chatgpt.com "Digital Elevation Data - with SRTM voids filled using accurate topographic mapping"
[2]: https://registry.opendata.aws/terrain-tiles/?utm_source=chatgpt.com "Terrain Tiles - Registry of Open Data on AWS"
[3]: https://github.com/tilezen/joerd/blob/master/docs/formats.md?utm_source=chatgpt.com "joerd/docs/formats.md at master · tilezen/joerd · GitHub"
[4]: https://github.com/tilezen/joerd/blob/master/docs/data-sources.md?utm_source=chatgpt.com "joerd/docs/data-sources.md at master · tilezen/joerd · GitHub"
[5]: https://globaldemportal.github.io/?utm_source=chatgpt.com "Global DEM Portal - SRTM · NASADEM · ALOS · Copernicus Downloader"
[6]: https://registry.opendata.aws/copernicus-dem/?utm_source=chatgpt.com "Copernicus Digital Elevation Model (DEM) - Registry of Open Data on AWS"
[7]: https://copernicus-dem-30m.s3.amazonaws.com/readme.html "Copernicus Digital Elevation Model datasets"
[8]: https://www.opentopography.org/news/new-machine-learning-derived-digital-terrain-models?utm_source=chatgpt.com "New Machine Learning Derived Digital Terrain Models | OpenTopography"
[9]: https://portal.opentopography.org/datasetMetadata?otCollectionID=OT.082025.4326.1&utm_source=chatgpt.com "OpenTopography - Global Ensemble Digital Terrain Model"
[10]: https://object.cloud.sdsc.edu/v1/AUTH_opentopography/www/metadata/GEDTM30_metadata.pdf?utm_source=chatgpt.com "August 4th 2025"
[11]: https://www.viewfinderpanoramas.org/dem3/?utm_source=chatgpt.com "Viewfinder Mountain Top Horizon Maps"
[12]: https://geoservice.dlr.de/web/services?utm_source=chatgpt.com "EOC Geoservice Services"
[13]: https://geoservice.dlr.de/web/dataguide/srtm/?utm_source=chatgpt.com "EOC Geoservice Data Guide"
[14]: https://geoservice.dlr.de/web/dataguide/tdm90/?utm_source=chatgpt.com "EOC Geoservice Data Guide"
