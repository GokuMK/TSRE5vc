# Task 39 - Temporal Anti-Aliasing and Upscaling

Status: draft (2026-10-29), after [task 38](38-motion-blur.md), whose
velocity buffer and camera history it builds on.

## Goal

Anti-aliasing users ask for (thin catenary wires, rails, alpha-tested
foliage and fences, shadow edges and specular shimmer), as TAA first; then
the same inputs for upscalers: FSR 2/3 (open source, Vulkan), DLSS (NVIDIA,
through its SDK), XeSS. QRhi only.

## What task 38 already gives

- The velocity buffer (`color4`, RGBA16F): each pixel's whole motion
  (x, y) and motion blur's (z, w, from the reference view), without
  jitter, in half normalized device units; written by the draws that
  write depth (translucent ones and decals keep what is behind); the
  distant band and the sky not (static world: from depth, as motion
  blur takes them). Written by moving instances only (x = 64 where
  none was drawn): the static world's whole motion from depth and last
  frame's view, as large engines do.
- One camera history (the real previous view-projection), with tile
  crossings shifted.
- Previous transforms of moving objects and their animated parts.

## Design notes

- **Jitter**: a sub-pixel offset per frame on the projection used for
  drawing only (Halton 2, 3; 8-16 phases); not on the velocity, the
  shadow maps, the selection pass, the environment cube or the fog
  volume's own jitter.
- **Resolve**: the history (RGBA16F) reprojected by the velocity, taken
  from the nearest depth of the 3 x 3 neighbourhood; the neighbourhood's
  colour clip in YCoCg against ghosting; a little sharpening; history
  dropped where a pixel was uncovered.
- **Order**: scene (jittered) -> AO, clouds, fog, rain composites -> TAA
  resolve -> motion blur (task 38) -> bloom, exposure, light shafts, tone
  mapping -> overlays and HUD.
- **Upscalers**: draw at a lower resolution, jittered; the upscaler
  replaces the resolve and outputs at the display's; everything after it
  at the display's. They take colour, depth, the velocity, the jitter,
  the exposure and a reactive mask (blended parts, particles, rain, glass).
  QRhi gives native Vulkan images (`QRhiTexture::nativeTexture()`) and
  lets native commands into its command buffer
  (`QRhiCommandBuffer::beginExternal()`), so an SDK can run inside the
  frame.
- **To check against TAA**: the effects with their own temporal history
  (fog volume, clouds, light shafts) and per-pixel noise (shadow filter,
  fog jitter, ambient occlusion); thin lines drawn as lines; the editor's
  overlays and picking (drawn after, not jittered).
