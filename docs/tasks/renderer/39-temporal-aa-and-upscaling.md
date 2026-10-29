# Task 39 - Temporal Anti-Aliasing and Upscaling

Status: first version on `feature/taa` (2026-10-29): steps 1 to 4 below;
tuning with the user, the mip bias and costs open (see Open). Builds on
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
- **The velocity stays unjittered** without a change: this frame's and
  last frame's positions are both projected with this frame's jittered
  projection (`uPrevPMatrix`, `uRefPMatrix` from `gluu->pMatrix`), so the
  jitter cancels. The static world's motion from depth: the unjittered
  view-projection (`ViewMotion::viewProjection`), half a pixel off at most.
- **Done**: Halton (2, 3), 8 phases, the distant and scene bands only
  (the sky band smooth); only in frames whose view gave its motion
  (`viewMotionTaken`: the 3D view's paint, not the map view or the
  selection pass).
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
4. **Blended**: a tenth of this frame still, more the faster the pixel
   moves (0.4 by 20 pixels a frame: each frame's history is resampled where
   the surface was, which blurred the ground along the motion; USA1 cab at
   192 km/h); in a squashed range (`c / (1 + max(c))`, reversible: lamps
   and the sun's glints do not flicker or leave trails, Karis).
5. **The same static surface kept whole, the camera standing still** (a
   depth history, R16F, two alternating: the 3 x 3's nearest distance,
   negative for a moving instance's): standing still, a static pixel's
   history is not clipped where the stored distance is the same or nearer
   (a wire thinner than a pixel this frame's jitter missed in the whole
   3 x 3: clipping removed Prudnik's distant catenary, the user,
   2026-10-29: "disappears sooner than could"; now continuous, as dark as
   its share of the pixel). Moving, the clip as without it. A passing
   train's place (negative) is clipped. `TSRE_TAA_DEBUG=2`: kept whole
   (green), clipped static (red), a moving instance's (blue), none
   (black); `TSRE_TAA_DEBUG=3`: this frame as drawn, unblended.
   - **Moving, why not** (the user chose this, "F", 2026-10-29): kept
     whole while moving too, wires over dark trees showed thicker and with
     copies of them 2-3 rows off (Prudnik,
     `core.startup.camera=-5417,14865,-869.37,171.00,338.43,-138.18,-9.33`,
     the D key); off while moving, plain TAA there.
   - **The alternative ("G", commit a882dccd, to come back to if wires
     from a moving cab or a moving camera should stay visible)**: kept
     whole moving too. The stored distance nearer than this frame's: that
     nearer static surface's history fetched where it was by its own
     motion at its own distance (not the pixel's: from a cab at 192 km/h
     the sky barely moves while a gantry passing in front of it moves far),
     when the pixel's own distance kept there (a second channel, RGBA16F)
     is that surface's (the 3 x 3's there marked a band a pixel wider and
     thickened wires) and it moves up to 12 pixels a frame (faster, specks
     of passing gantries stayed in the sky). Its cost: the thicker wires
     and copies above, while moving.
   - The flare that stays (both): standing, TAA averages a wire thinner
     than a pixel down to its share of it, nearly gone over dark trees;
     moving, more of each aliased frame shows, so it comes back towards
     TAA off's strength, and fades within a few frames of stopping
     (measured on the D key: moving edges 5-37 % stronger than standing,
     by variant and view). The proper fix, as games draw power lines:
     wires drawn at least a pixel wide, faded by their share of it, the
     same with TAA or without (TSRE first knowing which shapes are wires):
     a task of its own.
6. **Off the screen, after a jump, the first frame**: this frame alone.
7. The result (squashed) into the history for the next frame; the
   resolve reads the view itself, no copy.
8. **Sharpening** (`RhiTemporal.cpp`, a second pass): the history
   sharpened into the view by the contrast around each pixel (AMD's CAS,
   strength 0.4; `TSRE_TAA_SHARPEN` 0-1 for tuning, 0 none), clamped to
   the range of the pixel and its four neighbours (the plain CAS left a
   halo brighter than the sky beside a thin wire and the wire darker than
   it is, and made moving wires flare far more: Prudnik, 2026-10-29); the
   history stays unsharpened. It brought back most of the textures' detail TAA took
   (USA1's trees and grass). The textures' mip bias (about -0.5, the
   scene's samplers or `texture(..., bias)`: QRhi samplers have no LOD
   bias) not done: to see whether it is wanted.
9. `TSRE_TAA_DEBUG=1`: the motion (red x, green y, 8 px a frame full).

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
  pointer; `PASS_OVERLAY`) are **drawn after the resolve, unjittered**,
  as now. Through TAA a 1-pixel line jittered by up to half a pixel would
  be averaged into a wider, fainter one, and the moving pointer would
  trail. Wide lines (`core.rendering.defaultLineWidth`, items' own widths)
  are drawn where the backend has them (`QRhi::WideLines`): Vulkan, TSRE's
  QRhi backend, on the Steam Deck's AMD driver yes (the startup line says
  so since 2026-10-29); Direct3D and Metal not, unused by TSRE. With TAA on, the overlay pass waits until
  after the transmission pass and the resolve, with this frame's
  unjittered projection; depth-tested against the jittered depth (half a
  pixel at occlusion edges). Overlays drawn under the water surface then
  draw after it, depth-tested as before.
- **Picking and the depth probe** read the jittered depth: half a pixel.
- **Captures**: a still view converges over the jitter's phases (the
  harness's settling frames, at least 8); moving captures (task 38's
  `motionSeconds`) show TAA's own trails.
- **Costs** (Steam Deck, 1280 x 720, moving frames, two runs each; TAA
  on against off, with the velocity it shares with motion blur): USA1 cab
  50.2 / 58.5 ms (minimum / median) to 54.6 / 61.0, back view 49.8 /
  54.5 to 53.7 / 58.4, CMK 3D cab 71.3 / 77.6 to 77.1 / 83.2: 3 to 6 ms
  on the Steam Deck; the processor's time within the runs' noise (0 to
  1 ms).

### Settings

- `core.rendering.antiAliasing`: Off, TAA. Off by default and in every
  current preset (OpenRails Basic, Extended, Advanced Environment; the
  user, 2026-10-29).
- Sharpening strength (0 to 1).

## Upscaling (a later stage or task)

Not decided (the user, 2026-10-29): first a review of what FSR needs.
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

## First results (USA1, 2026-10-29)

- Still: the catenary, distant gantries and rails continuous lines
  instead of dots; trees' and fences' edges smooth; with the sharpening,
  textures nearly as sharp as without TAA.
- Moving: a passing train at 192 km/h sharp, no trails; the cab's track
  bed at 192 km/h softer than without TAA where it moves fastest (the
  harness steps 0.1 s a frame: six times a 60 fps frame's motion), at 23
  km/h clean.
- The editor's grid and track lines drawn after the resolve: full
  contrast, aliased as before (culled to the scene band: unculled, every
  overlay round the camera was drawn, 168 draws for 21).
- Sharpening (USA1's cab, 2026-10-29): 0.8 oversharpened (the user); 0.4
  and 0.6 to compare after the depth history.
- Fresh system (2026-10-29, two runs each): frame and processor times,
  off against TAA: USA1 cab 47.5 / 11.8 ms against 49.7 / 13.1-14.3, CMK
  3D cab 66.4 / 11.9 against 70.4 / 12.6-14.6. Motion blur alone adds
  about 0.7 ms of the processor's (the shared motion tracking); TAA's own
  1-2 ms more not yet found (no draw counts differ).

## Steps

1. The jitter (main view's bands only), unjittered velocity, the setting.
2. The resolve: reprojection, history, clip, blend; jumps and the first
   frame; debug views (`TSRE_TAA_DEBUG`: motion, clip, history weight).
3. Sharpening and the mip bias.
4. The overlays after the resolve, unjittered.
5. Captures and tuning with the user.
6. Docs, Polish, roadmap.
7. Later: the reactive mask; ambient occlusion's noise turned per frame;
   upscaling (after reviewing FSR).

## Open

- Tuning with the user: the sharpening's strength (0.4 or 0.6; a
  setting?), the mip bias. Softness at speed accepted ("simple motion
  blur", the user).
- TAA's own 1-2 ms of the processor's: to find (each part switched off in
  turn).
- Costs on the Steam Deck (3 to 6 ms): the resolve's 3 x 3 depth and
  colour, the velocity target shared with motion blur.
- Checks: Prudnik's catenary and stations, CMK's forest, clouds and the
  fog volume under TAA, water, night lamps.
- Later: the reactive mask for blended parts; ambient occlusion's noise
  turned per frame; upscaling.

## Decided (the user, 2026-10-29)

- TAA off by default and in all current presets.
- The editor's overlays after the resolve, unjittered.
- Upscaling: a later stage or its own task, after reviewing what FSR
  needs.
