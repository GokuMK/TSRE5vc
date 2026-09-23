# Slovakia and Wallonia extracted BigTIFF review

Bounded remote inspection performed 2026-09-23 against the extracted files at
<https://koniec.org/geo/>. The orthophoto archive was excluded. The inspection
used TIFF header, directory, block-table and selected data-block byte ranges;
it did not download either national raster. Total probe traffic stayed below
10 MiB.

## Server range-read result

The server is suitable for raster block reads.

- Both national TIFFs and Slovakia's external `.tif.ovr` return HTTP `206
  Partial Content`, an exact `Content-Range`, and only the requested bytes.
- Ranges above 4 GiB work. Successful probes included the first and last data
  blocks of each base raster, including offsets above 155 GB in Slovakia.
- Four simultaneous real block reads all returned exact `206` responses.
- A second eight-request parallel probe read 64 KiB at positions spread from
  10 GB to 140 GB. Every request returned `206`; from the test workstation,
  time to first byte was 0.33-0.44 s and total time was 0.37-0.48 s.
- The responses include stable `ETag` and `Last-Modified` values suitable for
  cache identity/revalidation.

The former Wallonia finding that the official ZIP host ignored byte ranges still
applies to that ZIP host. The extracted direct TIFF on `koniec.org` does support
ranges.

## Slovakia DTM 5.0

Files inspected:

- `dmr5_etrs89.tif`: 156,108,150,990 bytes;
- `dmr5_etrs89.tif.ovr`: 46,550,149,948 bytes;
- adjacent TFW, auxiliary XML, metadata XML and product information.

Base raster profile:

- little-endian BigTIFF, `423518 x 207589`, 1 m;
- one Float32 band, LZW, predictor 1;
- EPSG:3046, using ordinary raster X/Y as easting/northing;
- extent `191148,5289631` to `614666,5497220`;
- `128 x 128` tiles, 5,367,198 blocks;
- NoData `3.39999995214436425e+38`;
- first image directory at byte `156043742402`, near the end of the file;
- offset and byte-count tables are about 42.9 MB and 21.5 MB respectively.

The base image is technically block-readable. Its tiles are packed in row-major
order and average about 29 KiB compressed. A 2 km terrain area intersects about
`17 x 17` source blocks before alignment effects: roughly 8-9 MB of compressed
terrain, but 289 HTTP requests if each block is fetched separately. A reader
could reduce this to about 17 ranges by merging adjacent blocks in each row.

The external overview file has eight tiled Deflate levels at factors 2 through
256. It places its directories and tables before image data, but inherits the
base file's georeferencing rather than carrying a self-contained GeoTIFF CRS and
transform. The factor-32 level is suitable for distant terrain.

### Slovakia pre-conversion conclusion

Reconversion is not strictly required. A new generic remote-BigTIFF path could
range-read the directory at the end of the base file, page only the required
entries from its large block tables, merge adjacent tile ranges, and understand
the external overview file.

Reconversion to one self-contained COG is nevertheless the preferred first
integration. It avoids several new index/overview cases, reduces a detailed
terrain request from hundreds of 128-pixel blocks to a handful of 1024-pixel
blocks, and places the factor-32 distant-terrain overview in the same asset.
The present TSRE COG reader cannot use the original files directly: it expects
the directories in its bounded leading index, limits a complete block table to
16 MiB, and expects overviews and georeferencing in one TIFF.

The `.ovr` file is an image pyramid, not a vertical-datum correction. Product
information declares ETRS89 ellipsoidal height (EPSG:4937). Combining this source
with orthometric HGT/GEDTM30 therefore still needs an explicit vertical transform;
a constant Height-window offset cannot model a spatially varying geoid separation.
Horizontal EPSG:3046 support itself is small: it uses the existing GRS80 UTM-zone
34 Transverse Mercator parameters.

## Wallonia MNT 1 m 2021-2022

File inspected:

- `RELIEF_WALLONIE_MNT_1M_2021_2022.tif`: 44,014,505,895 bytes;
- adjacent TFW, PAM auxiliary XML and delivery notes.

Raster profile:

- little-endian BigTIFF, `253085 x 146727`, 1 m;
- one Float32 band, LZW, predictor 1;
- EPSG:3812 / Belgian Lambert 2008 coordinates; DNG/EPSG:5710 heights;
- PixelIsPoint registration, with first centre at `542188.5,667800.5`;
- NoData `-9999`;
- no overviews;
- stripped rather than tiled: 29,346 strips, each five rows high and the full
  253,085-pixel country width.

The strip byte-count table is only 117 KiB, but the data layout is unsuitable for
spatial range reads. Compressed strips have a 1,499,563-byte median, 2,537,767-byte
90th percentile and 2,646,674-byte maximum. A 2 km north-south terrain window
requires about 400 full-country-width strips, typically around 600 MB even when
only a 2 km-wide portion is wanted. There is no coarser level for distant terrain.

### Wallonia pre-conversion conclusion

Wallonia must be retiled before TSRE integration. Server range support cannot
fix the full-width strip organization. The converted file should also carry a
direct EPSG:3812 projected key so TSRE need not interpret the source's user-defined
GeoTIFF parameter records. The internal CRS class then needs the EPSG:3812
Lambert Conformal Conic 2SP definition; no horizontal datum grid is required.

## Conversion profile used for server-side preparation

Use a self-contained BigTIFF COG with 1024-pixel blocks, Float32 predictor,
Deflate compression and internal average-resampled overviews. A 1024 block is
4 MiB before compression, below TSRE's current 8 MiB compressed-range guard,
and matches the acquisition scale already used by its detailed terrain workflow.
GDAL's COG driver documents `BLOCKSIZE`, `COMPRESS`, `PREDICTOR`, `BIGTIFF`,
`NUM_THREADS`, `RESAMPLING`, and `OVERVIEWS` creation options.

Representative commands, to run locally on the server:

```bash
gdal_translate -of COG \
  -co BIGTIFF=YES -co BLOCKSIZE=1024 \
  -co COMPRESS=DEFLATE -co PREDICTOR=FLOATING_POINT \
  -co NUM_THREADS=ALL_CPUS -co RESAMPLING=AVERAGE \
  -co OVERVIEWS=FORCE_USE_EXISTING \
  dmr5_etrs89.tif slovakia_dmr5_1m_cog.tif

gdal_translate -of COG -a_srs EPSG:3812 \
  -co BIGTIFF=YES -co BLOCKSIZE=1024 \
  -co COMPRESS=DEFLATE -co PREDICTOR=FLOATING_POINT \
  -co NUM_THREADS=ALL_CPUS -co RESAMPLING=AVERAGE \
  -co OVERVIEWS=AUTO \
  RELIEF_WALLONIE_MNT_1M_2021_2022.tif wallonia_mnt_1m_cog.tif
```

Keep Slovakia's `.tif.ovr` beside the input while converting so GDAL can use its
existing levels. Before publishing either output, verify with `gdalinfo` and a
COG validator that:

- dimensions, geotransform, CRS, Float32 type and NoData are unchanged;
- directories and block tables are in the leading part of the file;
- 1024-pixel tiles and internal factors including 32 are present;
- a bounded `/vsicurl/` sample at the west, centre and east of valid coverage
  returns plausible heights;
- a four-range probe against the published output still returns exact `206`
  responses.

GDAL COG driver reference: <https://gdal.org/en/stable/drivers/raster/cog.html>.

## Conversion and TSRE integration, 2026-09-23

The server-side conversion was completed and independently checked with bounded
HTTP range requests:

- `slovakia_dmr5_1m_cog.tif`: 128,258,735,982 bytes;
- `wallonia_mnt_1m_cog.tif`: 39,697,878,039 bytes.

Both outputs are Deflate-compressed Float32 BigTIFF COGs with 1024 x 1024 tiles,
floating-point predictor 3, eight internal overviews, correct NoData metadata,
and their TIFF directories at the start. Slovakia declares EPSG:3046 and
Wallonia declares EPSG:3812. Four range probes per file, including offsets above
4 GiB and the final 64 bytes, returned exact `206`, `Content-Range`, and 64-byte
responses. West, centre and east samples also matched plausible local reads.

TSRE catalogue entries use the existing generic `file` / `geotiff` / `cog`
provider with the published national COG URLs. The shared CRS converter adds
EPSG:3046 through its existing GRS80 UTM-zone-34 path and EPSG:3812 through its
existing Lambert Conformal Conic 2SP path. Neither source has provider-specific
terrain-generation code.

Bounded TSRE live probes then confirmed the complete acquisition and decode path:

- Bratislava returned 199.628 m and 199.639 m from Slovakia, with one downloaded
  block; the repeat used one cache block and no downloads;
- Namur returned 83.5877 m and 83.5103 m from Wallonia, with one downloaded
  block; the repeat used one cache block and no downloads.
