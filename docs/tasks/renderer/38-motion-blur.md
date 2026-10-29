# Task 38 - Motion Blur

Status: design (2026-10-29), no code yet. Branch `feature/motion-blur`.

Related: [task 39](39-temporal-aa-and-upscaling.md) (TAA and upscalers,
which share this task's velocity buffer), [task 24](24-hdr-and-bloom.md)
(the HDR view, bloom), [task 29](29-auto-exposure.md),
[task 33](33-mist-and-ground-fog.md) and [task 28](28-volumetric-clouds.md)
(their own temporal history), [task 37](37-vegetation-shadows.md) (the
queue's animated submissions).

## Goal

The blur of fast motion, as the eye and a camera see it: from a cab at
100 km/h and more (160, 200, the Acela's 240) the ballast, sleepers, poles
and anything near the track streak, and a train passing at speed streaks
past a camera that stands still. What the user does with the camera does
not blur: flying the free camera, turning the head in a cab, moving within
it (the user, 2026-10-29: "not camera movement blur").

QRhi only (the OpenGL renderer has none of the passes this needs).

## What blurs and what does not

Blur comes from motion relative to a **reference frame** the camera holds
still in:

- **The camera riding a vehicle** (cab, the train's outside views,
  `Camera::cameraObject` with its `CameraMount`): the reference is the
  vehicle. The world blurs by the vehicle's motion, curves included; the
  cab and the vehicle's own train stay sharp; the head's turning and moving
  within the mount do not blur.
- **Any other camera** (free, editor, trackside): the reference is the
  world. The world stays sharp however the camera flies or turns; moving
  objects (trains, road cars) blur by their own motion.

So the blur of a pixel is its motion on screen less the part the camera's
own movement in its reference frame makes. That split is what lets one
velocity buffer serve motion blur and TAA (task 39), which needs the
pixel's whole motion.

## The velocity buffer (shared with task 39)

A fourth colour target of the view, RG16F: each pixel's motion on screen
from the previous frame to this one, in pixels, **the whole motion**
(camera and object), **without projection jitter** (task 39 adds jitter to
the projection used for drawing only). This is what TAA, FSR 2/3 and DLSS
take; motion blur derives its own from it.

- **Written by the scene's opaque, alpha-tested and water draws**
  (`StandardFog` and its PBR, terrain and water variants; distant terrain;
  the sky). The vertex shader projects the vertex's world position (the
  lit shaders' `vLightPosition`) with this frame's view-projection and the
  previous one's, the previous world position from the item's motion
  matrix; the fragment shader writes the difference in pixels.
- **Not written by** blended parts, particles, rain, glass and
  transmission (write mask off on that target): what lies behind keeps its
  motion. Task 39 adds a reactive mask for them.
- **The sky**: rotation only (it is at infinity), from the direction.
- **Cost**: 4 bytes a pixel a draw; one more target in every scene
  pipeline's render pass (all scene pipelines change once).

### Previous transforms

- **Static objects** (most of the world): the motion matrix is identity;
  only the camera term remains.
- **Moving objects**: the queue already marks animated submissions
  (`RenderQueue::setAnimated`: trains, car spawners, signals, level
  crossings; task 37's shadow caches). Producers of those also give an
  owner key (a new `RenderQueue::setMotionOwner(const void *)`: the
  vehicle, the road car). The queue keeps, per (owner, packet, `msMatrix`
  pointer), last frame's full transform (instance transform times
  `msMatrix`) and gives each item `motion = previous * current^-1`. Parts
  animated through `msMatrix` (wheels, rods, pantographs, doors) get their
  own previous matrix this way: turning wheels blur. A key not seen last
  frame gets identity (no blur on the first frame an object appears).
- **Per-item data**: items take their uniforms by index (task 25); a
  motion index per item into a small array of motion matrices (identity at
  0) costs 4 bytes a static item instead of 64.

### The camera's history

- **The real previous view-projection** (unjittered): what the velocity
  buffer and task 39 use. The fog volume (`RhiFog.cpp`, `previous`) and
  the clouds keep their own today; one camera history for all of them.
- **The riding frame**: when the camera rides an object, that object's
  world transform this frame and last (`O`, `O_prev`); the camera's pose
  in it held, the previous view motion blur takes is `view * O * O_prev^-1`.
  Not riding: this frame's view.
- **Tile crossings**: positions are relative to the camera's tile; when it
  changes, the previous view and every stored previous transform shift by
  the tiles' 2048 m so last frame lies in this frame's space.

## The motion blur pass

After the scene and its full-screen composites (ambient occlusion, clouds,
sky fog, rain), before `present()` (bloom, exposure, light shafts, tone
mapping): on the HDR view, so lamps streak brighter than their surround.
Following McGuire et al. 2012 ("A Reconstruction Filter for Plausible
Motion Blur") with Jimenez's changes (Call of Duty: Advanced Warfare,
SIGGRAPH 2014):

1. **The blur velocity per pixel**: the buffer's motion less the camera's
   own movement in its reference frame, computed from the pixel's depth:
   the pixel projected with the real previous view and with the reference
   one (above); their difference is taken off. (The object's previous
   position is taken as its current one there: off by the object's motion
   times the camera's own turn, too small to see.) Scaled from a frame's
   motion to the **exposure** (the setting, e.g. 1/120 s): the same blur
   at 30 and 120 frames a second. Limited to a maximum length (about 3 %
   of the view's width).
2. **Tile max**: the longest blur vector in tiles of that maximum length
   (compute, two reductions).
3. **Neighbour max**: the longest in each tile's 3 x 3 tiles.
4. **Gather** (full resolution): pixels whose neighbourhood moves less
   than half a pixel are copied. Others take 12-20 samples along the
   neighbour max's direction and along their own (half each), weighted by
   soft depth comparisons (what is in front blurs over what is behind, not
   the other way) and by how far each sample's own blur reaches, offsets
   jittered per pixel (interleaved gradient noise) to trade banding for
   fine noise (task 39 smooths it).
5. **Glow**: the glow target, bloom's source, blurred by the same samples
   (a second output), so lamps and signals streak in the bloom too.

Overlays and the HUD: drawn after the pass or masked (editor lines are
world objects and may blur with the world; the HUD never).

### Settings

- `core.rendering.motionBlur`: Off, Light (1/250 s), Normal (1/120 s),
  Strong (1/60 s). Default Normal, 1/120 s (the user, 2026-10-29; to be
  fine-tuned).
- Samples by quality: 12 or 20 (maybe tied to the existing quality
  presets). The Steam Deck at 1280 x 800 should stay under 2 ms at Normal.
- Physical blur at every speed (at 50 km/h only the nearest ballast
  smears), no fade by the train's speed (the user, 2026-10-29: physical
  first, fine-tuned later; a fade in by the riding train's speed is a small
  addition if wanted).

## Tests and captures

- **Velocity debug view** (`TSRE_MOTION_DEBUG`): the buffer and the blur
  velocity as colours; the tile max.
- **Moving captures**: the harness renders two frames with the simulation
  advanced by a fixed step between them (new view fields: `motionFrames`,
  `frameDt`), so the blur is the same each run. The harness can already
  ride a simulated train (`activity`, `simulateTo`, `train`,
  `trainView: "cab"`), and the simulation drives the player's train as an
  AI train (simulation task 01): USA1's `smmrex.act` (Summer Express, the
  Acela, from 12:00) run to 12:06:00 has the player's train (train 4) at
  135 km/h and AI Acelas at 149 km/h; the cab view draws (2026-10-29).
  Missing: `SimulationController::runTo` takes whole seconds; the second
  frame needs a run to a time in fractions of one (the private
  `runToTime(double)` made public, or a step).
- **Views** (light MSTS routes, not Prudnik; the user, 2026-10-29):
  - USA1 (Northeast Corridor, MSTS stock, game root
    `C:/MagiPacks/Microsoft Train Simulator`), `smmrex.act`, the player's
    Acela (train 4) from its cab, run from 12:02:00 to (2026-10-29): 12:04
    63 km/h, 12:08 192 km/h (straight, gantries and trees close), 12:12
    138, 12:16 23, 12:20 175; the whole run in 28 s. The Acela's 2D cab is
    not drawn, so the camera is at the front with the line ahead (the user:
    better than a 3D cab's seat, set back). The outside view riding the
    train too.
  - The cab staying sharp while the world blurs: a 3D cab (the ED160,
    `PSK_IC_ED160-013.con`, its end cars' `ED160_3D.s`; C:/trainsim), not
    a test of the blur's look.
  - A camera on a platform with a train passing at speed (Prudnik, or
    USA1): only the train blurs, its wheels most.
  - The free camera flying fast past static scenery: no blur.
  - The cab with the head turning: no blur but the world's.
  - A tile crossing at speed: no flash.
- **Costs**: Off, Light, Normal, Strong on the Steam Deck at 1280 x 800 and
  on the user's screen; the velocity target's cost alone (scene pass
  before and after).
- Suites: rhi-shaders (new shaders), the capture parity sets (no change
  with motion blur Off), settings, translations.

## Steps

1. The camera history (real previous view, riding frame, tile shifts) and
   the queue's previous transforms (`setMotionOwner`, keys, motion
   indices); trains, road cars and their animated parts give owners.
2. The velocity target: scene shaders write it, blended passes mask it,
   the sky's rotation; debug view. Check: static world from a riding
   camera moves by the train's motion, the cab is still.
3. The motion blur pass (blur velocity, tile max, neighbour max, gather,
   glow) and its setting; the harness's moving captures.
4. Tuning on the Steam Deck: exposure, samples, maximum length, noise;
   costs; the speed question with the user.
5. Docs, settings' Polish, roadmap.

## Later

- Task 39 (TAA, then FSR 2/3 and DLSS) on the same velocity buffer and
  camera history; motion blur then runs after the TAA resolve.
- Rain streaks and particles from their own motion.
