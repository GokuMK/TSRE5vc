# Task 07 - Keyboard

Status: designed 2026-10-30, questions at the end; not built.

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
with it first. Open Rails' signal keys are on Tab (later).

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
    // Whether it takes this key (a window's shortcut gives way to it).
    virtual bool takes(const QKeyEvent &event) const = 0;
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
taker, the arrows move it as W, A, S and D otherwise do. This is the
keypad setting's behaviour, on for that time. The taker uses W, A, S, D,
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
- the switches G / Shift+G, signals Tab (taken in the widget's `event()`
  before Qt's focus change), change cab Ctrl+E, uncouple U, autopilot
  Alt+A.

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
