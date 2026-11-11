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

**For later** (the user, 2026-11-11):
- **A stable cube.** Faces drawn into a second cube, switched when all six
  are new, faded over a few frames. Harder, with long fine-tuning; and only
  for the cube's large-scale uses, not close reflections.
- **Two cubes**, rather than leaving nearby things out of the one. One for
  the large scale, skipping close objects, so that the camera's place
  matters less; one for close reflections, with them. Leaving them out of a
  single cube would hurt reflections. Open: the cost of a texture copy in
  the middle of drawing a face.
