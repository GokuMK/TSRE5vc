# Task 38 - Motion Blur

Status: merged from `feature/motion-blur` (2026-10-29); head-turn blur
as an option and tuning open (see Open).

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
  `Camera::cameraObject`): the reference is the vehicle. The world blurs by
  the vehicle's motion, curves included; the cab and the vehicle's own
  train stay sharp; the head's turning does not blur.
- **Any other camera** (free, editor, trackside): the reference is the
  world. The world stays sharp however the camera flies or turns; moving
  objects (trains, road cars) blur by their own motion.

So the blur of a pixel is its motion on screen less the part the camera's
own movement in its reference frame makes. That split is what lets one
velocity buffer serve motion blur and TAA (task 39), which needs the
pixel's whole motion.

## The velocity buffer (shared with task 39)

A fourth colour target of the view (`color4`, RGBA16F), each pixel's
motion on screen from the previous frame to this one in half normalized
device units (times the view's size: pixels), **without projection
jitter**, twice:

- **x, y: the whole motion** (camera and object), last frame's position
  through last frame's view: what TAA, FSR 2/3 and DLSS take.
- **z, w: the blur's motion**, last frame's position through the
  **reference** view (riding: the view with the object where it was last
  frame and the head as now; else this frame's view). Exact for every
  surface: the riding cab's own last position seen with the head as now
  is where it is now, so the cab stays sharp however the head turns (the
  first version took the head's turn off in the blur pass with the
  pixel's point where it is now: a 3D cab at 100 km/h smeared by a head
  turn up to the longest blur, CMK `file0.act`, the user, 2026-10-29).
- **Written only by draws of moving instances** (as large engines do):
  a draw with an instance that has a motion index takes the moving
  programs (`MovingMain`, `MovingUnlit`, `MovingPbr`: `StandardFog` with
  `TSRE_VELOCITY`), the rest the programs as with motion blur off. Cleared
  to x = 64 (far beyond any motion): where nothing moving was drawn, the
  blur pass takes the static world's motion from depth and the camera,
  exact for what does not move. Every scene draw writing it cost the
  Steam Deck 5-6 ms in CMK's forest (overdraw), which grows with the
  scenery (USA1 2000, CMK 2010, Prudnik 2020: the user); now CMK's whole
  cost is 3-3.5 ms.
- Of the moving draws, those that write depth, unblended (write mask
  RGBA; `RhiProgram::writesVelocity` from the fragment shader's output at
  location 3). The test is the depth write, not blending: MSTS parts are
  all drawn blended (alpha 1 where opaque). Translucent parts and decals
  leave what lies behind them its motion; a reactive mask for them is
  task 39's.
- **The vertex shader** projects the vertex's world position with this
  frame's view-projection, its last-frame position with last frame's
  (`uPrevPMatrix`, the projection with last frame's view in this frame's
  space) and with the reference's (`uRefPMatrix`); a moving instance's
  last-frame position from its last-frame matrix (below).
- **Beyond the scene band** (the distant band, the sky and its clouds:
  their own projections): the static world too, from the depth slice they
  lie in.

### Previous transforms

- **Static objects**: index 0, last frame's position is this one's.
- **Moving objects**: submissions under `RenderQueue::setAnimated` with an
  owner (`setMotionOwner(owner, part)`): cars (`Eng`, the 3D cab included),
  animated world objects (`Tile`, the object), road cars (the spawner and
  each car's number: a list index moves when a car leaves). The queue keeps
  last frame's full matrix (instance times `msMatrix`) per (owner, part,
  packet, `msMatrix` pointer), by the order of their submissions, in a
  table kept from frame to frame (no allocation once seen; 0.6 ms a frame
  for 887 parts in a 3D cab with maps refilled each frame, the user's CPU
  being the limit), and gives each instance an index
  into a table (RGBA32F texture, binding 37, 256 matrices a row, 64 rows):
  parts animated through `msMatrix` (wheels, rods, pantographs) blur by
  their own motion. A key new this frame takes its current matrix (no blur
  the frame an object appears).
- **Instances**: with motion blur on, the per-instance record grows from
  20 to 24 floats (the motion index at float 20, attribute location 12).
- **Off costs nothing**: the scene programs' velocity code (`TSRE_VELOCITY`)
  is compiled only with motion blur on, the records stay 20 floats, the
  view's motion is not taken.

### The camera's history

`RouteEditorGLWidget::updateViewMotion`, before the gather, gives the
renderer a `Renderer::ViewMotion`:

- **world**: last frame's view in this frame's space,
  `view^-1 * previousView * T(tile shift)`.
- **reference**: riding the same object as last frame, the view the camera
  had with the object where it was last frame and the head as now
  (`Camera::viewAtPlacement`); otherwise identity (the world).
- **Tile crossings**: last frame's view and every stored previous matrix
  move by the tiles' 2048 m (`shiftMotion`).
- **A jump** (moved over 200 m, another field of view): no motion that
  frame.
- `CameraFree::getMatrix` now calls `getPos` before `getTarget`: as
  arguments in an unspecified order, the target was taken from the previous
  call's place, which for the view at another place turned it 2.5 degrees.

## The motion blur pass

`RhiMotionBlur.cpp`, after the scene and its full-screen composites
(ambient occlusion, clouds, sky fog, rain) and before any UI pass (the
compass, the HUD: they stay sharp), on the HDR view. McGuire et al. 2012
("A Reconstruction Filter for Plausible Motion Blur") with Jimenez's
changes (SIGGRAPH 2014), five fragment passes:

1. **The blur vector per pixel**: the velocity's z and w; beyond the
   scene band the static world's, from the pixel's depth (`P W P^-1` with
   the reference's movement). Scaled from a frame's motion to the
   **exposure** (`(1 / exposure) / frame time`: the same blur at any frame
   rate), limited to the quality's longest. Stored with the pixel's
   distance and the blur's radius, what the gather's samples read.
2. **Blocks**: the longest of 8 x 8 (a constant: unrolled).
3. **Tiles**: the longest in tiles of the maximum length (blocks of 8).
4. **Neighbours**: the longest in each tile's 3 x 3 tiles.
5. **Gather** (full resolution): pixels whose neighbourhood moves less than
   half a pixel are discarded (left as they are). Others take about one
   sample per 3 pixels of the neighbourhood's longest blur (4 to the
   quality's most), half along it and half along their own, weighted by
   soft depth comparisons and by how far each sample's own blur reaches
   (cone and cylinder weights). The colours between texels where the
   offsets fall (bilinear; whole texels drew steps); the offsets jittered
   per pixel and the tile looked up shifted by up to a quarter tile
   (Jimenez), both by blue noise (`CloudNoise::blue`): the interleaved
   gradient noise drew diagonal and vertical lines into the streaks (the
   user, 2026-10-29), as in the clouds and the light shafts. The glow,
   bloom's source, by the same samples (a second output), so lamps streak
   in the bloom too.

`TSRE_MOTION_DEBUG=1|2|3`: the whole motion, the blur vector, the
neighbourhood's longest as colours (red x, green y, 32 px full; blue when
one is negative).

### Settings

- `core.rendering.motionBlur`: Off, Light (1/250 s), Normal (1/120 s),
  Strong (1/60 s), Very strong (1/30 s). Default Off, as the other
  Advanced Environment effects: a new profile is OpenRails Extended
  (settings test); Normal 1/120 s with Advanced Environment (the user,
  2026-10-29; to be fine-tuned). Startup setting (the view's targets).
  Captures pass `--set core.rendering.motionBlur=120`.
- `core.rendering.motionBlurQuality`: Normal (the longest blur 3 % of the
  view's width, up to 12 samples), High (8 %, 32). Default Normal. GPU
  time only.
- Graphics presets: off in OpenRails Basic and Extended, Normal (1/120 s)
  in Advanced Environment (the user, 2026-10-29).
- **Very high, tried and dropped** (2026-10-29): the longest blur 15 % of
  the width, up to 64 samples. At 1/30 s and 192 km/h it looked as High
  (no streak passed High's 8 %; it differs only for nearer or faster
  motion: a train passing a few metres away, longer exposures) and cost
  about 2 ms more on the Steam Deck (USA1 cab, Very high 1/30 61.3 / 65.5
  ms against High 1/30 59.4 / 63.9). Dropped so as not to confuse users
  for so little (the user); to come back to with longer exposures or
  TAA.
- At 192 km/h the track bed moves 1.5 sleeper spacings in 1/60 s wherever
  it is on screen (both shrink with the distance alike): a box blur of 1.5
  periods leaves about a fifth of the sleepers' contrast, so they still
  show as faint bars; at 1/30 s (3 spacings) they smear. The qualities
  differ where the streaks pass Normal's 38 px (1280 wide): at 1/60 s
  little (the bottom rows), at 1/30 s the bottom rows and a train passing
  15 m away (82 px) (USA1, 2026-10-29).
- Physical blur at every speed, no fade by the train's speed (the user,
  2026-10-29: physical first, fine-tuned later).

## Tests and captures

- **The harness** (`renderer-capture`): a view's `motionSeconds` runs the
  simulation on by that step before each timing frame, which the frame
  takes as its last, as a ride does (the simulation steps by 0.1 s at
  least; the frame time is the step taken); `motionMove` (m) and
  `motionTurn` (degrees, yaw and pitch; riding, the head) move the camera
  over each step. `SimulationController::runToSeconds` (fractions of a
  second), `RouteEditorGLWidget::diagnosticTick` (the simulation's frame
  taken, the camera following its train) and `diagnosticCameraStep`.
- **USA1** (game root `C:/MagiPacks/Microsoft Train Simulator`),
  `smmrex.act`, the player's Acela (train 4) at 12:08:00, 192 km/h
  (2026-10-29, Normal):
  - Cab: the ground, ballast and sleepers streak from the line ahead; the
    gantries and trees beside the line little; the compass sharp.
  - Cab with the head turned 3 degrees (and 1 down) in the step: the same
    field around the new vanishing point, nothing from the turn; at
    63 km/h with 5 degrees, the same.
  - The free camera moving 5 m and turning 3 degrees in the step: the
    image as with motion blur Off (no difference).
  - Riding the train's back view: the train sharp, the world streaking.
  - A camera standing 15 m beside the line: the passing train streaks, the
    mast in front of it and the trees sharp.
  - A tile crossing at 198 km/h (from 12:08:15, the 28th step in the cab,
    the 61st in the back view): the same field as the frames around it,
    the train sharp.
  - Still views on bbb and Prudnik: as main's build (at most 1 of 255).
- Suites (2026-10-29): settings (297), rhi-shaders (93), gltf-pbr-gl,
  water-gl, environment-map-gl, cab3d, local-lights, eng-lights,
  shape-complex-gl, terrain-material-gl, terrain-mesh-gl, transfer-depth-gl,
  consist-preview-gl, selection-id, ctest (translations and the rest).
- Not run: the ED160's 3D cab on Prudnik (the cab sharp while the world
  streaks; the cab is drawn under the car's owner like its body).

## Costs

Steam Deck (Windows), 1280 x 720, USA1 at 192 km/h, Normal, each pass run
11 times in a frame against once (wall frame times are too noisy for less):

- The passes: 2.2 ms in the cab (the lower half streaking at full length,
  the worst case), 1.7 ms in the back view. Of the cab's: blur vector
  0.2, blocks, tiles and neighbours 0.3 (0.8 before the blocks' loop was
  unrolled), the colour and glow copies 0.45, the gather the rest (3.2 ms
  in all before the unrolled blocks, samples by length and the packed
  distance).
- Still pixels cost the copies and a discard; a still camera over a still
  scene still runs the passes.
- Whole frames with the velocity written but the passes skipped (one run
  each, 20 moving frames, minimum and median; 2026-10-29, RGBA16F):
  - USA1 cab: off 53.2 / 58.2 ms, velocity only 55.2 / 61.4, Normal 1/120
    56.1 / 64.6, Normal 1/30 58.7 / 64.3, High 1/30 59.4 / 63.9, Very
    high 1/30 61.3 / 65.5. Back view: off 51.7 / 57.8, Normal 1/120
    52.8 / 58.5, Very high 1/30 55.6 / 60.2.
  - CMK 3D cab at 100 km/h (`file0.act`, the user's): off 71.2 / 77.1,
    velocity only 76.0 / 83.1, on 78.2 / 84.2. Most of it is the scene
    writing the velocity, which grows with what is drawn (a dense forest's
    overdraw), not the passes.
- With the static world's motion from depth (two runs each): CMK 3D cab
  off 71.5 / 78.7 ms, on 75.0 / 81.7 (+3-3.5, was +7); USA1 cab off
  51.6 / 58.8, on 55.5 / 61.8 (+3, the passes: its scenery is light).
- CPU: 0.3 ms with motion blur on (CMK 3D cab, within the noise; 1.2 ms
  before the table kept from frame to frame). Off against main's build
  (five runs each): the same frame and CPU times, the same image.

The passes and the velocity are GPU work: users of desktop graphics cards
have it to spare (the user, 2026-10-29: the CPU is the limit there).

Over the 2 ms aim at Normal on the Steam Deck (an extra there, the user).

## Open

- **Head-turn blur as an option**, to be decided later (the user,
  2026-10-29): an addition on top of the exact blur, the reference with
  the head as last frame instead of as now: cab and world alike blur by
  the head's turn.
- What noise is left in long streaks: TAA (task 39) smooths it; a second
  gather pass (CryEngine 3) would square the samples.
- Tuning on the Steam Deck with the user: exposure, samples, maximum
  length, noise, costs; a fade by the riding train's speed if wanted.
- The ED160 3D cab capture on Prudnik; road cars (keyed per car) not
  captured.
- Task 39 (TAA, then FSR 2/3 and DLSS) on the same velocity buffer and
  camera history; motion blur then runs after the TAA resolve.
- Rain streaks and particles from their own motion.
