# TSRE user guides

Practical guides to using TSRE, with background explanations, step-by-step
workflows and worked examples. Choose a topic below.

Browser editions: [Guide index](index.html) ·
[Elevation](elevation-data.html) · [Imagery](imagery-data.html) ·
[Displays](displays.html) ([po polsku](displays.pl.html)).

## Elevation and imagery

| Guide | What you will learn |
| --- | --- |
| [Elevation data](elevation-data.md) | Load terrain heights, choose a fallback, and add WCS, COG, STAC or local GeoTIFF sources. |
| [Imagery data](imagery-data.md) | Load aerial photographs and satellite imagery, and add WMS, WMTS, STAC/COG or static-map URL sources, including Google Static Maps. |

Custom source definitions belong in `assets/geo/` in your TSRE working
directory, normally beside `TSRE5vc.exe`. The guides explain the exact filenames;
TSRE does not scan that folder for arbitrary JSON files. Downloaded raster data
belongs in the separately configured geodata directory.

## Shapes and content

| Guide | What you will learn |
| --- | --- |
| [Displays](displays.md) ([po polsku](displays.pl.md)) | Put live documents on shapes: departure boards, platform screens, train destination displays and coach panels; the `.sd` block, what documents can read, train data in timetables. |

For implementation history and source surveys, see the
[geodata development documentation](../tasks/geo/README.md).
