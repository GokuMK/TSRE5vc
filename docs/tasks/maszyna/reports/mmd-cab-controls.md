# MaSzyna cabs in `.mmd`: controls, the pack, a bridge to TSRE's 3D cab

Review for task maszyna 03 (2026-11-10), for the later cab task. Source:
`../maszyna` at 66a940d4 (`vehicle/Train.cpp`, `Gauge.cpp`, `Button.cpp`,
`input/drivermouseinput.cpp`); pack `~/ORTS/maszyna/dynamic` with includes
expanded.

## 1. How MaSzyna reads a cab

- The vehicle's `<TypeName>.mmd` is parsed twice: `TTrain::LoadMMediaFile`
  (Train.cpp:10199) reads `internaldata:`'s cab sounds; `TTrain::
  InitializeCab` (10326) reads the cab, again on every cab change (8300,
  10184). Cab 1 or 2 by the occupied end, 0 the engine room; the parser
  skips to `cab<N>definition:` (10376-10393).
- `TCab::Load` (131): optional `cablight:` (9 colours, ignored), the box
  `x1 y1 z1 x2 y2 z2`; then in order `driver<N>angle:`, `driver<N>pos:`,
  `driver<N>sitpos:` (10398-10433). Then tokens until EOF or
  `cab0definition:`: `cab<N>model:` for its own N (`none` on cab 1: the
  controls are in the exterior model), anything else tried as a gauge, then
  a button, then `pyscreen:`; unknown tokens skipped silently (10452-10575).
- So controls written once after `cab2model:` serve both cabs (ET22,
  `201e.mmd:139-238`: kabina_a / kabina_b with the same submodel names); the
  cab 0 section must come last (ET22: `include cab0.mmd end` at the end).
- Includes as in `.fiz` (`include file p1 … end`, `(pN)`), and `include [
  a b c ] end` picks one at random (allowRandom). Tokens are lower-cased.
- **Gauge** (Gauge.cpp:98-232): old `label: submodel anim scale offset
  friction [endvalue endscale]` or block `label: { submodel anim scale
  offset friction … key: value }`. Animations: `rot`/`rotvar` (rotation of
  scaled·360° about the submodel's local Y), `mov`/`movvar` (translation by
  scaled on local Z), `wip` (wiper: the submodel, child and grandchild),
  `dgt` (digit children rotated −36° per digit); scaled = value·scale +
  offset, rotvar/movvar lerp toward endscale; `friction` a smoothing time
  constant in s (358-372). Block keys: `type:` push/impulse/return,
  delayed, pushtoggle, toggle (default); `soundinc:`, `sounddec:`,
  `soundon:`, `soundoff:`, `soundN:`, `soundproofing:`.
- Values are MaSzyna's raw quantities: notch numbers (MainCtrlPos), the
  brake handle's positions, km/h, MPa (Train.cpp:9273-9399).
- Gauges find `name`, else `name_off`, in the cab model then the low-poly
  interior; `name_on` is shown while the gauge's state is on (186-217,
  381-388). Picking: submodel → label (`control_mapper`, 50-62).
- **Lamps `i-*`** (Button.cpp:24-71): `submodel_on`/`_off` in cab, low-poly
  interior, then exterior; turned by the train's logic each frame (e.g.
  8923) or bound to a bool (11607); all gated by low-voltage power (8785).
  Special: `i-instrumentlight[_m|_c|_a|_l]`, `i-doors: n sm`, `clock:
  analog` (fixed ClockShand/Mhand/Hhand), `brakes: i j sm…`, `eimscreen: i
  j sm…`.

## 2. Names MaSzyna knows

387 labels (Train.cpp:11487-12252); mouse commands in
`drivermouseinput.cpp:490-1112` (L/R buttons).

- **Traction**: mainctrl (mastercontrollerset), jointctrl (combined power
  and local/ED brake), scndctrl (shunt field), shuntmodepower, dirkey and
  dirforward/neutral/backward_bt, cruise control (tempomat_sw, speedinc/dec,
  speedbutton0-9), maxcurrent_sw (overload threshold), stlinoff_bt (motor
  connectors open), fuse_bt (overload reset), converterfuse_bt,
  relayreset1-3_bt, inverter enable/disable 1-12, motor blowers, cooling
  fans, diesel pumps and heaters; gauges enrot1m-3m, oilpress, oiltemp,
  water temps.
- **Brakes**: brakectrl (trainbrakeset), localbrake, manualbrake,
  alarmchain, releaser_bt (bail off), brake profile G/P/R switches, brake
  operation mode, spring brake, universalbrake1-3_bt, epbrake_bt, sand_bt,
  antislip_bt; gauges brakepress, pipepress, scndpress (main reservoir
  pipe), limpipepress, compressor (main reservoir), springbrakepress,
  `brakes:` per-car table.
- **Power**: main_on_bt / main_off_bt / main_sw (line breaker; on a diesel
  it starts the engine), pantfront/rear_sw, pantfront/rearoff_sw,
  pantalloff_sw, pantograph select and valves, pantograph compressor,
  compressor_sw (local, presets), converter_sw, battery_sw (on/off),
  cabactivation_sw, trainheating_sw; gauges hvoltage, lvoltage, hvcurrent*,
  pantpress, heating voltage and current.
- **Lights**: upper/left/right light switches front and rear, red markers,
  dimmer, light presets, cablight_sw, cablightdim_sw, instrument, dashboard,
  timetable lights, compartment lights.
- **Security**: security_reset_bt (vigilance, "czuwak"), shp_reset_bt (cab
  signal); lamps i-security_aware, i-security_cabsignal.
- **Doors**: left/right toggles and on/off, all, permits and presets, door
  step, mode, lock, departure signal.
- **Radio**: radio_sw, channel and volume, radiostop_sw, test, calls.
- **Misc**: horn high/low, whistle, wipers (multi-position), mirrors,
  coupling disconnect, distance counter (`dgt`), tachometers, clocks,
  universal0-29 (cosmetic: windows, visors, seats; `i-universal0-9`).
- **Lamps**: about 100 with computed states (breaker, relays and overloads,
  resistors, shunt steps, slip, direction, converter, compressor, fans,
  overheat, brake states, battery, cab active, heating, lights).
- Quirks: `i-edenabled` has no colon (never matches); some labels exist
  only in the mouse table (never loaded); `pyscreen: <submodel> <script>`
  renders a Python screen into a texture with touch input.

## 3. Pack statistics

- 1,453 `.mmd` with a `.fiz`; 1,109 define a cab; **658 drivable** (with
  mainctrl, jointctrl, brakectrl or dirkey) in 64 folders, using **291
  distinct cab models**.
- Names per drivable vehicle: average 62.8 (11 to 94); per cab model 65.4,
  of which 13.8 lamps and 4.5 universals; 307 of the 387 names used.
  mainctrl on 239 cab models, jointctrl on 52.
- Animations: rot 24,157; mov 10,125; wip 331; dgt 304; rotvar 64. Block
  syntax 27,823, old 7,701. `type:` return 1,676, push 199, impulse 51.
- Per cab model (of 291): sounds inc/dec 288, per-value sounds 283,
  **pyscreen 229** (koliber 121, timetable 120, ksiazka_wozu 76,
  en63_renderer 33, srg3000p 30, ETCS 19), wip 116, dgt 106.
- Most used (cab models / drivable vehicles): pipepress 285/618, brakepress
  284/617, brakectrl 282/640, cablight_sw 281/646, security_reset_bt
  280/641, radio_sw 269/627, main_on_bt 267/575, dimheadlights_sw 267/549,
  radiostop_sw 261/613, i-security_cabsignal 259/531, trainheating_sw
  255/567, dirkey 252/576, mainctrl 239/559, lvoltage 215/475, pantfront_sw
  207/443, compressor 196/506, hvoltage 180/460, compressor_sw 167/437,
  sand_bt 167/377, converter_sw 163/406, horn_bt 161/347, fuse_bt 149/329,
  manualbrake 128/339, battery_sw 123/300, localbrake 120/351, scndctrl 95.

## 4. TSRE's 3D cab today, and the mapping

- The chain (driving 08): `.cvf` → `CabView` controls, parts matched by
  `TYPE:order…` names (`CabView.h:15-137`); `Eng::poseCab` poses by key
  frame; `CabPicker` reads `SFileComplex` triangles only; the mouse in
  `RouteEditorGLWidget` (`cabPress`, `cabDrag`, `cabWheel`) sends `cab.<i>`;
  `LocoScript.cpp` turns it into loco commands through `tsre.cabview`, and
  `tsre.cabview.values` fills the frame's cab values.
- Reusable for MaSzyna: `E3dShape::setPartTransform` (rot/mov),
  `enableSubObjByName` (`_on`/`_off` lamps), `setSectionLight` (cab light),
  `setTextureOverride` (RmlUi screens instead of pyscreens), E3D parts'
  names, rest matrices and bounds. Missing: a reader for the controls,
  driver positions and sounds (`MaszynaVehicle` reads only the cab model
  names); picking on `.e3d` triangles.

| MaSzyna | TSRE command | Note |
|---|---|---|
| mainctrl | throttle | notch numbers, often over 40 |
| jointctrl | combined | ranges differ |
| brakectrl | trainBrake | the handle's own positions |
| localbrake | engineBrake | |
| dirkey, dir*_bt | direction | |
| alarmchain | emergency | approximate |
| releaser_bt | bailOff | |
| sand_bt | sander | |
| horn_bt, hornlow/high_bt, whistle_bt | horn | high/low tone lost |
| main_on/off_bt, main_sw | circuitBreaker | MaSzyna also toggles |
| main_on_bt (diesel) | engine | |
| pantfront/rear_sw, …off_sw, pantalloff_sw | pantograph1/2 | |
| battery_sw … | battery | |
| cabactivation_sw | masterKey | approximate |
| cablight_sw | cabLight | |
| wipers_sw | wipers | multi-position vs on/off |
| radio_sw | cabRadio | |
| mirrors_sw | mirrors | |
| door_*_sw | doorsLeft, doorsRight | |

Gauges with values TSRE has: tachometer (speed), brakepress
(brakeCylinder), pipepress (brakePipe), compressor (mainReservoir),
scndpress (mainPipe), limpipepress (equalisingReservoir), hvoltage
(lineVoltage), hvcurrent (ammeter), enrot1m (engineRpm), oilpress, temps,
clock. Lamps: mainbreaker, battery, cablight, radio, direction, doors,
converter (auxPower), compressor.

New in TSRE: second controller and shunt field; cruise control; hand
brake; brake profiles, modes, spring and EP brakes, anti-slip; relays and
overload resets; converter and compressor switches, pantograph selection,
train heating, inverters; diesel pumps and blowers; vigilance and cab
signal (security_reset_bt on 280 of 291 cabs); per-lamp head and marker
lights, dimmers; instrument and compartment lights; door permits; the
radio set; distance counter; voltage, current and pressure gauges.

## 5. What makes the bridge hard

1. Command handlers depend on which controls the cab has and their `type:`
   (push or toggle; Train.cpp:3091-3433): scripts need the list of labels
   and their types, not only commands.
2. Raw values and procedural animation: rot/mov from formulas, not key
   frames; friction smoothing and rotvar lerp; drag paths sampled over
   values; scripts publishing MaSzyna's units (notches, handle positions,
   km/h, MPa). The axes through TSRE's `.e3d` import to be checked.
3. Lamps are visibility with power gating; about 100 computed states, some
   on the exterior model.
4. `dgt` digits (106 cabs), `wip` wipers (116), analog clocks with fixed
   names; `.e3d` parts' own time animations (at_Seconds…) not read by
   `E3dFile` (not confirmed).
5. Python screens in 229 of 291 cabs: to port to RmlUi (timetable and
   ksiazka_wozu alone about 200 cabs).
6. Mouse: MaSzyna levers are a hidden-cursor vertical slider, left/right
   buttons increase/decrease; TSRE drags along the path, right button is
   the camera's.
7. Per-control sounds (inc/dec, on/off, per value, random sets,
   soundproofing); TSRE's 3D cab has none.
8. Parse quirks to reproduce: controls after the cab 2 section serve cab 1;
   cab 0 last; two syntaxes; random and parametrised includes; unknown
   labels dropped; picking needs `.e3d` triangles.
9. Joint controllers (52 cabs), second-unit `b` lamps and gauges, indexed
   tables.
