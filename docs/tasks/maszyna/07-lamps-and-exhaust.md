# Task maszyna 07: MaSzyna lamps and exhaust

## Objective

MaSzyna vehicles' outside lamps (head lamps, high beams, red markers) lit
and casting light, and their diesels' exhaust, through TSRE's own car
lights (task renderer 34, shapes 14) and exhaust (task driving 05,
renderer 41): MaSzyna's data read and adapted, no second light or particle
engine.

## Decisions (the user, 2026-11-11)

- **A beam per head lamp**: TSRE's fog needs true beams. Lamps very close
  together share one; otherwise each lamp has its own (Polish locos: 3).
  That is the ENG lights' rule already (renderer 34, `CarLights::beams`:
  lit near-white glows at the end the cone shines from, those under 15 cm
  apart one lamp), so MaSzyna's lamps go through it.
- **Which lamps are lit**: TSRE's train rules for every vehicle (the
  headlight key lights the leading end's white lamps, the last vehicle shows
  red markers), and on the driven loco its cab's switches, each its own
  lamp; the headlight key lights a set.
- **Exhaust**: MaSzyna's `smokesource_*` parts and `data/smokesource_*.txt`
  read and adapted into TSRE's emitters.

## What MaSzyna has (66a940d4)

- Lamps are pairs of parts, `<lamp>_on` and `<lamp>_off` (`TButton::Init`):
  `headlamp11`, `12`, `13` (end 1: upper, right, left), `headlamp21`-`23`
  (end 2), `highbeam12`, `13`, `22`, `23`, red markers `endsignal12`, `13`,
  `22`, `23` or both at once `endsignals1`, `2`, plates `endtab1`, `2`
  (`DynObj.cpp:2338-2370`). 74 of the pack's 249 vehicle folders have head
  lamps, 84 markers. TSRE hides `_on` parts at load (`E3dFile`), so every
  lamp looks off now.
- The light: one set per end (`lightarray.cpp`), its colour `LampRed`,
  `LampGreen`, `LampBlue` (`Headlights:`, white by default; one file in the
  pack sets it), its strength by `DimmedMultiplier` (0.6), `NormalMultiplier`
  (1.0), `HighBeamMultiplier` (2.8), `HighbeamDimmedMultiplier` (2.5).
- The cab: a switch per lamp (`leftlight_sw`, `rightlight_sw`,
  `upperlight_sw` in about 240 cabs; `rearleftlight_sw`, `rearrightlight_sw`,
  `rearupperlight_sw` in 36-45; `dimheadlights_sw` in 309).
- Exhaust: rotator parts named `smokesource_<kind>` (37 vehicle folders),
  their particles from `data/smokesource_<kind>.txt` (spawn rate, launch
  inclination, velocity, size, opacity, colour, size and opacity change),
  spawned by the engine's state (`particles.cpp`: RPM and power; opacity by
  the fuel fill; grey when cold).

## Design

### Lamps

1. **The car's lights from its model** (C++, at the car level: `Eng` and
   `EngLights`; `E3dShape` untouched, it is the e3d route task's): once the
   car's `.e3d` is loaded, its lamp parts make an `EngLights`, as an ENG's
   `Lights` block would:
   - a glow per lamp at its `_on` part's centre (its bounding sphere at
     rest), white (`Headlights:` colour) for head lamps and high beams, red
     for markers;
   - per end a cone for dim and one for bright, from the middle of that
     end's head lamps, out along the car;
   - conditions as Open Rails': an end's head lamps on with the headlights
     dim or bright on the first vehicle (end 1 on `Unit 2`, end 2 on
     `Unit 5`, first reversed); its red markers on the last (end 2 on
     `Unit 3`, end 1 on `Unit 4`); high beams only from the cab.
   `CarLights` then draws them as any car's: lenses, the beam rule above,
   reach, the daylight rule.
2. **The lamp parts**: each lamp's `_on` shown and `_off` hidden while its
   glow is lit, per car (`enableSubObjByName` on the car's shape state).
3. **The driven loco's own** (Lua, `maszyna.lights`): the cab's switches
   each set their lamp (the end by the cab's facing); the headlight key
   sets the set (the leading end's head lamps; dim by `dimheadlights_sw`
   or the key's dim step); the script publishes each end's lamps (MaSzyna's
   light bits) and the dim and high states; the host uses them for that car
   in place of the conditions.

### Exhaust

1. **Where**: a MaSzyna diesel's emitters are its model's `smokesource_*`
   parts, posed with the car (their place this frame), the gas leaving
   upward.
2. **What**: the diesel script's exhaust (`tsre.dieselengine`'s, the
   `exhaust` kind, as for an ENG diesel), adapted by the part's settings
   file: its colour, its puff size as the nozzle, its opacity and launch
   speed as scales on the soot and the speed.

## Steps

1. Lamps from the model, by TSRE's rules; the lamp parts switched.
2. The driven loco's cab switches and the headlight key (`maszyna.lights`).
3. Exhaust.
4. Checks: suites for the lights made from a model (positions, kinds,
   conditions), the parts switched, the script's lamp states; captures of
   an ET22 and an SM42 at night (beams, markers) and of an ST44's exhaust.

## Progress

- Steps 1 and 2 built (2026-11-11, uncommitted on `feature/maszyna`):
  - `MaszynaLamps` (`src/tsre/maszyna/MaszynaLamps.{h,cpp}`): the model's
    lamp parts (their `_on` part's bounding sphere centre through its world
    matrix; MaSzyna's x is the left, so x is negated into the car shape's
    MSTS space), the `.fiz` `Headlights:` colour and strengths
    (`LampRed`, `LampGreen`, `LampBlue`, `DimmedMultiplier`,
    `NormalMultiplier`, `HighBeamMultiplier`, `HighbeamDimmedMultiplier`), the lit
    set (`lit`: the train's rules, the cab's bits over them), the car's
    `EngLights` for a lit set (`lights`: a cone per end with lit head lamps,
    the leading end's first, reach 150 m × √strength within 20-250 m, 25°;
    a glow per lit lamp, white or red; plates are parts only).
  - `Eng`: a MaSzyna car's `getLights()` is `maszynaLights()`, made from the
    model the loaded shape holds (`E3dFile::read` by the shape's path, so
    no second read), each lit set's lights kept by key (CarLights holds
    them by address); its lamp parts switched in its own state
    (`switchLampParts`, `enableSubObjByName` on `_on` and `_off`, only when
    the lit set or the state changes). `E3dShape` untouched (the e3d route
    task's).
  - The driven loco: `maszyna.lights` (the cab's switches, the headlight
    key, the dimming) publishes `lamps1`, `lamps2`, `lampsControlled`
    (0 off the driven cab's loco: a heard AI loco runs it too),
    `lampsDim`; `SimulationController` hands them to the car
    (`Eng::setMaszynaCab`) for the driven train only. Wired into the three
    MaSzyna loco scripts.
  - Tests (`maszyna-scripts` 39): ET22's 12 lamp parts placed (end 1 at
    +9 m, the right lamp at +x, the upper one highest), the train's rules
    (alone, reversed, in the middle, headlights off) and the cab's over
    them, 3 beams (`CarLights::beams`); `maszyna.lights`: the key's set, a
    switch, the dimming, quiet off the cab.
- Smoke templates (the user, 2026-11-11): no MaSzyna `data/` folder
  (MaSzyna's global one: 12 templates, vehicles use 8, scenery 5, besides
  `load_weights.txt` and `sound_overrides.txt`); TSRE emitter presets fitted
  to the stock templates, in code or an appdata file (in code: typed, as
  CarExhaust's kinds are).
- Step 3 built (2026-11-11, uncommitted):
  - `MaszynaSmoke` (`src/tsre/maszyna/MaszynaSmoke.{h,cpp}`): the presets,
    MaSzyna's default as TSRE's diesel exhaust (0.5 m³/s, 10 m/s, soot 0.4,
    a 0.3 m nozzle), each other scaled as its template differs from the
    default (flow by puffs a second × a puff's area, speed by the launch
    speed, soot by the opacity, the nozzle by the smallest puff, its
    colour): st44 (2.2 × the flow, soot 0.53, 0.6 m), su45 and tractor,
    light (barely seen), medium, bus, st45; the scenery's four (chimney,
    two cooling towers, a bonfire) first guesses, unchecked, for task
    shapes 18. Unknown names take the default. A model's smoke sources:
    `smokesource_*` parts at their origin through their parents.
  - `Eng::emitMaszynaSmoke`: a MaSzyna car's smoke out of its model's
    sources, up, the engine's exhaust kind shared by them and scaled by
    each source's preset (`MaszynaSmoke::adapt`, the preset's colour where
    the engine gives none). MaSzyna diesels' definitions name the
    `exhaust` kind; `tsre.fiz`'s diesel publishes it (`c.exhaust`).
  - A `.fiz` gives no fuel figures, so its engine burned nothing idling
    (no friction): `tsre.dieselengine`'s exhaust now also comes from a
    running engine marked `runningExhaust` (`.fiz` diesels only), from its
    power and RPM as before. Fuel figures were not added: they bring
    friction, changing the force curves.
  - Test (`maszyna-scripts` 40): ST44's smoke source on its roof (4.5 m up)
    with the st44 preset, the presets' lookup and default, the adapted
    exhaust, the kind named, 0.48 m³/s idling.
- Captures (2026-11-11, QRhi Vulkan with local lights, USA2's evegrain at
  20:30 and nnmxdgds at noon in a scratch game root; the views' frame is the
  vehicle's: its front at the origin, ahead -z, x to its right):
  - A lone ET22 at night: its three head lamps lit in the Polish triangle,
    their beams lighting the track ahead, its rear's two red markers; ridden
    (the train driven), the headlight key's beams from the cab.
  - Found by the left lamp's switch: the headlight key's set applied in the
    next step overwrote a switch worked in the same step.
    `maszyna.lights.follow` now applies it as soon as the loco takes the
    key (the scripts call it after each command); suite case added.
  - ST44 pulling: a dark plume from its roof source (the st44 preset);
    idling nothing visible from 12 m (0.48 m³/s, little soot at no load).

- High beams (2026-11-13, the user's batch): MaSzyna lights them only from
  its light dimmer (`ModernDimmer` in the `.fiz`'s `Switches:`, positions
  in `DimmerList:` as high beam, dimmed, off; by default normal and
  dimmed; cab `moderndimmer_sw`; `DynObj.cpp` `SetLightDimmings`). In the
  stock pack the Vectron alone has it (`vectron.fiz.inc`: high, high
  dimmed, off, normal, dimmed, from normal, no cycling), with high beam
  parts (`highbeam12_On`...) and the switch. Ported as written:
  `maszyna.lights` reads the dimmer (`init(s, vehicle)`), `moderndimmer_sw`
  steps it (round with `Cycle`) or takes a position, its position dims,
  puts the cab's end's head lamps out, or turns the high beams on
  (`lampsHigh`, `lampsDimmer`); without `ModernDimmer` the switch does
  nothing. `MaszynaLamps`: with the cab's high beams a high beam lamp is
  lit in place of its side's head lamp, the upper one stays, the beams at
  the high beams' strength (`HighBeams` bit). Deviation: a model without a
  side's high beam lamp keeps that head lamp lit (MaSzyna shows no part
  there but its light still shines). The `.fiz`'s dimmed high beam key is
  `HighbeamDimmedMultiplier` (read as `HighDimMultiplier` before; no pack
  file sets either). Test (`maszyna-scripts`): the Vectron at normal (3
  head lamps, 150 m), dimmed (116 m), high (1 head lamp and 2 high beams,
  3 beams, 250 m), kept at its first, high dimmed (237 m), off (the cab's
  end out, its switches kept); an ET22's switch ignored.
  The pack's `.fiz` files are ISO-8859: a UTF-8 `grep` (ugrep here) skips
  them as binary without a word, so counts need `-a` (my first count of
  `ModernDimmer` said none).
