# Explore task 01 - The window and the main menu

Status 2026-11-20: design, no go. Part of the
[explore set](00-task-roadmap.md); its questions and the user's answers
are there.

## Goal
- The Load window: **Explore** (blue) left of **Edit** (green, was Load).
- **`--explore`** on the command line.
- **The explore mode window:** the view alone, every part of its interface in
  RmlUi, over black.
- **The main menu** on the left: Explore, Multiplayer, Options, Exit; by
  mouse or arrow keys.
- **Settings:** a new Explore mode section in the System tab: Resolution,
  Fill screen.

Not in this task: what Explore opens (task 02), the intro route (task
03), Options and Multiplayer (later).

## Design

### 1. The Load window
- **Buttons, left to right:** Explore, Edit, New, Exit.
- **Explore is blue:** a new `Game::StyleBlueButton`, set for each theme in
  `main.cpp` beside the green, yellow and red ones.
- **When it shows:** with Edit, when the root is valid. It needs no route
  chosen. A route chosen in the list goes on to the Explore window and is
  chosen there first (question 4: yes).
- **Edit is Load renamed:** new translation ids for both of its texts
  (`setLoadRoute` sets the text again on every click in the list), both
  `.ts` files.
- **Explore hides the Load window and opens the explore mode window.**
- **The editor's window is made only for Edit.** Today `LoadRouteEditor`
  makes `RouteEditorWindow`, and so its view, before the Load window
  shows. Explore needs neither.

### 2. The command line
`--explore` ("Start in explore mode", `consoleArgs["EXPLORE"]`). The game
root, the first that is valid:
1. `--game-root`;
2. `core.paths.gameRoot`;
3. the most recent of the Load window's roots, the last line of
   `cerecent.txt`;
4. none: the Load window opens; its Explore goes on as above.

For point 3, the Load window moves a root chosen again to the end of its
list. Today only a new root goes there.

`--route` with `--explore` chooses that route first, as the Load window's
choice does. `--set core.explore.fillScreen=true` works for one launch as
any setting. `--fullscreen` stays the editor's.

### 3. The window
`ExploreWindow`, in a new `src/explore` folder beside `routeEditor`,
`conEditor` and `shapeViewer`:
- A top-level `QWidget`. A layout without margins holds one
  `RouteEditorGLWidget` in the Explore role. No `QMainWindow`: no menu bar,
  docks or status bar.
- Title: "TSRE5" (and the route's name in a run).
- **Size:** `core.explore.resolution`, in physical pixels, as games count
  them. The window's size is that over the screen's device pixel ratio:
  1920 x 1080 on a 4K screen at 250 % is 768 x 432 logical. It is kept
  within the screen's free area. The window can be resized; a size dragged
  is not saved.
- **Fill screen** (`core.explore.fillScreen`): no frame, Qt's full screen
  state on the screen the window is on, the taskbar covered. The
  resolution is not used; the screen's own pixels are drawn.
  - **On Windows the window turns off the 1-pixel border** that `main.cpp`
    gives every window in full screen (per window:
    `QNativeInterface::Private::QWindowsWindow::setHasBorderInFullScreen(false)`).
    The border is there for the editor's Qt menus and tool windows over
    its view. Nothing Qt draws goes over this window, so it can be truly
    borderless.
  - F11 and Alt+Enter switch it for the session, as in the editor.
- **Both settings apply while it runs:** the window takes
  `runtimeSettingsChanged`; nothing reads them per frame.
- **Closing the window** (Alt+F4, the title bar) is Exit.

### 4. The view's Explore role
`RouteEditorGLWidget(QWidget *parent, Role role = Role::Editor)`. In
`Role::Explore`:
- **No editing:** no tool (`enableTool` refuses all but none), no pointer,
  no selection on a click, no right-click menu (its three `QMenu` sites),
  no Qt message box or dialog (their few sites; `playInit`'s dialog goes
  with `--play`).
- **The cab keeps its clicks:** `CabPicker`'s own pick, not the selection
  pass.
- **Map mode works** (the user, 2026-11-20), the same key switching. In
  the map too: no tools, no selection, no right-click menu.
- **The map's layers:** the role starts with its own defaults, fewer than
  the editor's (`MapLayers`). Proposed:
  - on: track, roads, terrain, the scale bar; track objects, of them
    signals, platforms, sidings and level crossings; the activity, of it
    the player's path, the trains, their names, and the simulation's
    track use and signal aspects;
  - off: junctions, track ends, the editor's chosen paths, the pointer,
    the faded overlay, OSM data, imagery, weather, markers; speed posts,
    car spawners, hazards, pickups, sound regions; AI paths, loose
    consists, speed zones, failed signals, events.
  - No toggles in this task: the editor's are in its Qt Map menu. RmlUi
    toggles come later (roadmap).
- **Input** reaches `Ui::Screen` first, as now. While the main menu shows,
  the menu has the keys (it is modal). Esc in a run: the game menu (task
  02).
- **Without a route the view draws a frame:** cleared, the UI over it.
  Today `canRenderFrame` needs a route and the UI is drawn inside
  `paintScene`. The surface starts when the window first shows, before
  any route. The editor's window shows only after its route has loaded.
- **`Game::playerMode`** becomes "the explore mode window runs", set by
  it, for its two places outside the view (`Route.cpp:555`,
  `Activity.cpp:1198`). Its setting goes (question 2: yes). The pointer
  check in the view becomes the
  role's.

### 5. The main menu
- **The document:** `appdata/0.7/ui/explore/main.rml`, with
  `explore.rcss` shared by the explore documents.
- **Layout:** the items in large text in the left third, centred
  vertically, as The Witcher 3 does. TSRE's version in small text at the
  bottom.
- **Input:** shown with `Screen::showMenu` (modal, first item focused).
  Hover lights an item, Up and Down move, Enter or a click opens it.
- **Items:**
  - **Explore:** task 02's window. In this task, an empty frame with Back
    (Esc).
  - **Multiplayer, Options:** greyed, "Later" (question 7: greyed).
  - **Exit:** quits TSRE.
- **Texts:** translation ids in `UiTexts.cpp`, both `.ts` files.
- **Scale:** a virtual pixel is the view's height over 1080. At 640 x 480
  and 1024 x 768 the room is 1440 x 1080, so the documents are laid out to
  fit 4:3. A wider window shows more around them.
- **Background:** black (task 03 puts the intro route behind it).

### 6. The settings
- **A new subgroup** `explore` in the `system` group, the System tab:
  "Explore mode", "The game window: its size and full screen". Placed after
  Audio.
- **`core.explore.resolution`,** a choice:
  - 640 x 480 and 1024 x 768 (4:3);
  - 1280 x 720, 1600 x 900, 1920 x 1080, 2560 x 1440 and 3840 x 2160
    (16:9);
  - default 1280 x 720 (question 3: yes).
- **`core.explore.fillScreen`,** on or off, default off (question 3: yes).
  "Fill screen: a window without borders over the whole screen, the taskbar
  too. The screen's own resolution is used."
- **Each needs** a claim in `Game.cpp`, the settings suite's count (two
  more), both `.ts` files. Run the `settings` suite.

## Steps
1. The settings; the Load window's buttons; `--explore` and the root
   order.
2. `ExploreWindow` and the view's Explore role, the frame without a route;
   map mode in the role, with its layers.
3. The main menu; Exit; Explore opens an empty window with Back.
4. `--play` (with `playInit` and `PlayActivitySelectWindow`) and the
   `playerMode` setting removed (question 2: yes).
5. Checks, documents.

## Checks
- **The `settings` suite:** the count, the claims, the subgroup.
- **A new suite, `explore-window`** (no content; offscreen or Xvfb, as
  `route-switch`):
  - the window made with no route: a frame is drawn, the menu in it;
  - Down and Enter reach each item; Exit quits;
  - 1024 x 768 gives that size over the device pixel ratio; Fill screen
    gives the full screen state;
  - the role refuses a tool and draws no pointer;
  - the map key switches to the map with the role's layers, and a click
    there selects nothing.
- **Live, under Xvfb with openbox:** the menu at 640 x 480 and 1920 x 1080
  on both renderers; Fill screen; the keys alone.
- **Windows, the user's check:** Fill screen covers the taskbar, and
  Alt+Tab away and back works.
- Before a merge: `all` built, ctest, the app suites.
