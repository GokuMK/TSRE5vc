# Task 39 - Temporal Anti-Aliasing and Upscaling

Status: design (2026-10-29), no code yet. Branch `feature/taa`. Builds on
[task 38](38-motion-blur.md) (merged): its velocity buffer, camera history
and moving instances' last-frame matrices.

Related: [task 28](28-volumetric-clouds.md) and
[task 33](33-mist-and-ground-fog.md) (their own temporal history),
[task 23](23-ambient-occlusion.md) (its per-pixel noise),
[task 24](24-hdr-and-bloom.md) (the HDR view, bloom, present).

## Goal

Anti-aliasing for the QRhi renderer, which has none: the multisample
setting (`core.rendering.antiAliasingSamples`) only asks OpenGL's window
for samples; the QRhi view draws into single-sample HDR textures. What
users see and ask about: thin catenary wires and rails breaking into dots,
alpha-tested foliage and fences crawling, shadow edges' steps, specular
shimmer, the editor's thin lines. TAA first, at the view's resolution;
then upscaling on the same inputs (below).

TAA is GPU work (a full-screen pass, two history textures): users with
desktop graphics cards have time there, their processors are the limit
(the user, 2026-10-29). The processor's share: a jitter a frame, one
pass recorded.

## What task 38 gives

- **The velocity target** (`color4`, RGBA16F): x, y the whole motion
  (half normalized device units), unjittered, written by draws of moving
  instances (trains, their 3D cabs, road cars, animated objects); x = 64
  where none was drawn: there the static world's motion from depth and
  last frame's view (`P W P^-1`, as the blur pass does with the
  reference).
- **The camera's history** (`Renderer::ViewMotion`): last frame's view in
  this frame's space, tile crossings shifted, jumps marked invalid.
- **Moving instances' last-frame matrices**, their animated parts
  included.

## Design

### Jitter

- A sub-pixel offset a frame, Halton (2, 3), 8 phases (16 to try), on
  every band of the **main view's** projection (sky, distant, scene) in
  `RhiRenderer::beginViewBand`: a clip-space translation (the
  projection's third column, x by `2 j_x / width`, y by `2 j_y / height`).
- Not on: secondary views (environment cube, water reflection), the
  shadow maps and the sky enclosure maps, the selection pass, the UI.
- **The velocity stays unjittered**: the vertex shader's current position
  less this frame's jitter (a uniform), last frame's position without
  last frame's. The static world's motion from depth: this frame's
  unjittered projection with the pixel's centre moved by the jitter.
- Effects reading the view's depth with this frame's projection (ambient
  occlusion, clouds, fog, rain, light shafts, motion blur) take the
  jittered one; half a pixel at most.

### Resolve

After the scene's full-screen composites (ambient occlusion, clouds, sky
fog), **before** rain (drops have no history: drawn after) and motion
blur, on the HDR view:

1. **Where the pixel was**: the motion of the nearest depth in its 3 x 3
   (edges keep their own motion, Karis 2014), from the velocity or, static,
   from depth.
2. **The history** there (RGBA16F, two textures alternating), sampled
   with a 5-tap Catmull-Rom filter (bilinear would blur a little more
   every frame).
3. **Clipped** to the 3 x 3 neighbourhood's colour box in YCoCg, by its
   mean and variance (Salvi 2016), towards the mean: what moved in or was
   uncovered does not ghost.
4. **Blended**: about a tenth of this frame, more where the pixel moves
   fast or the history was clipped hard; weighted by 1 / (1 + luma) (lamps
   and the sun's glints do not flicker or leave trails, Karis).
5. **Off the screen, after a jump, the first frame**: this frame alone.
6. The result into the view (as motion blur's gather: a copy and a pass)
   and into the history for the next frame.
7. **Sharpening**: light, after the resolve (contrast-adaptive, as AMD's
   CAS); the textures' mip bias about -0.5 with TAA on (the scene's
   samplers or `texture(..., bias)`: QRhi samplers have no LOD bias).

The glow (bloom's source) unresolved: bloom blurs it far more than the
jitter moves it.

### Where TAA needs care

- **Blended parts** (glass, smoke, MSTS translucent texels) write no
  velocity or depth: the history follows what is behind them; a window
  passing the camera may trail. First without; a reactive mask later (the
  blended draws marking where the history counts less), which the
  upscalers take too.
- **Clouds and fog volume** keep their own histories with their own
  jitter; under TAA a second accumulation: their noise sequences must not
  beat with the jitter's (8 phases).
- **Ambient occlusion**: its noise is the same each frame; under TAA it
  may turn each frame and be averaged (later, a quality gain).
- **Water**: the frame's copy it refracts is jittered as the frame; the
  reflection's view is not.
- **The editor's overlays** (track lines, markers, labels, the 3D
  pointer) are drawn in the scene's passes: with TAA thin lines smooth,
  the moving pointer may trail. Or drawn after the resolve, unjittered
  (aliased as now).
- **Picking and the depth probe** read the jittered depth: half a pixel.
- **Captures**: a still view converges over the jitter's phases (the
  harness's settling frames, at least 8); moving captures (task 38's
  `motionSeconds`) show TAA's own trails.
- **Steam Deck**: the resolve at 1280 x 800, estimated well under motion
  blur's cost (one pass, 9 + 5 taps).

### Settings

- `core.rendering.antiAliasing`: Off, TAA (later the upscalers' modes).
  Presets: to decide (below).
- Sharpening strength (0 to 1).

## Upscaling (after TAA)

The same inputs (jittered colour, depth, the velocity, the jitter, the
exposure, a reactive mask) at a lower resolution, the result at the
display's; everything after (motion blur, bloom, exposure, light shafts,
tone mapping, overlays, HUD) at the display's. For the Steam Deck and
4K screens: GPU time, not processor time.

- **Own temporal upsampling** (TAAU): the resolve above sampling a
  smaller frame into the display's: every QRhi backend (Vulkan, Direct3D,
  Metal, OpenGL), no SDK.
- **FSR 2/3** (AMD FidelityFX, MIT licence, HLSL compute): ported shaders
  or the SDK's Vulkan and Direct3D 12 backends inside the QRhi frame
  (`QRhiTexture::nativeTexture()`, `QRhiCommandBuffer::beginExternal()`).
  FSR 3's frame generation would show frames the processor never
  prepared: for processor-bound users the one upscaler feature that adds
  frames, but it needs the swap chain's pacing (hard under QRhi).
- **DLSS, XeSS**: NVIDIA's and Intel's SDKs, their licences against the
  project's to check first.

## Tests and captures

- **Thin lines**: catenary and rails (USA1's Northeast Corridor, Prudnik),
  a slow pan and a still view: no dots, no crawl. A reference: the same
  still view drawn at twice the size and halved.
- **Foliage and fences**: CMK's forest, alpha-tested.
- **Moving trains**: the side view and the cab at 192 km/h (USA1); trails
  behind the train, the cab interior (exact velocity).
- **Uncovering**: behind a passing train, round the cab's window frames
  when the head turns.
- **Jumps and tile crossings**: no smear on the frame after.
- **Costs**: the resolve on the Steam Deck and the user's screen; Off
  against main's build (nothing when off).
- Suites: rhi-shaders (the resolve's shaders), the capture parity sets
  with TAA off, settings, translations.

## Steps

1. The jitter (main view's bands only), unjittered velocity, the setting.
2. The resolve: reprojection, history, clip, blend; jumps and the first
   frame; debug views (`TSRE_TAA_DEBUG`: motion, clip, history weight).
3. Sharpening and the mip bias.
4. Captures and tuning with the user; the overlays' question.
5. Docs, Polish, roadmap.
6. Later: the reactive mask; upscaling (own TAAU, then FSR); ambient
   occlusion's noise turned per frame.

## Open questions for the user

- Presets: TAA off by default (a new profile is OpenRails Extended) and on
  in Advanced Environment only, or on in OpenRails Extended too
  (anti-aliasing is not an Advanced Environment look)?
- The editor's overlays: through TAA (smooth, the pointer may trail) or
  drawn after it (aliased as now)?
- Upscaling's direction: own TAAU first, or FSR 2/3 first?
