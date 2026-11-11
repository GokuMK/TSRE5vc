# Task 17 - Environment Map For Reflections

## Objective

Provide a cube map of the camera's surroundings so materials can reflect
their environment (metal, glass, glTF metallic-roughness materials). This
task adds the cube and its rendering; glTF PBR materials (shapes task 06)
sample it.

## Design

`src/tsre/renderer/EnvironmentMap.h` owns a cube texture with mipmaps, a
second cube prefiltered with the GGX lobe per roughness (what the scene
shaders sample), a framebuffer and a depth buffer. Faces follow
the OpenGL cube map order and orientation (+X, -X, +Y, -Y, +Z, -Z); the
`environment-map-gl` suite checks both by drawing a coloured marker in every
direction. The cube is bound on texture unit 10 for the scene shaders.

QRhi keeps both cubes in half floats where the GPU has them (RGBA16F, else
RGBA8; OpenGL RGBA8). With auto exposure the cube holds the scene's light,
a thirtieth at night: in 8 bits the sky from above was a few steps a
channel, and the shade, which takes its colour, changed hue from minute to
minute before sunrise (CMK 3:00-3:45: fields violet, red, olive in turn,
chromaticity up to 0.25 a minute; 0.025 in half floats, the dawn's own
drift; the user, 2026-10-28). The prefilter clamps what it reads to 0-1, as
8 bits held it, so the sun and lamps reflect as before; the Shape Viewer's
8-bit warehouse faces are converted on upload (its images unchanged).

### Route Editor

One cube, rendered from the camera position after the shadow maps, from the
queue already gathered for the main view (`Renderer::renderPassesRetained`
draws without consuming it):

- sky, distant terrain, then terrain, objects and water. Editor overlays
  are left out.
- Objects are limited per view (`Renderer::setViewLimits`): those farther
  than the object distance setting, or smaller than about one cube texel,
  are skipped. Terrain and sky are always drawn.
- Faces are rendered round robin, a few per frame; every face is rendered
  once before the round robin starts, so the cube is never partly empty.

What the gathered queue contains in every direction:

- Objects: `WorldObj` only drops objects behind the camera farther than
  their size plus 150 m, so everything near the camera is present.
- Terrain: patches used to be culled against the main view while
  gathering. With the cube on (`Terrain::gatherAllDirections`), surface
  patches within range are gathered all around the camera, and the renderer
  culls each view. Terrain packets now carry bounds for that; level of
  detail and procedural texture preparation still follow the main view.
- The distant pass is culled against its projection in the main view too.

### Shape Viewer

A fixed procedural warehouse interior (`EnvironmentMap::fillWarehouse`):
grey brick walls with high windows, a dark painted band along their lower
part and an open loading door in each wall with daylight outside, a concrete
floor, and a dark ceiling with rows of lamps. It is filled once at 256 px
per face. The band and doors give surfaces that look sideways (car bodies)
something to reflect at their own height; plain brick blurred by a satin
finish reflected as flat grey.

## Settings

All under Rendering > Reflections, applied while running:

| Setting | Default | Meaning |
| --- | --- | --- |
| `core.rendering.environmentMap.enabled` | on (since 0.7.8; off before) | Render the Route Editor cube |
| `core.rendering.environmentMap.faceSize` | 256 | 128, 256 or 512 px per face (512 since 2026-10-28, for screenshots: four times the cost a face) |
| `core.rendering.environmentMap.facesPerFrame` | 1 | 1, 2, 3 or 6 faces per frame (at 1 and 30 fps the cube is new five times a second: 2 or 3 for a discrete GPU, the user 2026-10-28) |
| `core.rendering.environmentMap.objectDistance` | 300 m | Object range in the cube |
| `core.rendering.environmentMap.preview` | off | Draw the cube unfolded in the lower-left corner |

With one face per frame, each frame renders one face with reduced object
range; the whole cube refreshes every six frames.

Hardware test on the heavy CMK route (2026-10-05): face size and object
distance have little effect on frame rate, so 256 px became the default and
64 px was dropped. One face per frame costs under 10% of the frame rate;
six per frame is clearly visible. Two per frame is a candidate default.

## Status

- [x] Cube, face rendering, round robin, view limits, warehouse fill,
  preview, settings, `environment-map-gl` suite.
- [x] Cost on hardware: see Settings. `RenderStats` reports the cube as phase
  `environment`.
- [x] First consumer: glTF metallic-roughness materials (shapes task 06),
  with reflections blurred through the mipmaps.
- [ ] MSTS shapes: reflecting materials need defaults chosen from their
  shader names.
- [ ] Note (user, 2026-10-12): glTF shapes are expensive to draw in the
  reflections. On `bbb`, with a field of reflective test spheres in the air
  (`core.startup.camera=-5306,14961,-468.29,69.05,-58.96,-175.54,2.80`),
  the cube cost 3.8-4.7 ms a frame on the Steam Deck at one face a frame
  (1280 x 800). To look at later: what the glTF draws cost per face, and
  whether they need their own distance or size limit in the cube.

## Cost on a ride and options (2026-11-11)

**The user.** On train rides one face a frame (a cube every 6 frames) is
too slow. Two a frame (every 3) looks fluid. Measure, review, propose.

**Measured.** CMK, activity 1 (the CD), ridden live from the cab at 15 m/s.
842 x 755, the user's profile (30 fps limit, clouds Ultra), Steam Deck, the
last 38 s of each run. Temporary timers: GPU time per face, the mips and
the prefilter (`TSRE_GPU_PASS_TIMES`); CPU time per face; the frame log.

Per face, at 256 px, one a frame (medians):

| Face | GPU ms | CPU ms |
| --- | --- | --- |
| +x | 2.0 | 2.1 |
| -x | 1.6 | 1.9 |
| up | 0.7 | 1.3 |
| down | 1.3 | 1.2 |
| +z | 1.4 | 1.7 |
| -z | 1.6 | 1.9 |

On top: the prefilter 0.27 ms GPU per face drawn, the mips 0.32 ms a frame,
0.6 ms CPU a frame to finish.

The cube a frame (the frame log's `environment` for CPU):

| | CPU ms | GPU ms |
| --- | --- | --- |
| 1 face | 2.5 | 2.1 |
| 2 faces | 4.3 | 3.6 |
| 3 faces | 5.8 | 4.4 |
| 1 face, 128 px | 2.3 | 1.5 |
| 2 faces, 128 px | 4.2 | 2.5 |
| 1 face, objects to 100 m | 2.5 | 2.1 |
| 1 face, objects under 3 texels left out | 2.5 | 2.1 |
| 1 face, clouds off | 2.2 | 2.1 |
| 1 face, terrain to 600 m | 2.2 | 1.7 |

The GPU numbers move with the Deck's clock, which follows the load. Compare
within a run, or the cube's share. A first run with objects to 100 m read
4.7 ms CPU; run again, 2.5 ms (a dip in the shared power or clock).

**Where it goes.**
- **Terrain.** The cube's distance limit leaves terrain out, so every side
  face draws the scene band's detailed terrain to 3 km. Limited to 600 m,
  where the distant band begins, side faces cost 25-30 % less GPU and
  about 20 % less CPU. The cube's preview looks the same on CMK: the
  distant terrain carries the horizon.
- **Objects matter little.** Objects to 100 m instead of 300 m, or leaving
  out those under 3 texels, changed nothing.
- **A fixed CPU cost per face, about 1.2 ms.** The up face, all but empty,
  costs 1.3 ms. A Very Sleepy profile at 3 faces a frame (30 s):
  - preparing draws (`recordInstances`) 54 %, of which `recordDraw` is 36 %
    (uniforms, bindings, samplers), grouping 7 % and `hasMesh` 6 %;
  - recording the face's commands in QRhi (`flushTarget`) about 25 %, half
    of it `setShaderResources`;
  - the three bands' setup (`beginViewBand`, frame uniforms with `pow`
    each time) 11.5 %;
  - the frustum test itself only about 6 %; the clouds about 3 %.
- **Resolution.** At 128 px the faces cost about 30 % less GPU (the down
  face 46 %) and the prefilter half. The CPU stays the same.
- **The cab.** From the cab, the cube sees the cab: its walls fill the
  faces. The world behind them is still drawn.
- **The user's idea, up and down in one frame, is today's order at two a
  frame:** +x -x, up down, +z -z. For a given cycle, the work per frame is
  the same however the faces are grouped (6 in 3 frames is 2 a frame).
  Grouping only spreads the peaks.

**Options**, for 2 faces a frame at about today's 1-face cost:
1. **Terrain in the cube to 600 m**, where the distant band takes over:
   -20 % GPU, -14 % CPU at one face. Small, low risk. Routes without
   distant terrain keep the full range.
2. **Work shared by the faces of a frame:**
   - one walk over the queue for all faces, with a mask of the faces each
     item reaches, instead of six full walks;
   - `hasMesh` once per item;
   - the frame uniforms computed once a frame and reused by every band and
     face.
   This attacks the fixed ~1.2 ms CPU per face. Moderate.
3. **128 px faces**, for rides or as the default with two a frame: 2 faces
   at 128 cost 2.5 ms GPU, about 1 face at 256 today. Softer sharp
   reflections. No code.
4. **Cheaper mips and prefilter:**
   - mips only for the faces drawn, not the whole cube (0.32 ms a frame);
   - fewer GGX samples on the rough levels (0.27 ms a face).
   Small to moderate.
5. **A far and a near cube** (the user's two-cube idea): sky, clouds and
   distant terrain into a far cube every N frames; each update copies it and
   draws the near content over it. Saves the sky, clouds and distant band
   per face, and helps the reflections' design too. Larger.
6. **Several faces in one recording** (multiview into a texture array): the
   CPU of preparing and recording shared by the faces. Every shader would
   need the view index. Largest.

1 + 2 + 3 together: two faces a frame at about today's one-face GPU, and
CPU near it.
