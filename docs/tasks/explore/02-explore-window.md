# Explore task 02 - The Explore window: choosing a run, and the run

Status 2026-11-20: built on `feature/explore` (the user's go: "Ok,
proceed with next step"), not merged; see "As built" at the end. Part of
the [explore set](00-task-roadmap.md); its questions and the user's
answers are there. Needs task 01.

## Goal
The main menu's Explore opens a large RmlUi window:
- the routes;
- for the route chosen, its runs: **exploring the route**, its
  **activities**, its **timetables**;
- for a timetable, its trains, **sortable and filterable**, to choose one;
- for exploring, a path, a consist, a start time, a season and the
  weather, made into an activity in memory;
- **Explore** (its button) loads the route and starts the run, the player's
  train driven from its start, the camera in its cab;
- **Esc in the run** opens the game menu: Resume, Main menu, Exit.

## The window
Over most of the room right of the main menu. Three columns:
1. **Routes:** their names (the TRK's `DisplayName`, else the folder),
   sorted.
2. **The route's runs:** "Explore the route" first, then the activities by
   name, then the timetable sets by name, under the headings Activities
   and Timetables. One list, so the keys walk it; Open Rails keeps the two
   kinds apart by a mode switch.
3. **Details and choices for the run chosen:**
   - **The route** (until a run is chosen): its picture (the TRK's
     `Graphic`, through `TexLib` as RmlUi images are) and description.
   - **Exploring:**
     - Start at, then Heading to: the route's player paths, grouped by
       their start, as Open Rails does;
     - Locomotive, then Consist: the game root's consists with an engine,
       those of the locomotive chosen;
     - Time: hours and minutes, 12:00 at first (question 8: yes);
     - Day, season and weather, as for a timetable (below).
   - **An activity:** its description and briefing (scrolled), start time,
     season, weather, duration, difficulty, the player's consist and path.
     Shown, not changed, as in Open Rails.
   - **A timetable set:** its trains in a table:
     - columns: Train (name), Starts (time), From and To, Category,
       Number, Line (the `#tsre` fields, when the set has them), Engine;
     - a click on a column's heading sorts by it, again the other way;
     - a filter field: text found in the name, number, or the stations;
     - "Drivable only", on at first: trains with an engine;
     - static trains never listed;
     - a day, season and weather to choose (question 9: a day too; see
       "The day" below).
- **At the bottom:** Explore (Enter). Greyed until the run is complete: a
  path and a consist for exploring, a train for a timetable.
- **Keys:** Left and Right between the columns, Up and Down in a list,
  Enter chooses. A gamepad later, through `Screen::Action`.

### The menu and its windows as tabs (the user, 2026-11-20)
The main menu stays live beside the window it opened, not dimmed:
- **The item of the window shown stays marked**, as a tab is. A click, or
  Enter, on another item shows its window in its place (Multiplayer and
  Options, later); the choices made in a window are kept while another is
  shown. Hovering only lights an item.
- **Keys:** Right or Enter on the marked item goes into its window; Left
  from the window's first column, or Esc anywhere in it, goes back to the
  item. Esc on the menu does nothing (task 01). No Back button.
- **One document:** the menu and its windows in `explore-menu.rml`, each
  window a panel shown one at a time (task 01's `explore-window.rml`
  folded in). RmlUi's arrow keys then cross between the menu and the
  window by themselves. Two documents would need the UI screen to pass
  the keys between them: a modal document keeps the focus to itself.
- **Data:** an RmlUi data model filled from C++ (as the HUD's); the lists'
  readers below.

## The day (question 9: a day to choose)
No day rules: the day sets the light of the time of day and the weather.
What TSRE has (checked 2026-11-20):
- **The world clock's date** (`WorldClock`, `yyyy-MM-dd`): the sun, sky,
  lights, weather and fog follow it. Its value at start is the setting
  `core.rendering.timeOfDay.date` (2026-06-21 by default).
- **A run's first day** is that date (`SimulationSession`: the run's
  options take `Game::timeOfDayDate`).
- **An MSTS activity's season moves the day into it** (`SeasonDate`): a
  date outside the season becomes the 15th of the season's middle month,
  the south's seasons half a year on.
- **Its weather:** an activity's rain or snow is used only by routes
  without weather of their own; a route with its own weather keeps it.

So the day and the season are tied: a season chosen apart from the day
would move the day. Decided (question 11, the user, 2026-11-20):
- **The day is chosen** (a date), the clock's date at first;
- **the season follows from it** at the route's latitude, shown beside it,
  not chosen apart;
- **the weather:** Clear, Rain, Snow, as an activity's header gives them;
- **the same for exploring and timetables;**
- **an activity's day can be chosen too;** the activity keeps its own
  season and weather, and its day is moved into its season as now.

The world clock's date is set to the day before the run starts.

## Reading the lists without a loaded route
Nothing goes into `ActLib`: it is filled when the route loads, and the
route switch clears it.
- **Routes:** each route's TRK when the window opens (`Trk`, the Load
  window's `checkRoute`). Pictures when a route is chosen (`TexLib`, in the
  background).
- **Activities:** `ACTIVITIES/*.act`, each read on its own
  (`Activity(path, name)` and `load()`), with its player service for the
  consist's and path's names. Activities of another route (its route ID)
  are left out.
- **Timetable sets:** `ACTIVITIES/OPENRAILS/*.timetable_or` and
  `*.timetablelist_or`, with the timetable reader
  (`Activity::loadTimetableSet`). The trains' cars, for the Engine column,
  by the light consist reader the simulation has
  (`Sim::TimetableTrainCars`).
- **Paths:** `PATHS/*.pat`: their start and end names, player paths only.
- **Consists:** the game root's `TRAINS/CONSISTS`, by `ConLib`'s simple
  list. It reads every consist, slow on large content, so it runs on a
  `QThreadPool` worker (never `std::thread` or `std::async`: Qt objects on
  MinGW). The lists fill in when it is done.
- **Kept for the session:** the routes and consists, not read again on
  every opening.

## Starting the run
1. **The route left,** if the view has one (the intro's, task 03; or a
   run's after Main menu): the route switch (editor task 10).
2. **The loading screen:** the route's loading image
   (`Trk::loadingScreenFile`: the TRK's `LoadingScreen`, else `load.ace`)
   over the window, with the route's name, in `loading.rml`. The route
   loads on the GUI thread, as in the editor, so one frame of it is drawn
   before the load starts. No progress at first.
3. **The route loads,** as the editor's does (`Route::load`).
4. **The run's activity:**
   - an activity: the route's own, loaded with it, found by file name;
   - a timetable: its set's activity, `playerTrain` the train chosen;
   - exploring: **a new activity in memory**, as the editor's New activity
     makes one (`ActLib::AddAct(..., true)`, `init`). Its player service is
     made from the path and consist, with the start time, season and
     weather; no traffic. Never saved: no file, and it is kept out of the
     lists of unsaved changes.
5. **The day, season and weather:** the world clock's date set to the day
   ("The day" above). The season and weather go into the run's activity
   header, where the run's input reads them now. The seasonal textures'
   season (`Game::season`) as the editor's choice of an activity sets it
   (to be checked in the task).
6. **The run:** `SimulationController::playDriving(activity)`: the
   player's train driven from its start, the camera in its cab (Drive
   follows). The HUD and the loco controls as in the editor.

## In the run
- **Esc:** the game menu, modal: Resume, Main menu, Exit (Options later).
  It pauses nothing for now (question 6: later). A server's run cannot be
  paused by one player anyway.
- **Main menu:** the run stopped (`SimulationController::stop`), the route
  left, the main menu over black (task 03: the intro route loads again).
- **Not here:** the editor's run grid (F4), speeds, run to a time, the
  Timetable window. What a player needs of them comes in later tasks, in
  RmlUi.

## Remembering the choices (question 5: yes)
The last route, run, explore choices (day included) and timetable train,
kept in the profile's folder (`explore.json`) and chosen first next time,
as Open Rails' `Menu_Selection`.

## Steps
1. The readers for the lists, without a loaded route.
2. The window's panel in the menu's document (tabs), its data model and
   keys.
3. The explore activity in memory.
4. Starting the run: the loading screen, the route, the activity, driving.
5. The game menu; back to the main menu.
6. The last choices kept.
7. Checks, documents.

## Checks
- **A suite, `explore-lists`,** run on content:
  - EUROPE1: its activities and their values; paths with the player flag;
    consists with an engine;
  - PRUDNIK: `PSK_2026`'s trains, the drivable ones, sorting, the filter.
- **A suite, `explore-start`,** through the window's data model (no
  clicks), each run with its train driven and the camera in its cab:
  - EUROPE1 `lclsrvce`;
  - a PRUDNIK timetable train;
  - exploring EUROPE1 on a player path;
  - a day chosen in winter: the run's first day is that date and the
    season winter.
- **Live, under Xvfb with openbox, keys only:** a route, an activity,
  Explore, Esc, Main menu, another run. PRUDNIK's 3D view is too slow on
  this machine: there only the lists and the start.
- Before a merge: `all` built, ctest, the app suites.

## As built (2026-11-20)
- **The lists** `Explore::Lists` (`src/explore/ExploreLists`): light
  readers of the blocks shown, not the editor's objects: an activity's
  parse reads its loose consists into the engine library, a path is a Qt
  object. The consists on a `QThreadPool` worker, kept for the session
  with the routes. On this machine (Release): EUROPE1's root 17 routes in
  4 ms, its 10 activities in 1 ms, 99 consists with an engine (of 139) in
  8 ms; PSK_2026's 296 trains in 18 ms.
  - A train with no stops (freight): From and To its path's start and end.
  - MSTS's `\n` in a TRK's or activity's texts shown as line breaks.
  - The route's latitude from its TRK alone (its projection, its start):
    `GeoWorldCoordinateConverter` got a virtual destructor for it.
- **The window** `Explore::Panel` (`src/explore/ExplorePanel`), the data
  model `explore` of `explore-menu.rml` (task 01's `explore-window.rml`
  folded in, as the tabs above say), `explore.rcss`.
  - Choices by a picker over the details (Start at, Heading to,
    Locomotive, Consist, the hour and the minutes in 5-minute steps, the
    day of the month and the month, the weather); Esc closes it.
  - The table: flex rows, each column a share; Category, Number and Line
    only when the set has them, and on views 16:10 and wider.
  - **Keys:** RmlUi's arrows stay inside a list (its navigation searches
    the nearest scroll container), so Left and Right across the columns
    (routes, runs, details, Explore) come from the panel; buttons side by
    side keep RmlUi's. Enter on a route goes on to its runs, on a run to
    its details (a timetable's: its trains, not the filter's text field);
    Right from the trains to the day under them.
  - Remembered in the profile's `explore.json`.
- **The run** `Explore::Run` (`src/explore/ExploreRun`), `loading.rml`,
  `explore-game.rml`.
  - The loading screen stays until the player's train is on the track,
    the seconds under the route's name ("Preparing the run: 12 s"):
    PSK_2026's first train (04:46) took about 3 minutes on this machine,
    the core's pre-run of the trains before it (as the editor's Drive).
  - The run's activity made the route's current one
    (`Route::activitySelected`, as the editor's choice): its trains drawn,
    a timetable's vehicles read ahead.
  - **The season (checked):** the editor's choice of an activity does not
    set `Game::season`; the run sets `core.startup.season` as a session
    value (SpringClear to WinterSnow, from its season and weather) before
    the route loads.
  - Exploring: the service named after its consist (the train's name on
    the map and in messages), no stops, no traffic; never saved.
  - Explore mode draws none of the editor's aids: the window turns the
    View menu's flags off while it is open (track database lines, the
    marks of interactive objects, grids, markers, track items) and back
    when it goes; an activity draws only its trains (no player's path
    line, events, zones or failed signals).
  - **The loco controls are shown in explore mode whatever the editor's
    HUD setting** (`core.interface.hud.enabled`, off by default). Chosen
    without a question: say if not.
  - The day: `WorldClock::setDate` before the run; an activity's moved
    into its season by the run, as before.
- **Checks:**
  - `explore-window` 41 (task 01's and the tabs: Enter, Right, Left, Esc,
    across the columns; two routes of only a TRK);
  - `explore-lists` on `--game-root`: EUROPE1 12, PRUDNIK 18 (the table
    through the model on an RmlUi context);
  - `explore-start` on `--game-root` and `--route` (none: nothing): EUROPE1
    30 (lclsrvce, then exploring Local Service on 15 January at 10:30 in
    snow: the first day that date, winter, WinterSnow), PRUDNIK 14 (a
    PSK_2026 train); each driven, the camera riding it, the game menu's
    Main menu back to the window, the run stopped, the route left;
  - `rmlui` 43, `settings` 306, `keys` 19, `editor-tools` 38, `map-view`
    48, `route-switch` 16, `driving` 81.
- **Live, Xvfb with openbox, keys only:** EUROPE1: the window, Local
  Service's details, exploring (the picker: Start at Carlisle, Heading to
  Dent), Explore, the loading screen, the run with the loco controls, the
  view behind the train (3), the map, Esc, Main menu, the window as it
  was. PRUDNIK: the table, a train chosen. Screens in the session's
  scratchpad (live/), not kept.
- **Not done here:** the Windows check of task 01; the app suites and
  ctest (before the merge).
