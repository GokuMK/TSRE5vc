# Task 24 - HDR, Tone Mapping and Bloom

## Objective

Light brighter than white on the QRhi renderer: lamps (task 21), glowing
surfaces and glints keep their range instead of clipping, and emitters such
as signal and train lights glow. The plumbing is done; the look (curve,
exposure, bloom strength, exposure over the day) waits for tuning by eye.
Defaults: no tone curve and exposure 0, as before; bloom strength 4 (user's
choice, 2026-10-08), which changes the image only where emissive surfaces
glow.

## Design

- With a tone curve chosen the view draws into RGBA16F; without one it
  stays RGBA8 (cheaper on weak GPUs). The lit shaders already produce values
  above 1 where lamps or emission add light; under QRhi they clamp alpha to
  [0, 1] and colour to >= 0, as 8-bit targets did implicitly (terrain writes
  alpha above 1, which a float target would blend into black).
- The legacy MSTS shading stays in display colour, so ordinary routes keep
  their look; PBR already shades in linear light and only stops clipping.
- `present()` takes the view's display colour to linear light, adds bloom,
  applies the exposure (stops) and maps the result through the curve:
  - Off: the view as before (pixel-identical with exposure 0 and no
    bloom).
  - Soft shoulder: display colour unchanged up to 0.85, then rolled off
    towards white with the hue kept. The usual look with highlights
    compressed (the brightest sky greys slightly).
  - ACES (Narkowicz fit) and AgX (minimal fit): filmic curves that also
    change mid-tones and saturation.
- Bloom is made only from emitted light: the lit shaders write their
  emission (PBR emissive, faded by fog) as a third colour output (glow,
  RGBA16F), and the bloom chain reads only that. Sunlit walls, snow and
  glints never glow. The chain (Jimenez 2014): up to six levels from half
  resolution, 13-tap downsamples, then 3 x 3 tent upsamples added back level
  by level; `present()` adds the sum, averaged over the levels, times the
  strength. The glow output is cleared to black in a pass of its own, as a
  pass clears every colour output with one colour (the background would
  glow).
- Glow splats (`RhiImage.cpp`): a lens a pixel or two across, or a flat lens
  seen nearly edge-on (a sliver), is drawn in one frame and missed in the
  next; without anti-aliasing its glow blinked, and bloom made each blink a
  halo. Glow of emissive surfaces therefore comes from their emitters (task
  21) until they are large on screen: each emitter adds its glow at its
  projected position, spread by a tent R pixels wide each way, R its
  projected radius rounded to whole pixels (1 to 16), whose weights sum to
  the same wherever it falls. The total is the emitter seen as a sphere of
  its area and radiance, `pi I (f/d)^2` pixels of glow, faded by fog, at its
  own power (no exposure, gain or daylight scale, as the surface's glow).
  While the emitter's radius grows from 16 to 32 pixels the splat hands over
  to the surface's own glow (`PbrShading.glsl`). The splats are one
  instanced draw in the main view's pass after the scene, depth-tested,
  adding to the glow output only; the emitters are gathered once a frame
  with the lights (`gatherLights`, emitters only, also those too dim to
  light anything).
- Glow follows the lamps' light (task 21): the lit shaders and the splats
  scale it by the daylight scale (3 % in full daylight with time of day on,
  full with it off, the editing mode) and drop it with local lights turned
  off. The lenses' emissive colour stays, as a material.
- Readbacks of a float view (`readColor`) convert to 8-bit; the
  transmission copy takes the view's format.

## Settings (Rendering, Image)

- `core.rendering.toneMapping`: Off (default), Soft shoulder, ACES, AgX.
- `core.rendering.exposure`: stops, -4 to 4 (0).
- `core.rendering.bloom`: strength, 0 (off) to 4 (default since 2026-10-08).

## Verification

- `rhi-shaders`: present and bloom passes bake for SPIR-V and GLSL 330.
- Defaults: EUROPE1 pixel-identical to the renderer before this task.
- Curves on EUROPE1 by day against Off: Soft shoulder RMSE 7.6 (sky top),
  ACES 23, AgX 27.
- Shape Viewer: EmissiveStrengthTest and CompareEmissiveStrength glow in
  proportion to their emission; LightsPunctualLamp's bright brass does not
  glow.
- EUROPE1 street lamps on 2026-12-21 at 18:00 with Soft shoulder, bloom 1.5
  and AO High: lamp lenses glow as points of light; QRhi on OpenGL matches
  Vulkan (RMSE 2.5); no Vulkan validation messages.
- Occlusion (2026-10-08, bbb at night behind a building): surfaces without
  emission write their glow with their colour's alpha, so blended ones (many
  MSTS shapes) cover the glow behind them; with alpha 0 the lamps behind a
  building glowed through its wall. Halos of lamps beside an edge still
  spread over it, as bloom does.
- Shimmer (2026-10-08, bbb lamp forest, bloom 1.5, Soft shoulder): 13
  captures stepping the camera 0.1 m, bloom energy change between steps
  (mean, max). A Karis average in the first downsample changed nothing (the
  blinking pixels are not brighter than the rest), so it was left out.

  | Region | Before | Point splats (hand-over 1-3 px) | Tent splats |
  |---|---|---|---|
  | Whole lamp band | 3.6 %, 9.6 % | 2.3 %, 5.9 % | 1.5 %, 3.1 % |
  | Distant lamps | 6.1 %, 14.0 % | 2.2 %, 4.1 % | 2.3 %, 4.9 % |
  | Mid-distance, left | 9.0 %, 25.7 % | 5.9 %, 20.3 % | 3.3 %, 6.6 % |
  | Mid-distance, right | 5.1 %, 12.2 % | 3.2 %, 9.5 % | 2.5 %, 6.5 % |

  The rest is partly real motion (halos crossing the regions' edges). The
  splats glow more than the slivers they replace: the band's bloom is about
  1.6 times what it was. Behind a building they stay hidden. Gathering the
  emitters costs about as much as gathering the lights (0.23 ms for 924
  lights).

## Later

- Tuning by eye: default curve, exposure, bloom strength, and the glow
  splats' brightness against the lenses' own glow.
- Exposure following time of day (replacing the lamps' daylight scale of
  task 21 with a real exposure) and eye adaptation.
- MSTS light sources as emitters (signal lamps, train lights), so they
  glow too.
