# Task 26 - Signal Lights

## Objective

Draw the lights of MSTS and Open Rails signals in the route editor. There
are no signal scripts, so every signal head shows its default aspect: the
most restrictive one its signal type defines. On the QRhi renderer with
local lights on the lights are emissive and glow through the bloom (task
24); elsewhere they are plain discs in the light's colour.

## Decisions (user, 2026-10-09)

| Topic | Decision |
|---|---|
| Parser | A new `sigcfg.dat` reader on `SimisTextReader`, as `SFileComplex` reads shapes, replacing the `ParserX` one. |
| Default aspect | The most restrictive aspect of the signal type. |
| First version | Steady lights only: no flashing, no fading on and off, no semaphore arm animation. They are future work, below. |
| Glow | The renderer's own bloom; no Open Rails glow quads growing with distance. |
| Daylight | Signal lights do not follow the lamps' daylight rule (3% of their glow by day, `Daylight::DayLocalLights`): their own scale keeps at least 50% by day. |
| Legacy and lights off | The OpenGL renderer, and QRhi with local lights off, draw a simple disc in the light's colour: no bloom imitation. |

## Review of the old parser (2026-10-09)

Compared with Open Rails (`Orts.Formats.Msts/SignalConfigurationFile.cs`):

- Read: signal shapes and sub-objects (the editor uses them), signal
  lights (index, name, position, radius), draw states and their lights.
- Skipped: `LightTextures`, `LightsTab` (colours), `SignalAspects`,
  `SigFlashDuration`, signal type flags (`SEMAPHORE`, `ABS`, `NO_GANTRY`),
  `SemaphoreInfo` and `SemaphorePos`, the light flag `SEMAPHORE_CHANGE`,
  and the Open Rails keys (`ORTSDayGlow`, `ORTSNightGlow`, `ORTSDayLight`,
  `ORTSOnOffTimes`, `ORTSSignalLightTex`, `ORTSSignalFunctions`,
  `ORTSNormalSubtypes`, `ORTSScript`, `ScriptFiles`,
  `ApproachControlSettings`).
- Wrong: `SignalFnType` and `SignalLightTex` were stored as the type's
  name, so signal types were keyed by their light texture; names were case
  sensitive where Open Rails lowercases them; lights without `Position` or
  `Radius` kept garbage; duplicate draw state names overwrote each other;
  only the first, uppercase `FLASHING` flag counted; sub-object indices
  were not range-checked; only `_info` and `_skip` were comments, where
  Open Rails ignores any block named `skip`, `comment` or starting with
  `#` or `_`.

## Design

### Parser and data

`SigCfg` keeps its public interface for the editor (signal shapes by name
and list position, `findSignalShape`, `loaded`, `sourceFileExists`, the
sub-object fields). Underneath:

- **Reading**: the file's bytes (compressed `SIMISA@F` files inflated),
  decoded and read with `SimisTextReader`. Unknown tokens and comment
  blocks are skipped; errors in one block skip that block and are logged,
  the rest is kept. A file that cannot be read at all reports an error, as
  before.
- **Names**: signal types, lights, light textures, light table entries and
  draw states are matched in lower case, as in Open Rails.
- **Everything Open Rails reads is kept**, also what the first version does
  not use (flashing, semaphores, speeds, approach control, Open Rails
  functions and subtypes), so later steps need no parser work:
  - light textures: name, file, UV corners;
  - light table: name, ARGB colour;
  - signal types: name, function (MSTS or Open Rails), Open Rails normal
    subtype, light texture, flags, flash on and off times, `ORTSOnOffTimes`,
    `SemaphoreInfo`, `SignalNumClearAhead`, day and night glow,
    `ORTSDayLight`, script name, request stop distances, approach control;
  - lights by index: name, position, radius, `SEMAPHORE_CHANGE`, own light
    texture;
  - draw states by index and name: lights with their flashing flag,
    semaphore position;
  - aspects: aspect, draw state, speed, flags;
  - signal shapes and sub-objects as today; script files.

### Default draw state

For a signal type: the aspect with the lowest rank among those it defines
(`STOP` < `STOP_AND_PROCEED` < `RESTRICTING` < `APPROACH_1` < `APPROACH_2`
< `APPROACH_3` < `CLEAR_1` < `CLEAR_2`; unknown aspects ignored) gives the
draw state. A type without aspects shows its draw state with the lowest
index.

### Placing the lights

- A signal's heads are its enabled signal units whose sub-object is a
  `SIGNAL_HEAD` with its bit set in `SignalSubObj`. The sub-object's
  `SigSubSType` names its signal type.
- The sub-object's name is a matrix of the shape. Shapes get a query for
  the transform of a named matrix with its parents, in the static pose
  (`SFileLegacy` and `SFileComplex`), in the same space as their vertices.
- Each light of the default draw state is a disc of the light's radius at
  its position in the head's frame, facing along the head, a little in
  front of it. The lights are built once the shape has loaded, and again
  when the shape or the enabled sub-objects change.

### Drawing

- One shared disc mesh; each light is a packet with its own transform and
  colour, submitted with the signal's selection id (clicking a light
  selects the signal). Lights cast no shadows.
- **QRhi, local lights on**: an unlit, emissive material in the light's
  colour (`LightsTab`), bright enough for the bloom. The light also brings
  an emitter, so distant signals keep a steady glow (glow splats, task 24).
  Its glow is scaled by the signal daylight scale: 50% by day, 100% at
  night, with the same transition as the lamps' (`Daylight`), and 100%
  when time of day is off. The emitter faces the way the lens does (a
  spot with a cone of 90 degrees): its glow splat fades by the cosine off
  the lens's axis and its light in fog (task 33) by the cosine squared,
  none from behind (user, 2026-10-17: signals seen from behind showed
  their aspect from afar).
- **OpenGL, or local lights off**: an unlit disc in the light's colour.

## Steps

1. Parser: the new reader and data; tests against CMK, PeakRail and the
   route template's `sigcfg.dat`; the editor unchanged.
2. Signal lights: matrix query, default draw states, packets, the two
   material paths, signal daylight scale; captures of a station at day and
   night.

## Verification

- Parser suite: counts of signal types, lights, draw states, aspects,
  shapes and sub-objects of CMK and PeakRail; types keyed by name; light
  colours and textures; comment blocks skipped; a malformed block does not
  lose the rest; the existing signal and TDB suites unchanged.
- Captures on QRhi and OpenGL, day and night: lights on the right heads,
  in the default aspect's colours, on the front of the heads only.

## Implementation status (2026-10-09)

| Step | Commit | Notes |
|---|---|---|
| 1. Parser | `594511e` | `sigcfg` suite; CMK 82 types, 192 shapes, 401 heads all with a type, 205 lights all with a colour; PeakRail and the route template clean. A truncated file still fails to load (`tdb-load`). |
| 2. Signal lights | this commit | `SignalLights`, `ComplexShape::matrixByName`, `Daylight::signalLights`, per-item `pbrGlowScale`. |

- **Placement** follows Open Rails: a light at sigcfg (x, y, z) sits at
  (-x, y, -z) in the head's MSTS frame, 1.5 cm towards -z. Captured on CMK
  (Warszawa Wschodnia, `PS_5k_BC.s`, tile -5308, -14961): the red light of
  the STOP aspect on the head's front, nothing from behind, an edge from
  the sides.
- **Discs are double-sided**: the shape's mirrored root turns the winding
  over; the head hides them from behind.
- **Brightness**: emission 4 times the light's linear colour, glow at the
  signal scale (50% by day). Midday: lit with a small glow; night: glowing
  through the bloom. OpenGL and local lights off: plain discs.
- Heads without a TDB link (signal unit) show no lights, as in Open Rails.
- The sigcfg.dat radii are often larger than the lamp openings of the
  shapes, whose hoods and lenses then cover part of the discs (see future
  work).

## Rework (user, 2026-11-17)

Users were not happy with the lights drawn alike at every distance: close
up the light leaked out of the bulb's opening in the signal shape, far away
it could be seen a little better, and seen from behind it leaked through the
back of the head. Reviewed against Open Rails
(`Viewer3D/Signals.cs`, `SceneryShader.fx` `_VSSignalProjection`) and
captured on OR_PSK_NEW PRUDNIK (`PS_5k_A`, QRhi Vulkan, local lights on,
auto exposure, tone mapping, bloom 3).

What Open Rails draws:

- The light: a square of the sigcfg radius with the light texture
  (`LightTex`, the light's `ORTSSignalLightTex`, else its type's
  `SignalLightTex`) over its UV rectangle, mirrored along x, cut out by the
  texture's alpha and blended, tinted by the light's colour (the texture's
  red blends towards it), 1.5 cm in front of the head; never sized by
  distance. PRUDNIK's `siglight.ace` is a lens filling only the middle of
  its square: the lens is about half the radius wide. `siglight_w24.ace` is
  a slanted bar.
- The glow: a second square 3 cm in front with `SignalLightGlow.png` (a
  soft blob, alpha 23 % at most), none within 100 m, beyond scaled by
  `ln(1 + (d - 100) / 40)` times 3 by day, 5 by night (`ORTSDayGlow`,
  `ORTSNightGlow`): 6 times the radius at 200 m, 16 at 1 km by night. Its
  forward offset scales with it.
- Seen from behind neither is drawn (back faces culled).

What TSRE drew: a disc of the full sigcfg radius, opaque, double-sided,
1.5 cm in front: on `PS_5k_A` 0.42 m wide where its lights are 0.35 m apart,
so it covered the opening's edge and its hood cut a dark arc into it.

Found on the way: glow splats blended the view's targets differently (the
colour and the ambient share untouched, the glow added), which needs
Vulkan's `independentBlend`; lavapipe applied the first target's blend to
all, so no glow splat reached the bloom there (signals' and lamps' alike;
AMD and NVIDIA drew them). Fixed on main (`58b5cac2`): one additive blend for
every target, the shader adding nothing but the glow; a fourth blend for the
velocity target (TAA, motion blur), as the pass has.

Changes:

- **Light textures.** A light is a square of its radius with its light
  texture, as Open Rails lays it (`SignalLights::lightTextureUv`), cut out at
  alpha 0.5, the texture's colour times the light's (emissive with local
  lights, unlit without). The texture is the route's (`TEXTURES` beside
  `sigcfg.dat`), else `GLOBAL/TEXTURES`; until it is loaded, or without it,
  the disc as before. The glow splat keeps the disc's energy: Open Rails'
  glow is the same whatever the texture, and the far glow stays as bright
  as before (with the lens's own share, a fifth for `siglight.ace`, it was
  dimmer at 300 m than before); close up the halo is as before (only within
  about 3 m, where the lens takes the glow over, is it brighter than the
  lens's own).
- **Seen from the front only.** The square faces -z as the lens does
  (`DiscMesh::sharedQuad`), single-sided, its winding turned by the shape's
  mirrored root (`mirrored`). The disc fallback stays double-sided.
- **Forward offset growing with distance.** `SignalLights::frontOffset`:
  1.5 cm plus 6e-7 d^2 m (twice the depth buffer's step there: 32-bit float
  depth, 0.2 m near plane), a metre at most (from 1.3 km): 2 cm at 100 m,
  7 cm at 300 m, 23 cm at 600 m, 62 cm at 1 km. The signal gives its
  distance from the camera (`SignalObj`). Not seen from the front on
  lavapipe nor by the user; a margin for GPUs' coarser depth.
- **Far glow, slightly more.** A signal's glow splat grows beyond 100 m by
  Open Rails' log, its energy by the square: `(1 + k ln(1 + (d - 100) /
  40))^2`, k 0.5 (`TSRE_SIGNAL_GLOW_GROWTH` for trying others, 0 none):
  2.0 times at 150 m, 3.6 at 300 m, 5.3 at 600 m, 6.7 at 1 km. Night, the
  reddest pixel at the signal on a straight approach (PRUDNIK node 3321,
  `PS_5k_A`): 150 m 166/34/36 before, 198/55/59 now; 300 m 97/11/17,
  161/33/48; 600 m nothing, 100/21/47; 1 km nothing, 70/15/44. By day
  300 m a little more, 600 m nothing either way. k 1.0 was tried: stronger,
  and the blue shunting signals' halos with it.

Not reproduced on lavapipe: the leak through the back (both `PS_5k_A` and
the shunting `PS_2k_Tm2`, 6 to 400 m, on the axis and aside); the user's
GPU rasterizes depth less precisely. The front-only square removes it
whatever the precision.

Not checked: W24 (`ltex_w24`, the slanted bar): its type's aspect names
the first of its two `W24` draw states, the dark one, so it shows no light
by default.

## Future work

- **Signals' light, a rework** (user, 2026-10-19; to come back to):
  - By day not visible enough.
  - The glow's size should scale with the distance (done 2026-11-17: its
    energy grows beyond 100 m, see Rework).
  - Light on the surroundings: today a signal lights nothing (its emitter
    is skipped for the lamps' grid); it lights the fog only, through a grid
    of its own (task 33) at 50 times the lens's even emitter (a lens
    gathers its light into a beam along the track), at the signal scale
    (the exposure undone: halos alike by day and by night). By night a real
    signal tints what is close in front of it: its hood, the mast or
    gantry, the ballast and rails some metres ahead. Reviewed: the lamps'
    grid takes spot lights (cones) already; a signal as a spot along its
    lens's axis (the fog's 90 degree cone, the cosine squared), at the
    beam's gain, would light a surface 1 m in front with about a tenth of
    the sun's light, 1 % at 3 m; at the lamps' scale (none by day). The
    signals keep their own grid (user: it needs more work anyway), so the
    surface light goes to the lamps' grid with the fog leaving it out
    there, or the lit shaders read the signals' grid too. Train lamps'
    emitters, signal-like, the same.
- Flashing lights (`SigFlashDuration`, `FLASHING` draw lights).
- Fading on and off (`ORTSOnOffTimes`).
- Semaphore arms posed and animated by draw state (`SemaphorePos`,
  `SemaphoreInfo`, `SEMAPHORE_CHANGE` lights dark while the arm moves).
- Previewing any aspect from the signal's properties.
- Light textures blended at their soft edge, as Open Rails blends them
  (cut out at alpha 0.5 now).
- `ORTSDayLight false` (lights off by day) and the Open Rails day and night
  glow values.
- Signal scripts and aspects from the route's state.

## Draw states sharing a name (user, 2026-10-09)

CMK has 109 draw states whose name an earlier state of the same signal type
already has, different states under one name: `PKP_SEM_4k_A` has `S5`
steady yellow (index 3) and `S5` flashing yellow (index 4), and its aspect
`APPROACH_2` names `S5`. Most likely a mistake of the script authors that
works in MSTS, which looks names up through the array and takes the first;
Open Rails keeps names in a map, so it renames the later ones (`DST<n>`).

The reader keeps both states as written, with their indices, and a name
finds the first, as in MSTS. Each such name is logged as a warning (the
route log shows the first 20), pointing out the duplicate.
