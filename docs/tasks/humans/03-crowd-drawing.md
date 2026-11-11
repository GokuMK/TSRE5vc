# Task 03 - Crowds drawn at scale

Status: design, 2026-11-11.

## Goal
500 animated humans drawn at once, made from a few human files, with a
quality setting. The user, 2026-11-11: 2 ms for 500 people would cost
quality, and a quality setting is expected. No work on the Steam Deck.

## Tiers
Proposal. Thresholds use the screen coverage TSRE already uses for glTF
levels (`MSFT_screencoverage`, task shapes 10). The distances are for a
1.75 m person at a 60° vertical field of view on a 1080-line screen.

| Tier | On screen | Mesh | Animation |
|---|---|---|---|
| Near | over ~60 px (closer than ~25-30 m) | level 0 | its own pose, every frame |
| Middle | ~30-60 px | levels 1-2 | a shared pose |
| Far | under ~30 px (beyond ~50-60 m) | impostor | a frame number only |

Most of a big station's 500 people are in the middle and far tiers. The
quality setting moves these thresholds and caps how many people get the
near tier.

## Shared poses
Middle-tier people playing the same clip do not each sample it. Each clip
has a bank of K phases (for example 16 for a one-second walk loop), sampled
once a frame. Each person uses the phase nearest its own time, with a fixed
random offset per person.

The cost is then clips × K, whatever the number of people. Near people
sample their own poses, because they crossfade, turn and stop.

Option, measured before choosing: lower mesh levels weighted to fewer
joints (no fingers, the spine merged). The generator moves those weights to
the parent joints. Sampling stays complete.

## Impostors
- **Proposal: baked by TSRE when a human file loads.** The roadmap's
  estimate: a few milliseconds of GPU per human file, a BC1/BC3 encode with
  `TerrainMaterialMap::encodeBC1` afterwards, one human file per frame; the
  same memory as impostors made beforehand. It works for every route's
  humans and needs no files. Made beforehand (images inside the `.glb`)
  only if step 3 measures a real problem.
- The human is rendered from 8 or 16 directions around it: one frame of
  the idle clip and 8 frames of the walk loop. With 8 directions and
  64×128 px cells that is 72 cells, about 576×1024 px per map. Two maps:
  colour with alpha, and normals with the recolour mask. This uses the same
  render-to-texture as the environment map and the displays.
- Drawn as instanced quads facing the camera. The cell comes from the
  viewing angle, the frame from the person's phase, the colours from the
  mask and the person's tints. Normals keep sun and time-of-day lighting.
- Trees' impostors (`Impostor.h`) are read from files and are still. Their
  choice of frame by direction, and their shader parts, are reused where
  they fit.
- Switching tiers with a dithered fade. TSRE has no level crossfades yet.

## Variation
- **Tint**: 4 regions (top, bottom, shoes, hair) recoloured per person from
  palettes, chosen at random with a seed. 4 colours per instance, packed.
  The palettes come from the route's `humans.dat` later.
- **Height**: ±6% uniform scale, with the walk played faster or slower to
  match, so feet don't slide.
- **Human files**: 8 to 16 to start, from the generator (task 01).
- **Per person**: the choice of idle variant and the phase offsets.

## Shadows
Near people cast moving shadows (`DrawInstance::dynamicShadow`, as moving
trains do). The middle tier casts maybe, from its lowest mesh level. Far
people cast none. Measured; part of the quality setting.

## Settings
Player settings, following the settings rules (read once, updated on
`runtimeSettingsChanged`, never read each frame):

- **human quality**: tier thresholds, the near tier's cap, impostor
  directions and cell size, shadows. Lower quality draws more people as
  impostors; it never draws fewer people (the user, 2026-11-11). A density
  factor is a last resort only, not planned;
- **people**: Always, Route defined, Never (roadmap, "Where humans live";
  used from task 05).

Developer switches (forcing a tier, logging timings) are `TSRE_*`
environment variables.

## Test scene
A crowd mode in the Shape Viewer, or a test executable using the capture
harness:

- N humans on a grid, some idle and some walking in circles,
- camera places for the near, middle and far tiers,
- captures, with times printed: animation CPU ms, gather ms, impostor bake
  ms, and GPU ms where the renderer gives them.

CPU times are measured on this machine in Release builds. GPU times only
mean something on the user's hardware, when the user runs it; llvmpipe here
checks correctness.

## Steps
1. Many instances: the test scene, a state per person, palettes batched,
   instanced draws, CPU timing.
2. Shared pose banks and the update tiers.
3. Impostor baking and drawing, tier switching. Bake time and memory
   measured; the run-time or beforehand choice confirmed.
4. Variation: tint masks, height, clip choice.
5. The quality setting; tiers tuned with the user.

## Acceptance
- 500 humans in the test scene at every quality level, both renderers.
- CPU times recorded here; the user judges the look and, on own hardware,
  the frame time.
- Tier changes not distracting (the user's eye).

## Decisions
- Impostor directions (8 or 16) follow the quality setting (the user,
  2026-11-12). 16 doubles the atlas, against visible jumps when walking
  past people.
