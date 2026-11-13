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

## Tests

- `sound` (113): two open windows: an outside sound as loud as outside, not
  louder; the ridden car's outside sound off in its cab, a window open as
  loud as outside at the window, two open as loud as outside at their
  middle, half open half, shut off.
- `mmd-sound-player` (42): MaSzyna's external sound from its cab shut 0.45,
  window open 0.81 (its window and outside columns, Some), half open
  halfway; an inside sound as shut; another car's through the window.
- `car-animation` (18): the windows' way and place; a car without window
  parts open or shut as set.

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
