# Task 41 - Exhaust: Smoke and Steam

Status: designed 2026-10-31. Decided (the user, 2026-10-31): lit
volumetric particles (C) for the smoke and steam near the train, then
spread smoke handed over to the fog volume (D); the grid volume march (B)
kept as the option if C does not meet expectations close up. The setting
and the OpenGL renderer still to decide (see "To decide").

The render team's part (C) of [driving task 05](../driving/05-exhaust.md):
drawing what leaves the nozzles. The driving side (A and B) is merged into
main: emitters, per-frame values, the diesel and steam models, the heard
trains, and a test state.

Related: [task 28](28-volumetric-clouds.md) (volumetric lighting, the
reduced-resolution march and its composite), [task 33](33-mist-and-ground-fog.md)
(the weather's wind, temperature and dew point, the fog volume),
[task 34](34-train-lights.md) (split the same way: test state first),
[task 37](37-vegetation-shadows.md) (shadow maps), tasks 38 and 39 (motion
blur and TAA: translucent surfaces write no velocity).

## Goal

Smoke and steam as the eye sees them: a diesel's exhaust darkening as it
notches up, a steam engine's beats, steam from the cocks and safety valves
hanging longer on a cold damp morning, all carried by the train and the
wind, rising, spreading and thinning, lit by the sun and sky and shadowed
by the scene. The user (task 05): "We want to enjoy some real world
simulations."

QRhi only (the OpenGL renderer has none of the passes this builds on),
unless decided otherwise.

## What arrives (task 05, A)

- **Emitters**, read once from the vehicle definitions
  (`VehicleDefinition::emitters`, `exhaustKinds`): kind, place and
  direction in the vehicle's frame, nozzle diameter.
- **Per frame, per vehicle, per kind** (`Eng::exhaust()`, `ExhaustOutput`):
  flow (m³/s, 0 off, shared by the kind's emitters), speed at the nozzle
  (m/s), temperature (°C), steam share and soot (0-1), beats (puffs a
  second, the stack), colour (ARGB from the ENG, 0 none).
- **The test state:** `TSRE_EXHAUST_TEST=1` turns every emitter on, static
  consists too.
- **Measured values**, MSTS's Dash 9 at 20 m/s: idle 0.83 m³/s at 3 m/s,
  150 °C, soot 0.1; notching up 5.4 m³/s, 500 °C, soot 0.9; full 11.3
  m³/s at 40 m/s, soot 0.25.

## How to draw it: the options

| | look | the Deck's cost | work |
|---|---|---|---|
| A. Particles, as Open Rails: camera-facing quads with a smoke texture, blended back to front | flat sprites; edges and repetition show up close (a cab behind the stack) | fill rate: many overlapping translucent quads near the camera | small |
| B. Ray-marched volumes: each plume a chain of puffs whose density a screen pass marches, with light marched to the sun | the real thing: depth, self-shadow, light through thin parts | high: a march per pixel of every plume, a second toward the sun | large |
| C. Lit volumetric particles: particles carry the motion; each is shaded as a soft sphere of noisy density (its thickness along the ray, light absorbed toward the sun by its depth in the plume), in a reduced-resolution target composited by depth as the clouds are | volume-like, soft against the scene, no sprite edges | moderate: a fragment per particle per covered pixel at half resolution | medium |
| D. Density put into the fog volume (task 33's froxels) | lit with the fog, but blurred: the froxels are metres wide near the camera | low | small, but too coarse for a plume a metre across |

**Decided: C, then D for spread smoke; B kept as the option** (the user,
2026-10-31: "C + later D for spreaded smoke sounds like an interesting
combination ... Don't remove B variant option to consider if C doesn't
meet expectations").

- **C for the smoke and steam near the train.** The simulation is the same
  as A's (and as Open Rails' functional reference), so the physical values
  are used as they are; the look approaches B's at a fraction of its cost;
  it reuses what the renderer has: the clouds' 3D noise textures and phase
  function, their reduced-resolution target composited by depth, the
  shadow maps' sampling, the environment's sky light, the rain's quads
  made in the vertex shader from a buffer, the weather's wind and dew
  point. New: the CPU simulation, the particle shader, the pass.
- **D for smoke spread wide, later.** On its own D is too coarse for a
  plume a metre across: its cells are 8 x 8 pixels at best, its 64 slices
  grow with the distance (1.5 m deep at 10 m, 7 m at 50 m), and it keeps
  95 % of the last frame's result (a moving plume would smear; its RGBA16F
  cells could hold the smoke's colour). It is right for what is large,
  thin and slow: smoke grown old and spread, which leaves the particles and
  is added to the fog volume, lying along the line or in a valley on a
  calm cold day and lit as the fog is.
- **B, if C falls short close up** (a cab behind a steam stack, a train
  passing through its plume): not another cloud layer as it is. The
  clouds stay affordable by marching at a reduced resolution and blending
  with the last frames by direction, right for clouds kilometres away but
  smearing a plume a few metres off that moves with the train; a plume
  needs a full march every frame. Done as a grid volume per train (some
  60-100 m, cells of about half a metre): a compute pass adds the
  particles into it each frame, a second works out the sun's light through
  it once, and a march over the smoke's screen area reads the two grids
  and the clouds' detail noise, with the clouds' lighting. Its cost follows
  the screen area the smoke covers, a few per cent in most views (about
  1-3 ms on the Deck, a guess; more inside a plume, where the path is
  metres, not kilometres). The simulation and D stay as they are.

## The simulation (CPU, per frame)

- **Emission:** each emitter's share of its kind's flow, as particles of a
  set volume at the nozzle (its diameter sets their first size), leaving
  along its direction at the nozzle's speed plus the vehicle's velocity.
  Steam stacks: a puff at each beat (beats a second, placed at the
  renderer's own frame times), its volume the flow over the beats.
- **Motion:** each particle slows from its speed toward the wind's
  (drag), rises by its temperature over the air's (buoyancy), and cools as
  it mixes.
- **Spread and thinning:** it grows as it mixes with air (turbulent
  diffusion: radius with the square root of its age, faster while fast),
  its density falling as its volume grows, so the same amount of smoke
  shows thinner.
- **Steam:** visible while the plume's water is above what the air holds:
  short-lived on a warm dry day, lingering at a small dew point spread;
  it evaporates rather than thins out.
- **Smoke:** its soot absorbs (dark), the colour from the ENG where given;
  it lasts until thinned out.
- **Limits:** a cap on particles in all (a setting's quality) and a
  distance beyond which emitters emit nothing; the oldest go first.

## Drawing (QRhi)

- Particles in an instance buffer (place, radius, density, age, kind),
  drawn as camera-facing quads into a reduced-resolution colour and
  transmittance target after the scene's opaque passes, against its depth
  (soft: fading where a particle meets the scene).
- **Each particle's shading:** the thickness of a sphere along the
  pixel's ray, its density shaped by a tiling 3D noise turned by the
  particle's age (no sprite texture); light: the sun through the shadow
  maps at its centre, absorbed by its depth into the plume toward the sun
  (an estimate from the plume's axis), scattered with the clouds' phase
  function; the sky's light from the environment; steam white and
  scattering, smoke absorbing by soot.
- **Composite** over the view by depth at full resolution, as the clouds'
  march is composited; before the blended pass, so glass and water see it.
- Night: the firebox glow under a steam stack and the train's lamps
  (task 34) are later steps.
- No velocity (translucent, task 38); TAA's history clipping keeps it from
  ghosting (task 39).

## Steps

The simulation first: every drawing option builds on it.

1. **Count our content** (C:/trainsim: CMK, PKOR, Prudnik): emitters by
   kind, as task 05 counted ~/ORTS.
2. **The simulation from the test state:** particles from every emitter of
   a static consist, their motion, spread and life; a debug draw of their
   centres.
3. **The volumetric particles:** the reduced-resolution target, the
   shading, the composite; the Deck's cost measured (tests/renderer cases
   with a static consist, `TSRE_EXHAUST_TEST=1`).
4. **The weather:** wind, temperature and dew point (task 33); steam's
   life; the sun's and sky's light.
5. **The simulation's values:** a driven diesel (MSTS's Dash 9 or GP38:
   idle, notching up, full, notching down), a steam train (EUROPE1
   `lclsrvce`'s heard train 3: stack beats, cocks, safety valves).
6. **Limits and settings:** the particle cap and distance as a quality
   setting; the Deck's budget.
7. **Spread smoke into the fog volume (D):** particles past an age or a
   size handed over to the fog volume as density and colour, thinning
   with it; checked on a calm cold morning and in wind.
8. **If C falls short close up: the grid volume (B)** for the near plume,
   measured against C at a cab behind a steam stack.

## To decide (the user)

1. ~~The approach~~: decided, C, then D for spread smoke, B kept as the
   option (2026-10-31).
2. **The setting:** Off / Normal / High (the particle cap and the
   target's resolution), default Normal? Off in the OpenRails presets?
3. **The OpenGL renderer:** none (recommended), or plain particles (A)
   there?
