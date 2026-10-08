# Task 24 - HDR, Tone Mapping and Bloom

## Objective

Light brighter than white on the QRhi renderer: lamps (task 21), glowing
surfaces and glints keep their range instead of clipping, and emitters such
as signal and train lights glow. The plumbing is done; the look (curve,
exposure, bloom strength, exposure over the day) waits for tuning by eye.
Defaults leave the image exactly as before.

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
- Readbacks of a float view (`readColor`) convert to 8-bit; the
  transmission copy takes the view's format.

## Settings (Rendering, Image)

- `core.rendering.toneMapping`: Off (default), Soft shoulder, ACES, AgX.
- `core.rendering.exposure`: stops, -4 to 4 (0).
- `core.rendering.bloom`: strength, 0 (off, default) to 4.

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

## Later

- Tuning by eye: default curve, exposure, bloom strength.
- Exposure following time of day (replacing the lamps' daylight scale of
  task 21 with a real exposure) and eye adaptation.
- MSTS light sources as emitters (signal lamps, train lights), so they
  glow too.
