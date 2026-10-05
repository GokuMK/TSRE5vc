# Task 17 - Environment Map For Reflections

## Objective

Provide a cube map of the camera's surroundings so materials can reflect
their environment (metal, glass, glTF metallic-roughness materials). This
task adds the cube and its rendering; nothing samples it for shading yet.
The glTF material work is its first consumer.

## Design

`src/tsre/renderer/EnvironmentMap.h` owns a cube texture (with mipmaps, for
blurred reflections later), a framebuffer and a depth buffer. Faces follow
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
grey brick walls with high windows, a concrete floor, and a dark ceiling with
rows of lamps. It is filled once at 256 px per face.

## Settings

All under Rendering > Reflections, applied while running:

| Setting | Default | Meaning |
| --- | --- | --- |
| `core.rendering.environmentMap.enabled` | off | Render the Route Editor cube |
| `core.rendering.environmentMap.faceSize` | 128 | 64, 128 or 256 px per face |
| `core.rendering.environmentMap.facesPerFrame` | 1 | 1, 2, 3 or 6 faces per frame |
| `core.rendering.environmentMap.objectDistance` | 300 m | Object range in the cube |
| `core.rendering.environmentMap.preview` | off | Draw the cube unfolded in the lower-left corner |

Six 128 px faces hold about as many pixels as one 320 x 240 view. With one
face per frame, each frame renders one 128 x 128 view with reduced object
range; the whole cube refreshes every six frames.

## Status

- [x] Cube, face rendering, round robin, view limits, warehouse fill,
  preview, settings, `environment-map-gl` suite.
- [ ] Cost on hardware (GPU time of the environment phase; `RenderStats`
  reports it as phase `environment`).
- [ ] Consumers: reflecting materials (glTF metallic-roughness, then MSTS
  defaults), with blurred reflections from the mipmaps.
