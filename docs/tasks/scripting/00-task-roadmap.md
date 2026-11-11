# Scripting Task Roadmap

This folder holds the tasks for Lua in TSRE and for RmlUi, the HTML/CSS-like
UI library scripted with Lua. Both are put in once and shared: the game UI,
cab displays, loco scripts and advanced signal scripts (the driving
roadmap's decisions of 2026-10-17 and 2026-10-24) all build on them.

## Execution Order
- [x] `01-lua-and-rmlui-foundations.md` (merged into main 2026-10-26: the
  Lua runtime, RmlUi, each renderer's UI drawer, the play HUD as a
  draggable document scaling with the view, images through TexLib, the
  input router with keyboard focus and actions for a gamepad): Lua 5.5 and
  RmlUi 6.3 vendored, no other new dependency (RmlUi's fonts through Qt,
  not FreeType); the Lua runtime (`src/tsre/script/`): one state per
  thread, a sandbox, time and memory limits, state tables saved and
  restored; RmlUi in TSRE (`src/tsre/ui/`): drawn by each renderer's own UI
  drawer over the finished frame, Lua in documents, data from the ridden
  train; first use: the play mode HUD as an RmlUi document.
- [x] `02-displays.md` (the user's, 2026-11-01; merged into main with task
  03, 2026-11-03): RmlUi documents drawn into textures on shapes, defined
  in the `.sd` (a texture replaced or a square anchored to a matrix):
  station and platform displays (clocks, departures and arrivals, the next
  trains), the platform found from the nearest platform line, shared
  textures; trains' displays; textures freed out of view; the user guide.
- [x] `03-car-data.md` (the user's, 2026-11-02; merged into main with task
  02, 2026-11-03): car numbers
  from the consists' UiDs, a timetable's `#tsre.cars` row overriding them
  (number and tags per vehicle); `train.cars` for Train displays, a `Car`
  input (a document per car), `Eng` reserved; the consist editor keeping
  UiDs (Renumber Cars), the timetable window's Cars table and tags picker;
  a platform's next trains (`trains`, the first two with their cars).
- [x] `04-ui-lua-memory.md` (a build.10 log, 2026-11-05): long drives
  still filled the UI's Lua state, without a leak: Lua's incremental
  collector fell behind the frames' short-lived userdata with finalizers.
  Fixed on `fix/ui-lua-memory` (generational collection, a 16 MB
  backstop), merged into main 2026-11-05 (`56b56f84`).
- [x] `05-module-load-deadline.md` (the user's report, 2026-11-11): after
  tasks maszyna 05 and 06 the ET22's script failed to load on CMK (nothing
  reacted): its 18 modules were read and compiled inside the load's 100 ms,
  and cold files on Windows took most of it. Fixed on
  `fix/lua-load-deadline`, merged into main 2026-11-11: `require`'s
  reading and compiling given back to the deadline, a failed module no
  longer "requires itself", each script's load time logged. Open: what a
  route without `MaxLineVoltage` gives a MaSzyna electric (CMK has one
  now); the Windows watchdog (driving task 02, open question 6).

## Later (other tasks, to be laid out)
- **The models' messages to the UI** (task 04's "Later"): a frame's
  messages as one list, `train.messages`, instead of an event each;
  `sound` triggers apart.
- **RmlUi on its own thread** (the user, 2026-11-02: an important future
  task, unavoidable as cab displays may become complex; the first
  releases keep RmlUi on the GUI thread, which renders). All of RmlUi
  moves together, as it is one global instance shared by the HUD, the UI
  drawers and the displays: a UI thread owns RmlUi and its Lua state,
  updates the documents and builds their draw lists; the render thread
  only uploads and draws finished lists (double-buffered in both
  renderers' UI drawers, which today hold the live list and its release
  queues). The documents' inputs as snapshots (`SimulationFrame` is one
  already); the HUD's mouse and keyboard posted to the UI thread (a frame
  later); TexLib's image checks made thread-safe. Loads, Lua and
  `ReleaseRenderManagers` then leave the frame. To be done with the
  shared render interface for displays (task 02's open items).
- **Fonts and documents' file access** (design 2026-11-03, in task 02's
  "Fonts and file access"): documents' paths kept to their folders, the
  include roots (`[[Trainset]]` ...) and `[[Route]]` (in the includes
  too), no `..`: built 2026-11-03 on `feature/file-access`, merged into
  main (c01e0d41), font files opened only by TSRE. On the user's
  go: kerning from `GPOS` (no text is kerned today); more Noto fonts
  shipped; content's fonts in `DISPLAYS/FONTS`, loaded by TSRE (not
  `@font-face`), TSRE's own staying in `appdata/<version>/ui/fonts`.
- Cab displays (a task of their own: task 02 left them out, with the CVF
  screens, ETCS and clicking on a display): an RmlUi context drawn into a
  texture on a 3D cab's screen part, `Input ( Eng )`'s data.
- Loco scripts on the simulation's Lua state (driving tasks, after the
  vehicle definitions).
- Advanced signal scripts in Lua (MSTS `sigscr` stays on its own
  interpreter).
- Game menus and dialogs in RmlUi; RmlUi's clip masks, filters and
  gradients when a document needs them.
