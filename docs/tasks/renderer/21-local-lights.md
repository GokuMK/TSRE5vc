# Task 21 - Local Lights

## Objective

Light from sources in the scene, not only the sun: glTF punctual lights
(`KHR_lights_punctual`) and emissive surfaces that light their surroundings,
in any number, on the QRhi renderer (task 20). The OpenGL renderer keeps the
sun and ambient light only.

## Sources

Lights travel with packets. `RenderItem::lights` holds emitters in the
packet's model space (before `msMatrix`); every queued instance of the
packet brings its lights with its own transform, so instanced objects and
repeated shapes light the scene without extra work by producers.

- Punctual lights: point and spot lights of the glTF scene, attached to the
  first packet of the shape and moved into its model space. Directional
  lights are left out (the sun lights the scene).
- Emissive surfaces: each emissive primitive becomes emitters at load. Its
  triangles are binned on a grid of cells about a quarter of the primitive's
  size (at least 0.5 m); each cell with emitted light gets one emitter at
  the centroid of its emission, with the summed emitted power and a radius
  matching the cell's area. The emission of a triangle is the material's
  emissive factor (with `KHR_materials_emissive_strength`) times the
  emissive map at the triangle's centre, decoded once at load.

## Units

The engine's light unit is the sun's: a white matte surface lit by
irradiance 1 shows white (the legacy diffuse colour of the sun).

- Punctual lights follow the Khronos sample viewer: a light of intensity
  I candela lights a white matte surface at distance d, facing it, to
  `I / (pi d^2)`, times the exposure setting.
- An emissive surface of displayed radiance M and area A lights a surface
  at distance d to `M A / (4 pi d^2)` (a sphere of that area and radiance),
  times the emissive gain setting. Emission therefore needs
  `KHR_materials_emissive_strength` (or a gain) to light more than its
  immediate surroundings, as in physically based renderers.
- Lights fall off as `I / (d^2 + r^2)` (r: the emitter's radius, softening
  close surfaces) and fade to zero at their range: the glTF range, or where
  their light falls under 1/512.

## Light grid

The QRhi renderer gathers the lights of all queued instances once per frame
and bins them into a world-space grid around the camera: 64 x 64 cells of
16 m horizontally, 16 layers vertically spanning the lights' heights. Every
view of the frame (main view, environment faces, water reflection) reads the
same grid. Each cell lists the lights whose range reaches it, the brightest
first, at most 64; fragments outside the grid get no local light.

The grid lives in three float textures (light data, cell offsets and counts,
light indices), so the shader code stays within GLSL 3.30 and could move to
the OpenGL renderer later.

## Shading

- PBR: each light is shaded like the sun (GGX specular, Lambert diffuse,
  clearcoat), with spot cones (smooth between the inner and outer angles).
- MSTS shapes and terrain: Lambert diffuse added to the sun and ambient
  light.
- Water and unlit packets take no local light.

## Settings

- `core.rendering.localLights.enabled` (on).
- `core.rendering.localLights.exposure` (1): punctual light scale.
- `core.rendering.localLights.emissiveGain` (1): emissive light scale.

## Verification

- Unit tests: emitter extraction (power and centroid of an emissive quad,
  emissive map masking), grid binning (a light reaches the cells within its
  range, the brightest kept first), units of point lights.
- Captures: Khronos `PointLightIntensityTest` (colour channels match the
  white light), `LightsPunctualLamp`, `EmissiveStrengthTest` and a route
  scene with emissive objects at night, on QRhi Vulkan and OpenGL.
- OpenGL renderer captures stay identical.

## Later

- Shadows from local lights.
- Directional punctual lights as extra suns.
- MSTS light sources (wagon lights, route lights) as emitters.
- HDR target with exposure and bloom.
