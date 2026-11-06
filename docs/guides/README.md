# TSRE user guides

Practical guides to using TSRE, with background explanations, step-by-step
workflows and worked examples. Choose a topic below.

Browser editions: [Guide index](index.html) ·
[Elevation](elevation-data.html) · [Imagery](imagery-data.html) ·
[Displays](displays.html) ([po polsku](displays.pl.html)) ·
[Shape lights](shape-lights.html) ·
[Shape emitters](shape-emitters.html).

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
| [Shape lights](shape-lights.md) | Give an S shape lamps that light the scene, as glTF lights do: the `.sd` block, placing and aiming, strength and reach. |
| [Shape emitters](shape-emitters.md) | Give an S shape smoke and steam: chimneys, stacks and vents in the `.sd`, drawn as the trains' exhausts. |

For implementation history and source surveys, see the
[geodata development documentation](../tasks/geo/README.md).
