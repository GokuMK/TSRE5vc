# Task sound 04: a MaSzyna cab's windows wired to TSRE's

Design, not built (2026-11-16).

## Objective

Three different things are called a window here. This task joins the
first two; the third is not in it.

1. **TSRE's cab window.** Each side of the cab is open or shut. Ctrl+Q
   toggles the left one and Ctrl+Shift+Q the right one. This is what the
   sound reads (task sound 03): open, the outside comes in. On an ENG loco
   it also moves the shape's window parts.
2. **A MaSzyna window control.** A `universalN:` in the `.mmd` that moves
   the glass (or a door) in the cab model when clicked, and plays its
   click. Nothing else: in MaSzyna it changes no sound
   (`Train.cpp:6640-6680`). In TSRE today it is not joined to 1 either:
   clicking it moves the glass but the sound stays shut, and Ctrl+Q
   changes the sound but the glass stays where it is.
3. **MaSzyna's head-out view.** While Ctrl+Left or Ctrl+Right is held,
   MaSzyna's camera moves out to that side's mirror, looking back along
   the train (`drivermode.cpp:691-703`, `Global.CabWindowOpen`). In
   MaSzyna this, not the glass, is what lets the outside in. TSRE has no
   such view. A separate feature (the user), not in this task.

**What this task does, on a MaSzyna cab:** where a side has a window
control, it and TSRE's window on that side become one window.
- A click on the glass opens or shuts TSRE's window: the outside comes in.
- Ctrl+Q moves the glass and plays its click.
- The outside comes in as the glass moves, from where the glass is.

Where a side has no window control (the EN57 families, SM42 and others),
nothing changes: Ctrl+Q lets the outside in, nothing moves, and the window
is heard 0.7 m to that side of the head, as now.

Which universal is a window, and on which side, comes from the content
review `reports/mmd-cab-windows.md` (all 1461 `.mmd`, 406 distinct
driving cab ends).

## Decisions

The user, 2026-11-15 and 2026-11-16:
- TSRE's window (Ctrl+Q) and MaSzyna's head-out view are separate
  features. This task is the window.
- **Side doors count as an open side.** They let the outside in as a window
  does, so doors and windows need not be told apart.
- **Cabs with side glass but no control for it** (110 cab ends: the EN57
  families, SM42 and others): Ctrl+Q works there as now (the outside comes
  in, nothing moves, heard 0.7 m to that side of the head). Whether to move
  their glass too is to be thought over later.
- The 4e grey and EP08 windows written upside down (value 0 = open) start
  open in MaSzyna. The user left it to me: **they start shut**, as every
  other TSRE cab's windows do. MaSzyna opens them only as a side effect of
  its gauges starting at 0. Their authors knew value 1 shuts them (the
  `soundinc:` is the closing sound). An open window greeting the driver
  with the noise outside would look like a bug.

## What is there

- `maszyna.cab` (Lua) works a universal as a plain switch: a click flips
  its own state and sends `{universalN, on}` to the loco, which ignores it.
- Ctrl+Q and Ctrl+Shift+Q send `windowLeft` and `windowRight` (the driver's
  sides) to the loco script. `tsre.controls` keeps the four windows by Open
  Rails' numbering (`windowLeftFront` ... `windowRightRear`, the front or
  rear cab's own sides) and publishes them.
- `SimulationController` animates the car's window groups from those
  values. `Eng::soundWindows` hears each window by its group's
  `CarAnimation::progress`. A MaSzyna car has no window parts there, so a
  window is open after a second and placed 0.7 m to the side of the head.
- `MaszynaCabPose::controlPlace` gives a control's part in the vehicle's
  model space (the cab sounds' places). A control's `soundinc`/`sounddec`
  play when its value goes up or down, whatever moved it
  (`MaszynaSounds`, the cab controls' sounds).
- `E3dFile::read` shares a model read by path. The vehicle's width is
  `MaszynaVehicle::width` (`.fiz` `Dimensions: W=`).

## Which control is an opening (rule R5)

A `universalN` whose part is bound and moves (`mov`, `rot`, `wip`, scale
not 0) is an opening when its part tree's box (the part and every part
under it, at rest, in the model's frame):
1. has its centre at the side wall: |x| at least 0.8 of the cab model's
   half width (the narrower side of its box) **and** at least 0.75 of the
   vehicle's (`W/2`);
2. and holds glass (a texture named with `szyb`, `okn`, `glass`, `szkl`,
   `window`, or a translucent part), or is at least 1.2 m tall (a door; the
   303e-b, 4e-15xx and EP07P doors have no glass texture).

Its side: +x is cab 1's left, −x cab 2's (MaSzyna's mirrors,
`Train.cpp:10672`). Not names, not pivots: names are wrong 8 times in 214
(the EP03's are swapped), `universal1` is the right window 99 times and the
left 18, and the EP03's pivots are 10 m from the glass.

Scored against the review's hand-made truth with doors counted (a
throwaway script, as the review's): **214 of 214 cab ends with openings
right, 192 of 192 without**. The vehicle's width is what keeps out the
EN57 AKL/AKM locker doors: their cab model is the locker alone, so against
its own box they sit at its edge. The rule holds for any vehicle share
from 0.7 to 0.8. Its thresholds were chosen looking at this content, so
this is a fit, not a test; other authors' vehicles may need it widened.

How far open, 0 to 1: the part's shown value (after its friction), or
1 − value where a `mov` part's box is higher at value 1 than at 0 (the 9
upside-down drop windows of the 4e grey and EP08). Hinged, sliding and vent
windows and doors: the value.

A side's **main opening** is its largest (the box's height times its
length along the car). Ctrl+Q works it:
- the window on most cabs;
- the door on the 303e, 4e and EP08's left side, where the door is the
  only opening;
- the door on the WMB10, over its two 0.1 m panes;
- the window on the ST44, over its vent pane.

## Design

1. **The openings** (`MaszynaCabControls`, C++): when the vehicle's cab
   controls are made, its cab model is read (`E3dFile::read`, shared with
   the renderer) and each universal judged by R5. Kept per control: the
   side (the driver's left or right in that cab), whether it is its side's
   main opening, and whether it is upside down. Published to the Lua
   module with the control's other fields: `window = "left"` or `"right"`,
   `windowMain`, `windowInverted`.
2. **The click and Ctrl+Q as one** (`maszyna.cab`): a side's main opening
   is no longer a plain switch.
   - Its value is the loco's `windowLeft` or `windowRight` (1 − that when
     upside down), so Ctrl+Q moves it and its `soundinc`/`sounddec` play.
   - A click sends `windowLeft` or `windowRight` to the loco, as Ctrl+Q
     does.
   - The others (the ST44's vent, the WMB10's panes) stay plain switches,
     heard by their motion (3).
   - The loco's windows start shut, so the upside-down ones start at 1.
3. **Heard as it moves, where it is** (`Eng::soundWindows`): for a side of
   the ridden cab with openings, its opening is the largest of its controls'
   shown motion (new on `CabPose`: a control's shown value as an opening, 0
   to 1). It is placed at that control's part (`controlPlace`, through the
   car's matrix as now). A side without openings stays as it is: the loco's
   window, open after a second, 0.7 m from the head.
4. **What was found, in the log**: with `TSRE_SOUND_LOG`, each MaSzyna
   cab's openings once when it is first ridden (`Sound: MaSzyna cab <mmd>
   end 1: left universal4 plane19, right universal3 szyba_mechu (main)`),
   to check other authors' content.

The window sounds: MaSzyna's cab plays the control's own (`soundinc`,
`sounddec`). `tsre.controls` also sends the ENG window triggers (260, 261),
which a MaSzyna car ignores.

## Tests

- `maszyna-cab` (the ET22, 303e, ST44, 4e grey, EN57 AKL cab models from
  `--game-root ~/ORTS/maszyna`):
  - the ET22: `universal3` right, `universal4` left, both main;
  - the 303e: the door on the left, the window on the right;
  - the ST44: the window main, the vent not;
  - the 4e grey: upside down;
  - the EN57 AKL: the locker doors not openings;
  - no other universal of those cabs is an opening.
- `mmd-cab-script`:
  - a main opening's value follows `windowLeft`, and an upside-down one is
    1 − that;
  - a click sends `windowLeft`;
  - a second opening stays a plain switch.
- `mmd-sound-player`: a window's opening follows the control's shown
  value through its friction, placed at its part; a side without openings
  as before.
- Live: the ET22 in the cab, Ctrl+Q: the pane goes down in a capture, the
  window's sound plays (`TSRE_SOUND_LOG`), and its share rises over the
  control's friction.

## Not in this task

- MaSzyna's head-out view (3 in the objective): a separate feature.
- Moving the glass of cabs without a window control. Ctrl+Q already works
  there (the outside comes in); only the glass stays still. For later.
- An opening's size: the WMB10's 0.1 m panes count as fully open at
  0.1 m, as loud as a dropped window. Weighting by the open area would be
  TSRE's own; nothing in MaSzyna to follow.
- Passenger views' windows.
