# Task 15 - Shape Emitters (.sd)

## Objective

Smoke and steam from shapes, defined in the shape's `.sd` and drawn by the
exhaust particles of task renderer 41: chimneys, factory stacks, steam
vents and fires in the scenery, and emitters placed on a vehicle's shape.
The `.sd` is the shape's own file ([[no-side-files]]).

Status: steps 1-5 done (2026-11-06), reviewed and merged into main
d72b9bc7 (2026-11-06). Next: the binding to an ENG's emitters (Later). Branch `feature/shape-emitters` (worktree
`../TSRE5vc-shape-emitters`), from `feature/shape-lights` (task 14: the
placement on a matrix, `Rotation` / `QDirection`, `On`, the shape's root).

## Review: what our emitter takes

Task driving 05 (the interface) and task renderer 41 (the drawing).

**Where (per emitter, fixed: `VehicleDefinition::Emitter`, from the ENG's
`Effects` blocks):** its name as written (`Exhaust1`, `StackFX`,
`CylindersFX`...), its kind (the name in lower case without "fx";
`Exhaust1..3` are `exhaust`), its place (m, the vehicle's coordinates,
drawn through the shape's mirrored root), its unit direction (up where none
is given), its nozzle's diameter (m). In the file seven numbers:
`Exhaust1 ( 0 4.83 -3.683  0 1 0  0.2 )`.

**How much (per kind, every frame, from the simulation; `ExhaustOutput`,
SI):**

| Field | Meaning |
| --- | --- |
| `flow` | m³/s, the kind's whole, shared equally by its emitters; 0 off |
| `speed` | m/s at the nozzle |
| `temperature` | °C: how fast it rises; with the dew point, how long steam shows |
| `steam` | 0-1, the share that is water vapour; the rest smoke |
| `soot` | 0-1, how dark the smoke is |
| `beats` | puffs a second (a steam stack's exhaust beats); 0 an even flow |
| `color` | ARGB from the ENG (Open Rails' diesel colours); 0 the renderer's |

**Per emitter, in code:** `wetness`, the steam's liquid water (0.1 for
stack, cylinder, drain and heating kinds; 0 otherwise: a safety valve's is
clear for a hand's breadth).

**The renderer's own:** the air (wind, temperature, dew point from the
climate and the weather), the vehicle's speed (from its place last frame),
at most 30 puffs a second an emitter, at most 3,000 (Normal) or 10,000
(High) particles in all, emission within 2,000 m of the camera, QRhi only
(`core.rendering.exhaust`). The route view drives the particles' frames
every frame (`RouteEditorGLWidget::beginExhaustFrame`), the simulation
running or not, so the editor shows them.

**The test state's values by kind** (`testExhaust`, `TSRE_EXHAUST_TEST=1`):

| Kind | flow | speed | temperature | steam | soot | beats |
| --- | --- | --- | --- | --- | --- | --- |
| `exhaust`, `generator` (diesel) | 0.5 | 10 | 300 | 0.1 | 0.4 | 0 |
| `stack` (steam loco) | 2 | 30 | 150 | 0.8 | 0.3 | 4 |
| `safetyvalves` | 1 | 100 | 200 | 1 | 0 | 0 |
| `heating` (hoses) | 0.02 | 5 | 110 | 1 | 0 | 0 |
| others (steam) | 0.3 | 40 | 120 | 1 | 0 | 0 |

**Cost:** a puff is never smaller than a sphere of the nozzle's diameter,
so a house's chimney (0.05 m³/s through 0.4 m) gives about 1.5 puffs a
second, some 20-30 alive (longer in cold damp air); 50 chimneys some 1,000
to 1,500. The cost is per live puff, not per emitter.

**A puff's size** is no parameter: it follows from the gas. At the nozzle a
puff carries the flow over up to 30 puffs a second (a stack: over its
beats), never less than a sphere of the nozzle's diameter, its volume 0.5
to 1.5 times that at random; its radius that sphere's, the nozzle's at
least. It grows as it takes in the air about it: 0.12 x its speed through
the air (a jet's some 7°, so a fast exit widens it near the nozzle), the
air's stirring (0.04 m/s and 0.03 x the wind's speed, each puff's +-40 %)
and 0.15 x its eddy's speed; slowed to the wind, by the turbulence alone.
It is gone past 90 s, 40 m, or too thin to show (smoke by its soot, steam
by its liquid water); drawn as a soft sphere of eroded noise its radius
wide. So `Diameter`, `Flow`, `ExitSpeed` and `Beats` give the size: a
large stack is a wide nozzle with a large flow (no size field, which would
part the puffs from their gas; an override later if an effect needs it).

**Each emitter its own look:** the particle system keeps a source per
owner and emitter, its noise seeded from them, every puff's way, speed,
size, heat and eddy drawn at random, and the large gusts by place: emitted
per placed object, ten chimneys of one shape do not look alike.

**Open Rails reads an ENG emitter's seven numbers and skips the rest of its
block** (`ParticleEmitterData`, `SkipRestOfBlock`, master and unstable):
more inside it, a name or a `ShapeHierarchy ( ... )`, does not break Open
Rails (MSTS and TSRE's own reader to be checked in the binding task).

## Design: `ESD_TSREEmitter`

One block an emitter, in the shape block of its `.sd`, beside
`ESD_TSRELight`:

```
ESD_TSREEmitter (
	Name ( "CHIMNEY" )
	ShapeHierarchy ( "MAIN" )
	Position ( 2.1 7.4 -1.0 )
	Rotation ( 0 0 0 )
	Diameter ( 0.4 )
	Kind ( Exhaust )
	Flow ( 0.05 )
	ExitSpeed ( 2 )
	Temperature ( 120 )
	Vapour ( 0.3 )
	Soot ( 0.5 )
	Beats ( 0 )
	ColorRGBA ( 404040ff )
	On ( 1 )
)
```

| Field | Meaning | Default |
| --- | --- | --- |
| `Kind` | defaults for the rest: the kinds of the table above (`Exhaust`, `Stack`, `SafetyValves`, `Heating`, `Steam`), its wetness too | `Steam` |
| `Flow`, `ExitSpeed`, `Temperature`, `Vapour`, `Soot`, `Beats` | `ExhaustOutput`'s fields, overriding the kind's | the kind's |
| `ColorRGBA` | the smoke's colour, hex `RRGGBBAA` (as lights') | the renderer's |
| `Diameter` | the nozzle (m) | `0.2` |
| `ShapeHierarchy`, `Position` | as a light's: on a matrix (following animated parts), the shape's own coordinates; the root, its first matrix | the root, `0 0 0` |
| `Rotation` / `QDirection` | as a light's, the gas leaving along +y (up) before the turn | up |
| `Variation` | each placed object's flow within +- this share, fixed by its UiD | `0` |
| `On` | `0`: off until something switches it on | `1` |
| `Name` | its name (the Shape Viewer; the binding later) | none |

`Speed` and `Steam` are SIMIS tokens (other layouts): `ExitSpeed` and
`Vapour` instead. The other names are free.

**Scenery:** emitted per placed object (`StaticObj`), as displays are
pushed, not per shape state: each object its own source, its own puffs.

**Limits:** scenery's particles a pool of their own beside the trains',
each with its cap; scenery's emitters within the trains' 2,000 m, the
nearest first, the pool's cap doing the rest (decided).

**Shape Viewer:** the particle system stays one, global (read by the QRhi
pass, fed by `Eng::emitExhaust`), for the main view only (the user, first
version): in the route editor the route view emits, its child shape views
(the route editor's shape window, the consist editor, the materials
window) do not; the standalone Shape Viewer (its own process) emits in its
main view. There it drives the frames (cap, clock, still air at a mild
day's temperature and dew point) and emits the shown shape's `.sd`
emitters, and an engine's ENG emitters at the test state's values: a
preview of a loco's smoke without the simulation. A particle system per
view: later, if child views should show smoke.

**OpenGL renderer:** none, as train exhaust.

## Decisions (user, 2026-11-06)

1. The gas leaves along +y (up) before the turn.
2. `Kind` defaults with the physical fields overriding them.
3. Binding to an ENG's emitters: a follow-up task. Options: an `.sd`
   emitter named as an ENG emitter takes over its place (follows moving
   parts), the simulation's values of its kind driving it; or the ENG
   names the shape's emitter inside its block (Open Rails skips extra
   values there; MSTS and TSRE's reader to be checked).
4. Separate caps for trains and scenery.
5. Branch from `feature/shape-lights`.
6. The Shape Viewer renders exhaust on QRhi: the main view only in the
   first version (the global particle system kept): the route view in the
   route editor, not its child shape views; the standalone Shape Viewer's
   main view.
7. Sharing one emitter between objects (the user's concern: ten chimneys
   alike) is not done: each placed object emits its own.
8. Scenery's pool: 1,000 (Normal) / 3,000 (High) particles; emitters
   within 2,000 m as the trains', the nearest first, the pool's cap doing
   the rest (the user: no shorter distance); the trains' 3,000 / 10,000
   kept.
9. `Variation ( 0.3 )` in this task: each placed object's flow within
   +-30 %, fixed by its UiD, so a row of houses does not smoke evenly.
10. A puff's size stays the gas's (no size field).

## Steps

- [x] 1. Reader for both backends (`ShapeEmitters`, as `ShapeLights`):
  `ComplexShape::emitters()`, author messages; suite `shape-emitters`.
- [x] 2. Scenery: placed objects emit their shape's emitters (placed per
  part, the root's mirror); the scenery pool, its cap and distance; checks
  of the pools in `exhaust-particles`.
- [x] 3. The standalone Shape Viewer's main view: its frames and emissions
  (`.sd` emitters, an engine's ENG emitters at the test state); child
  shape views none.
- [x] 4. Verification (below).
- [x] 5. Guide (`docs/guides/shape-emitters.md` and its HTML edition) and
  examples (`docs/examples/shape-emitters`).

### Implementation (2026-11-06)

- `ShapeEmitters` (`src/tsre/shape/ShapeEmitters.{h,cpp}`): `Emitter`
  (name, matrix, kind, place, way, diameter, `ExhaustOutput`, wetness,
  variation, on); one field reader for both backends (`readBlock`,
  `read`); a kind's defaults from `testExhaust` with the fields given
  overriding them in any order; `ColorRGBA`'s `RRGGBBAA` turned into the
  output's ARGB; problems named by the emitter; author messages once a
  shape's load (`ComplexShape::firstReport`). `exhaustWetness` moved from `Eng.cpp` to `CarExhaust` (shared).
- `ComplexShape::emitters()` and `emitterProblems()`: `SFileLegacy`
  (`loadSd`, swapped in `adopt`), `SFileComplex` (`loadMetadata`).
- `ShapeEmitters::giveOut`: a placed object's emitters into the scenery's
  pool, within 2,000 m of the camera; each through its part as posed (the
  root its first matrix, x mirrored as the shape's vertices); the flow by
  `Variation` from the object's UiD (none for uid 0: the Shape Viewer's,
  the shape's own flow); owner the object, so each object its own sources
  and puffs. (Named `giveOut`: `emit` is a Qt keyword.)
- `WorldObj::giveOutEmitters`, from `WorldObj::pushRenderItems` and
  `StaticObj::pushRenderItems`, gives out before their culling returns: a
  plume behind the camera or past the shape's distance levels is there
  when the camera turns. Static objects, gantries, collide objects,
  pickups, signals, speed posts, level crossings and hazards emit; track
  sections, dynamic track, forests, transfers and car spawners (their own
  drawing) do not.
- `ExhaustParticles`: two pools (`Pool::Trains`, `Pool::Scenery`), each
  its cap and count; `setLimit(int)` and `limit()` stay the trains'. A
  full trains pool replaces its oldest puff; a full scenery pool replaces
  the puff of its farthest emitter (`Particle::sourceDistance`), or drops
  the new one when its own emitter is farther. `setQuality` sets both
  caps from `core.rendering.exhaust` (trains 0 / 3,000 / 10,000, scenery
  0 / 1,000 / 3,000), for the route view and the Shape Viewer.
- Shape Viewer: `ShapeViewerGLWidget::setExhaust`, set by
  `ShapeViewerWindow` (the standalone viewer) only, which reads the
  setting then and when it changes; its frames (caps, clock, still air of
  15 °C and an 8 °C dew point), the shown shape's emitters, an engine's
  (`Eng::emitExhaust(model, preview, owner)`: the test state's values) and
  a consist's (each car its own source, the same engine twice too);
  another item shown drops the last one's smoke; views without the flag
  set the renderer's exhaust quality to 0 (they drew the route view's
  particles in their own space before). `Renderer::setViewDirections`:
  the viewer draws without view bands, so the exhaust pass had no view
  directions (the sky inverse, set by a sky band); QRhi keeps the view's
  own (`viewInverse`), the sky's, fog's, clouds' and light shafts' passes
  still seeing no sky. Its Hierarchy window lists the emitters on their
  matrices (`ShapeEmitters::partOf`, as drawn). The viewer capture harness takes
  `"exhaust": true`.
- Messages in both translations (8).

### Verification (2026-11-06)

- `shape-emitters` suite, 13 checks: the blocks read in both backends
  alike (values given over a kind's, `ColorRGBA` as ARGB, a kind's
  defaults and wetness, `Rotation` and `QDirection` turning up, an unknown
  kind as `Steam`, both turns, a colour not hex, `On ( 0 )`); a placed
  object's chimney emitting into the scenery pool from its part (6 m up:
  PART 5 m, 1 m above it), none past 2,000 m; two objects' `Variation`
  unlike; four author messages.
- `exhaust-particles` 18 (2 new): the pools' caps apart, a full scenery
  pool keeping its near emitter's puffs, the scenery's cap lowered
  without touching the trains'.
- EUROPE1 (QRhi Vulkan): six `tsre_smoke_demo` objects in a row
  (`tests/renderer/emitters`), plumes rising at 12:00 on 2026-07-15 and
  at 08:00 on 2026-01-15.
- Standalone Shape Viewer (QRhi): `cottage3.s` with an emitter on each
  chimney pot smoking; the Dash 9 previewing its ENG exhaust.
- No change without emitters: against the branch's base
  (`feature/shape-lights`'s build, one `--settings` file) EUROPE1's six
  parity views and the 33 Khronos samples pixel-identical.
- ctest and the suites `shape-lights`, `shape-materials`, `gltf-pbr-gl`,
  `local-lights`, `eng-lights`, `driving`, `vehicle-file`, `selection-id`,
  `rhi-shaders`, `cabview`, `simulation-ui`, `displays` (Xvfb) pass.
- Found, not of this task: the QRhi Vulkan validation layer reports a
  texture "used with different accesses within the same pass" every frame
  the exhaust pass draws, on main's build too (USA2's diesel capture: 123
  times); for the exhaust renderer's work (task renderer 41).

### Review before merging (2026-11-06)

A review of `feature/shape-lights` and this branch before merging them
(the user's request), by a reviewer agent and by hand:

- On main first, from the user's finding: the legacy backend did not read
  a reloaded shape's `.sd` when it loaded in place (threaded shape loading
  off, or `load()` on it; the loader's thread reads a fresh copy), and
  author messages were told once a session. Fixed on
  `fix/legacy-sd-reload` (main 8c2c7f1d): `loadSd()` reads afresh,
  `reload()` clears `loadedSd`; `ComplexShape::firstReport` /
  `forgetReports`, told again after a reload. Main merged into this
  branch.
- Here, with it: `loadSd()` clears the lights and emitters too (an in-place
  reload added them again: 18 emitters after two); the lights', emitters'
  and ENG lights' messages on `firstReport`.
- The Shape Viewer: `setViewDirections` set the sky inverse as valid, so
  light shafts (their gate checks only that; the default sky's sun
  straight up) could start running in the viewer on exhaust frames: now
  the view's own directions. uid 0 took `Variation`'s least flow (all of
  it off at `Variation ( 1 )`): none for uid 0. A consist with the same
  engine twice smoked from its first only (one `Eng`, one source): each car
  its own. The Hierarchy window took the last matrix of a name, the drawing
  the first: `partOf`. The setting read each frame (against the settings'
  rule): read once and when it changes.
- Scenery emitters from every object kind drawn through `WorldObj` (they
  were `StaticObj`'s only: no pickup, signal or level crossing smoked).
- Simpler: the parts' matrices cache dropped (every placed object has its
  own state, so it never served two objects; a few emitters' matrices cost
  nothing); the caps in `ExhaustParticles::setQuality`, not twice; stray
  comments in `Eng`.
- Guide: which objects smoke; a car's shape emitters give out nothing yet
  (it said they lacked the simulation's values); messages again after a
  reload (the lights' guide too).
- Left as it is: a full scenery pool scans its puffs for the farthest
  emitter's at each new puff, as a full trains pool scans for its oldest.
- Checks: `shape-emitters` 15 (2 new: uid 0 gives the shape's own flow,
  as the same shape without `Variation`; a legacy shape reloaded in place
  twice keeps one of each emitter and light and tells its messages again,
  failing without the new clears: 18 emitters, 2 lamps); ctest and the
  suites above, `gltf-lod`, `cab3d`, `car-animation`, `shape-animation`,
  `shape-complex(-gl)` pass. The viewer's captures (cottage pots, Dash 9)
  with light shafts at volumetric differ from those without as little as
  two runs without do (about 50 pixels of the puffs' noise); EUROPE1's
  smoke demo plumes as before.

## Later

- The binding to an ENG's emitters (decision 3).
- glTF: emitters in node extras.
- Switching scenery emitters (`On ( 0 )`) by time, scripts or events.
- The OpenGL renderer's plain particles (task renderer 41's A).
