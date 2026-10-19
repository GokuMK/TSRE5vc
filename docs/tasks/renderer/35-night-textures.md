# Task 35 - Night Textures

## Objective

Show the night textures of MSTS shapes. MSTS and Open Rails swap a shape's
texture for its night texture when the sun sets and draw it fully bright.
TSRE keeps the day texture, lit by its own light (sun, moon, sky, lamps,
exposure), and adds the night texture's light on top as the daylight
fades: lit windows show at dusk and in deep shadow, and at night the shape
looks as its author made it, with TSRE's lamps lighting it as well. The
brighter parts (windows) glowing slightly through the bloom is a bonus.
Also: MSTS night sub-objects.

## Content (C:/trainsim, 2026-10-18)

- Shapes with the night bit and night textures (`TEXTURES/NIGHT`): CMK 864
  of 11,520 `.sd` files and 1992 textures; Prudnik 1852 of 10,876 and 1653;
  PeakRail 70 of 818 and 53.
- Three kinds of night texture (random pairs decoded with `--aceconv`):
  - masks: the facade black, the windows lit or at their day colour
    (CMK `u79haus0084`, `K-Neumarkt1`);
  - darkened copies of the day texture (`Concrete2`, Prudnik `schornstein`,
    `bardes_schirm1`);
  - unchanged copies (CMK `femalehiker`, Prudnik `Bhf_Schilder_01`). The
    hiker is most likely bright on purpose: placed on a platform, it looks
    lit by a lamp that MSTS could not light (user).
- Night textures can be smaller than the day ones (`u79haus0084`: 256 x 256
  against 512 x 512); DXT1.
- Trainsets: night folders only for cab views (68), none for cars.
- `ESD_SubObj`: only road cars (CMK 17, Prudnik 23, PeakRail 21 `.sd`
  files: AstraEstate, BMW525i, Alpha147 ...). A plain MSTS-style token in
  MSTS-era content, most likely from MSTS (Open Rails' own carry `ORTS`);
  not confirmed from MSTS documentation.

## Format and Open Rails

- The `.sd` file's `ESD_Alternative_Texture` bit 0x100. TSRE reads its
  season and snow bits (0x1 to 0x80) already
  (`TerrainSeason::shapeTextureDirectory`), not this one.
- The night texture has the texture's name, in a `Night` folder next to the
  texture or one level up (`Helpers.GetNightTextureFile`, `.dds` before
  `.ace`): `TEXTURES/NIGHT` also for a seasonal `TEXTURES/SPRING` texture.
- Open Rails swaps the texture when the sun's direction is below the
  horizon (less a tiny offset per material); trainset night textures (cabs)
  also underground. A hard switch, no fade.
- Drawn fully bright: `ImageTextureIsNight` takes the ambient and shadow
  term to full and `NightColorModifier` to 1 (day textures are darkened at
  night). The night texture is the shape's finished, self-lit night look.
- Night textures are loaded only once it gets dark, except trainsets'.
- `ESD_SubObj`: sub-object 1 is hidden while the sun is up.
- PR #1251 (open, MSTS legacy shaders) adds a `NightLight` shader name:
  primitives drawn additively with the night texture, times
  (1 - daylight) x 0.5, daylight ramping as the sun's direction goes from
  0.1 to -0.1. None of our 44,000 shapes uses it.

## Design

### Loading

- The night bit read with the season bits. For a shape with it, each of
  its textures' night texture, searched as Open Rails does (next to the
  texture, then one level up; `.dds` before `.ace`). A texture without one
  stays day only.
- Loaded only when needed: from the sun some degrees above the horizon
  (before the lamps' fade begins) until it is that high again in the
  morning, with a margin against switching back and forth; always under
  cover (sky enclosure, task 27). Released by day.
- (Dropped in step 3: k, the night texture's darkening of the day
  texture, estimated once per texture; see the glow.)

### Shading (QRhi)

The MSTS shape material gets an optional night texture:

    colour = max(lit(day), L x N x night) + L x G x max(night - 1.25 x day, 0)

- L, lights on, as the lamps have it: with tone mapping off the lamps'
  fade (`Daylight` `localLights`: full at night, `DayLocalLights` by day);
  with auto exposure 1, constant, the exposure hiding it by day and showing
  it as the sun weakens (lamps' fade is off there too). 1 under cover.
- N, the self-lit base: the level that shows the raw night texture at its
  own brightness at night, as Open Rails does; the brighter of it and the
  lit day texture (in linear terms). Added, a night texture copying the day
  one doubled a facade still lit by the dusk (the lamps' scale is full from
  sunset; CMK at 20:00, the facade nearly white): the brighter of the two
  never exceeds either. The scale of a surface lit
  by a lamp, not of an emitter: real lit windows are about 1 % of a sunlit
  wall, so under auto exposure they are invisible by day except in deep
  shadow, and clear at night (as an emitter, as signal lenses at 4 times
  their colour, windows would glow at noon).
- G, the glow (a bonus, QRhi): where the night texture is painted brighter
  than the day one (lit windows, lamps), twice that light more, past white
  and into the bloom. Copies (the hiker, signs, whole fronts kept at their
  day brightness) and darkened copies get the base only: bright, not
  glowing. (Planned against the night texture's darkening k instead:
  masks, k about 0, keep whole street fronts at day brightness, lit by
  lamps, and the fronts glowed whole; step 3.)
- No offset per object (user: a station's signs would switch on at
  different times); everything brightens together as the daylight goes.
- The night texture's colour only; coverage (alpha test, blending) stays
  the day texture's.
- Fogged and hazed as the surface's own light (Haze.glsl).
- It lights nothing (no emitters, no light grid): perhaps later.

### OpenGL

The same self-lit base in the OpenGL shape shader with a second texture,
L from the lamps' fade; no glow (no HDR: the added light clips at white).

### Night sub-objects

`ESD_SubObj`: sub-object 1 drawn only at night: with the sun below the
horizon, as Open Rails (a visibility switch, independent of the exposure).
In our content only road cars, whose sub-object 1 is presumably their
lights' geometry (perhaps fake light cones, see below).

### Cost

- Memory: one more texture per night-textured material, often half the
  size, only from dusk to dawn.
- GPU: one more texture sample on those materials.

## Steps

1. Loading: the night bit, the night textures' lookup and lifetime; unit
   tests (the lookup on CMK and Prudnik paths, seasonal ones too).
2. QRhi: the self-lit base with L; captures at dusk and night, auto
   exposure and tone mapping off.
3. The glow (bonus): G, judged on captures; dropped if the data does not
   fit it.
4. OpenGL: the self-lit base.
5. Night sub-objects.

## Verification

- Captures: the CMK town and station and a Prudnik station at noon,
  sunset, dusk and 22:00, with auto exposure and with tone mapping off; the
  hiker on its platform; a street in shadow at dusk (the windows' light
  showing); day views against before (only deep shadows may change under
  auto exposure; how much measured).
- Unit: the lookup, the load and release elevations.
- Memory: textures loaded at noon and at night.
- Steam Deck: frame time in a town at night.

## Decisions (user, 2026-10-18 and 19)

- Emissive, not a swap: the day texture lit by TSRE's light, the night
  texture's light added; the price of one more texture in memory accepted.
- The k split: windows glowing slightly; the rest (the hiker, bright on
  purpose) bright but not glowing; darkened copies with practically no
  effect. The glow only a nice-to-have bonus: the texture data may not fit
  it; then the raw night texture alone.
- Lights on as the lamps: the lamps' fade with tone mapping off; constant
  with auto exposure, at the level of a lamp-lit surface.
- No offset per object.

- (2026-10-19) OpenGL with the self-lit base (step 4). The glow tried
  (step 3). Night sub-objects left for later: no ugly light cones seen,
  so those presumably use them.

## Related, later

- **Editing .sd values in the editor** (user, 2026-10-19): assets' night
  textures are often unfinished or odd (Prudnik `ZB1`: the facade's night
  texture a copy of the day one, the whole signal box lit by night; CMK
  `CMK-centralny1`, an underground station: night textures for 9 of its 14
  textures, but no night bit in its .sd, so neither MSTS nor Open Rails
  uses them; by night the station is black here, dim in Open Rails, which
  darkens day textures by a fixed share at night). Fixed easily if the
  editor edits ESD_Alternative_Texture and the other .sd values.
- **Under cover by day** (open): tunnels and underground stations by day
  have no night textures loaded. Keeping them loaded all day costs up to
  their share of the textures (in `TEXTURES/NIGHT`: CMK 379 MB against
  1264, Prudnik 408 against 2838); loading them by day only when the view
  is under cover is the cheaper alternative.

- **Light materials** (`vtx_state` light material): Open Rails maps -5
  normal, -6 and -7 specular 25 and 750, -8 full bright, -9 and -10
  vegetation, -11 half bright, -12 dark shade; TSRE draws all below -7
  unlit and -12 at half brightness, so vegetation and half bright come out
  full bright, and specular is ignored. Used in our content (of the text
  shapes, about 3 %: -9 common in CMK, -8, -11 and -12 in trainsets, -6 and
  -7 in GLOBAL). To fix with before and after image comparison (the parity
  views, shapes using each), as a task of its own or a step here.
- **PR #1251** (DetailMod2X, GlossMap, AlphRefMap, NightLight, baked
  vertex colours, UV operations, a second texture stage): none of our
  shapes uses its shader names; not now. The NightLight model is this
  task's added night light.
- **Fake light cones** (user, 2026-10-19): MSTS content fakes lamps' light
  with translucent cone geometry, which looks awful with TSRE's real lamps
  and fog. Idea: in shapes with night textures (foliage, smoke and water do
  not use them), leave out blended texels with alpha under 0.9 (not 1:
  alpha-tested cutouts stay); to review later. The road cars' night
  sub-objects may be such cones.

## Implementation status

Design (2026-10-19), on `feature/night-textures`.

### Steps 1 and 2: loading and the self-lit base (2026-10-19)

- `NightTextures` (shape): the lookup (a `Night` folder in any case next to
  the texture, else one level up; an .ace name finds the .dds), looked up
  once per folder; the frame's state (wanted, scale, under cover) set by the
  view; loaded below the sun at 20 degrees, released above 23.
- Both MSTS shape backends: `SFileLegacy` (the editor's for now) and
  `SFileComplex` (Compact, the editor's next; user: prepare both). The
  night bit read with the season bits; a shape holds its night textures
  while the frame wants them (TexLib references) and releases them by day;
  the packets carry `RenderItem::Material::nightTextureId`, resolved when
  drawing as the day texture. Under cover by day not loaded (yet).
- The view (`RouteEditorGLWidget`): scale the lamps' constant
  `DayLocalLights` with auto exposure, less the exposure's bias, else the
  lamps' fade `localLights` (also with lamps at full strength by day);
  under cover 1 without auto exposure; time of day off: none.
- The exposure's bias (user, 2026-10-19, Prudnik `ZB1`, a signal box whose
  facade's night texture copies the day one, its roof's darkened): by night
  the view is 1.75 stops brighter (at sunset 1.5), and the night texture
  came out three times its own brightness, paler than in the sun (the
  facade 226, 205, 150). Less the bias it shows as authored (182, 155, 94),
  as Open Rails shows it: the whole box lit by night, the asset's choice.
- The shared shader (`StandardFog.fs`, the main program only): the night
  texture on unit 13 (`NightTextures::TextureUnit`; 8 is OpenGL's instance
  matrices), the brighter of it and the lit day texture, before the haze.
  The QRhi renderer binds it while the frame wants night textures.
- Suite `night-textures` (12): the lookup (TEXTURES/NIGHT, from SPRING one
  level up, none, .dds, any case), the load and release elevations, CMK's
  textures.
- Captures (CMK, tile -5310 -14961, the u79haus row houses, clear, auto
  exposure): noon unchanged (night textures not loaded); from 19:00 the
  windows' light faint, at sunset (20:00 in that date) facades whose night
  textures copy the day ones lit, at 22:00 the night look: lit windows,
  street fronts lit as authored, darkened sides dark, far roofs with copied
  night textures bright, as in Open Rails. Legacy and Compact pixel for
  pixel the same (noon, 20:00, 22:00).

### Step 4: OpenGL (2026-10-19)

- GLUU gives `uNightSampler` its unit and caches `nightTextureEnabled` and
  `nightScale`, the scale per frame (the lamps' fade: no auto exposure
  there); `OpenGL3Renderer` binds a packet's night texture while the frame
  wants them. No glow (no HDR).
- Captures (the CMK row houses, `core.rendering.backend=opengl`): noon as
  before, at 20:00 and 22:00 lit windows and street fronts as on QRhi.

### Step 3: the glow (2026-10-19)

- Tried against k first (estimated per texture on a worker): on 76 CMK
  pairs k came out cleanly two-sided (31 masks about 0, 39 copies about 1,
  5 darkened), but masks keep whole street fronts at their day brightness
  (lit by lamps), and the CMK row houses' fronts glowed whole, haloed in
  the bloom. Dropped.
- Instead: where the night texture is more than 1.25 times the day one
  (painted lit: 16 of the 76 have such texels, most a few percent of them:
  lit windows, lamps; a lit shelter and a tunnel wall more), twice that
  light added and written to the glow output for the bloom (fogged as the
  surface). QRhi only. No CPU decoding, no k.
- Captures (the CMK row houses): the windows of the right block and far
  houses' lights glow slightly; fronts kept at day brightness and copies
  do not; one front painted brighter by night glows; noon identical.
