# Task 41 - Exhaust: Smoke and Steam

Status: designed 2026-10-31; steps 1 to 5 done 2026-10-31 (our content
counted; the simulation, its hookup and the setting; the volumetric
particles drawn in the QRhi renderer: their light from the scene's shading,
soft spheres of eroded noise at half size, composited by depth; the
plume's shadow on itself, the weather's light; the driving models' values
checked: wet exhaust steam, soot's colour, lamps by day). Decided (the user,
2026-10-31): lit volumetric particles (C) for the smoke and steam near the
train, then spread smoke handed over to the fog volume (D); the grid volume
march (B) kept as the option if C does not meet expectations close up. A
setting Off / Normal / High, Normal by default and in every preset (Open
Rails draws its emitters too); QRhi only, plain particles (A) in the OpenGL
renderer possible later.

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

QRhi only (the OpenGL renderer has none of the passes this builds on;
the user, 2026-10-31: none there for now, plain particles (A) possible
later, drawn from the same simulation).

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

## Step 1: our content (2026-10-31)

Emitters by kind, vehicles that have them (`VehicleDefinition::emitters`):

| | vehicles with emitters | kinds |
|---|---|---|
| C:/trainsim (CMK, PKOR, Prudnik's stock) | diesels 400 of 416, carriages 110 of 447; no steam or electric engine has any | exhaust 400, heatinghose 164, generator 54 |
| MSTS | diesels 8 of 8, steam 5 of 5 | exhaust 8; stack, cylinders, safetyvalves, whistle, drainpipe 5 each |

- **Diesels:** nearly every one has its exhaust; 164 vehicles have steam
  heating hoses (Prudnik: steam heating as on the SM42, no coal-fired
  locos, the user 2026-10-31), 54 a generator's exhaust.
- **Steam engines** only in MSTS's routes: EUROPE1's `lclsrvce` (task
  05's heard steam train) and EUROPE2's `oetutorial` (the driven 380 of
  `tests/renderer/driving-steam.json`) for the stack's beats, cocks and
  safety valves.

## Step 2: the simulation (2026-10-31)

- **`ExhaustParticles`** (`src/tsre/renderer`, renderer-neutral, one for
  the main view): particles in the scene's space, each a puff of an
  emitter's gas.
  - Emission: an even flow as `rate` puffs a second (20), a stack one a
    beat; a puff the gas of its share of the time (the kind's flow shared
    equally by its emitters), its radius from its volume, the nozzle's at
    least; spread along the way the nozzle went in the frame, each moved
    on for its time since. At the nozzle's speed along its direction plus
    the vehicle's velocity, from its place last frame (a move faster than
    150 m/s is a jump: no speed).
  - Motion, steps of 0.05 s at most (shorter while a fast jet grows more
    than 10 % a step): the radius grows by 0.12 times the speed through the
    air plus the air's stirring (0.04 + 0.03 x wind m/s); the air taken in
    shares the puff's momentum through the air ((r / r')^3); buoyancy of
    the mixture, g (T - Ta) / (T + 273.15), its temperature mixed by the
    share of nozzle gas left, (r0 / r)^3.
  - What shows, as extinction (1/m), for step 3: smoke (1 - steam share) x
    (0.1 + 3 x soot) x that share (a diesel's opacity meter reads 0.5-3
    1/m for visible smoke); steam 300 x the liquid water (kg/kg: 1.2 kg/m^3
    of air, droplets of some 6 um), the water the mixture holds beyond what
    its temperature can (Magnus; the air's own at its dew point; all of it
    at boiling and above, so superheated steam is clear at the nozzle).
  - Gone past 90 s or 40 m across, or once its optical depth through the
    centre is below 0.005 (past 0.5 s). Past the limit a new puff replaces
    the oldest. The camera's tile change shifts them all (and the
    emitters' last places).
- **Hookup:** `RouteEditorGLWidget::beginExhaustFrame` before the main
  view's gather: the limit by the setting, the tile shift, the frame's time
  (a capture's motion step), the weather's air: the low layer's wind x 0.6
  (a power law down to some 5 m), its temperature and dew point.
  `Eng::emitExhaust` from `pushRenderItemBetween`: each emitter's place and
  direction through the car's matrix (a point of its shape, whose root
  mirrors x), within 2 km of the camera. The frame closes after the
  overlays: an activity's trains are gathered with them.
- **The setting** `core.rendering.exhaust`: Off / Normal / High (3,000 or
  10,000 particles at once), hot (kept by the view); Normal by default and
  in every preset.
- **Debug:** `TSRE_EXHAUST_DEBUG=1` draws each particle's centre (a cross
  half its radius each way) and logs every 30 frames the particles live,
  emitted and replaced, the fastest along, the air.
- **Tests:** suite `exhaust-particles` (13 checks): an idle diesel in still
  air (100 puffs in 5 s, rising 3.6 m, 0.21 to 0.85 m across, its smoke
  0.38 to 0.006 1/m, straight up); in a 5 m/s wind (with the wind past 3 s,
  4.95 m/s); a train at 20 m/s (its oldest puff 5 m from where it left,
  none ahead); a jump of 1 km (no speed); cylinder cocks (steam seen 1.1 s
  at 25 °C, dew point 5 °C; 14.4 s at 2 °C, dew point 1 °C); a safety valve
  (clear at the nozzle, then 32.6 1/m, no smoke); a stack (40 puffs in 10 s
  at 4 beats; steam first, then smoke only); the limit (100 live of 1,000);
  off; half the flow (the same puffs, cbrt(1/2) the size); a tile shift.
- **Captures** (USA2 `nnmxdgds`, the driven Dash 9s as
  `driving-diesel.json`, `--set core.rendering.exhaust=1`): the plume
  leaves the exhaust stack; notched up (3.25 m^3/s, 16.8 m/s, 360 °C) it
  rises tens of metres in the calm; two locos keep 230-350 particles.
  Moving at 51-57 km/h (motion steps of 0.1 s), the puffs leave at
  14.4-15.3 m/s along and slow to the 1.8 m/s wind. The harness holds the
  simulation in its settle frames, so a train stands there: a straight-up
  plume; and a view riding a vehicle (`trainVehicle`, `trainEye`) did not
  follow it through motion steps (`trainView` "back" did).

## Step 3: the volumetric particles (2026-10-31)

`RhiExhaust.cpp`, in the main view after its opaque passes (with the
clouds and the sky's fog), before the blended ones:

1. **Their light**, one texel a particle (256 a row, 40 rows: High's
   10,000), by a program of the scene's own shading (`ExhaustLight.vs`,
   `.fs` in shaders330: `ShadowSampling.glsl`, `Haze.glsl`,
   `LocalLights.glsl`, `EnvironmentLighting.glsl`): the frame's uniforms
   as every material takes them (`writeFrameUniforms`), its textures as
   the draws' (`frameSlot`, the frame-only part of `recordDraw`'s slots,
   moved out unchanged: an image of Prudnik with it against main's differs
   in 14 pixels by 1 level). At each centre: the sun through the shadow
   maps (four taps over half the puff, the far map's shadow lighter, as on
   surfaces) and vegetation's, the clouds' shadow and cover
   (`enclosureSun`); the sky's light as `ambientLight`, under cover as the
   surfaces'; the lamps' (`localLightsDiffuse`, every side); the fog before
   it (`fogAt`), what the air lets through and its light (`applyHaze`'s
   terms; the classic fog without a physical sky). Five RGBA16F targets.
   `ShadowSampling.glsl` takes derivatives: a fragment shader, not the
   puffs' vertex shader.
2. **The puffs**, sorted far to near on the processor (those outside the
   view's sides and past its reach left out), instanced quads from the
   particle textures (no vertex buffers), into an RGBA16F target of half
   the view's size (High: the view's), premultiplied over, and an R16F one
   keeping the nearest seen puff (1 / (1 + depth), the largest; the targets
   clear to one colour). Per puff (vertex shader): its colour, the same
   all over it: shaded point by point, each sphere's sunlit rim and its
   self-shadow drew a stack of discs. A thin puff scatters the sun once
   through half its depth (Henyey-Greenstein: steam 0.7 forward at 0.75
   with 0.3 back at -0.2, smoke 0.4); a thick one as a ball, its sunlit
   side's share of the view (0.2 + 0.4 (1 - cos)); albedo steam 0.95,
   smoke 0.22 or the ENG's colour; the sky's light; the fog and the air
   before it. Per pixel: the ray's nearest point to the centre, the
   profile (1 - x^2)^2 keeping the puff's extinction (4 sigma r through the
   centre), its edge eroded by the clouds' Worley detail noise (a 32^3
   copy; 1.1 periods a radius, drifting through the puff with its age),
   the share of the chord in front of the scene (the farthest of the four
   view pixels a half-size pixel covers), alpha 1 - exp(-tau).
3. **The composite** over the view at its size: the puffs' image
   (bilinear), cut where the scene's depth there lies in front of the
   nearest puff about the pixel (softly, by 0.3 m + 2 % of the distance).

- **Noise across a plume:** at 20 puffs a second some 20 overlap along a
  plume; each in its own random place in the noise, their sum was a smooth
  grey column. An emitter's puffs now step 0.05 of a period through the
  noise from one to the next (`ExhaustParticles` `seed`), so neighbours
  share its shapes; dense puffs saturate inside, so the noise shapes their
  edges (the clouds' erosion).
- **The setting's quality:** Normal the half-size target, High the
  view's (and 10,000 particles). `Renderer::drawsExhaust` (QRhi only): the
  simulation runs only where it is drawn.
- **The Deck's cost** (pass repeated 11 times in a frame against once,
  31 moving frames; the close view of `exhaust-diesel`'s Dash 9 at half
  throttle, a plume of some 100 puffs over a sixth of the view): the puffs'
  pass 0.55 ms at Normal, 2.6 ms at High; the light pass under 0.1 ms. A
  camera inside dense steam (the whole view) may cost several ms at Normal.
- **Captures:** `tests/renderer/exhaust-diesel.json` (the Dash 9s: a dark
  grey column of ragged edges rising in the calm) and
  `exhaust-steam.json` (EUROPE2's 380 in clear afternoon light: the stack's
  beats a light grey-white plume bending with the wind, the cylinder cocks
  white jets at the front). The first view of a capture file does not ride
  its train (`trainVehicle`): both have a start view first.

## Step 4: the plume's shadow, the weather (2026-10-31)

- **The plume seen from the sun:** a 256 x 256 RGBA16F map about the puffs
  within 250 m of the camera (in view or not: those behind it shade the
  ones in it), across the light's axes; each puff a disc adding its optical
  depth along the light (4 sigma r (1 - x^2)^2, at most 30: cold air keeps
  hundreds of dense steam puffs, and the half floats overflowed, drawing
  the plume black), times its depth (0-1 along the light), times its
  depth's square (with its own spread across the chord). A point's optical
  depth before it: the column's, its depths taken as spread normally about
  their mean (the two moments; the logistic for the normal's integral),
  widened by half a puff's radius (where the point lies is known no better).
  What reaches it: 0.6 exp(-d) + 0.4 / (1 + 0.3 d), multiple scattering
  passing deep (a dense plume's shade grey, not black, its sunlit skin no
  step). On the Deck, 0.05-0.1 ms.
- **Where it is read:** per pixel, at the plume's surface along the pixel's
  ray, a little inside (0.2 r), the same point for every puff over the
  pixel: a pass before the colour draws the plume's nearest dense surface
  (each puff's profile without the noise, the nearest kept, into the
  half-size target the composite cuts by). A puff 3 radii and more behind
  it (another plume) at its own near side. The noise moves the point in
  and out (0.8 r), its billows standing out towards the light. Tried
  first: at each puff's centre (one value a puff: every centre lies deep
  in a plume of overlapping puffs, the whole plume in its own shade); at
  each sphere's near side (each puff's top lit, a stack of rolls); at the
  puffs' middle planes (too deep: lit at the edge only); on a plane in
  front of each centre (a plume receding from the camera stepped from lit
  to shaded at every puff). The front pass: 0.25 ms on the Deck (the close
  Dash 9 plume at Normal); with it all of smoke and steam about 0.85 ms
  there (the colour pass 0.45).
- **A thick puff** takes the sun evenly (0.8; the map does its shading), a
  thin one by the phase function as before.
- **The weather's light:** under clouds the surfaces' ambient is dimmed by
  them (`cloudSkyLight`; the snow preset's 0.17: 0.13 display) while the
  air's light stays bright (0.75): steam in shade, lit by that ambient,
  was black against a snowy day's air. A puff scatters the grey sky about
  it: its sky light goes from the surfaces' ambient to half the air's light
  at its place by the cloud cover (`1 - cloudSkyLight`). Clear, the
  surfaces' (about right for a clear sky's dome; the horizon's bright air
  would leave a sunlit plume no shaded side).
- **Steam's life by the weather** (the cylinder cocks and the stack, EUROPE2's
  380): the snow preset (-2 °C, dew point -3 °C) keeps 388 of 418 puffs
  emitted, the cocks' steam lying white along the track; the clear one (16
  °C, dew point 6 °C) 85 of 363.
- The time of day of an activity's capture is its simulation's (the views'
  `time` does not move the sun there).
- **B's cost against C's:** a grid volume per train (some 60-100 m, half a
  metre cells, filled and lit by compute passes) and a march over the
  smoke's screen area (some 32-64 steps a pixel, a 3D fetch and the noise
  each) would cost, at the view's size and the same area, about what C's
  High does (2-4 ms on the Deck, a guess), at half size about C's Normal
  with a temporal filter that smears a moving plume. Its gain would be the
  look (light through the volume, no puffs), not the cost.

## Step 5: the simulation's values (2026-10-31)

The driving models' values seen as they are (`exhaust-diesel-phases.json`,
`exhaust-steam.json`, `exhaust-ai-steam.json`); they stay the driving
side's. What the drawing does with them:

- **The Dash 9 (diesel.lua):** idle 0.57 m^3/s at 3 m/s, 150 °C, soot 0.10
  (a faint haze, nearly unseen, as a diesel idling); 2 s after notching to
  full 4.84 m^3/s at 25 m/s, 500 °C, soot 0.90 (the turbo's lag); full 7.74
  m^3/s at 40 m/s, soot 0.25; 2 s after notching down 1.23 m^3/s, soot 0.05
  (none to see). Notching drew dark grey: the smoke's colour now follows
  its soot, 0.45 + (0.05 - 0.45) sqrt(soot) (0.32 at 0.10, 0.25 at 0.25,
  0.07 at 0.90: black), an ENG's colour in its place.
- **The steam stack (SteamCore exhaust):** the cylinders' steam at 110 °C
  mixed with the fire's gases (250-400 °C) leaves at some 161 °C, 52 %
  steam: taken as mixed, clear until the air cools it, some 2 m above the
  chimney. Exhaust steam is wet and leaves the chimney as a core before it
  mixes: an emitter's steam carries droplets by its kind (`Eng`
  `exhaustWetness`): the stack's, the cylinder cocks', the drains', a
  heating hose's 10 % of the steam (white at once, evaporating within some
  0.6 s as the puff mixes, while the air's cooling condenses its own), a
  safety valve's and a whistle's none (dry, from the dome: clear for a
  hand's breadth, as they are). The 380's plume now starts white at the lip.
- **Under way** (EUROPE1's AI steam train 3 starting away: 10 m^3/s at 19.8
  m/s, 164 °C, 15.5 beats a second): the puffs leave the chimney straight up
  at the nozzle's speed and spread; the AI trains' models give exhaust as
  the driven ones'.
- **Lamps:** the scene's lamps keep their night strength by day (for their
  glow), and a lamp a few decimetres from a puff lit it brighter than the
  sun: each lamp's inverse square is softened by the puff's size (it lights
  the puff as a whole), none lights steam in the open by day, and at most
  about a white surface in full sun at night or under cover.
- **Not ours:** a glowing white column before the AI train's buffer beam,
  as tall as the loco, is the same with smoke and steam off: the loco's
  oversized lamp glow texture, which does not fit TSRE's lighting (the
  user, 2026-10-31).
- The safety valves did not lift in these runs (the 380 working, the AI
  train); their dry steam's clear gap is the suite's check.

## Realism round (the user, 2026-11-01)

The user: it works and follows the engines, but looks synthetic; asked for
randomness, puffs growing more with time, heavier ones sinking a little,
July against January (no difference seen), and whether ORTS' textures are
needed.

- **Turbulence** (`ExhaustParticles`): each puff rides its own eddy
  (velocity 0.25 m/s in calm air plus 0.15 of the wind's speed, an
  Ornstein-Uhlenbeck walk over some 2 s + its radius, less up and down),
  which stirs it in faster (0.15 of the eddy's speed); a field of large
  eddies (a few sines drifting with the wind, 30-60 m) sways whole plumes.
  No two puffs alike: direction (some 6°), speed (15 %), size (50 %), heat
  (8 %), stirring (40 %). Deterministic (xorshift); `setRandomness(0)` for
  the physics' checks (suite `Calm`).
- **Heavier:** a puff's condensed water weighs it down (buoyancy less g x
  liquid kg/kg): cooled wet steam sinks slowly (the cocks' at 2 °C, dew
  point 1 °C: 0.04 m/s after 20 s). Real plumes fall mostly in a moving
  train's wake (not modelled).
- **A stack's beat in several puffs** (up to 6, by the rate, now 30 a
  second): one ball a beat drew strings of beads in the turbulence. Each
  puff's thin-out threshold lowered by how many share its gas (a plume
  shows their sum; lowered for all, they lived ten times as many).
- **Drawing:** fixed-size wisps (4 m) besides the puff-sized billows, so a
  grown puff turns ragged, not a larger copy; erosion growing with age;
  the noise's contrast stretched (a dense puff's edge moved only a tenth of
  its radius: balls); a fringe over the eroded edge's first third (dense
  steam went opaque within a half-size pixel: stair steps); a softer
  profile ((1 - x^2)^3); each puff denser or thinner. The plume's surface
  for the shadow lookup and the composite: the puffs' opacity-weighted mean
  depth less their mean radius (the nearest dense puff jumped at each big
  puff's rim, drawing discs; a spread from half floats' sums of squares
  speckled). The sky's light per pixel: less deep in a puff, more at its
  edges and top and along the billows (in shade the plume was one flat
  grey); under clouds 0.8 of the air's light (0.5: slate grey).
- **July and January:** an env file without its own weather gets a derived
  preset (fair cumulus, 18 °C, dew point 9, every season). Smoke and steam
  now take their air as steam heating does (`Sim::OutsideAir`): the env
  file's own weather, else `Sim::ClimateTemperature` at the route's
  latitude by the date and the hour, 6.5 °C colder a km up, an activity's
  snow at 0 °C or below; a dew point near the night's lowest, lower in
  warmer seasons. A run's date is its MSTS activity's season's (USA2's
  nnmxdgds: winter, -6.8 °C, dew point -7.2; EUROPE2's oetutorial summer;
  EUROPE1's lclsrvce spring, 5.4 °C at 09:00), so with an activity the
  date set does not change the season. (The fog keeps the weather's air.)
- **Textures (ORTS):** not needed: the 3D noise is the texture, the same
  from every side as the camera moves (a sprite's picture is flat).
- **Cost:** in winter's near-saturated air a diesel's exhaust water hangs
  as vapour: the close Dash 9 view keeps some 600 puffs (100 in summer
  air) and costs some 2-3 ms over smoke and steam off on the Deck. Step 6.
- `TSRE_EXHAUST_DEBUG`'s crosses write depth: the puffs behind them are cut
  (debug only).
- **Windows over the smoke** (the user, 2026-11-01: carriage windows very
  dark): composited before the blended pass, a puff in front of a carriage
  had its windows drawn over it afterwards (their translucent texels write
  no depth), dark in a white plume. Now after the blended parts, the
  overlays and water, before glTF transmission (whose copy of the frame
  shows it). Checked with a temporary steam source in front of the 380's
  coach: the old order drew the window frames crisp through the steam, the
  new hides them. Smoke behind a window seen from a cab takes no tint of
  the glass (drawn over it).

## Open

- **Cost close up:** the four depth reads a pixel could be one from a
  half-size farthest-depth copy, the front pass drawn at a quarter size;
  measured with the limits (step 6). High's full-size colour pass costs
  some 3 ms close up.
- **The night:** no capture yet (an activity's clock sets the time): the
  lamps light the puffs (the light pass's `localLightsDiffuse`), the sky's
  night light; the firebox's glow later.
- Reflections and the environment's faces show no smoke; the glow (bloom)
  of a lamp behind it is not dimmed; the firebox's glow at night (later).

## Decided (the user, 2026-10-31)

1. **The approach:** C, then D for spread smoke; B kept as the option.
2. **The setting:** Off / Normal / High (the particle cap and the
   target's resolution), Normal by default. First Off in the OpenRails
   presets; but a new profile is OpenRails Extended (the settings suite
   checks it), so the two could not both hold, and Open Rails draws its
   emitters too: Normal in every preset (the user, 2026-10-31).
3. **The OpenGL renderer:** none for now; plain particles (A) there may
   come later, from the same simulation.
