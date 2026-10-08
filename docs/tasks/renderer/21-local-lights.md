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
and bins them into a world-space grid of 64 x 64 x 16 cells spanning the
lights' reach, within 4 km of the camera horizontally and 1 km vertically.
Horizontal cells are 16 m while the lights fit 1 km and double (aligned to
their size) for wider spreads; the 16 layers span the lights' heights.
Every view of the frame (main view, environment faces, water reflection)
reads the same grid. Each cell lists the lights whose range reaches it, the
brightest first, at most 64; fragments outside the grid get no local light.
A frame whose lights match the previous one keeps its grid without
rebinning or uploading.

Binning costs about 0.9 microseconds per light on the CPU (optimised
build: 1.7 ms for 2000 lights, 7.5 ms for 10,000), paid only when the lights
change. Larger counts would move binning to a compute pass.

The grid lives in three float textures (light data, cell offsets and counts,
light indices), so the shader code stays within GLSL 3.30 and could move to
the OpenGL renderer later.

## Shading

- PBR: each light is shaded like the sun (GGX specular, Lambert diffuse,
  clearcoat), with spot cones (smooth between the inner and outer angles).
- MSTS shapes and terrain: Lambert diffuse added to the sun and ambient
  light.
- Water and unlit packets take no local light.

With time of day (task 22) lamps and glows adapt to daylight as eyes and
cameras do: full at night, 3 % in full daylight, so lamps light the scene at
dusk and night without pools of light at noon.

With time of day off, lamps and glows are always at full strength, although
the fixed editor light is a daytime sun (about 55 degrees up), under which
time of day would give 3 %. This is intended, not a bug: off is an editing
mode in which every lamp's light stays visible for placing and checking
lamps. Pools of light in the daylit editor are therefore expected with
time of day off; to judge how lamps look by day, dusk or night, turn time
of day on (user's decision, 2026-10-08).

## Settings

- `core.rendering.localLights.enabled` (on).
- `core.rendering.localLights.exposure` (1): punctual light scale.
- `core.rendering.localLights.emissiveGain` (1): emissive light scale.

## Verification

- Unit tests: emitter extraction (power and centroid of an emissive quad,
  emissive map masking), grid binning (a light reaches the cells within its
  range, the brightest kept first), units of point lights.
- Captures: Khronos `PointLightIntensityTest` (colour channels match the
  white light), `LightsPunctualLamp`, `EmissiveStrengthTest`, on QRhi Vulkan
  and OpenGL.
- Route scene: `tests/renderer/lights` holds a generated street lamp (6 m
  pole, warm emissive lens, a 60 cd spot light down; `make_street_lamp.py`)
  and `lamp-views.json`, which places 14 lamps in two rows at the start of a
  route for the capture only (the harness's `objects` option) and views them
  from the street and from above (`aboveGround`). `lamp-root.sh` builds a
  game root of links in which the route finds the lamp, leaving the route
  unchanged. EUROPE1 on 2026-12-21: no light at noon, pools of light and
  lit walls at 15:45 and 18:00.
- OpenGL renderer captures stay identical.

## Later

- Shadows from local lights.
- Directional punctual lights as extra suns.
- MSTS light sources (wagon lights, route lights) as emitters.
- HDR target with exposure and bloom.
