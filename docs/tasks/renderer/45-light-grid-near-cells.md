# Task 45 - Fine Light Grid Cells Near the Camera

Status: done on `feature/light-grid-columns` (2026-11-06): 16 m columns
around the camera, below. A renderer task of its own, outside the shape
work that found it (task shapes 16).

## Done: 16 m columns around the camera (2026-11-06)

Decided with the user instead of the nested levels: one grid of 512 x 512
cells of 16 m, 4 km from the camera each way (8 km edge to edge, today's
reach), moved in 256 m steps (`LightGrid::Step`) so static lights keep
their cells, each cell a column of every height: the world is one level
(the user: some multi-level stations perhaps later, 99 % one level).
`LightGrid::CellsY` brings height layers back (the shaders read the count,
`localLightLayers.z`, the fog's `layers.w`); each layer costs 1 MB at this
reach. A cell's first index and count are packed into one float's bits
(`LightGrid::packCell`: the count in bits 0-6, the index in 7-29, bit 30
set so the float is an ordinary number) in an R32F texture of 4096 x 64,
read with `floatBitsToUint`: 1 MB, as the old RGBA32F cell texture (an
R32UI texture would need integer samplers through `RhiShaderSource`). The
lit shaders (`LocalLights.glsl`), the fog (`gridCell`) and the signals'
grid (the fog's halos, its own `LightGrid`) read it alike.

Measured at bbb's lamps (task 25's camera, 0:30, the user's profile with
the frame-rate limit off, 842 x 755 on the Deck's display; `TSRE_RHI_TRACE`'s
light line now gives the cells), and with one more street lamp placed
2.5 km away (the capture's `objects`):

| | Old, near | Old, far lamp | New, near | New, far lamp |
|---|---|---|---|---|
| Cells | 32 m x 4 m layers | 64 m x 4 m | 16 m columns | 16 m columns |
| Lamps in the camera's cell | 17 | 44 | 7 | 7 |
| Lamps a cell, mean / most | 17.0 / 49 | 27.8 / 64 (the cap) | 15.5 / 32 | 15.3 / 32 |
| Building the grid (CPU) | 5.4 ms | 3.7 ms | 2.6 ms | 2.6 ms |
| An upload | 1,464 KB | 1,288 KB | 1,176 KB | 1,176 KB |
| GPU, median / p90 | 19.4 / 26.4 ms | 20.6 / 28.7 ms | 18.9 / 25.3 ms | 18.9-19.1 / 25.1-25.4 ms |

The far lamp cost the old grid 1.2 ms (median) and 2.3 ms (p90) of GPU
time; the new grid does not notice it, and is 0.5 ms faster without it.
The images are the same (at most 1 of 255). The build is cheaper too: a
lamp reaching 25 m covers about 16 columns instead of about 117 cells of
4 m layers. (The old runs: one each. A first set ran the old exe with the
new `LocalLights.glsl` from the shared appdata: no local light on
surfaces; both builds' shaders now swapped in per run.)

The fog takes each slice's lamps from the cells at its two ends, so a
slice longer than a cell (far from the camera) could miss lamps between
them. The same scene in valley fog (`"weatherPreset": "valley_fog"`, 0:30)
shows nothing of it: the images alike (about 380 pixels differ, by at most
12 of 255: each cell's 16 lamps the fog takes, picked from other cells),
the GPU 23.3 ms (median) instead of 25.1.

The risk stays in theory (noted, not fixed: the user, 2026-11-06). The
fog's cells are 8 x 8 pixels (High; 16 x 16 Low) by 64 slices from 1 m to
10 km, each slice about 15.5 % of its distance: 1.5 m at 10 m, 16 m at
100 m, 62 m at 400 m, 155 m at 1 km. A lamp is listed in the columns
within its reach (its reach plus up to a cell from it), so a slice misses
it when the slice crosses its reach with both ends outside that: slices
longer than about twice the reach and a cell. For the usual 30 m reach
that is 60 to 76 m slices, 400 to 500 m from the camera (with the old
grid's 32 to 64 m cells, 600 to 800 m); for an asset's 250 m reach,
beyond 3 km. It would show as the halos of lamps 400 m or more away
dimming or flickering in thin mist as the camera moves (in thick fog the
fog hides them). If it shows: for long slices only, look up a few more
points along the slice (every 16 m, at most 4 to 8, lamps counted once),
or give the fog a coarse 64 m copy of the grid (64 KB) for slices longer
than about 16 m.

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
