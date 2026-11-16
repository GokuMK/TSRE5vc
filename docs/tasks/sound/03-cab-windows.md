# Task sound 03: open cab windows

## Objective

The ridden cab's open windows let the outside in. Until now TSRE's cab
windows (`ORTS_LEFTWINDOW`, `ORTS_RIGHTWINDOW`, Ctrl+Q and Ctrl+Shift+Q,
task driving 08) only moved: no sound read them, neither an ENG's `.sms`
nor a MaSzyna car's.

## Decisions (the user, 2026-11-13)

- Not a hard switch: an outside sound coming in at the open window, its
  share growing as the window opens.
- Two open windows never louder than the outside.
- Three parts: the route's outside track sound for the ridden car, the
  ridden car's own outside `.sms`, MaSzyna's soundproofing toward its open
  window column.

## What was there

- The cab view (`SoundListener::view` Cab, the camera in the car's 3D cab)
  plays the ridden car's cab `.sms` and its inside track sound; its outside
  `.sms` groups (`Activation ( ExternalCam )` only) and its outside track
  sound are off there.
- Every other outside sound (the train's other cars, other trains, the
  route's sound sources, level crossings) plays in a cab at half its volume
  (`SoundSource::OutsideHeardInside`, Open Rails' default pass-through): a
  flat gain, no muffling.
- MaSzyna cars: their own soundproofing tables (task maszyna 05), with an
  open window column (3) that only MaSzyna's head-out view uses.
- The engine (miniaudio) only pans; each source works out its gain with
  distance itself (Open Rails' model).

## Design (built 2026-11-13, `feature/maszyna`)

1. **The listener's windows** (`SoundListener::windows`, at most two):
   where each open window of the ridden cab is and how far open, 0 to 1.
   `windowShare()`: their openings added, at most 1 (two open windows let in
   no more than the outside); `windowPlace()`: their places weighted by how
   far open. In the cab view only.
2. **The car's windows** (`Eng::soundWindows`, called where the listener is
   made, `RouteEditorGLWidget::updateSound`): the ridden cab's two (Open
   Rails' numbering: the front cab's `WindowLeftFront`, `WindowRightFront`,
   the rear's `...Rear`); open by their animation's way
   (`CarAnimation::progress`), followed no faster than open in a second (a
   window without parts, set at once, opens in a second); placed at their
   first part through the car's drawing matrix (`CarAnimation::place`,
   `CarPlacement::placeCar`, as the cab's head is placed), else 0.7 m to the
   driver's left or right of the listener.
3. **Through a window** (`SoundSource`): a source of the ridden car whose
   group plays outside only becomes active in the cab while a window is
   open, its voices at the windows' place, its gain as outside at that
   place (its distance curves and roll-off from the source to the window)
   times the windows' share, so never above the outside level; shut, it goes
   off again. That is the ridden car's outside `.sms` and its outside track
   sound (`TrackSound` keeps both; nothing changed there).
4. **The other outside sounds**: their half volume in a cab goes toward the
   whole by the windows' share (`OutsideHeardInside + share × (1 −
   OutsideHeardInside)`), from where they are. One line beside 3.
5. **MaSzyna** (`MaszynaSounds::soundproofing(..., window)`): from a cab
   (columns 0 and 2), a sound not of the cab's own (any placement but
   Internal and General) goes toward its open window column by the share,
   never quieter than shut, never louder than its car's outside (following
   it, column 4); another car's sound heard through the listener's car goes
   toward that car's open window column (at most 1). Its emitters stay where
   they are (no second voice at the window).
6. **MaSzyna's running noise outside** (2026-11-14, the user's review):
   the ridden train's `outernoise:` (per bogie), kept from those aboard,
   comes in by the cab's open windows as MaSzyna's head-out view hears it
   (`Global.CabWindowOpen`, DynObj.cpp): as at a fully open window (the
   window column) times the windows' share, so growing as they open, as
   loud as outside fully open (MaSzyna's window and outside columns are
   both Some); shut, off again. The cab's own noises stay as they are (the
   user: fading them as a window opens, as MaSzyna's head-out view stops
   them, would make it worse).

## Tests

- `sound` (113): two open windows: an outside sound as loud as outside, not
  louder; the ridden car's outside sound off in its cab, a window open as
  loud as outside at the window, two open as loud as outside at their
  middle, half open half, shut off.
- `mmd-sound-player` (43): MaSzyna's external sound from its cab shut 0.45,
  window open 0.81 (its window and outside columns, Some), half open
  halfway; an inside sound as shut; another car's through the window; the
  ridden car's running noise outside off in its cab, a window open as
  outside, half open half.
- `car-animation` (18): the windows' way and place; a car without window
  parts open or shut as set.

## The user's review (2026-11-14)

The ET22's cab: "always too much constant noise", opening a window "no
difference", the motors much quieter than in the user's video of
2026-11-13 00:07 (a build before the merge ea72d2d1:
https://www.youtube.com/watch?v=6feHbpgL71E), "fans?" outside.

Headless (USA2, `evegrain.act`, the lone ET22, `TSRE_SOUND_LOG`): in the
cab, shut, the converter 0.40 standing to 0.6 running (`placement: custom`;
the ET22's own `soundproofing:` row 0.6 → 0.78 in the cab, 0.81 at the
window, so the window hardly changes it), the cab's `runningnoise:`
0.25-0.34, the motors under power 0.15-0.22 (coasting 0.03), the fans
0.10-0.21; a window open: the motors and fans ×2.5, now the bogies' running
noise 0.04-0.17 (at 30-36 km/h), the converter and the cab's running noise
as shut. Reading the merge's diff, nothing it changed drives the
converter, the motors, the fans or the running noises in a dry drive; what
it added in the cab: the emergency valve's hiss (full while the pipe
vents, some 20 s after a vigilance trip), rain (the activity's weather),
the direction relays and the brake cylinder's clicks, the windows. The
user's ears say otherwise: the hardware check below.

### To check on the hardware build

1. `TSRE_SOUND_LOG=1`: every 5 s `log.txt` has a line per MaSzyna car
   heard, `Sound: MaSzyna <car> window <share>: <key>=<gain> ...` (each
   key's loudest voice; `cab:` the cab's own). Drive the case of the video
   (CMK, the same speeds and notches): which keys are there that the ear
   does not expect, and the motors' (`tractionmotor`) against the
   converter's and `cab:runningnoise`.
2. Each sound off, in the vehicle's `.mmd` (the variant the consist uses;
   `//` before a one-line key, `/* */` around a `{ }` block over several
   lines; TSRE restarted): the converter and the compressor (`include [
   converter_compressor_set1.inc ... ]`), `ventilator:`, the cab's
   `runningnoise: { }` (in `internaldata:`), `outernoise:`, the motors
   (`tractionmotor: { }`). What makes the "constant noise", what masks the
   motors.
3. `TSRE_MASZYNA_RUNNING_NOISE=vehicle`: the route's track sound off for
   MaSzyna cars (the user: not the issue, motors were clear with it).
4. The video's build, if it is still there, on the same drive with
   `TSRE_SOUND_LOG=1`: its lines name the keys heard (no gains), so a key
   heard only in the new build shows.
5. A window open (Ctrl+Q) while running: the bogies' running noise coming
   in, growing as it opens.

### The comparison with MaSzyna (2026-11-15)

The same drive in TSRE and in MaSzyna's own build, `extra/sound-compare`.

The drive:
- the ET22 started step by step (in TSRE the battery stays on, see below);
- notched up every 2 s to 100 km/h;
- coasting, then service braking to a stop.

Each side logs every voice once a second. In TSRE that log is
`TSRE_SOUND_CSV`; in MaSzyna it is a log patch on eu07 66a940d4, built from
a copy of the source. Each side also records its mix: `TSRE_SOUND_RECORD`
on miniaudio's null device, and OpenAL Soft's wave writer.

The first comparison was wrong on MaSzyna's side. MaSzyna's cold ET22
stood with its handbrake on (position 20 of 20), and the driver powered
against it. The motors drew twice the current, so MaSzyna's motors and
blowers sounded much louder than they should. TSRE's run was also on
USA2's grades, at 3000 V against MaSzyna's 3600 V source, and notched every
2.3 s instead of every 2 s.

The corrected runs (2026-11-15):
- MaSzyna: the handbrake taken off while the compressor fills, a 3000 V
  supply.
- TSRE: `TSRE_LEVEL_TRACK`, notches every 2 s.

With those, the current at the same notch is within 4-8% (as
`extra/train-compare` found for the traction physics).

Fixed in TSRE, each one found by this comparison:
1. **The resistors' fans:** `RVentMinI` defaulted to 0. MaSzyna's default
   is 50 A. The ET22's fans had played at 1.56 times MaSzyna's level.
2. **The running noises:** they are now scaled by MaSzyna's track-quality
   factor at quality 20, × 1.2. Quality 20 is what every track in
   MaSzyna's sceneries has.
3. **The motors' power for their sound:** MaSzyna's `EnginePower` for series
   motors is the current × (1 + the notch's motors in series) × the voltage.
   TSRE's port had left out the `(1 + Mn)` factor.
4. **The sounds' height:** TSRE draws every car 0.28 m over the track's
   points (`CarPlacement::RailHeight`), its cab's head with it. MaSzyna's
   sounds were placed from the points themselves, 0.28 m under the model
   and the head: the motors under the floor further away (0.91 of
   MaSzyna's), the clock over the desk too.
5. **The cab clock:** `clock: analog` turns MaSzyna's three hands by the
   simulation's clock (loco scripts' `inputs.time`), and the seconds' hand
   ticks with `clock_seconds:`'s sounds, where its first part placed them.
   TSRE had neither.
6. **The cab's sounds at their controls:** as Train.cpp places those without
   a place of their own: the speed recorder's tick at the first speedometer,
   the brake's hisses at the brake handle, the local brake's at the local
   brake, the vigilance buzzers at its lamp. TSRE placed only the
   controllers' and the reverser's; the ET22's speed recorder ticked from the
   cab's middle.

Also in the kit: MaSzyna's cab view starts at the seated head
(`driver1sitpos:`, drivermode.cpp `CabView`), TSRE's at the standing one
([1] goes to the seated one). The drive now rides seated (`--cab-place 1`);
the capture's live run took the view's cab place only for its capture
after the run until now. The compressor is compared standing after the
drive (TSRE's hot reservoir is full at the start). The listener's place in
the car's frame is in `state.csv` (`listener`).

Levels in the cab now, windows shut, TSRE / MaSzyna (seated, level track,
3000 V, notches every 2 s):

| Sound | TSRE | MaSzyna | Ratio |
|---|---|---|---|
| Converter | 0.375 | 0.373 | 1.00 |
| Compressor (standing) | 0.482 | 0.482 | 1.00 |
| Running noise, powering | 0.40 | 0.40 | 1.01 |
| Running noise, at 100 km/h | 0.52 | 0.52 | 1.00 |
| Traction motors, powering | 0.205 | 0.208 | 0.99 |
| Motor blowers, powering | 0.059 | 0.060 | 0.97 |
| Speed recorder (`tachoclock`) | 0.208 | 0.214 | 0.97 |
| Clock's tick (`clock_seconds`) | 0.063 | 0.065 | 0.97 |
| Blocks' squeal (`brake`), braking | 0.27 | 0.27 | 1.00 |
| Blocks' noise (`brakesound`), braking | 0.16 | 0.16 | 1.00 |
| `wheel_clatter` | not played | 0.05–0.07 a joint | — |

The brake noises (2026-11-16) were 0.46 and 0.76 of MaSzyna's:
- TSRE's drive braked lighter: MaSzyna's service key puts an FV4a at
  position 4 (`BrakeCtrlPosNo / 2 + 1`, Train.cpp:2018), the ET22's 3.4 bar
  row; TSRE's `0.45` was position 2. Now `--brake 0.66` (a lever value
  takes the highest notch at or under it; position 4 is 0.6545).
- **Fixed in TSRE:** the brake force ratio the noises follow is MaSzyna's
  (`MaszynaBrake::forceRatio`): a block's force against its force at the
  most pressure, both with the blocks' friction at the speed. TSRE had the
  cylinder's share of its most; the ET22's cast iron grips better pressed
  less (2/3 of MaxBP at 80 km/h: 0.78, not 0.66).

The stop differed too (TSRE's cylinder 4.2 bar, MaSzyna's 3.58; 35 s and
44 s from 100 km/h). **MaSzyna's drive, not TSRE.** MaSzyna's log now has
the brake's reservoirs:
- The ET22's LSt fills the cylinder to (control reservoir − pipe) × 2.8.
- Its cold start empties the pipe. The releaser the drive holds to charge
  it again also vents the distributor's control reservoir, from 5.0 to
  3.26 bar.
- That reservoir refills from the pipe in about three minutes. At the
  braking it had 4.68 bar: (4.68 − 3.40) × 2.8 = 3.57 bar in the cylinder.
- TSRE's ET22 starts with it full (4.99): (4.99 − 3.40) × 2.8 is above the
  4.2 bar the cylinder is held at.
- The auxiliary reservoir is not it: the LSt feeds it from the main
  reservoir pipe in both, 8.2-8.4 bar.

MaSzyna's drive now waits for the control reservoir (4.95 bar) before
powering. Then MaSzyna's cylinder fills to 4.2 bar too, the stop takes 39 s
against TSRE's 35 s (USA2's curves; MaSzyna's track is straight), and the
brake noises match (1.00, above).

The motors in the last 10 s of power are 0.89 of MaSzyna's, from the drive
too: MaSzyna's notches its shunting steps above 100 km/h (to 103 km/h,
285 A), TSRE's stops at 99. From 80 to 100 km/h both are 0.237.

Left: the wheels' clatter at rail joints (later, the user 2026-11-15).

Found on the way:
- **The view tick heard the camera a tick late.** The listener was taken
  before the tick placed the trains, while the ridden car's sounds were
  placed after. A moving train's sounds were therefore out of place by its
  travel in one frame. At this machine's few frames a second that is
  metres, and the converter played at 0.78 instead of 0.39. On the user's
  hardware it is about 0.5 m at 100 km/h.
  - Fixed: `timerEvent` now places the trains, then the camera, then the
    sound (`diagnosticTick`'s order).
- **The brake pipe not charging after it emptied: MaSzyna's own lock, not
  a bug.** First seen after a battery off and on (the security system
  vents the pipe), then after the emergency position with the battery on:
  at Overcharge the handle's gauge shows 5 bar, but the pipe and the
  equalising reservoir stay empty. MaSzyna's FV4a feeds no pipe under
  2.75 bar unless the releaser is held (Mover.cpp:4475, `lock_old`), and
  `MaszynaHandle` ports that condition (the releaser: BailOff, `/`).
  MaSzyna's driver in the comparison holds it for the same reason. In TSRE:
  hold `/` at Release or Overcharge until the pipe has 3 bar.
- **The ET22's window glass is a cab control.** The ET22's windows are
  `universal3` and `universal4` in its `.mmd`. Clicking them slides the
  glass, but the loco's `windowLeft`/`windowRight` never change, so the
  sound does not either. In MaSzyna this control is cosmetic too: its
  head-out view is what changes the sound.
- **Ctrl+Q's outside noise is not placed at the left window**, as the
  design says. The user's report. Task 04 (2026-11-16) places a MaSzyna
  cab's at its glass, and found the rear cab's window keys heard on the
  driver's other side, now fixed; whether that was the user's case is not
  confirmed.

## Next: wiring a MaSzyna cab's window glass

MaSzyna gives the glass no meaning (a `universalN` moving a part). The
content review, `reports/mmd-cab-windows.md` (2026-11-16): all 324 window
controls are universals, left and right only from the geometry, no label
convention; a rule from moving glass at the side wall (or window words in
the names or sounds, and moving at the side wall) fits all 406 distinct cab
ends. The user's answers and the design: task
[`04-maszyna-cab-windows.md`](04-maszyna-cab-windows.md).

## Not in this task

- **The closed cab's muffling**: outside sounds in a cab are only half as
  loud now, not duller. Miniaudio can route those voices through a shared
  low-pass filter (a node every outside voice is attached to while the
  listener is in a cab), with the windows taking it away by their share.
  Not big: a filter node, the voices' routing chosen each update, its
  cut-off tuned by ear on the user's hardware.
- **Air rushing past an open window** (the user's idea, 2026-11-13): the one
  sound only an open window has. No ENG content has one, so it is TSRE's
  own: a recording in appdata (with a licence that allows it), or noise made
  on the fly (miniaudio's `ma_noise`, pink or brown, through a low-pass),
  its gain growing with the speed squared and the windows' share, its
  cut-off with the speed, placed at the window. Small in code; the work is
  making it sound right, which needs listening on real speakers (this
  machine has no sound device: levels only). MaSzyna's cabs have their own
  `windsound` (heard shut), which it would add to.
- A passenger view's windows; MaSzyna's emitters moved to the window.
