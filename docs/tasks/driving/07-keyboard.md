# Task 07 - Keyboard

Status: designed 2026-10-30; the user's answers in (2026-10-30, at the
end). Done 2026-10-30 on `feature/driving` (steps 1 to 5), before the
merge; not merged. Reopened 2026-10-30: the camera flies while a mouse
button is held, and Drive turns the editing tool off (the user's, at the
end). Done 2026-10-30 on `feature/driving`.

The user's request (2026-10-30), one more task before the merge: "If
camera object supports key overtake, editor widget should pass key event
in there and avoid executing editor keys. Something like it is done now
with map mode. Review and propose design. Then, for now, we could propose
train key support based on orts defaults."

**What it is:**
- An object the camera rides can take the view's keys. While it does,
  the view gives it every key first. The editor's own keys and shortcuts
  do nothing, as map mode keeps the 3D keys out today.
- The driven train's consist takes them. Its keys are Open Rails'
  defaults for the controls TSRE's loco scripts have, sent as the same
  commands the driving panel sends.

## Today (the review)

**Where a key goes in the editor** (`RouteEditorGLWidget::keyPressEvent`):
1. The UI router (`Ui::Screen::keyPress`, task scripting 01) takes it
   when a menu or a text field has the keys. With the view's keys it
   takes Esc (to a menu handler, none yet) and F8 (RmlUi's debugger).
2. Ctrl+Shift+F10 (TexLib's statistics) and `` ` `` (the map) always.
3. **Map mode:** the active map tool, then the map camera; then it
   returns. The 3D keys never run. This is the pattern to follow.
4. Esc ends a live Flex, Ruler or Telepole line. F grades the last Flex.
5. 1, 2 and 3 choose the followed train's view: cab, ahead, behind.
6. The camera (`Camera::keyDown`).
7. The editor's keys: Ctrl, Shift and Alt set the move step, B the
   terrain tile dialog, R, T, Y and Q the tools, Home a jump. Then the
   active tool's keys (`EditorTool::key`). An undo state starts on every
   key.

**Before all of that, the window's shortcuts** (Qt resolves them before the
widget sees the key):
- `E` select tool, `/` terrain camera, Ctrl+Z, Ctrl+C, Ctrl+V,
  Shift+Ctrl+S, F1 to F5 tool windows, F12 settings, Alt+F4.
- The menus' Alt+letters: Alt+R, E, 3, M, W, T, S, H (in English).
- Open Rails' bail off is `/`, its gear up `E`, its AI fire on Alt+H. The
  shortcut would win today.

**On the QRhi backend** the view is a `QWindow` in a container. That
window forwards keys and the mouse to the widget (`RhiRenderSurface`), but
not `ShortcutOverride`, which Qt sends to the focused window.

**Esc:** with no menu handler the router lets it through.
- `RouteEditorWindow::keyPressEvent` closes the editor on Esc. Task
  scripting 01's notes say Esc in the view gets there.
- Reading Qt, it shouldn't: the view's `keyPressEvent` leaves every key
  accepted, so none goes on to the window. To be checked in step 1.

**Tab** never reaches `keyPressEvent`: `QWidget::event` moves the focus
with it first. Open Rails' signal keys are on Tab; TSRE's will be on
another key (the user's, 2026-10-30).

**The free camera** (`CameraFree`, the one that rides trains):
- W, A, S and D move it: around the train, or the head inside a 3D cab
  within its mount's box. Space with W or S moves it up and down.
  Shift and Ctrl set its speed. `.` locks its height.
- The arrows move it only with the numeric keypad setting
  (`core.camera.useNumericKeypad`, off by default).
- The mouse turns it. `releaseKeys()` lets go of held keys when the UI
  takes the keys or the view loses focus.
- Open Rails' throttle is D and A and its reverser W and S. These clash.

**What it rides:** `Camera::getCameraObject()`, a `GameObj`. Riding a
simulated train it is the train's `Consist` (`SimulationController::
consistOf`). `GameObj` already has the riding hooks (`getCameraPosition`,
`getCameraMount`, `cameraAttached`). `Consist` knows nothing of the
simulation; the controller makes and places the consists.

**Commands:** `SimulationController::command(name, value)` goes to the
driven cab. The panel (`loco.rml`) uses names like `throttle "+"`,
`pantograph1 "up"`, `horn "on"`/`"off"` and `circuitBreaker "close"`/
`"released"`. The scripts also have held commands, `throttleIncrease`
true/false and the like, which move a lever while held at 5 steps a
second.

## Open Rails, as a functional reference

- **Keys are physical positions.** `InputSettings.cs` holds scan codes
  (set 1, a US keyboard's places), so on a German or French keyboard the
  same hand positions drive. Shift, Ctrl and Alt must match exactly.
- **One key, several commands.** O is the circuit breaker's closing order,
  the traction cut-off relay's closing order and the steam injector 2. P
  is pantograph 1 and the cylinder compound. The loco uses what it has.
- **Levers start, then move.** A press starts the lever moving and the
  release stops it (`StartIncrease`/`StopIncrease`). A notched lever
  without a smooth notch jumps to the next notch at the press, so a
  short tap moves it once. TSRE's held commands start moving without
  that jump, so a short press of a notched lever does nothing yet.
- **Push buttons** (horn, bail off, breaker orders) act while held.
  **Switches** (pantographs, cylinder cocks, master key) toggle at each
  press. **Steps** (headlight) move once a press.
- **Cameras** 1 cab, 2 ahead, 3 behind: the same as TSRE's follow keys.
  The arrows move and turn the camera.

## The design

### 1. An object takes the keys

```cpp
// src/tsre/KeyTaker.h
// What takes the view's keys from it (task driving 07): an object the
// camera rides and controls, a driven train's cab. Before the editor's keys.
class KeyTaker {
public:
    virtual ~KeyTaker() = default;
    // Pressed or released (auto-repeats included); true: used.
    virtual bool keyPress(const QKeyEvent &event) = 0;
    virtual bool keyRelease(const QKeyEvent &event) = 0;
    // Lets go of what it holds, as if released.
    virtual void releaseKeys() = 0;
};
```

- `GameObj` gets `virtual KeyTaker *keyTaker()`, returning nullptr by
  default.
- `Consist` returns its `keys` pointer, null unless it is driven. The
  simulation controller sets the pointer and clears it. A future object
  (a turntable, a crane, a car driven in the consist editor) does the
  same.
- The camera rides only in 3D, so map mode keeps the map's keys. A free
  camera flown off the train gets the editor's keys back. Keys drive only
  while the view rides the driven train. Open Rails drives with any camera,
  but this is the user's wish: the camera object decides.

### 2. The view's keys while taken (`RouteEditorGLWidget`)

When `camera->getCameraObject()->keyTaker()` is not null, a press goes:
1. The UI router, as now: a menu or text field, F8.
2. `` ` `` (map) and Ctrl+Shift+F10, as now.
3. 1, 2 and 3, the follow views, as now.
4. The taker.
5. If the taker didn't use it, the camera. Then it returns, as map mode
   does. The editor's keys, the tools' keys, the live line keys and the
   undo state never run.

Also, while keys are taken:
- **Modifier states:** Ctrl, Shift and Alt still update the move step
  and the modifier flags, so mouse tools see true states.
- **Esc** is used and does nothing. The editor stays open whatever step
  1 finds. Open Rails' Esc is its pause menu, and TSRE's game menu is a
  later task, its handler already in the router.
- **Releases** go to the taker and the camera. The camera always gets
  them, so it never keeps moving.
- **Shortcuts:** the widget overrides `event()`. On
  `QEvent::ShortcutOverride` it accepts every key except F1 to F12 and
  Alt+F4, so E, `/`, Ctrl+Z and the menus' Alt+letters reach the taker.
  The tool windows (F-keys) and the settings still open. Undo can't change
  the world under a running train. On the QRhi backend the view's window
  forwards `ShortcutOverride` to the widget, as it forwards keys, and
  gives back the widget's answer.
- **Letting go:** the widget remembers the taker that got the last
  press. It calls that taker's `releaseKeys()` when:
  - the taker changes (map mode, another view, the train left);
  - the view loses focus;
  - the UI takes the keys (`setKeysTaken`, beside the camera's).
  A held D then never leaves the throttle moving.
- **Focus:** when the view starts riding the driven train, it takes the
  keyboard focus. Drive is pressed in the Timetable window, and its keys
  would otherwise go there.

**The camera riding a taker:** in `CameraFree`, while its object has a
taker, the arrows move it as W, A, S and D otherwise do, whatever the
keypad setting. That setting is for the editor's tools (the user's,
2026-10-30). The taker uses W, A, S, D,
Space and `.` first. Shift and Ctrl alone still set the camera's speed.
The mouse turns it, as now.

### 3. Physical keys (`src/tsre/input/KeyPosition.h/.cpp`)

The positions are Open Rails' scan codes (set 1), with 0x100 marking the
grey keys (Insert, Delete, Home, End, Page Up/Down, arrows, Pause).
- **Linux** (xcb and Wayland): `nativeScanCode()` is the XKB keycode,
  the evdev code + 8. evdev equals set 1 on the main block; a short table
  covers the grey keys (evdev 102 to 111, 119).
- **Windows:** `nativeScanCode()` is already set 1, with 0x100 for
  extended keys.
- **No native code** (tests built with plain `QKeyEvent`s, other
  platforms): Qt's key is mapped to its US position.
- **Names:** US key names, as Open Rails' docs and help show them:
  "Shift+P", "Ctrl+;", "Alt+U", "Insert", "Space".
  `KeyPosition::parse`/`name` read and write them, for the table and later
  for the settings.

This is why the bindings don't use `QKeyEvent::key()`. It follows the
layout (Z and Y swap on a German keyboard), and Shift changes
punctuation (Shift+; arrives as `:`). Open Rails' Shift+; and Shift+'
(the handbrake) could not be matched that way.

### 4. The train's keys (`src/tsre/sim/DrivingKeys.h/.cpp`)

`Sim::DrivingKeys : KeyTaker`. It is the controller's, on the main thread,
one for the driven train.
- `drive()` sets it on the driven train's consist, and so does placing a
  consist made later.
- `leave()` and a reset clear it and call `releaseKeys()`.
- It sends through `SimulationController::command`.

**A binding** is a key with exact modifiers, a kind, and one or more
commands. Every command is sent. A loco without the part ignores its
command, as Open Rails lets the loco choose.

| Kind | Press | Release |
| --- | --- | --- |
| hold | `xIncrease` true | `xIncrease` false |
| button | its on value ("on", "close", "open") | its off value ("off", "released") |
| toggle | "toggle" | nothing |
| step | its value ("+", "-", 0) | nothing |

- **Auto-repeats** are ignored, presses and releases both. On X11 a
  held key repeats as press-release pairs.
- **Releases:** a press remembers what it started by its position. The
  release ends that, whatever the modifiers are then. Shift+R pressed,
  Shift let go, then R let go: the firing rate still stops falling.

**The defaults:** Open Rails' keys for what TSRE's scripts have now (a
US keyboard's names).

| Open Rails | Key | Kind | TSRE command |
| --- | --- | --- | --- |
| ThrottleIncrease / Decrease | D / A | hold | throttle (steam: the regulator) |
| ThrottleZero | Ctrl+A | step | throttle 0 |
| Forwards / Backwards | W / S | hold | direction, new (step at the press; steam: the reverser while held) |
| TrainBrakeIncrease / Decrease | ' / ; | hold | trainBrake |
| TrainBrakeZero | Ctrl+; | step | trainBrake 0 |
| EngineBrakeIncrease / Decrease | ] / [ | hold | engineBrake |
| DynamicBrakeIncrease / Decrease | . / , | hold | dynamicBrake |
| BailOff | / | button | bailOff |
| EmergencyPushButton | Backspace | toggle | emergency |
| Horn | Space | button | horn |
| HeadlightIncrease / Decrease | H / Shift+H | step | headlight +/- |
| Pantograph1 to 4 | P, Shift+P, Ctrl+P, Ctrl+Shift+P | toggle | pantograph1 to 4 |
| CircuitBreakerClosingOrder, TractionCutOffRelayClosingOrder, Injector2 | O | button, button, toggle | circuitBreaker close, tractionCutOffRelay close, injector2 |
| ...OpeningOrder (both), Injector1 | I | button, button, toggle | circuitBreaker open, tractionCutOffRelay open, injector1 |
| CircuitBreakerClosingAuthorization | Shift+O | toggle | circuitBreakerAuthorization |
| BatterySwitchClose / Open | Insert / Ctrl+Insert | step | battery on / off |
| MasterKey | Enter | toggle | masterKey |
| DieselPlayer / DieselHelper | Shift+Y / Ctrl+Y | toggle | engine / helperEngines |
| SteamHeatIncrease / Decrease | Alt+U / Alt+D | hold | steamHeat |
| ImmediateRefill | Ctrl+T | step | refuel and refill |
| FiringRateIncrease / Decrease | R / Shift+R | hold | firingRate |
| FireShovelFull | Ctrl+R | step | shovel |
| FireboxOpen / Close | F / Shift+F | hold | fireDoor |
| Firing | Ctrl+F | toggle | manualFiring |
| AIFireOn / Off / Reset | Alt+H / Ctrl+H / Ctrl+Alt+H | step | fireOn / fireOff / fireReset |
| BlowerIncrease / Decrease | N / Shift+N | hold | blower |
| DamperIncrease / Decrease | M / Shift+M | hold | damper |
| Injector1Increase / Decrease | K / Shift+K | hold | injector1Water |
| Injector2Increase / Decrease | L / Shift+L | hold | injector2Water |
| CylinderCocks / BlowdownValve | C / Shift+C | toggle | cylinderCocks / blowdown |
| GamePause | Pause | - | the run paused and resumed (the controller's) |

**Bound later, with their parts** (Open Rails' keys, kept free):
- sander X, bell B, alerter Z;
- doors Q / Shift+Q, wiper V, cab light L;
- gear E / Shift+E, handbrake Shift+' / Shift+;, retainers Shift+[ / ];
- brake hoses `\`, initialize brakes Shift+/, quick release Ctrl+/;
- water scoop Y, ejectors J, the cylinder compound P;
- electric train supply Alt+B, refill at a pickup T;
- cruise control, DPU and TCS keys;
- the switches G / Shift+G, change cab Ctrl+E, uncouple U, autopilot
  Alt+A; the signals on a key other than Open Rails' Tab.

**The scripts' side** (`tsre/*.lua`; the panel needs none of it):
- **"toggle"** for the switches: `util.switched(value, current)`, used by
  pantographs, emergency, master key, engine, helper engines, breaker
  authorization, injectors, cylinder cocks, blowdown and manual firing. The
  panel's `locoToggle` can send it too: it reads no stale values.
- **`directionIncrease` / `directionDecrease`:** electric and diesel step
  the direction at the press. Steam aliases them to the reverser's held
  commands, as it aliases the regulator to the throttle.
- **The notch jump:** a held command's start on a notched lever moves one
  notch at once, as Open Rails' `StartIncrease` does (`levers.start`).
  Then it moves while held.

### 5. Seeing the keys

- The driving panel gets a **Keys** toggle beside **All**, listing the
  bindings from `train.keys` (names, keys) in the user's language. It
  is the panel's version of Open Rails' F1 help.
- The bindings are fixed in this task. The user's own keys come later as
  settings (`driving.keys.<command>`, text like "Shift+P", the settings'
  `KeySequence` type, which exists unused). Unless the user wants them now
  (question 3).

## Steps

1. **The takeover:**
   - `KeyTaker`, `GameObj::keyTaker`, the consist's pointer;
   - the view's routing while taken, the shortcut override (both
     backends), Esc, letting go, the focus, the camera's arrows;
   - the routing decision as a small function, checked with a test taker.
2. **Physical keys:** `KeyPosition` with the Linux and Windows codes,
   names and parsing. Checked from native codes and names.
3. **The train's keys:**
   - `DrivingKeys` with the table and the controller's wiring;
   - the scripts' "toggle", direction holds and notch jump; Pause.
   - Checked in the driving suite: a key's commands, a hold's release
     with the modifier let go first, auto-repeats, `releaseKeys`. Then the
     three locos driven by keys only: electric (pantograph, breaker,
     throttle), diesel (engine, throttle), steam (regulator, reverser,
     injector).
4. **The panel's Keys list,** with English and Polish texts.
5. **End to end** under Xvfb with xdotool (keys held across frames, as
   for Open Rails): a train driven on EUROPE1 by keys, the editor's keys
   (R, Q, E, `/`, Ctrl+Z) doing nothing meanwhile, Esc not closing it, the
   map's keys back in map mode. The results in this doc.

## Questions for the user

1. **Physical keys or characters:**
   - Bind physical positions, as Open Rails does (recommended): the same
     places on any layout, and Open Rails' shifted punctuation keys work.
   - The alternative is Qt's characters: simpler, but a German keyboard
     swaps Y and Z, and Shift+; cannot be told from `:`.
2. **Esc while driving:** it does nothing until the game menu exists
   (recommended). The alternatives: Esc leaves driving, or pauses.
3. **The user's own keys:** fixed defaults now, settings later
   (recommended), or the settings in this task?
4. **The window's shortcuts while driving:** all give way except F1 to
   F12 and Alt+F4 (recommended). The alternative: only the keys the train
   binds give way, so Ctrl+Z still undoes while driving.

## The user's answers (2026-10-30)

1. "Ok": physical keys.
2. "Ok": Esc does nothing while driving.
   - The user: "Esc is handled in main window events, it should be
     reworked, but for now doesn't matter."
3. "Bindings next task": the user's own keys as settings in the next
   task. This task's bindings are fixed.
4. "Ok": every window shortcut gives way while driving, except F1 to F12
   and Alt+F4.

**The user's remarks:**
- **The arrows:** driving uses them for the camera even with the keypad
  setting on. That setting is for the editor's tools.
- **The signal keys** go on something other than Tab.
- **The shortcut override** (asked, not decided): the widget decides on
  both backends. On QRhi, Qt sends the event to the view's `QWindow`, so
  it must reach the widget. Either that window forwards it, one line
  beside the keys it forwards (chosen), or the widget filters that
  window's events, the surface giving its window.
- **1, 2 and 3** (an idea, not decided): the camera has the consist as
  its object, and the consist decides what to show.
  - The consist already decides where the camera sits
    (`getCameraPosition`, `getCameraMount`, `cameraVehicle`, its 3D cab).
  - The widget's `rideTrain` adds the vehicle and the eye for the cab,
    ahead and behind, from the simulation's frame (whether the train runs
    reversed). A consist knows nothing of the simulation, so the
    controller would give it that.
  - Followed AI trains use 1, 2 and 3 too. So such a consist would take
    those keys only, the editor keeping the rest. That needs a taker that
    takes some keys, beside one that takes them all.
  - Not in this task: 1, 2 and 3 stay the widget's, before the taker.

## Steps 1 to 4 done (2026-10-30)

**Step 1, the takeover:**
- `KeyTaker` (`src/tsre/KeyTaker.h`): press, release, let go. The design's
  `takes()` is gone: answer 4 made the shortcuts the view's rule.
- `GameObj::keyTaker()` gives NULL. `Consist::keys` is set by the
  controller.
- The view (`RouteEditorGLWidget`):
  - `keysTaker()` is checked each frame and at each key. A taker gone lets
    go, and the camera does too. A new one gets the window's activation and
    the view's focus.
  - The press path while taken: the UI router, `` ` ``, Ctrl+Shift+F10,
    then `followViewKey` (1, 2, 3, now a function shared with the editor's
    path), the taker, the camera.
  - `modifierKey` keeps Ctrl, Shift and Alt for the mouse tools on both
    paths.
  - `event()`: `ShortcutOverride` is accepted for all but F1 to F35 (Alt+F4
    included in that). Tab and Backtab go to `keyPressEvent`, not to Qt's
    focus chain.
  - `RhiRenderSurface`'s window forwards `ShortcutOverride` and returns
    whether the widget accepted it.
- `CameraFree::arrowsMove()`: the keypad setting, or riding an object
  with a taker.

**Step 2, physical keys** (`src/tsre/KeyPosition.h/.cpp`, beside
`GameObj`; no new folder in CMake):
- Keys that mean the same on every layout are read from Qt's key: Esc,
  Backspace, Tab, Enter, Space, Pause, the F keys and the grey keys.
- The rest come from the native code on the main block:
  - xcb and Wayland: the keycode − 8;
  - Windows: the scan code.
- Else, and in tests, they come from Qt's key at a US keyboard's place.
  Shifted characters sit at their key's place.
- Grey keys get their codes from Qt's key, so Linux and Windows need no
  extended-key tables.
- Windows is not checked here; no Windows build machine.

**Step 3, the train's keys:**
- `Sim::DrivingKeys` (`src/tsre/sim/DrivingKeys.h/.cpp`) holds the table
  above, 52 bindings. The `SimulationController` owns it.
  - Its sink sends to `command()`. Pause pauses or plays the activity.
  - `placeTrains` and the driven change give it to the driven train's
    consist.
  - `leave()` and stopping let go first, while the train is still driven.
- The scripts:
  - `util.switched` handles "toggle" for the pantographs, master key,
    breaker authorisation, emergency, the diesel's engine and helpers
    (the last asked, running at the start), and steam's injectors,
    cylinder cocks, blowdown and manual firing. These now return the state
    chosen, so the other units get "up" or "down", not "toggle".
  - `directionIncrease`/`Decrease`: a step at the press, the units given
    `direction`. On a steam loco they alias the reverser's held commands.
  - `levers.start`: Open Rails' jump to the next notch at the start of a
    hold, with its `SmoothMin` rule going down.
- **Found on the way: held levers never reached the other units.** A
  held command moved only the cab's lever.
  - Each script's `toUnits` now sends a lever's value along the line each
    step it moves, and the start's notch too.
  - A handler may give the name the units get (`return value, name`).
  - Levers set from the line make no sound: the cab's lever sounds once.
  - The panel's +/− clicks went through already.

**Step 4, the panel:**
- **Keys** beside **All values** lists the keys for the loco's parts:
  - every loco: throttle to pause;
  - pantographs, breaker, relay, engine and helpers, steam heat, when the
    loco publishes them;
  - steam's firing rows on `cutoff`.
- `train.keys` maps each Open Rails name to its key.
- 14 new texts in English and Polish.
- The panel's emergency button sends "toggle".

**Checked:**
- **Suite `keys` (14 checks):**
  - the camera's arrows: 30 m in ten frames riding a taker, 0 riding one
    without, 30 m with the keypad setting;
  - the places from Qt's keys and from xcb, Wayland and Windows codes;
  - names both ways;
  - each binding on a key of its own;
  - a held key, its auto-repeats and release;
  - Shift let go before R;
  - O's three commands;
  - letting go;
  - unbound keys left to the camera;
  - Pause;
  - a press without its release.
- **Suite `driving` (67, two new), electric.lua with a second electric loco
  behind it, by keys:**
  - W gives direction 1 on both;
  - a tap of D gives the throttle's first notch, 0.125 on both;
  - D held a second gives 0.875 on both, 9.3 m/s 3 s later;
  - P lowers pantograph 1 on both.
- **diesel.lua:** Shift+Y stops the running engine, again starts it.
- **steam.lua:** W held a second moves the cut-off from 0 to 0.5; D held
  half a second opens the regulator to 0.25.
- **Suite `rmlui` (40, one new):** the Keys list shows an electric's and a
  steam loco's rows.
- **Unchanged:** steam 11, heating 10, script 32, simulation-core 89,
  sound 95, vehicle-file 40, cab3d 28, units 18, ctest 11/11.

## Step 5 (end to end) done (2026-10-30)

**The run:**
- The editor under Xvfb with openbox, from a scratch copy of the
  worktree's profile, the HUD on.
- EUROPE1's Local Service; in the Timetable window the player's train (the
  Scotsman) chosen and Drive pressed.
- Keys held by `xdotool keydown`/`keyup` across frames.
- Both backends: OpenGL, and QRhi (Vulkan on llvmpipe).

**What happened:**
- **Focus:** after Drive the editor's main window was the active one, not
  the Timetable window. The view had the keys with no click.
- **Driving by keys alone:**
  - `;` held 8 s: the train brake to Release, the brake cylinder at 0 psi.
  - W held 2 s: the reverser.
  - D held: the regulator to 0.88 and 15,466 lbf.
  - 7 mph after 15 s, 18 mph later.
  - On QRhi: regulator 0.51, 2 mph.
  - X's auto-repeats (a held key repeats as press and release pairs)
    neither stopped nor stepped a held lever.
- **The Keys list** in the panel showed a steam loco's rows.
- **The editor's keys and shortcuts while driving:**
  - `/` reached the view (QRhi through the window's forwarding) and did not
    tick Settings > Stick Camera To Terrain.
  - E, Ctrl+Z, Esc and Tab reached the view too.
  - The editor stayed open, the view kept the focus, and the next keys
    still drove.
- **The camera:** the Down arrow moved it back off the cab window while
  the train ran.
- **Map mode:** `` ` `` gave the map. There S moved the map, not the train
  (the direction stayed F, the regulator unchanged). `` ` `` back: keys
  drove again.
- **After Leave:** `/` reached the view untaken and ticked the setting
  again, so the editor's shortcuts are back.

**Found:** after a menu is opened and closed with Esc, Qt's menu bar keeps
the keyboard until a second Esc or a click in the view. A key pressed
then is the menu bar's, and a shortcut like `/` fires.
- The editor does this everywhere, not only while driving, so it is left
  as it is.
- Later, the view could take the focus back when a menu closes while a
  train is driven.

## Design change: the camera flies while a mouse button is held (2026-10-30)

The user (2026-10-30), after step 5: the arrows are not comfortable for
moving the camera, and W and S, comfortable keys, are wasted on the
reverser.

**What clashes while the camera rides the driven train** (the train gets
each key first):

| Key | The camera (`CameraFree`) | The train (Open Rails) | Goes to |
|---|---|---|---|
| W / S | forward / back | reverser | the train |
| A / D | left / right | throttle | the train |
| Space (held with W/S) | up / down | horn | the train: the camera cannot move up or down while driving |
| `.` | lock its height | dynamic brake + | the train |
| Shift / Ctrl | fast / slow | modifiers | both, no clash |
| a mouse button held | turns | - | the camera |

- Open Rails has more of its later keys on the left: Q doors, E gear, Z
  alerter, X sander, R/F firing rate and fire door, C cylinder cocks.
- On a steam loco W and S are the cut-off, worked the whole run.
- Open Rails and MaSzyna's driving both leave the letters to the train
  and move the camera with the arrows (Open Rails: Alt+arrows turn,
  PgUp/PgDn zoom; MaSzyna: PgUp/PgDn up and down, its power controller on
  the keypad's + and -, the reverser D/R, the horns A/S). MaSzyna's
  scenery editor uses W, A, S and D.

**Considered:** fly while a mouse button is held; the letters by view (the
cab's to the train, the outside views' to the camera); TSRE's own layout
(the camera keeps W, A, S, D and Space, the levers on the right hand); a
toggle key; Alt as a modifier. **The user's choice: the first.** TSRE's
own layout may come back as a second preset in the key bindings task.

**The design:**
- While a mouse button turning the camera is held (`CameraFree`'s left or
  right button; the right always, the left with no tool active), W, A, S,
  D and Space go to the camera before the train. Otherwise they are the
  train's, as now. As in Unity's and Unreal's editors.
- A key keeps its owner until released: a key pressed before the button
  stays the train's, one pressed while flying stays the camera's after the
  button is let go. The view remembers which places it gave the camera.
- Space with W or S moves the camera up and down while flying; Space alone
  is still the horn.
- Shift and Ctrl stay the camera's speed while flying. `.` stays the
  dynamic brake's.
- The arrows stay the camera's while driving, for the keyboard alone and
  touchpads.
- A mouse button's release reaches the view that got its press (Qt's
  grab), so flying ends with it; the flown keys are let go with the
  camera's other keys (focus lost, the UI taking the keys, the taker
  changing).

**Open: Shift as a second way to fly** (the user's question, 2026-10-30).
- Neither TSRE's table nor Open Rails' defaults use Shift on W/S/A/D for
  the throttle or the reverser.
- Open Rails' cruise control does: Shift+W/S its mode, Shift+A/D its set
  speed, Ctrl+Shift+W/S/A/D its mode and maximum acceleration,
  Shift+Alt+A the set speed to zero. TSRE has no cruise control yet; those
  keys would move when it comes.
- Shift is the camera's fast key (`core.camera.speed.maximum`, 40 against
  3 standard). Flying with Shift would need another fast key, or a fly
  speed of its own.
- Shift+Space is free; Shift+Q and Shift+E are Open Rails' doors right and
  gear down.

**The user's answers (2026-10-30):**
- Only the mouse button first; Shift not now.
- "Drive should always set editing tool to none": a train driven turns
  the editor's tool off, so the left button turns and flies the camera
  too and clicks no tool.

## The design change done (2026-10-30)

**The code:**
- `Camera::takesKey(event)`, none by default. `CameraFree` takes W, A, S,
  D and Space pressed while its left or right button is held (not with
  Alt), and keeps each in `flown` until its release, auto-repeats
  included. `releaseKeys` empties `flown`.
- `RouteEditorGLWidget`, while a ridden object takes the keys:
  - a press: the camera's `takesKey` first, then the follow views, the
    train, the camera;
  - a release: the train's unless the camera flew on it; the camera gets
    every release as before;
  - a key the camera flies on cancels the click (`mouseClick`), as moving
    the mouse does, so letting go of the right button opens no context
    menu. Found on the first run: the menu opened and took the next keys.
- `SimulationController::drivenChanged` with a train: `enableTool("")`,
  the editor's no tool; the tool panels' buttons uncheck by the
  `toolEnabled` message.

**Suite `keys` (15, one new):** the routing: W pressed before the button
stays the train's (its repeat and release too); D and Space pressed with
the button held are the camera's; Alt+A and the Up arrow are not; D stays
the camera's after the button is let go, until released; then D is the
train's again; `releaseKeys` lets go of Space. Unchanged: driving 67,
rmlui 40, ctest 11/11.

**End to end** (EUROPE1, Local Service, the Scotsman under Xvfb, both
backends):
- E chose the select tool before Drive; after Drive the log shows `""`
  and the context menu "No Tool".
- View 2. The right button held and D for 1.5 s: the camera moved from
  (-694, -214) to (-594, -128) (QRhi: -596, -130); the throttle stayed 0;
  no context menu.
- D for 0.5 s without a button: the throttle to 0.061 (QRhi 0.054), the
  camera still.
- The left button held and A for 1.5 s: the camera back to (-694, -213);
  the throttle unchanged.

**Left:**
- A right click without moving still opens the editor's context menu while
  driving (Undo, Copy, Paste, Select Tool). A tool chosen there or in a
  panel takes the left button again.
- Shift as a second way to fly, when the user asks.

## Drive follows the train (2026-10-30)

The user found: the activity panel's Drive did not put the camera in the
train; Follow had to be pressed too. "Drive click should also make you
follow."
- **Reproduced** under Xvfb: the panel's Drive (Simulation, beside Play)
  showed the driving panel and Leave, the camera stayed where it was, and
  so the editor kept its keys. The Timetable window's Drive followed
  already (`followFromStart`); the panel's (`ActivityTools::
  simDriveEnabled`) only drove.
- **The fix, in one place:** `SimulationController` follows the train
  driven when the host reports it (the poll, beside `giveKeys`), so every
  Drive does: the panel's, the Timetable window's, a run started driving.
  Given back, the train stays followed.
- **Suite `simulation-ui`:** its Drive check asks for the train followed
  too: failing without the fix, passing with it.
- **Found on the way:** four of that suite's clock checks had failed since
  task 06 step 1 (`7d943103`): an MSTS activity's season moves the run's
  first day into it, and the suite's activity is summer, so its run starts
  2026-07-15, not the editor's 2026-10-15. The behaviour is task 06's; the
  checks now expect July. The suite was not among those run in task 06.
