# Task 27 - Sky Enclosure And Tunnel Lighting

Status: steps 1-3 done on `feature/clouds` with task 28 (2026-10-11, QRhi
only, off by default); see "Done". Signal lights' glow follows through the
splats; their own floor from enclosure is not separate yet.

## Problem

User report, 2026-10-10: lamps in tunnels are dimmed by the time of day as
if they stood in the sun.

- The daylight dimming of lamps (`Daylight::localLights`, 3% at noon,
  rising towards dusk) is one number for the whole frame. The QRhi renderer
  multiplies it into each lamp's intensity, emissive gain and glow on the
  CPU (`RhiRenderer` `gatherLights`, `RhiImage` glow splats, the
  per-item `pbrGlowScale`), before the shaders see them. Signal lights use
  their own factor (`Daylight::signalLights`, at least 50%).
- Ambient (sky) light is not blocked anywhere: the shaders add the same
  ambient term to every surface. Tunnel interiors are lit like open ground
  in shade; only direct sun is shadowed. Ambient occlusion (task 23) is
  screen-space, off by default and untuned, and too local for a tunnel.

The dimming stands in for the eye's adaptation: lamp intensities suit a
night view, and in daylight they would glow far too much. It is right in
the open and wrong wherever the sky is hidden.

## Options Considered

1. **Lamp strength from the sun shadow, per pixel** (the user's first
   idea): the lamp loop takes the day factor as a uniform and uses
   `mix(dayScale, 1, 1 - sunVisibility)`. No new pass, but ordinary shade
   (trees, buildings, the train) looks like a tunnel: lamp pools at night
   strength at noon, though shade on a sunny day still gets thousands of lux
   of skylight. Shadows are close to 0 or 1, so a sun scale above 1 would
   not soften it. Rejected for now: only routes with glTF lamps in tunnels
   are affected today, and its shader work is part of option 2 anyway.
2. **Sky enclosure map** (chosen): see Design.
3. **Automatic exposure** (eye adaptation, QRhi): later, on top of option 2;
   see "Later".
4. **Nothing**, as Open Rails does: tunnels stay wrong.

## Design

### Sky enclosure

A depth pass from straight above around the camera, like a shadow map with
the light at the zenith, reusing the shadow-caster drawing (task 10):
about 1024 x 1024 over 300-500 m, recentred as the camera moves.

- A surface with something above it (terrain over a tunnel, a bridge deck,
  a station roof) is enclosed. Terrain over a tunnel counts: the tunnel lies
  under the terrain surface, which is the top of the map.
- Grayscale, not 0 or 1: a wide soft filter (large-kernel PCF, or a
  filterable depth map such as variance or exponential shadow maps) gives
  gradual values, so a portal darkens gradually going in and roof edges are
  soft.
- Beyond the map's range, and with the pass off: open sky (today's
  behaviour).
- Foliage: tree canopies would count as roofs. Leave alpha-tested
  vegetation out of the pass, or weight it, after a look at forests.

### What it drives

- **Lamps**: per pixel, `mix(dayScale, 1, enclosure)`. The renderer passes
  lamp intensities without the day factor, plus the factor as a uniform;
  the lamp loop applies it with the enclosure of the lit surface.
- **Emissive surfaces** (lamp bulbs): the same, from their own pixel.
- **Glow** of each lamp (the glow splats): the enclosure at the lamp's
  position, one read of the map in the splat shader.
- **Signal lights**: from their daytime floor (50%) to full strength when
  enclosed.
- **Ambient**: `skyAmbient * mix(floor, 1, enclosure)`, so tunnels and
  underpasses go dark.

### Old content

Old routes rarely have lamps or emissive textures in tunnels; with a floor
of 0 their tunnels would go black. So the ambient keeps a floor:

- a global setting (`core.rendering.enclosedAmbient`, about 0.25-0.35 by
  default, to be tuned), the share of the sky ambient left where the sky
  is fully hidden;
- a route override in the TRK file, as TSRE's other route values there:
  `TsreEnclosedAmbient ( 0.3 )` (user, 2026-10-10: the TRK already holds
  TSRE values, and MSTS accepts extra tokens that follow the format).

Lamps follow enclosure fully: routes with lamps get proper lamp pools in
dark tunnels.

MSTS's full-bright parts (light material indices that ignore lighting, used
by a few routes for lit tunnel textures) belong to the separate task of
full MSTS shading, which is on the to-do list; not part of this task.

## Clouds

Recommended: include this task in the clouds rendering task. Both need
grayscale visibility from above, and they meet in the same shading terms:

- **Cloud shadows** follow the sun: a cloud transmittance (0-1) read per
  pixel by projecting along the sun direction onto the cloud density, no
  depth pass; it multiplies the sun shadow. Overcast weather also lowers
  the sun and the sky ambient everywhere.
- **Sky enclosure** looks up: the depth pass above.
- Shading: sun = sun colour x shadow x cloud transmittance; ambient = sky
  ambient (lower when overcast) x mix(floor, 1, enclosure).
- Both are top-down maps around the camera; one coordinate system, perhaps
  one texture with two channels.

## Later

- **Automatic exposure** (QRhi): the frame's average brightness (from the
  bloom downsample chain) sets the exposure over 1-3 s, and the lamp
  dimming gives way to physical lamp intensities. Tunnels brighten after a
  moment and portals glare. Needs the enclosed ambient first (otherwise a
  tunnel does not measure as dark) and long tuning.

## Steps

1. Sky enclosure pass (QRhi first; OpenGL if cheap, for ambient only, as
   lamps are QRhi-only), with a capture of a tunnel portal and an
   underpass.
2. Lamps, emissive surfaces, glow and signal lights from enclosure.
3. Ambient with the floor: setting, TRK override, editor field in the
   route settings window.
4. Clouds join here if done together.

## Verification

- Captures: a tunnel with glTF lamps at noon and at night (lamps the same
  inside, dim outside at noon), a tunnel without lamps (dim, readable at
  the floor), an underpass and a station roof, open shade under trees (no
  lamp pools at noon).
- Unit: the zenith projection, the filter's gradient at an edge, the TRK
  token round trip.
- Hardware: the cost of the extra pass on the Steam Deck.

## Done (2026-10-11, `feature/clouds`)

- Map: `renderSkyEnclosure` (editor) after the shadow maps draws terrain
  and opaque surfaces (passes terrain and opaque, alpha-tested vegetation
  left out) with the shadow program from 300 m above the camera into a
  1024 x 1024 depth map over 400 m, snapped to its texels
  (`Renderer::beginSkyEnclosure`, `renderEnclosureCasters`,
  `setSkyEnclosure`; QRhi: target `TargetEnclosure`, rows in OpenGL's order
  as the shadow maps; `planShadowCasters(..., enclosure)` picks all opaque
  surfaces).
- Shaders (`ShadowSampling.glsl`, `TSRE_RHI`): `skyEnclosed(position,
  normal)`: 16 taps 0.8 m apart around the point, each counting something
  1.5-4 m or more above it; walls are looked up 2 m out along their normal,
  past the eaves, so they do not count their own roof. Ambient (standard
  and PBR environment light, and the ambient share AO reads) times
  `mix(1, floor, enclosed)`; lamps (standard and PBR) times
  `mix(day, 1, enclosed)`; the PBR glow `mix(pbrGlowScale, full, enclosed)`.
  With the map on, the light grid is gathered without the day factor and
  the shaders apply it (`localLightDay`).
- Glow splats: gathered without the day factor; each carries its own
  (local or signal) and the splat looks up the map at the emitter.
- Settings `core.rendering.skyEnclosure` (off), `core.rendering.enclosedAmbient`
  (0.3); TRK `TsreEnclosedAmbient ( x )` (written only when set); route
  settings window: "Enclosed ambient" (Default = the setting's).
- Checked: EUROPE1 tunnel portal (tile -6110 15068) and stone bridge
  (-6110 15072) (`tests/renderer/enclosure-views.json`): the tunnel mouth
  and the ground and pier under the arch darken, open ground and walls stay
  (only thin strips at wall bases under eaves change); street lamps before
  the portal (`enclosure-lamp-views.json`, game root from
  `tests/renderer/lights/lamp-root.sh`) unchanged at noon and at night;
  defaults pixel-identical.
- Not shown yet: a lamp deep in a tunnel by day (the test lamp in the
  tunnel mouth was hidden by the portal); the Steam Deck's cost of the
  pass (another shadow-size depth pass, all opaque surfaces in 300 m).

### Found on the way: glow lost after depth-only passes (QRhi Vulkan)

On QRhi Vulkan (lavapipe), the glow (bloom input) of a frame is empty
whenever a depth-only pass with draws ran before the main view: the shadow
maps by day (lamps' 3 % glow at noon gone with shadows on, present with
them off) and the enclosure at night. QRhi OpenGL keeps it. Recording the
draws is not the cause; executing them is. Not fixed; to be checked on the
Steam Deck at dusk (lamps glowing while the sun still casts shadows).

### Cached map (2026-10-12)

Drawing the map every frame was a second pass over everything within 300 m:
in the CMK station hall about 6.5-8 ms and 1.6 ms of CPU on the Steam Deck.
It is now drawn from a point snapped to 31.25 m cells (80 texels, so its
texels keep their place) and 16 m in height, kept while the camera stays in
its cell, and drawn anyway once a second for edits; the result is the same.
See task 28, "Review and performance".

## Known Limitation (0.7.8)

Lamps in tunnels are dimmed by daylight as in the open. Today this affects
only routes with glTF lamps or emissive surfaces in tunnels.
