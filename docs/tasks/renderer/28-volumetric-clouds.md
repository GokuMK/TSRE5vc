# Task 28 - Volumetric Clouds and Sky

Status: design (user, 2026-10-10). Not started. Includes task 27 (sky
enclosure). Reads its weather from task environment 01; the first steps do
not need it.

## Objective

Clouds that look like clouds from a train or the editor camera at any time
of day: volumetric low and mid clouds, a flat high layer, a sky that matches
them, their shadows on the ground, overcast light, and light shafts. QRhi
renderer first; the OpenGL renderer and a generated skydome later.

## Decisions (user, 2026-10-10)

| Topic | Decision |
|---|---|
| Low and mid clouds | Raymarched volumes (Nubis-style), with the usual shortcuts |
| High clouds | Flat layer |
| Renderer | QRhi; OpenGL fallback and skydome generation later |
| Sky | A physical sky and atmosphere model, part of this task |
| Task 27 | Included (shared top-down maps and shading terms) |

## Options considered

1. **Flat layers** (MSTS's textured sky domes): no volume, wrong at low sun.
   Kept for high clouds and for the fallback.
2. **Sprite clusters** (Wang 2003, CryEngine 2 / Crysis): clouds built from
   lit sprites, soft against scene depth, edge glow towards the sun; shadows
   in a full-screen pass; screen-space sun shafts. Hand-built clouds, hard
   to drive from weather maps, flat up close. Rejected.
3. **Raymarched volumes** (Schneider 2015-2022, Hillaire 2016): density
   from a weather map, a height profile per cloud type and 3D noise;
   rendered at a reduced resolution with temporal reprojection (under 2 ms
   on a PS4 in 2015, a GPU class like the Steam Deck's). Chosen.

## Design

### Sky and atmosphere

Hillaire 2020: a transmittance LUT, a multiple-scattering LUT, a sky-view
LUT per frame and an aerial-perspective volume. Sun and moon from
`SunPosition`, `MoonPosition`. Replaces the flat sky colour on QRhi when
the physical sky is on (setting); the environment map (17), the water
reflection (18) and the fog take their colours from it. HDR values (24).

### Cloud density

- Weather from environment 01: per layer coverage, type, density, base and
  top, sampled with the generated detail and the wind drift.
- Height profile by type (stratus thin and flat, cumulus rounded and taller,
  cumulonimbus to the high layer).
- Noise: a 128^3 Perlin-Worley shape volume and a 32^3 Worley detail
  volume, generated once at start (or cached on disk), plus 2D curl noise
  for distortion; detail erodes the shape at the edges.
- A shell around the planet (radius 6360 km), so far clouds sink below the
  horizon.

### Rendering

- The camera is below the clouds in normal use; above or inside (mountain
  routes) works but is not optimised.
- Quarter resolution, one pixel of each 4 x 4 block marched per frame
  (blue-noise order), reprojected and blended with the history; full
  resolution upsampled with the scene depth.
- Marched up to the scene depth, so clouds sit in front of or behind
  terrain and objects; aerial perspective applies to them.
- Lighting: a short march towards the sun (self-shadow), Beer-Lambert with
  the powder term, a two-lobe Henyey-Greenstein phase, an octave
  approximation of multiple scattering, ambient from the sky above and the
  ground below.
- Early out at full opacity and beyond a distance; step length grows with
  distance.
- High layer: lit 2D cirrus textures on a shell at about 8-10 km, coverage
  from the weather's high layer.

### Cloud shadows and overcast

- A top-down cloud transmittance map along the sun direction around the
  camera, updated over several frames. The lit shaders multiply the sun
  term by it (terrain, shapes, glTF, water).
- Overcast (from the weather's coverage) lowers the sun and raises the
  ambient share in `Daylight`.
- Task 27's sky enclosure map shares its coordinate system: one texture,
  two channels if it fits.

### Light shafts

1. Screen-space: a radial blur from the sun of the sky's visibility times
   the cloud transmittance towards the sun (Mitchell 2007, Sousa 2008).
   Only with the sun on or near the screen.
2. Volumetric: a low-resolution march through the lower atmosphere reading
   the cloud transmittance map and the shadow maps: shafts under the clouds
   from any view, and later lamps in fog (froxels, Hillaire 2015).

### Performance

Steam Deck target: 2-3 ms for clouds and sky at 1280 x 800. Quality setting
Off / Low / Medium / High (march steps, resolution, noise octaves). Off
keeps today's sky.

## Steps

1. Physical sky (LUTs) on QRhi, with the sun and moon; environment map,
   water and fog colours from it.
2. Noise volumes; raymarched clouds with constant coverage; temporal
   reprojection.
3. Weather from environment 01 (presets first): coverage, types, wind
   drift; high layer.
4. Lighting quality: phase, powder, multiple scattering, ambient.
5. Cloud transmittance map: cloud shadows in the lit shaders; overcast in
   `Daylight`.
6. Screen-space light shafts.
7. Task 27: sky enclosure, lamps and ambient (see task 27's steps).
8. Volumetric light shafts.
9. OpenGL renderer: a flat cloud layer from the weather; generated skydome
   textures for routes and for MSTS and Open Rails.

## Verification

- Captures (QRhi): presets (fair cumulus, broken, overcast, front) at noon,
  in the evening, at sunset and with the sun behind a cumulus (shafts);
  clouds against a mountain; looking along a long straight towards the
  horizon.
- Temporal stability: a camera moving and turning over frames, without
  smearing or crawling.
- Unit: noise generation, the weather sampling, the shell geometry.
- Steam Deck: time per frame at each quality.

## References

- Schneider: [Horizon 2015](https://www.advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf),
  [Nubis 2017](https://advances.realtimerendering.com/s2017/Nubis%20-%20Authoring%20Realtime%20Volumetric%20Cloudscapes%20with%20the%20Decima%20Engine%20-%20Final%20.pdf),
  [Nubis Evolved 2022](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-NubisEvolved-NoVideos.pdf)
- Hillaire: [Sky, atmosphere and clouds in Frostbite, 2016](https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016_pbs_frostbite_sky_clouds.pdf);
  "A Scalable and Production Ready Sky and Atmosphere Rendering Technique",
  EGSR 2020; "Physically Based and Unified Volumetric Rendering in
  Frostbite", SIGGRAPH 2015
- Wenzel: [Real-time Atmospheric Effects in Games, 2006](https://advances.realtimerendering.com/s2006/Chapter6-Real-time%20Atmospheric%20Effects%20in%20Games.pdf)
- Sousa: [Crysis Next Gen Effects, GDC 2008](https://www.slideshare.net/slideshow/crysis-nextgen-effects-gdc-2008/25051981)
- Mitchell: "Volumetric Light Scattering as a Post-Process", GPU Gems 3,
  chapter 13
