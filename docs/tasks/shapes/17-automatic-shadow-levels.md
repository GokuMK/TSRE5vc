# Task 17 - Automatic Shadow Levels

## Objective

Give every shape a level of detail fit for shadow and reflection maps,
made when the shape loads where its author gave none: few parts, its
outline kept. The shadow maps (the sun's cached ones, the lamps' maps and
their tiles over moving trains) and the reflection maps then draw that
level, not the view's.

Status: planned (the user, 2026-11-09). Not started.

## Why (user, 2026-11-09)

- The lamps' tiles over moving trains (renderer task 46 A) can draw the
  cars near the camera with their own shapes, not their boxes. At the
  standing EMU at Prudnik this costs about 0.12 ms of the GPU and 0.13 ms
  of the CPU more a lamp than the boxes (9 lamps, 3 cars, 1,553 parts).
- A coarser level would cost less, but the content does not have one: the
  EMUs and the IC's ED160 at Prudnik and Zawiercie have a single distance
  level, as about two thirds of the cars' shapes on the route. Leaving
  out a car's small parts does not help either: MSTS cars are a few large
  primitives (parts over 1 m kept 1,206 of 1,553).
- So TSRE makes the level itself, at load, when a shape has no coarse one.

## Design (to be worked out)

- `ComplexShape::shadowLevel()` is the level shadows are drawn with where
  a shape stands for itself (task renderer 46 A). For now it gives the
  shape's coarsest level (glTF: its shadow floor level), the view's when
  the shape has one level; the generated level takes its place here, so
  the renderers stay as they are.
- Generation: a simplified mesh of the shape's parts (merged by material
  where the alpha test allows, decimated, small parts dropped), made on
  the loading thread (task 08) and kept with the shape; textures kept for
  the alpha test (windows, fences, trees).
- Reflection maps (water, environment) take it too.
- Measure with `TSRE_GPU_PASS_TIMES=1` (the lamps' tiles, the sun's
  shadow maps) and the frame log's CPU times.

## Related

- `docs/tasks/renderer/46-local-light-shadows.md` (part A: moving trains).
- `11-msts-distance-levels.md`, `10-gltf-native-lod.md` (the levels the
  content has).
