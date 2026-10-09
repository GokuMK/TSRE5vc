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
  when time of day is off.
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

## Future work

- Flashing lights (`SigFlashDuration`, `FLASHING` draw lights).
- Fading on and off (`ORTSOnOffTimes`).
- Semaphore arms posed and animated by draw state (`SemaphorePos`,
  `SemaphoreInfo`, `SEMAPHORE_CHANGE` lights dark while the arm moves).
- Previewing any aspect from the signal's properties.
- Light textures (`LightTex`, `ORTSSignalLightTex`) instead of plain discs.
- `ORTSDayLight false` (lights off by day) and the Open Rails day and night
  glow values.
- Signal scripts and aspects from the route's state.
