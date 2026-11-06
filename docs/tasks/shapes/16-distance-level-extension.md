# Task 16 - Distance Level Extension

## Objective

Let a shape's author choose what of an object is drawn on past its shape's
last level of detail (TSRE's and Open Rails' viewing extension), and give a
light its own view distance, so what makes no difference from afar is not
drawn there. Interior lamps (a cab's, a coach's, a station building's)
cannot be seen from a kilometre, yet each lights the scene and costs
drawing time wherever it is.

Status: done 2026-11-06, merged into main 5a8b8fba (2026-11-06).

## Why (user, 2026-11-06)

- TSRE draws a shape's last level on to the object view distance (3 km by
  default; Open Rails' viewing extension, task 11), and its lights with it:
  lights go as far as their object, not the last level's distance.
- The user's design: an `.sd` value for how the last level is drawn on
  (`None`, `MeshOnly`, `All`, `Default`; left out is `Default`, as today;
  only `All` and the default extend the lights), and a light's own optional
  distance limit for light-specific cases. Exhausts need no such rule.
  Named `ESD_TSREBeyondLastLevel ( DropNothing | DropLights | DropAll )`
  at first; renamed after the user's choice the same day.
- Far lights also coarsen the light grid around the camera (renderer task
  45); dropping them helps there too, but that grid is a renderer task of
  its own.

## Design

- `.sd`: `ESD_TSREDistanceLevelExtension ( MeshOnly )` in the `shape`
  block, `None`, `MeshOnly`, `All` or `Default` (any case). `Default` or
  left out: as the level mode has it (`TSRE_SHAPE_LOD` unset or 1: as
  `All`; 2: as `None`). A name of none of these: as `Default`, told once a
  shape.
- `ESD_TSRELight ( … ViewDistance ( 150 ) )`: farther from the camera (m)
  the light lights nothing; 0 or left out, no limit. `ViewDistance` is not
  in `TS.cpp`'s binary token table (no layout to keep).
- glTF: `extras.TSRE_distanceLevelExtension` (string) on the default scene,
  `extras.TSRE_viewDistance` (number) on a `KHR_lights_punctual` light.
  A glTF model's last level ends only with `MSFT_lod` and a last
  `MSFT_screencoverage` above 0.
- `MeshOnly`: the last level drawn on; its light-carrying packets still
  draw (glows, discs where there are no local lights), but none of its
  lights is gathered into the light grid: its `.sd` lights, its glTF
  emissive surfaces' light, a car's ENG lamps.
- `None`: nothing past the last level; its emitters give out nothing
  either, nor a car's exhaust, lamps or loads.
- `All`: the last level drawn on with its lights, in the Cull mode too.
- Open Rails skips the new entry silently (`STFReader.ParseBlock`: an
  unknown token's block is skipped) and ignores the light's new value as
  it ignores `ESD_TSRELight`.

## Implementation

- `ShapeLod`: `Extension`, `extensionFrom()`; `levelAt()` returns the level
  count past the last level; `apply()` returns a `Drawing` (shape, lights;
  `explicit operator bool` keeps `if (apply(...))`); `drawing()` without
  setting a level (the emitters' check); `level()` -1 where the object
  draws nothing; the unknown name told through `ComplexShape::firstReport`.
- `ComplexShape::distanceLevelExtension()`: `SFileLegacy` (`loadSd`, reset on a
  reload, swapped in `adopt`), `SFileComplex` (`loadMetadata`), `GltfShape`
  (the default scene's extras, swapped in `adopt`).
- `ShapeLights::Light::viewDistance` and `RenderItem::Light::viewDistance`
  (both MSTS readers, glTF lights, `itemLight`).
- `RenderQueue::setLightsShown()` (reset each frame),
  `DrawInstance::lightsShown`; `QueueRenderer::gatherLights` skips an
  instance's lights when not shown (emitters-only gathering, the glows,
  keeps them) and a light beyond its view distance from `viewPosition`.
- Callers set the queue from `apply()`'s drawing around their push:
  `StaticObj`, `WorldObj`, `TrackObj`, `Eng` (the body's drawing also
  covers its loads, ENG lamps and exhaust); `WorldObj::giveOutEmitters`
  asks `ShapeLod::drawing()`.

## Verification

- `shape-lights` (42): both backends and glTF read the values; the
  materials editor's save keeps `ESD_TSREDistanceLevelExtension`; each value's
  drawing near and past the last level on both backends (and `drawing()`,
  `level()`); an unknown name told once a shape; the renderer's gathering
  (lights not shown dropped, glows kept, a light past its view distance
  dropped, a new frame shows lights again).
- `gltf-lod` (49): a scene's `TSRE_distanceLevelExtension` `MeshOnly`
  past the last coverage: the last level drawn, unlit.
- Full build, ctest, and the suites of the emitters review: all pass.
- Route (EUROPE1, a scratch root from `tests/renderer/lights/lamp-root.sh`,
  the 14 light-only `.sd` lamps, last level 1000 m; `TSRE_RHI_TRACE`'s
  gathered lights): near, 14 for the plain lamp and for a copy with
  `ESD_TSREDistanceLevelExtension ( MeshOnly )`; 1.5 km away, 14 for the
  plain lamp (drawn on with its lights) and 0 for the copy once loaded
  (one light in the frame the shape loads in, its levels not yet known).

## Left

- A per-light "drop past the last level" (today the whole object's).
- Discs (no local lights) keep drawing past `ViewDistance` and with
  `MeshOnly`; they are part of the shape's look there.
- The light grid's coarse cells: renderer task 45.
