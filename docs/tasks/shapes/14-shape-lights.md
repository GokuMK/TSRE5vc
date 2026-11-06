# Task 14 - Shape Lights (.sd)

## Objective

Lights for S shapes, defined in the shape's `.sd` and drawn exactly as glTF
punctual lights are: point and spot lights placed on a matrix of the shape,
moving with it, lighting the scene through the light grid (task renderer
21). Content gets lamps on MSTS shapes without converting them to glTF, and
without side files ([[no-side-files]]: the `.sd` is the shape's own file).

Status: steps 1-5 done (2026-11-05) and the ENG binding (2026-11-06),
merged into main d72b9bc7 with task 15 (2026-11-06). A light's
`ViewDistance` and the `.sd`'s `ESD_TSREDistanceLevelExtension`: task 16.
Next: ENG lights placed on a matrix (Later).

## Review

### TSRE's .sd blocks

- Read by both MSTS backends: `SFileLegacy::loadSd` reads the shape block
  with `ParserX`; `SFileComplex` from the parsed document
  (`SFileDetail::Node`, `SFileDocument`). `ESD_TSREMaterial` (task 13) and
  `ESD_TSREDisplay` (task scripting 02) are TSRE's own blocks.
- `ESD_TSREDisplay` is the model to follow: one block per display,
  sub-blocks by name, one reader for both backends (`ShapeDisplays::field`
  over a token stream or a node), `ComplexShape::displays()` and
  `displayProblems()`, translated author messages for blocks left out
  (`leftOut`). A square hangs on a matrix: `ShapeHierarchy` (empty: the
  root), `Position` (m, in that matrix's space), `Rotation` (degrees about
  x, then y, then z; facing +z before it), placed per state from the posed
  part (`ComplexShape::partMatrix`), so it follows animation.
- The user's rule for names: a name in `TS.cpp`'s binary token table keeps
  that token's value layout. Relevant ones: `Position` (3), `Radius` (1),
  `Type` (1), `Name` (1 string), `LightColour` (1 ARGB hex, ENG `Lights`),
  `Angle` (1), `Azimuth`/`Elevation` (3), `Colour` (a r g b in shapes),
  `Direction` (1 number in activities: not usable for a vector). Free:
  `Rotation`, `Intensity`, `Range`, `Cone`, `ShapeHierarchy`.

### TSRE's glTF lights and their path

- `GltfShape` reads `KHR_lights_punctual` point and spot lights: colour,
  intensity (candela), range (0: none), inner and outer cone half-angles
  (defaults 0 and pi/4); directional lights skipped, `name` ignored.
  Positions and directions (the node's -Z) from node world matrices fixed at
  load (TSRE has no glTF animation); the most detailed level's lights at
  every level; radius 0.05 m.
- Lights travel with packets (`RenderItem::lights`, model space before
  `msMatrix`); glTF's ride a packet without a mesh. Every queued instance
  brings its lights with its transform (`QueueRenderer::gatherLights`).
- QRhi with local lights on: binned into the world-space grid (64 x 64 x
  16, at most 64 per cell, brightest first), shaded on PBR, MSTS shapes and
  terrain; I / (d^2 + r^2) to the range or where the light falls under
  1/512; `localLights.exposure`; the daylight rule (3 % by day, full at
  night and under cover); the fog volume lit by them; the W file's
  `TSRELightRange` (per object) lengthens the reach. No shadows.
- OpenGL renderer: no local lights at all; packet lights are ignored.
  Signal and train lamps are plain discs there (their placeholders).

### Open Rails (`upstream/unstable` 74da4d47a, the gltf2 pull request)

`master` has no glTF and no local lights; only the unstable merge has
them (`Viewer3D/GltfShape.cs`, `RenderFrame.cs`, `Lights.cs`,
`SceneryShader.fx`).

- `KHR_lights_punctual` with `type`, `name`, `color`, `intensity`, `range`
  (default infinite; 0 becomes 2000), `spot` cones. Directional lights read
  but dropped (the sun's slot). One mesh-less primitive per light node, on
  the hierarchy, so node animation moves lights (with `GltfAnimations`,
  off by default, no menu entry).
- `KHR_animation_pointer` on light colour, intensity, range and cones
  (multiplying the static values; intensity clamped to 1).
  `KHR_node_visibility` hides a node with its light. Supported list also
  has `EXT_lights_image_based`, `MSFT_lod`, `KHR_materials_variants`, ASOBO
  and MSFT packing extensions.
- Extras: `OPENRAILS_light_name` on a light; `OPENRAILS_node_day_night_switch`
  (a node, its light too, only at night).
- Headlights: an ENG `Light`'s `ShapeHierarchy` naming a glTF light's
  `OPENRAILS_light_name` binds that light: off by default, switched by the
  ENG cone's conditions and fades, keeping the glTF light's own type,
  colour, intensity, range and cone. Otherwise the first lit cone is a
  "Headlight" (intensity 4, linear falloff, range 4 x the cone's).
- Units: no photometric conversion; point lights x 0.08 and spots x 1 "by
  visual inspection"; attenuation 1/d^2 x clamp(1 - (d/range)^4); cones by
  smoothstep; by day intensity x 0.5 and range x 0.1. A glTF lamp tuned
  in one program is not the same in the other.
- Forward shading, every pixel loops over every light (no cap, no grid);
  no shadows from local lights.
- `.sd`: no light tokens; OR reads `ESD_Detail_Level`,
  `ESD_Alternative_Texture`, `ESD_No_Visual_Obstruction`, `ESD_Snapable`,
  `ESD_SubObj` (night sub-object), `ESD_Bounding_Box`,
  `ESD_ORTSSoundFileName`, `ESD_ORTSBellAnimationFPS`,
  `ESD_ORTSCustomAnimationFPS`. `.sd` lights are TSRE's only, as
  `ESD_TSREDisplay` is.

### glTF: what TSRE lacks (nice to have)

1. Light names (for switching, and OR's `OPENRAILS_light_name` binding).
2. Lights on animated nodes: needs glTF animation (task shapes 12 maps
   glTF channels onto parts later).
3. `KHR_animation_pointer` on lights (flashing beacons, crossings).
4. `KHR_node_visibility`, and a night-only switch (OR's extra).
5. `EXT_lights_ies` (photometric profiles of real lamps).
6. Shadows from local lights (renderer-wide, task 21 Later).
7. Directional lights: not needed for shapes (both programs drop them).

## Proposal: `ESD_TSRELight`

One block per light, in the `.sd`'s shape block, beside
`ESD_TSREDisplay`. The values mean what glTF's mean; the placement is the
display square's.

```
ESD_TSRELight (
    Name ( "LAMP1" )
    Type ( Spot )
    ShapeHierarchy ( "MAIN" )
    Position ( 0 8.95 1.51 )
    Rotation ( -90 0 0 )
    Color3F ( 1 0.8 0.6 )
    Intensity ( 270 )
    Range ( 40 )
    Cone ( 30 85 )
    Radius ( 0.1 )
)
```

| Field | Meaning | Default |
|---|---|---|
| `Type` | `Point` or `Spot` | `Point` |
| `Intensity` | candela, as glTF; scaled by the exposure setting | required |
| `Color3F` | linear red, green, blue, 0 to 1: glTF's `color` | `1 1 1` |
| `ColorRGBA` | the same as hex `RRGGBBAA`, sRGB as colour pickers give it; alpha multiplies the intensity, as an ENG lamp's | `ffffffff` |
| `Range` | m, as glTF: the light ends there; none: where it fades out (the object's `TSRELightRange` as today) | none |
| `Cone` | spot: inner and outer half-angles from the axis, degrees | `0 45` |
| `ShapeHierarchy` | the matrix it hangs on, moving with it | the root |
| `Position` | m, in that matrix's space | `0 0 0` |
| `QDirection` | rotation quaternion `x y z w`, the W file's layout (a glTF light node's `rotation` with y and z negated, the S shape being mirrored) | `0 0 0 1` |
| `Rotation` | the same in degrees about x, then y, then z, as display squares | `0 0 0` |
| `Radius` | m, the lamp's size: softens close surfaces, the disc's radius | `0.05` (as glTF lights) |
| `Name` | the light's name: shown in the Shape Viewer; ENG binding a follow-up | none |

- Direction as glTF's: the light shines along -z of its matrix, turned by
  `QDirection` or `Rotation`; the user's lamp file's `-0.7071 0 0 0.7071`
  (straight down) is `Rotation ( -90 0 0 )`. Positions are in the S shape's
  own coordinates (corrected 2026-11-06, see the binding's implementation):
  a glTF light node's `translation` is the `Position` with x negated, its
  `rotation` the `QDirection` with y and z negated.
- `Color3F` or `ColorRGBA`, `QDirection` or `Rotation`: one of each pair;
  with both, the later in the block wins, with an author message.
- A block without `Intensity`, or with an unknown `Type`, is left out with
  an author message, as displays are. A `ShapeHierarchy` that is not one of
  the shape's matrices hangs the light on the root, with a message.
- Token rule: `Position`, `QDirection`, `Radius`, `Type` and `Name` keep
  their token layouts (3, 4, 1, 1, 1 string); the other names are free.

### Drawing

- Read by one reader for both backends (`ShapeLights`, as
  `ShapeDisplays`), kept as `RenderItem::Light` values in its matrix's
  space; `ComplexShape::lights()` and `lightProblems()`.
- Pushed per state: a packet without a mesh per matrix holding lights,
  `msMatrix` the posed part's (as squares), so lights follow animation and
  ride instancing like glTF's. The light grid, fog, daylight rule, exposure
  and reach are unchanged: an `.sd` light and a glTF light with the same
  values light the scene identically.
- OpenGL, and QRhi with local lights off: a plain disc in the light's
  colour, `Radius` across, at its place facing its way, double-sided and
  unlit, as signal and train lamps get there (their disc code reused);
  glTF lights get the same disc, as both come from packet lights.
- Names: an `.sd` light's `Name`; a glTF light's `name`, or Open Rails'
  `extras.OPENRAILS_light_name` when given (the name its ENG binding uses).
  Kept with the shape's lights, listed in the Shape Viewer.

### Steps

- [x] 1. Reader for both backends, `ComplexShape::lights()`, author
  messages; glTF light names; suite `shape-lights` (fields, defaults, both
  pairs' precedence, both backends equal, problems).
- [x] 2. Push from both backends; the Shape Viewer lists a shape's lights
  (name, type, matrix). Suite: packets carry the lights in their part's
  space; a posed part moves its light.
- [x] 3. The disc where local lights are off or absent (OpenGL, QRhi with
  local lights off), for `.sd` and glTF lights.
- [x] 4. Verification content: instead of a stock lamp post, the glTF test
  lamp without its light and a light-only S shape carrying its light
  (below), so the same geometry compares pixel for pixel.
- [x] 5. Guide (`docs/guides/shape-lights.md`, its HTML edition
  `shape-lights.html` in the geo guides' style, linked from the other
  guides' pages and the index) and example (`docs/examples/shape-lights`).

### Implementation

- `ShapeLights` (`src/tsre/shape/ShapeLights.{h,cpp}`): `Light` (name,
  matrix, type, place and way in its matrix's space, linear colour,
  candela, range, radius, cones); `readBlock` (legacy, `ParserX`) and
  `read` (complex, `SFileDetail::Node`) share one field reader, as
  `ShapeDisplays`; problems named by the light's name. A parsed `.sd`
  gives a hex value as a number without its leading zeros: `ColorRGBA` is
  read as eight digits, padded.
- `ComplexShape::lights()` and `lightProblems()`: `SFileLegacy` (`loadSd`,
  swapped in `adopt`), `SFileComplex` (`loadMetadata`), `GltfShape` (its
  punctual lights with their names: `extras.OPENRAILS_light_name`, else
  `name`; in the shape's space).
- `ShapeLights::Drawn`, a member of each shape outside the loaded data
  `adopt` swaps (a reload's new lights rebuild it): per state a packet
  without a mesh per matrix holding lights and, where the scene has no
  local light, a disc per light. A state's packets are shared by the
  objects drawing it (instanced); their matrices are taken once a frame
  (`Renderer::frameNumber`) from `ComplexShape::partMatrix`, which builds
  the state's posed matrices on each call (the draw paths themselves use
  the static states' cached matrices). Both MSTS backends push it with
  their packets (the complex one before its shared packets' early return),
  never when picking parts; `GltfShape` pushes discs only, its own packet
  carrying its lights.
- Packet values as glTF's: intensity in candela over pi (task renderer 21's
  units), cones as half-angles, radius 0.05 by default. Discs: the shared
  `DiscMesh` (V layout), unlit, double-sided, the light's colour at full
  strength, casting no shadows.
- Shape Viewer: the Hierarchy window lists the lights under their matrices
  (an S shape's root lights under its first matrix since 2026-11-06; a glTF
  shape's at the top level), with type and candela.
- Author messages once a shape (`ErrorMessagesLib`, as displays'): the
  `.sd`'s problems, a `ShapeHierarchy` the shape has not.

### Verification (2026-11-05)

- Suite `shape-lights` (26 checks, in `all`): reading in both backends
  (values, defaults, `Rotation` and `QDirection` down, `ColorRGBA` with
  leading zeros and its alpha, the later of each pair, two blocks left out,
  a cone clamped, a matrix the shape has not), packets per matrix in both
  backends with the light in candela over pi, a posed part moving its
  light from the next frame, no lights when picking parts, discs (QRhi
  with local lights off; OpenGL with a context) unlit, casting nothing,
  placed and facing the light's way, author messages once a shape, a
  material edited in the Shape Viewer and saved keeping the lights, glTF
  names (Open Rails' extra first), an `.sd` light equal to the same glTF
  light, glTF discs.
- Route scene, EUROPE1 on 2026-12-21 at 18:00 (time held), QRhi Vulkan:
  `lamp-views.json` (glTF lamps) against `sd-lamp-views.json` (dark lamps
  with the light-only `tsre_street_lamp_light.s`, generated by
  `make_sd_lamp_light.py`): equal on both MSTS backends (the street view
  exactly, the view from above within a fraction of a pixel's level);
  without the light-only shapes 700 to 1750 pixels differ. On OpenGL the
  two scenes are equal too (discs, no light).
- Shape Viewer, the glTF lamp from below: the disc on the lens on OpenGL
  and on QRhi with local lights off, none with local lights on.
- No change elsewhere, against the branch's base (d980fec3) with one
  settings file (`--settings`): the 33 Khronos samples on QRhi and OpenGL
  pixel-identical (none has punctual lights), EUROPE1's six parity views
  (`parity-views.json`) pixel-identical on both MSTS backends; suites `shape-materials`,
  `gltf-pbr-gl`, `local-lights`, `displays` (under Xvfb), `mesh-store`,
  `selection-id`, `rhi-shaders`, `eng-lights`, `water-gl`,
  `environment-map-gl`, `consist-preview-gl` and ctest pass.

## Decisions (user, 2026-11-05)

1. Colour: two choices, `ColorRGBA` and `Color3F`. Read here as hex sRGB
   with alpha, and glTF's linear floats.
2. Direction: two choices, `QDirection` (the token reused, a quaternion as
   glTF nodes have) and `Rotation` as display squares.
3. Where local lights are off or absent: a disc, as signals and ENG lamps
   get now.
4. Light names in this task; the ENG binding a follow-up step.
5. ENG binding (later the same day): each bound ENG light switches its own
   shape light (not only the first lit cone); a bound cone without an
   automatic reversed copy, each end bound explicitly; the off-by-default
   field is `On` (1 on by default, 0 off), generic, not vehicle-specific;
   the `ShapeHierarchy` lookup (light, then matrix) in the binding step,
   placement on a matrix the step after; glTF lights bound by
   `OPENRAILS_light_name` only (a fallback to the glTF `name` kept as an
   option, Later); fades and flashing with task renderer 34's step 4, the
   binding taking a strength from 0 to 1; derived beams by option c (the
   others kept in the Next step for comparison); cab lights bound by the
   CVF a follow-up task.

## The ENG binding (design, user 2026-11-05; done 2026-11-06)

An ENG or WAG `Light` names a shape light in its `ShapeHierarchy`: the ENG
says when the light is on, the shape light what it is. Neither an addition
to the ENG's lights nor a replacement of them: a link.

- Each bound ENG light switches its own shape light, per car (each car
  draws its shape with a state of its own), by its own `Conditions` (user:
  not only the car's first lit cone, as Open Rails does). The shape light
  keeps its own place, aim, colour, strength, range and cone; the ENG
  `State`'s `Position`, `Azimuth`, `Angle` and `LightColour` are not used
  for it (a `State` is still written, the parsers need one).
- Fades and flashing (`FadeIn`, `FadeOut`, `Cycle`, `States`,
  `Transition`) are task renderer 34's step 4, not this step (user): TSRE's
  ENG lamps do not animate them yet, so bound lights switch on and off at
  once. The binding sets a light's strength from 0 to 1, not a plain
  on/off, so step 4's fades reach bound lights without changing it.
- Derived beams (user: option c): an ENG that binds any cone makes no
  derived beam from its unbound cones (an author message names them); its
  bound cones light their shape lights. An ENG without bindings keeps the
  first-lit-cone rule (task renderer 34) as now: the rule is for old
  content, written for MSTS's single headlight. A bound glow keeps its lens
  (an addition) and switches its shape light too (Open Rails binds glows
  but never lights them). The options compared (2026-11-05), for later:

  | Option | Rule | Forward headlight bound, old back-aimed cones (an EP07-style include) unbound |
  | --- | --- | --- |
  | a | the first-lit rule over all cones; a bound first lit cone suppresses the derived beam | works, by order: a bound ditch light before an unbound headlight would suppress the headlight's beam |
  | b | the rule among unbound cones only | fails: the back-aimed cone becomes the beam, lighting the first coach |
  | c (chosen) | binding opts out of the rule: no derived beams from an ENG that binds | works, whatever the order; binding only a ditch light leaves no main beam (bind the headlight too) |
- A bound cone gets no automatic reversed copy: a shape light has a fixed
  place in the shape, and the copy would light the front lamp of a flipped
  leading car into the train (Open Rails' copy binds the same light). The
  author binds each end: a front and a rear shape light, each named by its
  own ENG `Light` with its `Unit` (2 first, 5 first reversed; 3 last, 4
  last reversed), as glows are written today. Unbound cones keep their
  copies.
- Names: `.sd` lights by their `Name`; glTF lights only by
  `extras.OPENRAILS_light_name` (not their glTF `name`). Why: Open Rails
  binds glTF lights by that extra alone, so an ENG binding a glTF light
  then binds the same light in TSRE and Open Rails. Were the glTF `name`
  accepted too, content could bind in TSRE by a name Open Rails never
  matches, and its headlights would stay as they are in Open Rails, without
  a word to the author. (The Shape Viewer still shows the glTF `name` where
  the extra is missing.) Case is ignored.
- `ShapeHierarchy` looked up as Open Rails does: a shape light's name
  first, then a matrix name. A matrix: the ENG light stays on the shape's
  root for now, with an author message (placement on the matrix is the step
  after); neither: an author message (Open Rails warns and uses matrix 0).
- `On`: a new `.sd` field, `On ( 1 )` (the default) shines unless something
  switches it, `On ( 0 )` stays off until something switches it on (a bound
  ENG light now; scripts, night-only lights, level crossings later); in
  glTF the light's `extras.TSRE_on` (false). A light an ENG binds is always
  the ENG's; `On` decides only where nothing drives it (a bare shape in the
  Shape Viewer, a loco shape as scenery). Where a car is drawn as a car (the
  route editor's consists, the consist editor, the simulation, the Shape
  Viewer's engines and consists) its ENG is loaded and decides, with the
  test train state until the simulation gives one.

### Binding: implementation (2026-11-06)

- `ShapeLights::Light::bindName` (the `.sd`'s `Name`; a glTF light's
  `OPENRAILS_light_name` only) and `on` (`On`, glTF `extras.TSRE_on`);
  `ShapeLights::bound` finds a light by it, any case.
- Strengths per state: `ComplexShape::setLightStrength(state, light, 0..1;
  below 0 its own)` and `pushLights` (lights alone, for a car seen from its
  cab), in all three backends through `ShapeLights::Drawn` (a state's
  strengths into its packets when changed; discs for lit lights only;
  released states back to their own).
- glTF lights now drawn by `ShapeLights::Drawn` per state too (their own
  packet, shared by the states of a level, could not be switched per car);
  the glTF lamp scene is pixel-identical to before.
- `CarLights::switchShapeLights`: the binding found once per shape (its
  lights' data) and ENG; each bound shape light on while any light binding
  it holds its conditions; reversed copies bind nothing; an ENG binding a
  cone gives no derived beams; the reach of the lit bound lights (their
  Range, else 250 m) asked for while the shape draws; author messages once
  per ENG and shape (a matrix named, nothing named, cones left without a
  beam). `Eng` switches before its shape draws: in the route's view, from
  its 3D cab (the car's lights alone) and in the Shape Viewer and consist
  editor (`pushRenderItems`, with the test train state).
- Two fixes to steps 1-5 found on the way:
  - `Drawn` and `CarLights` read their copy of the shape's lights with a
    non-const `[]`, which detached it from the shape's: every push took it
    for a reloaded shape and made the packets again. Now `at()`, and the
    suite checks that the packets are kept between pushes.
  - A light without `ShapeHierarchy` hung on an identity matrix, so its x
    was mirrored against the shape's own vertices (both loaders draw an
    MSTS shape through its first matrix with x mirrored) and against ENG
    light positions. The root is now the first matrix: `.sd` positions are
    in the shape's own coordinates; a glTF light copied into an `.sd` takes
    x negated, and `QDirection`'s y and z negated (`make_sd_lamp_light.py`,
    the guide). glTF shapes (no parts) keep their own space.

### Binding: verification (2026-11-06)

- `shape-lights` suite, 38 checks: `On ( 0 )` dark and without a disc until
  switched, a released state back to its own; an ENG binding the front
  light by `Unit ( 2 )` and the rear one by `Unit ( 5 )`, a reversed copy
  binding nothing, headlights off turning both off, reach from `Range` or
  250 m; no derived beam from an ENG binding a cone, one from the same cone
  unbound; three author messages; glTF bound by `OPENRAILS_light_name` only,
  `TSRE_on`; packets kept between pushes; root lights drawn as the shape's
  vertices (x mirrored).
- The lamp scene again (EUROPE1, 2026-12-21 18:00, QRhi Vulkan): the dark
  glTF lamps with the regenerated light-only S shape equal the glTF lamps
  on both MSTS backends; the glTF lamps equal their captures before.
- USA2's EveGrain (20:30), the player's Dash 9 in a scratch game root of
  links (`~/ORTS` untouched): its ENG's front cone bound to an `.sd`
  `HEAD_FRONT`, a rear cone (`Unit ( 5 )`) to `HEAD_REAR`, both `On ( 0 )`:
  the derived beam gone, the shape light's pool on the track ahead, none
  with the headlights off (`headlight 0`).
- ctest and the suites `shape-materials`, `gltf-pbr-gl`, `local-lights`,
  `eng-lights`, `mesh-store`, `selection-id`, `rhi-shaders`, `water-gl`,
  `environment-map-gl`, `consist-preview-gl`, `cabview`, `simulation-ui`,
  `displays` (Xvfb) pass.
- Display squares keep their identity root (an empty `ShapeHierarchy`):
  the same mirror question for them, not changed here (their examples hang
  on `MAIN`).

## Later

- Cab lights bound by the CVF (a follow-up task; user 2026-11-05): a cab
  view control (`ORTS_CABLIGHT`, `ORTS_GENERIC_ITEM1/2`, or TSRE's own)
  switching lights in the 3D cab's shape, as an ENG light switches a car's;
  `On ( 0 )` marks such lamps off until then. Open Rails (unstable
  74da4d47a) has no such link: glTF lights in a car's or a cab's shape are
  always on unless an ENG binds them (car shapes only), `KHR_node_visibility`
  or its day/night switch hides them, or a pointer animation dims them; its
  cab light switch only swaps the cab's textures (a 2D cab's `CABLIGHT`
  folder, the cab renderer's lit look).
- A glTF fallback for the binding: a glTF light without
  `OPENRAILS_light_name` bound by its glTF `name` (always, or behind a
  setting or a TSRE extra), for models exported without Open Rails' extra
  (Blender writes the light's name, not the extra). The cost: such content
  binds in TSRE only (see the Next step's Names); an author message could
  say so where it happens.
- Placing ENG lights on a matrix named by `ShapeHierarchy` (or
  `ShapeIndex`), as Open Rails does: lenses and beams on a moving part
  taking its posed matrix once a frame, steady lenses merged per matrix,
  the root's mirrored x kept. No content seen uses it (task renderer 34's
  survey): checked on made-up cases. The step after the binding.
- Flashing (ENG-style `Cycle`/`States`/`Duration`, or
  `KHR_animation_pointer` for glTF), night-only lights.
- Colour temperature (Kelvin) as a third colour choice.
- IES profiles; shadows from local lights.
- A lights editor in the Shape Viewer writing the `.sd` lines (as the
  materials editor).
