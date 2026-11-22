# Task 09 - Route Editor in full screen

Status: done (2026-11-17). Built on `feature/editor-fullscreen`; the settings
suite and ctest pass; tested live by the user ("seems to work") and merged
into main.

## The request (a user, 2026-11-17)

Full screen for the Route Editor, from the command line and a setting: the
editor starts as a borderless window over the whole screen, the taskbar
covered, no title bar, the main menu bar hidden by default and sliding in when
the mouse reaches the top edge.

## The user's decisions (2026-11-17)

- Escape still closes the editor (as before, with the unsaved-changes prompt);
  F11 leaves full screen.
- Only the Route Editor. It is a stop gap until a proper gaming mode.
- One choice, **Route Editor window** (Normal, Maximized, Full screen),
  replacing "Start Route Editor maximized"; old profiles are migrated.

## What was there

- `core.interface.routeEditor.startMaximized` (legacy `settings.txt` key
  `fullscreen`): maximized with `Qt::CustomizeWindowHint`, which drops the
  title bar; the taskbar stayed. Its description said it was not full screen.
- `--set key=value` overrides any setting for one launch.
- The editor's menu bar is QMainWindow's own; "Hide Route Editor tools" and
  player mode hide it.

## What constrained the design

1. **Under QRhi the 3D view is a native child window**
   (`createWindowContainer` in `RhiRenderSurface`). No child widget can lie
   over it; a top-level window can, as the navigation and error windows do.
2. **Showing the editor's own menu bar resizes the view**: its swapchain and
   every render target made again, the picture moved down by the bar's
   height, on every reveal.
3. **A hidden QMenuBar's actions answer no shortcuts**: Qt fires an action's
   shortcut only when a visible widget holds it. The menus carry Ctrl+Z, C, V,
   Shift+Ctrl+S, E, F1 to F5, /, F12 and Alt+F4.
4. **A GPU window exactly as large as the screen** can be taken by the driver
   for the screen alone, so menus, tool windows and dialogs flicker or stay
   behind it (Qt's documented problem with OpenGL). Qt's fix on Windows is
   a 1-pixel border in full screen (`setHasBorderInFullScreen`, private
   API). Used until 2026-11-21, then dropped: see "The border, dropped".

## Built

**The setting** `core.interface.routeEditor.windowMode` (Interface tab, Route
Editor section), an enum: 0 Normal (1280 x 800, the default), 1 Maximized (as
"Start maximized" was: no title bar), 2 Full screen. It keeps the legacy file
key `fullscreen`. Read once, before the window first shows
(`RouteEditorWindow::applyStartWindowMode`); applies at the next start.

**The migration** (`SettingsManager::loadFile`): the old key is in
`RetiredKeys`. Before it is dropped, its value is read; the window mode is
then seeded with its default, which counts as a catalogue difference (the
Settings window's banner offers it), and set to Maximized when the old switch
was on and the profile had no window mode. Settings count unchanged (145).

**The command line**: `--fullscreen` (or `-fullscreen`) is
`--set core.interface.routeEditor.windowMode=2` for that launch
(`collectEarlySettingsArguments`); it is not saved in the profile.

**Switching while the editor runs**: **Window > Full Screen**, F11 or
Alt+Enter (Open Rails' key), a checked item. Qt's window state keeps what lies
under full screen: leaving returns to normal or maximized. Started in full
screen, the state under it is maximized (with the title bar).

**In full screen** (`RouteEditorWindow::fullScreenChanged`, on every window
state change): the editor's menu bar is hidden; its actions with shortcuts are
added to the window and removed on leaving; a `MenuRevealBar` is made, and
deleted later on leaving (leaving may come from its own Window menu). With the
tools hidden or in player mode the menu bar stays hidden and nothing slides
in, as before.

**`MenuRevealBar`** (`src/routeEditor/MenuRevealBar.h`): a frameless tool
window owned by the editor (`Qt::Tool`, `WindowDoesNotAcceptFocus`, shown
without activating, so the editor stays the active window and keeps its
shortcuts) holding a second QMenuBar with the editor's own menus.
- Polls `QCursor::pos()` every 50 ms: the mouse's events go to the window
  under it, the 3D view's mostly. Nothing while the editor is hidden or
  another application is in front.
- Slides in when the mouse is within 2 px of the editor's top edge and no
  button is held (a drag in the view reaching the edge does not reveal it).
- Slides out 400 ms after the mouse has left it (8 px under it still count),
  never while a menu is open.
- The slide (120 ms) grows the window's height with the bar at its bottom,
  so it never leaves the screen.

**Windows**: no border in full screen (see below).

## The border, dropped (2026-11-21)

`main.cpp` gave every window Qt's 1-pixel border in full screen (point 4).
The user saw a white line around Explore's full screen, which turned the
border off after showing. Measured on the Deck's 4K screen (a DPI-aware
probe of the windows and the screen's pixels):

- Explore: the window 3840 x 2160 without the border, its Vulkan view
  3838 x 2158: Qt kept the client size it had with the border, and the
  2 pixels right and bottom were never drawn (white).
- The editor with the border set before full screen: the window at -1,-1,
  its client 3840 x 2160, but the same 2 white pixels right and bottom
  (Qt laid it out at 3838 x 2158), and the menu bar shown again a few
  seconds after the start (a window state change without full screen
  reached `fullScreenChanged`).
- The editor without the border, QRhi Vulkan and the OpenGL renderer: the
  view the whole height, no white pixel at the edges, the menu bar hidden;
  the Navi tool window, the menu bar sliding in, the Window and Settings
  menus and the Settings Editor dialog all drawn over the view.

So no window gets the border: `main.cpp`'s default and Explore's
workaround are gone. Should a driver hide menus or dialogs behind the
full-screen view, the border comes back for the editor, set before it
enters full screen, with its 2-pixel strip to be solved.

## Checks

- Settings suite 303 passed (offscreen), with three new cases: an old profile
  with "Start maximized" on opens maximized, one with it off normal, and a
  profile holding both keeps its window mode.
- All targets build; ctest 11 passed.
- Translations: lupdate, the English sync, Polish for the six new strings;
  `validate_translations.py` passes, no unfinished Polish.

## The live test (the user, 2026-11-17: it works)

What it covered, for later checks. Both renderers (QRhi Vulkan, OpenGL), on the Deck's panel and the 4K screen:
- `--fullscreen` and the setting: no title bar, the taskbar covered;
- menus, the navigation, error and properties tool windows, the Settings
  window (F12) and file dialogs over the editor, without flicker;
- F11 and Alt+Enter both ways with a route loaded (the view resizes);
- the menu bar sliding in and out, its menus working, Window > Full Screen
  from it;
- shortcuts (Ctrl+Z, F1 to F5, E) with the bar hidden;
- Alt+Tab away and back.

Full screen on the 4K screen at 250 % draws 3840 x 2160, about twice the
pixels of the usual maximized editor view (2160 x 1948).

## Left as it is

- Alt+letter keys do not open the hidden bar's menus; the mouse and F11 do.
- Under Wayland a program cannot place its own windows: the bar would need
  another way there (not tried; TSRE runs on Windows).
- "Hide Route Editor tools" and player mode hide the menu bar the same way,
  so by Qt's rule (point 3) their menu shortcuts probably do not work today.
  Not checked, not changed.
- `Game::fullscreen` is no longer set (its declaration stays in `Game.h` until
  a full rebuild is due); `Game::loadLegacySettings`, which read
  `fullscreen` from `settings.txt`, is never called.
