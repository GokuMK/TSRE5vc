# Renderer Task Roadmap

This folder contains ordered tasks for migrating the TSRE renderer from legacy immediate drawing to gather-then-render.

## Execution Order
- [x] `01-runtime-pipeline-switch.md` (superseded: the switch was removed with the legacy pipeline)
- [x] `02-renderer-core-generic-queue.md`
- [x] `03-selection-and-picking-parity.md`
- [x] `04-terrain-highres-gather.md`
- [x] `05-terrain-distant-water-sky.md` (gather draws sky, distant terrain and water through the renderer)
- [x] `06-world-objects-shape-based.md`
- [x] `07-world-objects-procedural-and-helpers.md`
- [x] `08-overlays-tdb-activity-markers.md`
- [x] `09-hud-compass-pointer.md` (gather draws pointer, compass and HUD through the renderer)
- [x] `10-shadows-gather-pass.md` (gather draws both shadow maps from the gathered queue; three-map review in the task; the middle map's own size setting since 2026-10-29)
- [ ] `11-shader-pass-buckets-and-custom-shaders.md` (pass buckets implemented; per-pass shaders pending)
- [x] `13-selection-renderer-and-id-redesign.md` (32-bit selection IDs in an integer target; picking compared between renderers by the capture harness)
- [x] `15-shape-viewer-gather.md` (Shape Viewer and Consist Editor draw through the renderer)
- [x] `14-windows-hardware-validation.md` (manual Windows checks passed; further hardware runs only when they block work)
- [x] `12-parity-automation-and-performance-gate.md` (harness compares a capture with a baseline capture)
- [x] `16-renderer-owned-meshes.md` (every producer draws renderer-owned meshes; textures and framebuffers are a later step)
- [x] `17-environment-map.md` (cube map around the camera for reflections; measured on hardware; glTF PBR and water read it, MSTS shapes pending; QRhi's cubes in half floats since 2026-10-28, the shade's hue steady at night)
- [ ] `18-shaded-water.md` (one shaded water surface with waves and planar reflection; next: realistic water, Crysis target: FFT waves, refraction and soft shores, foam, river flow)
- [ ] `19-backend-boundary.md` (OpenGL calls left outside the renderer; state, views, measurement, readback, targets, programs, selection and reflection storage moved behind `Renderer`; texture uploads and the `GLUU` matrices and lights remain)
- [ ] `20-qrhi-renderer.md` (QRhi renderer on `feature/qrhi`: parity with the OpenGL renderer reached on lavapipe and on the Steam Deck; open: hardware sweep of all capture sets, water visibility without queries, QRhi OpenGL CPU cost (automatic API is Vulkan only until then), Direct3D 11 for development only)
- [ ] `21-local-lights.md` (glTF punctual lights and emissive surfaces light the scene on the QRhi renderer through a world-space light grid)
- [ ] `22-time-of-day.md` (sun position from the camera's latitude and longitude at a set time and date; first step of the environment task; the sun in the colour of its light through the air since 2026-10-28)
- [ ] `23-ambient-occlusion.md` (GTAO on the QRhi renderer from the view's depth, taken off the ambient light share the lit shaders write; Off/Low/Medium/High; since 2026-10-23 a cheaper unrolled loop and High at full resolution with 3 x 6 taps (2123x1948: +39 to +22-25 ms), blended MSTS parts in the depth it reads, the previews with a studio light so it shows; the steps' own noise, no echo rings of thin parts in a 3D cab (2026-10-23); open: a smaller radius near the camera, Medium's parts at large views)
- [ ] `24-hdr-and-bloom.md` (float view with tone curves and exposure; bloom only from emitted light; defaults unchanged, look to be tuned; warmth, the view's white balance, 0.2 by default since 2026-10-28)
- [x] `25-qrhi-hardware-investigation.md` (Steam Deck under Windows: paged terrain, frame pacing and memory fixed; per-draw recording cheaper (2026-10-23: frame texture slots once per batch, item uniforms by index; TRAINS CPU 13.3-15.2 to 11.8 ms, Prudnik 33 to 30.5 ms); open performance work in its handover, the uniform block copied whole per draw)
- [x] `26-signal-lights.md` (signal lights in the default aspect from sigcfg.dat; glow on QRhi, plain discs on OpenGL; flashing and animation later)
- [ ] `27-sky-enclosure-and-tunnel-lighting.md` (a top-down enclosure map: lamps at full strength and ambient with a floor where the sky is hidden; done with task 28, in `main` since 2026-10-14, off by default and on in the Advanced Environment graphics preset; found a QRhi Vulkan glow loss after depth-only passes; 2026-10-29: the floor is the ground for what stands on it, objects buried under the terrain no longer darken roofs' edges; the openness to be replaced by voxel lighting later)
- [ ] `28-volumetric-clouds.md` (physical sky, raymarched low and mid clouds, flat high layer, cloud shadows, overcast light and light shafts on QRhi; weather from `docs/tasks/environment/01-weather-data.md`; includes task 27. Steps 1-3 and 5-8 in `main` since 2026-10-14, off by default and on in the Advanced Environment graphics preset; timed on the Steam Deck; since 2026-10-16 (`feature/storms`): storms dark by their density, towers and anvils, rain under the clouds and near the view, falling drops (none under cover), blue noise for the march, light shafts with rays; open: cloud shadows past 7 km, shaft fine tuning, wet ground, step 9 (OpenGL fallback) not started)
- [ ] `29-auto-exposure.md` (lamps at one strength all day, the view's exposure adapting to its measured brightness, night views as Dimmed; setting "Lamps by day", Dimmed the default; in `main` since 2026-10-14; captures report it per view, and the night's ambient no longer cut by a floor in the shaders, since 2026-10-28)
- [ ] `30-cleanup-fine-tuning-and-fixes.md` (open list of small renderer items: cleanup such as settings read every frame, fine-tuning, bug fixes noted in other tasks; anisotropic filtering on QRhi Vulkan and shape mipmaps on by default since 2026-10-29; open: the near shadow map's stepped edge at 2048)
- [ ] `31-moonlight.md` (the moon lights the night by its phase and height and casts shadows; in every mode)
- [ ] `32-aerial-haze.md` (distant terrain through the air: per colour, the horizon's light by direction, the weather's haze; dark blue far mountains with any time and weather)
- [ ] `33-mist-and-ground-fog.md` (fog from the weather and the terrain: radiation and valley fog on calm clear mornings, pooled by a floor map around the camera; a froxel volume near the camera, High's per-pixel fog over it, lamps and signals lighting it, patches drifting with the wind, the terrain's shade of a low sun; pooling and a Ground mist preset; wrong terrain tiles left out of the floor map; merged from `feature/mist` with `feature/storms`; with `feature/train-lights`: the signals' and lamps' light out of the fog's history and every frame, lamps shining their own way, beams integrated over the slices, the sky's fog in linear light; headlight beams in it without a dark patch ahead or steps by the lamps (2026-10-23: lights ranked by their light over a grid cell, cones widened by the fog cell's size); open: step 6 (panel control, docs, timing), rings seen from above at dawn; 2026-10-29: none under cover, from the sky enclosure)
- [ ] `34-train-lights.md` (lamps and headlight beams of rolling stock from the ENG/WAG `Lights` block, lit by the train's state as Open Rails does; emissive lenses with glow splats facing their way, beams as spot lights from the car's end lighting the track and the fog; steps 1 to 3 merged from `feature/train-lights` (with `feature/simulation`); open: step 4 (flashing and fading), the beams' strength and the lamps' size, the content's one combined cone, since 2026-10-23 the first lit cone only (Open Rails' rule), shone out of each lit head lamp (LED dots less than 15 cm apart one lamp); each lamp's own aim needs data)
- [ ] `35-night-textures.md` (MSTS night textures over the lit day texture: the brighter of the two as the daylight fades, at the lamps' scale (less the exposure's bias), by night as authored, lit windows at dusk; loaded below the sun at 20 degrees; a glow where they are painted brighter than the day ones (QRhi); both shape backends, QRhi and OpenGL; merged from `feature/night-textures`; later: night sub-objects, under cover by day, fake light cones, editing .sd values)
- [ ] `37-vegetation-shadows.md` (the TREES forest measured: shadow maps of every tree all round the camera at the view's level, triangles of a pixel shaded in 2x2 blocks, the glTF texture sync on every push; merged from `feature/forest-performance` (2026-10-23); part 1 (TREES at 2123x1948: GPU 197 to 159 ms, cpu 24 to 5 ms): the sync once a frame, vegetation (impostor, MSTS light materials -9 and -10, forests) outside the sky enclosure, trees casting the level of 3 times their distance down to the last mesh before the impostor; part 2: shadow maps snapped to their texels and cached static casters with moving objects (trains, animated, edited for a second) drawn on top, redrawn when the static objects drawn change (QRhi; TREES 2123x1948: GPU 159 to 148 ms; since 2026-10-23 static consists stay in the cached maps, TRAINS' frame 91 to 58 ms), and a vegetation map set read by trees at half strength (crowns and crossed-quad trees without their hard own shade); since 2026-10-26 caches redrawn for loaded content only when they skipped a caster still coming, in turn, at most every 500 ms (frames making a texture 19.6 to 2.5 ms of shadows); open: the depth gap, impostor shadows facing the sun)
- [ ] `38-motion-blur.md` (the blur of fast motion on the QRhi renderer, physical, Off/Light/Normal/Strong/Very strong (1/250 to 1/30 s), off by default and in the OpenRails presets, Normal 1/120 s in Advanced Environment, and a quality (Normal/High: the longest streak 3/8 % of the width, 12/32 samples; Very high 15 %/64 tried and dropped): riding a train the world streaks by its motion while the cab, the train and the head's turning stay sharp (exact per vertex, from last frame's position through the reference view); a free camera never blurs the still world; trains and road cars streak by their own motion. A velocity target (RGBA16F: the whole motion for task 39 and the blur's) drawn by moving instances only, the static world's motion from depth; moving instances' last-frame matrices in a table kept from frame to frame; McGuire/Jimenez tile max and gather on colour and glow with blue noise and bilinear samples, before the UI; off costs nothing (as main's build); Steam Deck: USA1 cab +3 ms, CMK 3D cab +3-3.5 ms. `CameraFree::getMatrix` argument order fixed. Merged from `feature/motion-blur` (2026-10-29); open: head-turn blur as an option, tuning, TAA (task 39))
- [ ] `39-temporal-aa-and-upscaling.md` (TAA on the QRhi renderer, `core.rendering.antiAliasing` Off/TAA, off by default and in every preset: the main view jittered (Halton 2, 3, 8 phases), a resolve on task 38's velocity and the static world's motion from depth, the history clipped to the 3 x 3's colours in YCoCg, thin static features kept whole while the camera stands still (Prudnik's distant catenary), a clamped CAS sharpening (0.4), surface textures half a mip level sharper; the editor's overlays drawn after it unjittered. On `feature/taa` (2026-10-30); open: thin wires flare when the camera moves (TAA's limit for wires under a pixel; the fix, wires drawn a pixel wide, a task of its own; the variant keeping them moving noted), TAA's own 1-2 ms of the processor's, the reactive mask; upscaling (FSR) a later stage after a review)
- [ ] `40-cpu-frame-cost.md` (the processor's time a frame, measured at Prudnik's freight station (12,000 draws, 45 ms of the Deck's processor): part 1 on `feature/cpu-sort` (2026-10-30): the queue's lists stay in submission order and their draw order is made once a frame for all views (a slot per packet by first submission, a sort of the distinct packets only; the order as before, images equal pixel for pixel), each instance's bounding sphere at submission, no matrix copies on QRhi: sort 9.7 to 0.7 ms, culling 4.5 to 2.4, the frame 45 to 37 ms (timed); part 2 on `feature/material-table` (2026-10-30): measured, the uniforms were not the cost (5 of 6 draws share a block); recording cheaper (a resource-set key per run of draws, the frame's texture slots copied whole, unchanged uniform blocks taken again, PBR values by index, meshes at whole vertices drawn with a base vertex): recording 21.8 to 19.3 ms, the processor's frame 33.5 to 31.3 ms untimed, still view equal; next: vertex pools by vertex size, a frame's packet state, a review of the world's gather, shared mesh buffers and instancing across packets)

New vegetation drafts (2026-10-18):
[36 - Optional diffuse foliage backlighting](36-foliage-backlighting.md), and
the generic bulk-instance milestone in
[terrain vegetation scattering](../terrain/terrain-vegetation-scattering.md).
Backlighting is implemented for glTF materials and tree impostors, merged from
`feature/gltf-lod` (crowns seen from below stay dark: the shadow map holds
leaves opaque); the scattering milestone is not. Backlighting does not block the
grass submission experiment.

Renderer work continues on Windows hardware; software rendering (llvmpipe,
lavapipe) no longer shows the remaining problems. Measurement and
RenderDoc scripts: `scripts/hardware/`.

## Legacy Pipeline Removed

The gather renderer is the only pipeline. `paintGL2`, the validation mode,
runtime switching, the `core.rendering.pipeline` and `pipelineHotSwap`
settings, and every object's immediate `render()` draw are removed. Profiles
that still store the pipeline setting drop it on load.

Objects draw by submitting persistent `RenderItem`s to the `RenderQueue`
passed to their `pushRenderItems(RenderQueue &queue, ...)` functions; see
`RenderQueue.h` for the contract. New drawing code implements only that path.

## Stage 4: Modern Renderer

Each step was checked against baseline captures on EUROPE1, BNSF_SCENIC and
PROCEDURAL (shadows off and on) and the Shape Viewer set.

1. Submission interface: producers receive a `RenderQueue &` in
   `pushRenderItems()`; `Game::currentRenderer` is gone. Pixel-identical.
2. Packet rework: `RenderItem` holds a mesh, a material, a terrain block and
   bounds; no GL constants. glTF and `SFileComplex` classify surfaces.
   Pixel-identical.
3. Shader variants: terrain code behind `TSRE_TERRAIN`, `<name>Terrain`
   programs, shared includes. Pixel-identical.
4. Procedural terrain in one pass from per-tile texture arrays. Filtering
   differences only.
5. Renderer-owned culling against the scene and shadow views; bounds for
   `SFileLegacy` parts and `OglObj`. Pixel-identical; 6-80% fewer scene draws.
6. Instancing of repeated packets, with `SFileLegacy` packets shared between
   object states. A further 19-82% fewer scene draws; isolated foliage pixels
   differ through draw order.
7. Three shadow maps, one sampled per fragment. Sharper shadows near the
   camera.
8. Deferred renderer: not started; decide after reviewing steps 1-7 against
   the checklist below.

Deferred-readiness checklist for new renderer work:

- Materials describe what a G-buffer needs (surface, lighting, textures), not
  shader uniforms.
- Forward-only work (blended surfaces, lines, overlays, UI, pointer) is an
  explicit class, not implied by pass order.
- Views are data (`setCullView`, light view-projections).
- Producers use no GL types; the test `MatrixProbe` implements `RenderQueue`
  and must keep compiling.

Optional after stage 4, never blocking: GPU timing in the harness
(`GL_TIME_ELAPSED`, median and p90), in-shader hash noise vs cached noise
texture for procedural terrain, and instancing and shadow-map cost on
hardware GPUs.

## Checking Renderer Changes

The parity harness compares two captures made with the same renderer:

```bash
scripts/renderer-parity.sh <game-root> <route> baseline              # before the change
scripts/renderer-parity.sh <game-root> <route> current baseline      # after the change
```

`renderer-capture` writes `<output>/<route>/<label>/` (`--test-label`,
default `current`); `renderer-compare` compares `--test-baseline` (default
`baseline`) with `--test-label`. `shape-viewer-capture` and
`shape-viewer-compare` take the same options.

On Windows, `scripts/hardware/Capture-Views.ps1` does the same. Take the
baseline and the current capture with the same profile: settings such as
time of day change the images (bbb: RMSE 12 to 22 between captures a day
and a profile change apart, 1.2 to 2.6 for a fresh pair).

## Ground Rules For All Tasks
- Prefer incremental, reversible changes.
- Capture a baseline before a renderer change and compare after it.
- Preserve editor selection behavior.
- Keep existing asset formats and object model.
