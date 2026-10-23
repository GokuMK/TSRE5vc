# Task 11 - MSTS distance levels

Status: done 2026-10-19 (Open Rails' selection, the default).

## Before

Both MSTS backends read the distance levels: `SFileLegacy` those of the
first `lod_control`, `SFileComplex` all of them (only the first level with
`TSRE_MSTS_FIRST_LOD_ONLY=1`), and both upload every level. Only the Shape
Viewer chose one (`setCurrentDistanceLevel`); the route editor always drew
level 0, culling objects only past `objectLod` (3000 m) and behind the
camera.

## Selection

`ShapeLod` (`src/tsre/shape/ShapeLod.h`) picks the level as Open Rails does
(`SharedShape.PrepareFrame`, `Camera.InRange`): the most detailed level whose
`dlevel_selection` plus the shape's radius (half its bounding box diagonal;
Open Rails uses its first volume sphere) reaches the camera's horizontal
distance to the object; past them all the last one, drawn as far as objects
are (Open Rails' default `LODViewingExtension`). Shapes report their levels
through `ComplexShape::distanceLevelCount()` / `distanceLevelDistance()`;
static, track and generic world objects set the level on their shape state
before they push it.

`TSRE_SHAPE_LOD` (read once): unset or `1` as above; `0` always level 0;
`2` also hides an object past its last level's distance (MSTS, Open Rails
without the extension).

## Measurements

Prudnik, `core.startup.camera=-5373,14874,787.53,350.66,-1009.69,358.55,-9.34`,
1280x800, QRhi, 12:00 clear, editor helpers hidden; median of 60 frames
(each with an image readback), Steam Deck.

| Mode | Legacy frame | Compact frame | CPU | Draw calls | Instances |
|---|---|---|---|---|---|
| 0 (level 0) | 128-133 ms | 138 ms | 58 ms | 13,650 | 34.7k |
| 1 (default) | 97-99 ms | 105 ms | 36-41 ms | 10,094 | 20.6k |
| 2 (MSTS cull) | 89-90 ms | 94 ms | 32-35 ms | 9,596 | 15.7k |

Legacy and Compact render identical images in each mode. Mode 1 changes only
the grass shapes' far level (a field thins in the middle distance, as in Open
Rails); mode 2 also drops a near catenary mast and leaves bare patches in the
grass from a high editor camera, so it is not the default.

Rolling stock (2026-10-23): each car on the track chooses its level the
same way (`Eng::pushRenderItemBetween`, its freight loads too), from its
origin's distance to the camera; the shadow maps take the same level, so a
near car shadows itself in detail. In the TRAINS scene (renderer task 37)
few cars are far enough to change: 0.23 % of pixels, 2793 draws against
2775 at level 0.

## Not covered

- Signals, dynamic track and forests (their own paths; forests have no
  levels).
- `SFileComplex` draws one level of one `lod_control`; shapes with several
  controls select from the first.
- No scaling by zoom (field of view) and no hysteresis; Open Rails has
  neither.
- glTF shapes report no levels; [task 10](10-gltf-native-lod.md) can implement
  the same two accessors.
