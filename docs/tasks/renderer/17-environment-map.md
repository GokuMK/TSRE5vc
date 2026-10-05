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
| `core.rendering.environmentMap.enabled` | off | Render the Route Editor cube |
| `core.rendering.environmentMap.faceSize` | 256 | 128 or 256 px per face |
| `core.rendering.environmentMap.facesPerFrame` | 1 | 1, 2, 3 or 6 faces per frame |
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
