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
- [ ] `02-displays.md` (the user's, 2026-11-01; design): RmlUi documents
  drawn into textures on shapes, defined in the `.sd` (a texture replaced
  or a square anchored to a matrix): station displays first (clocks,
  departures and arrivals), the platform found from the nearest platform
  line, shared textures; then trains and cabs.
- [ ] `03-car-data.md` (the user's, 2026-11-02; go given): car numbers
  from the consists' UiDs, a timetable's `#tsre.cars` row overriding them
  (number and tags per vehicle); `train.cars` for Train displays, a `Car`
  input (a document per car), `Eng` reserved; the consist editor keeping
  UiDs, the timetable window's Cars table.

## Later (other tasks, to be laid out)
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
- Cab displays (part of task 02's later steps): an RmlUi context drawn
  into a texture on a 3D cab's screen part.
- Loco scripts on the simulation's Lua state (driving tasks, after the
  vehicle definitions).
- Advanced signal scripts in Lua (MSTS `sigscr` stays on its own
  interpreter).
- Game menus and dialogs in RmlUi; RmlUi's clip masks, filters and
  gradients when a document needs them.
