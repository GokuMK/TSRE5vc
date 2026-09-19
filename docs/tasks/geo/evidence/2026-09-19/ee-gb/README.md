# England / Estonia numeric TIFF fixtures

Retrieved 2026-09-19 from the official services. Each `.json` records the exact
public request URL, TIFF tags and independently unpacked maximum sample/index.
Raw TIFFs are unchanged; no authentication or secrets are involved.

- `gb-catalog-32.tif`: 32 x 32 big-endian, tiled Float32, EPSG:3857. Expanded
  output grid; 960 valid cells and 64 explicit NoData cells. Maximum at linear
  index 59 is 33.3849983215332 m. Environment Agency LIDAR Composite DTM;
  Open Government Licence v3.0, attribution: Environment Agency.
- `ee-land-32.tif`: 32 x 32 little-endian, stripped Float32, EPSG:3857 at exactly
  2 map m. Maximum at index 528 is 2.690624952316284 m. Zero is valid.
  Attribution: Republic of Estonia Land and Spatial Development Board,
  elevation data. Tests also mutate this fixture's image height to exercise
  padded-final-strip handling and reject truncated/arbitrary-length strips.

See [integration report](../../../england-estonia-validation.md) for full-block
requests and production generation/cache checks.
