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

## PBR's shade light from the sky, not the cube (2026-11-11)

**The report.** The user, on CMK's activity 2 at
`core.startup.camera=-5370,14879,826.06,356.29,-542.87,711.91,-0.33`. The
ET22, a MaSzyna PBR loco, turned green when the camera stepped back a few
metres into the bushes. Static views looked great; moving ones went wrong,
worse with fewer faces a frame, and with all six faces a frame a shape's
brightness sometimes flashed for one frame.

**Cause.** One cube is drawn at the camera's eye, and every PBR shape took
its diffuse light from it.
- The diffuse light was the cube's roughest level along the normal.
- It was divided by that level straight up (`ambientScale`).
- So bushes beside the camera tinted every PBR shape in view, and leaves,
  wires or a roof over the camera changed every shape's brightness.
- Nothing blends the faces over time, so a face update steps that light.
- The platform-roof fix (`skyAbove`, 2026-10-28, c97c8d78) changed only the
  classic shade.

**Now** (`PbrShading.glsl` `pbrShadeLight`, QRhi with a physical sky): the
diffuse light no longer comes from the cube.
- **From above:** the sky's light (`skyAbove`, from the atmosphere), as
  bright as the ambient light: the classic shade's colour.
- **From below:** the ground's. Its albedo (`PbrGroundAlbedo`, a little
  warm) times the sky's light and the sun's on level ground. The ground
  counts as sunlit where the point is in the open (clouds, covers); a
  surface facing down takes the point's own shadow, so an underframe over
  its car's shadow stays dark. The point's shadow alone made every wall
  facing away from the sun see unlit ground.
- **Between them:** by the normal's height.
- **Under cover:** towards the neutral ambient's colour and darker, as the
  classic shade.
- **Fallback:** without a physical sky, on OpenGL and in the Shape Viewer's
  room, the cube's diffuse light as before.
- **Reflections** still come from the cube.

**Measured** (`pbr-back` case: the user's camera and 2, 4 m back along the
view, free camera, 12:00, the user's profile; the loco's shaded side, mean
0 to 255):

| | 0 m old | 0 m new | 4 m old | 4 m new |
| --- | --- | --- | --- | --- |
| Lower light band (R G B) | 21 32 24 | 31 42 40 | 39 69 35 | 68 97 95 |

The new colour holds as the camera moves; the old one turned yellow-green
in the bushes. The shaded side is now cooler than the old static look
(mint-cyan instead of mint-green): that green was the camera's
surroundings.

**Captures.** A view's `"freeCamera": true` leaves the train the camera
rides (an activity starts it in the player's cab) and stands at the view's
place.

**Tested live by the user (2026-11-11):** the green loco, the one-frame
flashes and the partial refresh's problems all gone at once. The trade-off,
some realism lost, is worth pursuing later: colours from the environment
(the surroundings' tint in the shade) that don't depend on the camera's
place.

**For later** (the user, 2026-11-11):
- **A stable cube.** Faces drawn into a second cube, switched when all six
  are new, faded over a few frames. Harder, with long fine-tuning; and only
  for the cube's large-scale uses, not close reflections.
- **Two cubes**, rather than leaving nearby things out of the one. One for
  the large scale, skipping close objects, so that the camera's place
  matters less; one for close reflections, with them. Leaving them out of a
  single cube would hurt reflections. Open: the cost of a texture copy in
  the middle of drawing a face.

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

**The user (2026-11-11):**
- 1 and 2 clear to implement.
- 128 px is not enough for Medium or the default; users can still set it.
- 5 also serves the PBR shade's colour (the cold shade, above). Materials
  could take their environment light from the far cube: better than the
  sky alone, without the close objects' trouble. The far cube could hold
  the terrain past 300 m, and later further objects.

**Option 5 estimated before building.** A temporary switch drew the faces
as near updates would be: no sky, no distant band, no clouds, terrain to
300 m. The copy of the far face such an update adds is a 0.5 MB texture
copy.

| The cube a frame, same ride | Today | Near-only faces |
| --- | --- | --- |
| 1 face: CPU / GPU | 2.27 / 2.01 ms | 1.69 / 1.53 ms |
| 2 faces: CPU / GPU | 3.90 / 3.22 ms | 2.79 / 2.48 ms |

- **Most of it is the terrain past 300 m, which option 1 removes alone**
  (terrain to 600 m: -0.34 ms CPU, -0.42 ms GPU a face).
- **The sky, the distant band and the clouds are cheap together:** about
  0.25 ms CPU and 0.1 ms GPU a face.
- **The far cube costs a full face per far face drawn:** about 0.3 ms a
  frame of each if the far cube is new every 1.2 s.
- **Net, for speed, 5 adds about 0.2 ms a frame over 1 at two faces.** Its
  value is the far environment for PBR and reflections. Its lower half
  needs a ground (the terrain past 300 m lies within a degree below the
  horizon from 3 m up).
- **A near face with almost nothing to draw still costs about 0.7 ms
  CPU:** option 2's overhead.
- The GPU times move with the Deck's clock (the main view alone read 8.1
  to 9.5 ms across these runs): differences under about 10 % are noise.

## Options 1 and 2 built (2026-11-11)

**1: terrain in the cube to 1 km** (`EnvironmentTerrainDistance`,
`ViewLimits::terrainMaxDistance`). The cube's limits had left terrain out,
so every side face drew the scene's detailed terrain to 3 km.
- First built as "to 600 m where the route has distant terrain". CMK has
  none, so it did nothing there, and many routes have none.
- The user chose a fixed 1 km on every route. Past it the cube shows the
  fog's colour, or the distant terrain from 600 m where the route has it.
- From about 4 m up, terrain past 600 m lies within about 0.4° below the
  horizon: about a texel row of a 256 px face. Hills past 1 km leave the
  reflections.
- PeakRail's start, where the main view shows hills, gives the same cube
  preview with and without the limit.

**2: work shared by a frame's faces.**
- **A shortlist per pass** (`RhiRenderer::limitedPass`): the draws within
  the cube's limits, found once a frame for every face. Each face still
  tests its frustum, but walks the shortlist, not the whole queue (about
  3,400 draws at CMK, most past the limits).
- **The frame uniforms written once a cube update**
  (`applyBandUniforms`). Before, every band of every face wrote all of
  them, by name, into every program. Now the cube's first band in a frame
  writes them all; its later bands write the projections and the fog's
  level of detail (`writeViewUniforms`). The lights, the haze and the
  rest are the frame's. A program switched to between draws gets all of
  them again, as before.
- **Checked:**
  - the same pixels with and without it: PeakRail's start, all
    1,024,000 the same; CMK's outside view the same;
  - CMK's cab: 4,102 pixels off by 1 or 2 levels, under the 16,933 two
    runs of the same build differ by;
  - build all, ctest 11 of 11, `rhi-shaders` 166, `environment-map-gl`
    19.

**Measured**, the same ride. CPU without the GPU timers: they add a cost of
their own at every flush. The cube's CPU a frame, the mean of its faces:

| | 2 faces | 1 face |
| --- | --- | --- |
| Before | 2.55 ms | 1.31 ms |
| Option 1 | 1.77 ms | |
| Options 1 and 2 | 1.47 ms (three runs, 1.33 to 1.61) | 0.96 ms |

GPU (with the timers), the cube a frame:

| | 2 faces | 1 face |
| --- | --- | --- |
| Before | 3.22 ms | 2.01 ms |
| Option 1 | 2.64 ms | 1.77 ms |

Option 2 changes no GPU work. Two faces a frame now cost about what one
did before on the CPU, and about 30 % more on the GPU.

**Open: a stall.** In some runs, every frame's first cube face took about
2.5 ms more of wall time, with no CPU work behind it in a profile: a wait.
- It hit 3 of 4 runs with the frame log on (`TSRE_FRAME_LOG`), and also
  runs before option 2.
- It hit none of 3 runs without the frame log.
- Not explained; to look at when a session shows it.

**Option 4 looked at, not built.** Timing the prefilter by level:

| Level | L0 | L1 | L2 | L3 | L4 | L5 | L6 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ms a face | 0.03 | 0.10 | 0.04 | 0.025 | 0.02 | 0.02 | 0.02 |

- The rough levels' cost is their pass's fixed cost, not their samples:
  fewer samples there saves nothing. Only L1 (128 x 128, 32 samples) is
  sample-bound; halving its samples would save about 0.05 ms a face.
- Mips for the faces drawn only would take a render pass and a copy per
  level and face, as much fixed cost as the whole cube's `generateMips`
  (0.3 ms a frame).
- What would save the 0.3 ms: no mips at all, the prefilter reading the
  full-size faces with more samples on the small rough levels. It changes
  how the blurred reflections are made: a visual check first.

**The prefilter without mips, tried (2026-11-11):** the user asked to check
it.
- **The change, for the test:** a switch that skipped `generateMips`. The
  prefilter read the full-size faces with more samples on the rough
  levels: 32, 64, 128, 256, 256, 512 from L1.
- **Compared in the Shape Viewer's warehouse cube:** MetalRoughSpheres,
  MetalRoughSpheresNoTextures, DamagedHelmet.
- **Result:**
  - the smooth spheres the same;
  - the mid-rough ones (L2, L3) grainy and sparkling: a few dozen samples
    of the full-size faces miss lobes many texels wide, where each mip
    texel holds their average;
  - the roughest a little blotchy.
- **To match the mips' smoothness** those levels would need four to eight
  times the samples, about the 0.3 ms the mips cost.
- **On a route** (bbb at the user's sphere camera, MetalRoughSpheres placed
  by the capture: an overcast sky over flat fields): nearly the same, the
  mid-rough horizon line a little sharper, no grain. The grain needs
  contrast: the warehouse's lamps, a route's sun disc (clamped bright in
  the prefilter), lamps by night, lit windows.
- **Measured** (the user: the difference hard to see; the real gain
  first). The same ride, 2 faces a frame, two runs each, GPU time a frame:

  | | Mips | Prefilter | Together |
  | --- | --- | --- | --- |
  | With mips | 0.29 ms | 0.58 ms | 0.87 ms |
  | Without | | 2.09 ms | 2.09 ms |

- **Without mips the rough levels cost far more:** L4 0.39 ms, L5 0.36,
  L6 0.57, against 0.04 to 0.05 each with them. L6 is 4 x 4 texels a face,
  so 16 threads each run 512 samples one after another, each reading a
  random place in the full-size faces. With mips, 32 samples read texels
  that already hold the average, and stay in the cache.
- The mips are what make the prefilter cheap: they stay, and option 4 is
  closed. Halving L1's samples (about 0.05 ms a face) is left.

**A double-buffered cube, considered (the user, 2026-11-11):** faces drawn
into a second cube, the two swapped when all six are new; the mips made
once a cube, no copy.

Pros:
- **Mips once a cube, not every frame:** at 2 faces a frame, 0.29 to about
  0.10 ms a frame.
- **A coherent cube:** the six faces in use from the same few frames. No
  seams or light steps between faces of different ages (the partial
  refresh's trouble), and the prefilter reads neighbours of the same
  moment.
- **About 4 MB** for the second cube (256 px, half floats, mips).

Cons:
- **Reflections later:** a face is used in the frame it's drawn today (0
  to 2 frames old). Swapped, it waits for the rest of its cube, then stays
  a cycle: 2 to 5 frames old. About 80 ms more at 30 fps: 1.2 m at 15 m/s,
  3 m at 40 m/s.
- **A spike at the swap:** the prefilter needs the mips, so all six faces
  in the swap's frame (about 1.75 ms with the mips) against 0.87 ms every
  frame today. Spread over the next cycle, it needs a second prefiltered
  cube and 3 frames more.
- **The gain alone, about 0.2 ms a frame:** small for the work (the second
  cube's targets, the swap, the paths that draw all faces at once, OpenGL
  kept on one cube).

The fit: the far cube of option 5 (PBR's environment light, the large
scale), where a few frames' lag doesn't matter and coherence does; not the
close reflections, which want low lag (the user, earlier).

**Prudnik, the heavier route (the user, 2026-11-11).** PSK_2026 to 11:03:30,
train 154 ridden in its cab out of Zawiercie, through the station and past
the yard; 150 s live at 2 faces a frame. Same build, options 1 and 2 off
and on.

Prudnik is GPU-bound: about 15 to 17 fps against the 30 fps limit, the
GPU about 50 ms a frame, the main view 18 to 19 ms of it. Wall times there
include waits, so the CPU work was counted as the thread's cycles
(`QueryThreadCycleTime`).

| 2 faces a frame | Before | After |
| --- | --- | --- |
| fps (CPU runs / GPU-timer runs) | 14.5 / 15.9 | 15.4 / 17.0 |
| The cube's GPU a frame | 7.14 ms | 6.39 ms (-10 %) |
| Side faces' GPU | 3.6 to 4.7 ms | 3.0 to 3.9 ms (about -15 %) |
| The cube's CPU a frame | 34.5 Mcycles (about 12.4 ms) | 27.4 Mcycles (about 9.4 ms) |
| Second faces' CPU (-x, down, -z) | 12.8, 8.0, 13.0 Mc | 7.1, 4.1, 7.5 Mc (-45 %) |

- **The first face of a frame carries the frame's light gathering**
  (`prepareLights`: Prudnik's many lamps, about 10 Mcycles). The cube's
  first band is the frame's first band. The main view would pay it with the
  cube off.
- **Prudnik's faces are mostly objects within the cube's 300 m** (a dense
  station), where CMK's were terrain. The terrain limit helps less there.
  The object distance setting (`core.rendering.environmentMap.
  objectDistance`) is the lever on such routes.

**Tested live by the user (2026-11-11):** no regressions; merged into
`main`. Left for later: option 5 (the far cube, with the double buffer and
PBR's environment colours), option 6 (multiview), halving L1's prefilter
samples, the first-face stall.
