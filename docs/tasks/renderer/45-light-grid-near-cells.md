# Task 45 - Fine Light Grid Cells Near the Camera

Status: open issue, not started (written 2026-11-06). A renderer task of
its own, outside the shape work that found it (task shapes 16).

## The issue

The local lights' grid (task 21, `LightGrid`) is one world-space grid of
64 x 64 x 16 cells spanning every light it keeps, up to 4 km around the
camera. Its horizontal cells are 16 m while the lights fit about 1 km, and
double as they spread: 32 m past 1 km, 64 m past 2 km, 128 m past 4 km.
Its 16 layers span the lights' heights.

With the 3 km object distance, one lit object far away (a coach, a
station, a lamp on a hillside) makes the cells around the camera 4-8 times
wider, and the layers taller. Every pixel near the camera then walks more
lamps, a busy cell can reach its cap of 64 (its dimmest lamps dropped),
and the fog takes only a cell's first 16. So far lights cost most where
they are not: next to the camera. Not measured yet (an estimate from the
code).

## Why one grid

One world-space grid serves every view of the frame (the main view, the
environment faces, the water reflection) and the fog's march, which rules
out a grid sliced along one camera's view (clustered shading). It stays
within GLSL 3.30 (three float textures, binned on the CPU), and cells
aligned to their size keep static lights in their cells, so the grid is
rebuilt only when the lights change.

## A way to fix it

Two nested world-space levels, both read by every view:

- fine: 16 m cells over about 1 km around the camera, moved in steps of
  about 256 m so it is not rebuilt each frame; its own layers over its own
  lights;
- coarse: wide cells over the whole 8 km, as the grid is at its coarsest.

A point inside the fine window reads the fine level, any other the coarse
one; a light near the window's edge is binned into both. Both levels can
be rows of the existing cell texture with the index list shared (no new
bindings). Work: `LightGrid` (two levels), the lookup in
`LocalLights.glsl`, the fog's cells along a ray crossing levels
(`RhiFog.cpp`, `gridCell`), the grid's tests. An estimate: 150-250 lines,
about a day with captures.

Alternative: one grid whose cells widen with the distance from the camera
(a logarithm a lookup, recentred in steps); harder to reason about and
test.

## First

Measure: `TSRE_RHI_TRACE`'s light lines (lights, cell size), the cells'
lamp counts and the frame time on a lit station, with and without a lit
object 2 km away.

Task shapes 16 already lets content drop far lights (an `.sd`'s
`ESD_TSREDistanceLevelExtension ( MeshOnly )`, a light's `ViewDistance`);
it does not fix the grid for lights that should show from afar.
