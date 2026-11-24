# Explore Mode Task Roadmap

TSRE's game mode: a window with the 3D view alone, all of its interface
in RmlUi, a main menu over an intro route, and runs started from it.

Status 2026-11-20: tasks 01 and 02 built on `feature/explore` (worktree
`../TSRE5vc-explore`), the user's go for each; merged into main the same
day. Task 03 in design; task 04 (walking runs, the characters in explore
mode) built 2026-11-21 on `feature/explore`, merged into main 2026-11-22
with main brought in first. Task 05 (the Explore window in pages) built
2026-11-24 on `feature/explore`, not merged. The questions at the end
answered the same day; two left for later (6, pausing; 10, how the intro
ships).

Names used here: **the explore mode window** is the Qt window holding the
view; **the Explore window** is the RmlUi window that the main menu's
Explore opens.

## The user's request (2026-11-20)
- **Explore** in the Load window: a blue button left of the green one,
  which now says **Edit** (was Load). Or `--explore` on the command line.
- It opens a new window like the editor's, but with the 3D view alone: no
  panels, no menus. No Qt widgets for the interface: **RmlUi only**.
- **First decision:** keep `RouteEditorGLWidget` or make a new view for
  the game mode? Map mode asked the same, and kept one.
- **Settings:** a new section in the System tab, **Explore mode**:
  - **Resolution:** 640 x 480 and 1024 x 768 (4:3), the rest 16:9. Not
    that important, the window can be resized.
  - **Fill screen:** a true borderless window over the whole screen, the
    taskbar too.
- **`--explore`** uses the game root of the settings; if none, the recent
  one; if none, the Load window opens.
- **Later:** `--explore` with an activity or timetable file.
- **The menu's background is an intro route** (as Kingdom Come does it),
  loaded with the route switch. It lives in `assets/root/ROUTES/intro1`
  once unpacked. The first steps may show black.
- **The menu, on the left** (as The Witcher 3): Explore, Multiplayer,
  Options, Exit. Clicked, or chosen with the arrow keys, an item opens a
  large RmlUi window. **The Explore window comes first:**
  - it lists the routes; a route chosen lists its activities and
    timetables;
  - a timetable gives a list of its trains, sortable and filterable, to
    choose one;
  - "explore" also makes an MSTS-like activity: a service from a chosen
    path and consist, a start time, and so on. Open Rails' Menu.exe is
    worth a look;
  - Explore loads the route and the run, and the game begins.
- Design phase, no code changes; a new task folder for the documents.

## What TSRE has already (checked 2026-11-20)
- **The view**, `RouteEditorGLWidget` (`src/routeEditor`, 7,400 lines): a
  `QWidget` holding a render surface (OpenGL, or QRhi in a native child
  window). Inside it: the camera, riding a train, 3D cabs and their
  clicks (`CabPicker`, a ray pick of its own), the driving keys, the
  simulation's trains placed from frames, sound, route displays, the HUD
  and `Ui::Screen`, leaving and entering a route (editor task 10). It is
  also the editor's `ToolContext`: tools, selection, live edits.
- **It draws nothing without a loaded route** (`canRenderFrame`); the UI is
  drawn inside `paintScene`. The editor's window is shown only once the
  route has loaded (`showWindow` from `initRoute2`).
- **Map mode** is a second mode of the same view (editor task 04):
  `ViewMode::Map`, `paintMap` beside `paintScene`.
- **`Ui::Screen`** (scripting task 01): one RmlUi context per view; a
  document shown as a menu is modal (the keys are its own, its first
  control focused); `Screen::Action` (Up, Down, Left, Right, Accept, Back,
  Next, Previous, Menu) carries the keys now and a gamepad later;
  `setMenuHandler` takes Esc, written for "the game menu, later". Documents
  in `appdata/0.7/ui`, texts by translation id (`UiTexts.cpp`). A virtual
  pixel is the view's height over 1080.
- **The Load window** (`src/routeEditor/LoadWindow.cpp`): Load (green), New
  (yellow), Exit (red). Recent roots in `cerecent.txt` in the working
  folder; a root browsed for the first time is added at the end, one
  chosen again stays where it was.
- **The game root at start:** `--game-root`, else `core.paths.gameRoot`. The
  Load window opens unless both root and route are given
  (`prepareConfiguredRoute`).
- **Two player-facing remnants, both inside the editor's window:**
  - `--play [--file act]`: the play path from before the simulation
    (`Activity::initToPlay`, the `PlayActivitySelectWindow` dialog, the
    camera in the cab of the activity's first consist);
  - `core.network.playerMode` (Advanced, experimental): hides the
    editor's panels and menu bar and the pointer, skips a repair prompt
    (`Route.cpp:555`), changes which AI trains are drawn
    (`Activity.cpp:1198`). A global flag.
- **Runs start from an activity:** `SimulationController::playDriving` runs
  an MSTS activity with its player's train, or an Open Rails timetable set
  (`Activity::Kind::OrTimetables`) with the train chosen
  (`Activity::playerTrain`), driven from its start. An activity can live
  in memory only: the editor's New activity does it
  (`ActLib::AddAct(..., true)`). `Activity(path, name)` reads one without
  `ActLib`.
- **Route switch** (editor task 10): `switchRoute` = the view's
  `leaveRoute` + `RouteSwitch::leave`, then `enterRoute`. Another game root
  was left out of it.
- **Full screen in the editor** (editor task 09): Qt's full screen state,
  without a border. Until 2026-11-21 `main.cpp` gave every window a
  1-pixel border in full screen, which left a 2-pixel white strip
  (editor task 09, "The border, dropped").
- **Downloads:** `Game::InitAssets` fetches missing resource folders into
  `./assets` with `downloadResourceDirectory` from
  `koniec.org/tsre5/data/appdata/<name>.tar` (`procedural_examples` now).
  There is no intro route there yet.
- **Open Rails' Menu** (`Source/Menu/MainForm.cs`,
  `Source/Orts.Menu/Activities.cs` in `../openrails`):
  - Activity mode: the route list, then the activity list, which starts
    with "- Explore Route -" and "+ Explore in Activity Mode +". Exploring:
    Locomotive, then Consist; Starting at, then Heading to (player paths
    grouped by their start); Time (whole hours offered, typing allowed);
    Season; Weather. An activity shows its own values, unchangeable.
  - Timetable mode: timetable set, timetable, train, day, season,
    weather, weather file.
  - Multiplayer: host and port, user name.
  - The last choices are remembered (`Menu_Selection`).
  - "Explore in Activity Mode" makes an empty activity in memory
    (`Simulator.SetExploreThroughActivity`); nothing is written.

## The first decision: one view (the user, 2026-11-20)
**One view, as map mode.** The explore mode window holds the same
`RouteEditorGLWidget`, made in an **Explore role**. The user: map mode must
work in explore mode too, probably with fewer layers on by default.

Why:
- **A run needs what is inside the view:** both renderers and their
  surfaces, the camera riding a train, cabs and their clicks, the driving
  keys, trains placed from the simulation's frames, sound, displays, the
  HUD and the RmlUi screen, leaving and entering routes, the capture
  harness. A new view would either copy all that, and the two would drift
  apart, or need the view split first.
- **The split is possible later**, along `ToolContext` (the editing side,
  the view's private base). It is a refactor of the editor that gives a
  player nothing. Do it when the role's exceptions grow, not before.
- **Things come for free:** a map in a run (map mode), and later perhaps
  running the editor's route as a game ("play in editor").

The Explore role, set once when the view is made:
- no editing tool (none chosen, others refused), no pointer, no selection
  by click, no right-click menu, no Qt dialogs or message boxes (messages
  go to the UI);
- the cab's controls keep their clicks;
- Esc opens the game menu (`Screen::setMenuHandler`);
- **map mode works,** with the role's own default layers, fewer than the
  editor's (task 01, section 4), and no tools or selection there either;
- **a frame without a route:** cleared, the UI drawn over it;
- the editor windows' signals: the explore mode window connects only what
  it needs.

`Game::playerMode` becomes "the explore mode window runs" for the two
places outside the view; its setting goes (question 2: yes).

## The flow
1. **Start:** Explore in the Load window, or `--explore`.
2. **The explore mode window:** the main menu over black (task 03: over the
   intro route).
3. **Explore:** the Explore window, where a route and a run are chosen: an
   activity, a timetable's train, or exploring the route.
4. **Explore (its button):** the route's loading screen, the route, the
   run, the player's train driven from its start, the camera in its cab.
5. **Esc in the run:** the game menu. Main menu goes back to step 2; Exit
   quits.

## Tasks
- [x] 01 [The window and the main menu](01-window-and-menu.md) (built
  2026-11-20, merged into main): the Load window's buttons, `--explore`, the
  window with the view alone, the settings, the main menu over black.
- [x] 02 [The Explore window](02-explore-window.md) (built 2026-11-20,
  merged into main): routes, activities, timetables and their trains, exploring
  the route; starting the run; the game menu.
- [ ] 03 [The intro route](03-intro-route.md) (brief): its content, the
  switch to another game root, the scene behind the menu.
- [x] 04 [Walking runs](04-walking-runs.md) (built 2026-11-21,
  merged into main 2026-11-22): Drive or Walk in the Explore window; on foot at a station of
  the chosen train's timetable, at any station and time without a train,
  in activities and exploring (its train stopping at its path's
  platforms); the character placed on the platform.
- [x] 05 [The Explore window in pages](05-explore-pages.md) (built
  2026-11-24, not merged): a route page (picture, facts,
  description, Activities / Timetables / Explore the route), then each
  of those a page of the window's whole width: the timetable's table
  full width with a train's calls, an activity's briefing and train, the
  explore choices as lists; one bottom bar with Drive or Walk and Start.
  Narrow and wide views (over the menu at 3:2 and narrower, at most
  1450 dp wide) built the same day.

Later, not designed yet:
- **Options** in RmlUi: pages made from the settings registry (names,
  descriptions, choices are there already, translated), not written by
  hand for each setting.
- **Multiplayer:** a server's address and login, then joining a run's
  train (server tasks 06 and 07).
- **`--explore` with an activity or timetable file** (the user: later); it
  replaces `--play --file` (question 2).
- **Map layers in a run:** RmlUi toggles for them (the editor's are in its
  Qt Map menu); task 01 gives fixed defaults only.
- **The player's character** in a run: walking (the characters set, its
  own session and branch).
- **Gamepad** (`Screen::Action`).
- **RmlUi on its own thread** (scripting roadmap, Later): weighs more here,
  where the whole interface is RmlUi.
- **Loading a route off the GUI thread:** today the window freezes while a
  route loads (the editor's too).

## Questions and the user's answers (2026-11-20)
1. **One view with an Explore role** (recommended), or a new view?
   - **One view.** Map mode must work in explore mode too, probably with
     fewer layers on by default.
2. **Retire `--play` and `core.network.playerMode`** with task 01
   (recommended): explore mode does what they did. `--play --file` comes
   back as `--explore --file` (later).
   - **Yes.**
3. **Window defaults:** 1280 x 720 in a window, Fill screen off
   (recommended while the mode is young; games often fill the screen by
   default). The resolution counts physical pixels, as games do.
   - **Yes.**
4. **The Load window's Explore needs no route chosen;** a route chosen in
   its list is chosen first in the Explore window (recommended).
   - **Yes.**
5. **Remember the last choices** (route, run, explore choices, the
   timetable's train) in the profile's folder, `explore.json`, as the
   server keeps `users.json` there (recommended); or not at first.
   - **Yes** ("Hm, ok").
6. **The game menu pauses the run** while it is open (recommended; one
   player).
   - **Later.** The user is not sure: a remote session cannot be paused,
     and what the menu holds that would need a pause is not clear. Until
     then the game menu pauses nothing.
7. **Multiplayer and Options in task 01:** shown greyed until built
   (recommended), or hidden.
   - **Greyed.**
8. **The explore start time:** hours and minutes (recommended), or whole
   hours as Open Rails offers.
   - **Hours and minutes.**
9. **Timetable runs:** a season and weather to choose, as Open Rails, but
   no day (TSRE's timetable runs have no day rules) (recommended).
   - **A day to choose too.** No day rules, but the day sets the time of
     day's light and the weather. In TSRE that is the world clock's date
     (`WorldClock`, `yyyy-MM-dd`), which the sun, sky, lights, weather and
     fog follow. A run takes the session's date when its input has none
     (`SimulationController`). Task 02 offers a date.
10. **The intro route's archive** (task 03): downloaded the first time the
    explore mode window opens, as `procedural_examples` (recommended), or
    in the release ZIPs.
    - **Later.** The user is thinking of shipping one intro as a
      compressed archive in the appdata folder, as the places are: the
      tracked `appdata/0.7/geo/geo_cities_presets.zip`, unpacked with
      miniz into `assets/geo` the first time it is needed
      (`GeoPresetData.cpp`). The intro's would unpack into `assets/root`.

New from the answers (2026-11-20):

11. **The day and the season are tied** (task 02, "The day"): a run's first
    day is the clock's date, and an activity's season moves it into that
    season. Choose the day only, the season following from it at the
    route's latitude, for exploring and timetables (recommended)? And may
    an activity's day be chosen too, moved into its season as now
    (recommended: yes)?
    - **Yes to both** (the user, 2026-11-20).

Chosen without a question (say if not):
- Exit quits TSRE, also when started from the Load window; closing the
  window is Exit.
- ~~F11 and Alt+Enter switch Fill screen for the session, as in the
  editor.~~ **Not** (the user, 2026-11-20): those keys are the editor's,
  whose full screen is its own (the menu bar sliding in, its widgets
  working). In explore mode Fill screen is the setting alone.
- **The menu and its windows as tabs** (the user, 2026-11-20, in place of
  the menu dimmed behind a window): the main menu stays live beside the
  window it opened; task 02, "The window".
- With `core.network.clientLogin` set, `--explore` ignores it until
  Multiplayer.
