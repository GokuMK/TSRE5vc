Yes. For a **volumetric-cloud world generator**, I would simplify the problem and use meteorological data only to define the **large-scale cloud structure**, then let procedural noise generate the actual cloud detail.

The two sources that stand out are:

| Source | Resolution | Time | What we'd use |
|---|---:|---|---|
| **DWD ICON-EU** | ~6.5 km | forecast to 120 h | **true 3D cloud fields** |
| **EUMETSAT MTG / MSG** | ~2 km / few km | observations + history | beautiful real-world cloud shapes |
| **CERRA** | 5.5 km | 1984–present | historical cloud coverage |

### 1. DWD ICON-EU — probably the best source for our generator

This is actually much better than simply downloading a cloud-cover texture.

ICON-EU publishes the atmospheric model on vertical model levels, including:

- `CLC` — cloud cover per model level
- `QC` — **specific cloud liquid-water content**
- `QI` — **specific cloud ice content**
- `CLCL` — low cloud cover
- `CLCM` — middle cloud cover
- `CLCH` — high cloud cover
- `CLCT` — total cloud cover
- cloud-top height is also an ICON model variable. [dwd.de](https://dwd.de/EN/ourservices/nwp_icon_tutorial/pdf_volume/icon_tutorial2025_en.pdf?__blob=publicationFile\&v=3\&utm_source=chatgpt.com)

And DWD actually exposes the model-level `qc`, `qi` and `clc` files directly on its open-data server. [opendata.dwd.de](https://opendata.dwd.de/weather/nwp/icon-eu/grib/00/qc/?utm_source=chatgpt.com)

The European grid is about **6.5 km**, covering roughly 23.5°W–62.5°E and 29.5°N–70.5°N. Long forecasts reach **120 hours**, with regular-grid output hourly for much of the useful forecast period. [dwd.de](https://dwd.de/DWD/forschung/nwv/fepub/icon_database_main.pdf?utm_source=chatgpt.com)

That means we could construct something like:

```text
ICON QC + QI + CLC
        ↓
coarse 3D cloud-density volume
        ↓
interpolate horizontally/vertically
        ↓
domain-warped Worley/Perlin noise
        ↓
fine volumetric cloud density
```

A 6.5 km weather grid sounds coarse, but for this use it really isn't. A cumulonimbus system might be 20–100 km across. The model determines **where the cloud masses exist, how high they are and how thick they are**; our shader/generator provides all the 50–500 m detail.

For TSRE I'd probably make this the primary **forecast/weather preset**.

### 2. EUMETSAT — much prettier macro shapes

For current or historical real weather, **Meteosat is excellent**.

The new MTG FCI **Optimal Cloud Analysis (OCA)** product is:

- **2 km** at nadir
- every **10 minutes**
- NetCDF
- cloud phase
- cloud height
- microphysical properties
- available since 2025
- listed with `NoConditions` data policy. [api.eumetsat.int](https://api.eumetsat.int/data/browse/collections/EO%3AEUM%3ADAT%3A0684?format=html\&utm_source=chatgpt.com)

That's almost tailor-made for generating visually plausible clouds.

Even more interesting for random historical weather: the older MSG/SEVIRI archive is enormous. EUMETSAT has a homogeneous OCA climate record from **2004–2019 at 15-minute intervals**, containing:

- cloud-top pressure
- **cloud optical thickness**
- **effective radius**
- associated uncertainty. [api.eumetsat.int](https://api.eumetsat.int/data/browse/collections/EO%3AEUM%3ADAT%3A0617?format=html\&utm_source=chatgpt.com)

And the simpler MSG cloud-mask archive continues from **2004 to the present**. [api.eumetsat.int](https://api.eumetsat.int/data/browse/1.0.0/collections/EO%3AEUM%3ADAT%3AMSG%3ACLM?format=html\&utm_source=chatgpt.com)

So TSRE could literally pick:

> Belgium, 17 July 2011, 14:30 UTC

and obtain the real macro cloud pattern that existed then.

For graphics this may actually produce prettier worlds than model output because satellite observations preserve real fronts, holes, cloud streets and convective clusters rather than smoothed 6.5-km cells.

### 3. CERRA — great source of arbitrary historical weather

For a simpler historical mode, Copernicus **CERRA** covers essentially all Europe at **5.5 km**, from **September 1984 onward**. It includes:

- low cloud cover
- medium cloud cover
- high cloud cover
- total cloud cover. [Climate Data Store](https://cds.climate.copernicus.eu/datasets/reanalysis-cerra-single-levels?tab=download\&utm_source=chatgpt.com)

The analysis is normally every three hours. [Climate Data Store](https://cds.climate.copernicus.eu/datasets/reanalysis-cerra-single-levels?tab=overview\&utm_source=chatgpt.com)

So we could ship a mode such as:

```text
Weather:
  Current
  Forecast
  Historical random
  Historical date/time
  Procedural
```

with CERRA supplying the historical macro state.

## What I would implement

I think **ICON-EU + procedural detail** is the sweet spot.

Instead of treating weather data as a flat cloud texture, generate perhaps a coarse local volume around the route:

```text
              ICON model atmosphere

12 km       ░░░░░████░░░       ice / cirrus
10 km       ░░████████░░░
 8 km       ░██████████░░

 4 km       ░░░░████░░░░░
 3 km       ░█████████░░░       liquid cloud
 2 km       ███████████░░
 1 km       ░██████░░░░░░
```

Then each coarse meteorological cell only supplies things like:

```text
density
coverage
cloud base/top
liquid/ice ratio
```

and procedural noise turns that into perhaps **64–250 m effective visual detail**.

The source resolution then stops being particularly important.

And for a later **"real sky" mode**, EUMETSAT's 2-km OCA data could constrain the horizontal shapes much more tightly.

So, of everything I've checked, I'd start with **DWD ICON-EU `QC + QI + CLC`**. Unlike almost every ordinary weather API, those fields give us something approaching an actual **3D cloud volume rather than a weather-map cloud percentage**. [dwd.de](https://www.dwd.de/DE/leistungen/nwv_icon_tutorial/pdf_einzelbaende/icon_tutorial2025.pdf?__blob=publicationFile\&v=3\&utm_source=chatgpt.com)
